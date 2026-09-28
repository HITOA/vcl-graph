#pragma once

#include <VCLG/Core/IdentityProvider.hpp>
#include <VCLG/Graph/Node.hpp>
#include <VCLG/Graph/Port.hpp>
#include <VCLG/Graph/Parameter.hpp>

#include <memory>
#include <vector>
#include <algorithm>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>


namespace VCLG {
    class Node;

    /**
     * Owns the nodes, ports and parameters of a graph, and finds them by identity. Nodes keep plain
     * pointers to their ports and parameters: GraphInstance destroys those along with the node.
     */
    class GraphStorage {
    public:
        GraphStorage() = default;
        GraphStorage(const GraphStorage& other) = delete;
        GraphStorage(GraphStorage&& other) = delete;
        ~GraphStorage() = default;

        GraphStorage& operator=(const GraphStorage& other) = delete;
        GraphStorage& operator=(GraphStorage&& other) = delete;

        inline Node* AddNode(std::unique_ptr<Node> node) {
            Node* ptr = node.get();
            nodes.push_back(ptr);
            identityToNode.insert({ ptr->GetIdentity(), std::move(node) });
            return ptr;
        }

        /** Unregister `node` and hand back its ownership (null if it isn't in this storage). */
        inline std::unique_ptr<Node> RemoveNode(Node* node) {
            auto it = identityToNode.find(node->GetIdentity());
            if (it == identityToNode.end())
                return nullptr;
            std::unique_ptr<Node> owned = std::move(it->second);
            identityToNode.erase(it);
            nodes.erase(std::find(nodes.begin(), nodes.end(), node));
            return owned;
        }

        inline Node* GetNodeByIdentity(Identity identity) const {
            auto it = identityToNode.find(identity);
            return it != identityToNode.end() ? it->second.get() : nullptr;
        }

        inline Port* AddPort(std::unique_ptr<Port> port) {
            Port* ptr = port.get();
            identityToPort.insert({ ptr->GetIdentity(), std::move(port) });
            return ptr;
        }

        inline void RemovePort(Port* port) {
            identityToPort.erase(port->GetIdentity());
        }

        inline Port* GetPortByIdentity(Identity identity) const {
            auto it = identityToPort.find(identity);
            return it != identityToPort.end() ? it->second.get() : nullptr;
        }

        inline Parameter* AddParameter(std::unique_ptr<Parameter> parameter) {
            Parameter* ptr = parameter.get();
            identityToParameter.insert({ ptr->GetIdentity(), std::move(parameter) });
            return ptr;
        }

        inline void RemoveParameter(Parameter* parameter) {
            identityToParameter.erase(parameter->GetIdentity());
        }

        inline Parameter* GetParameterByIdentity(Identity identity) const {
            auto it = identityToParameter.find(identity);
            return it != identityToParameter.end() ? it->second.get() : nullptr;
        }

        inline llvm::ArrayRef<Node*> GetNodes() const { return nodes; }

    private:
        // Creation order, which callers (serialization, the UI) see.
        std::vector<Node*> nodes{};
        llvm::DenseMap<Identity, std::unique_ptr<Node>> identityToNode{};
        llvm::DenseMap<Identity, std::unique_ptr<Port>> identityToPort{};
        llvm::DenseMap<Identity, std::unique_ptr<Parameter>> identityToParameter{};
    };

}
