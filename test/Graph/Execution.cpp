#include "Common/GraphTest.hpp"

#include <VCL/AST/ConstantValue.hpp>


TEST_CASE_METHOD(Test::GraphTest, "Values flow through connections", "[Graph][Execution]") {
    auto graph = context.CreateInstance();
    auto* first = AddNode(*graph, "Add");
    auto* second = AddNode(*graph, "Add");
    second->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(Connect(*graph, first->GetOutputs()[0], second->GetInputs()[0]) != INVALID_IDENTITY);

    // Unconnected inputs take their initializer override, as Grog's port knobs do.
    VCL::ConstantScalar two{ 2.0f }, three{ 3.0f }, ten{ 10.0f };
    first->GetInputs()[0]->SetInitializerOverride(two);
    first->GetInputs()[1]->SetInitializerOverride(three);
    second->GetInputs()[1]->SetInitializerOverride(ten);

    Test::CompiledGraph compiled = Compile(*graph);
    compiled.Main();
    REQUIRE(*compiled.Global<float>(NodeSymbol(*graph, first, "output")) == 5.0f);
    REQUIRE(*compiled.Global<float>(NodeSymbol(*graph, second, "output")) == 15.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "Parameters are compiled in", "[Graph][Execution]") {
    auto graph = context.CreateInstance();
    auto* scale = AddNode(*graph, "Scale");
    scale->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    VCL::ConstantScalar factor{ 4.0f }, input{ 3.0f };
    REQUIRE(scale->GetParameters().size() == 1);
    scale->GetParameters()[0]->SetInitializerOverride(factor);
    scale->GetInputs()[0]->SetInitializerOverride(input);

    Test::CompiledGraph compiled = Compile(*graph);
    compiled.Main();
    REQUIRE(*compiled.Global<float>(NodeSymbol(*graph, scale, "output")) == 12.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "Reset runs the nodes' [NodeReset]", "[Graph][Execution]") {
    auto graph = context.CreateInstance();
    auto* counter = AddNode(*graph, "Counter");
    counter->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);

    Test::CompiledGraph compiled = Compile(*graph);
    float* output = compiled.Global<float>(NodeSymbol(*graph, counter, "output"));
    compiled.Main();
    compiled.Main();
    REQUIRE(*output == 2.0f);
    compiled.Reset();          // state = 10
    compiled.Main();
    REQUIRE(*output == 11.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "A feedback loop sees the previous run's value", "[Graph][Execution]") {
    // out = previous out + 1
    auto graph = context.CreateInstance();
    auto* loopIn = graph->InstantiateBuiltinNode<VCLG::FeedbackInputNode>();
    auto* loopOut = graph->InstantiateBuiltinNode<VCLG::FeedbackOutputNode>();
    loopOut->Update(loopIn->GetIdentity());
    auto* body = AddNode(*graph, "Add");
    body->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    VCL::ConstantScalar one{ 1.0f };
    body->GetInputs()[1]->SetInitializerOverride(one);
    REQUIRE(Connect(*graph, loopOut->GetOutputs()[0], body->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(Connect(*graph, body->GetOutputs()[0], loopIn->GetInputs()[0]) != INVALID_IDENTITY);

    Test::CompiledGraph compiled = Compile(*graph);
    float* output = compiled.Global<float>(NodeSymbol(*graph, body, "output"));
    compiled.Main();
    REQUIRE(*output == 1.0f);
    compiled.Main();
    REQUIRE(*output == 2.0f);
    compiled.Main();
    REQUIRE(*output == 3.0f);
}
