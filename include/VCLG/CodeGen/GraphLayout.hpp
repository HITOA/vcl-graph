#pragma once

#include <VCLG/Core/ValueFormat.hpp>

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
     * - a feedback value: `<Feedback Input path>#feedback`;
     * - a probe, the copy of an exposed input fed by a temporary: `<node path>.<input>#probe`.
     *
     * The key says which node a region belongs to; its **signature** says whether the region's
     * bytes still mean the same thing (§6.3). A recompiled graph takes over the regions of the
     * previous one whose key and signature are unchanged (`PlanMigration`).
     *
     * Regions are placed in execution order, each aligned to at least the vector width. The **UI
     * block** holds the inputs in the live set (`SlotPlannerOptions::liveInputs`), packed, each
     * aligned to its type: `Main` and `Reset` read it, the host writes it between calls. The
     * module's `InitUI(ui)` fills it with the values the graph gives the inputs.
     *
     * `exposed` lists, per node instance, the variables its UI may read (`[Expose]`, §3.4) and
     * where the host finds them (§6.2).
     */
    struct GraphLayout {
        enum class RegionKind { State, Output, Feedback, Probe };

        struct Region {
            std::string key{};
            RegionKind kind = RegionKind::State;
            uint64_t offset = 0;
            uint64_t size = 0;
            uint64_t alignment = 1;
            /** The external symbol the root frame's code reaches the region through. */
            std::string symbol{};
            /**
             * Hash of what gives the bytes their meaning: the generations of the graphs the node
             * belongs to (§6.1), the node's source (state and outputs), and the layout of what the
             * region holds. Equal keys and signatures: the old bytes are valid in the new graph.
             */
            uint64_t signature = 0;
        };

        struct Block {
            uint64_t size = 0;
            uint64_t alignment = 1;
        };

        /** A live input's value in the UI block. */
        struct UiEntry {
            /** `<node path>.<input>` (InputKey). */
            std::string key{};
            uint64_t offset = 0;
            uint64_t size = 0;
            uint64_t alignment = 1;
        };

        /** A variable a node's UI may read, and maybe write (§6.2). */
        struct Exposed {
            enum class Mode { Read, Write };
            enum class Location {
                /** In the state block, at `offset`: state, an output, a connected input's source or probe. */
                State,
                /** In the UI block, at `offset`: an input in the live set. */
                UI,
                /** An unconnected input folded into a constant: the module's constant `symbol`, read-only. */
                Constant
            };

            /** The node's path. */
            std::string node{};
            /** The variable's name in the node's source. */
            std::string name{};
            /** Its type in this compile, printed, and how its bytes read. */
            std::string type{};
            ValueFormat format{};
            uint64_t size = 0;
            Mode mode = Mode::Read;
            Location location = Location::State;
            uint64_t offset = 0;
            std::string symbol{};
            /**
             * Whether the host may write it now: `[Expose(Write)]`, and in host memory the node only
             * reads (state, or an input in the live set). False for an output, a connected input and
             * an input folded into a constant (writing that one needs it live: a recompile).
             */
            bool writable = false;
            /** Inputs: connected to another node's output (writes are refused while connected, §3.4). */
            bool connected = false;
        };

        Block state{};
        Block ui{};
        std::vector<Region> regions{};
        std::vector<UiEntry> uiEntries{};
        std::vector<Exposed> exposed{};

        /** The region `key`, or null. */
        inline const Region* FindRegion(llvm::StringRef key) const {
            for (const Region& region : regions)
                if (region.key == key)
                    return &region;
            return nullptr;
        }

        /** The UI block entry of the input `key`, or null. */
        inline const UiEntry* FindUiEntry(llvm::StringRef key) const {
            for (const UiEntry& entry : uiEntries)
                if (entry.key == key)
                    return &entry;
            return nullptr;
        }

        /** The exposed variable `name` of the node at `node`, or null. */
        inline const Exposed* FindExposed(llvm::StringRef node, llvm::StringRef name) const {
            for (const Exposed& entry : exposed)
                if (entry.node == node && entry.name == name)
                    return &entry;
            return nullptr;
        }

        static inline std::string StateKey(llvm::StringRef nodePath) { return nodePath.str(); }
        static inline std::string OutputKey(llvm::StringRef nodePath, llvm::StringRef output) { return nodePath.str() + "." + output.str(); }
        static inline std::string FeedbackKey(llvm::StringRef feedbackInputPath) { return feedbackInputPath.str() + "#feedback"; }
        static inline std::string InputKey(llvm::StringRef nodePath, llvm::StringRef input) { return nodePath.str() + "." + input.str(); }
        static inline std::string ProbeKey(llvm::StringRef nodePath, llvm::StringRef input) { return InputKey(nodePath, input) + "#probe"; }
    };

}
