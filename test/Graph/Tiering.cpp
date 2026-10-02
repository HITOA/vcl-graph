// Tiering (plan Phase 10, `state-as-data.md` §6.7): an unconnected input goes live (a UI entry)
// while it's edited and is folded back into a constant once settled. The live set is a compile
// option: a tier change changes neither the state layout nor any node's code.

#include "Common/GraphTest.hpp"

#include <VCLG/Translation/VariantCache.hpp>

#include <catch2/generators/catch_generators.hpp>

#include <VCL/AST/ConstantValue.hpp>


namespace {

    // The regions of two layouts are the same: same keys, places and signatures, so migration
    // keeps every one.
    void RequireSameRegions(const VCLG::GraphLayout& a, const VCLG::GraphLayout& b) {
        REQUIRE(a.state.size == b.state.size);
        REQUIRE(a.state.alignment == b.state.alignment);
        REQUIRE(a.regions.size() == b.regions.size());
        for (const VCLG::GraphLayout::Region& region : a.regions) {
            INFO(region.key);
            const VCLG::GraphLayout::Region* other = b.FindRegion(region.key);
            REQUIRE(other != nullptr);
            CHECK(other->offset == region.offset);
            CHECK(other->size == region.size);
            CHECK(other->signature == region.signature);
        }
    }

}

TEST_CASE_METHOD(Test::GraphTest, "A tier change keeps the state and the output, and translates no variant", "[Graph][Tiering]") {
    bool optimize = GENERATE(false, true);
    INFO("optimized: " << optimize);
    // Counter -> Add.A; Add.B is unconnected, and not exposed: tiering covers every unconnected input.
    auto graph = context.CreateInstance();
    auto* counter = AddNode(*graph, "Counter");
    auto* add = AddNode(*graph, "Add");
    add->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(Connect(*graph, counter->GetOutputs()[0], add->GetInputs()[0]) != INVALID_IDENTITY);
    add->GetInputs()[1]->SetInitializerOverride(VCL::ConstantScalar{ 0.5f });
    std::string a = NodePath(*graph, add);
    std::string key = VCLG::GraphLayout::InputKey(a, "inputB");

    // Folded: B is a constant.
    Test::CompiledGraph folded = Compile(*graph, Options(), optimize);
    REQUIRE(folded.layout.uiEntries.empty());
    folded.Reset();
    for (int i = 0; i < 3; ++i)
        folded.Main();
    REQUIRE(*folded.Output<float>(a, "output") == 13.5f);

    // Going live: B is a UI entry. Same regions, nothing translated.
    VCLG::CodeGenGraphOptions liveOptions = Options();
    liveOptions.planner.liveInputs.insert(key);
    VCLG::VariantCache::Statistics before = context.GetVariantCache().GetStatistics();
    Test::CompiledGraph live = Compile(*graph, liveOptions, optimize);
    VCLG::VariantCache::Statistics after = context.GetVariantCache().GetStatistics();
    CHECK(after.translated == before.translated);
    RequireSameRegions(folded.layout, live.layout);

    const VCLG::GraphLayout::UiEntry* entry = live.layout.FindUiEntry(key);
    REQUIRE(entry != nullptr);
    REQUIRE(live.layout.FindUiEntry(a, "inputB") == entry);
    REQUIRE(live.layout.FindUiEntry(a, "inputA") == nullptr);
    REQUIRE(live.layout.FindUiEntry(a + "x", "inputB") == nullptr);
    CHECK(entry->type == "float32");
    CHECK(entry->format == VCLG::ValueFormat{ VCLG::ValueFormat::Element::Float32, 1 });
    CHECK(entry->size == 4);
    float* b = (float*)(live.ui.Bytes() + entry->offset);
    // InitUI seeded it with the graph's value.
    REQUIRE(*b == 0.5f);

    // The swap: the live graph takes over the state, the counter goes on.
    live.Reset();
    Test::Migrate(folded, live);
    live.Main();
    REQUIRE(*live.Output<float>(a, "output") == 14.5f);

    // Live: the host writes the UI block between calls, no compile.
    *b = 2.0f;
    live.Main();
    REQUIRE(*live.Output<float>(a, "output") == 17.0f);

    // Folding back: the host kept the value as the input's initializer; the constant is that value.
    add->GetInputs()[1]->SetInitializerOverride(VCL::ConstantScalar{ 2.0f });
    before = context.GetVariantCache().GetStatistics();
    Test::CompiledGraph refolded = Compile(*graph, Options(), optimize);
    after = context.GetVariantCache().GetStatistics();
    CHECK(after.translated == before.translated);
    RequireSameRegions(live.layout, refolded.layout);
    REQUIRE(refolded.layout.uiEntries.empty());
    refolded.Reset();
    Test::Migrate(live, refolded);
    refolded.Main();
    REQUIRE(*refolded.Output<float>(a, "output") == 18.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "A live input and its folded constant give the same output", "[Graph][Tiering]") {
    bool optimize = GENERATE(false, true);
    float value = GENERATE(0.0f, 1.0f, -3.25f);
    INFO("optimized: " << optimize << ", value: " << value);
    auto graph = context.CreateInstance();
    auto* counter = AddNode(*graph, "Counter");
    auto* add = AddNode(*graph, "Add");
    add->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(Connect(*graph, counter->GetOutputs()[0], add->GetInputs()[0]) != INVALID_IDENTITY);
    add->GetInputs()[1]->SetInitializerOverride(VCL::ConstantScalar{ value });
    std::string a = NodePath(*graph, add);

    VCLG::CodeGenGraphOptions liveOptions = Options();
    liveOptions.planner.liveInputs.insert(VCLG::GraphLayout::InputKey(a, "inputB"));
    Test::CompiledGraph folded = Compile(*graph, Options(), optimize);
    Test::CompiledGraph live = Compile(*graph, liveOptions, optimize);
    folded.Reset();
    live.Reset();
    for (int i = 0; i < 4; ++i) {
        folded.Main();
        live.Main();
        REQUIRE(*live.Output<float>(a, "output") == *folded.Output<float>(a, "output"));
    }
    REQUIRE(*folded.Output<float>(a, "output") == 14.0f + value);
}

TEST_CASE_METHOD(Test::GraphTest, "A connected input in the live set stays connected", "[Graph][Tiering]") {
    // The live set outlives the edit that put an input in it: a connection made meanwhile wins.
    auto graph = context.CreateInstance();
    auto* counter = AddNode(*graph, "Counter");
    auto* add = AddNode(*graph, "Add");
    add->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(Connect(*graph, counter->GetOutputs()[0], add->GetInputs()[0]) != INVALID_IDENTITY);
    std::string a = NodePath(*graph, add);

    VCLG::CodeGenGraphOptions liveOptions = Options();
    liveOptions.planner.liveInputs.insert(VCLG::GraphLayout::InputKey(a, "inputA"));
    // And a key no node has.
    liveOptions.planner.liveInputs.insert(VCLG::GraphLayout::InputKey("g0/n999", "inputA"));
    Test::CompiledGraph live = Compile(*graph, liveOptions);
    REQUIRE(live.layout.uiEntries.empty());
    live.Reset();
    live.Main();
    REQUIRE(*live.Output<float>(a, "output") == 11.0f);
}
