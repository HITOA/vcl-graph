#include <VCLG/CodeGen/CodeGenGraph.hpp>

#include <VCLG/Core/Diagnostics.hpp>
#include <VCLG/Translation/VariantCache.hpp>

#include "CodeGenFrame.hpp"
#include "ElaboratedDiagnosticScope.hpp"

#include <VCL/CodeGen/CodeGenModule.hpp>
#include <VCL/Core/SourceManager.hpp>
#include <VCL/Frontend/CompilerInvocation.hpp>
#include <VCL/Frontend/ModuleCache.hpp>

#include <llvm/ADT/StringMap.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>


namespace {

    // The context's invocation, reporting to `recorder` (which passes everything on to the
    // context's consumer): the compile's own diagnostic engine.
    std::shared_ptr<VCL::CompilerInvocation> RecordingInvocation(VCL::CompilerInvocation& original, VCL::DiagnosticConsumer* recorder) {
        auto invocation = std::make_shared<VCL::CompilerInvocation>();
        invocation->GetDiagnosticOptions() = original.GetDiagnosticOptions();
        invocation->GetTargetOptions() = original.GetTargetOptions();
        invocation->GetCodeGenOptions() = original.GetCodeGenOptions();
        invocation->GetDiagnosticOptions().SetDiagnosticConsumer(recorder);
        return invocation;
    }

    // Renames the variant's symbols in `module` and `translated` from its key's prefix to `prefix`:
    // one instance's own copy of the variant.
    void RenameVariant(llvm::Module& module, VCLG::TranslatedNode& translated, const std::string& prefix) {
        std::string from = translated.key.ManglingPrefix() + ".";
        auto rename = [&](std::string& symbol) {
            if (llvm::StringRef{ symbol }.starts_with(from))
                symbol = prefix + "." + symbol.substr(from.size());
        };
        for (llvm::GlobalValue& value : module.global_values()) {
            std::string name = value.getName().str();
            if (!value.isDeclaration() && llvm::StringRef{ name }.starts_with(from)) {
                rename(name);
                value.setName(name);
            }
        }
        VCLG::NodeInterface& interface = translated.interface;
        for (std::string* symbol : { &interface.process, &interface.reset, &interface.init })
            rename(*symbol);
        for (VCLG::NodeInterface::Port& port : interface.ports)
            rename(port.defaultValue);
    }

    void Replay(VCL::CompilerContext& cc, std::vector<VCL::Diagnostic>& diagnostics) {
        for (VCL::Diagnostic& diagnostic : diagnostics)
            cc.GetDiagnosticsEngine().Diagnose(VCLG::CopyDiagnostic(diagnostic));
    }

}

