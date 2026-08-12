#pragma once

#include <VCLG/Graph/Node.hpp>

#include <string>
#include <memory>


namespace VCLG {
    class GraphInstance;

    class SubgraphOutputNode : public TransientNode {
    public:
        SubgraphOutputNode(size_t hash, size_t size, GraphInstance& owner, Identity identity) : 
                TransientNode{ hash, size, owner, "New Output", identity } {}

        void Initialize() override;
        void Destroy() override;
        bool Emit(CodeGenGraph& codegen) override;

        virtual VCL::Type* GetType();

        inline void SetDisplayName(const std::string& name) { displayName = name; }
    };

    class SubgraphInputNode : public TransientNode {
    public:
        SubgraphInputNode(size_t hash, size_t size, GraphInstance& owner, Identity identity) : 
                TransientNode{ hash, size, owner, "New Input", identity } {}

        void Initialize() override;
        void Destroy() override;
        bool Emit(CodeGenGraph& codegen) override;

        virtual VCL::Type* GetType();

        inline void SetDisplayName(const std::string& name) { displayName = name; }
    
    protected:
        VCL::Type* type{};
    };

    class SubgraphNode : public TransientNode {
    public:
        using IdentityMap = llvm::DenseMap<VCLG::Identity, VCLG::Identity>;

        SubgraphNode(size_t hash, size_t size, GraphInstance& owner, Identity identity) :
            instance{ nullptr }, nodeToPort{}, TransientNode{ hash, size, owner, "Subgraph", identity } {}
        
        void Initialize() override;
        void Destroy() override;
        bool Emit(CodeGenGraph& codegen) override;
        void SetGraph(std::shared_ptr<GraphInstance> instance);
        void Update();

        inline std::shared_ptr<GraphInstance> GetGraph() { return instance; }

    protected:
        std::shared_ptr<GraphInstance> instance;
        IdentityMap nodeToPort;
    };

    class FeedbackInputNode : public TransientNode {
    public:
        FeedbackInputNode(size_t hash, size_t size, GraphInstance& owner, Identity identity) : 
                TransientNode{ hash, size, owner, "New Feedback Input", identity } {}

        void Initialize() override;
        void Destroy() override;
        bool Emit(CodeGenGraph& codegen) override;

        virtual VCL::Type* GetType();

        inline void SetDisplayName(const std::string& name) { displayName = name; }
    };

    class FeedbackOutputNode : public TransientNode {
    public:
        FeedbackOutputNode(size_t hash, size_t size, GraphInstance& owner, Identity identity) :
            feedbackIdentity{ INVALID_IDENTITY }, TransientNode{ hash, size, owner, "Feedback Output", identity } {}
        
        void Initialize() override;
        void Destroy() override;
        bool Emit(CodeGenGraph& codegen) override;
        void Update(Identity feedbackIdentity);

        inline Identity GetFeedbackIdentity() const { return feedbackIdentity; }

    private:
        Identity feedbackIdentity;
    };

}