#include <VCLG/Graph.hpp>

#include "NodeProcessor.hpp"

#include <queue>

#define IT_MAX 100000


VCLG::Graph::NodeHandle::NodeHandle(Graph* graph, uint32_t nodeIdx) : graph{ graph }, nodeIdx{ nodeIdx } {}

VCLG::Node* VCLG::Graph::NodeHandle::operator->() {
    return graph->nodes[nodeIdx].get();
}

VCLG::Node* VCLG::Graph::NodeHandle::Get() {
    return graph->nodes[nodeIdx].get();
}

VCLG::Graph::PortHandle VCLG::Graph::NodeHandle::GetInput(uint32_t idx) {
    return PortHandle{ *this, idx, true };
}

VCLG::Graph::PortHandle VCLG::Graph::NodeHandle::GetOutput(uint32_t idx) {
    return PortHandle{ *this, idx, false };
}

VCLG::Graph* VCLG::Graph::NodeHandle::GetGraph() {
    return graph;
}

uint32_t VCLG::Graph::NodeHandle::GetNodeIdx() {
    return nodeIdx;
}

bool VCLG::Graph::NodeHandle::SetAsGraphInput() {
    graph->inputNodes.push_back(nodeIdx);
    return true;
}

bool VCLG::Graph::NodeHandle::SetAsGraphOutput() {
    graph->outputNodes.push_back(nodeIdx);
    return true;
}

VCLG::Graph::PortHandle::PortHandle(NodeHandle handle, uint32_t portIdx, bool input) : handle{ handle }, portIdx{ portIdx }, input{ input } {}

VCLG::Port* VCLG::Graph::PortHandle::operator->() {
    if (input)
        return handle.Get()->GetInputs()[portIdx].get();
    else 
        return handle.Get()->GetOutputs()[portIdx].get();
}

VCLG::Port* VCLG::Graph::PortHandle::Get() {
    if (input)
        return handle.Get()->GetInputs()[portIdx].get();
    else 
        return handle.Get()->GetOutputs()[portIdx].get();
}

bool VCLG::Graph::PortHandle::Connect(PortHandle handle) {
    if (handle.input == input) {
        this->handle.GetGraph()->logger->Error("Cannot connect two input or output port together.");
        return false;
    }

    Connection conn{};
    if (input) {
        conn.inputNodeIdx = this->handle.GetNodeIdx();
        conn.inputNodePortIdx = portIdx;
        conn.outputNodeIdx = handle.handle.GetNodeIdx();
        conn.outputNodePortIdx = handle.portIdx;
    } else {
        conn.outputNodeIdx = this->handle.GetNodeIdx();
        conn.outputNodePortIdx = portIdx;
        conn.inputNodeIdx = handle.handle.GetNodeIdx();
        conn.inputNodePortIdx = handle.portIdx;
    }
    return this->handle.GetGraph()->AddConnection(conn);
}

VCLG::Graph::Graph(std::shared_ptr<VCL::Logger> logger) : logger{ logger } {

}

VCLG::Graph::~Graph() {

}

VCLG::Graph::NodeHandle VCLG::Graph::AddNode(std::unique_ptr<Node> node) {
    uint32_t newNodeIdx = (uint32_t)nodes.size();
    node->Reset();
    if (node->GetInputs().size() == 0 && node->GetOutputs().size() == 0 && logger)
        logger->Warning("Node {} doesn't have any inputs & outputs and cannot be connected to other nodes in the graph.", newNodeIdx);
    nodes.push_back(std::move(node));
    return NodeHandle{ this, newNodeIdx };
}

bool VCLG::Graph::AddConnection(Connection connection) {
    Node* outputNode = nodes[connection.outputNodeIdx].get();
    Node* inputNode = nodes[connection.inputNodeIdx].get();

    Port* outputPort = outputNode->GetOutputs()[connection.outputNodePortIdx].get();
    Port* inputPort = inputNode->GetInputs()[connection.inputNodePortIdx].get();

    if (!outputPort->CanConnect(inputPort)) {
        if (logger)
            logger->Error("Cannot connect given ports. Type doesn't match.");
        return false;
    }

    connections.push_back(connection);
    return true;
}

std::unique_ptr<VCL::ASTProgram> VCLG::Graph::Compile() {
    std::vector<uint32_t> nodesOrder{};

    if (!GetNodesOrder(nodesOrder))
        return nullptr;

    std::unordered_map<std::string, std::string> inputsRemapping{};
    std::unordered_set<std::string> outputs{};

    for (auto& connection : connections) {
        std::string outputPortName = nodes[connection.outputNodeIdx]->GetOutputs()[connection.outputNodePortIdx]->GetName();
        std::string inputPortName = nodes[connection.inputNodeIdx]->GetInputs()[connection.inputNodePortIdx]->GetName();
        
        std::string prefixedOutputPortName = "Node" + std::to_string(connection.outputNodeIdx) + "_" + outputPortName;
        std::string prefixedInputPortName = "Node" + std::to_string(connection.inputNodeIdx) + "_" + inputPortName;

        inputsRemapping[prefixedInputPortName] = prefixedOutputPortName;
        outputs.insert(prefixedOutputPortName);
    }

    NodeProcessor processor{ inputsRemapping, outputs };
    std::vector<std::unique_ptr<VCL::ASTStatement>> statements{};

    for (uint32_t nodeIdx : nodesOrder) {
        processor.Process(nodes[nodeIdx].get(), nodeIdx);
        std::unique_ptr<VCL::ASTProgram> nodeProgram = nodes[nodeIdx]->Reset();
        for (size_t i = 0; i < nodeProgram->statements.size(); ++i) {
            statements.push_back(std::move(nodeProgram->statements[i]));
        }
    }

    std::shared_ptr<VCL::Source> graphSource = std::make_shared<VCL::Source>();
    graphSource->source = "";
    graphSource->path = "graph.vcl";

    return std::make_unique<VCL::ASTProgram>(std::move(statements), graphSource);
}

bool VCLG::Graph::GetNodesOrder(std::vector<uint32_t>& order) {
    order.clear();

    std::unordered_set<uint32_t> addedNodes{};
    std::queue<uint32_t> nodesToVisit{ outputNodes.begin(), outputNodes.end() };

    uint32_t it = 0;
    while (true) {
        if (nodesToVisit.empty())
            break;

        if (it >= IT_MAX) {
            if (logger)
                logger->Error("Infinite recursion detected while compiling the graph.");
            return false;
        }

        uint32_t currentNodeIdx = nodesToVisit.front();
        nodesToVisit.pop();

        if (!addedNodes.count(currentNodeIdx)) {
            addedNodes.insert(currentNodeIdx);
            order.push_back(currentNodeIdx);
        } else {
            order.erase(std::remove(order.begin(), order.end(), currentNodeIdx), order.end());
        }

        for (auto& connection : connections) {
            if (connection.inputNodeIdx == currentNodeIdx)
                nodesToVisit.push(connection.outputNodeIdx);
        }

        ++it;
    }

    std::reverse(order.begin(), order.end());

    return true;
}