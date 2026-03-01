#pragma once

#include <VCLG/Core/Allocator.hpp>
#include <VCLG/Core/IdentityProvider.hpp>
#include <VCLG/Graph/Node.hpp>
#include <VCLG/Graph/Port.hpp>

#include <memory>
#include <vector>
#include <algorithm>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>


namespace VCLG {
    class Node;
 
    class GraphStorage {
    public:
        GraphStorage() : nodes{}, identityToNode{}, identityToPort{} {}
        GraphStorage(const GraphStorage& other) = delete;
        GraphStorage(GraphStorage&& other) = delete;
        ~GraphStorage() = default;

        GraphStorage& operator=(const GraphStorage& other) = delete;
        GraphStorage& operator=(GraphStorage&& other) = delete;

        inline void AddNode(Node* node) { 
            nodes.push_back(node);
            identityToNode.insert({ node->GetIdentity(), node });
        }

        inline void RemoveNode(Node* node) {
            if (!identityToNode.count(node->GetIdentity()))
                return;
            identityToNode.erase(node->GetIdentity());
            auto it = std::find(nodes.begin(), nodes.end(), node);
            if (it != nodes.end())
                nodes.erase(it);
        }

        inline Node* GetNodeByIdentity(Identity identity) const {
            if (identityToNode.count(identity))
                return identityToNode.at(identity);
            return nullptr;
        }

        inline void AddPort(Port* port) {
            identityToPort.insert({ port->GetIdentity(), port });
        }

        inline void RemovePort(Port* port) {
            if (identityToPort.count(port->GetIdentity()))
                identityToPort.erase(port->GetIdentity());
        }

        inline Port* GetPortByIdentity(Identity identity) const {
            if (identityToPort.count(identity))
                return identityToPort.at(identity);
            return nullptr;
        }

        inline llvm::ArrayRef<Node*> GetNodes() const { return nodes; }
        
    private:
        std::vector<Node*> nodes;
        llvm::DenseMap<Identity, Node*> identityToNode;
        llvm::DenseMap<Identity, Port*> identityToPort;
    };

}