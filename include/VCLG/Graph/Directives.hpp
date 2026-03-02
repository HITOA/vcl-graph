#pragma once

#include <VCL/Core/Directive.hpp>
#include <VCL/AST/ConstantValue.hpp>

#include <string>


namespace VCLG {

    class MetadataDirective : public VCL::DirectiveHandler {
    public:
        MetadataDirective() = delete;
        MetadataDirective(const std::string& name, VCL::ConstantValue::ConstantValueClass type) 
                : name{ name }, type{ type } {}
        ~MetadataDirective() = default;

        bool OnSema(VCL::Sema& sema, VCL::DirectiveDecl* decl) override;
        
    private:
        std::string name;
        VCL::ConstantValue::ConstantValueClass type;
    };

}