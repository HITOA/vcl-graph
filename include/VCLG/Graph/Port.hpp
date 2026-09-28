#pragma once

#include <VCLG/Core/IdentityProvider.hpp>

#include <VCL/AST/Type.hpp>
#include <VCL/AST/ConstantValue.hpp>

#include <optional>
#include <string>


namespace VCLG {
    class Node;

    class Port {
    public:
        enum PortKind {
            None = 0,
            Input,
            Output
        };
    public:
        Port() = delete;
        Port(Identity owner, VCL::Type* type, const std::string& displayName, PortKind kind, VCL::ConstantValue* initializer, 
                bool isDependent, Identity identity) :
                owner{ owner }, type{ type }, displayName{ displayName }, kind{ kind }, 
                initializer{ initializer }, initializerOverride{}, isDependent{ isDependent }, identity{ identity } {}
        Port(const Port& other) = delete;
        Port(Port&& other) = delete;
        virtual ~Port() = default;

        Port& operator=(const Port& other) = delete;
        Port& operator=(Port&& other) = delete;

        /**
         * The value the user gave this port, used while it is unconnected, in place of the declared
         * initializer. GraphInstance creates it from the declared value (or zero) when the type is
         * scalar; null otherwise.
         */
        inline VCL::ConstantScalar* GetInitializerOverride() { return initializerOverride ? &*initializerOverride : nullptr; }
        inline void SetInitializerOverride(const VCL::ConstantScalar& value) { initializerOverride.emplace(value); }

        inline Identity GetOwner() const { return owner; }
        /**
         * The declared type. For a dependent port it names the node's AutoParameters; the types
         * inferred from the connections are in an ElaboratedGraph (`GetPortType`).
         */
        inline VCL::Type* GetType() const { return type; }
        inline const std::string& GetDisplayName() const { return displayName; }
        inline void SetDisplayName(const std::string& displayName) { this->displayName = displayName; }
        inline PortKind GetKind() const { return kind; }
        inline VCL::ConstantValue* GetInitializer() const { return initializer; }
        inline bool IsDependent() const { return isDependent; }
        inline Identity GetIdentity() const { return identity; }

    private:
        Identity owner;
        VCL::Type* type;
        std::string displayName;
        PortKind kind;
        VCL::ConstantValue* initializer;
        std::optional<VCL::ConstantScalar> initializerOverride;
        bool isDependent;
        Identity identity;
    };

}