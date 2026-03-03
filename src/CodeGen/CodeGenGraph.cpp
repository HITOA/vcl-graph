#include <VCLG/CodeGen/CodeGenGraph.hpp>

#include <VCLG/Graph/Port.hpp>

#include <VCL/Core/SourceManager.hpp>
#include <VCL/Frontend/CompilerInstance.hpp>
#include <VCL/Core/Source.hpp>
#include <VCL/Lex/Lexer.hpp>
#include <VCL/Lex/TokenStream.hpp>
#include <VCL/Sema/Sema.hpp>
#include <VCL/Parse/Parser.hpp>
#include <VCL/CodeGen/CodeGenModule.hpp>
#include <VCL/CodeGen/Optimizer.hpp>

#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Transforms/Utils/Cloning.h>

#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <iostream>


VCLG::CodeGenGraph::CodeGenGraph(GraphContext& graphContext, GraphInstance& graph, llvm::Module& module) :
        graphContext{ graphContext }, graph{ graph }, module{ module }, 
        aggregatedImportedModuleTable{}, nodeCompilerInstances{}, inPortToOutPort{}, outPortGlobalVar{} {
    
}

bool VCLG::CodeGenGraph::LinkNow() {
    llvm::Linker linker{ module };

    for (auto mod : aggregatedImportedModuleTable) {
        std::unique_ptr<llvm::Module> clonedModule = mod.second->GetModule().withModuleDo([this](llvm::Module& module){
            return llvm::CloneModule(module);
        });
        if (linker.linkInModule(std::move(clonedModule))) {
            graphContext.GetCompilerContext().GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
                .SetCompilerInfo(__FILE__, __func__, __LINE__)
                .Report();
            return false;
        }
    }

    return true;
}

bool VCLG::CodeGenGraph::Emit() {
    entrypoint = std::make_unique<CodeGenEntrypoint>(*this);
    entrypoint->Begin();

    BuildPortMap();
    std::vector<Node*> nodes = BuildOrderedNodeList();

    for (Node* node : nodes) {
        switch (node->GetNodeClass()) {
            case Node::SourceNodeClass:
                if (!EmitSourceNode((SourceNode*)node))
                    return false;
                break;
            default:
                break;
        }
    }

    entrypoint->End();
    return true;
}

bool VCLG::CodeGenGraph::EmitSourceNode(SourceNode* node) {
    VCL::Source* source = graphContext.GetCompilerContext().GetSourceManager().LoadFromMemory("", node->GetSource());
    SourceNodeDefinition* nodeDefinition = graphContext.GetDefinitionRegistry().GetOrCreateSourceNodeDefinition(source);

    std::shared_ptr<VCL::CompilerInstance> instance = graphContext.GetCompilerContext().CreateInstance();
    nodeCompilerInstances.push_back(instance);
    
    instance->CreateASTContext();
    instance->CreateExportSymbolTable();
    instance->CreateImportModuleTable();
    instance->CreateDefineTable();

    VCL::Lexer lexer{ source->GetBufferRef(), 
        instance->GetCompilerContext().GetDiagnosticReporter(), 
        instance->GetCompilerContext().GetIdentifierTable() };
    VCL::TokenStream stream{ lexer };
    VCL::Sema sema{ instance->GetASTContext(),
        instance->GetCompilerContext().GetDiagnosticReporter(),
        instance->GetCompilerContext().GetIdentifierTable(),
        instance->GetCompilerContext().GetDirectiveRegistry(),
        instance->GetExportSymbolTable(),
        instance->GetImportModuleTable(),
        instance->GetDefineTable() };
    VCL::Parser parser{ stream, sema, instance->GetCompilerContext().GetAttributeTable() };
    
    if (!parser.Parse())
        return false;
    
    for (auto pair : instance->GetImportModuleTable())
        aggregatedImportedModuleTable.Add(pair.first, pair.second);
    
    VCL::CodeGenModule cgm{
        module, 
        instance->GetASTContext(), 
        instance->GetCompilerContext().GetDiagnosticReporter(),
        instance->GetCompilerContext().GetTarget(),
        instance->GetImportModuleTable(),
        instance->GetCompilerContext().GetAttributeTable(),
        instance->GetCompilerContext().GetIdentifierTable() };
    if (!cgm.Emit())
        return false;

    for (size_t i = 0; i < node->GetInputs().size(); ++i) {
        Port* inPort = node->GetInputs()[i];
        if (!inPortToOutPort.count(inPort))
            continue;
        Port* connectedPort = inPortToOutPort[inPort];
        if (!outPortGlobalVar.count(connectedPort)) {
            graphContext.GetCompilerContext().GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
                .SetCompilerInfo(__FILE__, __func__, __LINE__)
                .Report();
            return false;
        }
        llvm::GlobalVariable* connectedVariable = outPortGlobalVar[connectedPort];

        SourcePortDefinition* inPortDefinition = nodeDefinition->GetPorts()[i];
        std::optional<std::string> mangledName = instance->GetMangledSymbolName(inPortDefinition->GetName());
        if (!mangledName.has_value()) {
            graphContext.GetCompilerContext().GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
                .SetCompilerInfo(__FILE__, __func__, __LINE__)
                .Report();
            return false;
        }
        llvm::GlobalVariable* variable = module.getGlobalVariable(mangledName.value(), true);
        if (!variable) {
            graphContext.GetCompilerContext().GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
                .SetCompilerInfo(__FILE__, __func__, __LINE__)
                .Report();
            return false;
        }
        
        variable->setInitializer(connectedVariable->getInitializer());
        variable->replaceAllUsesWith(connectedVariable);
        variable->eraseFromParent();
    }

    for (size_t i = 0; i < node->GetOutputs().size(); ++i) {
        Port* outPort = node->GetOutputs()[i];
        SourcePortDefinition* outPortDefinition = nodeDefinition->GetPorts()[i + node->GetInputs().size()];
        std::optional<std::string> mangledName = instance->GetMangledSymbolName(outPortDefinition->GetName());
        if (!mangledName.has_value()) {
            graphContext.GetCompilerContext().GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
                .SetCompilerInfo(__FILE__, __func__, __LINE__)
                .Report();
            return false;
        }

        llvm::GlobalVariable* variable = module.getGlobalVariable(mangledName.value(), true);
        if (!variable) {
            graphContext.GetCompilerContext().GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
                .SetCompilerInfo(__FILE__, __func__, __LINE__)
                .Report();
            return false;
        }
        outPortGlobalVar.insert({ outPort, variable });
    }

    // Add node process function to the entrypoint
    std::optional<std::string> mangledEntrypointName = instance->GetMangledSymbolName(nodeDefinition->GetEntrypoint()->GetIdentifierInfo()->GetName());
    if (!mangledEntrypointName.has_value()) {
        graphContext.GetCompilerContext().GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
            .SetCompilerInfo(__FILE__, __func__, __LINE__)
            .Report();
        return false;
    }
    llvm::Function* processFunction = module.getFunction(mangledEntrypointName.value());
    if (!processFunction) {
        graphContext.GetCompilerContext().GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
            .SetCompilerInfo(__FILE__, __func__, __LINE__)
            .Report();
        return false;
    }
    return entrypoint->AddNodeEntrypoint(processFunction);
}

