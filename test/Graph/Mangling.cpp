#include "Common/GraphTest.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <algorithm>


static std::vector<std::string> Symbols(Test::GraphTest& test, VCLG::GraphInstance& graph) {
    std::vector<std::string> names{};
    REQUIRE(test.Emit(graph, [&](llvm::Module& m) {
        for (llvm::GlobalVariable& global : m.globals())
            names.push_back(global.getName().str());
        for (llvm::Function& function : m)
            if (!function.isDeclaration())
                names.push_back(function.getName().str());
    }));
    std::sort(names.begin(), names.end());
    return names;
}

TEST_CASE_METHOD(Test::GraphTest, "Symbol names are the same on every compile", "[Graph][Mangling]") {
    mode = GENERATE(Test::Legacy, Test::Planned);
    auto graph = context.CreateInstance();
    auto* a = AddNode(*graph, "Counter");
    auto* b = AddNode(*graph, "Scale");
    b->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(Connect(*graph, a->GetOutputs()[0], b->GetInputs()[0]) != INVALID_IDENTITY);

    REQUIRE(Symbols(*this, *graph) == Symbols(*this, *graph));
}

TEST_CASE_METHOD(Test::GraphTest, "Symbols are named after the node's path", "[Graph][Mangling]") {
    mode = GENERATE(Test::Legacy, Test::Planned);
    auto graph = context.CreateInstance();
    auto* first = AddNode(*graph, "Counter");
    auto* second = AddNode(*graph, "Counter");
    first->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    second->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);

    std::vector<std::string> names = Symbols(*this, *graph);
    auto has = [&](const std::string& name) { return std::find(names.begin(), names.end(), name) != names.end(); };
    // Two instances of the same node: two separate states and two separate functions. In planned
    // mode, a node's state is its region's bound symbol.
    std::string state = mode == Test::Legacy ? ".state" : "#state";
    REQUIRE(has(NodePath(*graph, first) + state));
    REQUIRE(has(NodePath(*graph, second) + state));
    REQUIRE(has(NodeSymbol(*graph, first, "Process")));
    REQUIRE(has(NodeSymbol(*graph, second, "Process")));
    REQUIRE(has("Main"));
    REQUIRE(has("Reset"));
}
