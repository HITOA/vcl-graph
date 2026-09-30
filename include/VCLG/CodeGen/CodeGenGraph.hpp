#pragma once

#include <VCLG/Graph/GraphContext.hpp>
#include <VCLG/Graph/GraphInstance.hpp>
#include <VCLG/Graph/Elaboration.hpp>
#include <VCLG/Graph/Node.hpp>
#include <VCLG/CodeGen/GraphLayout.hpp>
#include <VCLG/CodeGen/SlotPlanner.hpp>
#include <VCLG/Core/Diagnostics.hpp>
#include <VCLG/Translation/Translation.hpp>

#include <VCL/Sema/ModuleTable.hpp>

#include <optional>
#include <vector>


namespace VCLG {

    struct CodeGenGraphOptions {
        /** What the instances of one variant share in the module (§7.2). */
        enum class CodeSharing {
            /**
             * One copy of the variant's code, called by every instance: least code, but LLVM can't
             * specialize it for one instance's constant inputs. Measured 1.18x slower (1.43x on
             * presets, plan P6.2): kept to measure, not to use. Voices share code through
             * their instance frame, not through this mode.
             */
            PerVariant,
            /**
             * One copy per instance, named after its path, cloned from the variant's single
             * translation: each is specialized for its own call (the code before P6). The default,
             * inside instance frames too.
             */
            PerInstance
        };

        /**
         * `Main` and `Reset` get the floating-point attributes VCL gives node code, so
         * LLVM may inline the nodes into them (§7.4). Without them, LLVM never does.
         */
        bool inlineNodes = true;
        /**
         * Debug check of always-written outputs (§3.3): each temporary output is
         * filled with a canary pattern instead of zeros, and checked after the call. A violation
         * increments the module's external global `vclg.always_written.violations` (`uint32`) and
         * stores the output's key (a C string) in `vclg.always_written.last`.
         */
#ifdef VCLG_CHECK_ALWAYS_WRITTEN
        bool checkAlwaysWritten = true;
#else
        bool checkAlwaysWritten = false;
#endif
        SlotPlannerOptions planner{};
        /**
         * Takes unchanged variants from the GraphContext's VariantCache, and stores the new ones
         * (§7.3). Off, every variant is translated (the cache is left untouched).
         */
        bool useVariantCache = true;
        CodeSharing codeSharing = CodeSharing::PerInstance;
    };

    /**
     * Compiles a graph into one LLVM module (`state-as-data.md` §4, §5): elaborates it (see
     * ElaboratedGraph), translates every source node that runs, plans where each piece of storage
     * lives (SlotPlanner), and emits `Main(ui)` / `Reset(ui)`, which call the nodes' entry points
     * with their slots. The host allocates and binds what the layout (`GetLayout`) describes.
     *
     * Each variant (§7.1) is translated once, its symbols prefixed by its key's hash, and taken from
     * the GraphContext's VariantCache when unchanged. Its instances call one copy of its code, or
     * each its own copy, prefixed by the instance's graph path (`CodeGenGraphOptions::codeSharing`).
     * What the graph binds (state, host ports) is named after each instance's graph path.
     */
    class CodeGenGraph {
    public:
        CodeGenGraph() = delete;
        CodeGenGraph(GraphContext& graphContext, GraphInstance& graph, llvm::Module& module, CodeGenGraphOptions options = {});
        CodeGenGraph(const CodeGenGraph& other) = delete;
        CodeGenGraph(CodeGenGraph&& other) = delete;
        ~CodeGenGraph();

        CodeGenGraph& operator=(const CodeGenGraph& other) = delete;
        CodeGenGraph& operator=(CodeGenGraph&& other) = delete;

        inline GraphContext& GetGraphContext() const { return graphContext; }
        inline GraphInstance& GetGraphInstance() const { return graph; }
        inline llvm::LLVMContext& GetLLVMContext() const { return module.getContext(); }
        inline llvm::Module& GetLLVMModule() const { return module; }
        inline const CodeGenGraphOptions& GetOptions() const { return options; }

        bool LinkNow();

        bool Emit();

        /** After Emit: the elaborated graph that was compiled. */
        inline const ElaboratedGraph& GetElaboratedGraph() const { return elaborated; }
        /** After Emit: the slot plan of the root frame, or null. */
        inline const SlotPlan* GetPlan() const { return plan ? &*plan : nullptr; }
        /** After Emit: what the host binds and allocates. */
        inline const GraphLayout& GetLayout() const { static const GraphLayout empty{}; return plan ? plan->layout : empty; }
        /** After Emit: the interface of the source node at `path`, or null. */
        const NodeInterface* GetNodeInterface(llvm::StringRef path) const;

    private:
        using NodeIndex = ElaboratedGraph::NodeIndex;

        bool TranslateNodes();
        bool ReportGraphError(const std::string& message);

    private:
        GraphContext& graphContext;
        GraphInstance& graph;
        llvm::Module& module;
        CodeGenGraphOptions options;
        /** Between the compile's diagnostics and the user's consumer; before `cc`, which reports to it. */
        DiagnosticRecorder recorder;
        VCL::CompilerContext cc;
        VCL::ModuleTable aggregatedImportedModuleTable;

        ElaboratedGraph elaborated;
        /** Each source node's translation, by node index. */
        std::vector<std::optional<TranslatedNode>> translatedNodes;
        std::optional<SlotPlan> plan;
    };

}
