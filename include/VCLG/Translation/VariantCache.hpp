#pragma once

#include <VCLG/Translation/Translation.hpp>

#include <VCL/Core/Diagnostic.hpp>
#include <VCL/Frontend/CompilerContext.hpp>

#include <llvm/ADT/StringMap.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>


namespace VCLG {

    /**
     * The compiled variants of source nodes, kept between compiles (`state-as-data.md` §7.3), by
     * variant key. An entry holds the variant's unoptimized module (libraries not linked), its
     * NodeInterface with the always-written proof, and what its translation reported, replayed at
     * every instance using it.
     *
     * An entry is used only while the sources it was compiled from are the ones loaded: the node's
     * source is in the key, and each library it imports (transitively) must still be the same
     * buffer with the same hash. Entries that no longer are, or that no compile used for
     * `maxIdleCompiles` compiles, are dropped at the end of a compile.
     *
     * Failed variants aren't kept: a failure can depend on what the key doesn't see (a library
     * that didn't import), so they are translated again on the next compile. Within one compile,
     * a variant is translated once and its errors reported at each instance (CodeGenGraph).
     *
     * Owned by the GraphContext. Modules are kept in the LLVM context they were compiled in; a
     * compile into another context translates again.
     */
    class VariantCache {
    public:
        struct Entry {
            /** The interface's `definition` is the one current when the entry was made: use the node's. */
            TranslatedNode node{};
            std::unique_ptr<llvm::Module> module{};
            std::vector<VCL::Diagnostic> diagnostics{};
            /** The node's source, and its buffer (diagnostics point into it). */
            std::string source{};
            const char* buffer = nullptr;
            uint64_t lastUsed = 0;
        };

        struct Statistics {
            /** Variants translated, and taken from the cache, since the cache was made. */
            uint64_t translated = 0;
            uint64_t reused = 0;
        };

    public:
        VariantCache() = default;
        VariantCache(const VariantCache& other) = delete;
        VariantCache(VariantCache&& other) = delete;
        ~VariantCache() = default;

        VariantCache& operator=(const VariantCache& other) = delete;
        VariantCache& operator=(VariantCache&& other) = delete;

        /** Held by a compile while it uses the cache. */
        inline std::mutex& GetMutex() { return mutex; }

        /**
         * The entry of `key`, if it is still valid for the sources loaded in `cc` and was compiled
         * in `context`; null otherwise.
         */
        Entry* Find(const VariantKey& key, VCL::CompilerContext& cc, llvm::LLVMContext& context);

        /** Stores a copy of a translated variant of the node source `source`, and of its module. */
        void Insert(const TranslatedNode& node, const llvm::Module& module, std::vector<VCL::Diagnostic> diagnostics,
            VCL::Source& source);

        /** Ends a compile: drops the entries no longer valid, or unused for `maxIdleCompiles` compiles. */
        void EndCompile(VCL::CompilerContext& cc);

        inline void Clear() { entries.clear(); }
        inline size_t Size() const { return entries.size(); }
        inline const Statistics& GetStatistics() const { return statistics; }
        inline Statistics& GetStatistics() { return statistics; }

        uint64_t maxIdleCompiles = 32;

    private:
        bool IsValid(const Entry& entry, VCL::CompilerContext& cc) const;

    private:
        std::mutex mutex{};
        llvm::StringMap<std::unique_ptr<Entry>> entries{};
        uint64_t compile = 0;
        Statistics statistics{};
    };

}
