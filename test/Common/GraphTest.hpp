#pragma once

#include <catch2/catch_test_macros.hpp>

#include <VCLG/Graph/GraphContext.hpp>
#include <VCLG/Graph/GraphInstance.hpp>
#include <VCLG/Graph/BuiltinNodes.hpp>
#include <VCLG/CodeGen/CodeGenGraph.hpp>

#include <VCL/Core/SourceManager.hpp>
#include <VCL/Core/Diagnostic.hpp>
#include <VCL/Frontend/TextDiagnosticConsumer.hpp>
#include <VCL/Frontend/ExecutionSession.hpp>
#include <VCL/Frontend/Directives.hpp>

#include <llvm/IR/Verifier.h>

#include <memory>
#include <string>
#include <vector>


namespace Test {

    // Keeps error messages instead of printing them, so tests can check what was reported.
    class RecordingDiagnosticConsumer : public VCL::TextDiagnosticConsumer {
    public:
        void HandleTextDiagnostic(VCL::Diagnostic&& diagnostic, const std::string& message) override {
            if (diagnostic.GetSeverity() == VCL::Diagnostic::SeverityLevel::Error)
                errors.push_back(message);
        }

        inline bool HasError(const std::string& text) const {
            for (const std::string& error : errors)
                if (error.find(text) != std::string::npos)
                    return true;
            return false;
        }

        std::vector<std::string> errors{};
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
    };

}
