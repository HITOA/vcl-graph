#include <VCLG/Core/SubstitutionTable.hpp>

#include <VCL/AST/TypePrinter.hpp>
#include <iostream>


VCLG::SubstitutionTable::SubstitutionTable() : table{} {

}

void VCLG::SubstitutionTable::SetTypeSubstitution(VCL::TypeAliasDecl* decl, VCL::Type* type) {
    table[decl] = type;
    if (type != nullptr) {
        std::cout << "Substituting " << decl->GetIdentifierInfo()->GetName().str() << " for " << VCL::TypePrinter::Print(type) << std::endl;
    }
}

void VCLG::SubstitutionTable::SetScalarSubstitution(VCL::VarDecl* decl, VCL::ConstantScalar* value) {
    table[decl] = value;
    if (value != nullptr) {
        switch (value->GetKind()) {
            case VCL::BuiltinType::Float32:
                std::cout << "Substituting " << decl->GetIdentifierInfo()->GetName().str() << " for " << value->Get<float>() << std::endl;
                break;
            case VCL::BuiltinType::Float64:
                std::cout << "Substituting " << decl->GetIdentifierInfo()->GetName().str() << " for " << value->Get<double>() << std::endl;
                break;
            default:
                std::cout << "Substituting " << decl->GetIdentifierInfo()->GetName().str() << " for " << *(long*)value->Data() << std::endl;
                break;
        }
    }
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
