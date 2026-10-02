#include <VCLG/Graph/Elaboration.hpp>

#include <VCLG/Graph/GraphContext.hpp>
#include <VCLG/Graph/GraphInstance.hpp>
#include <VCLG/Graph/BuiltinNodes.hpp>
#include <VCLG/Graph/Converter.hpp>
#include <VCLG/Graph/Definition.hpp>
#include <VCLG/Graph/Parameter.hpp>
#include <VCLG/Graph/Port.hpp>

#include <VCL/AST/Decl.hpp>
#include <VCL/AST/Expr.hpp>
#include <VCL/AST/Template.hpp>
#include <VCL/AST/TypePrinter.hpp>
#include <VCL/Core/SourceManager.hpp>

#include <algorithm>
#include <cstring>
#include <functional>
#include <unordered_set>


VCLG::ElaboratedGraph::ElaboratedGraph() : nodes{}, scopes{}, executionOrder{}, rootPorts{}, error{},
    allocator{ std::make_unique<llvm::BumpPtrAllocator>() } {}

VCL::Type* VCLG::ElaboratedGraph::GetPortType(const Port* port) const {
    auto it = rootPorts.find(port);
    if (it == rootPorts.end())
        return nullptr;
    const Node& node = nodes[it->second.node];
    return it->second.isInput ? node.inputs[it->second.index].type : node.outputs[it->second.index].type;
}


namespace VCLG {

    class Elaborator {
    public:
        using NodeIndex = ElaboratedGraph::NodeIndex;
        using ScopeIndex = ElaboratedGraph::ScopeIndex;
        static constexpr uint32_t Invalid = ElaboratedGraph::Invalid;

        Elaborator(GraphContext& context, ElaboratedGraph& result) : context{ context }, result{ result } {}

        void Run(GraphInstance& root);

    private:
        using PortSlot = ElaboratedGraph::PortSlot;
        using PortMap = llvm::DenseMap<const Port*, PortSlot>;
        using IdentityMap = llvm::DenseMap<Identity, NodeIndex>;

        // Flattening
        bool Flatten(GraphInstance& graph, ScopeIndex scope, PortMap& ports, IdentityMap& byIdentity);
        NodeIndex AddNode(VCLG::Node* node, ScopeIndex scope);
        void AddPorts(ElaboratedGraph::Node& node, NodeIndex index, VCLG::Node* editorNode, PortMap& ports);
        void AddGenericAlias(ElaboratedGraph::Node& node, VCL::Type* type);

        // Inference
        bool Infer();
        void ResetTypes();
        bool Propagate(NodeIndex index);
        bool IsConverted(NodeIndex index) const;
        bool Check();
        bool SubstituteType(NodeIndex index, VCL::Type* baseType, VCL::Type* connectedType);
        bool SubstituteExpression(NodeIndex index, VCL::DeclRefExpr* baseExpr, VCL::ConstantScalar* scalar);
        VCL::Type* GenerateSubstitutedType(NodeIndex index, VCL::Type* baseType);
        static std::optional<uint64_t> GetConstantScalarDataFromTemplateArgument(const VCL::TemplateArgument& arg);

        // Order
        bool TopologicalOrder(llvm::ArrayRef<NodeIndex> roots, std::vector<NodeIndex>& order);
        void BuildExecutionOrder();

        bool Fail(NodeIndex node, ScopeIndex scope, std::string message);
        std::string TypeName(VCL::Type* type);

    private:
        GraphContext& context;
        ElaboratedGraph& result;

        /** Graphs being flattened, to reject a subgraph that contains itself. */
        std::vector<GraphInstance*> stack{};
        /** Output nodes of each scope: the roots of the execution order once the scope runs. */
        std::vector<std::vector<NodeIndex>> scopeOutputNodes{};
        /** For each node and output, the inputs it feeds through plain connections. */
        std::vector<llvm::SmallVector<llvm::SmallVector<std::pair<NodeIndex, uint32_t>, 2>, 2>> consumers{};
        /** Feedback Outputs reading each Feedback Input. */
        llvm::DenseMap<NodeIndex, llvm::SmallVector<NodeIndex, 2>> feedbackReaders{};
    };

}

namespace {

