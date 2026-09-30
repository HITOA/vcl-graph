#pragma once

#include <catch2/catch_test_macros.hpp>

#include <VCLG/Graph/GraphContext.hpp>
#include <VCLG/Graph/GraphInstance.hpp>
#include <VCLG/Graph/BuiltinNodes.hpp>
#include <VCLG/Graph/Elaboration.hpp>
#include <VCLG/CodeGen/CodeGenGraph.hpp>
#include <VCLG/CodeGen/Migration.hpp>
#include <VCLG/CodeGen/Optimizer.hpp>
#include <VCLG/CodeGen/SlotPlanner.hpp>
#include <VCLG/Core/Diagnostics.hpp>
#include <VCLG/Translation/Translation.hpp>

#include <VCL/Core/SourceManager.hpp>
#include <VCL/Core/Diagnostic.hpp>
#include <VCL/Frontend/TextDiagnosticConsumer.hpp>
#include <VCL/Frontend/ExecutionSession.hpp>
#include <VCL/Frontend/Directives.hpp>

#include <llvm/IR/Verifier.h>
#include <llvm/Support/MathExtras.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>


namespace Test {

    // Keeps error messages instead of printing them, so tests can check what was reported, and
    // the graph path of the node each error was attributed to (empty outside any node).
    class RecordingDiagnosticConsumer : public VCL::TextDiagnosticConsumer {
    public:
        void HandleTextDiagnostic(VCL::Diagnostic&& diagnostic, const std::string& message) override {
            if (diagnostic.GetSeverity() == VCL::Diagnostic::SeverityLevel::Warning)
                warnings.push_back(message);
            if (diagnostic.GetSeverity() != VCL::Diagnostic::SeverityLevel::Error)
                return;
            const VCLG::NodeDiagnosticScope* scope = VCLG::NodeDiagnosticScope::Current();
            errors.push_back(message);
            errorPaths.push_back(scope ? scope->GetPath() : std::string{});
        }

        inline bool HasError(const std::string& text) const {
            return FindError(text) != nullptr;
        }

        // Graph path of the first error containing `text`, or nullptr if there is none.
        inline const std::string* FindError(const std::string& text) const {
            for (size_t i = 0; i < errors.size(); ++i)
                if (errors[i].find(text) != std::string::npos)
                    return &errorPaths[i];
            return nullptr;
        }

        inline bool HasWarning(const std::string& text) const {
            for (const std::string& warning : warnings)
                if (warning.find(text) != std::string::npos)
                    return true;
            return false;
        }

        std::vector<std::string> errors{};
        std::vector<std::string> errorPaths{};
        std::vector<std::string> warnings{};
    };

    // A zeroed, aligned host allocation (a state or UI block).
    struct HostBlock {
        void* data = nullptr;

        HostBlock() = default;
        HostBlock(uint64_t size, uint64_t alignment) {
            alignment = std::max<uint64_t>(alignment, 64);
            size = llvm::alignTo(std::max<uint64_t>(size, 64), alignment);
            data = std::aligned_alloc(alignment, size);
            std::memset(data, 0, size);
        }
        HostBlock(const HostBlock&) = delete;
        HostBlock(HostBlock&& other) noexcept : data{ other.data } { other.data = nullptr; }
        HostBlock& operator=(HostBlock&& other) noexcept { std::swap(data, other.data); return *this; }
        ~HostBlock() { std::free(data); }

        inline uint8_t* Bytes() const { return (uint8_t*)data; }
    };

    // A compiled graph, ready to run: state and host ports are regions of the state block, found
    // through the layout by path and name (state fields through the node's interface).
    struct CompiledGraph {
        std::unique_ptr<VCL::ExecutionSession> session{};
        VCLG::GraphLayout layout{};
        HostBlock state{};
        HostBlock ui{};
        // Node path -> state field name -> offset in the node's `State`.
        std::map<std::string, std::map<std::string, uint64_t>> stateFields{};

        inline void Main() { ((void(*)(const void*))session->Lookup("Main"))(ui.data); }
        inline void Reset() { ((void(*)(const void*))session->Lookup("Reset"))(ui.data); }

        // A symbol of the module, or one the host binds (AudioOutput...).
        template<typename T>
        inline T* Global(const std::string& name) { return (T*)session->Lookup(name); }

        // Output `name` (the port's variable name) of the node at `path`: a host port (the fixture
        // observes every output unless told otherwise).
        template<typename T>
        inline T* Output(const std::string& path, const std::string& name) {
            const VCLG::GraphLayout::Region* region = layout.FindRegion(VCLG::GraphLayout::OutputKey(path, name));
            REQUIRE(region != nullptr);
            return (T*)(state.Bytes() + region->offset);
        }

        // State variable `field` of the node at `path`.
        template<typename T>
        inline T* State(const std::string& path, const std::string& field) {
            const VCLG::GraphLayout::Region* region = layout.FindRegion(VCLG::GraphLayout::StateKey(path));
            REQUIRE(region != nullptr);
            REQUIRE(stateFields[path].contains(field));
            return (T*)(state.Bytes() + region->offset + stateFields[path][field]);
        }

