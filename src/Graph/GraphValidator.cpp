#include <VCLG/Graph/GraphValidator.hpp>

#include <VCLG/Graph/GraphContext.hpp>
#include <VCLG/Graph/GraphInstance.hpp>

#include <VCL/Core/SourceManager.hpp>
#include <VCL/AST/Type.hpp>
#include <VCL/AST/TypePrinter.hpp>

#include <llvm/ADT/DenseMap.h>

#include <unordered_set>
#include <queue>
#include <iostream>


VCLG::GraphValidator::GraphValidator() : inPortToOutPort{}, connectedOutPort{} {

}

bool VCLG::GraphValidator::Validate(GraphInstance& graph) {
    ClearSubstitutionTable(graph);
    std::vector<Node*> dependentNodes = BuildOrderedDependentNodeList(graph);
    VCL::ASTContext& globalASTContext = graph.GetGraphContext().GetGlobalASTContext();
    
    for (Node* node : dependentNodes) {
        for (Port* inPort : Node::GetNodeInputs(node)) {
            if (!inPort->IsDependent())
                continue;
            inPort->SetTentativeType(nullptr);
        }
        for (Port* outPort : Node::GetNodeOutputs(node)) {
            if (!outPort->IsDependent())
                continue;
            outPort->SetTentativeType(nullptr);
        }
    }

    for (Node* node : dependentNodes) {
        for (Port* inPort : Node::GetNodeInputs(node)) {
            if (inPortToOutPort.count(inPort)) {
                Port* outPort = inPortToOutPort[inPort];
                if (outPort->IsDependent() && outPort->GetTentativeType() == nullptr)
                    continue;
                if (!SubstituteType(node, inPort->GetType(), outPort->GetLastTentativeType()))
                    return false;
            }
        }
        for (Port* outPort : Node::GetNodeOutputs(node)) {
            if (connectedOutPort.count(outPort)) {
                for (const Connection& connection : graph.GetConnections()) {
                    Port* inPort = graph.GetPortByIdentity(connection.GetInputPortIdentity());
                    if (inPort->IsDependent() && inPort->GetTentativeType() == nullptr)
                        continue;
                    if (outPort == graph.GetPortByIdentity(connection.GetOutputPortIdentity())) {
                        if (!SubstituteType(node, outPort->GetType(), inPort->GetLastTentativeType()))
                            return false;
                    }
                }
            }
        }

        for (Port* inPort : Node::GetNodeInputs(node)) {
            if (!inPort->IsDependent())
                continue;
            inPort->SetTentativeType(GenerateSubstitutedType(globalASTContext, node, inPort->GetType()));
        }
        for (Port* outPort : Node::GetNodeOutputs(node)) {
            if (!outPort->IsDependent())
                continue;
            outPort->SetTentativeType(GenerateSubstitutedType(globalASTContext, node, outPort->GetType()));
        }
    }
    
    for (Node* node : dependentNodes) {
        for (Port* inPort : Node::GetNodeInputs(node)) {
            if (!inPort->IsDependent())
                continue;
            inPort->SetSubstitutedType(inPort->GetTentativeType());
        }
        for (Port* outPort : Node::GetNodeOutputs(node)) {
            if (!outPort->IsDependent())
                continue;
            outPort->SetSubstitutedType(outPort->GetTentativeType());
        }
    }

    return true;
}

void VCLG::GraphValidator::ClearSubstitutionTable(GraphInstance& graph) {
    allocator.Reset();
    for (Node* node : graph.GetNodes()) {
        for (auto param : node->GetSubstitutionTable()) {
            if (param.first->GetDeclClass() == VCL::Decl::TypeAliasDeclClass) {
                node->GetSubstitutionTable().SetTypeSubstitution((VCL::TypeAliasDecl*)param.first, nullptr);
            } else {
                node->GetSubstitutionTable().SetScalarSubstitution((VCL::VarDecl*)param.first, nullptr);
            }
        }
    }
}

