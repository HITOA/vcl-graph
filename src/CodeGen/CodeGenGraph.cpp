#include <VCLG/CodeGen/CodeGenGraph.hpp>

#include <VCLG/Graph/Port.hpp>
#include <VCLG/Graph/Parameter.hpp>
#include <VCLG/AST/ASTParameterWriter.hpp>
#include <VCLG/AST/ASTAutoParameterSubstitution.hpp>
#include <VCLG/AST/ASTPortTypeOverrideWriter.hpp>

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
#include <llvm/IR/Verifier.h>

#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <iostream>


VCLG::CodeGenGraph::CodeGenGraph(GraphContext& graphContext, GraphInstance& graph, llvm::Module& module) :
        graphContext{ graphContext }, graph{ graph }, module{ module }, cc{ graphContext.GetCompilerContext().GetInvocation() },
        aggregatedImportedModuleTable{}, nodeCompilerInstances{}, inPortToOutPort{}, outPortGlobalVar{} {
    
    cc.CopyDiagnosticEngine(graphContext.GetCompilerContext());
    cc.CopySourceManager(graphContext.GetCompilerContext());
    cc.CopyIdentifierTable(graphContext.GetCompilerContext());
    cc.CopyAttributeTable(graphContext.GetCompilerContext());
    cc.CopyDirectiveRegistry(graphContext.GetCompilerContext());
    cc.CopyTarget(graphContext.GetCompilerContext());
    cc.CopyTypeCache(graphContext.GetCompilerContext());
    cc.CopyModuleCache(graphContext.GetCompilerContext());
    cc.CreateLLVMContext();
}

bool VCLG::CodeGenGraph::LinkNow() {
    llvm::Linker linker{ module };

    for (auto mod : aggregatedImportedModuleTable) {
        std::unique_ptr<llvm::Module> clonedModule = mod.second->GetModule().withModuleDo([this](llvm::Module& module){
            return llvm::CloneModule(module);
        });
        if (linker.linkInModule(std::move(clonedModule))) {
            cc.GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
                .SetCompilerInfo(__FILE__, __func__, __LINE__)
                .Report();
            return false;
        }
    }

    if (llvm::verifyModule(module, &llvm::errs())) {
        cc.GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
            .SetCompilerInfo(__FILE__, __func__, __LINE__)
            .Report();
        return false;
    }

    return true;
}

bool VCLG::CodeGenGraph::Emit() {
    entrypoint = std::make_unique<CodeGenEntrypoint>(*this, "Main");
    reset = std::make_unique<CodeGenEntrypoint>(*this, "Reset");
    entrypoint->Begin();
    reset->Begin();

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

    reset->End();
    entrypoint->End();
    return true;
}

