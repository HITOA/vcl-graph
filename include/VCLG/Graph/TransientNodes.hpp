#pragma once

#include <VCLG/Graph/Node.hpp>

#include <string>


namespace VCLG {

    class SubgraphOutputNode : public TransientNode {
    public:
        SubgraphOutputNode(size_t size, GraphInstance& owner, Identity identity) : 
                TransientNode{ size, owner, "New Output", identity } {}
        ~SubgraphOutputNode();

        void Initialize() override;

        inline void SetDisplayName(const std::string& name) { displayName = name; }
    };

    class SubgraphInputNode : public TransientNode {
    public:
        SubgraphInputNode(size_t size, GraphInstance& owner, Identity identity) : 
                TransientNode{ size, owner, "New Input", identity } {}
        ~SubgraphInputNode();

        void Initialize() override;

        inline VCL::Type* GetType() const { return type; }

        inline void SetDisplayName(const std::string& name) { displayName = name; }
    
    protected:
        VCL::Type* type{};
    };

}