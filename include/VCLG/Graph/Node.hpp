#pragma once

#include <VCLG/Core/IdentityProvider.hpp>

#include <VCL/Core/Source.hpp>

#include <llvm/ADT/SmallVector.h>

#include <string>
#include <cstdint>


namespace VCLG {
    class Port;

    class Node {
    public:
        enum NodeClass {
            SourceNodeClass,
            SubGraphNodeClass,
            TransientNodeClass
        };

        enum class NodeFlag : uint32_t {
            None = 0,
            IsInputNode = 1,
            IsOutputNode = 2
        };

    public:
        Node() = delete;
        Node(NodeClass nodeClass, Identity identity) : nodeClass{ nodeClass }, identity{ identity }, flags{ NodeFlag::None } {}
        Node(const Node& other) = delete;
        Node(Node&& other) = delete;
        ~Node() = default;

        Node& operator=(const Node& other) = delete;
        Node& operator=(Node&& other) = delete;

        inline NodeClass GetNodeClass() const { return nodeClass; }
        inline Identity GetIdentity() const { return identity; }

        inline bool HasFlag(NodeFlag flag) const { return ((uint32_t)flags & (uint32_t)flag) != 0; }
        inline void AddFlag(NodeFlag flag) { this->flags = (NodeFlag)((uint32_t)flags | (uint32_t)flag); }

    private:
        NodeClass nodeClass;
        NodeFlag flags;
        Identity identity;
    };

    class SourceNode : public Node {    
    public:
        SourceNode(const std::string& source, llvm::ArrayRef<Port*> inPorts, llvm::ArrayRef<Port*> outPorts, Identity identity) :
                source{ source }, inPorts{ inPorts }, outPorts{ outPorts }, Node{ Node::SourceNodeClass, identity } {}
        ~SourceNode() = default;
        
        inline llvm::StringRef GetSource() const { return source; }
        inline llvm::ArrayRef<Port*> GetInputs() const { return inPorts; }
        inline llvm::ArrayRef<Port*> GetOutputs() const { return outPorts; }

    private:
        std::string source;
        llvm::SmallVector<Port*, llvm::CalculateSmallVectorDefaultInlinedElements<Port*>::value / 2> inPorts;
        llvm::SmallVector<Port*, llvm::CalculateSmallVectorDefaultInlinedElements<Port*>::value / 2> outPorts;
    };

    class SubGraphNode : public Node {

    };

    class TransientNode : public Node {

    };

}