#include "Common/GraphTest.hpp"

#include <VCL/AST/Expr.hpp>
#include <VCL/AST/Stmt.hpp>
#include <VCL/Core/Source.hpp>
#include <VCL/Core/Target.hpp>

#include <llvm/IR/DerivedTypes.h>

#include <cstdlib>
#include <cstring>


// The translation of a node on its own (P3.1, P3.3-P3.5): its entry points are compiled and called
// from C++ with a hand-allocated `State` and port values, as the graph codegen will call them.

namespace {

    // A zeroed, aligned block of `size` bytes (at least one).
    struct Block {
        explicit Block(uint64_t size, uint64_t alignment) {
            uint64_t allocated = ((std::max<uint64_t>(size, 1) + 63) / 64) * 64;
            data = std::aligned_alloc(std::max<uint64_t>(alignment, 64), allocated);
            std::memset(data, 0, allocated);
        }
        ~Block() { std::free(data); }
        Block(const Block&) = delete;
        Block& operator=(const Block&) = delete;

        template<typename T>
        T* At(uint64_t offset) { return (T*)((uint8_t*)data + offset); }

        void* data;
    };

    // Whether every statement of the translated functions (and every declaration) points into `source`.
    bool PointsInto(VCL::SourceRange range, llvm::StringRef buffer) {
        const char* start = range.start.GetPtr();
        return start != nullptr && start >= buffer.begin() && start < buffer.end();
    }

    bool StatementsPointInto(VCL::Stmt* stmt, llvm::StringRef buffer) {
        if (stmt == nullptr)
            return true;
        if (!PointsInto(stmt->GetSourceRange(), buffer))
            return false;
        switch (stmt->GetStmtClass()) {
            case VCL::Stmt::CompoundStmtClass:
                for (VCL::Stmt* s : ((VCL::CompoundStmt*)stmt)->GetStmts())
                    if (!StatementsPointInto(s, buffer))
                        return false;
                return true;
            case VCL::Stmt::IfStmtClass:
                return StatementsPointInto(((VCL::IfStmt*)stmt)->GetThenStmt(), buffer)
                    && StatementsPointInto(((VCL::IfStmt*)stmt)->GetElseStmt(), buffer);
            case VCL::Stmt::WhileStmtClass:
                return StatementsPointInto(((VCL::WhileStmt*)stmt)->GetThenStmt(), buffer);
            case VCL::Stmt::ForStmtClass:
                return StatementsPointInto(((VCL::ForStmt*)stmt)->GetThenStmt(), buffer);
            default:
                return true;
        }
    }

}

TEST_CASE_METHOD(Test::GraphTest, "The NodeInterface of a translated node", "[Translation][Interface]") {
    SECTION("Counter: state, an output, [NodeReset]") {
        Test::Translation counter = Translate("Counter");
        REQUIRE(counter.node.has_value());
        const VCLG::NodeInterface& interface = counter.Interface();
        REQUIRE(interface.definition == context.GetDefinitionRegistry().GetOrCreateSourceNodeDefinition(LoadNode("Counter")));
        REQUIRE(!interface.process.empty());
        REQUIRE(!interface.reset.empty());
        REQUIRE(!interface.init.empty());
        REQUIRE(interface.ports.size() == 1);
        REQUIRE(interface.ports[0].byReference); // an output
        REQUIRE(interface.ports[0].size == 4);
        REQUIRE(interface.stateFields.size() == 1);
        REQUIRE(interface.stateFields[0].offset == 0);
        REQUIRE(interface.stateSize == 4);
        REQUIRE(interface.hostSymbols.empty());

        // The entry points are external, under the node's prefix, with the ABI's attributes.
        counter.module.withModuleDo([&](llvm::Module& m) {
            llvm::Function* process = m.getFunction(interface.process);
            REQUIRE(process != nullptr);
            REQUIRE(process->hasExternalLinkage());
            REQUIRE(llvm::StringRef{ interface.process }.ends_with(".Process"));
            for (llvm::Argument& arg : process->args()) {
                REQUIRE(arg.hasNoAliasAttr());
                REQUIRE(arg.hasNoCaptureAttr());
                REQUIRE(arg.getDereferenceableBytes() == 4);
            }
        });
    }
    SECTION("Scale: a parameter is a constant, not state; no [NodeReset]") {
        Test::Translation scale = Translate("Scale");
        REQUIRE(scale.node.has_value());
        const VCLG::NodeInterface& interface = scale.Interface();
        REQUIRE(interface.reset.empty());
        REQUIRE(interface.stateSize == 0);
        REQUIRE(interface.stateFields.empty());
        REQUIRE(interface.ports.size() == 2);
        REQUIRE(!interface.ports[0].byReference); // a scalar input: by value
        REQUIRE(interface.ports[1].byReference);
    }
    SECTION("HostReader: the host variables it uses") {
        Test::Translation reader = Translate("Translation/HostReader");
        REQUIRE(reader.node.has_value());
        REQUIRE(reader.Interface().hostSymbols == std::vector<std::string>{ "Level", "Meter" });
        Run(reader);
        // `in` variables are defined by the host; `out` ones by the library that declares them.
        float level = 1.5f;
        reader.session->DefineSymbolPtr("Level", &level);
        Block state{ reader.Interface().stateSize, reader.Interface().stateAlignment };
        float output = 0.0f;
        reader.Function<void(void*, float*)>(reader.Interface().process)(state.data, &output);
        REQUIRE(output == 3.0f);
        REQUIRE(*(float*)reader.session->Lookup("Meter") == 3.0f);
    }
    SECTION("ArraySum: an aggregate input is passed by reference") {
        Test::Translation sum = Translate("ArraySum");
        REQUIRE(sum.node.has_value());
        REQUIRE(sum.Interface().ports[0].byReference);
        REQUIRE(sum.Interface().ports[0].size == 16);
    }
}

