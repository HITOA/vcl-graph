#include "Common/GraphTest.hpp"


static std::string NodePath(VCLG::GraphInstance& graph, VCLG::Node* node) {
    return "g" + std::to_string(graph.GetIdentity()) + "/n" + std::to_string(node->GetIdentity());
}

TEST_CASE_METHOD(Test::GraphTest, "An error names the node it comes from", "[Graph][Diagnostics]") {
    // A Feedback Input that no Feedback Output reads used to fail the compile without a message.
    auto graph = context.CreateInstance();
    auto* loopIn = graph->InstantiateBuiltinNode<VCLG::FeedbackInputNode>();

    REQUIRE_FALSE(Emit(*graph, [](llvm::Module&) {}));
    const std::string* path = consumer.FindError("feedback is never read");
    REQUIRE(path != nullptr);
    REQUIRE(*path == NodePath(*graph, loopIn));
    REQUIRE(VCLG::NodeDiagnosticScope::Current() == nullptr);
}

TEST_CASE_METHOD(Test::GraphTest, "An error inside a subgraph names the use and the inner node", "[Graph][Diagnostics]") {
    auto sub = context.CreateInstance();
    auto* out = sub->InstantiateBuiltinNode<VCLG::SubgraphOutputNode>();

    auto root = context.CreateInstance();
    auto* use = root->InstantiateBuiltinNode<VCLG::SubgraphNode>();
    use->SetGraph(sub);
    auto* sink = AddNode(*root, "Add");
    sink->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(root->Connect(use->GetOutputs()[0], sink->GetInputs()[0]) != INVALID_IDENTITY);

    REQUIRE_FALSE(Emit(*root, [](llvm::Module&) {}));
    const std::string* path = consumer.FindError("subgraph output is not connected");
    REQUIRE(path != nullptr);
    REQUIRE(*path == NodePath(*root, use) + "/n" + std::to_string(out->GetIdentity()));
    REQUIRE(VCLG::NodeDiagnosticScope::Current() == nullptr);
}

TEST_CASE_METHOD(Test::GraphTest, "Scopes nest and restore the enclosing one", "[Graph][Diagnostics]") {
    REQUIRE(VCLG::NodeDiagnosticScope::Current() == nullptr);
    {
        VCLG::NodeDiagnosticScope outer{ "g1/n2", "Voice" };
        {
            VCLG::NodeDiagnosticScope inner{ "g1/n2/n5", "Low-Pass" };
            REQUIRE(VCLG::NodeDiagnosticScope::Current() == &inner);
            REQUIRE(inner.GetParent() == &outer);
        }
        REQUIRE(VCLG::NodeDiagnosticScope::Current() == &outer);
    }
    REQUIRE(VCLG::NodeDiagnosticScope::Current() == nullptr);
}
