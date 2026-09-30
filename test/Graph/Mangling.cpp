#include "Common/GraphTest.hpp"

#include <VCLG/Translation/VariantCache.hpp>


#include <algorithm>


static VCLG::CodeGenGraphOptions Sharing(Test::GraphTest& test, VCLG::CodeGenGraphOptions::CodeSharing sharing) {
    VCLG::CodeGenGraphOptions options = test.Options();
    options.codeSharing = sharing;
    return options;
}

static std::vector<std::string> Symbols(Test::GraphTest& test, VCLG::GraphInstance& graph,
        VCLG::CodeGenGraphOptions::CodeSharing sharing = VCLG::CodeGenGraphOptions::CodeSharing::PerInstance) {
    std::vector<std::string> names{};
    REQUIRE(test.Emit(graph, [&](llvm::Module& m) {
        for (llvm::GlobalVariable& global : m.globals())
            names.push_back(global.getName().str());
        for (llvm::Function& function : m)
            if (!function.isDeclaration())
                names.push_back(function.getName().str());
    }, Sharing(test, sharing)));
    std::sort(names.begin(), names.end());
    return names;
}

TEST_CASE_METHOD(Test::GraphTest, "Symbol names are the same on every compile", "[Graph][Mangling]") {
    auto graph = context.CreateInstance();
    auto* a = AddNode(*graph, "Counter");
    auto* b = AddNode(*graph, "Scale");
    b->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(Connect(*graph, a->GetOutputs()[0], b->GetInputs()[0]) != INVALID_IDENTITY);

    REQUIRE(Symbols(*this, *graph) == Symbols(*this, *graph));
    auto perVariant = VCLG::CodeGenGraphOptions::CodeSharing::PerVariant;
    REQUIRE(Symbols(*this, *graph, perVariant) == Symbols(*this, *graph, perVariant));
}

TEST_CASE_METHOD(Test::GraphTest, "Per variant, instances share its code and each bind their own state", "[Graph][Mangling]") {
    auto graph = context.CreateInstance();
    auto* first = AddNode(*graph, "Counter");
    auto* second = AddNode(*graph, "Counter");
    first->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    second->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);

    auto perVariant = VCLG::CodeGenGraphOptions::CodeSharing::PerVariant;
    std::vector<std::string> names = Symbols(*this, *graph, perVariant);
    auto has = [&](const std::string& name) { return std::find(names.begin(), names.end(), name) != names.end(); };
    // Two separate states (their regions' bound symbols), named after the instances' paths.
    std::string state = "#state";
    REQUIRE(has(NodePath(*graph, first) + state));
    REQUIRE(has(NodePath(*graph, second) + state));
    REQUIRE(has("Main"));
    REQUIRE(has("Reset"));
    // One function, named after the variant (§7.2).
    size_t processes = std::count_if(names.begin(), names.end(), [](const std::string& name) {
        return llvm::StringRef{ name }.ends_with(".Process");
    });
    REQUIRE(processes == 1);
    REQUIRE(Emit(*graph, [&](llvm::Module&, VCLG::CodeGenGraph& cgg) {
        const VCLG::NodeInterface* a = cgg.GetNodeInterface(NodePath(*graph, first));
        const VCLG::NodeInterface* b = cgg.GetNodeInterface(NodePath(*graph, second));
        REQUIRE((a != nullptr && b != nullptr));
        CHECK(a->process == b->process);
        CHECK(llvm::StringRef{ a->process }.starts_with("v"));
    }, Sharing(*this, perVariant)));
}

TEST_CASE_METHOD(Test::GraphTest, "Per instance, each instance has a copy of its variant named after its path", "[Graph][Mangling]") {
    auto graph = context.CreateInstance();
    auto* first = AddNode(*graph, "Counter");
    auto* second = AddNode(*graph, "Counter");
    first->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    second->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    VCLG::VariantCache& cache = context.GetVariantCache();

    VCLG::VariantCache::Statistics before = cache.GetStatistics();
    std::vector<std::string> names = Symbols(*this, *graph);
    // One translation, two copies.
    CHECK(cache.GetStatistics().translated - before.translated == 1);
    auto has = [&](const std::string& name) { return std::find(names.begin(), names.end(), name) != names.end(); };
    REQUIRE(has(NodePath(*graph, first) + ".Process"));
    REQUIRE(has(NodePath(*graph, second) + ".Process"));
    REQUIRE(has(NodePath(*graph, first) + "#state"));
    REQUIRE(has(NodePath(*graph, second) + "#state"));
    for (const std::string& name : names)
        CHECK_FALSE(llvm::StringRef{ name }.starts_with("v"));

    // Both run, each on its own state.
    Test::CompiledGraph compiled = Compile(*graph);
    compiled.Reset();
    compiled.Main();
    compiled.Main();
    CHECK(*compiled.Output<float>(NodePath(*graph, first), "output") == 12.0f);
    CHECK(*compiled.Output<float>(NodePath(*graph, second), "output") == 12.0f);
}