    // As Elaborator::Flatten: a use's scope path is its parent's plus "/n<SubgraphNode identity>",
    // a node's path its scope's plus "/n<identity>".
    void CollectNodePaths(VCLG::GraphInstance& graph, const std::string& scope, const VCLG::GraphInstance& target,
            VCLG::Identity node, std::vector<const VCLG::GraphInstance*>& stack, std::vector<std::string>& paths) {
        if (std::find(stack.begin(), stack.end(), &graph) != stack.end())
            return;
        stack.push_back(&graph);
        if (&graph == &target)
            paths.push_back(scope + "/n" + std::to_string(node));
        for (VCLG::Node* child : graph.GetNodes()) {
            auto* use = llvm::dyn_cast<VCLG::SubgraphNode>(child);
            std::shared_ptr<VCLG::GraphInstance> subgraph = use ? use->GetGraph() : nullptr;
            if (subgraph)
                CollectNodePaths(*subgraph, scope + "/n" + std::to_string(child->GetIdentity()), target, node, stack, paths);
        }
        stack.pop_back();
    }

}

std::vector<std::string> VCLG::FindNodePaths(GraphInstance& root, const GraphInstance& graph, Identity node) {
    std::vector<std::string> paths{};
    std::vector<const GraphInstance*> stack{};
    CollectNodePaths(root, "g" + std::to_string(root.GetIdentity()), graph, node, stack, paths);
    return paths;
}

VCLG::ElaboratedGraph VCLG::Elaborate(GraphInstance& root) {
    ElaboratedGraph result{};
    Elaborator elaborator{ root.GetGraphContext(), result };
    elaborator.Run(root);
    return result;
}

void VCLG::Elaborator::Run(GraphInstance& root) {
    result.scopes.push_back({ Invalid, "g" + std::to_string(root.GetIdentity()), root.GetName(), root.GetIdentity(),
        root.GetGeneration() });
    scopeOutputNodes.emplace_back();

    IdentityMap byIdentity{};
    if (!Flatten(root, 0, result.rootPorts, byIdentity))
        return;

    consumers.resize(result.nodes.size());
    for (NodeIndex i = 0; i < result.nodes.size(); ++i) {
        ElaboratedGraph::Node& node = result.nodes[i];
        consumers[i].resize(node.outputs.size());
        if (node.kind == VCLG::Node::NodeKind::FeedbackOutput && node.feedbackInput != Invalid)
            feedbackReaders[node.feedbackInput].push_back(i);
    }
    for (NodeIndex i = 0; i < result.nodes.size(); ++i) {
        ElaboratedGraph::Node& node = result.nodes[i];
        for (uint32_t j = 0; j < node.inputs.size(); ++j) {
            const std::optional<ElaboratedGraph::Edge>& edge = node.inputs[j].edge;
            if (edge && edge->converter == nullptr)
                consumers[edge->node][edge->output].push_back({ i, j });
        }
    }

    if (!Infer())
        return;
    BuildExecutionOrder();
}

//
// Flattening
//

bool VCLG::Elaborator::Flatten(GraphInstance& graph, ScopeIndex scope, PortMap& ports, IdentityMap& byIdentity) {
    if (std::find(stack.begin(), stack.end(), &graph) != stack.end())
        return Fail(Invalid, scope, "subgraph `" + graph.GetName() + "` contains itself");
    stack.push_back(&graph);

    for (VCLG::Node* node : graph.GetNodes()) {
        SubgraphNode* subgraphNode = llvm::dyn_cast<SubgraphNode>(node);
        if (!subgraphNode) {
            NodeIndex index = AddNode(node, scope);
            byIdentity[node->GetIdentity()] = index;
            AddPorts(result.nodes[index], index, node, ports);
            continue;
        }

        std::shared_ptr<GraphInstance> subgraph = subgraphNode->GetGraph();
        if (!subgraph)
            continue;

        ScopeIndex childScope = (ScopeIndex)result.scopes.size();
        result.scopes.push_back({ scope, result.scopes[scope].path + "/n" + std::to_string(node->GetIdentity()),
            node->GetDisplayName().str(), subgraph->GetIdentity(), subgraph->GetGeneration() });
        scopeOutputNodes.emplace_back();

        PortMap childPorts{};
        IdentityMap childByIdentity{};
        if (!Flatten(*subgraph, childScope, childPorts, childByIdentity))
            return false;

        // The SubgraphNode's ports are the ports of this copy's Subgraph Input and Output nodes.
        for (VCLG::Node* inner : subgraph->GetNodes()) {
            if (!llvm::isa<SubgraphInputNode>(inner) && !llvm::isa<SubgraphOutputNode>(inner))
                continue;
            Port* port = subgraphNode->GetPortForNode(inner->GetIdentity());
            if (!port)
                continue;
            NodeIndex innerIndex = childByIdentity.at(inner->GetIdentity());
            if (llvm::isa<SubgraphInputNode>(inner)) {
                ports[port] = { innerIndex, 0, true };
                if (port->GetInitializerOverride())
                    result.nodes[innerIndex].inputs[0].initializer = *port->GetInitializerOverride();
            } else {
                ports[port] = { innerIndex, 0, false };
            }
        }
    }

    for (VCLG::Node* node : graph.GetNodes()) {
        FeedbackOutputNode* feedbackOutput = llvm::dyn_cast<FeedbackOutputNode>(node);
        if (!feedbackOutput)
            continue;
        auto it = byIdentity.find(feedbackOutput->GetFeedbackIdentity());
        if (it != byIdentity.end() && result.nodes[it->second].kind == VCLG::Node::NodeKind::FeedbackInput)
            result.nodes[byIdentity.at(node->GetIdentity())].feedbackInput = it->second;
    }

    for (const Connection& connection : graph.GetConnections()) {
        auto in = ports.find(graph.GetPortByIdentity(connection.GetInputPortIdentity()));
        auto out = ports.find(graph.GetPortByIdentity(connection.GetOutputPortIdentity()));
        if (in == ports.end() || out == ports.end() || !in->second.isInput || out->second.isInput)
            continue;
        result.nodes[in->second.node].inputs[in->second.index].edge =
            ElaboratedGraph::Edge{ out->second.node, out->second.index, connection.GetConverter() };
    }

    stack.pop_back();
    return true;
}

