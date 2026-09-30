#pragma once

#include <llvm/ADT/StringRef.h>

#include <cstdint>
#include <string>
#include <vector>


namespace VCLG {

    /**
     * Where a compiled graph keeps what the host owns (`state-as-data.md` §6.2), produced by the
     * slot planner with the module: the host allocates the **state block** and the **UI block** with
     * these sizes and alignments, and binds every region's symbol to `state block + offset` before
     * the module is linked (`ExecutionSession::DefineSymbolPtr`).
     *
     * A region is identified by its key, built from the graph path of what it holds:
     * - a node's `State`: the node's path (`g1/n3`);
     * - an output kept by the host (it must persist, or it's observed): `<node path>.<output>`;
     * - a feedback value: `<Feedback Input path>#feedback`.
     *
     * Regions are placed in execution order, each aligned to at least the vector width. The UI
     * block is empty until inputs can be live (plan P9/P10); `Main` and `Reset` already take it.
     */
    struct GraphLayout {
        enum class RegionKind { State, Output, Feedback };

        struct Region {
            std::string key{};
            RegionKind kind = RegionKind::State;
            uint64_t offset = 0;
            uint64_t size = 0;
            uint64_t alignment = 1;
            /** The external symbol the root frame's code reaches the region through. */
            std::string symbol{};
        };

        struct Block {
            uint64_t size = 0;
            uint64_t alignment = 1;
        };

        Block state{};
        Block ui{};
        std::vector<Region> regions{};

        /** The region `key`, or null. */
        inline const Region* FindRegion(llvm::StringRef key) const {
            for (const Region& region : regions)
                if (region.key == key)
                    return &region;
            return nullptr;
        }

        static inline std::string StateKey(llvm::StringRef nodePath) { return nodePath.str(); }
        static inline std::string OutputKey(llvm::StringRef nodePath, llvm::StringRef output) { return nodePath.str() + "." + output.str(); }
        static inline std::string FeedbackKey(llvm::StringRef feedbackInputPath) { return feedbackInputPath.str() + "#feedback"; }
    };

}
