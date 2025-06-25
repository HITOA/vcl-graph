#pragma once

#include <VCLG/Node.hpp>

#include <vector>


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

            Node* operator->();
            Node* Get();

            PortHandle GetInput(uint32_t idx);
            PortHandle GetOutput(uint32_t idx);

            Graph* GetGraph();
            uint32_t GetNodeIdx();

            bool SetAsGraphInput(); // This node will be treated as a graph input
            bool SetAsGraphOutput(); // This node will be treated as a graph output

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

        private:
            NodeHandle handle;
            uint32_t portIdx;
            bool input;
        };

        Graph() = delete;
        Graph(std::shared_ptr<VCL::Logger> logger = nullptr);
        ~Graph();

        NodeHandle AddNode(std::unique_ptr<Node> node);
        bool AddConnection(Connection connection);

        std::unique_ptr<VCL::ASTProgram> Compile();

    private:
        bool GetNodesOrder(std::vector<uint32_t>& order);

    private:
        std::shared_ptr<VCL::Logger> logger;
        std::vector<std::unique_ptr<Node>> nodes;
        std::vector<uint32_t> inputNodes;
        std::vector<uint32_t> outputNodes;
        std::vector<Connection> connections;
    };

}