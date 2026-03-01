#pragma once

#include <VCLG/Core/IdentityProvider.hpp>


namespace VCLG {
    class Node;
    class Port;

    class Connection {
    public:
        Connection() = delete;
        Connection(Identity inPortIdentity, Identity outPortIdentity, Identity selfIdentity) :
                inPortIdentity{ inPortIdentity }, outPortIdentity{ outPortIdentity }, selfIdentity{ selfIdentity } {}
        Connection(const Connection& other) = default;
        Connection(Connection&& other) = default;
        ~Connection() = default;
        
        Connection& operator=(const Connection& other) = default;
        Connection& operator=(Connection&& other) = default;

        inline Identity GetIdentity() const { return selfIdentity; }
        inline Identity GetInputPortIdentity() const { return inPortIdentity; }
        inline Identity GetOutputPortIdentity() const { return outPortIdentity; }

    private:
        Identity selfIdentity;
        Identity inPortIdentity;
        Identity outPortIdentity;
    };

}