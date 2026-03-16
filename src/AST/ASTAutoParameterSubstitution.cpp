#include <VCLG/AST/ASTAutoParameterSubstitution.hpp>

#include <VCL/Core/Diagnostic.hpp>
#include <VCL/AST/Expr.hpp>
#include <VCL/AST/ExprEvaluator.hpp>


void VCLG::ASTAutoParameterSubstitution::HandleTopLevelDecl(VCL::Decl* decl) {
    switch (decl->GetDeclClass()) {
        case VCL::Decl::TypeAliasDeclClass: {
            VCL::TypeAliasDecl* typeAliasDecl = (VCL::TypeAliasDecl*)decl;
            SourceAutoParameterDefinition* autoParam = GetAutoParamFromDecl(typeAliasDecl);
            if (!autoParam)
                return;
            VCL::Type* type = table.GetTypeSubstitution((VCL::TypeAliasDecl*)autoParam->GetDecl());
            if (!type)
                return;
            typeAliasDecl->SetType(type);
            return;
        }
        case VCL::Decl::VarDeclClass: {
            VCL::VarDecl* varDecl = (VCL::VarDecl*)decl;
            SourceAutoParameterDefinition* autoParam = GetAutoParamFromDecl(varDecl);
            if (!autoParam)
                return;
            VCL::ConstantScalar* scalar = table.GetScalarSubstitution((VCL::VarDecl*)autoParam->GetDecl());
            if (!scalar)
                return;
            VCL::Expr* expr = VCL::NumericLiteralExpr::Create(
                sema.GetASTContext(), 
                *scalar, 
                varDecl->GetInitializer()->GetSourceRange());
            expr = sema.ActOnCast(expr, varDecl->GetValueType(), expr->GetSourceRange());
            if (!expr) {
                sema.GetDiagnosticReporter().Error(VCL::Diagnostic::CustomDiagnostic, "failed to write auto-parameter")
                    .SetCompilerInfo(__FILE__, __func__, __LINE__)
                    .Report();
                return;
            }
            VCL::ExprEvaluator eval{ sema.GetASTContext() };
            VCL::ConstantValue* value = eval.Visit(expr);
            if (!value) {
                sema.GetDiagnosticReporter().Error(VCL::Diagnostic::CustomDiagnostic, "failed to write auto-parameter")
                    .SetCompilerInfo(__FILE__, __func__, __LINE__)
                    .Report();
                return;
            }
            expr->SetConstantValue(value);
            varDecl->SetInitializer(expr);
            return;
        }
        default:
            return;
    }
}

VCLG::SourceAutoParameterDefinition* VCLG::ASTAutoParameterSubstitution::GetAutoParamFromDecl(VCL::NamedDecl* decl) {
    for (SourceAutoParameterDefinition* autoParam : autoParameters) {
        VCL::NamedDecl* namedDecl = (VCL::NamedDecl*)autoParam->GetDecl();
        if (namedDecl->GetIdentifierInfo() == decl->GetIdentifierInfo())
            return autoParam;
    }
    return nullptr;
}