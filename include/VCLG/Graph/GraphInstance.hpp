#pragma once

#include <VCLG/Core/Allocator.hpp>
#include <VCLG/Core/IdentityProvider.hpp>
#include <VCLG/Graph/GraphStorage.hpp>
#include <VCLG/Graph/Port.hpp>
#include <VCLG/Graph/Node.hpp>
#include <VCLG/Graph/Connection.hpp>
#include <VCLG/Graph/GraphUserDataTailAllocator.hpp>
#include <VCLG/Graph/GraphValidator.hpp>

#include <VCL/AST/Template.hpp>
#include <VCL/Frontend/CompilerInstance.hpp>

#include <llvm/ADT/DenseMap.h>

#include <vector>
#include <memory>


namespace VCLG {
    class GraphContext;

    class GraphInstance {
    public:
        GraphInstance() = delete;
        GraphInstance(GraphContext& graphContext, 
            std::shared_ptr<GraphUserDataTailAllocator> userDataTailAllocator = std::make_shared<GraphUserDataTailAllocator>(),
            std::unique_ptr<Allocator> allocator = std::make_unique<TLSFAllocator>());
        GraphInstance(const GraphInstance& other) = delete;
        GraphInstance(GraphInstance&& other) = delete;
        ~GraphInstance();

        GraphInstance& operator=(const GraphInstance& other) = delete;
        GraphInstance& operator=(GraphInstance&& other) = delete;

        inline GraphContext& GetGraphContext() { return graphContext; }
        inline GraphValidator& GetGraphValidator() { return validator; }

        inline llvm::ArrayRef<Node*> GetNodes() const { return storage.GetNodes(); }
        inline llvm::ArrayRef<Connection> GetConnections() const { return connections; }

        inline Node* GetNodeByIdentity(Identity identity) const { return storage.GetNodeByIdentity(identity); }
        inline Port* GetPortByIdentity(Identity identity) const { return storage.GetPortByIdentity(identity); }

        Connection* FindConnectionByPort(Port* outPort, Port* inPort);

        SourceNode* InstantiateSourceNode(VCL::Source* source);
        
        void DestroyNode(Node* node);
        void DestroyConnection(Identity identity);

        bool Connect(Identity portAIdentity, Identity portBIdentity);
        bool Connect(Port* portA, Port* portB);

        void Reset();

    private:
        void DestroyNodeConnections(Node* node);
        void DestroySourceNode(SourceNode* node);

        bool ConnectOutputToInput(Port* outPort, Port* inPort);
        bool HasConnection(Port* outPort, Port* inPort);
        bool CanBeConnected(VCL::Type* outType, VCL::Type* inType);

    private:
        GraphContext& graphContext;

        GraphValidator validator;

        std::unique_ptr<Allocator> allocator;
        IdentityProvider identityProvider;
        GraphStorage storage;

        std::vector<Connection> connections;

        std::shared_ptr<GraphUserDataTailAllocator> userDataTailAllocator;
    };

}