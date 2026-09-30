#include "Common/GraphTest.hpp"

#include <VCLG/Translation/VariantCache.hpp>

#include <VCL/AST/ConstantValue.hpp>
#include <VCL/Core/SourceManager.hpp>
#include <VCL/Frontend/ModuleCache.hpp>

#include <llvm/Support/FileSystem.h>

#include <algorithm>


namespace {

    // The functions of `m` named after a node's processing entry point.
    size_t CountProcesses(llvm::Module& m) {
        return std::count_if(m.begin(), m.end(), [](llvm::Function& function) {
            return !function.isDeclaration() && function.getName().ends_with(".Process");
        });
    }

    // Variants translated and taken from the cache since `before`.
    struct CacheDelta {
        uint64_t translated;
        uint64_t reused;
    };

    VCLG::CodeGenGraphOptions PerVariant(const Test::GraphTest& test) {
        VCLG::CodeGenGraphOptions options = test.Options();
        options.codeSharing = VCLG::CodeGenGraphOptions::CodeSharing::PerVariant;
        return options;
    }

    CacheDelta Since(const VCLG::VariantCache::Statistics& before, const VCLG::VariantCache& cache) {
        return { cache.GetStatistics().translated - before.translated, cache.GetStatistics().reused - before.reused };
    }

}