std::vector<VCLG::Node*> VCLG::GraphValidator::BuildOrderedDependentNodeList(GraphInstance& graph) {
    inPortToOutPort.clear();
    connectedOutPort.clear();

    for (const Connection& connection : graph.GetConnections()) {
        Port* inPort = graph.GetPortByIdentity(connection.GetInputPortIdentity());
        Port* outPort = graph.GetPortByIdentity(connection.GetOutputPortIdentity());
        inPortToOutPort.insert({ inPort, outPort });
        connectedOutPort.insert({ outPort });
    }

    std::vector<Node*> nodes{};

    std::queue<Node*> nodeToVisite{};

    for (Node* node : graph.GetNodes()) {
        bool isStubBack = true;
        for (Port* outPort : Node::GetNodeOutputs(node)) {
            if (connectedOutPort.count(outPort))
                isStubBack = false;
        }

        if (isStubBack)
            nodeToVisite.push(node);
    }
    
    while (!nodeToVisite.empty()) {
        Node* currentNode = nodeToVisite.front();
        nodeToVisite.pop();

        nodes.push_back(currentNode);

        for (Port* inPort : Node::GetNodeInputs(currentNode)) {
            if (!inPortToOutPort.count(inPort))
                continue;
            Port* connectedPort = inPortToOutPort[inPort];
            Node* connectedNode = graph.GetNodeByIdentity(connectedPort->GetOwner());
            nodeToVisite.push(connectedNode);
        }
    }

    int32_t size = (int32_t)nodes.size();
    for (int32_t i = size - 1; i >= 0; --i)
        nodes.push_back(nodes[i]);

    return std::move(nodes);
}

bool VCLG::GraphValidator::SubstituteType(Node* node, VCL::Type* baseType, VCL::Type* connectedType) {
    SubstitutionTable& table = node->GetSubstitutionTable();
    switch (baseType->GetTypeClass()) {
        case VCL::Type::TypeAliasTypeClass: {
            VCL::TypeAliasType* aliasType = (VCL::TypeAliasType*)baseType;
            VCL::TypeAliasDecl* aliasDecl = aliasType->GetDecl();
            if (!table.HasDecl(aliasDecl))
                return true;
            VCL::Type* substitutedType = table.GetTypeSubstitution(aliasDecl);
            if (substitutedType != nullptr && VCL::Type::IsCanonicallyEqual(substitutedType, connectedType))
                return true;
            else if (substitutedType != nullptr)
                return false;
            table.SetTypeSubstitution(aliasDecl, connectedType);
            return true;
        }
        case VCL::Type::TemplateSpecializationTypeClass: {
            if (connectedType->GetTypeClass() != VCL::Type::TemplateSpecializationTypeClass)
                return false;
            VCL::TemplateSpecializationType* speType = (VCL::TemplateSpecializationType*)baseType;
            VCL::TemplateSpecializationType* speTypeConnected = (VCL::TemplateSpecializationType*)connectedType;
            if (speType->GetTemplateDecl() != speTypeConnected->GetTemplateDecl())
                return false;
            VCL::TemplateArgumentList* argList = speType->GetTemplateArgumentList();
            VCL::TemplateArgumentList* argListConnected = speTypeConnected->GetTemplateArgumentList();
            for (size_t i = 0; i < argList->GetCount(); ++i) {
                const VCL::TemplateArgument& arg = argList->GetArgs()[i];
                const VCL::TemplateArgument& argConnected = argListConnected->GetArgs()[i];
                if (arg.GetKind() == VCL::TemplateArgument::Type) {
                    if (!SubstituteType(node, arg.GetType().GetType(), argConnected.GetType().GetType()))
                        return false;
                } else if (arg.GetKind() == VCL::TemplateArgument::Expression) {
                    if (arg.GetExpr()->GetExprClass() != VCL::Expr::DeclRefExprClass)
                        continue;
                    VCL::DeclRefExpr* declRefExpr = (VCL::DeclRefExpr*)arg.GetExpr();
                    VCL::ConstantScalar* scalar = nullptr;
                    if (argConnected.GetKind() == VCL::TemplateArgument::Integral) {
                        scalar = allocator.Allocate<VCL::ConstantScalar>(1);
                        *scalar = argConnected.GetIntegral();
                    } else if (argConnected.GetKind() == VCL::TemplateArgument::Expression) {
                        VCL::ConstantValue* value = argConnected.GetExpr()->GetConstantValue();
                        scalar = (VCL::ConstantScalar*)value;
                    }
                    if (!SubstituteExpression(node, declRefExpr, scalar))
                        return false;
                }
            }
            return true;
        }
        default:
            return VCL::Type::IsCanonicallyEqual(baseType, connectedType);
    }
}

