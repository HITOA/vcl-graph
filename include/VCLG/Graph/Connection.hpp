#pragma once

#include <VCLG/Core/IdentityProvider.hpp>


namespace VCLG {
    class Node;
    class Port;
    class Converter;

    class Connection {
    public:
        Connection() = delete;
        Connection(Identity inPortIdentity, Identity outPortIdentity, Identity selfIdentity, Converter* converter) :
                inPortIdentity{ inPortIdentity }, outPortIdentity{ outPortIdentity }, selfIdentity{ selfIdentity }, converter{ converter } {}
        Connection(const Connection& other) = default;
        Connection(Connection&& other) = default;
        ~Connection() = default;
        
        Connection& operator=(const Connection& other) = default;
        Connection& operator=(Connection&& other) = default;

        inline Identity GetIdentity() const { return selfIdentity; }
        inline Identity GetInputPortIdentity() const { return inPortIdentity; }
        inline Identity GetOutputPortIdentity() const { return outPortIdentity; }
        inline Converter* GetConverter() const { return converter; }

    private:
        Identity selfIdentity;
        Identity inPortIdentity;
        Identity outPortIdentity;
        Converter* converter;
    };

}