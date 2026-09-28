#pragma once

#include <VCL/AST/Decl.hpp>
#include <VCL/AST/Type.hpp>
#include <VCL/AST/ConstantValue.hpp>

#include <llvm/ADT/DenseMap.h>

#include <variant>


namespace VCLG {

    class SubstitutionTable {
    public:
        SubstitutionTable();
        SubstitutionTable(const SubstitutionTable& other) = default;
        SubstitutionTable(SubstitutionTable&& other) = default;
        ~SubstitutionTable() = default;
        
        SubstitutionTable& operator=(const SubstitutionTable& other) = default;
        SubstitutionTable& operator=(SubstitutionTable&& other) = default;


        void SetTypeSubstitution(VCL::TypeAliasDecl* decl, VCL::Type* type);
        void SetScalarSubstitution(VCL::VarDecl* decl, VCL::ConstantScalar* value);

        VCL::Type* GetTypeSubstitution(VCL::TypeAliasDecl* decl) const;
        VCL::ConstantScalar* GetScalarSubstitution(VCL::VarDecl* decl) const;

        bool HasDecl(VCL::Decl* decl) const;

        inline size_t Size() const { return table.size(); }
        inline void Clear() { table.clear(); }
        /** Keeps the declarations, marks each of them unresolved. */
        void ClearValues();

        llvm::DenseMap<VCL::Decl*, std::variant<VCL::Type*, VCL::ConstantScalar*>>::iterator begin() { return table.begin(); }
        llvm::DenseMap<VCL::Decl*, std::variant<VCL::Type*, VCL::ConstantScalar*>>::iterator end() { return table.end(); }
    
    private:
        llvm::DenseMap<VCL::Decl*, std::variant<VCL::Type*, VCL::ConstantScalar*>> table;
    };

}