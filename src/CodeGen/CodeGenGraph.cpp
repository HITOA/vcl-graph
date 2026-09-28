#include <VCLG/CodeGen/CodeGenGraph.hpp>

#include <VCLG/Graph/Port.hpp>
#include <VCLG/Graph/Parameter.hpp>
#include <VCLG/Graph/Converter.hpp>
#include <VCLG/AST/ASTParameterWriter.hpp>
#include <VCLG/AST/ASTAutoParameterSubstitution.hpp>
#include <VCLG/AST/ASTPortTypeOverrideWriter.hpp>
#include <VCLG/Core/Diagnostics.hpp>

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
#include <llvm/Support/raw_ostream.h>

#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <iostream>


namespace {

    // Opens the diagnostic scopes of the subgraph uses enclosing `scope` (outermost first), then
    // the scope of `node` itself, so that what's reported meanwhile is attributed to that node.
    class ElaboratedDiagnosticScope {
    public:
        ElaboratedDiagnosticScope(const VCLG::ElaboratedGraph& graph, VCLG::ElaboratedGraph::ScopeIndex scope,
                VCLG::ElaboratedGraph::NodeIndex node) {
            llvm::SmallVector<VCLG::ElaboratedGraph::ScopeIndex, 4> chain{};
            for (VCLG::ElaboratedGraph::ScopeIndex s = scope; s != 0 && s != VCLG::ElaboratedGraph::Invalid;
                    s = graph.GetScopes()[s].parent)
                chain.push_back(s);
            for (auto it = chain.rbegin(); it != chain.rend(); ++it)
                scopes.push_back(std::make_unique<VCLG::NodeDiagnosticScope>(
                    graph.GetScopes()[*it].path, graph.GetScopes()[*it].displayName));
            if (node != VCLG::ElaboratedGraph::Invalid)
                scopes.push_back(std::make_unique<VCLG::NodeDiagnosticScope>(
                    graph.GetNode(node).path, graph.GetNode(node).displayName));
        }

        ~ElaboratedDiagnosticScope() {
            // Innermost first: each scope restores its parent.
            while (!scopes.empty())
                scopes.pop_back();
        }

    private:
        std::vector<std::unique_ptr<VCLG::NodeDiagnosticScope>> scopes{};
    };

    // The initial value of a global of `type` given as a scalar: splat across a vector, or across
    // each lane.
    llvm::Constant* MakeInitializer(VCL::CodeGenModule& cgm, VCL::ConstantScalar value, VCL::Type* type, uint32_t width) {
        llvm::Constant* constant = cgm.GenerateConstantValue(&value);
        type = VCL::Type::GetCanonicalType(type);
        if (type->GetTypeClass() == VCL::Type::VectorTypeClass)
            return llvm::ConstantDataVector::getSplat(width, constant);
        if (type->GetTypeClass() == VCL::Type::LanesTypeClass) {
            llvm::SmallVector<llvm::Constant*> elements{};
            elements.assign(width, constant);
            return llvm::ConstantArray::get(llvm::ArrayType::get(constant->getType(), width), elements);
        }
        return constant;
    }

}

VCLG::CodeGenGraph::CodeGenGraph(GraphContext& graphContext, GraphInstance& graph, llvm::Module& module) :
        graphContext{ graphContext }, graph{ graph }, module{ module },
        cc{ graphContext.GetCompilerContext().GetInvocation() },
        aggregatedImportedModuleTable{}, elaborated{}, nodeCompilerInstances{}, outputGlobals{}, feedbackGlobals{} {
    
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
            cc.GetDiagnosticReporter().Error(VCL::Diagnostic::CustomDiagnostic,
                    "failed to link imported module `" + mod.first->GetName().str() + "`")
                .SetCompilerInfo(__FILE__, __func__, __LINE__)
                .Report();
            return false;
        }
    }

    std::string verifierOutput{};
    llvm::raw_string_ostream verifierStream{ verifierOutput };
    if (llvm::verifyModule(module, &verifierStream)) {
        cc.GetDiagnosticReporter().Error(VCL::Diagnostic::CustomDiagnostic,
                "generated graph code is invalid (LLVM verifier): " + verifierStream.str())
            .SetCompilerInfo(__FILE__, __func__, __LINE__)
            .Report();
        return false;
    }

    return true;
}

