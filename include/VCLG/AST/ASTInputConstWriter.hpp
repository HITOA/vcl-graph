#pragma once

#include <VCL/AST/ASTConsumer.hpp>
#include <VCL/Core/Attribute.hpp>


namespace VCLG {

    /**
     * Makes `[Input]` variables const as they are parsed, so that Sema rejects any write to an input
     * (and passing one to an `inout`/`out` parameter) in the functions that follow. An input written
     * by its node would change what every other consumer of the producer reads.
     *
     * Runs after ASTPortTypeOverrideWriter, which replaces the type of some inputs.
     */
    class ASTInputConstWriter : public VCL::ASTConsumer {
    public:
        ASTInputConstWriter(VCL::AttributeDefinition* inputAttribute) : inputAttribute{ inputAttribute } {}

        void HandleTopLevelDecl(VCL::Decl* decl) override;

    private:
        VCL::AttributeDefinition* inputAttribute;
    };

}
