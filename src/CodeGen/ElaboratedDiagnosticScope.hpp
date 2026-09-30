#pragma once

#include <VCLG/Core/Diagnostics.hpp>
#include <VCLG/Graph/Elaboration.hpp>

#include <llvm/ADT/SmallVector.h>

#include <memory>
#include <vector>


namespace VCLG {

    /**
     * Opens the diagnostic scopes of the subgraph uses enclosing `scope` (outermost first), then
     * the scope of `node` itself, so that what's reported meanwhile is attributed to that node.
     */
    class ElaboratedDiagnosticScope {
    public:
        ElaboratedDiagnosticScope(const ElaboratedGraph& graph, ElaboratedGraph::ScopeIndex scope, ElaboratedGraph::NodeIndex node) {
            llvm::SmallVector<ElaboratedGraph::ScopeIndex, 4> chain{};
            for (ElaboratedGraph::ScopeIndex s = scope; s != 0 && s != ElaboratedGraph::Invalid; s = graph.GetScopes()[s].parent)
                chain.push_back(s);
            for (auto it = chain.rbegin(); it != chain.rend(); ++it)
                scopes.push_back(std::make_unique<NodeDiagnosticScope>(graph.GetScopes()[*it].path, graph.GetScopes()[*it].displayName));
            if (node != ElaboratedGraph::Invalid)
                scopes.push_back(std::make_unique<NodeDiagnosticScope>(graph.GetNode(node).path, graph.GetNode(node).displayName));
        }

        /** The scope of `node`, inside the subgraph uses enclosing it. */
        ElaboratedDiagnosticScope(const ElaboratedGraph& graph, ElaboratedGraph::NodeIndex node) :
                ElaboratedDiagnosticScope{ graph, graph.GetNode(node).scope, node } {}

        ~ElaboratedDiagnosticScope() {
            // Innermost first: each scope restores its parent.
            while (!scopes.empty())
                scopes.pop_back();
        }

    private:
        std::vector<std::unique_ptr<NodeDiagnosticScope>> scopes{};
    };

}
