#include "Common/GraphTest.hpp"

#include <VCL/AST/ConstantValue.hpp>
#include <VCL/Core/SourceManager.hpp>


namespace {

    // A counter as Counter.vcl, counting by `step`, loaded from memory so that tests can edit it.
    std::string CounterSource(const std::string& step) {
        return "[Output(\"Out\")]\n"
               "float32 output = 0.0;\n"
               "float32 state = 0.0;\n"
               "[NodeReset]\n"
               "void Reset() { state = 10.0; }\n"
               "[NodeProcess]\n"
               "void Process() { state += " + step + "; output = state; }\n";
    }

}

TEST_CASE("A migration plan copies the regions whose key and signature are unchanged", "[Graph][Migration]") {
    using Kind = VCLG::GraphLayout::RegionKind;
    VCLG::GraphLayout from{};
    from.regions = {
        { "g1/n1", Kind::State, 0, 16, 16, "g1/n1#state", 1 },
        { "g1/n2", Kind::State, 16, 16, 16, "g1/n2#state", 2 },
        { "g1/n3", Kind::State, 32, 8, 16, "g1/n3#state", 3 },
        { "g1/n4", Kind::State, 48, 0, 16, "g1/n4#state", 4 },
        { "g1/n5.output", Kind::Output, 64, 4, 16, "g1/n5.output#output", 5 },
    };
    VCLG::GraphLayout to{};
    to.regions = {
        { "g1/n9", Kind::State, 0, 16, 16, "g1/n9#state", 9 },       // new
        { "g1/n1", Kind::State, 16, 16, 16, "g1/n1#state", 1 },      // moved
        { "g1/n2", Kind::State, 32, 16, 16, "g1/n2#state", 2 },      // moved, still next to n1
        { "g1/n3", Kind::State, 48, 8, 16, "g1/n3#state", 33 },      // new signature
        { "g1/n4", Kind::State, 64, 0, 16, "g1/n4#state", 4 },       // empty
        { "g1/n5.output", Kind::Output, 80, 4, 16, "g1/n5.output#output", 5 },
    };
    VCLG::MigrationPlan plan = VCLG::PlanMigration(from, to);
    REQUIRE(plan.copies.size() == 2);
    CHECK(plan.copies[0].from == 0);
    CHECK(plan.copies[0].to == 16);
    CHECK(plan.copies[0].size == 32);
    CHECK(plan.copies[1].from == 64);
    CHECK(plan.copies[1].to == 80);
    CHECK(plan.copies[1].size == 4);
    CHECK(plan.Bytes() == 36);

    uint8_t oldBlock[96]{}, newBlock[96]{};
    for (uint32_t i = 0; i < 96; ++i)
        oldBlock[i] = (uint8_t)i;
    plan.Apply(oldBlock, newBlock);
    CHECK(newBlock[16] == 0);
    CHECK(newBlock[47] == 31);
    CHECK(newBlock[48] == 0);
    CHECK(newBlock[80] == 64);
    CHECK(newBlock[83] == 67);
    CHECK(newBlock[84] == 0);
}