VCLG::Elaborator::NodeIndex VCLG::Elaborator::AddNode(VCLG::Node* node, ScopeIndex scope) {
    NodeIndex index = (NodeIndex)result.nodes.size();
    ElaboratedGraph::Node& elaborated = result.nodes.emplace_back();
    elaborated.kind = node->GetKind();
    elaborated.path = result.scopes[scope].path + "/n" + std::to_string(node->GetIdentity());
    elaborated.displayName = node->GetDisplayName().str();
    elaborated.scope = scope;
    elaborated.isOutputNode = node->HasFlag(VCLG::Node::NodeFlag::IsOutputNode);
    elaborated.definition = nullptr;
    elaborated.feedbackInput = Invalid;
    if (elaborated.isOutputNode)
        scopeOutputNodes[scope].push_back(index);
    return index;
}

void VCLG::Elaborator::AddPorts(ElaboratedGraph::Node& node, NodeIndex index, VCLG::Node* editorNode, PortMap& ports) {
    for (Port* port : editorNode->GetInputs()) {
        ElaboratedGraph::Input& input = node.inputs.emplace_back();
        input.name = port->GetDisplayName();
        input.declaredType = port->GetType();
        input.type = nullptr;
        input.isDependent = port->IsDependent();
        if (port->GetInitializerOverride())
            input.initializer = *port->GetInitializerOverride();
        ports[port] = { index, (uint32_t)node.inputs.size() - 1, true };
    }
    for (Port* port : editorNode->GetOutputs()) {
        ElaboratedGraph::Output& output = node.outputs.emplace_back();
        output.name = port->GetDisplayName();
        output.declaredType = port->GetType();
        output.type = nullptr;
        output.isDependent = port->IsDependent();
        ports[port] = { index, (uint32_t)node.outputs.size() - 1, false };
    }

    switch (node.kind) {
        case VCLG::Node::NodeKind::Source: {
            SourceNode* sourceNode = llvm::cast<SourceNode>(editorNode);
            node.source = sourceNode->GetSource().str();
            VCL::Source* source = context.GetCompilerContext().GetSourceManager().GetSourceFromName(node.source);
            if (source)
                node.definition = context.GetDefinitionRegistry().GetOrCreateSourceNodeDefinition(source);
            if (node.definition) {
                for (const SourceAutoParameterDefinition& autoParam : node.definition->GetAutoParameters()) {
                    if (autoParam.GetDecl()->GetDeclClass() == VCL::Decl::TypeAliasDeclClass)
                        node.substitutions.SetTypeSubstitution((VCL::TypeAliasDecl*)autoParam.GetDecl(), nullptr);
                    else
                        node.substitutions.SetScalarSubstitution((VCL::VarDecl*)autoParam.GetDecl(), nullptr);
                }
            }
            for (Parameter* parameter : sourceNode->GetParameters()) {
                if (parameter->GetInitializerOverride())
                    node.parameters.push_back(*parameter->GetInitializerOverride());
                else
                    node.parameters.push_back(std::nullopt);
            }
            break;
        }
        case VCLG::Node::NodeKind::SubgraphInput: {
            // Carries the value given to the SubgraphNode's input: one input (connected by
            // Flatten), one output of the same type.
            VCL::Type* type = llvm::cast<SubgraphInputNode>(editorNode)->GetType();
            ElaboratedGraph::Input& input = node.inputs.emplace_back();
            input.name = node.displayName;
            input.declaredType = type;
            input.type = nullptr;
            input.isDependent = false;
            break;
        }
        case VCLG::Node::NodeKind::SubgraphOutput: {
            // Its output is what the SubgraphNode's consumers read: same "Generic" type as its input.
            ElaboratedGraph::Output& output = node.outputs.emplace_back();
            output.name = node.displayName;
            output.declaredType = node.inputs[0].declaredType;
            output.type = nullptr;
            output.isDependent = true;
            AddGenericAlias(node, node.inputs[0].declaredType);
            break;
        }
        case VCLG::Node::NodeKind::FeedbackInput:
            AddGenericAlias(node, node.inputs[0].declaredType);
            break;
        default:
            break;
    }
}

