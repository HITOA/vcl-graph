#include "Common/GraphTest.hpp"


TEST_CASE_METHOD(Test::GraphTest, "A templated node takes the type connected to it", "[Graph][Inference]") {
    auto graph = context.CreateInstance();
    auto* source = AddNode(*graph, "ArraySource");
    auto* pass = AddNode(*graph, "Passthrough");
    VCL::Type* arrayType = source->GetOutputs()[0]->GetType();

    REQUIRE(pass->GetInputs()[0]->IsDependent());
    REQUIRE(pass->GetOutputs()[0]->IsDependent());
    REQUIRE(Connect(*graph, source->GetOutputs()[0], pass->GetInputs()[0]) != INVALID_IDENTITY);

    REQUIRE(VCL::Type::IsCanonicallyEqual(pass->GetInputs()[0]->GetLastType(), arrayType));
    REQUIRE(VCL::Type::IsCanonicallyEqual(pass->GetOutputs()[0]->GetLastType(), arrayType));

    SECTION("and propagates it downstream") {
        auto* next = AddNode(*graph, "Passthrough");
        REQUIRE(Connect(*graph, pass->GetOutputs()[0], next->GetInputs()[0]) != INVALID_IDENTITY);
        REQUIRE(VCL::Type::IsCanonicallyEqual(next->GetOutputs()[0]->GetLastType(), arrayType));
    }

    SECTION("and a concrete consumer of another type is refused") {
        auto* add = AddNode(*graph, "Add");
        REQUIRE(Connect(*graph, pass->GetOutputs()[0], add->GetInputs()[0]) == INVALID_IDENTITY);
    }
}

TEST_CASE_METHOD(Test::GraphTest, "A templated node compiles for the inferred type", "[Graph][Inference]") {
    auto graph = context.CreateInstance();
    auto* source = AddNode(*graph, "ArraySource");
    auto* pass = AddNode(*graph, "Passthrough");
    pass->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(Connect(*graph, source->GetOutputs()[0], pass->GetInputs()[0]) != INVALID_IDENTITY);

    Test::CompiledGraph compiled = Compile(*graph);
    compiled.Main();
    float* values = compiled.Global<float>(NodeSymbol(*graph, pass, "output"));
    REQUIRE(values != nullptr);
    for (int i = 0; i < 4; ++i)
        REQUIRE(values[i] == (float)i);
}

TEST_CASE_METHOD(Test::GraphTest, "A Feedback Input takes the type of what feeds it", "[Graph][Inference]") {
    auto graph = context.CreateInstance();
    auto* source = AddNode(*graph, "ArraySource");
    auto* loopIn = graph->InstantiateTransientNode<VCLG::FeedbackInputNode>();
    REQUIRE(Connect(*graph, source->GetOutputs()[0], loopIn->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(VCL::Type::IsCanonicallyEqual(loopIn->GetType(), source->GetOutputs()[0]->GetType()));
}
