#include "Common/GraphTest.hpp"

#include <VCLG/Graph/Converter.hpp>

#include <VCL/AST/ConstantValue.hpp>


namespace {

    // Lets an int32 output feed a float32 input, and makes the input take the int32 type: the
    // input's type then depends on what feeds it, per copy.
    class AdoptingConverter : public VCLG::Converter {
    public:
        bool Convertible(VCL::Type* outType, VCL::Type* inType) override {
            // Only a float32 input as declared: not a dependent one (an AutoParameter aliasing float32).
            return IsBuiltin(outType, VCL::BuiltinType::Int32) && inType->GetTypeClass() == VCL::Type::BuiltinTypeClass
                && IsBuiltin(inType, VCL::BuiltinType::Float32);
        }

        VCL::Type* GetInputType(VCL::Type* outType, VCL::Type* inType) override {
            return outType;
        }

        bool Emit(llvm::IRBuilder<>& builder, VCL::Type* outType, VCL::Type* inType,
                llvm::GlobalVariable* outGV, llvm::GlobalVariable* inGV) override {
            builder.CreateStore(builder.CreateLoad(inGV->getValueType(), outGV), inGV);
            return true;
        }

    private:
        static bool IsBuiltin(VCL::Type* type, VCL::BuiltinType::Kind kind) {
            type = VCL::Type::GetCanonicalType(type);
            return type->GetTypeClass() == VCL::Type::BuiltinTypeClass && ((VCL::BuiltinType*)type)->GetKind() == kind;
        }
    };

    std::string NodePath(VCLG::GraphInstance& graph, VCLG::Node* node) {
        return "g" + std::to_string(graph.GetIdentity()) + "/n" + std::to_string(node->GetIdentity());
    }

    const VCLG::ElaboratedGraph::Node* FindNode(const VCLG::ElaboratedGraph& elaborated, const std::string& path) {
        for (const VCLG::ElaboratedGraph::Node& node : elaborated.GetNodes())
            if (node.path == path)
                return &node;
        return nullptr;
    }

    bool IsBuiltin(VCL::Type* type, VCL::BuiltinType::Kind kind) {
        if (!type)
            return false;
        type = VCL::Type::GetCanonicalType(type);
        return type->GetTypeClass() == VCL::Type::BuiltinTypeClass && ((VCL::BuiltinType*)type)->GetKind() == kind;
    }

}

