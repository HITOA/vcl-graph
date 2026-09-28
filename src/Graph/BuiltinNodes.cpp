#include <VCLG/Graph/BuiltinNodes.hpp>

#include <VCLG/Graph/GraphInstance.hpp>
#include <VCLG/Graph/Elaboration.hpp>

#include <VCL/AST/Decl.hpp>

#include <unordered_set>


void VCLG::SubgraphOutputNode::Initialize() {
    AddFlag(NodeFlag::IsOutputNode);
    AddFlag(NodeFlag::IsDependent);
    VCL::ASTContext& context = owner.GetGraphContext().GetGlobalASTContext();
    VCL::IdentifierTable& identifierTable = owner.GetGraphContext().GetCompilerContext().GetIdentifierTable();
    VCL::Type* type = context.GetTypeCache().GetOrCreateBuiltinType(VCL::BuiltinType::Float32);
    VCL::TypeAliasDecl* aliasDecl = VCL::TypeAliasDecl::Create(context, identifierTable.Get("Generic"), type, VCL::SourceRange{});
    type = context.GetTypeCache().GetOrCreateTypeAliasType(type, aliasDecl);
    aliasDecl->SetType(type);
    Port* port = owner.InstantiatePort(GetIdentity(), type, "In", Port::PortKind::Input, nullptr, true);
    inPorts.push_back(port);
}

void VCLG::SubgraphOutputNode::Destroy() {
    owner.DestroyPort(inPorts[0]);
}

void VCLG::SubgraphInputNode::Initialize() {
    AddFlag(NodeFlag::IsInputNode);
    Port* port = owner.InstantiatePort(GetIdentity(), type, "Out", Port::PortKind::Output, nullptr, false);
    outPorts.push_back(port);
}

void VCLG::SubgraphInputNode::Destroy() {
    owner.DestroyPort(outPorts[0]);
}

VCL::Type* VCLG::SubgraphInputNode::GetType() {
    return type;
}

void VCLG::SubgraphNode::Initialize() {

}

void VCLG::SubgraphNode::Destroy() {
    for (Port* port : outPorts)
        owner.DestroyPort(port);
    for (Port* port : inPorts)
        owner.DestroyPort(port);
}

void VCLG::SubgraphNode::SetGraph(std::shared_ptr<GraphInstance> instance) {
    this->instance = instance;
    Update();
}

VCLG::Port* VCLG::SubgraphNode::GetPortForNode(Identity innerNode) const {
    auto it = nodeToPort.find(innerNode);
    if (it == nodeToPort.end())
        return nullptr;
    return owner.GetPortByIdentity(it->second);
}

void VCLG::SubgraphNode::Update() {
    displayName = instance->GetName();
    ElaboratedGraph elaborated = Elaborate(*instance);
    auto outputType = [&](SubgraphOutputNode* outputNode) {
        Port* port = outputNode->GetInputs()[0];
        VCL::Type* type = elaborated.GetPortType(port);
        return type ? type : port->GetType();
    };
    
    outPorts.clear();
    inPorts.clear();

    std::unordered_set<Identity> visitedNode{};

    for (Node* node : instance->GetNodes()) {
        if (SubgraphOutputNode* outputNode = llvm::dyn_cast<SubgraphOutputNode>(node)) {
            if (!nodeToPort.count(outputNode->GetIdentity())) {
                Port* port = owner.InstantiatePort(
                    GetIdentity(), outputType(outputNode), outputNode->GetDisplayName().str(), Port::PortKind::Output, nullptr, false);
                nodeToPort.insert({ outputNode->GetIdentity(), port->GetIdentity() });
                outPorts.push_back(port);
            } else {
                Port* port = owner.GetPortByIdentity(nodeToPort.at(outputNode->GetIdentity()));
                if (port->GetType() != outputType(outputNode)) {
                    owner.DestroyPort(port);
                    port = owner.InstantiatePort(
                        GetIdentity(), outputType(outputNode), outputNode->GetDisplayName().str(), Port::PortKind::Output, nullptr, false);
                    nodeToPort[outputNode->GetIdentity()] = port->GetIdentity();
                }
                port->SetDisplayName(outputNode->GetDisplayName().str());
                outPorts.push_back(port);
            }
            visitedNode.insert(outputNode->GetIdentity());
        } else if (SubgraphInputNode* inputNode = llvm::dyn_cast<SubgraphInputNode>(node)) {
            if (!nodeToPort.count(inputNode->GetIdentity())) {
                Port* port = owner.InstantiatePort(
                    GetIdentity(), inputNode->GetType(), inputNode->GetDisplayName().str(), Port::PortKind::Input, nullptr, false);
                nodeToPort.insert({ inputNode->GetIdentity(), port->GetIdentity() });
                inPorts.push_back(port);
            } else {
                Port* port = owner.GetPortByIdentity(nodeToPort.at(inputNode->GetIdentity()));
                port->SetDisplayName(inputNode->GetDisplayName().str());
                inPorts.push_back(port);
            }
            visitedNode.insert(inputNode->GetIdentity());
        }
    }

    std::vector<Identity> toRemove{};

    for (auto& entry : nodeToPort) {
        if (visitedNode.count(entry.first))
            continue;
        Port* port = owner.GetPortByIdentity(entry.second);
        owner.DestroyPort(port);
        toRemove.push_back(entry.first);
    }

    for (Identity& identity : toRemove)
        nodeToPort.erase(identity);
}

