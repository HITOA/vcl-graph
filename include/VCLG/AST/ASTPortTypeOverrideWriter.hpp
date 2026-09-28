#pragma once

#include <VCLG/Graph/Definition.hpp>
#include <VCL/AST/Type.hpp>

#include <VCL/AST/ASTConsumer.hpp>
#include <VCL/Core/Identifier.hpp>

#include <llvm/ADT/ArrayRef.h>


namespace VCLG {

    /**
     * Gives input ports another type than the declared one (`overrides[i]` for the i-th input, null
     * to keep it), e.g. the type a converter promoted them to.
     */
    class ASTPortTypeOverrideWriter : public VCL::ASTConsumer {
    public:
        ASTPortTypeOverrideWriter(VCL::ASTContext& astContext, VCL::IdentifierTable& identifierTable, 
                llvm::ArrayRef<SourcePortDefinition> definitions, llvm::ArrayRef<VCL::Type*> overrides) :
            astContext{ astContext }, identifierTable{ identifierTable }, definitions{ definitions }, overrides{ overrides } {}

        void HandleTopLevelDecl(VCL::Decl* decl) override;

    private:
        VCL::ASTContext& astContext;
        VCL::IdentifierTable& identifierTable;

        llvm::ArrayRef<SourcePortDefinition> definitions;
        llvm::ArrayRef<VCL::Type*> overrides;
    };

}