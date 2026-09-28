#pragma once

#include <VCLG/Core/IdentityProvider.hpp>

#include <VCL/AST/Type.hpp>
#include <VCL/AST/ConstantValue.hpp>

#include <optional>
#include <string>


namespace VCLG {
    class Node;

    class Parameter {
    public:
        Parameter() = delete;
        Parameter(Identity owner, VCL::Type* type, const std::string& displayName, VCL::ConstantValue* initializer, Identity identity) :
                owner{ owner }, type{ type }, displayName{ displayName },
                initializer{ initializer }, initializerOverride{}, identity{ identity } {}
        Parameter(const Parameter& other) = delete;
        Parameter(Parameter&& other) = delete;
        virtual ~Parameter() = default;

        Parameter& operator=(const Parameter& other) = delete;
        Parameter& operator=(Parameter&& other) = delete;

        /**
         * The value the user gave this parameter, in place of the declared initializer.
         * GraphInstance creates it from the declared value (or zero) when the type is scalar;
         * null otherwise.
         */
        inline VCL::ConstantScalar* GetInitializerOverride() { return initializerOverride ? &*initializerOverride : nullptr; }
        inline void SetInitializerOverride(const VCL::ConstantScalar& value) { initializerOverride.emplace(value); }

        inline Identity GetOwner() const { return owner; }
        inline VCL::Type* GetType() const { return type; }
        inline bool IsDependent() const { return type->IsDependent(); }
        inline const std::string& GetDisplayName() const { return displayName; }
        inline void SetDisplayName(const std::string& displayName) { this->displayName = displayName; }
        inline VCL::ConstantValue* GetInitializer() const { return initializer; }
        inline Identity GetIdentity() const { return identity; }

    private:
        Identity owner;
        VCL::Type* type;
        std::string displayName;
        VCL::ConstantValue* initializer;
        std::optional<VCL::ConstantScalar> initializerOverride;
        Identity identity;
    };

}