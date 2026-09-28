#pragma once

#include <VCLG/Graph/Definition.hpp>
#include <VCL/AST/ConstantValue.hpp>

#include <VCL/AST/ASTConsumer.hpp>
#include <VCL/Core/Identifier.hpp>

#include <llvm/ADT/ArrayRef.h>

#include <optional>


namespace VCLG {

    class ASTParameterWriter : public VCL::ASTConsumer {
    public:
        ASTParameterWriter(VCL::ASTContext& astContext, VCL::IdentifierTable& identifierTable, 
                llvm::ArrayRef<SourceParameterDefinition> definitions, llvm::ArrayRef<std::optional<VCL::ConstantScalar>> parameters) :
            astContext{ astContext }, identifierTable{ identifierTable }, definitions{ definitions }, parameters{ parameters } {}

        void HandleTopLevelDecl(VCL::Decl* decl) override;

    private:
        VCL::ASTContext& astContext;
        VCL::IdentifierTable& identifierTable;

        llvm::ArrayRef<SourceParameterDefinition> definitions;
        llvm::ArrayRef<std::optional<VCL::ConstantScalar>> parameters;
    };

}