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
    compiled.Reset();
    compiled.Main();
    REQUIRE(*compiled.Output<float>(NodePath(*graph, first), "output") == 5.0f);
    REQUIRE(*compiled.Output<float>(NodePath(*graph, second), "output") == 15.0f);
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
    compiled.Reset();
    compiled.Main();
    REQUIRE(*compiled.Output<float>(NodePath(*graph, scale), "output") == 12.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "Reset runs the nodes' [NodeReset]", "[Graph][Execution]") {
    auto graph = context.CreateInstance();
    auto* counter = AddNode(*graph, "Counter");
    counter->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);

    Test::CompiledGraph compiled = Compile(*graph);
    float* output = compiled.Output<float>(NodePath(*graph, counter), "output");
    // Before any Reset, the state block is as the host allocated it: zeros.
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
    float* output = compiled.Output<float>(NodePath(*graph, body), "output");
    compiled.Reset();
    compiled.Main();
    REQUIRE(*output == 1.0f);
    compiled.Main();
    REQUIRE(*output == 2.0f);
    compiled.Main();
    REQUIRE(*output == 3.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "A feedback loop of a type inferred from AutoParameters", "[Graph][Execution]") {
    // StereoSource -> BlockGain -> Feedback Input; Feedback Output -> BlockGain (output). The loop's
    // type, Block<float32, 2>, exists only as the elaborated type of the first BlockGain's output.
    auto graph = context.CreateInstance();
    auto* loopIn = graph->InstantiateBuiltinNode<VCLG::FeedbackInputNode>();
    auto* loopOut = graph->InstantiateBuiltinNode<VCLG::FeedbackOutputNode>();
    loopOut->Update(loopIn->GetIdentity());
    auto* source = AddNode(*graph, "StereoSource");
    auto* gain = AddNode(*graph, "BlockGain");
    REQUIRE(Connect(*graph, source->GetOutputs()[0], gain->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(Connect(*graph, gain->GetOutputs()[0], loopIn->GetInputs()[0]) != INVALID_IDENTITY);

    // Relinking the Feedback Output (as Grog does after every connection to a Feedback Input) takes
    // the new type; it used to crash on a type without a canonical form.
    loopOut->Update(loopIn->GetIdentity());
    REQUIRE(loopOut->GetOutputs().size() == 1);
    // ...and that type has a canonical form (the instantiation of Block<float32, 2>).
    REQUIRE(VCL::Type::GetCanonicalType(loopOut->GetOutputs()[0]->GetType()) != nullptr);

    auto* sink = AddNode(*graph, "BlockGain");
    sink->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(Connect(*graph, loopOut->GetOutputs()[0], sink->GetInputs()[0]) != INVALID_IDENTITY);

    // The sink is a side branch of the loop; the steady state is checked here (Planned.cpp checks
    // that a side branch reads the previous value).
    Test::CompiledGraph compiled = Compile(*graph);
    float* output = compiled.Output<float>(NodePath(*graph, sink), "output");
    compiled.Reset();
    for (int i = 0; i < 3; ++i)
        compiled.Main();
    REQUIRE(output[0] == 0.25f);
    REQUIRE(output[1] == 0.5f);
}
