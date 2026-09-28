#pragma once

#include <VCLG/Core/IdentityProvider.hpp>
#include <VCLG/Graph/GraphContext.hpp>
#include <VCLG/Graph/GraphStorage.hpp>
#include <VCLG/Graph/Port.hpp>
#include <VCLG/Graph/Node.hpp>
#include <VCLG/Graph/Connection.hpp>

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
        GraphInstance(GraphContext& graphContext, Identity identity);
        GraphInstance(const GraphInstance& other) = delete;
        GraphInstance(GraphInstance&& other) = delete;
        ~GraphInstance();

        GraphInstance& operator=(const GraphInstance& other) = delete;
        GraphInstance& operator=(GraphInstance&& other) = delete;

        inline GraphContext& GetGraphContext() { return graphContext; }
        inline Identity GetIdentity() const { return identity; }

        inline llvm::ArrayRef<Node*> GetNodes() const { return storage.GetNodes(); }
        inline llvm::ArrayRef<Connection> GetConnections() const { return connections; }

        inline Node* GetNodeByIdentity(Identity identity) const { return storage.GetNodeByIdentity(identity); }
        inline Port* GetPortByIdentity(Identity identity) const { return storage.GetPortByIdentity(identity); }
        inline Parameter* GetParameterByIdentity(Identity identity) const { return storage.GetParameterByIdentity(identity); }

        Connection* FindConnectionByPort(Port* outPort, Port* inPort);
        Connection* FindConnectionByIdentity(Identity identity);

        SourceNode* InstantiateSourceNode(VCL::Source* source);

        template<typename T, typename... Args>
        inline T* InstantiateBuiltinNode(Args&&... args) {
            std::unique_ptr<T> node = std::make_unique<T>(*this, identityProvider.Next(), std::forward<Args>(args)...);
            T* ptr = node.get();
            ptr->Initialize();
            storage.AddNode(std::move(node));
            return ptr;
        }

        Port* InstantiatePort(Identity owner, VCL::Type* type, const std::string& displayName, 
            Port::PortKind kind, VCL::ConstantValue* initializer, bool isDependent);
        void DestroyPort(Port* port);
        Port* OverwritePort(Port* port, Identity owner, VCL::Type* type, const std::string& displayName, 
            Port::PortKind kind, VCL::ConstantValue* initializer, bool isDependent);

        Parameter* InstantiateParameter(Identity owner, VCL::Type* type, const std::string& displayName, VCL::ConstantValue* initializer);
        void DestroyParameter(Parameter* parameter);
        
        void DestroyNode(Node* node);
        void DestroyConnection(Identity identity);
        void DestroyAllPortConnections(Identity portIdentity);

        /**
         * Connects an output and an input (in either order). Refused (INVALID_IDENTITY) when the
         * input is already connected, when it would close a cycle, or when the types don't match:
         * directly, through a converter, or by inferring the dependent ports' types.
         */
        Identity Connect(Identity portAIdentity, Identity portBIdentity);
        Identity Connect(Port* portA, Port* portB);

        /**
         * Fill `order` with the nodes reachable upstream from `roots`, in execution order: every
         * node comes after the nodes feeding its inputs, and a Feedback Input comes after the
         * Feedback Outputs that read it. Returns false if the connections contain a cycle.
         */
        bool BuildExecutionOrder(llvm::ArrayRef<Node*> roots, std::vector<Node*>& order) const;

        void Reset();

        inline const std::string& GetName() const { return name; }
        inline void SetName(const std::string& name) { this->name = name; }

    private:
        void DestroyNodeConnections(Node* node);
        void DestroySourceNode(SourceNode* node);
        void DestroyBuiltinNode(BuiltinNode* node);
        Port* CreatePort(Identity identity, Identity owner, VCL::Type* type, const std::string& displayName, 
            Port::PortKind kind, VCL::ConstantValue* initializer, bool isDependent);

        Identity ConnectOutputToInput(Port* outPort, Port* inPort);
        Identity HasConnection(Port* outPort, Port* inPort);
        bool WouldCreateCycle(Port* outPort, Port* inPort) const;

    private:
        Identity identity;
        GraphContext& graphContext;

        IdentityProvider identityProvider;
        GraphStorage storage;

        std::vector<Connection> connections;

        std::string name;
    };

}