        // The value of every node's state variable `field`, in the order of the nodes' paths.
        template<typename T>
        std::vector<T> StateValues(const std::string& field) {
            std::vector<T> values{};
            for (auto& [path, fields] : stateFields)
                if (fields.contains(field))
                    values.push_back(*State<T>(path, field));
            return values;
        }
    };

    // What Grog does when it swaps a recompiled graph in: `to` (already Reset) takes over the
    // regions of `from` that are unchanged.
    inline VCLG::MigrationPlan Migrate(const CompiledGraph& from, CompiledGraph& to) {
        VCLG::MigrationPlan plan = VCLG::PlanMigration(from.layout, to.layout);
        plan.Apply(from.state.data, to.state.data);
        return plan;
    }

    // A node translated on its own (not through the graph codegen), and its module.
    struct Translation {
        VCLG::ElaboratedGraph elaborated{};
        std::optional<VCLG::TranslatedNode> node{};
        llvm::orc::ThreadSafeModule module{};
        std::unique_ptr<VCL::ExecutionSession> session{};

        inline const VCLG::NodeInterface& Interface() const { return node->interface; }

        template<typename F>
        inline F* Function(const std::string& symbol) { return (F*)session->Lookup(symbol); }
    };

    class GraphTest {
    public:
        GraphTest() : invocation{ MakeInvocation(consumer) }, context{ invocation } {
            VCL::CompilerContext& cc = context.GetCompilerContext();
            cc.GetDirectiveRegistry().CreateDirectiveHandler<VCL::ImportDirective>(cc.GetIdentifierTable().Get("import"), cc, "Libraries");
        }

        inline VCL::Source* LoadNode(const std::string& name) {
            VCL::Source* source = context.GetCompilerContext().GetSourceManager().LoadFromDisk("Nodes/" + name + ".vcl");
            REQUIRE(source != nullptr);
            return source;
        }

        inline VCLG::SourceNode* AddNode(VCLG::GraphInstance& graph, const std::string& name) {
            VCLG::SourceNode* node = graph.InstantiateSourceNode(LoadNode(name));
            REQUIRE(node != nullptr);
            return node;
        }

        inline VCLG::Identity Connect(VCLG::GraphInstance& graph, VCLG::Port* from, VCLG::Port* to) {
            return graph.Connect(from, to);
        }

        // Graph path of a node of the root graph `graph` (the prefix of its symbols, its key).
        static inline std::string NodePath(VCLG::GraphInstance& graph, VCLG::Node* node) {
            return "g" + std::to_string(graph.GetIdentity()) + "/n" + std::to_string(node->GetIdentity());
        }

        // Mangled name of a node's symbol (a function, a constant) compiled in the root graph `graph`.
        static inline std::string NodeSymbol(VCLG::GraphInstance& graph, VCLG::Node* node, const std::string& variable) {
            return NodePath(graph, node) + "." + variable;
        }

        // What the fixture compiles with: every output observed by the host, so that tests can read
        // them.
        inline VCLG::CodeGenGraphOptions Options() const {
            VCLG::CodeGenGraphOptions options{};
            options.planner.observeAllOutputs = true;
            return options;
        }

        // Emits `graph` into a fresh module and passes it to `inspect` (no optimization, nothing
        // internalized). Returns false if emission or verification failed.
        template<typename F>
        bool Emit(VCLG::GraphInstance& graph, F&& inspect) {
            llvm::orc::ThreadSafeModule module = MakeModule();
            return module.withModuleDo([&](llvm::Module& m) {
                VCLG::CodeGenGraph cgg{ context, graph, m, Options() };
                if (!cgg.Emit() || !cgg.LinkNow() || llvm::verifyModule(m, &llvm::errs()))
                    return false;
                inspect(m);
                return true;
            });
        }

        // The slot plan of `graph`, printed; empty if it can't be compiled.
        std::string Plan(VCLG::GraphInstance& graph, VCLG::CodeGenGraphOptions options = {}) {
            std::string plan{};
            llvm::orc::ThreadSafeModule module = MakeModule();
            module.withModuleDo([&](llvm::Module& m) {
                VCLG::CodeGenGraph cgg{ context, graph, m, options };
                if (cgg.Emit() && cgg.GetPlan() != nullptr)
                    plan = VCLG::PrintPlan(cgg.GetElaboratedGraph(), *cgg.GetPlan());
            });
            return plan;
        }

