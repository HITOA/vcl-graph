#include <VCLG/AST/ASTParameterWriter.hpp>

#include <VCL/AST/Expr.hpp>


void VCLG::ASTParameterWriter::HandleTopLevelDecl(VCL::Decl* decl) {
    if (decl->GetDeclClass() != VCL::Decl::VarDeclClass)
        return;

    VCL::VarDecl* varDecl = (VCL::VarDecl*)decl;

    for (size_t i = 0; i < parameters.size(); ++i) {
        SourceParameterDefinition* definition = definitions[i];
        Parameter* parameter = parameters[i];

        if (parameter->GetInitializerOverride() == nullptr || 
            parameter->GetInitializerOverride()->GetConstantValueClass() != VCL::ConstantValue::ConstantScalarClass)
            continue;

        if (varDecl->GetIdentifierInfo() == identifierTable.Get(definition->GetName())) {
            VCL::NumericLiteralExpr* expr = VCL::NumericLiteralExpr::Create(
                astContext, 
                *(VCL::ConstantScalar*)parameter->GetInitializerOverride(), 
                varDecl->GetInitializer()->GetSourceRange());
            varDecl->SetInitializer(expr);
        }
    }
}