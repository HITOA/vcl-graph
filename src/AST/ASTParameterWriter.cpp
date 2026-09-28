#include <VCLG/AST/ASTParameterWriter.hpp>

#include <VCL/AST/Expr.hpp>


void VCLG::ASTParameterWriter::HandleTopLevelDecl(VCL::Decl* decl) {
    if (decl->GetDeclClass() != VCL::Decl::VarDeclClass)
        return;

    VCL::VarDecl* varDecl = (VCL::VarDecl*)decl;

    for (size_t i = 0; i < parameters.size(); ++i) {
        const SourceParameterDefinition& definition = definitions[i];
        Parameter* parameter = parameters[i];

        if (parameter->GetInitializerOverride() == nullptr)
            continue;

        if (varDecl->GetIdentifierInfo() == identifierTable.Get(definition.GetName())) {
            VCL::NumericLiteralExpr* expr = VCL::NumericLiteralExpr::Create(
                astContext, 
                *parameter->GetInitializerOverride(), 
                varDecl->GetInitializer()->GetSourceRange());
            varDecl->SetInitializer(expr);
        }
    }
}