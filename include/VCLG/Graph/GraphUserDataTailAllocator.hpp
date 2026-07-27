#pragma once

#include <cstddef>


namespace VCLG {
    class Node;
    class Port;
    class Parameter;

    class GraphUserDataTrailAllocator {
    public:
        virtual void ConstructNodeUserData(Node* node, void* ptr) const {}
        virtual void DestroyNodeUserData(Node* node, void* ptr) const {}
        virtual size_t GetNodeUserDataAdditionalSize() const { return 0; }

        virtual void ConstructPortUserData(Port* port, void* ptr) const {}
        virtual void DestroyPortUserData(Port* port, void* ptr) const {}
        virtual size_t GetPortUserDataAdditionalSize() const { return 0; }

        virtual void ConstructParameterUserData(Parameter* parameter, void* ptr) const {}
        virtual void DestroyParameterUserData(Parameter* parameter, void* ptr) const {}
        virtual size_t GetParameterUserDataAdditionalSize() const { return 0; }
    };

}