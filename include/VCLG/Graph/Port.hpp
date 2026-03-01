#pragma once

#include <VCLG/Core/IdentityProvider.hpp>

#include <VCL/AST/Type.hpp>


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
        Port(Identity owner, VCL::Type* type, const std::string& displayName, PortKind kind, Identity identity) :
                owner{ owner }, type{ type }, displayName{ displayName }, kind{ kind }, identity{ identity } {}
        Port(const Port& other) = delete;
        Port(Port&& other) = delete;
        ~Port() = default;

        Port& operator=(const Port& other) = delete;
        Port& operator=(Port&& other) = delete;

        inline Identity GetOwner() const { return owner; }
        inline VCL::Type* GetType() const { return type; }
        inline bool IsDependent() const { return type->IsDependent(); }
        inline const std::string& GetDisplayName() const { return displayName; }
        inline PortKind GetKind() const { return kind; }
        inline Identity GetIdentity() const { return identity; }

    private:
        Identity owner;
        VCL::Type* type;
        std::string displayName;
        PortKind kind;
        Identity identity;
    };

}