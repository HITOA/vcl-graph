#include <VCLG/Core/SubstitutionTable.hpp>

#include <VCL/AST/TypePrinter.hpp>
#include <iostream>


VCLG::SubstitutionTable::SubstitutionTable() : table{} {

}

void VCLG::SubstitutionTable::SetTypeSubstitution(VCL::TypeAliasDecl* decl, VCL::Type* type) {
    table[decl] = type;
}

void VCLG::SubstitutionTable::SetScalarSubstitution(VCL::VarDecl* decl, VCL::ConstantScalar* value) {
    table[decl] = value;
}

VCL::Type* VCLG::SubstitutionTable::GetTypeSubstitution(VCL::TypeAliasDecl* decl) {
    if (table.count(decl))
        return std::get<VCL::Type*>(table.at(decl));
    return nullptr;
}

VCL::ConstantScalar* VCLG::SubstitutionTable::GetScalarSubstitution(VCL::VarDecl* decl) {
    if (table.count(decl))
        return std::get<VCL::ConstantScalar*>(table.at(decl));
    return nullptr;
}

bool VCLG::SubstitutionTable::HasDecl(VCL::Decl* decl) {
    return table.count(decl) > 0;
}
