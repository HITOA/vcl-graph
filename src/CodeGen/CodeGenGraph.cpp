#include <VCLG/CodeGen/CodeGenGraph.hpp>

#include <VCLG/Core/Diagnostics.hpp>

#include "CodeGenFrame.hpp"
#include "ElaboratedDiagnosticScope.hpp"

#include <VCL/CodeGen/CodeGenModule.hpp>

#include <llvm/Linker/Linker.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>


VCLG::CodeGenGraph::CodeGenGraph(GraphContext& graphContext, GraphInstance& graph, llvm::Module& module, CodeGenGraphOptions options) :
        graphContext{ graphContext }, graph{ graph }, module{ module }, options{ std::move(options) },
        cc{ graphContext.GetCompilerContext().GetInvocation() }, aggregatedImportedModuleTable{}, elaborated{} {
    
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
        ElaboratedDiagnosticScope diagnosticScope{ elaborated, error->scope, error->node };
        return ReportGraphError(error->message);
    }

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

bool VCLG::CodeGenGraph::ReportGraphError(const std::string& message) {
    // A mistake in the graph the user can fix. The node is identified by the diagnostic scope open
    // around it.
    cc.GetDiagnosticReporter().Error(VCL::Diagnostic::CustomDiagnostic, message)
        .SetCompilerInfo(__FILE__, __func__, __LINE__)
        .Report();
    return false;
}
