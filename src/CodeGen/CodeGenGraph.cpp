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


VCLG::CodeGenGraph::CodeGenGraph(GraphContext& graphContext, GraphInstance& graph, llvm::Module& module, std::string manglingScope) :
        graphContext{ graphContext }, graph{ graph }, module{ module },
        manglingScope{ manglingScope.empty() ? "g" + std::to_string(graph.GetIdentity()) : std::move(manglingScope) },
        cc{ graphContext.GetCompilerContext().GetInvocation() },
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
    if (!graph.Validate())
        return false;

    entrypoint = std::make_unique<CodeGenEntrypoint>(*this, "Main");
    reset = std::make_unique<CodeGenEntrypoint>(*this, "Reset");
    entrypoint->Begin();
    reset->Begin();

    BuildPortMap();
    std::vector<Node*> nodes{};
    if (!BuildOrderedNodeList(nodes)) {
        cc.GetDiagnosticReporter().Error(VCL::Diagnostic::InternalError)
            .SetCompilerInfo(__FILE__, __func__, __LINE__)
            .Report();
        return false;
    }

    for (Node* node : nodes) {
        if (SourceNode* sourceNode = llvm::dyn_cast<SourceNode>(node)) {
            if (!EmitSourceNode(sourceNode))
                return false;
        } else if (!EmitBuiltinNode(llvm::cast<BuiltinNode>(node))) {
            return false;
        }
    }

    reset->End();
    entrypoint->End();
    return true;
}

bool VCLG::CodeGenGraph::EmitSourceNode(SourceNode* node) {
    VCL::Source* source = cc.GetSourceManager().GetSourceFromName(node->GetSource());
    assert(source != nullptr);

    SourceNodeDefinition* nodeDefinition = graphContext.GetDefinitionRegistry().GetOrCreateSourceNodeDefinition(source);

    std::shared_ptr<VCL::CompilerInstance> instance = cc.CreateInstance();
    nodeCompilerInstances.push_back(instance);
    
    instance->SetManglingPrefix(GetNodeManglingPrefix(node));
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

bool VCLG::CodeGenGraph::EmitBuiltinNode(BuiltinNode* node) {
    return node->Emit(*this);
}

VCLG::Port* VCLG::CodeGenGraph::GetInPortToOutPort(Port* inPort) {
    if (inPortToOutPort.count(inPort))
        return inPortToOutPort[inPort];
    return nullptr;
}

llvm::GlobalVariable* VCLG::CodeGenGraph::GetOutPortGlobalVar(Port* port) {
    if (outPortGlobalVar.count(port))
        return outPortGlobalVar[port];
    return nullptr;
}

void VCLG::CodeGenGraph::AddOutPortGlobalVar(Port* port, llvm::GlobalVariable* var) {
    outPortGlobalVar.insert({ port, var });
}

void VCLG::CodeGenGraph::ImportSubgraph(CodeGenGraph& codegen) {
    for (auto instance : codegen.nodeCompilerInstances)
        nodeCompilerInstances.push_back(instance);

    for (auto module : codegen.aggregatedImportedModuleTable)
        aggregatedImportedModuleTable.Add(module.first, module.second);
}

void VCLG::CodeGenGraph::BuildPortMap() {
    inPortToOutPort.clear();

    for (const Connection& connection : graph.GetConnections()) {
        Port* inPort = graph.GetPortByIdentity(connection.GetInputPortIdentity());
        Port* outPort = graph.GetPortByIdentity(connection.GetOutputPortIdentity());
        inPortToOutPort.insert({ inPort, outPort });
    }
}

bool VCLG::CodeGenGraph::BuildOrderedNodeList(std::vector<Node*>& nodes) {
    // Roots are visited last-to-first, which keeps their relative order the same as before.
    llvm::SmallVector<Node*> roots{};
    for (Node* node : graph.GetNodes())
        if (node->HasFlag(Node::NodeFlag::IsOutputNode))
            roots.push_back(node);
    std::reverse(roots.begin(), roots.end());

    // Unreachable in practice: GraphInstance refuses connections that would close a cycle.
    return graph.BuildExecutionOrder(roots, nodes);
}

std::string VCLG::CodeGenGraph::GetNodeManglingPrefix(Node* node) const {
    return manglingScope + "/n" + std::to_string(node->GetIdentity());
}
