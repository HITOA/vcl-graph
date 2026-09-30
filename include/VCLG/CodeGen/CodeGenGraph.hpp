#pragma once

#include <VCLG/Graph/GraphContext.hpp>
#include <VCLG/Graph/GraphInstance.hpp>
#include <VCLG/Graph/Elaboration.hpp>
#include <VCLG/Graph/Node.hpp>
#include <VCLG/CodeGen/CodeGenEntrypoint.hpp>
#include <VCLG/CodeGen/GraphLayout.hpp>
#include <VCLG/CodeGen/SlotPlanner.hpp>
#include <VCLG/Translation/Translation.hpp>

#include <VCL/Sema/ModuleTable.hpp>

#include <optional>
#include <vector>


namespace VCLG {

    struct CodeGenGraphOptions {
        /**
         * - Planned (`state-as-data.md` §5): every source node is translated (§4), a slot planner
         *   decides where each piece of storage lives, and `Main(ui)` / `Reset(ui)` call the
         *   translated entry points with the slots. The host binds the layout's regions
         *   (`GetLayout`).
         * - Legacy: each node compiled as written, its variables as globals; `Main()` / `Reset()`.
         *   Kept to compare the two until the planned mode is proven (plan P4.8 removes it).
         */
        enum class Mode { Legacy, Planned };

        Mode mode = Mode::Planned;
        /**
         * Planned mode: `Main` and `Reset` get the floating-point attributes VCL gives node code, so
         * LLVM may inline the nodes into them (§7.4). Without them, LLVM never does.
         */
        bool inlineNodes = true;
        /**
         * Planned mode, debug check of always-written outputs (§3.3): each temporary output is
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
    };

    /**
     * Compiles a graph into one LLVM module: elaborates it (see ElaboratedGraph), then emits each
     * elaborated node in execution order. Every copy of a subgraph gets its own symbols, prefixed
     * by its graph path.
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
        /** After Emit, planned mode: the slot plan of the root frame, or null. */
        inline const SlotPlan* GetPlan() const { return plan ? &*plan : nullptr; }
        /** After Emit, planned mode: what the host binds and allocates (empty in legacy mode). */
        inline const GraphLayout& GetLayout() const { static const GraphLayout empty{}; return plan ? plan->layout : empty; }
        /** After Emit, planned mode: the interface of the source node at `path`, or null. */
        const NodeInterface* GetNodeInterface(llvm::StringRef path) const;

    private:
        using NodeIndex = ElaboratedGraph::NodeIndex;

        bool EmitPlanned();
        bool TranslateNodes();

        bool EmitSourceNode(NodeIndex index);
        bool EmitSubgraphInputNode(NodeIndex index);
        bool EmitSubgraphOutputNode(NodeIndex index);
        bool EmitFeedbackInputNode(NodeIndex index);
        bool EmitFeedbackOutputNode(NodeIndex index);

        /**
         * The global holding the value of `index`'s input `input` when it's not simply the
         * producer's (converted connection, or unconnected): declared as `name` under the node's
         * path, filled by the converter or with the input's initializer.
         */
        llvm::GlobalVariable* EmitInputGlobal(NodeIndex index, uint32_t input, llvm::StringRef name);
        /** Declares a global `name` of `type` under the mangling prefix `path`, with an optional initial value. */
        llvm::GlobalVariable* EmitGlobal(const std::string& path, llvm::StringRef name, VCL::Type* type,
            const std::optional<VCL::ConstantScalar>& initializer);
        llvm::GlobalVariable* GetSourceGlobal(const ElaboratedGraph::Edge& edge);
        bool ReportGraphError(const std::string& message);

    private:
        std::unique_ptr<CodeGenEntrypoint> entrypoint;
        std::unique_ptr<CodeGenEntrypoint> reset;
        GraphContext& graphContext;
        GraphInstance& graph;
        llvm::Module& module;
        CodeGenGraphOptions options;
        VCL::CompilerContext cc;
        VCL::ModuleTable aggregatedImportedModuleTable;

        ElaboratedGraph elaborated;
        std::vector<std::shared_ptr<VCL::CompilerInstance>> nodeCompilerInstances;
        /** The global holding each output of each elaborated node, once emitted. */
        std::vector<llvm::SmallVector<llvm::GlobalVariable*, 2>> outputGlobals;
        /** The global holding each Feedback Input's value, declared by the first Feedback Output reading it. */
        llvm::DenseMap<NodeIndex, llvm::GlobalVariable*> feedbackGlobals;

        /** Planned mode: each source node's translation, by node index. */
        std::vector<std::optional<TranslatedNode>> translatedNodes;
        std::optional<SlotPlan> plan;
    };

}
