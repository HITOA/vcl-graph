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

    for (Node* node : dependentNodes) {
        for (Port* inPort : Node::GetNodeInputs(node)) {
            if (inPortToOutPort.count(inPort)) {
                Port* outPort = inPortToOutPort[inPort];
                if (!SubstituteType(node, inPort->GetType(), outPort->GetLastType()))
                    return false;
            }
        }
        for (Port* outPort : Node::GetNodeOutputs(node)) {
            if (connectedOutPort.count(outPort)) {
                for (const Connection& connection : graph.GetConnections()) {
                    Port* inPort = graph.GetPortByIdentity(connection.GetInputPortIdentity());
                    if (inPort->IsDependent())
                        continue;
                    if (outPort == graph.GetPortByIdentity(connection.GetOutputPortIdentity())) {
                        if (!SubstituteType(node, outPort->GetType(), inPort->GetLastType()))
                            return false;
                    }
                }
            }
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

    std::unordered_set<Node*> visitedNodes{};
    std::queue<Node*> nodeToVisite{};

    for (Node* node : graph.GetNodes()) {
        if (node->HasFlag(Node::NodeFlag::IsOutputNode)) {
            nodeToVisite.push(node);
        } else {
            bool isStub = true;
            for (Port* output : Node::GetNodeOutputs(node)) {
                if (connectedOutPort.count(output)) {
                    isStub = false;
                    break;
                }
            }
            if (isStub)
                nodeToVisite.push(node);
        }
    }
    
    while (!nodeToVisite.empty()) {
        Node* currentNode = nodeToVisite.front();
        nodeToVisite.pop();

        if (visitedNodes.count(currentNode) && currentNode->HasFlag(Node::NodeFlag::IsDependent)) {
            nodes.erase(std::remove(nodes.begin(), nodes.end(), currentNode), nodes.end());
            nodes.push_back(currentNode);
        } else if (currentNode->HasFlag(Node::NodeFlag::IsDependent)) {
            nodes.push_back(currentNode);
            visitedNodes.insert(currentNode);
        }

        for (Port* inPort : Node::GetNodeInputs(currentNode)) {
            if (!inPortToOutPort.count(inPort))
                continue;
            Port* connectedPort = inPortToOutPort[inPort];
            Node* connectedNode = graph.GetNodeByIdentity(connectedPort->GetOwner());
            nodeToVisite.push(connectedNode);
        }
    }

    std::reverse(nodes.begin(), nodes.end());
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
            table.SetTypeSubstitution(aliasDecl, VCL::Type::GetCanonicalType(connectedType));
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