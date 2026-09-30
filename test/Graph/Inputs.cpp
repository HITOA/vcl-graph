#include "Common/GraphTest.hpp"

#include <VCLG/CodeGen/Optimizer.hpp>
#include <VCLG/Graph/Converter.hpp>

#include <VCL/Core/Target.hpp>

#include <catch2/generators/catch_generators.hpp>


// Inputs are read-only (ASTInputConstWriter): a node that writes one is rejected when it's loaded.

namespace {

    // An int32 output feeding a float32 input: the conversion writes the input's storage.
    class IntToFloatConverter : public VCLG::Converter {
    public:
        bool Convertible(VCL::Type* outType, VCL::Type* inType) override {
            return IsBuiltin(outType, VCL::BuiltinType::Int32) && IsBuiltin(inType, VCL::BuiltinType::Float32);
        }

        VCL::Type* GetInputType(VCL::Type* outType, VCL::Type* inType) override {
            return inType;
        }

        bool Emit(llvm::IRBuilder<>& builder, VCL::Type* outType, VCL::Type* inType,
                llvm::Value* outPtr, llvm::Value* inPtr) override {
            llvm::Value* value = builder.CreateLoad(GetGraphContext().ConvertType(outType), outPtr);
            builder.CreateStore(builder.CreateSIToFP(value, GetGraphContext().ConvertType(inType)), inPtr);
            return true;
        }

    private:
        static bool IsBuiltin(VCL::Type* type, VCL::BuiltinType::Kind kind) {
            type = VCL::Type::GetCanonicalType(type);
            return type->GetTypeClass() == VCL::Type::BuiltinTypeClass && ((VCL::BuiltinType*)type)->GetKind() == kind;
        }
    };

}

TEST_CASE_METHOD(Test::GraphTest, "Writing an input is an error", "[Graph][Inputs]") {
    auto graph = context.CreateInstance();
    REQUIRE(graph->InstantiateSourceNode(LoadNode("WritesInput")) == nullptr);
    REQUIRE(consumer.HasError("'input' is read-only"));
}

TEST_CASE_METHOD(Test::GraphTest, "Passing an input to an inout parameter is an error", "[Graph][Inputs]") {
    auto graph = context.CreateInstance();
    REQUIRE(graph->InstantiateSourceNode(LoadNode("InoutInput")) == nullptr);
    REQUIRE(consumer.HasError("qualifiers dropped"));
}

TEST_CASE_METHOD(Test::GraphTest, "An input can be read and passed to a default parameter", "[Graph][Inputs]") {
    mode = GENERATE(Test::Legacy, Test::Planned);
    // Unconnected, the input holds its declared (aggregate) initializer.
    auto graph = context.CreateInstance();
    auto* sum = AddNode(*graph, "ArraySum");
    sum->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);

    Test::CompiledGraph compiled = Compile(*graph);
    compiled.Reset();
    compiled.Main();
    REQUIRE(*compiled.Output<float>(NodePath(*graph, sum), "output") == 14.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "A converter writes a const input, optimized", "[Graph][Inputs]") {
    mode = GENERATE(Test::Legacy, Test::Planned);
    // Legacy: the input is const for the node only; if its global were an LLVM constant, the
    // optimizer could fold the node's read of it to the initializer (0) and drop the converter's
    // store. Planned: the converter writes a temporary the node reads.
    IntToFloatConverter converter{};
    context.AddConverter(&converter);

    auto graph = context.CreateInstance();
    auto* source = AddNode(*graph, "IntSource");
    auto* scale = AddNode(*graph, "Scale");
    scale->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(Connect(*graph, source->GetOutputs()[0], scale->GetInputs()[0]) != INVALID_IDENTITY);

    // Only the output read here is observed: the rest is the optimizer's to fold.
    VCLG::CodeGenGraphOptions options = Options();
    options.planner.observeAllOutputs = false;
    options.planner.observedOutputs.insert(VCLG::GraphLayout::OutputKey(NodePath(*graph, scale), "output"));
    Test::CompiledGraph compiled = Compile(*graph, options, true);
    compiled.Reset();
    compiled.Main();
    REQUIRE(*compiled.Output<float>(NodePath(*graph, scale), "output") == 6.0f); // 3 * the default factor, 2
}

TEST_CASE_METHOD(Test::GraphTest, "A converted input is a temporary", "[Graph][Inputs][Plan]") {
    IntToFloatConverter converter{};
    context.AddConverter(&converter);

    auto graph = context.CreateInstance();
    auto* source = AddNode(*graph, "IntSource");
    auto* scale = AddNode(*graph, "Scale");
    scale->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(Connect(*graph, source->GetOutputs()[0], scale->GetInputs()[0]) != INVALID_IDENTITY);

    // Both nodes write their output on every call (proven): temporaries, nobody observing them.
    // Scale's input, converted, is a temporary filled right before its call; IntSource's output
    // lives until then. Stateless nodes still get a region (empty) each.
    std::string a = NodePath(*graph, source), b = NodePath(*graph, scale);
    std::string w = std::to_string(context.GetCompilerContext().GetTarget().GetVectorWidthInByte());
    std::string w2 = std::to_string(2 * context.GetCompilerContext().GetTarget().GetVectorWidthInByte());
    std::string expected =
        a + " IntSource\n"
        "  state: #0\n"
        "  out value: #1\n" +
        b + " Scale\n"
        "  state: #2\n"
        "  in input: #3\n"
        "  out output: #4\n"
        "#0 S1 host state " + a + ": " + a + "#state [0, 0)\n"
        "#1 S4 temporary int32 " + a + ".value: calls 0..1\n"
        "#2 S1 host state " + b + ": " + b + "#state [" + w + ", " + w + ")\n"
        "#3 S4 temporary float32 " + b + ".input: calls 1..1\n"
        "#4 S4 temporary float32 " + b + ".output: calls 1..1\n"
        "state block: " + w2 + " bytes, aligned to " + w + "\n";
    REQUIRE(Plan(*graph) == expected);
}
