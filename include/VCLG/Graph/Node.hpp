#pragma once

#include <VCLG/Core/IdentityProvider.hpp>
#include <VCLG/Core/SubstitutionTable.hpp>

#include <VCL/Core/Source.hpp>

#include <llvm/ADT/SmallVector.h>

#include <string>
#include <cstdint>


namespace VCLG {
    class Port;
    class Parameter;

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
            IsOutputNode = 2,
            IsDependent = 4
        };

    public:
        Node() = delete;
        Node(NodeClass nodeClass, Identity identity) : nodeClass{ nodeClass }, identity{ identity }, table{}, flags{ NodeFlag::None } {}
        Node(const Node& other) = delete;
        Node(Node&& other) = delete;
        virtual ~Node() = default;

        Node& operator=(const Node& other) = delete;
        Node& operator=(Node&& other) = delete;

        inline NodeClass GetNodeClass() const { return nodeClass; }
        inline Identity GetIdentity() const { return identity; }

        inline bool HasFlag(NodeFlag flag) const { return ((uint32_t)flags & (uint32_t)flag) != 0; }
        inline void AddFlag(NodeFlag flag) { this->flags = (NodeFlag)((uint32_t)flags | (uint32_t)flag); }

        inline SubstitutionTable& GetSubstitutionTable() { return table; }
        
        static llvm::ArrayRef<Port*> GetNodeInputs(Node* node);
        static llvm::ArrayRef<Port*> GetNodeOutputs(Node* node);

    private:
        NodeClass nodeClass;
        NodeFlag flags;

        SubstitutionTable table;

        Identity identity;
    };

    class SourceNode : public Node {    
    public:
        SourceNode(const std::string& source, llvm::StringRef displayName, llvm::ArrayRef<Port*> inPorts, llvm::ArrayRef<Port*> outPorts, 
                    llvm::ArrayRef<Parameter*> parameters, Identity identity) :
                source{ source }, displayName{ displayName }, inPorts{ inPorts }, outPorts{ outPorts },
                    parameters{ parameters }, Node{ Node::SourceNodeClass, identity } {}
        ~SourceNode() = default;
        
        inline llvm::StringRef GetSource() const { return source; }
        inline llvm::StringRef GetDisplayName() const { return displayName; }
        inline llvm::ArrayRef<Port*> GetInputs() const { return inPorts; }
        inline llvm::ArrayRef<Port*> GetOutputs() const { return outPorts; }
        inline llvm::ArrayRef<Parameter*> GetParameters() const { return parameters; }

    private:
        std::string source;
        llvm::StringRef displayName;
        llvm::SmallVector<Port*, 4> inPorts;
        llvm::SmallVector<Port*, 4> outPorts;
        llvm::SmallVector<Parameter*, 4> parameters;
    };

    class SubGraphNode : public Node {

    };

    class TransientNode : public Node {

    };

}