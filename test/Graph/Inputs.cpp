#include "Common/GraphTest.hpp"

#include <VCLG/CodeGen/Optimizer.hpp>
#include <VCLG/Graph/Converter.hpp>


// Inputs are read-only (ASTInputConstWriter): a node that writes one is rejected when it's loaded.

namespace {

    // An int32 output feeding a float32 input: the conversion writes the input's global.
    class IntToFloatConverter : public VCLG::Converter {
    public:
        bool Convertible(VCL::Type* outType, VCL::Type* inType) override {
            return IsBuiltin(outType, VCL::BuiltinType::Int32) && IsBuiltin(inType, VCL::BuiltinType::Float32);
        }

        VCL::Type* GetInputType(VCL::Type* outType, VCL::Type* inType) override {
            return inType;
        }

        bool Emit(llvm::IRBuilder<>& builder, VCL::Type* outType, VCL::Type* inType,
                llvm::GlobalVariable* outGV, llvm::GlobalVariable* inGV) override {
            llvm::Value* value = builder.CreateLoad(builder.getInt32Ty(), outGV);
            builder.CreateStore(builder.CreateSIToFP(value, builder.getFloatTy()), inGV);
            return true;
        }

    private:
        static bool IsBuiltin(VCL::Type* type, VCL::BuiltinType::Kind kind) {
            type = VCL::Type::GetCanonicalType(type);
            return type->GetTypeClass() == VCL::Type::BuiltinTypeClass && ((VCL::BuiltinType*)type)->GetKind() == kind;
        }
    };

}

TEST_CASE_METHOD(Test::GraphTest, "Writing an input is an error", "[Graph][Inputs]") {
    auto graph = context.CreateInstance();
    REQUIRE(graph->InstantiateSourceNode(LoadNode("WritesInput")) == nullptr);
    REQUIRE(consumer.HasError("'input' is read-only"));
}

TEST_CASE_METHOD(Test::GraphTest, "Passing an input to an inout parameter is an error", "[Graph][Inputs]") {
    auto graph = context.CreateInstance();
    REQUIRE(graph->InstantiateSourceNode(LoadNode("InoutInput")) == nullptr);
    REQUIRE(consumer.HasError("qualifiers dropped"));
}

TEST_CASE_METHOD(Test::GraphTest, "An input can be read and passed to a default parameter", "[Graph][Inputs]") {
    auto graph = context.CreateInstance();
    auto* sum = AddNode(*graph, "ArraySum");
    sum->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);

    Test::CompiledGraph compiled = Compile(*graph);
    compiled.Main();
    REQUIRE(*compiled.Global<float>(NodeSymbol(*graph, sum, "output")) == 14.0f);
}

TEST_CASE_METHOD(Test::GraphTest, "A converter writes a const input, optimized", "[Graph][Inputs]") {
    // The input is const for the node only: if its global were an LLVM constant, the optimizer could
    // fold the node's read of it to the initializer (0) and drop the converter's store.
    IntToFloatConverter converter{};
    context.AddConverter(&converter);

    auto graph = context.CreateInstance();
    auto* source = AddNode(*graph, "IntSource");
    auto* scale = AddNode(*graph, "Scale");
    scale->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(Connect(*graph, source->GetOutputs()[0], scale->GetInputs()[0]) != INVALID_IDENTITY);

    std::string outputSymbol = NodeSymbol(*graph, scale, "output");
    llvm::orc::ThreadSafeModule module{
        context.GetCompilerContext().GetLLVMContext().withContextDo([](llvm::LLVMContext* c) {
            return std::make_unique<llvm::Module>("converted", *c);
        }),
        context.GetCompilerContext().GetLLVMContext() };
    bool optimized = module.withModuleDo([&](llvm::Module& m) {
        VCLG::CodeGenGraph cgg{ context, *graph, m };
        if (!cgg.Emit())
            return false;
        llvm::GlobalVariable* output = m.getGlobalVariable(outputSymbol, true);
        if (output == nullptr)
            return false;
        output->setLinkage(llvm::GlobalValue::ExternalLinkage); // kept by the optimizer
        output->setDSOLocal(false);
        VCLG::Optimizer optimizer{};
        return optimizer.Optimize(cgg);
    });
    REQUIRE(optimized);

    VCL::ExecutionSession session{};
    session.DefineDefaultMemIntrinsic();
    session.DefineDefaultMathIntrinsic();
    REQUIRE(session.SubmitModule(std::move(module)));
    ((void(*)())session.Lookup("Main"))();
    REQUIRE(*(float*)session.Lookup(outputSymbol) == 6.0f); // 3 * the default factor, 2
}