VCLG::CodeGenGraph::CodeGenGraph(GraphContext& graphContext, GraphInstance& graph, llvm::Module& module, CodeGenGraphOptions options) :
        graphContext{ graphContext }, graph{ graph }, module{ module }, options{ std::move(options) },
        recorder{ graphContext.GetCompilerContext().GetInvocation()->GetDiagnosticOptions().GetDiagnosticConsumer() },
        cc{ RecordingInvocation(*graphContext.GetCompilerContext().GetInvocation(), &recorder) }, aggregatedImportedModuleTable{}, elaborated{} {

    cc.CreateDiagnosticEngine();
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
    VariantCache& cache = graphContext.GetVariantCache();
    std::unique_lock<std::mutex> lock{ cache.GetMutex(), std::defer_lock };
    if (options.useVariantCache)
        lock.lock();
    bool perInstance = options.codeSharing == CodeGenGraphOptions::CodeSharing::PerInstance;

    // Each variant is translated once per compile, and what its translation reported is reported
    // again at each of its instances.
    struct Variant {
        /** Under the variant's names; nullopt when it failed. */
        std::optional<TranslatedNode> translated;
        std::vector<VCL::Diagnostic> diagnostics;
        /** PerInstance: the variant's module, cloned for each instance. */
        std::unique_ptr<llvm::Module> module;
    };
    llvm::StringMap<Variant> variants{};
    llvm::Linker linker{ module };
    bool succeeded = true;

    // Links the code of the instance `index`: PerVariant, the variant's only copy; PerInstance, a
    // copy of its own, under the instance's names.
    auto link = [&](NodeIndex index, Variant& variant, std::unique_ptr<llvm::Module> nodeModule) {
        TranslatedNode translated = *variant.translated;
        translated.interface.definition = elaborated.GetNode(index).definition;
        if (perInstance)
            RenameVariant(*nodeModule, translated, elaborated.GetNode(index).path);
        for (const LibraryDependency& library : translated.libraries) {
            VCL::Source* source = library.direct ? cc.GetSourceManager().GetSourceFromName(library.source) : nullptr;
            if (VCL::Module* libraryModule = source ? cc.GetModuleCache().Get(source) : nullptr)
                aggregatedImportedModuleTable.Add(library.name, libraryModule);
        }
        translatedNodes[index] = std::move(translated);
        return !linker.linkInModule(std::move(nodeModule)) || ReportGraphError("failed to link the node's code into the graph");
    };

    for (NodeIndex index : elaborated.GetExecutionOrder()) {
        const ElaboratedGraph::Node& node = elaborated.GetNode(index);
        if (node.kind != Node::NodeKind::Source)
            continue;
        // Everything reported while translating the node is attributed to it.
        ElaboratedDiagnosticScope diagnosticScope{ elaborated, index };
        std::optional<VariantKey> key = MakeVariantKey(cc, node);
        if (!key) {
            succeeded = false;
            continue;
        }

        // Another instance of a variant this compile already has.
        auto seen = variants.find(key->text);
        if (seen != variants.end()) {
            Variant& variant = seen->second;
            Replay(cc, variant.diagnostics);
            if (!variant.translated) {
                succeeded = false;
            } else if (perInstance) {
                if (!link(index, variant, llvm::CloneModule(*variant.module)))
                    return false;
            } else {
                translatedNodes[index] = *variant.translated;
                translatedNodes[index]->interface.definition = node.definition;
            }
            continue;
        }
        Variant& variant = variants[key->text];

        std::unique_ptr<llvm::Module> nodeModule{};
        if (VariantCache::Entry* entry = options.useVariantCache ? cache.Find(*key, cc, module.getContext()) : nullptr) {
            // Compiled by an earlier compile.
            for (VCL::Diagnostic& diagnostic : entry->diagnostics)
                variant.diagnostics.push_back(CopyDiagnostic(diagnostic));
            Replay(cc, variant.diagnostics);
            variant.translated = entry->node;
            nodeModule = llvm::CloneModule(*entry->module);
            ++cache.GetStatistics().reused;
        } else {
            nodeModule = std::make_unique<llvm::Module>(key->ManglingPrefix(), module.getContext());
            nodeModule->setDataLayout(module.getDataLayout());
            nodeModule->setTargetTriple(module.getTargetTriple());
            recorder.Start();
            variant.translated = TranslateSourceNode(graphContext, cc, node, *nodeModule);
            variant.diagnostics = recorder.Stop();
            if (!variant.translated) {
                succeeded = false;
                continue;
            }
            if (options.useVariantCache) {
                std::vector<VCL::Diagnostic> diagnostics{};
                for (VCL::Diagnostic& diagnostic : variant.diagnostics)
                    diagnostics.push_back(CopyDiagnostic(diagnostic));
                cache.Insert(*variant.translated, *nodeModule, std::move(diagnostics), *cc.GetSourceManager().GetSourceFromName(node.source));
                ++cache.GetStatistics().translated;
            }
        }
        if (perInstance)
            variant.module = llvm::CloneModule(*nodeModule);
        if (!link(index, variant, std::move(nodeModule)))
            return false;
    }

    if (options.useVariantCache)
        cache.EndCompile(cc);
    return succeeded;
}

bool VCLG::CodeGenGraph::ReportGraphError(const std::string& message) {
    // A mistake in the graph the user can fix. The node is identified by the diagnostic scope open
    // around it.
    cc.GetDiagnosticReporter().Error(VCL::Diagnostic::CustomDiagnostic, message)
        .SetCompilerInfo(__FILE__, __func__, __LINE__)
        .Report();
    return false;
}