void VCLG::Elaborator::AddGenericAlias(ElaboratedGraph::Node& node, VCL::Type* type) {
    if (type->GetTypeClass() == VCL::Type::TypeAliasTypeClass)
        node.substitutions.SetTypeSubstitution(((VCL::TypeAliasType*)type)->GetDecl(), nullptr);
}

//
// Inference
//

bool VCLG::Elaborator::Infer() {
    std::vector<NodeIndex> all{};
    all.reserve(result.nodes.size());
    for (NodeIndex i = 0; i < result.nodes.size(); ++i)
        all.push_back(i);
    std::vector<NodeIndex> order{};
    if (!TopologicalOrder(all, order))
        return Fail(Invalid, 0, "the graph contains a cycle");

    // A Feedback Output's type is the type inferred for its Feedback Input, which can depend on
    // what the Feedback Output feeds. Start from the type the editor gave its port, and infer
    // again while it changes.
    constexpr int maxPasses = 4;
    for (int pass = 0; pass < maxPasses; ++pass) {
        ResetTypes();
        // Two sweeps: consumers to producers, then producers to consumers.
        for (auto it = order.rbegin(); it != order.rend(); ++it)
            if (!Propagate(*it))
                return false;
        for (NodeIndex index : order)
            if (!Propagate(index))
                return false;

        bool changed = false;
        for (ElaboratedGraph::Node& node : result.nodes) {
            if (node.kind != VCLG::Node::NodeKind::FeedbackOutput || node.feedbackInput == Invalid || node.outputs.empty())
                continue;
            VCL::Type* type = result.nodes[node.feedbackInput].inputs[0].type;
            if (type && type != node.outputs[0].declaredType) {
                node.outputs[0].declaredType = type;
                changed = true;
            }
        }
        if (!changed)
            break;
    }

    return Check();
}

void VCLG::Elaborator::ResetTypes() {
    result.allocator->Reset();
    for (ElaboratedGraph::Node& node : result.nodes) {
        node.substitutions.ClearValues();
        for (ElaboratedGraph::Input& input : node.inputs)
            input.type = nullptr;
        for (ElaboratedGraph::Output& output : node.outputs)
            output.type = nullptr;
    }
}

