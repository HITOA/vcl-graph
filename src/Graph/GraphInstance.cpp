#include <VCLG/Graph/GraphInstance.hpp>

#include <VCLG/Graph/GraphContext.hpp>
#include <VCLG/Graph/Port.hpp>
#include <VCLG/Graph/Parameter.hpp>

#include <VCL/AST/ConstantValue.hpp>
#include <VCL/AST/Expr.hpp>
#include <VCL/AST/Template.hpp>
#include <VCL/Frontend/CompilerInstance.hpp>

#include <iostream>


VCLG::GraphInstance::GraphInstance(GraphContext& graphContext, 
    std::shared_ptr<GraphUserDataTailAllocator> userDataTailAllocator,
    std::unique_ptr<Allocator> allocator) :
    graphContext{ graphContext }, validator{}, allocator{ std::move(allocator) }, identityProvider{ }, storage{},
    userDataTailAllocator{ userDataTailAllocator } {
}

VCLG::GraphInstance::~GraphInstance() {
    Reset();
}

VCLG::SourceNode* VCLG::GraphInstance::InstantiateSourceNode(VCL::Source* source) {
    SourceNodeDefinition* definition = graphContext.GetDefinitionRegistry().GetOrCreateSourceNodeDefinition(source);
    if (!definition)
        return nullptr;

    size_t nodeAdditionalDataSize = userDataTailAllocator->GetNodeUserDataAdditionalSize();
    size_t nodeTotalSize = sizeof(SourceNode) + nodeAdditionalDataSize;

    size_t portAdditionalDataSize = userDataTailAllocator->GetPortUserDataAdditionalSize();
    size_t portTotalSize = sizeof(Port) + portAdditionalDataSize;

    size_t parameterAdditionalDataSize = userDataTailAllocator->GetParameterUserDataAdditionalSize();
    size_t parameterTotalSize = sizeof(Parameter) + portAdditionalDataSize;

    Identity instancedNodeIdentity = identityProvider.Next();
    
    llvm::SmallVector<Port*> inPorts;
    llvm::SmallVector<Port*> outPorts;
    llvm::SmallVector<Parameter*> parameters;

    for (SourcePortDefinition* port : definition->GetPorts()) {
        Port::PortKind kind = Port::PortKind::Input;
        if (!port->IsInput())
            kind = Port::PortKind::Output;

        VCL::ConstantValue* initializer = nullptr;
        if (port->GetDecl()->GetInitializer())
            initializer = port->GetDecl()->GetInitializer()->GetConstantValue();

        Identity instancedPortIdentity = identityProvider.Next();
        Port* instancedPort = (Port*)allocator->Allocate(portTotalSize, 4);
        new (instancedPort) Port{ 
            instancedNodeIdentity, port->GetDecl()->GetValueType().GetType(), port->GetDisplayName(), 
            kind, initializer, port->IsDependent(), instancedPortIdentity };
        void* ptr = ((uint8_t*)instancedPort) + sizeof(Port);
        userDataTailAllocator->ConstructPortUserData(instancedPort, ptr);

        storage.AddPort(instancedPort);

        if (kind == Port::Input)
            inPorts.push_back(instancedPort);
        else
            outPorts.push_back(instancedPort);
    }

    for (SourceParameterDefinition* parameter : definition->GetParameters()) {
        VCL::ConstantValue* initializer = nullptr;
        if (parameter->GetDecl()->GetInitializer())
            initializer = parameter->GetDecl()->GetInitializer()->GetConstantValue();

        Identity instancedParameterIdentity = identityProvider.Next();
        Parameter* instancedParameter = (Parameter*)allocator->Allocate(parameterTotalSize, 4);
        new (instancedParameter) Parameter{ 
            instancedNodeIdentity, parameter->GetDecl()->GetValueType().GetType(), 
            parameter->GetDisplayName(), 
            initializer, instancedParameterIdentity };
        
        void* ptr = ((uint8_t*)instancedParameter) + sizeof(Parameter);
        userDataTailAllocator->ConstructParameterUserData(instancedParameter, ptr);

        parameters.push_back(instancedParameter);
    }

    SourceNode* node = (SourceNode*)allocator->Allocate(nodeTotalSize, 4);
    new (node) SourceNode{ 
        source->GetBufferIdentifier().str(), definition->GetDisplayName(), 
        inPorts, outPorts, parameters, instancedNodeIdentity };

    for (SourceAutoParameterDefinition* autoParam : definition->GetAutoParameters()) {
        if (autoParam->GetDecl()->GetDeclClass() == VCL::Decl::TypeAliasDeclClass) {
            node->GetSubstitutionTable().SetTypeSubstitution((VCL::TypeAliasDecl*)autoParam->GetDecl(), nullptr);
        } else {
            node->GetSubstitutionTable().SetScalarSubstitution((VCL::VarDecl*)autoParam->GetDecl(), nullptr);
        }
    }

    void* ptr = ((uint8_t*)node) + sizeof(SourceNode);
    userDataTailAllocator->ConstructNodeUserData(node, ptr);

    storage.AddNode(node);

    if (definition->HasFlag(SourceNodeDefinition::DefinitionNodeFlag::IsInputNode))
        node->AddFlag(Node::NodeFlag::IsInputNode);
    if (definition->HasFlag(SourceNodeDefinition::DefinitionNodeFlag::IsOutputNode))
        node->AddFlag(Node::NodeFlag::IsOutputNode);

    if (definition->GetAutoParameters().size() > 0)
        node->AddFlag(Node::NodeFlag::IsDependent);

    return node;
}