TEST_CASE_METHOD(Test::GraphTest, "A counter keeps counting when an unrelated node is added", "[Graph][Migration]") {
    auto graph = context.CreateInstance();
    auto* counter = AddNode(*graph, "Counter");
    counter->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    std::string path = NodePath(*graph, counter);

    Test::CompiledGraph before = Compile(*graph);
    before.Reset();
    for (int i = 0; i < 3; ++i)
        before.Main();
    REQUIRE(*before.Output<float>(path, "output") == 13.0f);

    auto* other = AddNode(*graph, "Counter");
    other->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    Test::CompiledGraph after = Compile(*graph);
    after.Reset();
    const VCLG::GraphLayout::Region* oldState = before.layout.FindRegion(VCLG::GraphLayout::StateKey(path));
    const VCLG::GraphLayout::Region* newState = after.layout.FindRegion(VCLG::GraphLayout::StateKey(path));
    REQUIRE(oldState != nullptr);
    REQUIRE(newState != nullptr);
    CHECK(oldState->signature == newState->signature);

    Migrate(before, after);
    after.Main();
    CHECK(*after.Output<float>(path, "output") == 14.0f);
    // The new node starts from its reset state.
    CHECK(*after.Output<float>(NodePath(*graph, other), "output") == 11.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "A counter resets when its source changes", "[Graph][Migration]") {
    VCL::SourceManager& sources = context.GetCompilerContext().GetSourceManager();
    const std::string name = "Memory/EditedCounter.vcl";
    // The source manager doesn't copy in-memory sources.
    const std::string original = CounterSource("1.0"), same = CounterSource("1.0"), edited = CounterSource("2.0");
    auto graph = context.CreateInstance();
    auto* counter = graph->InstantiateSourceNode(sources.LoadFromMemory(original, name));
    REQUIRE(counter != nullptr);
    counter->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    std::string path = NodePath(*graph, counter);

    Test::CompiledGraph before = Compile(*graph);
    before.Reset();
    for (int i = 0; i < 3; ++i)
        before.Main();
    REQUIRE(*before.Output<float>(path, "output") == 13.0f);

    SECTION("the same source keeps the state") {
        REQUIRE(sources.ReplaceFromMemory(same, name) != nullptr);
        Test::CompiledGraph after = Compile(*graph);
        after.Reset();
        Migrate(before, after);
        after.Main();
        CHECK(*after.Output<float>(path, "output") == 14.0f);
    }
    SECTION("an edited source resets it") {
        REQUIRE(sources.ReplaceFromMemory(edited, name) != nullptr);
        Test::CompiledGraph after = Compile(*graph);
        after.Reset();
        VCLG::MigrationPlan plan = Migrate(before, after);
        after.Main();
        CHECK(*after.Output<float>(path, "output") == 12.0f);
        // Its output is a region of the node too: nothing is left to migrate.
        CHECK(plan.copies.empty());
    }
}

TEST_CASE_METHOD(Test::GraphTest, "A parameter resets the node only when it changes the state's shape", "[Graph][Migration]") {
    auto graph = context.CreateInstance();
    auto* accumulator = AddNode(*graph, "Migration/Accumulator");
    accumulator->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    std::string path = NodePath(*graph, accumulator);

    Test::CompiledGraph before = Compile(*graph);
    before.Reset();
    for (int i = 0; i < 3; ++i)
        before.Main();
    REQUIRE(*before.Output<float>(path, "output") == 3.0f);

    SECTION("a parameter that keeps the shape keeps the state") {
        FindParameter(accumulator, "Step")->SetInitializerOverride(VCL::ConstantScalar{ 10.0f });
        Test::CompiledGraph after = Compile(*graph);
        after.Reset();
        Migrate(before, after);
        after.Main();
        CHECK(*after.Output<float>(path, "output") == 13.0f);
    }
    SECTION("a parameter that changes the shape resets it") {
        FindParameter(accumulator, "Size")->SetInitializerOverride(VCL::ConstantScalar{ (uint32_t)8 });
        Test::CompiledGraph after = Compile(*graph);
        after.Reset();
        CHECK(after.layout.FindRegion(VCLG::GraphLayout::StateKey(path))->signature
            != before.layout.FindRegion(VCLG::GraphLayout::StateKey(path))->signature);
        Migrate(before, after);
        after.Main();
        CHECK(*after.Output<float>(path, "output") == 1.0f);
    }
}

TEST_CASE_METHOD(Test::GraphTest, "A held output keeps its value", "[Graph][Migration]") {
    auto graph = context.CreateInstance();
    auto* held = AddNode(*graph, "Planned/EveryOtherCall");
    held->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    std::string path = NodePath(*graph, held);

    // Not observed: a host port because the node doesn't write it on every call (S3).
    VCLG::CodeGenGraphOptions options{};
    Test::CompiledGraph before = Compile(*graph, options);
    REQUIRE(before.layout.FindRegion(VCLG::GraphLayout::OutputKey(path, "output")) != nullptr);
    before.Reset();
    for (int i = 0; i < 3; ++i)
        before.Main();
    // Written by calls 0 and 2; the next call doesn't write it.
    REQUIRE(*before.Output<float>(path, "output") == 2.0f);

    auto* other = AddNode(*graph, "Counter");
    other->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    Test::CompiledGraph after = Compile(*graph, options);
    after.Reset();
    Migrate(before, after);
    after.Main();
    CHECK(*after.Output<float>(path, "output") == 2.0f);
    after.Main();
    CHECK(*after.Output<float>(path, "output") == 4.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "Two instances of one definition keep their own state", "[Graph][Migration]") {
    auto graph = context.CreateInstance();
    auto* first = AddNode(*graph, "Counter");
    auto* second = AddNode(*graph, "Counter");
    first->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    second->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    std::string firstPath = NodePath(*graph, first), secondPath = NodePath(*graph, second);

    Test::CompiledGraph before = Compile(*graph);
    before.Reset();
    *before.State<float>(firstPath, "state") = 100.0f;
    before.Main();
    REQUIRE(*before.Output<float>(firstPath, "output") == 101.0f);
    REQUIRE(*before.Output<float>(secondPath, "output") == 11.0f);

    auto* other = AddNode(*graph, "Add");
    other->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    Test::CompiledGraph after = Compile(*graph);
    after.Reset();
    Migrate(before, after);
    after.Main();
    CHECK(*after.Output<float>(firstPath, "output") == 102.0f);
    CHECK(*after.Output<float>(secondPath, "output") == 12.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "Nothing migrates across a graph Reset", "[Graph][Migration]") {
    auto graph = context.CreateInstance();
    auto* counter = AddNode(*graph, "Counter");
    counter->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    std::string path = NodePath(*graph, counter);

    Test::CompiledGraph before = Compile(*graph);
    before.Reset();
    for (int i = 0; i < 3; ++i)
        before.Main();

    // A preset load: the new counter has the old one's identity, hence its path.
    graph->Reset();
    auto* fresh = AddNode(*graph, "Counter");
    fresh->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(NodePath(*graph, fresh) == path);

    Test::CompiledGraph after = Compile(*graph);
    after.Reset();
    VCLG::MigrationPlan plan = Migrate(before, after);
    CHECK(plan.copies.empty());
    after.Main();
    CHECK(*after.Output<float>(path, "output") == 11.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "Feedback regions migrate", "[Graph][Migration][Feedback]") {
    // out = previous out + 1
    auto graph = context.CreateInstance();
    auto* loopIn = graph->InstantiateBuiltinNode<VCLG::FeedbackInputNode>();
    auto* loopOut = graph->InstantiateBuiltinNode<VCLG::FeedbackOutputNode>();
    loopOut->Update(loopIn->GetIdentity());
    auto* body = AddNode(*graph, "Add");
    body->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    body->GetInputs()[1]->SetInitializerOverride(VCL::ConstantScalar{ 1.0f });
    REQUIRE(Connect(*graph, loopOut->GetOutputs()[0], body->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(Connect(*graph, body->GetOutputs()[0], loopIn->GetInputs()[0]) != INVALID_IDENTITY);
    std::string path = NodePath(*graph, body);

    // The body's output is a temporary: only the feedback region carries the value.
    VCLG::CodeGenGraphOptions options{};
    Test::CompiledGraph before = Compile(*graph, options);
    REQUIRE(before.layout.FindRegion(VCLG::GraphLayout::OutputKey(path, "output")) == nullptr);
    before.Reset();
    for (int i = 0; i < 3; ++i)
        before.Main();

    auto* other = AddNode(*graph, "Counter");
    other->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    options.planner.observedOutputs.insert(VCLG::GraphLayout::OutputKey(path, "output"));
    Test::CompiledGraph after = Compile(*graph, options);
    after.Reset();
    Migrate(before, after);
    after.Main();
    CHECK(*after.Output<float>(path, "output") == 4.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "An output that becomes a host port starts from its initializer", "[Graph][Migration]") {
    auto graph = context.CreateInstance();
    auto* doubler = AddNode(*graph, "Migration/Doubler");
    doubler->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    doubler->GetInputs()[0]->SetInitializerOverride(VCL::ConstantScalar{ 3.0f });
    std::string path = NodePath(*graph, doubler);
    std::string key = VCLG::GraphLayout::OutputKey(path, "output");

    // Always written and not observed: a temporary (S4).
    VCLG::CodeGenGraphOptions options{};
    Test::CompiledGraph before = Compile(*graph, options);
    REQUIRE(before.layout.FindRegion(key) == nullptr);
    before.Reset();
    before.Main();

    // Observed: a host port (S3), with nothing to take over.
    options.planner.observedOutputs.insert(key);
    Test::CompiledGraph after = Compile(*graph, options);
    REQUIRE(after.layout.FindRegion(key) != nullptr);
    after.Reset();
    Migrate(before, after);
    CHECK(*after.Output<float>(path, "output") == 7.0f);
    after.Main();
    CHECK(*after.Output<float>(path, "output") == 6.0f);
}
