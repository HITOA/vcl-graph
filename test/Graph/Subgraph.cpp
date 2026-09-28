#include "Common/GraphTest.hpp"

#include <VCL/AST/ConstantValue.hpp>


// A subgraph containing one Counter, exposed through a Subgraph Output.
static std::shared_ptr<VCLG::GraphInstance> MakeCounterSubgraph(Test::GraphTest& test) {
    auto sub = test.context.CreateInstance();
    auto* counter = test.AddNode(*sub, "Counter");
    auto* out = sub->InstantiateTransientNode<VCLG::SubgraphOutputNode>();
    REQUIRE(sub->Connect(counter->GetOutputs()[0], out->GetInputs()[0]) != INVALID_IDENTITY);
    return sub;
}

// Uses `sub` twice in `root`, each use feeding its own output node.
static void UseTwice(Test::GraphTest& test, VCLG::GraphInstance& root, std::shared_ptr<VCLG::GraphInstance> sub) {
    for (int i = 0; i < 2; ++i) {
        auto* use = root.InstantiateTransientNode<VCLG::SubgraphNode>();
        use->SetGraph(sub);
        auto* sink = test.AddNode(root, "Add");
        sink->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
        REQUIRE(root.Connect(use->GetOutputs()[0], sink->GetInputs()[0]) != INVALID_IDENTITY);
    }
}

// Every global whose name ends with ".state", after running Main three times.
static std::vector<float> CounterStates(Test::GraphTest& test, VCLG::GraphInstance& root) {
    std::vector<std::string> names{};
    REQUIRE(test.Emit(root, [&](llvm::Module& m) {
        for (llvm::GlobalVariable& global : m.globals())
            if (global.getName().ends_with(".state"))
                names.push_back(global.getName().str());
    }));
    Test::CompiledGraph compiled = test.Compile(root);
    for (int i = 0; i < 3; ++i)
        compiled.Main();
    std::vector<float> values{};
    for (const std::string& name : names)
        values.push_back(*compiled.Global<float>(name));
    return values;
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
    auto* first = middle->InstantiateTransientNode<VCLG::SubgraphNode>();
    first->SetGraph(counter);
    auto* second = middle->InstantiateTransientNode<VCLG::SubgraphNode>();
    second->SetGraph(counter);
    auto* sum = AddNode(*middle, "Add");
    auto* out = middle->InstantiateTransientNode<VCLG::SubgraphOutputNode>();
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
    auto* in = sub->InstantiateTransientNode<Test::FloatSubgraphInputNode>();
    auto* add = AddNode(*sub, "Add");
    auto* out = sub->InstantiateTransientNode<VCLG::SubgraphOutputNode>();
    VCL::ConstantScalar one{ 1.0f };
    add->GetInputs()[1]->SetInitializerOverride(&one);
    REQUIRE(sub->Connect(in->GetOutputs()[0], add->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(sub->Connect(add->GetOutputs()[0], out->GetInputs()[0]) != INVALID_IDENTITY);

    auto root = context.CreateInstance();
    auto* source = AddNode(*root, "Add");
    VCL::ConstantScalar five{ 5.0f };
    source->GetInputs()[0]->SetInitializerOverride(&five);
    auto* use = root->InstantiateTransientNode<VCLG::SubgraphNode>();
    use->SetGraph(sub);
    auto* sink = AddNode(*root, "Add");
    sink->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(use->GetInputs().size() == 1);
    REQUIRE(use->GetOutputs().size() == 1);
    REQUIRE(root->Connect(source->GetOutputs()[0], use->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(root->Connect(use->GetOutputs()[0], sink->GetInputs()[0]) != INVALID_IDENTITY);

    Test::CompiledGraph compiled = Compile(*root);
    compiled.Main();
    REQUIRE(*compiled.Global<float>(NodeSymbol(*root, sink, "output")) == 6.0f);
}
