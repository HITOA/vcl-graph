#include "NodeMetadata.hpp"

#include <VCLG/Node.hpp>
#include <VCLG/Port.hpp>


bool VCLG::NodeMetadata::Process(Node* node) {
    this->node = node;
    node->inputs.clear();
    node->outputs.clear();
    node->program->Accept(this);
    return true;
}

void VCLG::NodeMetadata::VisitProgram(VCL::ASTProgram* node) {
    for (auto& statement : node->statements)
        statement->Accept(this);
}

void VCLG::NodeMetadata::VisitFunctionDeclaration(VCL::ASTFunctionDeclaration* node) {
    if (node->prototype->attributes.HasAttributeByName("NodeProcess")) {
        if (!node->prototype->arguments.empty())
            throw std::runtime_error{ "Node entry point cannot have parameter(s)." };
        if (node->prototype->type->type != VCL::TypeInfo::TypeName::Void)
            throw std::runtime_error{ "Node entry point's return type must be void." };
    }
}

void VCLG::NodeMetadata::VisitVariableDeclaration(VCL::ASTVariableDeclaration* node) {
    if (node->type->IsInput()) {
        this->node->inputs.emplace_back(std::make_shared<Port>(node->name, node->type));
    } else if (node->type->IsOutput()) {
        this->node->outputs.emplace_back(std::make_shared<Port>(node->name, node->type));
    }
}
