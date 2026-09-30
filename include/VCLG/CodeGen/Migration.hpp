#pragma once

#include <VCLG/CodeGen/GraphLayout.hpp>

#include <cstdint>
#include <vector>


namespace VCLG {

    /**
     * How the state block of a recompiled graph takes over the state of the graph it replaces
     * (`state-as-data.md` §6.4, §6.5): every region of the old layout whose key and signature are
     * found unchanged in the new one is copied to its new place. Everything else keeps what the new
     * graph's `Reset` gave it.
     *
     * Computed off the audio thread; `Apply` only copies bytes, and is what the audio thread runs
     * when it adopts the new graph, before its first `Main`.
     */
    struct MigrationPlan {
        struct Copy {
            uint64_t from = 0;
            uint64_t to = 0;
            uint64_t size = 0;
        };

        /** In the new layout's order; regions adjacent in both blocks are merged into one copy. */
        std::vector<Copy> copies{};

        /** The number of bytes copied. */
        uint64_t Bytes() const;

        /** Copies from the old state block `from` into the new one `to`. */
        void Apply(const void* from, void* to) const;
    };

    /** The regions of `from` that `to` takes over. */
    MigrationPlan PlanMigration(const GraphLayout& from, const GraphLayout& to);

}
