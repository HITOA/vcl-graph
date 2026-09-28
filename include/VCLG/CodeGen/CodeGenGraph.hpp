#pragma once

#include <VCLG/Graph/GraphContext.hpp>
#include <VCLG/Graph/GraphInstance.hpp>
#include <VCLG/Graph/Elaboration.hpp>
#include <VCLG/Graph/Node.hpp>
#include <VCLG/CodeGen/CodeGenEntrypoint.hpp>

#include <VCL/Sema/ModuleTable.hpp>

#include <vector>


namespace VCLG {

    /**
     * Compiles a graph into one LLVM module: elaborates it (see ElaboratedGraph), then emits each
     * elaborated node in execution order. Every copy of a subgraph gets its own symbols, prefixed
     * by its graph path.
     */
    class CodeGenGraph {
    public:
        CodeGenGraph() = delete;
        CodeGenGraph(GraphContext& graphContext, GraphInstance& graph, llvm::Module& module);
        CodeGenGraph(const CodeGenGraph& other) = delete;
        CodeGenGraph(CodeGenGraph&& other) = delete;
        ~CodeGenGraph() = default;

        CodeGenGraph& operator=(const CodeGenGraph& other) = delete;
        CodeGenGraph& operator=(CodeGenGraph&& other) = delete;

        inline GraphContext& GetGraphContext() const { return graphContext; }
        inline GraphInstance& GetGraphInstance() const { return graph; }
        inline llvm::LLVMContext& GetLLVMContext() const { return module.getContext(); }
        inline llvm::Module& GetLLVMModule() const { return module; }

        bool LinkNow();

        bool Emit();

    private:
        using NodeIndex = ElaboratedGraph::NodeIndex;

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
        VCL::CompilerContext cc;
        VCL::ModuleTable aggregatedImportedModuleTable;

        ElaboratedGraph elaborated;
        std::vector<std::shared_ptr<VCL::CompilerInstance>> nodeCompilerInstances;
        /** The global holding each output of each elaborated node, once emitted. */
        std::vector<llvm::SmallVector<llvm::GlobalVariable*, 2>> outputGlobals;
        /** The global holding each Feedback Input's value, declared by the first Feedback Output reading it. */
        llvm::DenseMap<NodeIndex, llvm::GlobalVariable*> feedbackGlobals;
    };

}
