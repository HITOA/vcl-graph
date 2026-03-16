#pragma once

#include <VCLG/Graph/Definition.hpp>
#include <VCLG/Graph/Parameter.hpp>

#include <VCL/AST/ASTConsumer.hpp>
#include <VCL/Core/Identifier.hpp>

#include <llvm/ADT/ArrayRef.h>


namespace VCLG {

    class ASTParameterWriter : public VCL::ASTConsumer {
    public:
        ASTParameterWriter(VCL::ASTContext& astContext, VCL::IdentifierTable& identifierTable, 
                llvm::ArrayRef<SourceParameterDefinition*> definitions, llvm::ArrayRef<Parameter*> parameters) :
            astContext{ astContext }, identifierTable{ identifierTable }, definitions{ definitions }, parameters{ parameters } {}

        void HandleTopLevelDecl(VCL::Decl* decl) override;

    private:
        VCL::ASTContext& astContext;
        VCL::IdentifierTable& identifierTable;

        llvm::ArrayRef<SourceParameterDefinition*> definitions;
        llvm::ArrayRef<Parameter*> parameters;
    };

}