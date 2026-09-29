#include <VCLG/AST/ASTInputConstWriter.hpp>

#include <VCL/AST/Decl.hpp>


void VCLG::ASTInputConstWriter::HandleTopLevelDecl(VCL::Decl* decl) {
    if (decl->GetDeclClass() != VCL::Decl::VarDeclClass)
        return;

    VCL::VarDecl* varDecl = (VCL::VarDecl*)decl;
    if (varDecl->HasAttribute(inputAttribute) == nullptr)
        return;

    VCL::QualType type = varDecl->GetValueType();
    type.AddQualifier(VCL::Qualifier::Const);
    varDecl->SetValueType(type);
}
