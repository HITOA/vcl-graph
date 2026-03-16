#pragma once

#include <VCLG/Core/SubstitutionTable.hpp>
#include <VCLG/Graph/Definition.hpp>
#include <VCLG/Graph/Parameter.hpp>

#include <VCL/AST/ASTConsumer.hpp>
#include <VCL/Core/Identifier.hpp>
#include <VCL/Sema/Sema.hpp>

#include <llvm/ADT/ArrayRef.h>


namespace VCLG {

    class ASTAutoParameterSubstitution : public VCL::ASTConsumer {
    public:
        ASTAutoParameterSubstitution(VCL::Sema& sema, VCL::IdentifierTable& identifierTable, 
            SubstitutionTable& table, llvm::ArrayRef<SourceAutoParameterDefinition*> autoParameters) :
            sema{ sema }, identifierTable{ identifierTable }, table{ table }, autoParameters{ autoParameters } {}

        void HandleTopLevelDecl(VCL::Decl* decl) override;

    private:
        SourceAutoParameterDefinition* GetAutoParamFromDecl(VCL::NamedDecl* decl);

    private:
        VCL::Sema& sema;
        VCL::IdentifierTable& identifierTable;
        SubstitutionTable& table;
        llvm::ArrayRef<SourceAutoParameterDefinition*> autoParameters;
    };

}