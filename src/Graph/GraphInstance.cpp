#include <VCLG/Graph/GraphInstance.hpp>

#include <VCLG/Graph/GraphContext.hpp>
#include <VCLG/Graph/Port.hpp>
#include <VCLG/Graph/Parameter.hpp>
#include <VCLG/Graph/BuiltinNodes.hpp>
#include <VCLG/Graph/Elaboration.hpp>
#include <VCLG/Graph/Converter.hpp>

#include <VCL/AST/ConstantValue.hpp>
#include <VCL/AST/Expr.hpp>
#include <VCL/AST/Template.hpp>
#include <VCL/Frontend/CompilerInstance.hpp>

#include <iostream>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>


// The value a port or parameter starts with, for the user to edit: a copy of its scalar initializer,
// or zero when it has no initializer and a scalar (or vector of scalars) type. None otherwise.
// Ports also accept lanes of scalars (`throughLanes`); parameters don't.
static std::optional<VCL::ConstantScalar> GetDefaultInitializerOverride(VCL::Type* type, VCL::ConstantValue* initializer,
        bool throughLanes) {
    if (initializer) {
        if (initializer->GetConstantValueClass() != VCL::ConstantValue::ConstantScalarClass)
            return std::nullopt;
        return *(VCL::ConstantScalar*)initializer;
    }

    type = VCL::Type::GetCanonicalType(type);
    if (type->GetTypeClass() == VCL::Type::VectorTypeClass)
        type = ((VCL::VectorType*)type)->GetElementType().GetType();
    if (throughLanes && type->GetTypeClass() == VCL::Type::LanesTypeClass)
        type = ((VCL::LanesType*)type)->GetElementType().GetType();
    if (type->GetTypeClass() != VCL::Type::BuiltinTypeClass)
        return std::nullopt;

    VCL::BuiltinType::Kind kind = ((VCL::BuiltinType*)type)->GetKind();
    if (VCL::BuiltinType::GetKindCategory(kind) == VCL::BuiltinType::Category::VoidKind)
        return std::nullopt;
    return VCL::ConstantScalar{ kind };
}

