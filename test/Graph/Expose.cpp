// UI access to node state (plan P9.1, `state-as-data.md` §3.4, §5.3, §6.2): what the planner does
// with exposed variables and the live set, and where the layout says the host finds them.

#include "Common/GraphTest.hpp"

#include <VCLG/Translation/VariantCache.hpp>

#include <catch2/generators/catch_generators.hpp>

#include <VCL/AST/ConstantValue.hpp>
#include <VCL/Core/Target.hpp>


namespace {

    using Exposed = VCLG::GraphLayout::Exposed;

    // The fixture's options, without observing every output: the planner places them.
    VCLG::CodeGenGraphOptions Unobserved(Test::GraphTest& test) {
        VCLG::CodeGenGraphOptions options = test.Options();
        options.planner.observeAllOutputs = false;
        return options;
    }

}

TEST_CASE_METHOD(Test::GraphTest, "An input in the live set is read from the UI block", "[Graph][Expose]") {
    bool optimize = GENERATE(false, true);
    INFO("optimized: " << optimize);
    auto graph = context.CreateInstance();
    auto* gain = AddNode(*graph, "Expose/Gain");
    gain->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    std::string g = NodePath(*graph, gain);
    VCLG::CodeGenGraphOptions options = Options();
    options.planner.liveInputs.insert(VCLG::GraphLayout::InputKey(g, "gain"));

    SECTION("Its declared initializer") {
        Test::CompiledGraph compiled = Compile(*graph, options, optimize);
        const Exposed* entry = compiled.layout.FindExposed(g, "gain");
        REQUIRE(entry != nullptr);
        REQUIRE(entry->location == Exposed::Location::UI);
        REQUIRE(entry->mode == Exposed::Mode::Write);
        REQUIRE(entry->writable);
        REQUIRE(!entry->connected);
        REQUIRE(entry->type == "float32");
        REQUIRE(entry->format == VCLG::ValueFormat{ VCLG::ValueFormat::Element::Float32, 1 });
        REQUIRE(compiled.layout.FindUiEntry(VCLG::GraphLayout::InputKey(g, "gain")) != nullptr);
        REQUIRE(compiled.layout.ui.size >= 4);

        // InitUI wrote the value the input has as a constant.
        REQUIRE(*compiled.Exposed<float>(g, "gain") == 2.0f);
        compiled.Reset();
        compiled.Main();
        REQUIRE(*compiled.Output<float>(g, "output") == 2.0f);

        // Changed by the host between calls, without recompiling.
        *compiled.Exposed<float>(g, "gain") = 5.0f;
        compiled.Main();
        REQUIRE(*compiled.Output<float>(g, "output") == 5.0f);
        *compiled.Exposed<float>(g, "gain") = -0.5f;
        compiled.Main();
        REQUIRE(*compiled.Output<float>(g, "output") == -0.5f);
    }
    SECTION("The graph's value") {
        gain->GetInputs()[1]->SetInitializerOverride(VCL::ConstantScalar{ 3.0f });
        Test::CompiledGraph compiled = Compile(*graph, options, optimize);
        REQUIRE(*compiled.Exposed<float>(g, "gain") == 3.0f);
        compiled.Reset();
        compiled.Main();
        REQUIRE(*compiled.Output<float>(g, "output") == 3.0f);
    }
}

