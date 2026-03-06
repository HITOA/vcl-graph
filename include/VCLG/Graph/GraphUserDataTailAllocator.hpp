#pragma once

#include <cstddef>


namespace VCLG {
    class Node;
    class Port;

    class GraphUserDataTailAllocator {
    public:
        virtual void ConstructNodeUserData(Node* node, void* ptr) const {}
        virtual void DestroyNodeUserData(Node* node, void* ptr) const {}
        virtual size_t GetNodeUserDataAdditionalSize() const { return 0; }

        virtual void ConstructPortUserData(Port* port, void* ptr) const {}
        virtual void DestroyPortUserData(Port* port, void* ptr) const {}
        virtual size_t GetPortUserDataAdditionalSize() const { return 0; }
    };

}