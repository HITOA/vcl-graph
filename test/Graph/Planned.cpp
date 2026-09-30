// The planned codegen (plan Phase 4): the slot plan, what each storage class guarantees, and
// equivalence with the legacy codegen.

#include "Common/GraphTest.hpp"

#include <VCLG/Graph/Converter.hpp>

#include <VCL/AST/ConstantValue.hpp>
#include <VCL/Core/Target.hpp>

#include <catch2/generators/catch_generators.hpp>

#include <cstring>
#include <functional>


namespace {

    // Only these outputs are observed (host ports); the others are the planner's to place.
    VCLG::CodeGenGraphOptions Observing(Test::GraphTest& test, std::initializer_list<std::string> keys) {
        VCLG::CodeGenGraphOptions options = test.Options();
        options.planner.observeAllOutputs = false;
        options.planner.observedOutputs.insert(keys.begin(), keys.end());
        return options;
    }

    // Every output of every source node of `graph` after each of `calls` calls to Main (following
    // a Reset), as raw bytes.
    std::vector<std::vector<uint8_t>> Run(Test::GraphTest& test, VCLG::GraphInstance& graph, Test::Mode mode, int calls, bool resetTwice = false) {
        test.mode = mode;
        Test::CompiledGraph compiled = test.Compile(graph);
        VCLG::ElaboratedGraph elaborated = VCLG::Elaborate(graph);
        REQUIRE(elaborated.Succeeded());

        // Sizes from the planned layout (both modes store the same types).
        std::vector<std::pair<uint8_t*, uint64_t>> outputs{};
        for (VCLG::ElaboratedGraph::NodeIndex index : elaborated.GetExecutionOrder()) {
            const VCLG::ElaboratedGraph::Node& node = elaborated.GetNode(index);
            if (node.definition == nullptr)
                continue;
            for (uint32_t o = 0; o < node.outputs.size(); ++o) {
                const std::string& name = node.definition->GetPorts()[node.inputs.size() + o].GetName();
                llvm::Type* type = test.context.ConvertType(VCLG::ElaboratedGraph::TypeOf(node.outputs[o]));
                REQUIRE(type != nullptr);
                llvm::DataLayout layout = test.context.GetCompilerContext().GetTarget().GetTargetMachine()->createDataLayout();
                outputs.emplace_back(compiled.Output<uint8_t>(node.path, name), layout.getTypeStoreSize(type));
            }
        }

        std::vector<std::vector<uint8_t>> results{};
        for (int run = 0; run < (resetTwice ? 2 : 1); ++run) {
            compiled.Reset();
            for (int call = 0; call < calls; ++call) {
                compiled.Main();
                std::vector<uint8_t>& bytes = results.emplace_back();
                for (auto [address, size] : outputs)
                    bytes.insert(bytes.end(), address, address + size);
            }
        }
        return results;
    }

    // The two codegens compute the same outputs, call after call; and in planned mode, a second
    // Reset starts the same sequence again (Reset runs `__Init`, §5.7).
    void CheckEquivalent(Test::GraphTest& test, VCLG::GraphInstance& graph, int calls = 8) {
        std::vector<std::vector<uint8_t>> legacy = Run(test, graph, Test::Legacy, calls);
        std::vector<std::vector<uint8_t>> planned = Run(test, graph, Test::Planned, calls, true);
        REQUIRE(planned.size() == 2 * legacy.size());
        for (int call = 0; call < calls; ++call) {
            INFO("call " << call);
            REQUIRE(planned[call] == legacy[call]);
            REQUIRE(planned[calls + call] == planned[call]);
        }
    }