void VCLG::CodeGenGraph::BuildPortMap() {
    inPortToOutPort.clear();

    for (const Connection& connection : graph.GetConnections()) {
        Port* inPort = graph.GetPortByIdentity(connection.GetInputPortIdentity());
        Port* outPort = graph.GetPortByIdentity(connection.GetOutputPortIdentity());
        inPortToOutPort.insert({ inPort, outPort });
    }
}

std::vector<VCLG::Node*> VCLG::CodeGenGraph::BuildOrderedNodeList() {
    // For now, I assume the graph is acyclic

    std::vector<Node*> nodes{};

    std::unordered_set<Node*> visitedNodes{};
    std::queue<Node*> nodeToVisite{};

    for (Node* node : graph.GetNodes())
        if (node->HasFlag(Node::NodeFlag::IsOutputNode))
            nodeToVisite.push(node);
    
    while (!nodeToVisite.empty()) {
        Node* currentNode = nodeToVisite.front();
        nodeToVisite.pop();

        if (visitedNodes.count(currentNode)) {
            nodes.erase(std::remove(nodes.begin(), nodes.end(), currentNode), nodes.end());
            nodes.push_back(currentNode);
        } else {
            nodes.push_back(currentNode);
            visitedNodes.insert(currentNode);
        }

        for (Port* inPort : GetNodeInputs(currentNode)) {
            if (!inPortToOutPort.count(inPort))
                continue;
            Port* connectedPort = inPortToOutPort[inPort];
            Node* connectedNode = graph.GetNodeByIdentity(connectedPort->GetOwner());
            nodeToVisite.push(connectedNode);
        }
    }

    std::reverse(nodes.begin(), nodes.end());
    return std::move(nodes);
}

llvm::ArrayRef<VCLG::Port*> VCLG::CodeGenGraph::GetNodeInputs(Node* node) {
    switch (node->GetNodeClass()) {
        case Node::SourceNodeClass:
            return ((SourceNode*)node)->GetInputs();
        default:
            return {};
    }
}

llvm::ArrayRef<VCLG::Port*> VCLG::CodeGenGraph::GetNodeOutputs(Node* node) {
    switch (node->GetNodeClass()) {
        case Node::SourceNodeClass:
            return ((SourceNode*)node)->GetOutputs();
        default:
            return {};
    }
}
