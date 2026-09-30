#pragma once

#include <catch2/catch_test_macros.hpp>

#include <VCLG/Graph/GraphContext.hpp>
#include <VCLG/Graph/GraphInstance.hpp>
#include <VCLG/Graph/BuiltinNodes.hpp>
#include <VCLG/Graph/Elaboration.hpp>
#include <VCLG/CodeGen/CodeGenGraph.hpp>
#include <VCLG/Core/Diagnostics.hpp>
#include <VCLG/Translation/Translation.hpp>

#include <VCL/Core/SourceManager.hpp>
#include <VCL/Core/Diagnostic.hpp>
#include <VCL/Frontend/TextDiagnosticConsumer.hpp>
#include <VCL/Frontend/ExecutionSession.hpp>
#include <VCL/Frontend/Directives.hpp>

#include <llvm/IR/Verifier.h>

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

    // A compiled graph, ready to run. Globals are looked up by their mangled name, which is the
    // node's path in the graph plus the variable name (see NodeSymbol).
    struct CompiledGraph {
        std::unique_ptr<VCL::ExecutionSession> session{};

        inline void Main() { ((void(*)())session->Lookup("Main"))(); }
        inline void Reset() { ((void(*)())session->Lookup("Reset"))(); }

        template<typename T>
        inline T* Global(const std::string& name) { return (T*)session->Lookup(name); }
    };

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

        // Mangled name of a variable of a node compiled in the root graph `graph`.
        static inline std::string NodeSymbol(VCLG::GraphInstance& graph, VCLG::Node* node, const std::string& variable) {
            return "g" + std::to_string(graph.GetIdentity()) + "/n" + std::to_string(node->GetIdentity()) + "." + variable;
        }

        // Emits `graph` into a fresh module and passes it to `inspect` (no optimization, nothing
        // internalized). Returns false if emission or verification failed.
        template<typename F>
        bool Emit(VCLG::GraphInstance& graph, F&& inspect) {
            llvm::orc::ThreadSafeModule module = MakeModule();
            return module.withModuleDo([&](llvm::Module& m) {
                VCLG::CodeGenGraph cgg{ context, graph, m };
                if (!cgg.Emit() || !cgg.LinkNow() || llvm::verifyModule(m, &llvm::errs()))
                    return false;
                inspect(m);
                return true;
            });
        }

        // Emits, links and JIT-compiles `graph`. Every defined symbol is made external so that
        // tests can read node state by name.
        CompiledGraph Compile(VCLG::GraphInstance& graph) {
            CompiledGraph compiled{};
            llvm::orc::ThreadSafeModule module = MakeModule();
            bool emitted = module.withModuleDo([&](llvm::Module& m) {
                VCLG::CodeGenGraph cgg{ context, graph, m };
                if (!cgg.Emit() || !cgg.LinkNow() || llvm::verifyModule(m, &llvm::errs()))
                    return false;
                for (llvm::GlobalVariable& global : m.globals())
                    if (!global.isDeclaration())
                        global.setLinkage(llvm::GlobalValue::ExternalLinkage);
                for (llvm::Function& function : m)
                    if (!function.isDeclaration())
                        function.setLinkage(llvm::GlobalValue::ExternalLinkage);
                return true;
            });
            REQUIRE(emitted);
            compiled.session = std::make_unique<VCL::ExecutionSession>();
            compiled.session->DefineDefaultMemIntrinsic();
            compiled.session->DefineDefaultMathIntrinsic();
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
