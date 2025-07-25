#pragma once

#include <VCLG/Node.hpp>
#include <VCLG/ExecutionContext.hpp>

#include <vector>
#include <atomic>
#include <functional>


namespace VCLG {

    class Graph {
    public:
        struct Connection {
            uint32_t outputNodeIdx;
            uint32_t outputNodePortIdx;
            uint32_t inputNodeIdx;
            uint32_t inputNodePortIdx;
        };

        class PortHandle;

        class NodeHandle {
        public:
            NodeHandle() = delete;
            NodeHandle(Graph* graph, uint32_t nodeIdx);
            ~NodeHandle() = default;

            Node* Get();

            PortHandle GetInput(uint32_t idx);
            PortHandle GetOutput(uint32_t idx);

            Graph* GetGraph();
            uint32_t GetNodeIdx();

            bool SetAsGraphInput(); // This node will be treated as a graph input
            bool SetAsGraphOutput(); // This node will be treated as a graph output

            inline Node* operator->() { return graph->nodes[nodeIdx].get(); }
            inline NodeHandle& operator*() { return *this; }
            inline bool operator==(const NodeHandle& rhs) { return nodeIdx == rhs.nodeIdx && graph == rhs.graph; }
            inline bool operator!=(const NodeHandle& rhs) { return !(*this == rhs); }
            inline NodeHandle operator++() { return NodeHandle{ graph, ++nodeIdx }; }
            inline NodeHandle operator++(int) { return NodeHandle{ graph, nodeIdx++ }; }

        private:
            Graph* graph;
            uint32_t nodeIdx;  
        };

        class PortHandle {
        public:
            PortHandle() = delete;
            PortHandle(NodeHandle handle, uint32_t portIdx, bool input);
            ~PortHandle() = default;

            Port* operator->();
            Port* Get();

            bool Connect(PortHandle handle);

            Graph* GetGraph();
            NodeHandle GetNodeHandle();
            uint32_t GetPortIdx();
            bool IsPortInput();

        private:
            NodeHandle handle;
            uint32_t portIdx;
            bool input;
        };

        struct ExecutionContextHolder {
            std::shared_ptr<ExecutionContext> context;
            std::atomic_uint32_t counter;
        };

        class ExecutionContextHandle {
        public:
            ExecutionContextHandle() = delete;
            ExecutionContextHandle(ExecutionContextHolder* holder);
            ~ExecutionContextHandle();

            ExecutionContext* operator->();

            operator bool() const { return holder->context != nullptr; }

        private:
            ExecutionContextHolder* holder;
        };

    public:
        Graph() = delete;
        Graph(std::shared_ptr<VCL::Logger> logger = nullptr);
        ~Graph();

        NodeHandle AddNode(std::unique_ptr<Node> node);
        bool AddConnection(Connection connection);

        NodeHandle begin();
        NodeHandle end();
        
        bool Compile(std::function<void*(ExecutionContext*)> userDataConstructor = nullptr, std::function<void(void*)> userDataDestroyer = nullptr);

        ExecutionContextHandle GetExecutionContext();

    private:
        bool GetNodesOrder(std::vector<uint32_t>& order);
        std::unique_ptr<VCL::ASTFunctionDeclaration> CreateGraphEntrypoint(const std::vector<std::string>& nodesEntrypoint);
        std::shared_ptr<ExecutionContextHolder> CreateExecutionContext(std::unique_ptr<VCL::ASTProgram> program);

    private:
        std::shared_ptr<VCL::Logger> logger;
        std::vector<std::unique_ptr<Node>> nodes;
        std::vector<uint32_t> inputNodes;
        std::vector<uint32_t> outputNodes;
        std::vector<Connection> connections;

        std::shared_ptr<ExecutionContextHolder> current;
        std::shared_ptr<ExecutionContextHolder> previous;

        std::atomic<ExecutionContextHolder*> currentHolder;
    };

}