void VCLG::FeedbackInputNode::Initialize() {
    AddFlag(NodeFlag::IsOutputNode);
    AddFlag(NodeFlag::IsDependent);
    VCL::ASTContext& context = owner.GetGraphContext().GetGlobalASTContext();
    VCL::IdentifierTable& identifierTable = owner.GetGraphContext().GetCompilerContext().GetIdentifierTable();
    VCL::Type* type = context.GetTypeCache().GetOrCreateBuiltinType(VCL::BuiltinType::Float32);
    VCL::TypeAliasDecl* aliasDecl = VCL::TypeAliasDecl::Create(context, identifierTable.Get("Generic"), type, VCL::SourceRange{});
    type = context.GetTypeCache().GetOrCreateTypeAliasType(type, aliasDecl);
    aliasDecl->SetType(type);
    Port* port = owner.InstantiatePort(GetIdentity(), type, "In", Port::PortKind::Input, nullptr, true);
    inPorts.push_back(port);
}

void VCLG::FeedbackInputNode::Destroy() {
    owner.DestroyPort(inPorts[0]);
}

void VCLG::FeedbackOutputNode::Initialize() {

}

void VCLG::FeedbackOutputNode::Destroy() {
    for (Port* port : outPorts)
        owner.DestroyPort(port);
}

void VCLG::FeedbackOutputNode::Update(Identity feedbackIdentity) {
    if (feedbackIdentity == INVALID_IDENTITY) {
        for (Port* port : outPorts)
            owner.DestroyPort(port);
        outPorts.clear();
        this->feedbackIdentity = feedbackIdentity;
        return;
    }

    VCLG::Node* node = owner.GetNodeByIdentity(feedbackIdentity);
    VCLG::FeedbackInputNode* feedbackNode = llvm::dyn_cast_or_null<VCLG::FeedbackInputNode>(node);
    if (!feedbackNode)
        return;

    displayName = feedbackNode->GetDisplayName();
    VCL::Type* feedbackType = Elaborate(owner).GetPortType(feedbackNode->GetInputs()[0]);
    if (!feedbackType)
        feedbackType = feedbackNode->GetInputs()[0]->GetType();
    if (this->feedbackIdentity == feedbackNode->GetIdentity()) {
        Port* port = GetOutputs()[0];
        if (port->GetType() != feedbackType) {
            outPorts[0] = owner.OverwritePort(
                port, GetIdentity(), feedbackType, "Out", Port::PortKind::Output, nullptr, false);
        }
    } else {
        if (!GetOutputs().empty()) {
            Port* port = GetOutputs()[0];
            outPorts[0] = owner.OverwritePort(
                    port, GetIdentity(), feedbackType, "Out", Port::PortKind::Output, nullptr, false);
        } else {
            Port* port = owner.InstantiatePort(
                GetIdentity(), feedbackType, "Out", Port::PortKind::Output, nullptr, false);
            outPorts.push_back(port);
        }
        this->feedbackIdentity = feedbackNode->GetIdentity();
    }
}
