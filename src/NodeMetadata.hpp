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
        void VisitFunctionDeclaration(VCL::ASTFunctionDeclaration* node) override;
        void VisitVariableDeclaration(VCL::ASTVariableDeclaration* node) override;
        
    private:
        Node* node;
    };

}