TEST_CASE_METHOD(Test::GraphTest, "Repeated nodes share their variants without symbol collisions", "[Graph][Variants]") {
    // Three counters, each scaled: two scales by the default factor, one by 3.
    auto graph = context.CreateInstance();
    std::vector<VCLG::SourceNode*> scales{};
    for (int i = 0; i < 3; ++i) {
        auto* counter = AddNode(*graph, "Counter");
        auto* scale = AddNode(*graph, "Scale");
        scale->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
        REQUIRE(Connect(*graph, counter->GetOutputs()[0], scale->GetInputs()[0]) != INVALID_IDENTITY);
        scales.push_back(scale);
    }
    FindParameter(scales[2], "Factor")->SetInitializerOverride(VCL::ConstantScalar{ 3.0f });

    // Counter, Scale by 2, Scale by 3; per instance, one copy each.
    REQUIRE(Emit(*graph, [](llvm::Module& m) { CHECK(CountProcesses(m) == 3); }, PerVariant(*this)));
    REQUIRE(Emit(*graph, [](llvm::Module& m) { CHECK(CountProcesses(m) == 6); }));
    CHECK(consumer.errors.empty());

    Test::CompiledGraph compiled = Compile(*graph, PerVariant(*this));
    compiled.Reset();
    compiled.Main();
    CHECK(*compiled.Output<float>(NodePath(*graph, scales[0]), "output") == 22.0f);
    CHECK(*compiled.Output<float>(NodePath(*graph, scales[1]), "output") == 22.0f);
    CHECK(*compiled.Output<float>(NodePath(*graph, scales[2]), "output") == 33.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "An instance's storage doesn't change its variant", "[Graph][Variants]") {
    // Two scales of one counter: only the first one's output is observed (a host port), the
    // second one's is a temporary.
    auto graph = context.CreateInstance();
    auto* counter = AddNode(*graph, "Counter");
    auto* observed = AddNode(*graph, "Scale");
    auto* hidden = AddNode(*graph, "Scale");
    observed->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    hidden->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(Connect(*graph, counter->GetOutputs()[0], observed->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(Connect(*graph, counter->GetOutputs()[0], hidden->GetInputs()[0]) != INVALID_IDENTITY);

    VCLG::CodeGenGraphOptions options = PerVariant(*this);
    options.planner.observeAllOutputs = false;
    options.planner.observedOutputs.insert(VCLG::GraphLayout::OutputKey(NodePath(*graph, observed), "output"));
    REQUIRE(Emit(*graph, [&](llvm::Module& m, VCLG::CodeGenGraph& cgg) {
        REQUIRE(cgg.GetLayout().FindRegion(VCLG::GraphLayout::OutputKey(NodePath(*graph, observed), "output")) != nullptr);
        REQUIRE(cgg.GetLayout().FindRegion(VCLG::GraphLayout::OutputKey(NodePath(*graph, hidden), "output")) == nullptr);
        CHECK(cgg.GetNodeInterface(NodePath(*graph, observed))->process == cgg.GetNodeInterface(NodePath(*graph, hidden))->process);
        CHECK(CountProcesses(m) == 2);
    }, options));
}

TEST_CASE_METHOD(Test::GraphTest, "A failing variant is reported at each instance, on every compile", "[Graph][Variants]") {
    // Fed an int32, FirstChannel's input is a scalar: `input.channels` doesn't compile.
    auto graph = context.CreateInstance();
    std::vector<std::string> paths{};
    for (int i = 0; i < 2; ++i) {
        auto* source = AddNode(*graph, "IntSource");
        auto* reader = AddNode(*graph, "Variants/FirstChannel");
        reader->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
        REQUIRE(Connect(*graph, source->GetOutputs()[0], reader->GetInputs()[0]) != INVALID_IDENTITY);
        paths.push_back(NodePath(*graph, reader));
    }

    for (int compile = 0; compile < 2; ++compile) {
        INFO("compile " << compile);
        consumer.errors.clear();
        consumer.errorPaths.clear();
        REQUIRE_FALSE(Emit(*graph, [](llvm::Module&) {}));
        REQUIRE(!consumer.errors.empty());
        size_t perInstance = std::count(consumer.errorPaths.begin(), consumer.errorPaths.end(), paths[0]);
        CHECK(perInstance > 0);
        CHECK(std::count(consumer.errorPaths.begin(), consumer.errorPaths.end(), paths[1]) == perInstance);
        CHECK(perInstance * 2 == consumer.errors.size());
    }
}

TEST_CASE_METHOD(Test::GraphTest, "A variant's warnings are reported at each instance, on every compile", "[Graph][Variants]") {
    auto graph = context.CreateInstance();
    for (int i = 0; i < 2; ++i)
        AddNode(*graph, "Translation/AlwaysWrittenReadFirst")->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);

    for (int compile = 0; compile < 2; ++compile) {
        INFO("compile " << compile);
        consumer.warnings.clear();
        REQUIRE(Emit(*graph, [](llvm::Module&) {}));
        CHECK(std::count_if(consumer.warnings.begin(), consumer.warnings.end(), [](const std::string& warning) {
            return warning.find("is [AlwaysWritten], but the node reads it before writing it") != std::string::npos;
        }) == 2);
    }
}

TEST_CASE_METHOD(Test::GraphTest, "A second compile of the same graph translates no variant", "[Graph][Variants]") {
    auto graph = context.CreateInstance();
    auto* counter = AddNode(*graph, "Counter");
    auto* scale = AddNode(*graph, "Scale");
    auto* mix = AddNode(*graph, "StructUser");
    scale->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    mix->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(Connect(*graph, counter->GetOutputs()[0], scale->GetInputs()[0]) != INVALID_IDENTITY);
    VCLG::VariantCache& cache = context.GetVariantCache();

    VCLG::VariantCache::Statistics start = cache.GetStatistics();
    Test::CompiledGraph first = Compile(*graph);
    CHECK(Since(start, cache).translated == 3);

    VCLG::VariantCache::Statistics before = cache.GetStatistics();
    Test::CompiledGraph second = Compile(*graph, std::nullopt, true);
    CacheDelta delta = Since(before, cache);
    CHECK(delta.translated == 0);
    CHECK(delta.reused == 3);

    // The cached variants run as the fresh ones.
    for (Test::CompiledGraph* compiled : { &first, &second }) {
        compiled->Reset();
        compiled->Main();
        CHECK(*compiled->Output<float>(NodePath(*graph, scale), "output") == 22.0f);
        CHECK(*compiled->Output<float>(NodePath(*graph, mix), "output") == 3.5f);
    }

    SECTION("without the cache, every variant is translated and the cache is left as is") {
        VCLG::CodeGenGraphOptions options = Options();
        options.useVariantCache = false;
        size_t size = cache.Size();
        VCLG::VariantCache::Statistics uncached = cache.GetStatistics();
        Test::CompiledGraph third = Compile(*graph, options);
        CHECK(Since(uncached, cache).translated == 0);
        CHECK(Since(uncached, cache).reused == 0);
        CHECK(cache.Size() == size);
        third.Reset();
        third.Main();
        CHECK(*third.Output<float>(NodePath(*graph, scale), "output") == 22.0f);
    }
}

TEST_CASE_METHOD(Test::GraphTest, "Changing one parameter translates one variant", "[Graph][Variants]") {
    auto graph = context.CreateInstance();
    auto* counter = AddNode(*graph, "Counter");
    auto* first = AddNode(*graph, "Scale");
    auto* second = AddNode(*graph, "Scale");
    first->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    second->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(Connect(*graph, counter->GetOutputs()[0], first->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(Connect(*graph, counter->GetOutputs()[0], second->GetInputs()[0]) != INVALID_IDENTITY);
    VCLG::VariantCache& cache = context.GetVariantCache();
    Compile(*graph);

    FindParameter(first, "Factor")->SetInitializerOverride(VCL::ConstantScalar{ 5.0f });
    VCLG::VariantCache::Statistics before = cache.GetStatistics();
    Test::CompiledGraph compiled = Compile(*graph);
    CacheDelta delta = Since(before, cache);
    CHECK(delta.translated == 1);
    // The counter and the other scale.
    CHECK(delta.reused == 2);

    compiled.Reset();
    compiled.Main();
    CHECK(*compiled.Output<float>(NodePath(*graph, first), "output") == 55.0f);
    CHECK(*compiled.Output<float>(NodePath(*graph, second), "output") == 22.0f);

    // Back to the first value: that variant is still cached.
    FindParameter(first, "Factor")->SetInitializerOverride(VCL::ConstantScalar{ 2.0f });
    before = cache.GetStatistics();
    Compile(*graph);
    CHECK(Since(before, cache).translated == 0);
}

TEST_CASE_METHOD(Test::GraphTest, "Editing a library invalidates only the variants importing it", "[Graph][Variants]") {
    auto graph = context.CreateInstance();
    auto* counter = AddNode(*graph, "Counter");
    auto* mix = AddNode(*graph, "StructUser");
    counter->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    mix->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    VCLG::VariantCache& cache = context.GetVariantCache();
    Compile(*graph);

    // What a host does when a library changes: the compiled library goes, the source is replaced.
    llvm::SmallString<128> path{};
    REQUIRE(!llvm::sys::fs::real_path("Libraries/Mix.vcl", path));
    const std::string edited = "template<typename T>\n"
                               "export float32 SumFields(T s) {\n"
                               "    return s.a + s.b + 1.0;\n"
                               "}\n";
    VCL::CompilerContext& cc = context.GetCompilerContext();
    REQUIRE(cc.GetModuleCache().Invalidate(path.str()));
    REQUIRE(cc.GetSourceManager().ReplaceFromMemory(edited, path.str()) != nullptr);

    VCLG::VariantCache::Statistics before = cache.GetStatistics();
    Test::CompiledGraph compiled = Compile(*graph);
    CacheDelta delta = Since(before, cache);
    CHECK(delta.translated == 1);
    CHECK(delta.reused == 1);

    compiled.Reset();
    compiled.Main();
    CHECK(*compiled.Output<float>(NodePath(*graph, mix), "output") == 4.5f);
    CHECK(*compiled.Output<float>(NodePath(*graph, counter), "output") == 11.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "Variants no compile uses are dropped", "[Graph][Variants]") {
    auto counters = context.CreateInstance();
    AddNode(*counters, "Counter")->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    auto scales = context.CreateInstance();
    AddNode(*scales, "Scale")->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    VCLG::VariantCache& cache = context.GetVariantCache();
    cache.maxIdleCompiles = 2;

    Compile(*counters);
    Compile(*scales);
    CHECK(cache.Size() == 2);
    // Two compiles without the counter.
    Compile(*scales);
    CHECK(cache.Size() == 1);

    VCLG::VariantCache::Statistics before = cache.GetStatistics();
    Compile(*counters);
    CHECK(Since(before, cache).translated == 1);
}
