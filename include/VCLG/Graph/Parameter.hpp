#pragma once

#include <VCLG/Core/IdentityProvider.hpp>

#include <VCL/AST/Type.hpp>
#include <VCL/AST/ConstantValue.hpp>


namespace VCLG {
    class Node;

    class Parameter {
    public:
        Parameter() = delete;
        Parameter(Identity owner, VCL::Type* type, const std::string& displayName, VCL::ConstantValue* initializer, Identity identity) :
                owner{ owner }, type{ type }, displayName{ displayName },
                initializer{ initializer }, initializerOverride{ nullptr }, identity{ identity } {}
        Parameter(const Parameter& other) = delete;
        Parameter(Parameter&& other) = delete;
        virtual ~Parameter() = default;

        Parameter& operator=(const Parameter& other) = delete;
        Parameter& operator=(Parameter&& other) = delete;

        inline void SetInitializerOverride(VCL::ConstantValue* value) { this->initializerOverride = value; }

        inline Identity GetOwner() const { return owner; }
        inline VCL::Type* GetType() const { return type; }
        inline bool IsDependent() const { return type->IsDependent(); }
        inline const std::string& GetDisplayName() const { return displayName; }
        inline void SetDisplayName(const std::string& displayName) { this->displayName = displayName; }
        inline VCL::ConstantValue* GetInitializer() const { return initializer; }
        inline VCL::ConstantValue* GetInitializerOverride() const { return initializerOverride; }
        inline Identity GetIdentity() const { return identity; }

    private:
        Identity owner;
        VCL::Type* type;
        std::string displayName;
        VCL::ConstantValue* initializer;
        VCL::ConstantValue* initializerOverride;
        Identity identity;
    };

}