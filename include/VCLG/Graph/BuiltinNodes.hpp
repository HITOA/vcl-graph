#pragma once

#include <VCLG/Graph/Node.hpp>

#include <string>
#include <memory>


namespace VCLG {
    class GraphInstance;

    class SubgraphOutputNode final : public BuiltinNode {
    public:
        SubgraphOutputNode(GraphInstance& owner, Identity identity) : 
                BuiltinNode{ NodeKind::SubgraphOutput, owner, "New Output", identity } {}

        static bool classof(const Node* node) { return node->GetKind() == NodeKind::SubgraphOutput; }

        void Initialize() override;
        void Destroy() override;
        bool Emit(CodeGenGraph& codegen) override;

        VCL::Type* GetType();

        using Node::SetDisplayName;
    };

    /**
     * A subgraph input of a fixed type, given at creation (e.g. a host's control, gate and audio
     * inputs are all SubgraphInputNodes with different types).
     */
    class SubgraphInputNode final : public BuiltinNode {
    public:
        SubgraphInputNode(GraphInstance& owner, Identity identity, VCL::Type* type) : 
                BuiltinNode{ NodeKind::SubgraphInput, owner, "New Input", identity }, type{ type } {}

        static bool classof(const Node* node) { return node->GetKind() == NodeKind::SubgraphInput; }

        void Initialize() override;
        void Destroy() override;
        bool Emit(CodeGenGraph& codegen) override;

        VCL::Type* GetType();

        using Node::SetDisplayName;
    
    private:
        VCL::Type* type;
    };

    class SubgraphNode final : public BuiltinNode {
    public:
        using IdentityMap = llvm::DenseMap<VCLG::Identity, VCLG::Identity>;

        SubgraphNode(GraphInstance& owner, Identity identity) :
            BuiltinNode{ NodeKind::Subgraph, owner, "Subgraph", identity }, instance{ nullptr }, nodeToPort{} {}

        static bool classof(const Node* node) { return node->GetKind() == NodeKind::Subgraph; }
        
        void Initialize() override;
        void Destroy() override;
        bool Emit(CodeGenGraph& codegen) override;
        void SetGraph(std::shared_ptr<GraphInstance> instance);
        void Update();

        inline std::shared_ptr<GraphInstance> GetGraph() { return instance; }

    private:
        std::shared_ptr<GraphInstance> instance;
        IdentityMap nodeToPort;
    };

    class FeedbackInputNode final : public BuiltinNode {
    public:
        FeedbackInputNode(GraphInstance& owner, Identity identity) : 
                BuiltinNode{ NodeKind::FeedbackInput, owner, "New Feedback Input", identity } {}

        static bool classof(const Node* node) { return node->GetKind() == NodeKind::FeedbackInput; }

        void Initialize() override;
        void Destroy() override;
        bool Emit(CodeGenGraph& codegen) override;

        VCL::Type* GetType();

        using Node::SetDisplayName;
    };

    class FeedbackOutputNode final : public BuiltinNode {
    public:
        FeedbackOutputNode(GraphInstance& owner, Identity identity) :
            BuiltinNode{ NodeKind::FeedbackOutput, owner, "Feedback Output", identity }, feedbackIdentity{ INVALID_IDENTITY } {}

        static bool classof(const Node* node) { return node->GetKind() == NodeKind::FeedbackOutput; }
        
        void Initialize() override;
        void Destroy() override;
        bool Emit(CodeGenGraph& codegen) override;
        void Update(Identity feedbackIdentity);

        inline Identity GetFeedbackIdentity() const { return feedbackIdentity; }

    private:
        Identity feedbackIdentity;
    };

}