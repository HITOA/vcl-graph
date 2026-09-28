#pragma once

#include <VCLG/Core/IdentityProvider.hpp>

#include <VCL/Core/Source.hpp>

#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/Casting.h>

#include <string>
#include <cstdint>
#include <memory>


namespace VCLG {
    class Port;
    class Parameter;
    class GraphInstance;
    class SourceNode;
    class SubgraphNode;
    class BuiltinNode;

    class Node {
    public:
        /**
         * The concrete kind of a node, for `llvm::isa` / `llvm::dyn_cast` / `llvm::cast` (each node
         * class has a `classof`). The set is closed: every kind is defined in vcl-graph, and the
         * concrete classes are `final`. Builtin kinds must stay contiguous between FirstBuiltin and
         * LastBuiltin.
         */
        enum class NodeKind : uint8_t {
            Source,
            SubgraphInput,
            SubgraphOutput,
            Subgraph,
            FeedbackInput,
            FeedbackOutput,

            FirstBuiltin = SubgraphInput,
            LastBuiltin = FeedbackOutput
        };

        enum class NodeFlag : uint32_t {
            None = 0,
            IsInputNode = 1,
            IsOutputNode = 2,
            IsDependent = 4
        };

    public:
        Node() = delete;
        Node(NodeKind kind, Identity identity, const std::string& displayName, 
                llvm::ArrayRef<Port*> inPorts = {}, llvm::ArrayRef<Port*> outPorts = {}) : 
            kind{ kind }, flags{ NodeFlag::None }, identity{ identity }, 
                displayName{ displayName }, inPorts{ inPorts }, outPorts{ outPorts } {}
        Node(const Node& other) = delete;
        Node(Node&& other) = delete;
        virtual ~Node() = default;

        Node& operator=(const Node& other) = delete;
        Node& operator=(Node&& other) = delete;

        inline NodeKind GetKind() const { return kind; }
        inline Identity GetIdentity() const { return identity; }

        inline llvm::StringRef GetDisplayName() const { return displayName; }
        inline llvm::ArrayRef<Port*> GetInputs() const { return inPorts; }
        inline llvm::ArrayRef<Port*> GetOutputs() const { return outPorts; }

        inline bool HasFlag(NodeFlag flag) const { return ((uint32_t)flags & (uint32_t)flag) != 0; }
        inline void AddFlag(NodeFlag flag) { this->flags = (NodeFlag)((uint32_t)flags | (uint32_t)flag); }

        void NormalizePortDisplayNameLength();

    protected:
        // Public only on the kinds the user can rename (subgraph inputs/outputs, feedback inputs).
        inline void SetDisplayName(const std::string& name) { displayName = name; }

    private:
        NodeKind kind;
        NodeFlag flags;

        Identity identity;

    protected:
        std::string displayName;
        llvm::SmallVector<Port*, 4> inPorts;
        llvm::SmallVector<Port*, 4> outPorts;
    };

    class SourceNode final : public Node {    
    public:
        SourceNode(const std::string& source, const std::string& displayName, llvm::ArrayRef<Port*> inPorts, llvm::ArrayRef<Port*> outPorts, 
                    llvm::ArrayRef<Parameter*> parameters, Identity identity) :
                Node{ NodeKind::Source, identity, displayName, inPorts, outPorts }, source{ source }, parameters{ parameters } {}
        ~SourceNode() = default;

        static bool classof(const Node* node) { return node->GetKind() == NodeKind::Source; }
        
        inline llvm::StringRef GetSource() const { return source; }
        inline llvm::ArrayRef<Parameter*> GetParameters() const { return parameters; }

        void NormalizePortAndParameterDisplayNameLength();

    private:
        std::string source;
        llvm::SmallVector<Parameter*, 4> parameters;
    };
    
    /**
     * Base of the nodes defined by vcl-graph itself rather than by a VCL source (see
     * BuiltinNodes.hpp). Emission is done by CodeGenGraph (a switch on NodeKind over the elaborated
     * graph); what's left here is the editing behaviour (creating and destroying ports).
     */
    class BuiltinNode : public Node {
    public:
        BuiltinNode(NodeKind kind, GraphInstance& owner, const std::string& displayName, Identity identity) : 
            Node{ kind, identity, displayName }, owner{ owner } {};
        virtual ~BuiltinNode() = default;

        static bool classof(const Node* node) {
            return node->GetKind() >= NodeKind::FirstBuiltin && node->GetKind() <= NodeKind::LastBuiltin;
        }

        virtual void Initialize() = 0;
        virtual void Destroy() = 0;

        inline GraphInstance& GetOwner() { return owner; }

    protected:
        GraphInstance& owner;
    };

}