TEST_CASE_METHOD(Test::GraphTest, "Translated entry points run on a hand-allocated State", "[Translation][Execution]") {
    using ProcessOutput = void(void* self, float* output);

    SECTION("Counter: __Init, [NodeReset], three calls") {
        Test::Translation counter = Translate("Counter");
        Run(counter);
        const VCLG::NodeInterface& interface = counter.Interface();
        Block state{ interface.stateSize, interface.stateAlignment };
        float output = -1.0f;
        counter.Function<ProcessOutput>(interface.init)(state.data, &output);
        REQUIRE(output == 0.0f);
        counter.Function<ProcessOutput>(interface.reset)(state.data, &output);
        auto* process = counter.Function<ProcessOutput>(interface.process);
        for (float expected : { 11.0f, 12.0f, 13.0f }) {
            process(state.data, &output);
            REQUIRE(output == expected);
        }
        REQUIRE(*state.At<float>(interface.stateFields[0].offset) == 13.0f);
    }
    SECTION("Scale: an input by value, an output by reference") {
        Test::Translation scale = Translate("Scale");
        Run(scale);
        Block state{ scale.Interface().stateSize, scale.Interface().stateAlignment };
        float output = 0.0f;
        scale.Function<void(void*, float, float*)>(scale.Interface().process)(state.data, 3.0f, &output);
        REQUIRE(output == 6.0f); // the parameter's default factor, 2
    }
    SECTION("ArraySum: an aggregate input by reference, read by a helper") {
        Test::Translation sum = Translate("ArraySum");
        Run(sum);
        Block state{ sum.Interface().stateSize, sum.Interface().stateAlignment };
        alignas(64) float values[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
        float output = 0.0f;
        sum.Function<void(void*, float*, float*)>(sum.Interface().process)(state.data, values, &output);
        REQUIRE(output == 14.0f);
    }
    SECTION("A helper takes an output by reference and uses state") {
        Test::Translation node = Translate("Rules/OutputToInout");
        Run(node);
        Block state{ node.Interface().stateSize, node.Interface().stateAlignment };
        float output = 0.0f;
        auto* process = node.Function<void(void*, float, float*)>(node.Interface().process);
        process(state.data, 1.5f, &output);
        process(state.data, 2.0f, &output);
        REQUIRE(output == 3.5f);
        REQUIRE(*state.At<float>(node.Interface().stateFields[0].offset) == 3.5f);
    }
    SECTION("A template of the node using state, with two specializations") {
        Test::Translation node = Translate("Translation/TemplateState");
        Run(node);
        Block state{ node.Interface().stateSize, node.Interface().stateAlignment };
        float output = 0.0f;
        auto* process = node.Function<void(void*, float, float*)>(node.Interface().process);
        process(state.data, 2.0f, &output);
        REQUIRE(output == 5.0f);  // sum: 2, then 3
        process(state.data, 2.0f, &output);
        REQUIRE(output == 11.0f); // sum: 5, then 6
    }
    SECTION("A library template instantiated with the node's struct") {
        Test::Translation node = Translate("StructUser");
        Run(node);
        Block state{ node.Interface().stateSize, node.Interface().stateAlignment };
        float output = 0.0f;
        node.Function<ProcessOutput>(node.Interface().process)(state.data, &output);
        REQUIRE(output == 3.5f);
    }
}

TEST_CASE_METHOD(Test::GraphTest, "Translated code keeps the user's source locations", "[Translation][Diagnostics]") {
    Test::Translation counter = Translate("Translation/TemplateState");
    REQUIRE(counter.node.has_value());
    llvm::StringRef buffer = LoadNode("Translation/TemplateState")->GetBufferRef().getBuffer();
    VCL::TranslationUnitDecl* tu = counter.node->translationUnit;
    int functions = 0;
    for (auto it = tu->Begin(); it != tu->End(); ++it) {
        INFO("declaration " << (int)it->GetDeclClass());
        REQUIRE(PointsInto(it->GetSourceRange(), buffer));
        if (it->GetDeclClass() != VCL::Decl::FunctionDeclClass)
            continue;
        auto* function = (VCL::FunctionDecl*)it.Get();
        ++functions;
        for (auto param = function->Begin(); param != function->End(); ++param)
            REQUIRE(PointsInto(param->GetSourceRange(), buffer));
        REQUIRE(StatementsPointInto(function->GetBody(), buffer));
    }
    REQUIRE(functions == 4); // the two specializations of Add, Process, __Init
}

TEST_CASE_METHOD(Test::GraphTest, "The printed translation", "[Translation][Print]") {
    SECTION("Counter") {
        Test::Translation counter = Translate("Counter");
        REQUIRE(counter.node.has_value());
        REQUIRE(VCLG::PrintTranslation(*counter.node) ==
            "struct State { float32 state; }\n"
            "export void Reset(inout State & self [noalias nocapture align dereferenceable], "
                "out float32 & output [noalias nocapture align dereferenceable])\n"
            "export void Process(inout State & self [noalias nocapture align dereferenceable], "
                "out float32 & output [noalias nocapture align dereferenceable])\n"
            "export void __Init(inout State & self [noalias nocapture align dereferenceable], "
                "out float32 & output [noalias nocapture align dereferenceable])\n");
    }
    SECTION("Rules/OutputToInout: a helper keeps its parameters after self") {
        Test::Translation node = Translate("Rules/OutputToInout");
        REQUIRE(node.node.has_value());
        REQUIRE(VCLG::PrintTranslation(*node.node) ==
            "struct State { float32 total; }\n"
            "void Accumulate(inout State & self, float32 value, inout float32 & result)\n"
            "export void Process(inout State & self [noalias nocapture align dereferenceable], "
                "const float32 input [noalias nocapture readonly align dereferenceable], "
                "out float32 & output [noalias nocapture align dereferenceable])\n"
            "export void __Init(inout State & self [noalias nocapture align dereferenceable], "
                "out float32 & output [noalias nocapture align dereferenceable])\n");
    }
}

TEST_CASE_METHOD(Test::GraphTest, "The State layout is read from the emitted type", "[Translation][Layout]") {
    Test::Translation node = Translate("Translation/Layout");
    REQUIRE(node.node.has_value());
    const VCLG::NodeInterface& interface = node.Interface();
    REQUIRE(interface.stateFields.size() == 4);

    uint32_t width = context.GetCompilerContext().GetTarget().GetVectorWidthInElement();
    node.module.withModuleDo([&](llvm::Module& m) {
        llvm::LLVMContext& c = m.getContext();
        llvm::StructType* expected = llvm::StructType::get(c, {
            llvm::Type::getInt8Ty(c),
            llvm::FixedVectorType::get(llvm::Type::getFloatTy(c), width),
            llvm::ArrayType::get(llvm::Type::getInt32Ty(c), 5),
            llvm::Type::getDoubleTy(c) });
        const llvm::StructLayout* layout = m.getDataLayout().getStructLayout(expected);
        for (uint32_t i = 0; i < 4; ++i)
            REQUIRE(interface.stateFields[i].offset == layout->getElementOffset(i));
        REQUIRE(interface.stateSize == m.getDataLayout().getTypeAllocSize(expected));
    });
    // The vector field is aligned for SIMD, and so is the whole State.
    REQUIRE(interface.stateFields[1].offset % (width * 4) == 0);
    REQUIRE(interface.stateAlignment == width * 4);

    Run(node);
    Block state{ interface.stateSize, interface.stateAlignment };
    float output = 0.0f;
    node.Function<void(void*, float*)>(interface.init)(state.data, &output);
    REQUIRE(*state.At<uint8_t>(interface.stateFields[0].offset) == 1);
    REQUIRE(*state.At<double>(interface.stateFields[3].offset) == 0.5);
    node.Function<void(void*, float*)>(interface.process)(state.data, &output);
    REQUIRE(output == 2.5f);
    REQUIRE(*state.At<float>(interface.stateFields[1].offset + 4 * (width - 1)) == 1.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "The always-written proof", "[Translation][Proof]") {
    SECTION("An output written on every call is proven") {
        Test::Translation node = Translate("Scale");
        REQUIRE(node.node.has_value());
        REQUIRE(node.Interface().ports[1].provenAlwaysWritten);
        REQUIRE(node.Interface().IsAlwaysWritten(1));
    }
    SECTION("An output written on some calls is not") {
        Test::Translation node = Translate("Translation/ConditionalOutput");
        REQUIRE(node.node.has_value());
        REQUIRE(!node.Interface().ports[1].provenAlwaysWritten);
        REQUIRE(!node.Interface().IsAlwaysWritten(1));
    }
    SECTION("A promise the analysis can't see through") {
        Test::Translation node = Translate("Translation/LoopAlwaysWritten");
        REQUIRE(node.node.has_value());
        REQUIRE(!node.Interface().ports[1].provenAlwaysWritten);
        REQUIRE(node.Interface().IsAlwaysWritten(1));
        REQUIRE(consumer.warnings.empty());
    }
    SECTION("A promise the code contradicts is a warning") {
        Test::Translation node = Translate("Translation/AlwaysWrittenReadFirst");
        REQUIRE(node.node.has_value());
        REQUIRE(!node.Interface().ports[1].provenAlwaysWritten);
        REQUIRE(consumer.HasWarning("output 'output' is [AlwaysWritten], but the node reads it before writing it"));
    }
}