bool VCLG::CodeGenGraph::Emit() {
    elaborated = Elaborate(graph);
    if (const std::optional<ElaboratedGraph::Error>& error = elaborated.GetError()) {
        ElaboratedDiagnosticScope diagnosticScope{ elaborated, error->scope, error->node };
        return ReportGraphError(error->message);
    }

    entrypoint = std::make_unique<CodeGenEntrypoint>(*this, "Main");
    reset = std::make_unique<CodeGenEntrypoint>(*this, "Reset");
    entrypoint->Begin();
    reset->Begin();

    outputGlobals.clear();
    outputGlobals.resize(elaborated.GetNodes().size());
    for (NodeIndex index = 0; index < elaborated.GetNodes().size(); ++index)
        outputGlobals[index].assign(elaborated.GetNode(index).outputs.size(), nullptr);
    feedbackGlobals.clear();

    for (NodeIndex index : elaborated.GetExecutionOrder()) {
        const ElaboratedGraph::Node& node = elaborated.GetNode(index);
        // Everything reported while emitting the node (parse, Sema, codegen, converters) is
        // attributed to it, inside the subgraph uses enclosing it.
        ElaboratedDiagnosticScope diagnosticScope{ elaborated, node.scope, index };
        bool emitted = false;
        switch (node.kind) {
            case Node::NodeKind::Source: emitted = EmitSourceNode(index); break;
            case Node::NodeKind::SubgraphInput: emitted = EmitSubgraphInputNode(index); break;
            case Node::NodeKind::SubgraphOutput: emitted = EmitSubgraphOutputNode(index); break;
            case Node::NodeKind::FeedbackInput: emitted = EmitFeedbackInputNode(index); break;
            case Node::NodeKind::FeedbackOutput: emitted = EmitFeedbackOutputNode(index); break;
            case Node::NodeKind::Subgraph:
                emitted = VCLG_CHECK(cc.GetDiagnosticReporter(), !"subgraph uses are flattened by elaboration");
                break;
        }
        if (!emitted)
            return false;
    }

    reset->End();
    entrypoint->End();
    return true;
}