bool VCLG::Elaborator::Propagate(NodeIndex index) {
    // Unify this node's dependent ports with what they're connected to, in both directions.
    for (uint32_t i = 0; i < result.nodes[index].inputs.size(); ++i) {
        const ElaboratedGraph::Input& input = result.nodes[index].inputs[i];
        if (!input.edge || input.edge->converter != nullptr)
            continue;
        const ElaboratedGraph::Output& source = result.nodes[input.edge->node].outputs[input.edge->output];
        if (!input.isDependent && !source.isDependent)
            continue;
        if ((source.isDependent || IsConverted(input.edge->node)) && source.type == nullptr)
            continue;
        if (!SubstituteType(index, input.declaredType, ElaboratedGraph::TypeOf(source)))
            return Fail(index, result.nodes[index].scope, "input `" + input.name + "` can't take `"
                + TypeName(ElaboratedGraph::TypeOf(source)) + "`");
    }
    for (uint32_t o = 0; o < result.nodes[index].outputs.size(); ++o) {
        const ElaboratedGraph::Output& output = result.nodes[index].outputs[o];
        for (auto [consumer, consumerInput] : consumers[index][o]) {
            const ElaboratedGraph::Input& input = result.nodes[consumer].inputs[consumerInput];
            if (!input.isDependent && !output.isDependent)
                continue;
            if (input.isDependent && input.type == nullptr)
                continue;
            if (!SubstituteType(index, output.declaredType, ElaboratedGraph::TypeOf(input)))
                return Fail(index, result.nodes[index].scope, "output `" + output.name + "` can't give `"
                    + TypeName(ElaboratedGraph::TypeOf(input)) + "`");
        }
    }

    ElaboratedGraph::Node& node = result.nodes[index];
    for (ElaboratedGraph::Input& input : node.inputs)
        if (input.isDependent)
            input.type = GenerateSubstitutedType(index, input.declaredType);
    for (ElaboratedGraph::Output& output : node.outputs)
        if (output.isDependent)
            output.type = GenerateSubstitutedType(index, output.declaredType);

    // An input fed through a converter takes the type the converter gives it.
    for (ElaboratedGraph::Input& input : node.inputs) {
        if (!input.edge || input.edge->converter == nullptr)
            continue;
        VCL::Type* sourceType = ElaboratedGraph::TypeOf(result.nodes[input.edge->node].outputs[input.edge->output]);
        input.type = input.edge->converter->Convertible(sourceType, input.declaredType)
            ? input.edge->converter->GetInputType(sourceType, input.declaredType) : nullptr;
    }

    // A Subgraph Input passes on the value it's given, in the type it's given.
    if (node.kind == VCLG::Node::NodeKind::SubgraphInput)
        node.outputs[0].type = node.inputs[0].type;

    return true;
}

// A Subgraph Input fed through a converter: its output's type is only known once the converter
// has given it (in the sweep producers to consumers); until then it's unknown, not its declared type.
bool VCLG::Elaborator::IsConverted(NodeIndex index) const {
    const ElaboratedGraph::Node& node = result.nodes[index];
    return node.kind == VCLG::Node::NodeKind::SubgraphInput && node.inputs[0].edge && node.inputs[0].edge->converter != nullptr;
}

bool VCLG::Elaborator::Check() {
    for (NodeIndex index = 0; index < result.nodes.size(); ++index) {
        ElaboratedGraph::Node& node = result.nodes[index];
        for (ElaboratedGraph::Input& input : node.inputs) {
            if (!input.edge)
                continue;
            const ElaboratedGraph::Output& source = result.nodes[input.edge->node].outputs[input.edge->output];
            VCL::Type* sourceType = ElaboratedGraph::TypeOf(source);
            if (input.edge->converter != nullptr) {
                if (!input.edge->converter->Convertible(sourceType, input.declaredType))
                    return Fail(index, node.scope, "input `" + input.name + "` can't take `" + TypeName(sourceType) + "`");
                continue;
            }
            // Concrete ports were checked when connected; this copy can still differ from the
            // declared type when its value comes from a Subgraph Input given another type.
            if (!input.isDependent && !source.isDependent && source.type != nullptr
                    && !VCL::Type::IsCanonicallyEqual(source.type, input.declaredType))
                return Fail(index, node.scope, "input `" + input.name + "` can't take `" + TypeName(sourceType) + "`");
        }
    }

    // Concrete ports have their declared type (unless a converter or a Subgraph Input gave them
    // another one, above).
    for (ElaboratedGraph::Node& node : result.nodes) {
        for (ElaboratedGraph::Input& input : node.inputs)
            if (!input.isDependent && input.type == nullptr)
                input.type = input.declaredType;
        for (ElaboratedGraph::Output& output : node.outputs)
            if (!output.isDependent && output.type == nullptr)
                output.type = output.declaredType;
    }
    return true;
}

