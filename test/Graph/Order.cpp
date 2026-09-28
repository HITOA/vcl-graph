#include "Common/GraphTest.hpp"

#include <algorithm>
#include <chrono>


static size_t Position(const std::vector<VCLG::Node*>& order, VCLG::Node* node) {
    auto it = std::find(order.begin(), order.end(), node);
    REQUIRE(it != order.end());
    return (size_t)(it - order.begin());
}

TEST_CASE_METHOD(Test::GraphTest, "Producers run before their consumers", "[Graph][Order]") {
    auto graph = context.CreateInstance();
    auto* source = AddNode(*graph, "Add");
    auto* left = AddNode(*graph, "Add");
    auto* right = AddNode(*graph, "Add");
    auto* sink = AddNode(*graph, "Add");
    // Created in an order that doesn't match the data flow.
    REQUIRE(Connect(*graph, left->GetOutputs()[0], sink->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(Connect(*graph, right->GetOutputs()[0], sink->GetInputs()[1]) != INVALID_IDENTITY);
    REQUIRE(Connect(*graph, source->GetOutputs()[0], left->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(Connect(*graph, source->GetOutputs()[0], right->GetInputs()[0]) != INVALID_IDENTITY);

    std::vector<VCLG::Node*> order{};
    VCLG::Node* roots[] = { sink };
    REQUIRE(graph->BuildExecutionOrder(roots, order));
    REQUIRE(order.size() == 4);
    REQUIRE(Position(order, source) < Position(order, left));
    REQUIRE(Position(order, source) < Position(order, right));
    REQUIRE(Position(order, left) < Position(order, sink));
    REQUIRE(Position(order, right) < Position(order, sink));
}

TEST_CASE_METHOD(Test::GraphTest, "Deep diamond chains stay fast", "[Graph][Order][Regression]") {
    // The old graph walks queued a node once per path: 2^40 here.
    auto graph = context.CreateInstance();
    VCLG::SourceNode* previous = AddNode(*graph, "Add");
    for (int i = 0; i < 40; ++i) {
        auto* x = AddNode(*graph, "Add");
        auto* y = AddNode(*graph, "Add");
        auto* merge = AddNode(*graph, "Add");
        REQUIRE(Connect(*graph, previous->GetOutputs()[0], x->GetInputs()[0]) != INVALID_IDENTITY);
        REQUIRE(Connect(*graph, previous->GetOutputs()[0], y->GetInputs()[0]) != INVALID_IDENTITY);
        REQUIRE(Connect(*graph, x->GetOutputs()[0], merge->GetInputs()[0]) != INVALID_IDENTITY);
        REQUIRE(Connect(*graph, y->GetOutputs()[0], merge->GetInputs()[1]) != INVALID_IDENTITY);
        previous = merge;
    }
    previous->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);

    auto start = std::chrono::steady_clock::now();
    REQUIRE(VCLG::Elaborate(*graph).Succeeded());
    REQUIRE(Emit(*graph, [](llvm::Module&) {}));
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    REQUIRE(seconds < 10.0); // generous for sanitizer builds; it takes a fraction of a second
}

TEST_CASE_METHOD(Test::GraphTest, "A Feedback Input runs after the Feedback Outputs reading it", "[Graph][Order]") {
    auto graph = context.CreateInstance();
    auto* loopIn = graph->InstantiateBuiltinNode<VCLG::FeedbackInputNode>();
    auto* loopOut = graph->InstantiateBuiltinNode<VCLG::FeedbackOutputNode>();
    loopOut->Update(loopIn->GetIdentity());
    auto* body = AddNode(*graph, "Add");
    auto* sink = AddNode(*graph, "Add");
    sink->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(Connect(*graph, loopOut->GetOutputs()[0], body->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(Connect(*graph, body->GetOutputs()[0], loopIn->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(Connect(*graph, body->GetOutputs()[0], sink->GetInputs()[0]) != INVALID_IDENTITY);

    std::vector<VCLG::Node*> order{};
    REQUIRE(graph->BuildExecutionOrder(graph->GetNodes(), order));
    REQUIRE(Position(order, loopOut) < Position(order, body));
    REQUIRE(Position(order, body) < Position(order, loopIn));
    REQUIRE(Position(order, body) < Position(order, sink));
}

TEST_CASE_METHOD(Test::GraphTest, "Only nodes feeding an output node are emitted", "[Graph][Order]") {
    auto graph = context.CreateInstance();
    auto* used = AddNode(*graph, "Counter");
    auto* sink = AddNode(*graph, "Scale");
    auto* unused = AddNode(*graph, "Counter");
    sink->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(Connect(*graph, used->GetOutputs()[0], sink->GetInputs()[0]) != INVALID_IDENTITY);

    REQUIRE(Emit(*graph, [&](llvm::Module& m) {
        REQUIRE(m.getGlobalVariable(NodeSymbol(*graph, used, "state"), true) != nullptr);
        REQUIRE(m.getGlobalVariable(NodeSymbol(*graph, unused, "state"), true) == nullptr);
    }));
}