bool VCLG::CodeGenGraph::EmitSourceNode(NodeIndex index) {
    const ElaboratedGraph::Node& node = elaborated.GetNode(index);
    VCL::Source* source = cc.GetSourceManager().GetSourceFromName(node.source);
    SourceNodeDefinition* nodeDefinition = node.definition;
    if (!VCLG_CHECK(cc.GetDiagnosticReporter(), source != nullptr && nodeDefinition != nullptr))
        return false;

    std::shared_ptr<VCL::CompilerInstance> instance = cc.CreateInstance();
    nodeCompilerInstances.push_back(instance);
    
    instance->SetManglingPrefix(node.path);
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
        nodeDefinition->GetParameters(), node.parameters };

    ASTAutoParameterSubstitution autoParameterWriter{
        sema,
        instance->GetCompilerContext().GetIdentifierTable(),
        node.substitutions, 
        nodeDefinition->GetAutoParameters() };

    // Concrete inputs whose type differs in this copy (e.g. promoted by a converter).
    llvm::SmallVector<VCL::Type*, 4> portTypeOverrides{};
    for (const ElaboratedGraph::Input& input : node.inputs)
        portTypeOverrides.push_back(!input.isDependent && input.type != input.declaredType ? input.type : nullptr);

    ASTPortTypeOverrideWriter portWriter{ 
        instance->GetASTContext(),
        instance->GetCompilerContext().GetIdentifierTable(), 
        nodeDefinition->GetPorts(), portTypeOverrides };

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

    for (uint32_t i = 0; i < node.inputs.size(); ++i) {
        const ElaboratedGraph::Input& input = node.inputs[i];

        const SourcePortDefinition& inPortDefinition = nodeDefinition->GetPorts()[i];
        std::optional<std::string> mangledName = instance->GetMangledSymbolName(inPortDefinition.GetName());
        if (!VCLG_CHECK(cc.GetDiagnosticReporter(), mangledName.has_value()))
            return false;
        llvm::GlobalVariable* variable = module.getGlobalVariable(mangledName.value(), true);
        if (!VCLG_CHECK(cc.GetDiagnosticReporter(), variable != nullptr))
            return false;

        if (!input.edge) {
            if (input.initializer)
                variable->setInitializer(MakeInitializer(cgm, *input.initializer, ElaboratedGraph::TypeOf(input), 
                    cc.GetTarget().GetVectorWidthInElement()));
            continue;
        }

        llvm::GlobalVariable* connectedVariable = GetSourceGlobal(*input.edge);
        if (!VCLG_CHECK(cc.GetDiagnosticReporter(), connectedVariable != nullptr))
            return false;

        if (input.edge->converter == nullptr) {
            variable->replaceAllUsesWith(connectedVariable);
            variable->eraseFromParent();
        } else {
            VCL::Type* sourceType = ElaboratedGraph::TypeOf(elaborated.GetNode(input.edge->node).outputs[input.edge->output]);
            if (!input.edge->converter->Emit(entrypoint->GetIRBuilder(), sourceType, ElaboratedGraph::TypeOf(input), 
                    connectedVariable, variable))
                return false;
        }
    }

    for (uint32_t i = 0; i < node.outputs.size(); ++i) {
        const SourcePortDefinition& outPortDefinition = nodeDefinition->GetPorts()[i + node.inputs.size()];
        std::optional<std::string> mangledName = instance->GetMangledSymbolName(outPortDefinition.GetName());
        if (!VCLG_CHECK(cc.GetDiagnosticReporter(), mangledName.has_value()))
            return false;

        llvm::GlobalVariable* variable = module.getGlobalVariable(mangledName.value(), true);
        if (!VCLG_CHECK(cc.GetDiagnosticReporter(), variable != nullptr))
            return false;
        outputGlobals[index][i] = variable;
    }

    // Add node process function to the entrypoint
    std::optional<std::string> mangledEntrypointName = instance->GetMangledSymbolName(nodeDefinition->GetEntrypoint()->GetIdentifierInfo()->GetName());
    if (!VCLG_CHECK(cc.GetDiagnosticReporter(), mangledEntrypointName.has_value()))
        return false;
    llvm::Function* processFunction = module.getFunction(mangledEntrypointName.value());
    if (!VCLG_CHECK(cc.GetDiagnosticReporter(), processFunction != nullptr))
        return false;

    if (nodeDefinition->GetReset() != nullptr) {
        std::optional<std::string> mangledResetEntrypointName = instance->GetMangledSymbolName(nodeDefinition->GetReset()->GetIdentifierInfo()->GetName());
        if (!VCLG_CHECK(cc.GetDiagnosticReporter(), mangledResetEntrypointName.has_value()))
            return false;
        llvm::Function* resetFunction = module.getFunction(mangledResetEntrypointName.value());
        if (!VCLG_CHECK(cc.GetDiagnosticReporter(), resetFunction != nullptr))
            return false;

        if (!reset->AddNodeEntrypoint(resetFunction))
            return false;
    }

    return entrypoint->AddNodeEntrypoint(processFunction);
}