TEST_CASE_METHOD(Test::GraphTest, "Each use of a subgraph gets its own types", "[Graph][Elaboration]") {
    AdoptingConverter converter{};
    context.AddConverter(&converter);

    // sub: in (float32) -> Passthrough -> out
    auto sub = context.CreateInstance();
    VCL::Type* float32 = context.GetGlobalASTContext().GetTypeCache().GetOrCreateBuiltinType(VCL::BuiltinType::Float32);
    auto* in = sub->InstantiateBuiltinNode<VCLG::SubgraphInputNode>(float32);
    auto* pass = AddNode(*sub, "Passthrough");
    auto* out = sub->InstantiateBuiltinNode<VCLG::SubgraphOutputNode>();
    REQUIRE(sub->Connect(in->GetOutputs()[0], pass->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(sub->Connect(pass->GetOutputs()[0], out->GetInputs()[0]) != INVALID_IDENTITY);

    // root: Add (float32) -> floatUse -> floatSink,  IntSource (int32, converted) -> intUse -> intSink
    auto root = context.CreateInstance();
    auto* floatSource = AddNode(*root, "Add");
    VCL::ConstantScalar five{ 5.0f };
    floatSource->GetInputs()[0]->SetInitializerOverride(five);
    auto* intSource = AddNode(*root, "IntSource");
    auto* floatUse = root->InstantiateBuiltinNode<VCLG::SubgraphNode>();
    floatUse->SetGraph(sub);
    auto* intUse = root->InstantiateBuiltinNode<VCLG::SubgraphNode>();
    intUse->SetGraph(sub);
    auto* floatSink = AddNode(*root, "Passthrough");
    auto* intSink = AddNode(*root, "Passthrough");
    floatSink->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    intSink->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(root->Connect(floatSource->GetOutputs()[0], floatUse->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(root->Connect(intSource->GetOutputs()[0], intUse->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(root->Connect(floatUse->GetOutputs()[0], floatSink->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(root->Connect(intUse->GetOutputs()[0], intSink->GetInputs()[0]) != INVALID_IDENTITY);

    VCLG::ElaboratedGraph elaborated = VCLG::Elaborate(*root);
    REQUIRE(elaborated.Succeeded());

    std::string passId = "/n" + std::to_string(pass->GetIdentity());
    const VCLG::ElaboratedGraph::Node* floatCopy = FindNode(elaborated, NodePath(*root, floatUse) + passId);
    const VCLG::ElaboratedGraph::Node* intCopy = FindNode(elaborated, NodePath(*root, intUse) + passId);
    REQUIRE(floatCopy != nullptr);
    REQUIRE(intCopy != nullptr);
    REQUIRE(IsBuiltin(floatCopy->outputs[0].type, VCL::BuiltinType::Float32));
    REQUIRE(IsBuiltin(intCopy->outputs[0].type, VCL::BuiltinType::Int32));

    // Seen from the root: the SubgraphNodes' ports and what they feed.
    REQUIRE(IsBuiltin(elaborated.GetPortType(intUse->GetInputs()[0]), VCL::BuiltinType::Int32));
    REQUIRE(IsBuiltin(elaborated.GetPortType(intUse->GetOutputs()[0]), VCL::BuiltinType::Int32));
    REQUIRE(IsBuiltin(elaborated.GetPortType(intSink->GetOutputs()[0]), VCL::BuiltinType::Int32));
    REQUIRE(IsBuiltin(elaborated.GetPortType(floatSink->GetOutputs()[0]), VCL::BuiltinType::Float32));

    // The subgraph on its own is unchanged.
    REQUIRE(IsBuiltin(VCLG::Elaborate(*sub).GetPortType(pass->GetOutputs()[0]), VCL::BuiltinType::Float32));

    Test::CompiledGraph compiled = Compile(*root);
    compiled.Main();
    REQUIRE(*compiled.Global<float>(NodeSymbol(*root, floatSink, "output")) == 5.0f);
    REQUIRE(*compiled.Global<int32_t>(NodeSymbol(*root, intSink, "output")) == 3);
}

TEST_CASE_METHOD(Test::GraphTest, "A type error inside one use of a subgraph names that use", "[Graph][Elaboration][Diagnostics]") {
    AdoptingConverter converter{};
    context.AddConverter(&converter);

    // sub: in (float32) -> Add. The Add can't take int32.
    auto sub = context.CreateInstance();
    VCL::Type* float32 = context.GetGlobalASTContext().GetTypeCache().GetOrCreateBuiltinType(VCL::BuiltinType::Float32);
    auto* in = sub->InstantiateBuiltinNode<VCLG::SubgraphInputNode>(float32);
    auto* add = AddNode(*sub, "Add");
    auto* out = sub->InstantiateBuiltinNode<VCLG::SubgraphOutputNode>();
    REQUIRE(sub->Connect(in->GetOutputs()[0], add->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(sub->Connect(add->GetOutputs()[0], out->GetInputs()[0]) != INVALID_IDENTITY);

    auto root = context.CreateInstance();
    auto* intSource = AddNode(*root, "IntSource");
    auto* use = root->InstantiateBuiltinNode<VCLG::SubgraphNode>();
    use->SetGraph(sub);
    auto* sink = AddNode(*root, "Add");
    sink->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(root->Connect(use->GetOutputs()[0], sink->GetInputs()[0]) != INVALID_IDENTITY);
    REQUIRE(root->Connect(intSource->GetOutputs()[0], use->GetInputs()[0]) != INVALID_IDENTITY);

    VCLG::ElaboratedGraph elaborated = VCLG::Elaborate(*root);
    REQUIRE_FALSE(elaborated.Succeeded());

    REQUIRE_FALSE(Emit(*root, [](llvm::Module&) {}));
    const std::string* path = consumer.FindError("can't take `int32`");
    REQUIRE(path != nullptr);
    REQUIRE(*path == NodePath(*root, use) + "/n" + std::to_string(add->GetIdentity()));
}

TEST_CASE_METHOD(Test::GraphTest, "A subgraph containing itself is reported", "[Graph][Elaboration][Regression]") {
    auto sub = context.CreateInstance();
    auto* self = sub->InstantiateBuiltinNode<VCLG::SubgraphNode>();
    self->SetGraph(sub);

    auto root = context.CreateInstance();
    auto* use = root->InstantiateBuiltinNode<VCLG::SubgraphNode>();
    use->SetGraph(sub);

    VCLG::ElaboratedGraph elaborated = VCLG::Elaborate(*root);
    REQUIRE_FALSE(elaborated.Succeeded());
    REQUIRE(elaborated.GetError()->message.find("contains itself") != std::string::npos);

    REQUIRE_FALSE(Emit(*root, [](llvm::Module&) {}));
    const std::string* path = consumer.FindError("contains itself");
    REQUIRE(path != nullptr);
    REQUIRE(*path == NodePath(*root, use) + "/n" + std::to_string(self->GetIdentity()));

    // Break the cycle before the graphs are destroyed.
    sub->DestroyNode(self);
}

TEST_CASE_METHOD(Test::GraphTest, "Elaboration runs only the subgraph uses that are needed", "[Graph][Elaboration][Order]") {
    auto sub = context.CreateInstance();
    auto* counter = AddNode(*sub, "Counter");
    auto* out = sub->InstantiateBuiltinNode<VCLG::SubgraphOutputNode>();
    REQUIRE(sub->Connect(counter->GetOutputs()[0], out->GetInputs()[0]) != INVALID_IDENTITY);

    auto root = context.CreateInstance();
    auto* used = root->InstantiateBuiltinNode<VCLG::SubgraphNode>();
    used->SetGraph(sub);
    auto* unused = root->InstantiateBuiltinNode<VCLG::SubgraphNode>();
    unused->SetGraph(sub);
    auto* sink = AddNode(*root, "Scale");
    sink->AddFlag(VCLG::Node::NodeFlag::IsOutputNode);
    REQUIRE(root->Connect(used->GetOutputs()[0], sink->GetInputs()[0]) != INVALID_IDENTITY);

    VCLG::ElaboratedGraph elaborated = VCLG::Elaborate(*root);
    REQUIRE(elaborated.Succeeded());
    std::string counterId = "/n" + std::to_string(counter->GetIdentity());
    bool usedRuns = false, unusedRuns = false;
    for (VCLG::ElaboratedGraph::NodeIndex index : elaborated.GetExecutionOrder()) {
        usedRuns |= elaborated.GetNode(index).path == NodePath(*root, used) + counterId;
        unusedRuns |= elaborated.GetNode(index).path == NodePath(*root, unused) + counterId;
    }
    REQUIRE(usedRuns);
    REQUIRE_FALSE(unusedRuns);
}