    class IntToFloatConverter : public VCLG::Converter {
    public:
        bool Convertible(VCL::Type* outType, VCL::Type* inType) override {
            return IsBuiltin(outType, VCL::BuiltinType::Int32) && IsBuiltin(inType, VCL::BuiltinType::Float32);
        }
        VCL::Type* GetInputType(VCL::Type* outType, VCL::Type* inType) override { return inType; }
        bool Emit(llvm::IRBuilder<>& builder, VCL::Type* outType, VCL::Type* inType, llvm::Value* outPtr, llvm::Value* inPtr) override {
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

TEST_CASE_METHOD(Test::GraphTest, "The plan of a small graph", "[Graph][Plan]") {
    // Counter -> Scale -> ConditionalOutput -> Add (+ 2, unconnected), nothing observed.
    auto graph = context.CreateInstance();
    auto* counter = AddNode(*graph, "Counter");
    auto* scale = AddNode(*graph, "Scale");
    auto* conditional = AddNode(*graph, "Translation/ConditionalOutput");
    auto* add = AddNode(*graph, "Add");
    add->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    VCL::ConstantScalar two{ 2.0f };
    add->GetInputs()[1]->SetInitializerOverride(two);
    REQUIRE(Connect(*graph, counter->GetOutputs()[0], scale->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(Connect(*graph, scale->GetOutputs()[0], conditional->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(Connect(*graph, conditional->GetOutputs()[0], add->GetInputs()[0]) != INVALID_IDENTITY);

    // - state: S1, always (Counter's 4 bytes; the others are empty);
    // - outputs written on every call (Counter, Scale, Add: proven) are temporaries (S4), living
    //   until their last reader;
    // - ConditionalOutput's output isn't always written: a host port (S3);
    // - Add's second input is unconnected: a constant (S6).
    std::string c = NodePath(*graph, counter), s = NodePath(*graph, scale), o = NodePath(*graph, conditional), a = NodePath(*graph, add);
    uint64_t width = context.GetCompilerContext().GetTarget().GetVectorWidthInByte();
    auto at = [&](uint64_t begin, uint64_t end) { return "[" + std::to_string(begin) + ", " + std::to_string(end) + ")"; };
    std::string expected =
        c + " Counter\n"
        "  state: #0\n"
        "  out output: #1\n" +
        s + " Scale\n"
        "  state: #2\n"
        "  in input: #1\n"
        "  out output: #3\n" +
        o + " ConditionalOutput\n"
        "  state: #4\n"
        "  in input: #3\n"
        "  out output: #5\n" +
        a + " Add\n"
        "  state: #6\n"
        "  in inputA: #5\n"
        "  in inputB: #7\n"
        "  out output: #8\n"
        "#0 S1 host state " + c + ": " + c + "#state " + at(0, 4) + "\n"
        "#1 S4 temporary float32 " + c + ".output: calls 0..1\n"
        "#2 S1 host state " + s + ": " + s + "#state " + at(width, width) + "\n"
        "#3 S4 temporary float32 " + s + ".output: calls 1..2\n"
        "#4 S1 host state " + o + ": " + o + "#state " + at(2 * width, 2 * width) + "\n"
        "#5 S3 host port float32 " + o + ".output: " + o + ".output#output " + at(3 * width, 3 * width + 4) + "\n"
        "#6 S1 host state " + a + ": " + a + "#state " + at(4 * width, 4 * width) + "\n"
        "#7 S6 constant float32 " + a + ".inputB = 2\n"
        "#8 S4 temporary float32 " + a + ".output: calls 3..3\n"
        "state block: " + std::to_string(5 * width) + " bytes, aligned to " + std::to_string(width) + "\n";
    REQUIRE(Plan(*graph) == expected);

    // Observed, an output is a host port whatever the analysis says.
    std::string observed = Plan(*graph, Observing(*this, { VCLG::GraphLayout::OutputKey(s, "output") }));
    REQUIRE(observed.find("S3 host port float32 " + s + ".output") != std::string::npos);
}

TEST_CASE_METHOD(Test::GraphTest, "One producer feeding two inputs of one node", "[Graph][Plan]") {
    mode = GENERATE(Test::Legacy, Test::Planned);
    // Both inputs of the Add get the same pointer: `noalias` holds, since neither is written
    // during the call (§4.6, invariant 3).
    auto graph = context.CreateInstance();
    auto* counter = AddNode(*graph, "Counter");
    auto* add = AddNode(*graph, "Add");
    add->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(Connect(*graph, counter->GetOutputs()[0], add->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(Connect(*graph, counter->GetOutputs()[0], add->GetInputs()[1]) != INVALID_IDENTITY);

    VCLG::CodeGenGraphOptions options = Observing(*this, { VCLG::GraphLayout::OutputKey(NodePath(*graph, add), "output") });
    options.mode = mode;
    Test::CompiledGraph compiled = Compile(*graph, options, true);
    compiled.Reset();   // Counter starts at 10
    compiled.Main();
    REQUIRE(*compiled.Output<float>(NodePath(*graph, add), "output") == 22.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "An output written on some calls keeps its value", "[Graph][Plan]") {
    mode = GENERATE(Test::Legacy, Test::Planned);
    auto graph = context.CreateInstance();
    auto* node = AddNode(*graph, "Planned/EveryOtherCall");
    auto* pass = AddNode(*graph, "Passthrough");
    pass->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(Connect(*graph, node->GetOutputs()[0], pass->GetInputs()[0]) != INVALID_IDENTITY);

    std::string passOutput = VCLG::GraphLayout::OutputKey(NodePath(*graph, pass), "output");
    REQUIRE(Plan(*graph, Observing(*this, { passOutput })).find("S3 host port float32 " + NodePath(*graph, node) + ".output") != std::string::npos);

    VCLG::CodeGenGraphOptions options = Observing(*this, { passOutput });
    options.mode = mode;
    Test::CompiledGraph compiled = Compile(*graph, options, true);
    compiled.Reset();
    float expected[] = { 0.0f, 0.0f, 2.0f, 2.0f, 4.0f, 4.0f };
    for (float value : expected) {
        compiled.Main();
        REQUIRE(*compiled.Output<float>(NodePath(*graph, pass), "output") == value);
    }
}

TEST_CASE_METHOD(Test::GraphTest, "An output is the same with and without [AlwaysWritten]", "[Graph][Plan]") {
    // The same loop, promised (a temporary) and not (a host port), each read by a Passthrough.
    auto graph = context.CreateInstance();
    auto* promised = AddNode(*graph, "Translation/LoopAlwaysWritten");
    auto* plain = AddNode(*graph, "Planned/LoopNotPromised");
    auto* promisedSink = AddNode(*graph, "Passthrough");
    auto* plainSink = AddNode(*graph, "Passthrough");
    promisedSink->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    plainSink->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    VCL::ConstantScalar half{ 0.5f };
    promised->GetInputs()[0]->SetInitializerOverride(half);
    plain->GetInputs()[0]->SetInitializerOverride(half);
    REQUIRE(Connect(*graph, promised->GetOutputs()[0], promisedSink->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(Connect(*graph, plain->GetOutputs()[0], plainSink->GetInputs()[0]) != INVALID_IDENTITY);

    VCLG::CodeGenGraphOptions options = Observing(*this, {
        VCLG::GraphLayout::OutputKey(NodePath(*graph, promisedSink), "output"),
        VCLG::GraphLayout::OutputKey(NodePath(*graph, plainSink), "output") });
    std::string plan = Plan(*graph, options);
    REQUIRE(plan.find("S4 temporary Array<float32, 1024> " + NodePath(*graph, promised) + ".output") != std::string::npos);
    REQUIRE(plan.find("S3 host port Array<float32, 1024> " + NodePath(*graph, plain) + ".output") != std::string::npos);

    Test::CompiledGraph compiled = Compile(*graph, options, true);
    compiled.Reset();
    for (int call = 0; call < 4; ++call) {
        compiled.Main();
        const float* a = compiled.Output<float>(NodePath(*graph, promisedSink), "output");
        const float* b = compiled.Output<float>(NodePath(*graph, plainSink), "output");
        REQUIRE(std::memcmp(a, b, 1024 * sizeof(float)) == 0);
    }
}

TEST_CASE_METHOD(Test::GraphTest, "A broken [AlwaysWritten] promise", "[Graph][Plan]") {
    auto graph = context.CreateInstance();
    auto* node = AddNode(*graph, "Planned/BrokenPromise");
    auto* pass = AddNode(*graph, "Passthrough");
    pass->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(Connect(*graph, node->GetOutputs()[0], pass->GetInputs()[0]) != INVALID_IDENTITY);
    VCLG::CodeGenGraphOptions options = Observing(*this, { VCLG::GraphLayout::OutputKey(NodePath(*graph, pass), "output") });

    SECTION("reads zeros, deterministically") {
        Test::CompiledGraph compiled = Compile(*graph, options);
        compiled.Reset();
        compiled.Main();
        const float* values = compiled.Output<float>(NodePath(*graph, pass), "output");
        REQUIRE(values[0] == 1.0f);
        REQUIRE(values[1] == 2.0f);
        REQUIRE(values[2] == 0.0f);
        REQUIRE(values[3] == 0.0f);
    }
    SECTION("is caught by the canary") {
        options.checkAlwaysWritten = true;
        Test::CompiledGraph compiled = Compile(*graph, options);
        compiled.Reset();
        compiled.Main();
        compiled.Main();
        REQUIRE(*compiled.Global<uint32_t>("vclg.always_written.violations") == 2);
        REQUIRE(std::string{ *compiled.Global<const char*>("vclg.always_written.last") } == NodePath(*graph, node) + ".output");
    }
    SECTION("a kept promise isn't") {
        auto kept = context.CreateInstance();
        auto* loop = AddNode(*kept, "Translation/LoopAlwaysWritten");
        auto* sink = AddNode(*kept, "Passthrough");
        sink->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
        REQUIRE(Connect(*kept, loop->GetOutputs()[0], sink->GetInputs()[0]) != INVALID_IDENTITY);
        VCLG::CodeGenGraphOptions keptOptions = Observing(*this, { VCLG::GraphLayout::OutputKey(NodePath(*kept, sink), "output") });
        keptOptions.checkAlwaysWritten = true;
        Test::CompiledGraph compiled = Compile(*kept, keptOptions);
        compiled.Reset();
        compiled.Main();
        REQUIRE(*compiled.Global<uint32_t>("vclg.always_written.violations") == 0);
    }
}

TEST_CASE_METHOD(Test::GraphTest, "A side branch of a feedback loop reads the previous value", "[Graph][Feedback]") {
    // body = previous body + 1; sink = previous body + body. The sink depends on the body, so it
    // always runs after it: legacy mode gave it this call's value (no delay), planned mode the
    // previous one, like every other reader (§5.6, the one intended change of behaviour).
    auto graph = context.CreateInstance();
    auto* loopIn = graph->InstantiateBuiltinNode<VCLG::FeedbackInputNode>();
    auto* loopOut = graph->InstantiateBuiltinNode<VCLG::FeedbackOutputNode>();
    loopOut->Update(loopIn->GetIdentity());
    auto* body = AddNode(*graph, "Add");
    auto* sink = AddNode(*graph, "Add");
    sink->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    VCL::ConstantScalar one{ 1.0f };
    body->GetInputs()[1]->SetInitializerOverride(one);
    REQUIRE(Connect(*graph, loopOut->GetOutputs()[0], body->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(Connect(*graph, body->GetOutputs()[0], loopIn->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(Connect(*graph, loopOut->GetOutputs()[0], sink->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(Connect(*graph, body->GetOutputs()[0], sink->GetInputs()[1]) != INVALID_IDENTITY);

    // The body's output is a temporary (only the sink is observed), read by the end-of-frame copy.
    std::string sinkOutput = VCLG::GraphLayout::OutputKey(NodePath(*graph, sink), "output");
    std::string plan = Plan(*graph, Observing(*this, { sinkOutput }));
    INFO(plan);
    REQUIRE(plan.find("S4 temporary float32 " + NodePath(*graph, body) + ".output: calls 1..4") != std::string::npos);
    REQUIRE(plan.find("S5 feedback float32 " + NodePath(*graph, loopIn) + "#feedback") != std::string::npos);

    for (Test::Mode compiledMode : { Test::Legacy, Test::Planned }) {
        VCLG::CodeGenGraphOptions options = Observing(*this, { sinkOutput });
        options.mode = compiledMode;
        Test::CompiledGraph compiled = Compile(*graph, options, true);
        compiled.Reset();
        for (int call = 1; call <= 3; ++call) {
            compiled.Main();
            float value = *compiled.Output<float>(NodePath(*graph, sink), "output");
            REQUIRE(value == (compiledMode == Test::Legacy ? 2.0f * call : 2.0f * call - 1.0f));
        }
    }
}

TEST_CASE_METHOD(Test::GraphTest, "The planned codegen computes what the legacy one does", "[Graph][Equivalence]") {
    SECTION("a chain with state, a parameter and a templated node") {
        auto graph = context.CreateInstance();
        auto* counter = AddNode(*graph, "Counter");
        auto* scale = AddNode(*graph, "Scale");
        auto* pass = AddNode(*graph, "Passthrough");
        pass->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
        REQUIRE(Connect(*graph, counter->GetOutputs()[0], scale->GetInputs()[0]) != INVALID_IDENTITY);
        REQUIRE(Connect(*graph, scale->GetOutputs()[0], pass->GetInputs()[0]) != INVALID_IDENTITY);
        CheckEquivalent(*this, *graph);
    }
    SECTION("aggregates: blocks, arrays, a library struct") {
        auto graph = context.CreateInstance();
        auto* source = AddNode(*graph, "StereoSource");
        auto* gain = AddNode(*graph, "BlockGain");
        auto* gain2 = AddNode(*graph, "BlockGain");
        auto* array = AddNode(*graph, "ArraySource");
        auto* arrayPass = AddNode(*graph, "Passthrough");
        auto* sum = AddNode(*graph, "ArraySum");
        auto* user = AddNode(*graph, "StructUser");
        for (auto* node : { (VCLG::Node*)gain2, (VCLG::Node*)arrayPass, (VCLG::Node*)sum, (VCLG::Node*)user })
            node->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
        REQUIRE(Connect(*graph, source->GetOutputs()[0], gain->GetInputs()[0]) != INVALID_IDENTITY);
        REQUIRE(Connect(*graph, gain->GetOutputs()[0], gain2->GetInputs()[0]) != INVALID_IDENTITY);
        REQUIRE(Connect(*graph, array->GetOutputs()[0], arrayPass->GetInputs()[0]) != INVALID_IDENTITY);
        CheckEquivalent(*this, *graph);
    }
    SECTION("a subgraph used twice") {
        auto sub = context.CreateInstance();
        auto* counter = AddNode(*sub, "Counter");
        auto* out = sub->InstantiateBuiltinNode<VCLG::SubgraphOutputNode>();
        REQUIRE(sub->Connect(counter->GetOutputs()[0], out->GetInputs()[0]) != INVALID_IDENTITY);
        auto root = context.CreateInstance();
        for (int i = 0; i < 2; ++i) {
            auto* use = root->InstantiateBuiltinNode<VCLG::SubgraphNode>();
            use->SetGraph(sub);
            auto* sink = AddNode(*root, "Add");
            sink->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
            REQUIRE(root->Connect(use->GetOutputs()[0], sink->GetInputs()[0]) != INVALID_IDENTITY);
        }
        CheckEquivalent(*this, *root);
    }
    SECTION("a converted input") {
        IntToFloatConverter converter{};
        context.AddConverter(&converter);
        auto graph = context.CreateInstance();
        auto* source = AddNode(*graph, "IntSource");
        auto* scale = AddNode(*graph, "Scale");
        scale->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
        REQUIRE(Connect(*graph, source->GetOutputs()[0], scale->GetInputs()[0]) != INVALID_IDENTITY);
        CheckEquivalent(*this, *graph);
    }
    SECTION("a feedback loop without side branches") {
        auto graph = context.CreateInstance();
        auto* loopIn = graph->InstantiateBuiltinNode<VCLG::FeedbackInputNode>();
        auto* loopOut = graph->InstantiateBuiltinNode<VCLG::FeedbackOutputNode>();
        loopOut->Update(loopIn->GetIdentity());
        auto* body = AddNode(*graph, "Add");
        auto* scale = AddNode(*graph, "Scale");
        scale->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
        VCL::ConstantScalar one{ 1.0f };
        body->GetInputs()[1]->SetInitializerOverride(one);
        REQUIRE(Connect(*graph, loopOut->GetOutputs()[0], body->GetInputs()[0]) != INVALID_IDENTITY);
        REQUIRE(Connect(*graph, body->GetOutputs()[0], scale->GetInputs()[0]) != INVALID_IDENTITY);
        REQUIRE(Connect(*graph, scale->GetOutputs()[0], loopIn->GetInputs()[0]) != INVALID_IDENTITY);
        CheckEquivalent(*this, *graph);
    }
    SECTION("an output that persists") {
        auto graph = context.CreateInstance();
        auto* node = AddNode(*graph, "Planned/EveryOtherCall");
        auto* pass = AddNode(*graph, "Passthrough");
        pass->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
        REQUIRE(Connect(*graph, node->GetOutputs()[0], pass->GetInputs()[0]) != INVALID_IDENTITY);
        CheckEquivalent(*this, *graph);
    }
}