bool VCLG::CodeGenGraph::EmitSubgraphInputNode(NodeIndex index) {
    const ElaboratedGraph::Node& node = elaborated.GetNode(index);
    const ElaboratedGraph::Input& input = node.inputs[0];
    // Given a plain connection, the subgraph reads the producer's value directly.
    if (input.edge && input.edge->converter == nullptr) {
        outputGlobals[index][0] = GetSourceGlobal(*input.edge);
        return VCLG_CHECK(cc.GetDiagnosticReporter(), outputGlobals[index][0] != nullptr);
    }
    outputGlobals[index][0] = EmitInputGlobal(index, 0, node.displayName);
    return outputGlobals[index][0] != nullptr;
}

bool VCLG::CodeGenGraph::EmitSubgraphOutputNode(NodeIndex index) {
    const ElaboratedGraph::Node& node = elaborated.GetNode(index);
    const ElaboratedGraph::Input& input = node.inputs[0];
    if (!input.edge)
        return ReportGraphError("subgraph output is not connected");
    if (input.edge->converter == nullptr) {
        outputGlobals[index][0] = GetSourceGlobal(*input.edge);
        return VCLG_CHECK(cc.GetDiagnosticReporter(), outputGlobals[index][0] != nullptr);
    }
    outputGlobals[index][0] = EmitInputGlobal(index, 0, node.displayName);
    return outputGlobals[index][0] != nullptr;
}

bool VCLG::CodeGenGraph::EmitFeedbackOutputNode(NodeIndex index) {
    const ElaboratedGraph::Node& node = elaborated.GetNode(index);
    if (node.feedbackInput == ElaboratedGraph::Invalid)
        return ReportGraphError("feedback output is not linked to a feedback input");
    if (!VCLG_CHECK(cc.GetDiagnosticReporter(), node.outputs.size() == 1))
        return false;

    // The first reader declares the global holding the feedback value; the Feedback Input, emitted
    // after its readers, replaces it with what feeds it.
    llvm::GlobalVariable*& variable = feedbackGlobals[node.feedbackInput];
    if (variable == nullptr) {
        const ElaboratedGraph::Node& feedbackInput = elaborated.GetNode(node.feedbackInput);
        variable = EmitGlobal(node.path, feedbackInput.displayName, ElaboratedGraph::TypeOf(node.outputs[0]), std::nullopt);
        if (!variable)
            return false;
    }
    outputGlobals[index][0] = variable;
    return true;
}

bool VCLG::CodeGenGraph::EmitFeedbackInputNode(NodeIndex index) {
    const ElaboratedGraph::Node& node = elaborated.GetNode(index);
    const ElaboratedGraph::Input& input = node.inputs[0];
    // Declared by the Feedback Output(s) reading this feedback, which the execution order emits first.
    llvm::GlobalVariable* variable = feedbackGlobals.lookup(index);
    if (variable == nullptr)
        return ReportGraphError("feedback is never read: no feedback output is linked to it");
    if (!input.edge)
        return ReportGraphError("feedback input is not connected");
    llvm::GlobalVariable* connectedVariable = GetSourceGlobal(*input.edge);
    if (!VCLG_CHECK(cc.GetDiagnosticReporter(), connectedVariable != nullptr))
        return false;

    if (input.edge->converter != nullptr) {
        VCL::Type* sourceType = ElaboratedGraph::TypeOf(elaborated.GetNode(input.edge->node).outputs[input.edge->output]);
        return input.edge->converter->Emit(entrypoint->GetIRBuilder(), sourceType, ElaboratedGraph::TypeOf(input),
            connectedVariable, variable);
    }

    variable->replaceAllUsesWith(connectedVariable);
    variable->eraseFromParent();
    feedbackGlobals[index] = connectedVariable;
    // Nodes reading a Feedback Output can still be emitted after this one.
    for (NodeIndex reader = 0; reader < elaborated.GetNodes().size(); ++reader)
        if (elaborated.GetNode(reader).feedbackInput == index && !outputGlobals[reader].empty())
            outputGlobals[reader][0] = connectedVariable;
    return true;
}

