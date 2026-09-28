#include <VCLG/Core/SubstitutionTable.hpp>



VCLG::SubstitutionTable::SubstitutionTable() : table{} {

}

void VCLG::SubstitutionTable::SetTypeSubstitution(VCL::TypeAliasDecl* decl, VCL::Type* type) {
    table[decl] = type;
}

void VCLG::SubstitutionTable::SetScalarSubstitution(VCL::VarDecl* decl, VCL::ConstantScalar* value) {
    table[decl] = value;
}

VCL::Type* VCLG::SubstitutionTable::GetTypeSubstitution(VCL::TypeAliasDecl* decl) const {
    if (table.count(decl))
        return std::get<VCL::Type*>(table.at(decl));
    return nullptr;
}

VCL::ConstantScalar* VCLG::SubstitutionTable::GetScalarSubstitution(VCL::VarDecl* decl) const {
    if (table.count(decl))
        return std::get<VCL::ConstantScalar*>(table.at(decl));
    return nullptr;
}

bool VCLG::SubstitutionTable::HasDecl(VCL::Decl* decl) const {
    return table.count(decl) > 0;
}

void VCLG::SubstitutionTable::ClearValues() {
    for (auto& entry : table) {
        if (std::holds_alternative<VCL::Type*>(entry.second))
            entry.second = (VCL::Type*)nullptr;
        else
            entry.second = (VCL::ConstantScalar*)nullptr;
    }
}
