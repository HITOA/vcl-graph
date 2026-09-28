#include <VCLG/AST/ASTPortTypeOverrideWriter.hpp>

#include <VCL/AST/Expr.hpp>


void VCLG::ASTPortTypeOverrideWriter::HandleTopLevelDecl(VCL::Decl* decl) {
    if (decl->GetDeclClass() != VCL::Decl::VarDeclClass)
        return;

    VCL::VarDecl* varDecl = (VCL::VarDecl*)decl;

    for (size_t i = 0; i < overrides.size(); ++i) {
        const SourcePortDefinition& definition = definitions[i];

        if (varDecl->GetIdentifierInfo() == definition.GetDecl()->GetIdentifierInfo()) {
            if (overrides[i] != nullptr) {
                varDecl->SetValueType(overrides[i]);
                varDecl->SetInitializer(nullptr);
            }
            break;
        }
    }
}