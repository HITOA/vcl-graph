#include "Common/GraphTest.hpp"



TEST_CASE_METHOD(Test::GraphTest, "A templated node takes the type connected to it", "[Graph][Inference]") {
    auto graph = context.CreateInstance();
    auto* source = AddNode(*graph, "ArraySource");
    auto* pass = AddNode(*graph, "Passthrough");
    VCL::Type* arrayType = source->GetOutputs()[0]->GetType();

    REQUIRE(pass->GetInputs()[0]->IsDependent());
    REQUIRE(pass->GetOutputs()[0]->IsDependent());
    REQUIRE(Connect(*graph, source->GetOutputs()[0], pass->GetInputs()[0]) != INVALID_IDENTITY);

    VCLG::ElaboratedGraph elaborated = VCLG::Elaborate(*graph);
    REQUIRE(elaborated.Succeeded());
    REQUIRE(VCL::Type::IsCanonicallyEqual(elaborated.GetPortType(pass->GetInputs()[0]), arrayType));
    REQUIRE(VCL::Type::IsCanonicallyEqual(elaborated.GetPortType(pass->GetOutputs()[0]), arrayType));

    SECTION("and propagates it downstream") {
        auto* next = AddNode(*graph, "Passthrough");
        REQUIRE(Connect(*graph, pass->GetOutputs()[0], next->GetInputs()[0]) != INVALID_IDENTITY);
        REQUIRE(VCL::Type::IsCanonicallyEqual(VCLG::Elaborate(*graph).GetPortType(next->GetOutputs()[0]), arrayType));
    }

    SECTION("without writing it into the graph") {
        REQUIRE(pass->GetInputs()[0]->GetType() != arrayType);
        REQUIRE(pass->GetInputs()[0]->GetType()->GetTypeClass() == VCL::Type::TypeAliasTypeClass);
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
    float* values = compiled.Output<float>(NodePath(*graph, pass), "output");
    REQUIRE(values != nullptr);
    for (int i = 0; i < 4; ++i)
        REQUIRE(values[i] == (float)i);
}

TEST_CASE_METHOD(Test::GraphTest, "A Feedback Input takes the type of what feeds it", "[Graph][Inference]") {
    auto graph = context.CreateInstance();
    auto* source = AddNode(*graph, "ArraySource");
    auto* loopIn = graph->InstantiateBuiltinNode<VCLG::FeedbackInputNode>();
    REQUIRE(Connect(*graph, source->GetOutputs()[0], loopIn->GetInputs()[0]) != INVALID_IDENTITY);
    VCL::Type* type = VCLG::Elaborate(*graph).GetPortType(loopIn->GetInputs()[0]);
    REQUIRE(type != nullptr);
    REQUIRE(VCL::Type::IsCanonicallyEqual(type, source->GetOutputs()[0]->GetType()));
}
