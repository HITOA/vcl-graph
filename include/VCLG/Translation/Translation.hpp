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
     * node's code, so it is part of the variant key. Storage lowering is the only step so
     * far, and always the first.
     */
    struct TranslationProfile {
        enum class Step {
            /** `State`, `self`, port parameters, rebuilt calls, `__Init` (§4.4). */
            StorageLowering
        };

        std::vector<Step> steps{ Step::StorageLowering };
    };

    /**
     * What a variant's code depends on (`state-as-data.md` §7.1): the node's source (name and hash),
     * its parameter values, AutoParameter substitutions, port type overrides, the logical width and
     * the translation profile. Not where anything is stored, nor the node's path: every instance
     * of a variant, in any storage context, shares its code. The imported libraries are checked
     * separately (see VariantCache).
     */
    struct VariantKey {
        /** The full description; two variants are the same when their texts are. */
        std::string text{};
        uint64_t hash = 0;

        /** Prefix of the variant's symbols: `v` and the hash in hex (§7.2). */
        std::string ManglingPrefix() const;
    };

    /** The key of `node`'s variant; nullopt, with an error reported, if its source isn't loaded. */
    std::optional<VariantKey> MakeVariantKey(VCL::CompilerContext& cc, const ElaboratedGraph::Node& node,
        const TranslationProfile& profile = {});

    /**
     * A library a variant was compiled against: the variant is only valid while the library's
     * source is the same (§7.3). Transitive imports are listed too (a library's constants and
     * templates can end up in the node's code), but only direct ones are linked with the graph.
     */
    struct LibraryDependency {
        /** The import's name (direct imports). */
        VCL::IdentifierInfo* name = nullptr;
        /** Buffer identifier of the library's source. */
        std::string source{};
        /** Start of the source's buffer: a reloaded source is a new buffer, even with the same text. */
        const char* buffer = nullptr;
        uint64_t hash = 0;
        bool direct = false;
    };

    /** A translated variant of a source node. */
    struct TranslatedNode {
        NodeInterface interface{};
        VariantKey key{};
        std::vector<LibraryDependency> libraries{};
        /**
         * Owns the translated AST, and the types it uses. Null when the variant was taken from the
         * cache (see VariantCache).
         */
        std::shared_ptr<VCL::CompilerInstance> instance{};
        /**
         * The translated module: the node's constants, `State`, the rewritten and synthesized
         * functions. Null when the variant was taken from the cache.
         */
        VCL::TranslationUnitDecl* translationUnit = nullptr;
    };

    /**
     * Translates an elaborated source node (`state-as-data.md` §4.1): parses it with what makes this
     * copy (parameters, substitutions, port type overrides), runs the lowering steps of `profile`,
     * emits the result into `module` (unoptimized, libraries not linked), reads the `State` layout
     * from the emitted types, and proves which outputs are always written.
     *
     * `module` receives only this variant: the proof runs on a copy of it. Symbols are prefixed with
     * the variant key's hash (VariantKey::ManglingPrefix), so every instance of the variant has the
     * same symbols. The rules of the node were checked when its definition was loaded. Reports
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
