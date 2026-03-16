#pragma once

#include <VCLG/Core/SubstitutionTable.hpp>
#include <VCLG/Core/Allocator.hpp>
#include <VCLG/Core/IdentityProvider.hpp>
#include <VCLG/Graph/GraphStorage.hpp>
#include <VCLG/Graph/Port.hpp>
#include <VCLG/Graph/Node.hpp>
#include <VCLG/Graph/Connection.hpp>
#include <VCLG/Graph/GraphUserDataTailAllocator.hpp>
#include <VCLG/Graph/GraphValidator.hpp>

#include <VCL/AST/Template.hpp>
#include <VCL/Frontend/CompilerInstance.hpp>

#include <llvm/ADT/DenseMap.h>
#include <llvm/Support/Allocator.h>

#include <vector>
#include <memory>
#include <unordered_set>
#include <unordered_map>


namespace VCLG {
    class GraphInstance;

    class GraphValidator {
    public:
        GraphValidator();
        GraphValidator(const GraphValidator& other) = delete;
        GraphValidator(GraphValidator&& other) = delete;
        ~GraphValidator() = default;

        GraphValidator& operator=(const GraphValidator& other) = delete;
        GraphValidator& operator=(GraphValidator&& other) = delete;

        bool Validate(GraphInstance& graph);
    
    private:
        void ClearSubstitutionTable(GraphInstance& graph);
        std::vector<Node*> BuildOrderedDependentNodeList(GraphInstance& graph);

        bool SubstituteType(Node* node, VCL::Type* baseType, VCL::Type* connectedType);
        bool SubstituteExpression(Node* node, VCL::DeclRefExpr* baseExpr, VCL::ConstantScalar* scalar);

        VCL::Type* GenerateSubstitutedType(VCL::ASTContext& globalASTContext, Node* node, VCL::Type* baseType);

    private:
        llvm::BumpPtrAllocator allocator{};

        std::unordered_map<Port*, Port*> inPortToOutPort{};
        std::unordered_set<Port*> connectedOutPort{};
    };

}