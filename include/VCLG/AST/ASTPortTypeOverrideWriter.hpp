#pragma once

#include <VCLG/Graph/Definition.hpp>
#include <VCLG/Graph/Port.hpp>

#include <VCL/AST/ASTConsumer.hpp>
#include <VCL/Core/Identifier.hpp>

#include <llvm/ADT/ArrayRef.h>


namespace VCLG {

    class ASTPortTypeOverrideWriter : public VCL::ASTConsumer {
    public:
        ASTPortTypeOverrideWriter(VCL::ASTContext& astContext, VCL::IdentifierTable& identifierTable, 
                llvm::ArrayRef<SourcePortDefinition*> definitions, llvm::ArrayRef<Port*> ports) :
            astContext{ astContext }, identifierTable{ identifierTable }, definitions{ definitions }, ports{ ports } {}

        void HandleTopLevelDecl(VCL::Decl* decl) override;

    private:
        VCL::ASTContext& astContext;
        VCL::IdentifierTable& identifierTable;

        llvm::ArrayRef<SourcePortDefinition*> definitions;
        llvm::ArrayRef<Port*> ports;
    };

}