bool VCLG::CodeGenGraph::EmitSourceNode(SourceNode* node) {
    VCL::Source* source = cc.GetSourceManager().LoadFromMemory("", node->GetSource());
    SourceNodeDefinition* nodeDefinition = graphContext.GetDefinitionRegistry().GetOrCreateSourceNodeDefinition(source);

    std::shared_ptr<VCL::CompilerInstance> instance = cc.CreateInstance();
    nodeCompilerInstances.push_back(instance);
    
    instance->CreateASTContext();
    instance->CreateExportSymbolTable();
    instance->CreateImportModuleTable();
    instance->CreateDefineTable();

    VCL::Lexer lexer{ source->GetBufferRef(), 
        instance->GetCompilerContext().GetDiagnosticReporter(), 
        instance->GetCompilerContext().GetIdentifierTable() };
    VCL::TokenStream stream{ lexer };
    VCL::Sema sema{ 
        instance->GetCompilerContext(),
        instance->GetASTContext(),
        instance->GetCompilerContext().GetDiagnosticReporter(),
        instance->GetCompilerContext().GetIdentifierTable(),
        instance->GetCompilerContext().GetDirectiveRegistry(),
        instance->GetExportSymbolTable(),
        instance->GetImportModuleTable(),
        instance->GetDefineTable() };
    VCL::Parser parser{ stream, sema, instance->GetCompilerContext().GetAttributeTable() };

    ASTParameterWriter parameterWriter{ 
        instance->GetASTContext(),
        instance->GetCompilerContext().GetIdentifierTable(), 
        nodeDefinition->GetParameters(), node->GetParameters() };

    ASTAutoParameterSubstitution autoParameterWriter{
        sema,
        instance->GetCompilerContext().GetIdentifierTable(),
        node->GetSubstitutionTable(), 
        nodeDefinition->GetAutoParameters() };

    ASTPortTypeOverrideWriter portWriter{ 
        instance->GetASTContext(),
        instance->GetCompilerContext().GetIdentifierTable(), 
        nodeDefinition->GetPorts(), node->GetInputs() };

    VCL::MultiplexerASTConsumer astConsumer{};
    astConsumer.PushConsumer(&parameterWriter);
    astConsumer.PushConsumer(&autoParameterWriter);
    astConsumer.PushConsumer(&portWriter);

    parser.SetASTConsumer(&astConsumer);
    
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
    if (!cgm.Emit(false))
        return false;

    for (size_t i = 0; i < node->GetInputs().size(); ++i) {
        Port* inPort = node->GetInputs()[i];

        SourcePortDefinition* inPortDefinition = nodeDefinition->GetPorts()[i];
        std::optional<std::string> mangledName = instance->GetMangledSymbolName(inPortDefinition->GetName());
        if (!mangledName.has_value()) {
            cc.GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
                .SetCompilerInfo(__FILE__, __func__, __LINE__)
                .Report();
            return false;
        }
        llvm::GlobalVariable* variable = module.getGlobalVariable(mangledName.value(), true);

        if (!inPortToOutPort.count(inPort)) {
            if (inPort->GetInitializerOverride() != nullptr) {
                VCL::Type* type = VCL::Type::GetCanonicalType(inPort->GetType());
                llvm::Constant* value = cgm.GenerateConstantValue(inPort->GetInitializerOverride());
                if (type->GetTypeClass() == VCL::Type::VectorTypeClass)
                    value = llvm::ConstantDataVector::getSplat(cc.GetTarget().GetVectorWidthInElement(), value);
                variable->setInitializer(value);
            }
            continue;
        }

        Port* connectedPort = inPortToOutPort[inPort];
        if (!outPortGlobalVar.count(connectedPort)) {
            cc.GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
                .SetCompilerInfo(__FILE__, __func__, __LINE__)
                .Report();
            return false;
        }
        llvm::GlobalVariable* connectedVariable = outPortGlobalVar[connectedPort];

        if (!variable) {
            cc.GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
                .SetCompilerInfo(__FILE__, __func__, __LINE__)
                .Report();
            return false;
        }

        Connection* conn = graph.FindConnectionByPort(connectedPort, inPort);
        if (!conn) {
            cc.GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
                .SetCompilerInfo(__FILE__, __func__, __LINE__)
                .Report();
            return false;
        }

        if (conn->GetConverter() == nullptr) {
            variable->setInitializer(connectedVariable->getInitializer());
            variable->replaceAllUsesWith(connectedVariable);
            variable->eraseFromParent();
        } else {
            if (!conn->GetConverter()->Emit(entrypoint->GetIRBuilder(), connectedPort, inPort, connectedVariable, variable))
                return false;
        }
    }

    for (size_t i = 0; i < node->GetOutputs().size(); ++i) {
        Port* outPort = node->GetOutputs()[i];
        SourcePortDefinition* outPortDefinition = nodeDefinition->GetPorts()[i + node->GetInputs().size()];
        std::optional<std::string> mangledName = instance->GetMangledSymbolName(outPortDefinition->GetName());
        if (!mangledName.has_value()) {
            cc.GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
                .SetCompilerInfo(__FILE__, __func__, __LINE__)
                .Report();
            return false;
        }

        llvm::GlobalVariable* variable = module.getGlobalVariable(mangledName.value(), true);
        if (!variable) {
            cc.GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
                .SetCompilerInfo(__FILE__, __func__, __LINE__)
                .Report();
            return false;
        }
        outPortGlobalVar.insert({ outPort, variable });
    }

    // Add node process function to the entrypoint
    std::optional<std::string> mangledEntrypointName = instance->GetMangledSymbolName(nodeDefinition->GetEntrypoint()->GetIdentifierInfo()->GetName());
    if (!mangledEntrypointName.has_value()) {
        cc.GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
            .SetCompilerInfo(__FILE__, __func__, __LINE__)
            .Report();
        return false;
    }
    llvm::Function* processFunction = module.getFunction(mangledEntrypointName.value());
    if (!processFunction) {
        cc.GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
            .SetCompilerInfo(__FILE__, __func__, __LINE__)
            .Report();
        return false;
    }

    if (nodeDefinition->GetReset() != nullptr) {
        std::optional<std::string> mangledResetEntrypointName = instance->GetMangledSymbolName(nodeDefinition->GetReset()->GetIdentifierInfo()->GetName());
        if (!mangledResetEntrypointName.has_value()) {
            cc.GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
                .SetCompilerInfo(__FILE__, __func__, __LINE__)
                .Report();
            return false;
        }
        llvm::Function* resetFunction = module.getFunction(mangledResetEntrypointName.value());
        if (!resetFunction) {
            cc.GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
                .SetCompilerInfo(__FILE__, __func__, __LINE__)
                .Report();
            return false;
        }

        if (!reset->AddNodeEntrypoint(resetFunction))
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

        for (Port* inPort : Node::GetNodeInputs(currentNode)) {
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