#pragma once

#include <VCLG/Core/Allocator.hpp>
#include <VCLG/Core/IdentityProvider.hpp>
#include <VCLG/Graph/GraphStorage.hpp>
#include <VCLG/Graph/Port.hpp>
#include <VCLG/Graph/Node.hpp>
#include <VCLG/Graph/Connection.hpp>

#include <vector>


namespace VCLG {
    class GraphContext;

    class GraphInstance {
    public:
        GraphInstance() = delete;
        GraphInstance(GraphContext& graphContext, std::unique_ptr<Allocator> allocator = std::make_unique<TLSFAllocator>());
        GraphInstance(const GraphInstance& other) = delete;
        GraphInstance(GraphInstance&& other) = delete;
        ~GraphInstance();

        GraphInstance& operator=(const GraphInstance& other) = delete;
        GraphInstance& operator=(GraphInstance&& other) = delete;

        inline llvm::ArrayRef<Node*> GetNodes() const { return storage.GetNodes(); }
        inline llvm::ArrayRef<Connection> GetConnections() const { return connections; }


        inline Node* GetNodeByIdentity(Identity identity) const { return storage.GetNodeByIdentity(identity); }
        inline Port* GetPortByIdentity(Identity identity) const { return storage.GetPortByIdentity(identity); }


        SourceNode* InstantiateSourceNode(VCL::Source* source);
        
        void DestroyNode(Node* node);

        bool Connect(Identity portAIdentity, Identity portBIdentity);
        bool Connect(Port* portA, Port* portB);

        void Reset();

    private:
        void DestroySourceNode(SourceNode* node);

        bool ConnectOutputToInput(Port* outPort, Port* inPort);
        bool HasConnection(Port* outPort, Port* inPort);
        bool CanBeConnected(VCL::Type* outType, VCL::Type* inType);

    private:
        GraphContext& graphContext;

        std::unique_ptr<Allocator> allocator;
        IdentityProvider identityProvider;
        GraphStorage storage;

        std::vector<Connection> connections;
    };

}