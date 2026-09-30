#include <VCLG/CodeGen/CodeGenGraph.hpp>

#include <VCLG/Graph/Port.hpp>
#include <VCLG/Graph/Parameter.hpp>
#include <VCLG/Graph/Converter.hpp>
#include <VCLG/Translation/SourceNodeCompilation.hpp>
#include <VCLG/Core/Diagnostics.hpp>

#include "CodeGenFrame.hpp"
#include "ElaboratedDiagnosticScope.hpp"

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


VCLG::CodeGenGraph::CodeGenGraph(GraphContext& graphContext, GraphInstance& graph, llvm::Module& module, CodeGenGraphOptions options) :
        graphContext{ graphContext }, graph{ graph }, module{ module }, options{ std::move(options) },
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

VCLG::CodeGenGraph::~CodeGenGraph() = default;

const VCLG::NodeInterface* VCLG::CodeGenGraph::GetNodeInterface(llvm::StringRef path) const {
    for (NodeIndex index = 0; index < translatedNodes.size(); ++index)
        if (translatedNodes[index] && elaborated.GetNode(index).path == path)
            return &translatedNodes[index]->interface;
    return nullptr;
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
        VCLG::ElaboratedDiagnosticScope diagnosticScope{ elaborated, error->scope, error->node };
        return ReportGraphError(error->message);
    }
    if (options.mode == CodeGenGraphOptions::Mode::Planned)
        return EmitPlanned();

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
        VCLG::ElaboratedDiagnosticScope diagnosticScope{ elaborated, node.scope, index };
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

bool VCLG::CodeGenGraph::EmitPlanned() {
    // Offsets and sizes are computed with the data layout the JIT compiles for.
    module.setDataLayout(cc.GetTarget().GetTargetMachine()->createDataLayout());

    // 1. Every source node, translated (§4) into a module of its own, then linked in.
    if (!TranslateNodes())
        return false;

    // 2. The slot plan of the root frame (§5.3).
    VCL::ModuleTable noImports{};
    VCL::CodeGenModule types{ module, graphContext.GetGlobalASTContext(), cc.GetDiagnosticReporter(), cc.GetTarget(), noImports,
        cc.GetAttributeTable(), cc.GetIdentifierTable() };
    const llvm::DataLayout& layout = module.getDataLayout();
    std::string error{};
    NodeIndex errorNode = ElaboratedGraph::Invalid;
    plan = PlanSlots(elaborated, FrameKind::Root,
        [this](NodeIndex index) -> const NodeInterface* {
            return index < translatedNodes.size() && translatedNodes[index] ? &translatedNodes[index]->interface : nullptr;
        },
        [&](VCL::Type* type) -> std::optional<std::pair<uint64_t, uint64_t>> {
            llvm::Type* converted = type ? types.GetCGT().ConvertType(VCL::QualType{ type }) : nullptr;
            if (converted == nullptr)
                return std::nullopt;
            return std::make_pair((uint64_t)layout.getTypeAllocSize(converted), (uint64_t)layout.getABITypeAlign(converted).value());
        },
        cc.GetTarget().GetVectorWidthInByte(), options.planner, error, errorNode);
    if (!plan) {
        std::optional<ElaboratedDiagnosticScope> scope{};
        if (errorNode != ElaboratedGraph::Invalid)
            scope.emplace(elaborated, errorNode);
        return ReportGraphError(error);
    }

    // 3. The root frame: `Main(ui)` and `Reset(ui)` (§5.4-§5.7).
    CodeGenFrame frame{ graphContext, cc, module, elaborated, *plan, translatedNodes, options };
    if (!frame.EmitMain() || !frame.EmitReset())
        return false;

    // The entry points and input defaults are only used by the frames: internal, like any node
    // code, so that they disappear once inlined or folded.
    for (const std::optional<TranslatedNode>& node : translatedNodes) {
        if (!node)
            continue;
        for (const std::string* symbol : { &node->interface.process, &node->interface.reset, &node->interface.init })
            if (llvm::Function* function = symbol->empty() ? nullptr : module.getFunction(*symbol))
                function->setLinkage(llvm::GlobalValue::InternalLinkage);
        // An input default is only used by an unconnected input the graph gives no value of its
        // own (an aggregate initializer): the others go now, rather than in the optimizer.
        for (const NodeInterface::Port& port : node->interface.ports) {
            llvm::GlobalVariable* constant = port.defaultValue.empty() ? nullptr : module.getGlobalVariable(port.defaultValue);
            if (constant == nullptr)
                continue;
            if (constant->use_empty())
                constant->eraseFromParent();
            else
                constant->setLinkage(llvm::GlobalValue::InternalLinkage);
        }
    }
    return true;
}

bool VCLG::CodeGenGraph::TranslateNodes() {
    translatedNodes.clear();
    translatedNodes.resize(elaborated.GetNodes().size());
    llvm::Linker linker{ module };
    for (NodeIndex index : elaborated.GetExecutionOrder()) {
        const ElaboratedGraph::Node& node = elaborated.GetNode(index);
        if (node.kind != Node::NodeKind::Source)
            continue;
        // Everything reported while translating the node is attributed to it.
        ElaboratedDiagnosticScope diagnosticScope{ elaborated, index };
        auto nodeModule = std::make_unique<llvm::Module>(node.path, module.getContext());
        nodeModule->setDataLayout(module.getDataLayout());
        nodeModule->setTargetTriple(module.getTargetTriple());
        std::optional<TranslatedNode> translated = TranslateSourceNode(graphContext, cc, node, *nodeModule);
        if (!translated)
            return false;
        for (auto pair : translated->instance->GetImportModuleTable())
            aggregatedImportedModuleTable.Add(pair.first, pair.second);
        if (linker.linkInModule(std::move(nodeModule)))
            return ReportGraphError("failed to link the node's code into the graph");
        translatedNodes[index] = std::move(translated);
    }
    return true;
}

bool VCLG::CodeGenGraph::EmitSourceNode(NodeIndex index) {
    const ElaboratedGraph::Node& node = elaborated.GetNode(index);
    VCL::Source* source = cc.GetSourceManager().GetSourceFromName(node.source);
    SourceNodeDefinition* nodeDefinition = node.definition;
    if (!VCLG_CHECK(cc.GetDiagnosticReporter(), source != nullptr && nodeDefinition != nullptr))
        return false;

    SourceNodeCompilation compilation{ graphContext, cc, node, source, node.path };
    std::shared_ptr<VCL::CompilerInstance> instance = compilation.GetInstance();
    nodeCompilerInstances.push_back(instance);
    if (!compilation.Parse())
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
    cgm.SetOptions(instance->GetCompilerContext().GetInvocation()->GetCodeGenOptions());
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
        // The input is const for the node (ASTInputConstWriter), but not for the graph: a converter
        // writes into it before the node runs.
        variable->setConstant(false);

        if (!input.edge) {
            if (input.initializer)
                variable->setInitializer(VCLG::MakeScalarInitializer(cgm, *input.initializer, ElaboratedGraph::TypeOf(input), 
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
        variable->setInitializer(VCLG::MakeScalarInitializer(cgm, *initializer, type, cc.GetTarget().GetVectorWidthInElement()));
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
