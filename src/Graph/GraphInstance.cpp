#include <VCLG/Graph/GraphInstance.hpp>

#include <VCLG/Graph/GraphContext.hpp>
#include <VCLG/Graph/Port.hpp>
#include <VCLG/Graph/Parameter.hpp>

#include <VCL/AST/ConstantValue.hpp>
#include <VCL/AST/Expr.hpp>
#include <VCL/AST/Template.hpp>
#include <VCL/Frontend/CompilerInstance.hpp>

#include <iostream>


VCLG::GraphInstance::GraphInstance(GraphContext& graphContext, Identity identity,
    std::shared_ptr<GraphUserDataTrailAllocator> userDataTailAllocator,
    std::unique_ptr<Allocator> allocator) :
    graphContext{ graphContext }, identity{ identity }, validator{}, allocator{ std::move(allocator) }, identityProvider{ }, storage{},
    userDataTailAllocator{ userDataTailAllocator }, name{ "New Graph" } {
}

VCLG::GraphInstance::~GraphInstance() {
    Reset();
}

VCLG::Connection* VCLG::GraphInstance::FindConnectionByPort(Port* outPort, Port* inPort) {
    for (int i = 0; i < connections.size(); ++i) {
        Connection& conn = connections[i];
        Port* currentInPort = GetPortByIdentity(conn.GetInputPortIdentity());
        Port* currentOutPort = GetPortByIdentity(conn.GetOutputPortIdentity());
        if (outPort == currentOutPort && inPort == currentInPort)
            return &conn;
    }
    return nullptr;
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
    size_t parameterTotalSize = sizeof(Parameter) + parameterAdditionalDataSize;

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

        Port* instancedPort = InstantiatePort(instancedNodeIdentity, port->GetDecl()->GetValueType().GetType(), 
            port->GetDisplayName(), kind, initializer, port->IsDependent());
        
        if (kind == Port::Input)
            inPorts.push_back(instancedPort);
        else
            outPorts.push_back(instancedPort);
    }

    for (SourceParameterDefinition* parameter : definition->GetParameters()) {
        VCL::ConstantValue* initializer = nullptr;
        if (parameter->GetDecl()->GetInitializer())
            initializer = parameter->GetDecl()->GetInitializer()->GetConstantValue();

        Parameter* instancedParameter = InstantiateParameter(instancedNodeIdentity, 
            parameter->GetDecl()->GetValueType().GetType(), parameter->GetDisplayName(), initializer);

        parameters.push_back(instancedParameter);
    }

    SourceNode* node = (SourceNode*)allocator->Allocate(nodeTotalSize, 8);
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

VCLG::Port* VCLG::GraphInstance::InstantiatePort(Identity owner, VCL::Type* type, const std::string& displayName, 
        Port::PortKind kind, VCL::ConstantValue* initializer, bool isDependent) {

    size_t portAdditionalDataSize = userDataTailAllocator->GetPortUserDataAdditionalSize();
    size_t portTotalSize = sizeof(Port) + portAdditionalDataSize;

    Identity instancedPortIdentity = identityProvider.Next();
    Port* port = (Port*)allocator->Allocate(portTotalSize, 8);
    new (port) Port{ owner, type, displayName, kind, initializer, isDependent, instancedPortIdentity };
    void* ptr = ((uint8_t*)port) + sizeof(Port);
    userDataTailAllocator->ConstructPortUserData(port, ptr);
    storage.AddPort(port);

    return port;
}

void VCLG::GraphInstance::DestroyPort(Port* port) {
    DestroyAllPortConnections(port->GetIdentity());
    size_t portAdditionalDataSize = userDataTailAllocator->GetPortUserDataAdditionalSize();
    size_t portTotalSize = sizeof(Port) + portAdditionalDataSize;
    storage.RemovePort(port);
    void* ptr = ((uint8_t*)port) + sizeof(Port);
    userDataTailAllocator->DestroyPortUserData(port, ptr);
    port->~Port();
    allocator->Deallocate(port, portTotalSize);
}

VCLG::Parameter* VCLG::GraphInstance::InstantiateParameter(Identity owner, VCL::Type* type, const std::string& displayName, VCL::ConstantValue* initializer) {
    size_t parameterAdditionalDataSize = userDataTailAllocator->GetParameterUserDataAdditionalSize();
    size_t parameterTotalSize = sizeof(Parameter) + parameterAdditionalDataSize;
    
    Identity instancedParameterIdentity = identityProvider.Next();
    Parameter* instancedParameter = (Parameter*)allocator->Allocate(parameterTotalSize, 8);
    new (instancedParameter) Parameter{ 
        owner, type, displayName, 
        initializer, instancedParameterIdentity };
    
    void* ptr = ((uint8_t*)instancedParameter) + sizeof(Parameter);
    userDataTailAllocator->ConstructParameterUserData(instancedParameter, ptr);
    storage.AddParameter(instancedParameter);

    return instancedParameter;
}

void VCLG::GraphInstance::DestroyParameter(Parameter* parameter) {
    size_t parameterAdditionalDataSize = userDataTailAllocator->GetParameterUserDataAdditionalSize();
    size_t parameterTotalSize = sizeof(Parameter) + parameterAdditionalDataSize;
    
    storage.RemoveParameter(parameter);
    void* ptr = ((uint8_t*)parameter) + sizeof(Parameter);
    userDataTailAllocator->DestroyParameterUserData(parameter, ptr);
    parameter->~Parameter();
    allocator->Deallocate(parameter, parameterAdditionalDataSize);
}

