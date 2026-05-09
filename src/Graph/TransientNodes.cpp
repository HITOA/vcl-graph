#include <VCLG/Graph/TransientNodes.hpp>

#include <VCLG/Graph/GraphInstance.hpp>

#include <VCL/AST/Decl.hpp>


VCLG::SubgraphOutputNode::~SubgraphOutputNode() {
    owner.DestroyPort(inPorts[0]);
}

void VCLG::SubgraphOutputNode::Initialize() {
    AddFlag(NodeFlag::IsOutputNode);
    VCL::ASTContext& context = owner.GetGraphContext().GetGlobalASTContext();
    VCL::IdentifierTable& identifierTable = owner.GetGraphContext().GetCompilerContext().GetIdentifierTable();
    VCL::Type* type = context.GetTypeCache().GetOrCreateBuiltinType(VCL::BuiltinType::Float32);
    VCL::TypeAliasDecl* aliasDecl = VCL::TypeAliasDecl::Create(context, identifierTable.Get("Generic"), type, VCL::SourceRange{});
    type = context.GetTypeCache().GetOrCreateTypeAliasType(type, aliasDecl);
    aliasDecl->SetType(type);
    Port* port = owner.InstantiatePort(GetIdentity(), type, "In", Port::PortKind::Input, nullptr, true);
    GetSubstitutionTable().SetTypeSubstitution(aliasDecl, nullptr);
    inPorts.push_back(port);
}

VCLG::SubgraphInputNode::~SubgraphInputNode() {
    owner.DestroyPort(outPorts[0]);
}

void VCLG::SubgraphInputNode::Initialize() {
    AddFlag(NodeFlag::IsInputNode);
    VCL::ASTContext& context = owner.GetGraphContext().GetGlobalASTContext();
    Port* port = owner.InstantiatePort(GetIdentity(), type, "Out", Port::PortKind::Output, nullptr, false);
    outPorts.push_back(port);
}