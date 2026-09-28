#pragma once

#include <VCLG/Graph/GraphContext.hpp>
#include <VCLG/Graph/GraphInstance.hpp>
#include <VCLG/Graph/Node.hpp>
#include <VCLG/CodeGen/CodeGenEntrypoint.hpp>

#include <VCL/Sema/ModuleTable.hpp>

#include <vector>


namespace VCLG {

    class CodeGenGraph {
    public:
        CodeGenGraph() = delete;
        /**
         * `manglingScope` prefixes the symbols of every node emitted here. Empty means the root graph
         * ("g<graph identity>"). A subgraph passes the path of the SubgraphNode that uses it, so the
         * same subgraph used twice gets two separate copies of its nodes' code and state.
         */
        CodeGenGraph(GraphContext& graphContext, GraphInstance& graph, llvm::Module& module, std::string manglingScope = {});
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
        bool EmitSourceNode(SourceNode* node);
        bool EmitTransientNode(TransientNode* node);

        Port* GetInPortToOutPort(Port* inPort);
        llvm::GlobalVariable* GetOutPortGlobalVar(Port* port);
        void AddOutPortGlobalVar(Port* port, llvm::GlobalVariable* var);

        void ImportSubgraph(CodeGenGraph& codegen);

        /** Mangling prefix for the CompilerInstances created for `node`: "<scope>/n<identity>". */
        std::string GetNodeManglingPrefix(Node* node) const;
    
    private:
        void BuildPortMap();
        bool BuildOrderedNodeList(std::vector<Node*>& nodes);

    private:
        std::unique_ptr<CodeGenEntrypoint> entrypoint;
        std::unique_ptr<CodeGenEntrypoint> reset;
        GraphContext& graphContext;
        GraphInstance& graph;
        llvm::Module& module;
        std::string manglingScope;
        VCL::CompilerContext cc;
        VCL::ModuleTable aggregatedImportedModuleTable;
        
        std::vector<std::shared_ptr<VCL::CompilerInstance>> nodeCompilerInstances;
        std::unordered_map<Port*, Port*> inPortToOutPort;
        std::unordered_map<Port*, llvm::GlobalVariable*> outPortGlobalVar;
    };

}