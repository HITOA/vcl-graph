#include "Common/GraphTest.hpp"

#include <VCL/AST/ConstantValue.hpp>

#include <algorithm>



// A subgraph containing one Counter, exposed through a Subgraph Output.
static std::shared_ptr<VCLG::GraphInstance> MakeCounterSubgraph(Test::GraphTest& test) {
    auto sub = test.context.CreateInstance();
    auto* counter = test.AddNode(*sub, "Counter");
    auto* out = sub->InstantiateBuiltinNode<VCLG::SubgraphOutputNode>();
    REQUIRE(sub->Connect(counter->GetOutputs()[0], out->GetInputs()[0]) != INVALID_IDENTITY);
    return sub;
}

// Uses `sub` twice in `root`, each use feeding its own output node.
static void UseTwice(Test::GraphTest& test, VCLG::GraphInstance& root, std::shared_ptr<VCLG::GraphInstance> sub) {
    for (int i = 0; i < 2; ++i) {
        auto* use = root.InstantiateBuiltinNode<VCLG::SubgraphNode>();
        use->SetGraph(sub);
        auto* sink = test.AddNode(root, "Add");
        sink->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
        REQUIRE(root.Connect(use->GetOutputs()[0], sink->GetInputs()[0]) != INVALID_IDENTITY);
    }
}

// Every node's state variable `state` (the counters'), after running Main three times.
static std::vector<float> CounterStates(Test::GraphTest& test, VCLG::GraphInstance& root) {
    Test::CompiledGraph compiled = test.Compile(root);
    for (int i = 0; i < 3; ++i)
        compiled.Main();
    return compiled.StateValues<float>("state");
}

