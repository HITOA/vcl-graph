#include "Common/GraphTest.hpp"



TEST_CASE_METHOD(Test::GraphTest, "Recompiling with a cached library", "[Graph][Lifetime][Regression]") {
    // Like Grog: every compile creates new node ASTs while the imported library stays cached.
    // StructUser instantiates a library template with its own struct; that instantiation must not
    // stay attached to the library after the node's AST is gone (vcl-review.md T3).
    for (int compile = 0; compile < 5; ++compile) {
        auto graph = context.CreateInstance();
        auto* node = AddNode(*graph, "StructUser");
        node->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
        Test::CompiledGraph compiled = Compile(*graph);
        compiled.Reset();
        compiled.Main();
        REQUIRE(*compiled.Output<float>(NodePath(*graph, node), "output") == 3.5f);
    }
    REQUIRE(consumer.errors.empty());
}

TEST_CASE_METHOD(Test::GraphTest, "Many graphs in one context", "[Graph][Lifetime]") {
    // Builds, compiles and destroys graphs repeatedly; with VCLG_SANITIZE this catches leaks and
    // use-after-free in definitions, ports and type caches.
    for (int i = 0; i < 20; ++i) {
        auto graph = context.CreateInstance();
        auto* counter = AddNode(*graph, "Counter");
        auto* scale = AddNode(*graph, "Scale");
        auto* pass = AddNode(*graph, "Passthrough");
        pass->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
        REQUIRE(Connect(*graph, counter->GetOutputs()[0], scale->GetInputs()[0]) != INVALID_IDENTITY);
        REQUIRE(Connect(*graph, scale->GetOutputs()[0], pass->GetInputs()[0]) != INVALID_IDENTITY);
        Test::CompiledGraph compiled = Compile(*graph);
        compiled.Main();
        REQUIRE(*compiled.Output<float>(NodePath(*graph, pass), "output") == 2.0f);
        graph->DestroyNode(scale);
    }
}
