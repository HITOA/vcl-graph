#pragma once

#include <VCLG/Core/Allocator.hpp>
#include <VCLG/Core/IdentityProvider.hpp>
#include <VCLG/Graph/GraphContext.hpp>
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
#include <string>


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

        SubgraphNode* InstantiateSubgraphNode(std::shared_ptr<GraphInstance> instance);

        template<typename T, typename... Args>
        inline T* InstantiateTransientNode(Args&&... args) {
            size_t nodeAdditionalDataSize = userDataTailAllocator->GetNodeUserDataAdditionalSize();
            size_t nodeTotalSize = sizeof(T) + nodeAdditionalDataSize;
            Identity instancedNodeIdentity = identityProvider.Next();
            T* ptr = (T*)allocator->Allocate(nodeTotalSize, alignof(T));
            new (ptr) T{ sizeof(T), *this, instancedNodeIdentity, std::forward<Args>(args)... };
            ptr->Initialize();
            void* userDataPtr = ((uint8_t*)ptr) + sizeof(T);
            userDataTailAllocator->ConstructNodeUserData(ptr, userDataPtr);
            storage.AddNode(ptr);
            return ptr;
        }

        Port* InstantiatePort(Identity owner, VCL::Type* type, const std::string& displayName, 
            Port::PortKind kind, VCL::ConstantValue* initializer, bool isDependent);
        void DestroyPort(Port* port);
        
        void DestroyNode(Node* node);
        void DestroyConnection(Identity identity);

        bool Connect(Identity portAIdentity, Identity portBIdentity);
        bool Connect(Port* portA, Port* portB);

        void Reset();

        inline const std::string& GetName() const { return name; }
        inline void SetName(const std::string& name) { this->name = name; }

        inline void* GetUserDataPtr() const { return userDataPtr; }
        inline void SetUserDataPtr(void* userDataPtr) { this->userDataPtr = userDataPtr; }

    private:
        void DestroyNodeConnections(Node* node);
        void DestroySourceNode(SourceNode* node);
        void DestroyTransientNode(TransientNode* node);

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

        std::string name;
        void* userDataPtr;
    };

}