bool VCLG::Elaborator::SubstituteType(NodeIndex index, VCL::Type* baseType, VCL::Type* connectedType) {
    SubstitutionTable& table = result.nodes[index].substitutions;
    switch (baseType->GetTypeClass()) {
        case VCL::Type::TypeAliasTypeClass: {
            VCL::TypeAliasType* aliasType = (VCL::TypeAliasType*)baseType;
            VCL::TypeAliasDecl* aliasDecl = aliasType->GetDecl();
            if (!table.HasDecl(aliasDecl))
                return true;
            VCL::Type* substitutedType = table.GetTypeSubstitution(aliasDecl);
            if (substitutedType != nullptr)
                return SubstituteType(index, substitutedType, connectedType);
            table.SetTypeSubstitution(aliasDecl, connectedType);
            return true;
        }
        case VCL::Type::TemplateSpecializationTypeClass: {
            if (connectedType->GetTypeClass() != VCL::Type::TemplateSpecializationTypeClass)
                return false;
            VCL::TemplateSpecializationType* speType = (VCL::TemplateSpecializationType*)baseType;
            VCL::TemplateSpecializationType* speTypeConnected = (VCL::TemplateSpecializationType*)connectedType;
            if (speType->GetTemplateDecl() != speTypeConnected->GetTemplateDecl())
                return false;
            VCL::TemplateArgumentList* argList = speType->GetTemplateArgumentList();
            VCL::TemplateArgumentList* argListConnected = speTypeConnected->GetTemplateArgumentList();
            for (size_t i = 0; i < argList->GetCount(); ++i) {
                const VCL::TemplateArgument& arg = argList->GetArgs()[i];
                const VCL::TemplateArgument& argConnected = argListConnected->GetArgs()[i];
                if (arg.GetKind() == VCL::TemplateArgument::Type) {
                    if (argConnected.GetKind() != VCL::TemplateArgument::Type)
                        return false;
                    if (!SubstituteType(index, arg.GetType().GetType(), argConnected.GetType().GetType()))
                        return false;
                } else if (arg.GetKind() == VCL::TemplateArgument::Expression
                        && arg.GetExpr()->GetExprClass() == VCL::Expr::DeclRefExprClass) {
                    VCL::DeclRefExpr* declRefExpr = (VCL::DeclRefExpr*)arg.GetExpr();
                    VCL::ConstantScalar* scalar = nullptr;
                    if (argConnected.GetKind() == VCL::TemplateArgument::Integral) {
                        scalar = result.allocator->Allocate<VCL::ConstantScalar>(1);
                        new (scalar) VCL::ConstantScalar{ argConnected.GetIntegral() };
                    } else if (argConnected.GetKind() == VCL::TemplateArgument::Expression) {
                        VCL::ConstantValue* value = argConnected.GetExpr()->GetConstantValue();
                        if (value && value->GetConstantValueClass() == VCL::ConstantValue::ConstantScalarClass)
                            scalar = (VCL::ConstantScalar*)value;
                    }
                    if (!scalar || !SubstituteExpression(index, declRefExpr, scalar))
                        return false;
                } else {
                    std::optional<uint64_t> s1 = GetConstantScalarDataFromTemplateArgument(arg);
                    std::optional<uint64_t> s2 = GetConstantScalarDataFromTemplateArgument(argConnected);
                    if (!s1.has_value() || !s2.has_value() || s1.value() != s2.value())
                        return false;
                }
            }
            return true;
        }
        default:
            return VCL::Type::IsCanonicallyEqual(baseType, connectedType);
    }
}

bool VCLG::Elaborator::SubstituteExpression(NodeIndex index, VCL::DeclRefExpr* baseExpr, VCL::ConstantScalar* scalar) {
    SubstitutionTable& table = result.nodes[index].substitutions;
    if (baseExpr->GetValueDecl()->GetDeclClass() != VCL::Decl::VarDeclClass)
        return true;
    VCL::VarDecl* varDecl = (VCL::VarDecl*)baseExpr->GetValueDecl();
    if (!table.HasDecl(varDecl)) {
        if (baseExpr->GetConstantValue() == nullptr
                || baseExpr->GetConstantValue()->GetConstantValueClass() != VCL::ConstantValue::ConstantScalarClass) {
            return false;
        }
        return memcmp(scalar->Data(), ((VCL::ConstantScalar*)baseExpr->GetConstantValue())->Data(), sizeof(uint8_t) * 8) == 0;
    }
    VCL::ConstantScalar* substitutedScalar = table.GetScalarSubstitution(varDecl);
    if (substitutedScalar != nullptr)
        return memcmp(substitutedScalar->Data(), scalar->Data(), sizeof(uint8_t) * 8) == 0;
    table.SetScalarSubstitution(varDecl, scalar);
    return true;
}

