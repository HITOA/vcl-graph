#include <VCLG/AST/ASTPortTypeOverrideWriter.hpp>

#include <VCL/AST/Expr.hpp>


void VCLG::ASTPortTypeOverrideWriter::HandleTopLevelDecl(VCL::Decl* decl) {
    if (decl->GetDeclClass() != VCL::Decl::VarDeclClass)
        return;

    VCL::VarDecl* varDecl = (VCL::VarDecl*)decl;

    for (size_t i = 0; i < ports.size(); ++i) {
        SourcePortDefinition* definition = definitions[i];
        Port* port= ports[i];

        if (varDecl->GetIdentifierInfo() == definition->GetDecl()->GetIdentifierInfo()) {
            if (port->GetOverrideType() != nullptr) {
                varDecl->SetValueType(port->GetOverrideType());
                varDecl->SetInitializer(nullptr);
            }
            break;
        }
    }
}