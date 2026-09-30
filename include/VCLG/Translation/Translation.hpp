#pragma once

#include <VCLG/Graph/Elaboration.hpp>
#include <VCLG/Graph/GraphContext.hpp>
#include <VCLG/Translation/NodeInterface.hpp>

#include <VCL/AST/Decl.hpp>
#include <VCL/Core/Diagnostic.hpp>
#include <VCL/Frontend/CompilerContext.hpp>
#include <VCL/Frontend/CompilerInstance.hpp>

#include <llvm/IR/Module.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>


namespace VCLG {

    /**
     * Which lowering steps the translation runs, in order (`state-as-data.md` §4.7). It changes the
     * node's code, so it will be part of the variant key (P6). Storage lowering is the only step so
     * far, and always the first.
     */
    struct TranslationProfile {
        enum class Step {
            /** `State`, `self`, port parameters, rebuilt calls, `__Init` (§4.4). */
            StorageLowering
        };

        std::vector<Step> steps{ Step::StorageLowering };
    };

    /** A translated variant of a source node. */
    struct TranslatedNode {
        NodeInterface interface{};
        /**
         * Owns the translated AST, and the types it uses; its import table lists the libraries the
         * module needs linked (see LinkLibraries).
         */
        std::shared_ptr<VCL::CompilerInstance> instance{};
        /** The translated module: the node's constants, `State`, the rewritten and synthesized functions. */
        VCL::TranslationUnitDecl* translationUnit = nullptr;
    };

    /**
     * Translates an elaborated source node (`state-as-data.md` §4.1): parses it with what makes this
     * copy (parameters, substitutions, port type overrides), runs the lowering steps of `profile`,
     * emits the result into `module` (unoptimized, libraries not linked), reads the `State` layout
     * from the emitted types, and proves which outputs are always written.
     *
     * `module` receives only this variant: the proof runs on a copy of it. Symbols are prefixed with
     * the node's path. The rules of the node were checked when its definition was loaded. Reports
     * and returns nullopt on failure.
     */
    std::optional<TranslatedNode> TranslateSourceNode(GraphContext& graphContext, VCL::CompilerContext& cc,
        const ElaboratedGraph::Node& node, llvm::Module& module, const TranslationProfile& profile = {});

    /** Links into `module` a copy of every library `instance` imported. */
    bool LinkLibraries(llvm::Module& module, VCL::CompilerInstance& instance, VCL::DiagnosticReporter& reporter);

    /**
     * The declarations of a translated module, one per line: constants, `State` and its fields, and
     * each function's signature with its parameters' codegen flags. Bodies aren't printed.
     */
    std::string PrintTranslation(const TranslatedNode& node);

}