TEST_CASE_METHOD(Test::GraphTest, "An exposed input not in the live set stays a constant the host can read", "[Graph][Expose]") {
    bool optimize = GENERATE(false, true);
    INFO("optimized: " << optimize);
    auto graph = context.CreateInstance();
    auto* gain = AddNode(*graph, "Expose/Gain");
    gain->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    gain->GetInputs()[1]->SetInitializerOverride(VCL::ConstantScalar{ 4.0f });
    std::string g = NodePath(*graph, gain);

    Test::CompiledGraph compiled = Compile(*graph, std::nullopt, optimize);
    const Exposed* entry = compiled.layout.FindExposed(g, "gain");
    REQUIRE(entry != nullptr);
    REQUIRE(entry->location == Exposed::Location::Constant);
    REQUIRE(entry->mode == Exposed::Mode::Write);
    // Writing it takes a recompile with the input live.
    REQUIRE(!entry->writable);
    REQUIRE(compiled.layout.uiEntries.empty());
    REQUIRE(*compiled.Exposed<float>(g, "gain") == 4.0f);
    compiled.Reset();
    compiled.Main();
    REQUIRE(*compiled.Output<float>(g, "output") == 4.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "Exposed state reads what the node wrote, and editable state is written by the host",
        "[Graph][Expose]") {
    bool optimize = GENERATE(false, true);
    INFO("optimized: " << optimize);
    auto graph = context.CreateInstance();
    auto* watch = AddNode(*graph, "Expose/Watch");
    watch->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    watch->GetInputs()[0]->SetInitializerOverride(VCL::ConstantScalar{ 0.25f });
    std::string w = NodePath(*graph, watch);

    Test::CompiledGraph compiled = Compile(*graph, Unobserved(*this), optimize);
    const Exposed* calls = compiled.layout.FindExposed(w, "calls");
    REQUIRE(calls != nullptr);
    REQUIRE(calls->location == Exposed::Location::State);
    REQUIRE(calls->mode == Exposed::Mode::Read);
    REQUIRE(!calls->writable);
    REQUIRE(calls->format == VCLG::ValueFormat{ VCLG::ValueFormat::Element::UInt32, 1 });
    // At its field of the node's state region.
    const VCLG::GraphLayout::Region* state = compiled.layout.FindRegion(VCLG::GraphLayout::StateKey(w));
    REQUIRE(state != nullptr);
    REQUIRE(calls->offset >= state->offset);
    REQUIRE(calls->offset + calls->size <= state->offset + state->size);

    uint32_t width = context.GetCompilerContext().GetTarget().GetVectorWidthInElement();
    const Exposed* last = compiled.layout.FindExposed(w, "last");
    REQUIRE(last != nullptr);
    REQUIRE(last->format == VCLG::ValueFormat{ VCLG::ValueFormat::Element::Float32, width });
    REQUIRE(last->size == 4 * width);

    const Exposed* offset = compiled.layout.FindExposed(w, "offset");
    REQUIRE(offset != nullptr);
    REQUIRE(offset->mode == Exposed::Mode::Write);
    REQUIRE(offset->writable);

    compiled.Reset();
    for (int call = 0; call < 3; ++call)
        compiled.Main();
    REQUIRE(*compiled.Exposed<uint32_t>(w, "calls") == 3);
    REQUIRE(*compiled.Exposed<float>(w, "output") == 0.25f);
    REQUIRE(compiled.Exposed<float>(w, "last")[width - 1] == 0.25f);

    // The host writes editable state between calls; the node reads it on the next.
    *compiled.Exposed<float>(w, "offset") = 1.5f;
    compiled.Main();
    REQUIRE(*compiled.Exposed<uint32_t>(w, "calls") == 4);
    REQUIRE(*compiled.Exposed<float>(w, "output") == 1.75f);
}

TEST_CASE_METHOD(Test::GraphTest, "An exposed always-written output is a host port; the variant is unchanged", "[Graph][Expose]") {
    auto graph = context.CreateInstance();
    auto* counter = AddNode(*graph, "Counter");
    auto* watch = AddNode(*graph, "Expose/Watch");
    watch->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(Connect(*graph, counter->GetOutputs()[0], watch->GetInputs()[0]) != INVALID_IDENTITY);
    std::string w = NodePath(*graph, watch);
    std::string key = VCLG::GraphLayout::OutputKey(w, "output");

    // Proven always written, and still a host port: the UI reads it after the call (§5.3).
    bool emitted = Emit(*graph, [&](llvm::Module&, VCLG::CodeGenGraph& cgg) {
        const VCLG::NodeInterface* interface = cgg.GetNodeInterface(w);
        REQUIRE(interface != nullptr);
        REQUIRE(interface->ports[1].provenAlwaysWritten);
        REQUIRE(cgg.GetLayout().FindRegion(key) != nullptr);
    }, Unobserved(*this));
    REQUIRE(emitted);
    REQUIRE(Plan(*graph, Unobserved(*this)).find("S3 host port float32 " + key) != std::string::npos);

    Test::CompiledGraph compiled = Compile(*graph, Unobserved(*this));
    compiled.Reset();
    compiled.Main();
    REQUIRE(*compiled.Exposed<float>(w, "output") == 11.0f);
    compiled.Main();
    REQUIRE(*compiled.Exposed<float>(w, "output") == 12.0f);

    // Exposure is a slot-planner decision: observing more or fewer outputs translates nothing.
    VCLG::VariantCache::Statistics before = context.GetVariantCache().GetStatistics();
    Test::CompiledGraph observed = Compile(*graph, Options());
    VCLG::VariantCache::Statistics after = context.GetVariantCache().GetStatistics();
    REQUIRE(after.translated == before.translated);
    REQUIRE(after.reused == before.reused + 2);
}

TEST_CASE_METHOD(Test::GraphTest, "A connected editable input ignores the UI block", "[Graph][Expose]") {
    auto graph = context.CreateInstance();
    auto* counter = AddNode(*graph, "Counter");
    auto* gain = AddNode(*graph, "Expose/Gain");
    gain->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(Connect(*graph, counter->GetOutputs()[0], gain->GetInputs()[1]) != INVALID_IDENTITY);
    std::string c = NodePath(*graph, counter), g = NodePath(*graph, gain);
    VCLG::CodeGenGraphOptions options = Options();
    // In the live set, but connected: the connection wins (§3.4).
    options.planner.liveInputs.insert(VCLG::GraphLayout::InputKey(g, "gain"));

    Test::CompiledGraph compiled = Compile(*graph, options);
    REQUIRE(compiled.layout.FindUiEntry(VCLG::GraphLayout::InputKey(g, "gain")) == nullptr);
    REQUIRE(compiled.layout.ui.size == 0);
    const Exposed* entry = compiled.layout.FindExposed(g, "gain");
    REQUIRE(entry != nullptr);
    REQUIRE(entry->mode == Exposed::Mode::Write);
    REQUIRE(entry->connected);
    REQUIRE(!entry->writable);
    // Read where the producer's output is (a host port here: the fixture observes every output).
    REQUIRE(entry->location == Exposed::Location::State);
    REQUIRE(entry->offset == compiled.layout.FindRegion(VCLG::GraphLayout::OutputKey(c, "output"))->offset);

    compiled.Reset();
    compiled.Main();
    REQUIRE(*compiled.Output<float>(g, "output") == 11.0f);
    REQUIRE(*compiled.Exposed<float>(g, "gain") == 11.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "An exposed input fed by a temporary is read through its probe", "[Graph][Expose]") {
    bool optimize = GENERATE(false, true);
    INFO("optimized: " << optimize);
    auto graph = context.CreateInstance();
    auto* counter = AddNode(*graph, "Counter");
    auto* watch = AddNode(*graph, "Expose/Watch");
    watch->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(Connect(*graph, counter->GetOutputs()[0], watch->GetInputs()[0]) != INVALID_IDENTITY);
    std::string c = NodePath(*graph, counter), w = NodePath(*graph, watch);

    // Counter's output is always written and not observed: a temporary, copied into a probe (S8).
    std::string plan = Plan(*graph, Unobserved(*this));
    REQUIRE(plan.find("S4 temporary float32 " + VCLG::GraphLayout::OutputKey(c, "output")) != std::string::npos);
    REQUIRE(plan.find("S8 probe float32 " + VCLG::GraphLayout::ProbeKey(w, "input")) != std::string::npos);

    Test::CompiledGraph compiled = Compile(*graph, Unobserved(*this), optimize);
    const Exposed* entry = compiled.layout.FindExposed(w, "input");
    REQUIRE(entry != nullptr);
    REQUIRE(entry->location == Exposed::Location::State);
    REQUIRE(entry->connected);
    REQUIRE(entry->offset == compiled.layout.FindRegion(VCLG::GraphLayout::ProbeKey(w, "input"))->offset);
    compiled.Reset();
    compiled.Main();
    REQUIRE(*compiled.Exposed<float>(w, "input") == 11.0f);
    compiled.Main();
    REQUIRE(*compiled.Exposed<float>(w, "input") == 12.0f);
    REQUIRE(*compiled.Exposed<float>(w, "output") == 12.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "liveEditableInputs puts every unconnected editable input in the live set", "[Graph][Expose]") {
    auto graph = context.CreateInstance();
    auto* gain = AddNode(*graph, "Expose/Gain");
    gain->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    std::string g = NodePath(*graph, gain);
    VCLG::CodeGenGraphOptions options = Options();
    options.planner.liveEditableInputs = true;

    Test::CompiledGraph compiled = Compile(*graph, options, true);
    // `gain` is [Expose(Write)]: live. `input` isn't exposed: still a constant.
    REQUIRE(compiled.layout.uiEntries.size() == 1);
    REQUIRE(compiled.layout.uiEntries[0].key == VCLG::GraphLayout::InputKey(g, "gain"));
    const Exposed* entry = compiled.layout.FindExposed(g, "gain");
    REQUIRE(entry != nullptr);
    REQUIRE(entry->location == Exposed::Location::UI);
    REQUIRE(entry->writable);
    compiled.Reset();
    *compiled.Exposed<float>(g, "gain") = 7.0f;
    compiled.Main();
    REQUIRE(*compiled.Output<float>(g, "output") == 7.0f);
}
