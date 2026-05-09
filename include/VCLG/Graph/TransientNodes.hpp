#pragma once

#include <VCLG/Graph/Node.hpp>

#include <string>


namespace VCLG {

    class SubgraphOutputNode : public TransientNode {
    public:
        SubgraphOutputNode(size_t size, GraphInstance& owner, Identity identity, const std::string& displayName) : 
                TransientNode{ size, owner, displayName, identity } {}
        ~SubgraphOutputNode();

        void Initialize() override;

        inline std::string& GetDisplayNameString() { return displayName; }
    };

    class SubgraphInputNode : public TransientNode {
    public:
        SubgraphInputNode(size_t size, GraphInstance& owner, Identity identity, const std::string& displayName, VCL::Type* type) : 
                type{ type }, TransientNode{ size, owner, displayName, identity } {}
        ~SubgraphInputNode();

        void Initialize() override;

        inline VCL::Type* GetType() const { return type; }

        inline std::string& GetDisplayNameString() { return displayName; }
    
    private:
        VCL::Type* type;
    };

}