void VCLG::GraphInstance::DestroyNode(Node* node) {
    switch (node->GetNodeClass()) {
        case Node::SourceNodeClass:
            DestroySourceNode((SourceNode*)node);
            break;
        case Node::TransientNodeClass:
            DestroyTransientNode((TransientNode*)node);
            break;
        default:
            abort();
            return;
    }
}

void VCLG::GraphInstance::DestroyConnection(Identity identity) {
    for (int i = 0; i < connections.size(); ++i) {
        if (connections[i].GetIdentity() == identity) {
            Connection& conn = connections[i];
            Port* inPort = GetPortByIdentity(conn.GetInputPortIdentity());
            Port* outPort = GetPortByIdentity(conn.GetOutputPortIdentity());
            if (conn.GetConverter() != nullptr)
                conn.GetConverter()->OnLinkDestroyed(outPort, inPort);
            connections.erase(connections.begin() + i);
            break;
        }
    }
    validator.Validate(*this);
}

void VCLG::GraphInstance::DestroyAllPortConnections(Identity identity) {
    int i = 0;
    while (i < connections.size()) {
        const Connection& connection = connections[i];
        if (connection.GetInputPortIdentity() == identity || connection.GetOutputPortIdentity() == identity) {
            DestroyConnection(connection.GetIdentity());
        } else {
            ++i;
        }
    }
}

VCLG::Identity VCLG::GraphInstance::Connect(Identity portAIdentity, Identity portBIdentity) {
    Port* portA = storage.GetPortByIdentity(portAIdentity);
    Port* portB = storage.GetPortByIdentity(portBIdentity);
    return Connect(portA, portB);
}

VCLG::Identity VCLG::GraphInstance::Connect(Port* portA, Port* portB) {
    if (portA->GetKind() == portB->GetKind())
        return INVALID_IDENTITY;
    
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
            DestroyConnection(conn.GetIdentity());
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
    for (Port* port : node->GetInputs())
        DestroyPort(port);
    for (Port* port : node->GetOutputs())
        DestroyPort(port);
    for (Parameter* parameter : node->GetParameters())
        DestroyParameter(parameter);
    void* ptr = ((uint8_t*)node) + sizeof(SourceNode);
    userDataTailAllocator->DestroyNodeUserData(node, ptr);
    node->~SourceNode();
    allocator->Deallocate(node, nodeTotalSize);
}

void VCLG::GraphInstance::DestroyTransientNode(TransientNode* node) {
    size_t portAdditionalDataSize = userDataTailAllocator->GetPortUserDataAdditionalSize();
    size_t portTotalSize = sizeof(Port) + portAdditionalDataSize;

    size_t nodeAdditionalDataSize = userDataTailAllocator->GetNodeUserDataAdditionalSize();
    size_t nodeTotalSize = node->GetSize() + nodeAdditionalDataSize;

    DestroyNodeConnections(node);
    storage.RemoveNode(node);
    void* ptr = ((uint8_t*)node) + node->GetSize();
    userDataTailAllocator->DestroyNodeUserData(node, ptr);
    node->Destroy();
    node->~TransientNode();
    allocator->Deallocate(node, nodeTotalSize);
}

VCLG::Identity VCLG::GraphInstance::ConnectOutputToInput(Port* outPort, Port* inPort) {
    if (Identity identity = HasConnection(outPort, inPort); identity != INVALID_IDENTITY)
        return identity;
    
    for (Connection& conn : connections)
        if (conn.GetInputPortIdentity() == inPort->GetIdentity())
            return INVALID_IDENTITY;

    VCL::Type* outType = outPort->GetLastType();
    VCL::Type* inType = inPort->GetLastType();

    if ((!outPort->IsDependent() && inPort->IsDependent()) 
        || (outPort->IsDependent() && !inPort->IsDependent())
        || (!outPort->IsDependent() && !inPort->IsDependent())) {
        for (Converter* converter : graphContext.GetConverters()) {
            if (converter->Convertible(outPort, inPort)) {
                Identity connectionIdentity = identityProvider.Next();
                connections.emplace_back(inPort->GetIdentity(), outPort->GetIdentity(), connectionIdentity, converter);
                converter->OnLinkCreated(outPort, inPort);
                return connectionIdentity;
            }
        }
    } if (outPort->IsDependent() || inPort->IsDependent()) {
        Identity connectionIdentity = identityProvider.Peek();
        connections.emplace_back(inPort->GetIdentity(), outPort->GetIdentity(), connectionIdentity, nullptr);
        if (validator.Validate(*this)) {
            identityProvider.Next();
            return connectionIdentity;
        } else {
            for (int i = 0; i < connections.size(); ++i) {
                if (connections[i].GetIdentity() == connectionIdentity) {
                    connections.erase(connections.begin() + i);
                    break;
                }
            }
            return INVALID_IDENTITY;
        }
    } else if (VCL::Type::IsCanonicallyEqual(outType, inType)) {
        Identity connectionIdentity = identityProvider.Next();
        connections.emplace_back(inPort->GetIdentity(), outPort->GetIdentity(), connectionIdentity, nullptr);
        return connectionIdentity;
    }

    return INVALID_IDENTITY;
}

VCLG::Identity VCLG::GraphInstance::HasConnection(Port* outPort, Port* inPort) {
    for (Connection& conn : connections)
        if (conn.GetInputPortIdentity() == inPort->GetIdentity() && conn.GetOutputPortIdentity() == outPort->GetIdentity())
            return conn.GetIdentity();
    return INVALID_IDENTITY;
}

bool VCLG::GraphInstance::CanBeConnected(VCL::Type* outType, VCL::Type* inType) {
    return VCL::Type::IsCanonicallyEqual(outType, inType);
}