        // Emits, links and JIT-compiles `graph` with `options` (by default, Options()); with
        // `optimize`, through VCLG::Optimizer as Grog does. The state and UI blocks are allocated
        // and every region bound, as Grog's ExecutionContext does.
        CompiledGraph Compile(VCLG::GraphInstance& graph, std::optional<VCLG::CodeGenGraphOptions> options = std::nullopt,
                bool optimize = false) {
            if (!options)
                options = Options();
            CompiledGraph compiled{};
            llvm::orc::ThreadSafeModule module = MakeModule();
            bool emitted = module.withModuleDo([&](llvm::Module& m) {
                VCLG::CodeGenGraph cgg{ context, graph, m, *options };
                if (!cgg.Emit())
                    return false;
                compiled.layout = cgg.GetLayout();
                for (const VCLG::ElaboratedGraph::Node& node : cgg.GetElaboratedGraph().GetNodes()) {
                    const VCLG::NodeInterface* interface = cgg.GetNodeInterface(node.path);
                    if (interface == nullptr)
                        continue;
                    llvm::ArrayRef<VCLG::SourceStateDefinition> variables = interface->definition->GetStateVariables();
                    for (uint32_t i = 0; i < variables.size() && i < interface->stateFields.size(); ++i)
                        compiled.stateFields[node.path][variables[i].GetName()] = interface->stateFields[i].offset;
                }
                if (optimize) {
                    VCLG::Optimizer optimizer{};
                    return optimizer.Optimize(cgg) && !llvm::verifyModule(m, &llvm::errs());
                }
                return cgg.LinkNow() && !llvm::verifyModule(m, &llvm::errs());
            });
            std::string errors{};
            for (const std::string& error : consumer.errors)
                errors += error + "\n";
            INFO(errors);
            REQUIRE(emitted);
            compiled.session = std::make_unique<VCL::ExecutionSession>();
            compiled.session->DefineDefaultMemIntrinsic();
            compiled.session->DefineDefaultMathIntrinsic();
            compiled.state = HostBlock{ compiled.layout.state.size, compiled.layout.state.alignment };
            compiled.ui = HostBlock{ compiled.layout.ui.size, compiled.layout.ui.alignment };
            for (const VCLG::GraphLayout::Region& region : compiled.layout.regions)
                REQUIRE(compiled.session->DefineSymbolPtr(region.symbol, compiled.state.Bytes() + region.offset));
            REQUIRE(compiled.session->SubmitModule(std::move(module)));
            return compiled;
        }

        // Elaborates `graph` and translates `node` (a source node of the root graph) into its own
        // module; nullopt in `node` if the translation failed.
        Translation Translate(VCLG::GraphInstance& graph, VCLG::Node* node) {
            Translation translation{};
            translation.elaborated = VCLG::Elaborate(graph);
            REQUIRE(translation.elaborated.Succeeded());
            std::string path = "g" + std::to_string(graph.GetIdentity()) + "/n" + std::to_string(node->GetIdentity());
            const VCLG::ElaboratedGraph::Node* elaboratedNode = nullptr;
            for (const VCLG::ElaboratedGraph::Node& candidate : translation.elaborated.GetNodes())
                if (candidate.path == path)
                    elaboratedNode = &candidate;
            REQUIRE(elaboratedNode != nullptr);
            translation.module = MakeModule();
            translation.module.withModuleDo([&](llvm::Module& m) {
                translation.node = VCLG::TranslateSourceNode(context, context.GetCompilerContext(), *elaboratedNode, m);
            });
            return translation;
        }

        // Translates `name`, alone in a graph.
        Translation Translate(const std::string& name) {
            auto graph = context.CreateInstance();
            graphs.push_back(graph);
            VCLG::SourceNode* node = AddNode(*graph, name);
            node->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
            return Translate(*graph, node);
        }

        // Links the translated module's libraries and JIT-compiles it.
        void Run(Translation& translation) {
            REQUIRE(translation.node.has_value());
            bool linked = translation.module.withModuleDo([&](llvm::Module& m) {
                return VCLG::LinkLibraries(m, *translation.node->instance, context.GetCompilerContext().GetDiagnosticReporter())
                    && !llvm::verifyModule(m, &llvm::errs());
            });
            REQUIRE(linked);
            translation.session = std::make_unique<VCL::ExecutionSession>();
            translation.session->DefineDefaultMemIntrinsic();
            translation.session->DefineDefaultMathIntrinsic();
            REQUIRE(translation.session->SubmitModule(std::move(translation.module)));
        }

    private:
        static inline std::shared_ptr<VCL::CompilerInvocation> MakeInvocation(RecordingDiagnosticConsumer& consumer) {
            auto invocation = std::make_shared<VCL::CompilerInvocation>();
            invocation->GetDiagnosticOptions().SetDiagnosticConsumer(&consumer);
            return invocation;
        }

        inline llvm::orc::ThreadSafeModule MakeModule() {
            return llvm::orc::ThreadSafeModule{
                context.GetCompilerContext().GetLLVMContext().withContextDo([](llvm::LLVMContext* c) {
                    return std::make_unique<llvm::Module>("test graph", *c);
                }),
                context.GetCompilerContext().GetLLVMContext() };
        }

    public:
        RecordingDiagnosticConsumer consumer{};
        std::shared_ptr<VCL::CompilerInvocation> invocation;
        VCLG::GraphContext context;
        // Graphs made by the fixture (Translate), destroyed before the context.
        std::vector<std::shared_ptr<VCLG::GraphInstance>> graphs{};
    };

}