TEST_CASE_METHOD(Test::GraphTest, "Each use of a subgraph has its own state", "[Graph][Subgraph]") {
    // The same subgraph used twice is compiled twice, under two different graph paths.
    auto sub = MakeCounterSubgraph(*this);
    auto root = context.CreateInstance();
    UseTwice(*this, *root, sub);

    std::vector<float> states = CounterStates(*this, *root);
    REQUIRE(states.size() == 2);
    for (float state : states)
        REQUIRE(state == 3.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "Nested uses of a subgraph have their own state", "[Graph][Subgraph]") {
    // `middle` uses the counter subgraph twice (summed); the root uses `middle` twice: 4 counters.
    auto counter = MakeCounterSubgraph(*this);
    auto middle = context.CreateInstance();
    auto* first = middle->InstantiateBuiltinNode<VCLG::SubgraphNode>();
    first->SetGraph(counter);
    auto* second = middle->InstantiateBuiltinNode<VCLG::SubgraphNode>();
    second->SetGraph(counter);
    auto* sum = AddNode(*middle, "Add");
    auto* out = middle->InstantiateBuiltinNode<VCLG::SubgraphOutputNode>();
    REQUIRE(middle->Connect(first->GetOutputs()[0], sum->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(middle->Connect(second->GetOutputs()[0], sum->GetInputs()[1]) != INVALID_IDENTITY);
    REQUIRE(middle->Connect(sum->GetOutputs()[0], out->GetInputs()[0]) != INVALID_IDENTITY);

    auto root = context.CreateInstance();
    UseTwice(*this, *root, middle);

    std::vector<float> states = CounterStates(*this, *root);
    REQUIRE(states.size() == 4);
    for (float state : states)
        REQUIRE(state == 3.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "Values flow into and out of a subgraph", "[Graph][Subgraph]") {
    // sub: in -> Add(+1) -> out.  root: 5 -> sub -> sink
    auto sub = context.CreateInstance();
    VCL::Type* float32 = context.GetGlobalASTContext().GetTypeCache().GetOrCreateBuiltinType(VCL::BuiltinType::Float32);
    auto* in = sub->InstantiateBuiltinNode<VCLG::SubgraphInputNode>(float32);
    auto* add = AddNode(*sub, "Add");
    auto* out = sub->InstantiateBuiltinNode<VCLG::SubgraphOutputNode>();
    VCL::ConstantScalar one{ 1.0f };
    add->GetInputs()[1]->SetInitializerOverride(one);
    REQUIRE(sub->Connect(in->GetOutputs()[0], add->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(sub->Connect(add->GetOutputs()[0], out->GetInputs()[0]) != INVALID_IDENTITY);

    auto root = context.CreateInstance();
    auto* source = AddNode(*root, "Add");
    VCL::ConstantScalar five{ 5.0f };
    source->GetInputs()[0]->SetInitializerOverride(five);
    auto* use = root->InstantiateBuiltinNode<VCLG::SubgraphNode>();
    use->SetGraph(sub);
    auto* sink = AddNode(*root, "Add");
    sink->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(use->GetInputs().size() == 1);
    REQUIRE(use->GetOutputs().size() == 1);
    REQUIRE(root->Connect(source->GetOutputs()[0], use->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(root->Connect(use->GetOutputs()[0], sink->GetInputs()[0]) != INVALID_IDENTITY);

    Test::CompiledGraph compiled = Compile(*root);
    compiled.Reset();
    compiled.Main();
    REQUIRE(*compiled.Output<float>(NodePath(*root, sink), "output") == 6.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "An edit inside a subgraph changes the recompiled graph", "[Graph][Subgraph]") {
    // Counter -> Scale (parameter) -> Add (input B unconnected) -> Subgraph Output.
    auto sub = context.CreateInstance();
    auto* counter = AddNode(*sub, "Counter");
    auto* scale = dynamic_cast<VCLG::SourceNode*>(AddNode(*sub, "Scale"));
    auto* add = AddNode(*sub, "Add");
    auto* out = sub->InstantiateBuiltinNode<VCLG::SubgraphOutputNode>();
    REQUIRE(sub->Connect(counter->GetOutputs()[0], scale->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(sub->Connect(scale->GetOutputs()[0], add->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(sub->Connect(add->GetOutputs()[0], out->GetInputs()[0]) != INVALID_IDENTITY);
    auto root = context.CreateInstance();
    auto* use = root->InstantiateBuiltinNode<VCLG::SubgraphNode>();
    use->SetGraph(sub);
    auto* sink = AddNode(*root, "Add");
    sink->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(root->Connect(use->GetOutputs()[0], sink->GetInputs()[0]) != INVALID_IDENTITY);
    std::string s = NodePath(*root, sink);

    auto run = [&]() {
        Test::CompiledGraph compiled = Compile(*root);
        compiled.Reset();
        compiled.Main();
        return *compiled.Output<float>(s, "output");
    };
    REQUIRE(run() == 22.0f);
    scale->GetParameters()[0]->SetInitializerOverride(VCL::ConstantScalar{ 3.0f });
    REQUIRE(run() == 33.0f);
    add->GetInputs()[1]->SetInitializerOverride(VCL::ConstantScalar{ 1.0f });
    REQUIRE(run() == 34.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "FindNodePaths names every copy of a subgraph's node as elaboration does", "[Graph][Subgraph]") {
    // The root uses `middle` twice; `middle` uses the counter subgraph twice: 4 counters.
    auto counter = MakeCounterSubgraph(*this);
    auto middle = context.CreateInstance();
    auto* first = middle->InstantiateBuiltinNode<VCLG::SubgraphNode>();
    first->SetGraph(counter);
    auto* second = middle->InstantiateBuiltinNode<VCLG::SubgraphNode>();
    second->SetGraph(counter);
    auto* sum = AddNode(*middle, "Add");
    auto* out = middle->InstantiateBuiltinNode<VCLG::SubgraphOutputNode>();
    REQUIRE(middle->Connect(first->GetOutputs()[0], sum->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(middle->Connect(second->GetOutputs()[0], sum->GetInputs()[1]) != INVALID_IDENTITY);
    REQUIRE(middle->Connect(sum->GetOutputs()[0], out->GetInputs()[0]) != INVALID_IDENTITY);
    auto root = context.CreateInstance();
    UseTwice(*this, *root, middle);
    auto unused = context.CreateInstance();

    VCLG::Node* counterNode = nullptr;
    for (VCLG::Node* node : counter->GetNodes())
        if (node->GetKind() == VCLG::Node::NodeKind::Source)
            counterNode = node;
    REQUIRE(counterNode != nullptr);

    VCLG::ElaboratedGraph elaborated = VCLG::Elaborate(*root);
    REQUIRE(elaborated.Succeeded());
    auto expected = [&](VCLG::Identity graph, VCLG::Identity node) {
        std::vector<std::string> paths{};
        for (const VCLG::ElaboratedGraph::Node& n : elaborated.GetNodes())
            if (elaborated.GetScopes()[n.scope].graph == graph && n.path.ends_with("/n" + std::to_string(node)))
                paths.push_back(n.path);
        std::sort(paths.begin(), paths.end());
        return paths;
    };
    auto found = [&](VCLG::GraphInstance& graph, VCLG::Identity node) {
        std::vector<std::string> paths = VCLG::FindNodePaths(*root, graph, node);
        std::sort(paths.begin(), paths.end());
        return paths;
    };

    REQUIRE(found(*counter, counterNode->GetIdentity()).size() == 4);
    REQUIRE(found(*counter, counterNode->GetIdentity()) == expected(counter->GetIdentity(), counterNode->GetIdentity()));
    REQUIRE(found(*middle, sum->GetIdentity()).size() == 2);
    REQUIRE(found(*middle, sum->GetIdentity()) == expected(middle->GetIdentity(), sum->GetIdentity()));
    VCLG::Node* sink = root->GetNodes().back();
    REQUIRE(found(*root, sink->GetIdentity()) == std::vector<std::string>{ NodePath(*root, sink) });
    REQUIRE(VCLG::FindNodePaths(*root, *unused, 1).empty());
}