// A substituted type is a new specialization, with its arguments as substituted (e.g. an alias, or a
// constant in place of a named one), which nothing instantiates: it has no canonical form, and code
// calling GetCanonicalType on a port type (the editor, converters) gets null. Its canonical
// specialization usually is instantiated, by the node that declared the connected port: take that
// instantiation, as Sema does for a specialization written with non-canonical arguments. Otherwise
// the type stays without one (the node's own compile instantiates its ports).
static void AdoptCanonicalInstantiation(VCL::ASTContext& context, VCL::TemplateSpecializationType* type) {
    llvm::SmallVector<VCL::TemplateArgument> args{};
    for (const VCL::TemplateArgument& arg : type->GetTemplateArgumentList()->GetArgs()) {
        switch (arg.GetKind()) {
            case VCL::TemplateArgument::Type: {
                VCL::Type* canonical = VCL::Type::GetCanonicalType(arg.GetType().GetType());
                if (canonical == nullptr)
                    return;
                args.push_back(VCL::TemplateArgument{ VCL::QualType{ canonical, arg.GetType().GetQualifiers() } });
                break;
            }
            case VCL::TemplateArgument::Expression: {
                VCL::ConstantValue* value = arg.GetExpr()->GetConstantValue();
                if (value == nullptr || value->GetConstantValueClass() != VCL::ConstantValue::ConstantScalarClass)
                    return;
                args.push_back(VCL::TemplateArgument{ *(VCL::ConstantScalar*)value });
                break;
            }
            default:
                args.push_back(arg);
                break;
        }
    }
    VCL::TemplateArgumentList* canonicalArgs =
        VCL::TemplateArgumentList::Create(context, args, type->GetTemplateArgumentList()->GetSourceRange());
    VCL::TemplateSpecializationType* canonical =
        context.GetTypeCache().GetOrCreateTemplateSpecializationType(type->GetTemplateDecl(), canonicalArgs);
    if (canonical->GetInstantiatedType() != nullptr)
        type->SetInstantiatedType(canonical->GetInstantiatedType());
}

VCL::Type* VCLG::Elaborator::GenerateSubstitutedType(NodeIndex index, VCL::Type* baseType) {
    const SubstitutionTable& table = result.nodes[index].substitutions;
    VCL::ASTContext& globalASTContext = context.GetGlobalASTContext();
    switch (baseType->GetTypeClass()) {
        case VCL::Type::TypeAliasTypeClass: {
            VCL::TypeAliasDecl* aliasDecl = ((VCL::TypeAliasType*)baseType)->GetDecl();
            if (!table.HasDecl(aliasDecl))
                return baseType;
            return table.GetTypeSubstitution(aliasDecl);
        }
        case VCL::Type::TemplateSpecializationTypeClass: {
            VCL::TemplateSpecializationType* speType = (VCL::TemplateSpecializationType*)baseType;
            VCL::TemplateArgumentList* argList = speType->GetTemplateArgumentList();
            llvm::SmallVector<VCL::TemplateArgument> substitutedArgs{};
            for (size_t i = 0; i < argList->GetCount(); ++i) {
                const VCL::TemplateArgument& arg = argList->GetArgs()[i];
                if (arg.GetKind() == VCL::TemplateArgument::Type) {
                    VCL::Type* newType = GenerateSubstitutedType(index, arg.GetType().GetType());
                    if (!newType)
                        return nullptr;
                    substitutedArgs.push_back(VCL::TemplateArgument{ newType });
                } else if (arg.GetKind() == VCL::TemplateArgument::Expression) {
                    if (arg.GetExpr()->GetExprClass() != VCL::Expr::DeclRefExprClass) {
                        substitutedArgs.push_back(arg);
                        continue;
                    }
                    VCL::DeclRefExpr* declRefExpr = (VCL::DeclRefExpr*)arg.GetExpr();
                    if (declRefExpr->GetValueDecl()->GetDeclClass() != VCL::Decl::VarDeclClass) {
                        substitutedArgs.push_back(arg);
                        continue;
                    }
                    VCL::VarDecl* decl = (VCL::VarDecl*)declRefExpr->GetValueDecl();
                    if (!table.HasDecl(decl)) {
                        substitutedArgs.push_back(arg);
                        continue;
                    }
                    VCL::ConstantScalar* scalar = table.GetScalarSubstitution(decl);
                    if (!scalar)
                        return nullptr;
                    substitutedArgs.push_back(VCL::TemplateArgument{ *scalar });
                } else {
                    substitutedArgs.push_back(arg);
                }
            }
            VCL::TemplateArgumentList* substitutedArgList =
                VCL::TemplateArgumentList::Create(globalASTContext, substitutedArgs, argList->GetSourceRange());
            VCL::TemplateSpecializationType* substitutedType =
                globalASTContext.GetTypeCache().GetOrCreateTemplateSpecializationType(speType->GetTemplateDecl(), substitutedArgList);
            if (substitutedType->GetInstantiatedType() == nullptr)
                AdoptCanonicalInstantiation(globalASTContext, substitutedType);
            return substitutedType;
        }
        default:
            return baseType;
    }
}

