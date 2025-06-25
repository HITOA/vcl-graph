#include "NodeMetadata.hpp"

#include <VCLG/Node.hpp>
#include <VCLG/Port.hpp>


bool VCLG::NodeMetadata::Process(Node* node) {
    this->node = node;
    node->program->Accept(this);
    return true;
}

void VCLG::NodeMetadata::VisitProgram(VCL::ASTProgram* node) {
    for (auto& statement : node->statements)
        statement->Accept(this);
}

void VCLG::NodeMetadata::VisitCompoundStatement(VCL::ASTCompoundStatement* node) {

}

void VCLG::NodeMetadata::VisitFunctionPrototype(VCL::ASTFunctionPrototype* node) {

}

void VCLG::NodeMetadata::VisitFunctionDeclaration(VCL::ASTFunctionDeclaration* node) {

}

void VCLG::NodeMetadata::VisitStructureDeclaration(VCL::ASTStructureDeclaration* node) {

}

void VCLG::NodeMetadata::VisitTemplateDeclaration(VCL::ASTTemplateDeclaration* node) {

}

void VCLG::NodeMetadata::VisitTemplateFunctionDeclaration(VCL::ASTTemplateFunctionDeclaration* node) {

}

void VCLG::NodeMetadata::VisitReturnStatement(VCL::ASTReturnStatement* node) {

}

void VCLG::NodeMetadata::VisitIfStatement(VCL::ASTIfStatement* node) {

}

void VCLG::NodeMetadata::VisitWhileStatement(VCL::ASTWhileStatement* node) {

}

void VCLG::NodeMetadata::VisitForStatement(VCL::ASTForStatement* node) {

}

void VCLG::NodeMetadata::VisitBreakStatement(VCL::ASTBreakStatement* node) {

}

void VCLG::NodeMetadata::VisitBinaryArithmeticExpression(VCL::ASTBinaryArithmeticExpression* node) {

}

void VCLG::NodeMetadata::VisitBinaryLogicalExpression(VCL::ASTBinaryLogicalExpression* node) {

}

void VCLG::NodeMetadata::VisitBinaryComparisonExpression(VCL::ASTBinaryComparisonExpression* node) {

}

void VCLG::NodeMetadata::VisitAssignmentExpression(VCL::ASTAssignmentExpression* node) {

}

void VCLG::NodeMetadata::VisitPrefixArithmeticExpression(VCL::ASTPrefixArithmeticExpression* node) {

}

void VCLG::NodeMetadata::VisitPrefixLogicalExpression(VCL::ASTPrefixLogicalExpression* node) {

}

void VCLG::NodeMetadata::VisitPostfixArithmeticExpression(VCL::ASTPostfixArithmeticExpression* node) {

}

void VCLG::NodeMetadata::VisitFieldAccessExpression(VCL::ASTFieldAccessExpression* node) {

}

void VCLG::NodeMetadata::VisitSubscriptExpression(VCL::ASTSubscriptExpression* node) {

}

void VCLG::NodeMetadata::VisitLiteralExpression(VCL::ASTLiteralExpression* node) {

}

void VCLG::NodeMetadata::VisitVariableExpression(VCL::ASTVariableExpression* node) {

}

void VCLG::NodeMetadata::VisitVariableDeclaration(VCL::ASTVariableDeclaration* node) {
    if (node->type->IsInput()) {
        this->node->inputs.emplace_back(std::make_shared<Port>(node->name, node->type));
    } else if (node->type->IsOutput()) {
        this->node->outputs.emplace_back(std::make_shared<Port>(node->name, node->type));
    }
}

void VCLG::NodeMetadata::VisitFunctionCall(VCL::ASTFunctionCall* node) {

}

void VCLG::NodeMetadata::VisitAggregateExpression(VCL::ASTAggregateExpression* node) {

}
