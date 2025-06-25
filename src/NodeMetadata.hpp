#pragma once

#include <VCL/AST.hpp>


namespace VCLG {
    class Node;

    class NodeMetadata : public VCL::ASTVisitor {
    public:
        NodeMetadata() = default;
        ~NodeMetadata() = default;

        bool Process(Node* node);

        void VisitProgram(VCL::ASTProgram* node) override;
        void VisitCompoundStatement(VCL::ASTCompoundStatement* node) override;
        void VisitFunctionPrototype(VCL::ASTFunctionPrototype* node) override;
        void VisitFunctionDeclaration(VCL::ASTFunctionDeclaration* node) override;
        void VisitStructureDeclaration(VCL::ASTStructureDeclaration* node) override;
        void VisitTemplateDeclaration(VCL::ASTTemplateDeclaration* node) override;
        void VisitTemplateFunctionDeclaration(VCL::ASTTemplateFunctionDeclaration* node) override;
        void VisitReturnStatement(VCL::ASTReturnStatement* node) override;
        void VisitIfStatement(VCL::ASTIfStatement* node) override;
        void VisitWhileStatement(VCL::ASTWhileStatement* node) override;
        void VisitForStatement(VCL::ASTForStatement* node) override;
        void VisitBreakStatement(VCL::ASTBreakStatement* node) override;
        void VisitBinaryArithmeticExpression(VCL::ASTBinaryArithmeticExpression* node) override;
        void VisitBinaryLogicalExpression(VCL::ASTBinaryLogicalExpression* node) override;
        void VisitBinaryComparisonExpression(VCL::ASTBinaryComparisonExpression* node) override;
        void VisitAssignmentExpression(VCL::ASTAssignmentExpression* node) override;
        void VisitPrefixArithmeticExpression(VCL::ASTPrefixArithmeticExpression* node) override;
        void VisitPrefixLogicalExpression(VCL::ASTPrefixLogicalExpression* node) override;
        void VisitPostfixArithmeticExpression(VCL::ASTPostfixArithmeticExpression* node) override;
        void VisitFieldAccessExpression(VCL::ASTFieldAccessExpression* node) override;
        void VisitSubscriptExpression(VCL::ASTSubscriptExpression* node) override;
        void VisitLiteralExpression(VCL::ASTLiteralExpression* node) override;
        void VisitVariableExpression(VCL::ASTVariableExpression* node) override;
        void VisitVariableDeclaration(VCL::ASTVariableDeclaration* node) override;
        void VisitFunctionCall(VCL::ASTFunctionCall* node) override;
        void VisitAggregateExpression(VCL::ASTAggregateExpression* node) override;
        
    private:
        Node* node;
    };

}