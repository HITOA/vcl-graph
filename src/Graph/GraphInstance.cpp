#include <VCLG/Graph/GraphInstance.hpp>

#include <VCLG/Graph/GraphContext.hpp>


VCLG::GraphInstance::GraphInstance(GraphContext& graphContext, std::unique_ptr<Allocator> allocator) :
    graphContext{ graphContext }, allocator{ std::move(allocator) }, identityProvider{ }, storage{} {

}

VCLG::GraphInstance::~GraphInstance() {
    Reset();
}

VCLG::SourceNode* VCLG::GraphInstance::InstantiateSourceNode(VCL::Source* source) {
    SourceNodeDefinition* definition = graphContext.GetDefinitionRegistry().GetOrCreateSourceNodeDefinition(source);
    if (!definition)
        return nullptr;

    Identity instancedNodeIdentity = identityProvider.Next();
    
    llvm::SmallVector<Port*> inPorts;
    llvm::SmallVector<Port*> outPorts;

    for (SourcePortDefinition* port : definition->GetPorts()) {
        Port::PortKind kind = Port::PortKind::Input;
        if (!port->IsInput())
            kind = Port::PortKind::Output;

        Identity instancedPortIdentity = identityProvider.Next();
        Port* instancedPort = (Port*)allocator->Allocate(sizeof(Port), 4);
        new (instancedPort) Port{ instancedNodeIdentity, port->GetDecl()->GetValueType().GetType(), port->GetDisplayName(), kind, instancedPortIdentity };

        storage.AddPort(instancedPort);

        if (kind == Port::Input)
            inPorts.push_back(instancedPort);
        else
            outPorts.push_back(instancedPort);
    }

    SourceNode* node = (SourceNode*)allocator->Allocate(sizeof(SourceNode), 4);
    new (node) SourceNode{ source->GetBufferIdentifier().str(), inPorts, outPorts, instancedNodeIdentity };
    storage.AddNode(node);
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

void VCLG::GraphInstance::DestroySourceNode(SourceNode* node) {
    storage.RemoveNode(node);
    for (Port* port : node->GetInputs()) {
        storage.RemovePort(port);
        allocator->Deallocate(port, sizeof(Port));
    }
    for (Port* port : node->GetOutputs()) {
        storage.RemovePort(port);
        allocator->Deallocate(port, sizeof(Port));
    }
    allocator->Deallocate(node, sizeof(SourceNode));
}

bool VCLG::GraphInstance::ConnectOutputToInput(Port* outPort, Port* inPort) {
    if (HasConnection(outPort, inPort))
        return true;
    
    for (Connection& conn : connections)
        if (conn.GetInputPortIdentity() == inPort->GetIdentity())
            return false;

    VCL::Type* outType = outPort->GetType();
    VCL::Type* inType = inPort->GetType();

    if (!CanBeConnected(outType, inType))
        return false;

    Identity connectionIdentity = identityProvider.Next();
    connections.emplace_back(inPort->GetIdentity(), outPort->GetIdentity(), connectionIdentity);
    return true;
}

bool VCLG::GraphInstance::HasConnection(Port* outPort, Port* inPort) {
    for (Connection& conn : connections)
        if (conn.GetInputPortIdentity() == inPort->GetIdentity() && conn.GetOutputPortIdentity() == outPort->GetIdentity())
            return true;
    return false;
}

bool VCLG::GraphInstance::CanBeConnected(VCL::Type* outType, VCL::Type* inType) {
    return outType == inType;
}