bool VCLG::GraphValidator::SubstituteExpression(Node* node, VCL::DeclRefExpr* baseExpr, VCL::ConstantScalar* scalar) {
    SubstitutionTable& table = node->GetSubstitutionTable();
    if (baseExpr->GetValueDecl()->GetDeclClass() != VCL::Decl::VarDeclClass)
        return true;
    VCL::VarDecl* varDecl = (VCL::VarDecl*)baseExpr->GetValueDecl();
    if (!table.HasDecl(varDecl))
        return true;
    VCL::ConstantScalar* substitutedScalar = table.GetScalarSubstitution(varDecl);
    if (substitutedScalar != nullptr && memcmp(substitutedScalar->Data(), scalar->Data(), sizeof(uint8_t) * 8) == 0)
        return true;
    else if (substitutedScalar != nullptr)
        return false;
    table.SetScalarSubstitution(varDecl, scalar);
    return true;
}

VCL::Type* VCLG::GraphValidator::GenerateSubstitutedType(VCL::ASTContext& globalASTContext, Node* node, VCL::Type* baseType) {
    SubstitutionTable& table = node->GetSubstitutionTable();
    switch (baseType->GetTypeClass()) {
        case VCL::Type::TypeAliasTypeClass: {
            VCL::TypeAliasType* aliasType = (VCL::TypeAliasType*)baseType;
            VCL::TypeAliasDecl* aliasDecl = aliasType->GetDecl();
            if (!table.HasDecl(aliasDecl))
                return baseType;
            return table.GetTypeSubstitution(aliasDecl);
        }
        case VCL::Type::TemplateSpecializationTypeClass: {
            VCL::TemplateSpecializationType* speType = (VCL::TemplateSpecializationType*)baseType;
            VCL::TemplateArgumentList* argList = speType->GetTemplateArgumentList();
            llvm::SmallVector<VCL::TemplateArgument> substitutedArgs{};
            for (size_t i = 0; i < argList->GetCount(); ++i) {
                const VCL::TemplateArgument& arg = argList->GetArgs()[i];
                if (arg.GetKind() == VCL::TemplateArgument::Type) {
                    VCL::Type* newType = GenerateSubstitutedType(globalASTContext, node, arg.GetType().GetType());
                    if (!newType)
                        return nullptr;
                    substitutedArgs.push_back(VCL::TemplateArgument{ newType });
                } else if (arg.GetKind() == VCL::TemplateArgument::Expression) {
                    if (arg.GetExpr()->GetExprClass() != VCL::Expr::DeclRefExprClass) {
                        substitutedArgs.push_back(arg);
                        continue;
                    }
                    VCL::DeclRefExpr* declRefExpr = (VCL::DeclRefExpr*)arg.GetExpr();
                    if (declRefExpr->GetValueDecl()->GetDeclClass() != VCL::Decl::VarDeclClass) {
                        substitutedArgs.push_back(arg);
                        continue;
                    }
                    VCL::VarDecl* decl = (VCL::VarDecl*)declRefExpr->GetValueDecl();
                    if (!table.HasDecl(decl)) {
                        substitutedArgs.push_back(arg);
                        continue;
                    }
                    substitutedArgs.push_back(VCL::TemplateArgument{ *table.GetScalarSubstitution(decl) });
                } else {
                    substitutedArgs.push_back(arg);
                }
            }
            VCL::TemplateArgumentList* substitutedArgList = 
                VCL::TemplateArgumentList::Create(globalASTContext, substitutedArgs, argList->GetSourceRange());
            return globalASTContext.GetTypeCache().GetOrCreateTemplateSpecializationType(speType->GetTemplateDecl(), substitutedArgList);
        }
        default:
            return baseType;
    }
}