void VCLG::GraphInstance::DestroyNode(Node* node) {
    switch (node->GetNodeClass()) {
        case Node::SourceNodeClass:
            DestroySourceNode((SourceNode*)node);
            break;
        default:
            abort();
            return;
    }
}

void VCLG::GraphInstance::DestroyConnection(Identity identity) {
    for (int i = 0; i < connections.size(); ++i) {
        if (connections[i].GetIdentity() == identity) {
            connections.erase(connections.begin() + i);
            break;
        }
    }
}

bool VCLG::GraphInstance::Connect(Identity portAIdentity, Identity portBIdentity) {
    Port* portA = storage.GetPortByIdentity(portAIdentity);
    Port* portB = storage.GetPortByIdentity(portBIdentity);
    return Connect(portA, portB);
}

bool VCLG::GraphInstance::Connect(Port* portA, Port* portB) {
    if (portA->GetKind() == portB->GetKind())
        return false;
    
    if (portA->GetKind() == Port::Input)
        return ConnectOutputToInput(portB, portA);
    else
        return ConnectOutputToInput(portA, portB);
}

void VCLG::GraphInstance::Reset() {
    connections.clear();
    llvm::SmallVector<Node*> nodes{ storage.GetNodes() };
    for (Node* node : nodes)
        DestroyNode(node);
    identityProvider.Reset();
}

void VCLG::GraphInstance::DestroyNodeConnections(Node* node) {
    int i = 0;
    while (i < connections.size()) {
        Connection& conn = connections[i];
        Port* inPort = GetPortByIdentity(conn.GetInputPortIdentity());
        Port* outPort = GetPortByIdentity(conn.GetOutputPortIdentity());
        if (inPort->GetOwner() == node->GetIdentity() || outPort->GetOwner() == node->GetIdentity()) {
            connections.erase(connections.begin() + i);
        } else {
            ++i;
        }
    }
}

void VCLG::GraphInstance::DestroySourceNode(SourceNode* node) {
    size_t nodeAdditionalDataSize = userDataTailAllocator->GetNodeUserDataAdditionalSize();
    size_t nodeTotalSize = sizeof(SourceNode) + nodeAdditionalDataSize;

    size_t portAdditionalDataSize = userDataTailAllocator->GetPortUserDataAdditionalSize();
    size_t portTotalSize = sizeof(Port) + portAdditionalDataSize;

    size_t parameterAdditionalDataSize = userDataTailAllocator->GetParameterUserDataAdditionalSize();
    size_t parameterTotalSize = sizeof(Parameter) + portAdditionalDataSize;

    DestroyNodeConnections(node);
    storage.RemoveNode(node);
    for (Port* port : node->GetInputs()) {
        storage.RemovePort(port);
        void* ptr = ((uint8_t*)port) + sizeof(Port);
        userDataTailAllocator->DestroyPortUserData(port, ptr);
        port->~Port();
        allocator->Deallocate(port, portTotalSize);
    }
    for (Port* port : node->GetOutputs()) {
        storage.RemovePort(port);
        void* ptr = ((uint8_t*)port) + sizeof(Port);
        userDataTailAllocator->DestroyPortUserData(port, ptr);
        port->~Port();
        allocator->Deallocate(port, portTotalSize);
    }
    for (Parameter* parameter : node->GetParameters()) {
        void* ptr = ((uint8_t*)parameter) + sizeof(Parameter);
        userDataTailAllocator->DestroyParameterUserData(parameter, ptr);
        parameter->~Parameter();
        allocator->Deallocate(parameter, parameterTotalSize);
    }
    void* ptr = ((uint8_t*)node) + sizeof(SourceNode);
    userDataTailAllocator->DestroyNodeUserData(node, ptr);
    node->~SourceNode();
    allocator->Deallocate(node, nodeTotalSize);
}

bool VCLG::GraphInstance::ConnectOutputToInput(Port* outPort, Port* inPort) {
    if (HasConnection(outPort, inPort))
        return true;
    
    for (Connection& conn : connections)
        if (conn.GetInputPortIdentity() == inPort->GetIdentity())
            return false;

    VCL::Type* outType = outPort->GetLastType();
    VCL::Type* inType = inPort->GetLastType();

    Identity connectionIdentity = identityProvider.Peek();
    connections.emplace_back(inPort->GetIdentity(), outPort->GetIdentity(), connectionIdentity);
    if (validator.Validate(*this)) {
        std::cout << "Validation OK" << std::endl;
        identityProvider.Next();
        return true;
    } else {
        std::cout << "Validation Failed" << std::endl;
        DestroyConnection(connectionIdentity);
        return false;
    }
}

bool VCLG::GraphInstance::HasConnection(Port* outPort, Port* inPort) {
    for (Connection& conn : connections)
        if (conn.GetInputPortIdentity() == inPort->GetIdentity() && conn.GetOutputPortIdentity() == outPort->GetIdentity())
            return true;
    return false;
}

bool VCLG::GraphInstance::CanBeConnected(VCL::Type* outType, VCL::Type* inType) {
    return VCL::Type::IsCanonicallyEqual(outType, inType);
}