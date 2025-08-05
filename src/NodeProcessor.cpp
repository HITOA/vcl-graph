#include "NodeProcessor.hpp"

#define KEEP_NAME_ATTRIBUTE "DoNotMangle"



VCLG::NodeProcessor::NodeProcessor(const std::unordered_map<std::string, std::string>& inputsRemapping, const std::unordered_set<std::string>& outputs) :
    inputsRemapping{ inputsRemapping }, outputs{ outputs } {};

bool VCLG::NodeProcessor::Process(Node* node, uint32_t nodeIdx) {
    this->node = node;
    this->symbolPrefix = std::string{ "Node" } + std::to_string(nodeIdx) + "_";
    this->scope = std::make_unique<RenamingScopeManager>();
    node->program->Accept(this);
    return true;
}

void VCLG::NodeProcessor::VisitProgram(VCL::ASTProgram* node) {
    removeCurrentGlobalStatement = false;
    int i = 0; 
    while (i < node->statements.size()) {
        node->statements[i]->Accept(this);
        if (removeCurrentGlobalStatement) {
            node->statements.erase(node->statements.begin() + i);
            removeCurrentGlobalStatement = false;
        } else {
            ++i;
        }
    }
}

void VCLG::NodeProcessor::VisitCompoundStatement(VCL::ASTCompoundStatement* node) {
    scope->PushScope();
    for (auto& statement : node->statements)
        statement->Accept(this);
    scope->PopScope();
}

void VCLG::NodeProcessor::VisitFunctionPrototype(VCL::ASTFunctionPrototype* node) {
    std::string newName = node->name;
    if (scope->IsCurrentScopeGlobal() && !node->attributes.HasAttributeByName(KEEP_NAME_ATTRIBUTE));
        newName = symbolPrefix + newName;

    RenameTypeInfo(node->type);
    
    scope->PushNewName(node->name, newName);
    node->name = newName;
}

void VCLG::NodeProcessor::VisitFunctionDeclaration(VCL::ASTFunctionDeclaration* node) {
    node->prototype->Accept(this);

    scope->PushScope();

    for (auto& arg : node->prototype->arguments) {
        RenameTypeInfo(arg->type);
        scope->PushNewName(arg->name, arg->name);
    }

    node->body->Accept(this);

    scope->PopScope();
}

void VCLG::NodeProcessor::VisitStructureDeclaration(VCL::ASTStructureDeclaration* node) {
    std::string newName = node->name;
    if (scope->IsCurrentScopeGlobal());
        newName = symbolPrefix + newName;
    
    scope->PushNewName(node->name, newName);
    node->name = newName;
}

void VCLG::NodeProcessor::VisitTemplateDeclaration(VCL::ASTTemplateDeclaration* node) {
    std::string newName = node->name;
    if (scope->IsCurrentScopeGlobal());
        newName = symbolPrefix + newName;
    
    scope->PushNewName(node->name, newName);
    node->name = newName;
}

void VCLG::NodeProcessor::VisitTemplateFunctionDeclaration(VCL::ASTTemplateFunctionDeclaration* node) {
    std::string newName = node->name;
    if (scope->IsCurrentScopeGlobal());
        newName = symbolPrefix + newName;
    
    RenameTypeInfo(node->type);

    scope->PushScope();

    for (auto& arg : node->arguments) {
        RenameTypeInfo(arg->type);
        scope->PushNewName(arg->name, arg->name);
    }

    node->body->Accept(this);

    scope->PopScope();

    scope->PushNewName(node->name, newName);
    node->name = newName;
}

void VCLG::NodeProcessor::VisitReturnStatement(VCL::ASTReturnStatement* node) {
    node->expression->Accept(this);
}

void VCLG::NodeProcessor::VisitIfStatement(VCL::ASTIfStatement* node) {
    node->condition->Accept(this);

    scope->PushScope();

    node->thenStmt->Accept(this);
    if (node->elseStmt)
        node->elseStmt->Accept(this);

    scope->PopScope();
}

void VCLG::NodeProcessor::VisitWhileStatement(VCL::ASTWhileStatement* node) {
    scope->PushScope();

    node->condition->Accept(this);
    node->thenStmt->Accept(this);

    scope->PopScope();
}