std::optional<uint64_t> VCLG::Elaborator::GetConstantScalarDataFromTemplateArgument(const VCL::TemplateArgument& arg) {
    switch (arg.GetKind()) {
        case VCL::TemplateArgument::Expression: {
            if (arg.GetExpr()->GetConstantValue() == nullptr)
                return {};
            if (arg.GetExpr()->GetConstantValue()->GetConstantValueClass() != VCL::ConstantValue::ConstantScalarClass)
                return {};
            return ((VCL::ConstantScalar*)arg.GetExpr()->GetConstantValue())->Get<uint64_t>();
        }
        case VCL::TemplateArgument::Integral:
            return arg.GetIntegral().Get<uint64_t>();
        default:
            return {};
    }
}

//
// Order
//

bool VCLG::Elaborator::TopologicalOrder(llvm::ArrayRef<NodeIndex> roots, std::vector<NodeIndex>& order) {
    enum class VisitState : uint8_t { Unvisited, InProgress, Done };
    std::vector<VisitState> states(result.nodes.size(), VisitState::Unvisited);

    // Depth-first, post-order: each node is appended once the nodes feeding its inputs are. A
    // Feedback Input shares its value with the Feedback Outputs reading it, which must run first;
    // Feedback Outputs have no inputs, so that edge can never be part of a cycle.
    std::function<bool(NodeIndex)> visit = [&](NodeIndex index) -> bool {
        if (states[index] == VisitState::Done)
            return true;
        if (states[index] == VisitState::InProgress)
            return false;
        states[index] = VisitState::InProgress;

        for (const ElaboratedGraph::Input& input : result.nodes[index].inputs)
            if (input.edge && !visit(input.edge->node))
                return false;
        if (auto it = feedbackReaders.find(index); it != feedbackReaders.end())
            for (NodeIndex reader : it->second)
                if (!visit(reader))
                    return false;

        states[index] = VisitState::Done;
        order.push_back(index);
        return true;
    };

    order.clear();
    for (NodeIndex root : roots)
        if (!visit(root))
            return false;
    return true;
}

void VCLG::Elaborator::BuildExecutionOrder() {
    // A subgraph use runs when one of its nodes is needed by what runs outside it (only reachable
    // through its Subgraph Outputs); then all of its own output nodes run as well.
    std::vector<bool> reached(result.nodes.size(), false);
    std::vector<bool> scopeRuns(result.scopes.size(), false);
    std::vector<NodeIndex> toVisit{ scopeOutputNodes[0].rbegin(), scopeOutputNodes[0].rend() };
    scopeRuns[0] = true;

    while (!toVisit.empty()) {
        NodeIndex index = toVisit.back();
        toVisit.pop_back();
        if (reached[index])
            continue;
        reached[index] = true;

        const ElaboratedGraph::Node& node = result.nodes[index];
        if (!scopeRuns[node.scope]) {
            scopeRuns[node.scope] = true;
            toVisit.insert(toVisit.end(), scopeOutputNodes[node.scope].rbegin(), scopeOutputNodes[node.scope].rend());
        }
        for (const ElaboratedGraph::Input& input : node.inputs)
            if (input.edge)
                toVisit.push_back(input.edge->node);
        if (auto it = feedbackReaders.find(index); it != feedbackReaders.end())
            toVisit.insert(toVisit.end(), it->second.begin(), it->second.end());
    }

    std::vector<NodeIndex> roots{};
    for (NodeIndex i = 0; i < result.nodes.size(); ++i)
        if (reached[i])
            roots.push_back(i);
    // Can't fail: the whole graph was ordered by Infer.
    TopologicalOrder(roots, result.executionOrder);
}

bool VCLG::Elaborator::Fail(NodeIndex node, ScopeIndex scope, std::string message) {
    if (!result.error)
        result.error = ElaboratedGraph::Error{ node, scope, std::move(message) };
    return false;
}

std::string VCLG::Elaborator::TypeName(VCL::Type* type) {
    if (!type)
        return "?";
    return VCL::TypePrinter::Print(type);
}
