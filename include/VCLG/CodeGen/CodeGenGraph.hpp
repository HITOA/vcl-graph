#pragma once

#include <VCLG/Graph/GraphContext.hpp>
#include <VCLG/Graph/GraphInstance.hpp>
#include <VCLG/Graph/Node.hpp>

#include <VCL/Sema/ModuleTable.hpp>

#include <vector>


namespace VCLG {

    class CodeGenGraph {
    public:
        CodeGenGraph() = delete;
        CodeGenGraph(GraphContext& graphContext, GraphInstance& graph, llvm::Module& module);
        CodeGenGraph(const CodeGenGraph& other) = delete;
        CodeGenGraph(CodeGenGraph&& other) = delete;
        ~CodeGenGraph() = default;

        CodeGenGraph& operator=(const CodeGenGraph& other) = delete;
        CodeGenGraph& operator=(CodeGenGraph&& other) = delete;

        bool Emit();
        bool EmitSourceNode(SourceNode* node);
    
    private:
        void BuildPortMap();
        std::vector<Node*> BuildOrderedNodeList();

        llvm::ArrayRef<Port*> GetNodeInputs(Node* node);
        llvm::ArrayRef<Port*> GetNodeOutputs(Node* node);

    private:
        GraphContext& graphContext;
        GraphInstance& graph;
        llvm::Module& module;
        VCL::ModuleTable aggregatedImportedModuleTable;
        
        std::vector<std::shared_ptr<VCL::CompilerInstance>> nodeCompilerInstances;
        std::unordered_map<Port*, Port*> inPortToOutPort;
        std::unordered_map<Port*, llvm::GlobalVariable*> outPortGlobalVar;
    };

}