VCLG::GraphInstance::GraphInstance(GraphContext& graphContext, Identity identity) :
    graphContext{ graphContext }, identity{ identity }, identityProvider{ }, storage{}, name{ "New Graph" } {
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

VCLG::Connection* VCLG::GraphInstance::FindConnectionByIdentity(Identity identity) {
    for (int i = 0; i < connections.size(); ++i) {
        Connection& conn = connections[i];
        if (conn.GetIdentity() == identity)
            return &conn;
    }
    return nullptr;
}

VCLG::SourceNode* VCLG::GraphInstance::InstantiateSourceNode(VCL::Source* source) {
    SourceNodeDefinition* definition = graphContext.GetDefinitionRegistry().GetOrCreateSourceNodeDefinition(source);
    if (!definition)
        return nullptr;

    Identity instancedNodeIdentity = identityProvider.Next();
    
    llvm::SmallVector<Port*> inPorts;
    llvm::SmallVector<Port*> outPorts;
    llvm::SmallVector<Parameter*> parameters;

    for (const SourcePortDefinition& port : definition->GetPorts()) {
        Port::PortKind kind = Port::PortKind::Input;
        if (!port.IsInput())
            kind = Port::PortKind::Output;

        VCL::ConstantValue* initializer = nullptr;
        if (port.GetDecl()->GetInitializer())
            initializer = port.GetDecl()->GetInitializer()->GetConstantValue();

        Port* instancedPort = InstantiatePort(instancedNodeIdentity, port.GetDecl()->GetValueType().GetType(), 
            port.GetDisplayName(), kind, initializer, port.IsDependent());
        
        if (kind == Port::Input)
            inPorts.push_back(instancedPort);
        else
            outPorts.push_back(instancedPort);
    }

    for (const SourceParameterDefinition& parameter : definition->GetParameters()) {
        VCL::ConstantValue* initializer = nullptr;
        if (parameter.GetDecl()->GetInitializer())
            initializer = parameter.GetDecl()->GetInitializer()->GetConstantValue();

        Parameter* instancedParameter = InstantiateParameter(instancedNodeIdentity, 
            parameter.GetDecl()->GetValueType().GetType(), parameter.GetDisplayName(), initializer);

        parameters.push_back(instancedParameter);
    }

    std::unique_ptr<SourceNode> ownedNode = std::make_unique<SourceNode>(
        source->GetBufferIdentifier().str(), definition->GetDisplayName().str(), 
        inPorts, outPorts, parameters, instancedNodeIdentity);
    SourceNode* node = ownedNode.get();

    storage.AddNode(std::move(ownedNode));

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
    return CreatePort(identityProvider.Next(), owner, type, displayName, kind, initializer, isDependent);
}

void VCLG::GraphInstance::DestroyPort(Port* port) {
    DestroyAllPortConnections(port->GetIdentity());
    storage.RemovePort(port);
}

VCLG::Port* VCLG::GraphInstance::OverwritePort(Port* port, Identity owner, VCL::Type* type, const std::string& displayName, 
        Port::PortKind kind, VCL::ConstantValue* initializer, bool isDependent) {
    Identity identity = port->GetIdentity();
    DestroyPort(port);
    return CreatePort(identity, owner, type, displayName, kind, initializer, isDependent); // `port` was destroyed above
}

VCLG::Parameter* VCLG::GraphInstance::InstantiateParameter(Identity owner, VCL::Type* type, const std::string& displayName, VCL::ConstantValue* initializer) {
    std::unique_ptr<Parameter> parameter = std::make_unique<Parameter>(owner, type, displayName, initializer, identityProvider.Next());
    if (auto value = GetDefaultInitializerOverride(type, initializer, false))
        parameter->SetInitializerOverride(*value);
    return storage.AddParameter(std::move(parameter));
}

void VCLG::GraphInstance::DestroyParameter(Parameter* parameter) {
    storage.RemoveParameter(parameter);
}

void VCLG::GraphInstance::DestroyNode(Node* node) {
    if (SourceNode* sourceNode = llvm::dyn_cast<SourceNode>(node))
        DestroySourceNode(sourceNode);
    else
        DestroyBuiltinNode(llvm::cast<BuiltinNode>(node));
}

void VCLG::GraphInstance::DestroyConnection(Identity identity) {
    for (int i = 0; i < connections.size(); ++i) {
        if (connections[i].GetIdentity() == identity) {
            connections.erase(connections.begin() + i);
            break;
        }
    }
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
    DestroyNodeConnections(node);
    std::unique_ptr<Node> owned = storage.RemoveNode(node);
    for (Port* port : node->GetInputs())
        DestroyPort(port);
    for (Port* port : node->GetOutputs())
        DestroyPort(port);
    for (Parameter* parameter : node->GetParameters())
        DestroyParameter(parameter);
}

void VCLG::GraphInstance::DestroyBuiltinNode(BuiltinNode* node) {
    DestroyNodeConnections(node);
    std::unique_ptr<Node> owned = storage.RemoveNode(node);
    node->Destroy();
}

VCLG::Port* VCLG::GraphInstance::CreatePort(Identity identity, Identity owner, VCL::Type* type, const std::string& displayName, 
        Port::PortKind kind, VCL::ConstantValue* initializer, bool isDependent) {
    std::unique_ptr<Port> port = std::make_unique<Port>(owner, type, displayName, kind, initializer, isDependent, identity);
    if (auto value = GetDefaultInitializerOverride(type, initializer, true))
        port->SetInitializerOverride(*value);
    return storage.AddPort(std::move(port));
}

VCLG::Identity VCLG::GraphInstance::ConnectOutputToInput(Port* outPort, Port* inPort) {
    if (Identity identity = HasConnection(outPort, inPort); identity != INVALID_IDENTITY)
        return identity;
    
    for (Connection& conn : connections)
        if (conn.GetInputPortIdentity() == inPort->GetIdentity())
            return INVALID_IDENTITY;

    // Ordinary connections must stay acyclic: loops go through Feedback Input/Output nodes.
    if (WouldCreateCycle(outPort, inPort))
        return INVALID_IDENTITY;

    // The output's type as currently inferred (its declared type while unresolved).
    VCL::Type* outType = Elaborate(*this).GetPortType(outPort);
    if (!outType)
        outType = outPort->GetType();

    if (!outPort->IsDependent() || !inPort->IsDependent()) {
        for (Converter* converter : graphContext.GetConverters()) {
            if (converter->Convertible(outType, inPort->GetType())) {
                Identity connectionIdentity = identityProvider.Next();
                connections.emplace_back(inPort->GetIdentity(), outPort->GetIdentity(), connectionIdentity, converter);
                return connectionIdentity;
            }
        }
    }

    if (outPort->IsDependent() || inPort->IsDependent()) {
        // Accepted if the types can still be inferred with the connection.
        Identity connectionIdentity = identityProvider.Peek();
        connections.emplace_back(inPort->GetIdentity(), outPort->GetIdentity(), connectionIdentity, nullptr);
        if (Elaborate(*this).Succeeded()) {
            identityProvider.Next();
            return connectionIdentity;
        }
        connections.pop_back();
        return INVALID_IDENTITY;
    }

    if (VCL::Type::IsCanonicallyEqual(outType, inPort->GetType())) {
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

bool VCLG::GraphInstance::WouldCreateCycle(Port* outPort, Port* inPort) const {
    // The new edge goes producer -> consumer. It closes a cycle if the producer already depends,
    // directly or not, on the consumer.
    Node* producer = GetNodeByIdentity(outPort->GetOwner());
    Node* consumer = GetNodeByIdentity(inPort->GetOwner());
    if (producer == consumer)
        return true;

    std::unordered_map<Port*, Node*> inPortToSource{};
    for (const Connection& connection : connections) {
        Port* connectionOutPort = GetPortByIdentity(connection.GetOutputPortIdentity());
        inPortToSource[GetPortByIdentity(connection.GetInputPortIdentity())] = GetNodeByIdentity(connectionOutPort->GetOwner());
    }

    std::unordered_set<Node*> visited{ producer };
    std::vector<Node*> toVisit{ producer };
    while (!toVisit.empty()) {
        Node* node = toVisit.back();
        toVisit.pop_back();
        for (Port* nodeInPort : node->GetInputs()) {
            auto it = inPortToSource.find(nodeInPort);
            if (it == inPortToSource.end())
                continue;
            if (it->second == consumer)
                return true;
            if (visited.insert(it->second).second)
                toVisit.push_back(it->second);
        }
    }
    return false;
}

bool VCLG::GraphInstance::BuildExecutionOrder(llvm::ArrayRef<Node*> roots, std::vector<Node*>& order) const {
    order.clear();

    std::unordered_map<Port*, Node*> inPortToSource{};
    for (const Connection& connection : connections) {
        Port* outPort = GetPortByIdentity(connection.GetOutputPortIdentity());
        inPortToSource[GetPortByIdentity(connection.GetInputPortIdentity())] = GetNodeByIdentity(outPort->GetOwner());
    }

    // A Feedback Output shares its Feedback Input's value and must be emitted first. Feedback
    // Outputs have no inputs, so this ordering edge can never be part of a cycle.
    std::unordered_map<Identity, std::vector<Node*>> feedbackReaders{};
    for (Node* node : GetNodes()) {
        FeedbackOutputNode* feedbackOutputNode = llvm::dyn_cast<FeedbackOutputNode>(node);
        if (!feedbackOutputNode)
            continue;
        Identity feedbackIdentity = feedbackOutputNode->GetFeedbackIdentity();
        if (feedbackIdentity != INVALID_IDENTITY)
            feedbackReaders[feedbackIdentity].push_back(node);
    }

    enum class VisitState : uint8_t { Unvisited, InProgress, Done };
    std::unordered_map<Node*, VisitState> states{};

    // Depth-first, post-order: each node is appended once all its dependencies are.
    std::function<bool(Node*)> visit = [&](Node* node) -> bool {
        VisitState& state = states[node]; // references into an unordered_map survive rehashing
        if (state == VisitState::Done)
            return true;
        if (state == VisitState::InProgress)
            return false;
        state = VisitState::InProgress;

        for (Port* inPort : node->GetInputs()) {
            auto it = inPortToSource.find(inPort);
            if (it != inPortToSource.end() && !visit(it->second))
                return false;
        }
        if (auto it = feedbackReaders.find(node->GetIdentity()); it != feedbackReaders.end()) {
            for (Node* reader : it->second)
                if (!visit(reader))
                    return false;
        }

        state = VisitState::Done;
        order.push_back(node);
        return true;
    };

    for (Node* root : roots)
        if (!visit(root))
            return false;
    return true;
}