void VCLG::NodeProcessor::VisitForStatement(VCL::ASTForStatement* node) {
    scope->PushScope();

    node->start->Accept(this);
    node->condition->Accept(this);
    node->end->Accept(this);
    node->thenStmt->Accept(this);

    scope->PopScope();
}

void VCLG::NodeProcessor::VisitBreakStatement(VCL::ASTBreakStatement* node) {
    
}

void VCLG::NodeProcessor::VisitBinaryArithmeticExpression(VCL::ASTBinaryArithmeticExpression* node) {
    node->lhs->Accept(this);
    node->rhs->Accept(this);
}

void VCLG::NodeProcessor::VisitBinaryLogicalExpression(VCL::ASTBinaryLogicalExpression* node) {
    node->lhs->Accept(this);
    node->rhs->Accept(this);
}

void VCLG::NodeProcessor::VisitBinaryComparisonExpression(VCL::ASTBinaryComparisonExpression* node) {
    node->lhs->Accept(this);
    node->rhs->Accept(this);
}

void VCLG::NodeProcessor::VisitAssignmentExpression(VCL::ASTAssignmentExpression* node) {
    node->lhs->Accept(this);
    node->rhs->Accept(this);
}

void VCLG::NodeProcessor::VisitPrefixArithmeticExpression(VCL::ASTPrefixArithmeticExpression* node) {
    node->expression->Accept(this);
}

void VCLG::NodeProcessor::VisitPrefixLogicalExpression(VCL::ASTPrefixLogicalExpression* node) {
    node->expression->Accept(this);
}

void VCLG::NodeProcessor::VisitPostfixArithmeticExpression(VCL::ASTPostfixArithmeticExpression* node) {
    node->expression->Accept(this);
}

void VCLG::NodeProcessor::VisitFieldAccessExpression(VCL::ASTFieldAccessExpression* node) {
    node->expression->Accept(this);
}

void VCLG::NodeProcessor::VisitSubscriptExpression(VCL::ASTSubscriptExpression* node) {
    node->expression->Accept(this);
    node->index->Accept(this);
}

void VCLG::NodeProcessor::VisitLiteralExpression(VCL::ASTLiteralExpression* node) {

}

void VCLG::NodeProcessor::VisitIdentifierExpression(VCL::ASTIdentifierExpression* node) {
    std::string newName = scope->GetNewName(node->name);
    if (!newName.empty())
        node->name = newName;
}

void VCLG::NodeProcessor::VisitVariableDeclaration(VCL::ASTVariableDeclaration* node) {
    std::string newName = node->name;
    if (scope->IsCurrentScopeGlobal()) {
        newName = symbolPrefix + newName;

        if (node->type->IsExtern()) {
            if (inputsRemapping.count(newName)) {
                removeCurrentGlobalStatement = true;
                scope->PushNewName(node->name, inputsRemapping.at(newName));
            } else if (outputs.count(newName)) {
                node->type->qualifiers = VCL::TypeInfo::QualifierFlag::None;
                scope->PushNewName(node->name, newName);
                node->name = newName;
            } else {
                scope->PushNewName(node->name, newName);
                node->name = newName;
            }
        }
    }

    RenameTypeInfo(node->type);

    if (node->expression)
        node->expression->Accept(this);
}

void VCLG::NodeProcessor::VisitFunctionCall(VCL::ASTFunctionCall* node) {
    std::string newName = scope->GetNewName(node->name);
    if (!newName.empty())
        node->name = newName;

    for (auto& templateArg : node->templateArguments) {
        if (templateArg->type == VCL::TemplateArgument::TemplateValueType::Typename)
            RenameTypeInfo(templateArg->typeInfo);
    }

    for (auto& arg : node->arguments)
        arg->Accept(this);
}

void VCLG::NodeProcessor::VisitAggregateExpression(VCL::ASTAggregateExpression* node) {
    for (auto& value : node->values)
        value->Accept(this);
}

void VCLG::NodeProcessor::RenameTypeInfo(std::shared_ptr<VCL::TypeInfo> typeInfo) {
    if (typeInfo->type == VCL::TypeInfo::TypeName::Custom) {
        std::string newName = scope->GetNewName(typeInfo->name);
        if (!newName.empty())
            typeInfo->name = newName;
    }

    for (auto& templateArg : typeInfo->arguments) {
        if (templateArg->type == VCL::TemplateArgument::TemplateValueType::Typename)
            RenameTypeInfo(templateArg->typeInfo);
    }
}