llvm::GlobalVariable* VCLG::CodeGenGraph::EmitInputGlobal(NodeIndex index, uint32_t inputIndex, llvm::StringRef name) {
    const ElaboratedGraph::Node& node = elaborated.GetNode(index);
    const ElaboratedGraph::Input& input = node.inputs[inputIndex];
    VCL::Type* type = ElaboratedGraph::TypeOf(input);
    llvm::GlobalVariable* variable = EmitGlobal(node.path, name, type, input.edge ? std::nullopt : input.initializer);
    if (!variable || !input.edge)
        return variable;

    llvm::GlobalVariable* connectedVariable = GetSourceGlobal(*input.edge);
    if (!VCLG_CHECK(cc.GetDiagnosticReporter(), connectedVariable != nullptr))
        return nullptr;
    VCL::Type* sourceType = ElaboratedGraph::TypeOf(elaborated.GetNode(input.edge->node).outputs[input.edge->output]);
    if (!input.edge->converter->Emit(entrypoint->GetIRBuilder(), sourceType, type, connectedVariable, variable))
        return nullptr;
    return variable;
}

llvm::GlobalVariable* VCLG::CodeGenGraph::EmitGlobal(const std::string& path, llvm::StringRef name, VCL::Type* type,
        const std::optional<VCL::ConstantScalar>& initializer) {
    std::shared_ptr<VCL::CompilerInstance> instance = cc.CreateInstance();
    nodeCompilerInstances.push_back(instance);
    
    instance->SetManglingPrefix(path);
    instance->CreateASTContext();
    instance->CreateExportSymbolTable();
    instance->CreateImportModuleTable();
    instance->CreateDefineTable();

    VCL::Sema sema{ 
        instance->GetCompilerContext(),
        instance->GetASTContext(),
        instance->GetCompilerContext().GetDiagnosticReporter(),
        instance->GetCompilerContext().GetIdentifierTable(),
        instance->GetCompilerContext().GetDirectiveRegistry(),
        instance->GetExportSymbolTable(),
        instance->GetImportModuleTable(),
        instance->GetDefineTable() };
    
    VCL::IdentifierInfo* identifier = instance->GetCompilerContext().GetIdentifierTable().Get(name);
    VCL::VarDecl* varDecl = sema.ActOnVarDecl(type, identifier, VCL::VarDecl::VarAttrBitfield{ 0 }, nullptr, VCL::SourceRange{});
    if (!varDecl)
        return nullptr;
    
    VCL::CodeGenModule cgm{
        module, 
        instance->GetASTContext(), 
        instance->GetCompilerContext().GetDiagnosticReporter(),
        instance->GetCompilerContext().GetTarget(),
        instance->GetImportModuleTable(),
        instance->GetCompilerContext().GetAttributeTable(),
        instance->GetCompilerContext().GetIdentifierTable() };
    if (!cgm.EmitGlobalVarDecl(varDecl))
        return nullptr;

    std::optional<std::string> mangledName = instance->GetMangledSymbolName(identifier->GetName());
    if (!VCLG_CHECK(cc.GetDiagnosticReporter(), mangledName.has_value()))
        return nullptr;
    llvm::GlobalVariable* variable = module.getGlobalVariable(mangledName.value(), true);
    if (!VCLG_CHECK(cc.GetDiagnosticReporter(), variable != nullptr))
        return nullptr;

    if (initializer)
        variable->setInitializer(MakeInitializer(cgm, *initializer, type, cc.GetTarget().GetVectorWidthInElement()));
    return variable;
}

llvm::GlobalVariable* VCLG::CodeGenGraph::GetSourceGlobal(const ElaboratedGraph::Edge& edge) {
    return outputGlobals[edge.node][edge.output];
}

bool VCLG::CodeGenGraph::ReportGraphError(const std::string& message) {
    // A mistake in the graph the user can fix. The node is identified by the diagnostic scope open
    // around it.
    cc.GetDiagnosticReporter().Error(VCL::Diagnostic::CustomDiagnostic, message)
        .SetCompilerInfo(__FILE__, __func__, __LINE__)
        .Report();
    return false;
}
