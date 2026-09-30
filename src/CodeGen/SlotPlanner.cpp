#include <VCLG/CodeGen/SlotPlanner.hpp>

#include <VCLG/Graph/Definition.hpp>

#include <VCL/AST/TypePrinter.hpp>

#include <llvm/Support/MathExtras.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/xxhash.h>

#include <algorithm>
#include <map>
#include <sstream>


namespace {

    using VCLG::ElaboratedGraph;
    using VCLG::GraphLayout;
    using VCLG::SlotPlan;
    using NodeIndex = ElaboratedGraph::NodeIndex;

    class Planner {
    public:
        Planner(const ElaboratedGraph& graph, VCLG::FrameKind frame, VCLG::NodeInterfaceFunction interfaces,
                VCLG::TypeLayoutFunction typeLayout, uint64_t minimumAlignment, const VCLG::SlotPlannerOptions& options) :
                graph{ graph }, interfaces{ std::move(interfaces) }, typeLayout{ std::move(typeLayout) },
                minimumAlignment{ std::max<uint64_t>(minimumAlignment, 1) }, options{ options } {
            plan.frame = frame;
        }

        std::optional<SlotPlan> Run(std::string& error, NodeIndex& errorNode) {
            // Instance frames address their host slots from a base pointer (S2, plan P11).
            if (plan.frame != VCLG::FrameKind::Root)
                return Fail(ElaboratedGraph::Invalid, "instance frames are not supported yet", error, errorNode);

            llvm::ArrayRef<NodeIndex> order = graph.GetExecutionOrder();
            plan.nodes.resize(graph.GetNodes().size());
            for (uint32_t position = 0; position < order.size(); ++position) {
                NodeIndex index = order[position];
                const ElaboratedGraph::Node& node = graph.GetNode(index);
                std::optional<std::string> message{};
                switch (node.kind) {
                    case VCLG::Node::NodeKind::Source: message = PlanSourceNode(index, position); break;
                    case VCLG::Node::NodeKind::SubgraphInput:
                    case VCLG::Node::NodeKind::SubgraphOutput: message = PlanCopyNode(index, position); break;
                    case VCLG::Node::NodeKind::FeedbackOutput: message = PlanFeedbackOutput(index); break;
                    case VCLG::Node::NodeKind::FeedbackInput: message = PlanFeedbackInput(index, (uint32_t)order.size()); break;
                    case VCLG::Node::NodeKind::Subgraph: message = "subgraph uses are flattened by elaboration"; break;
                }
                if (message)
                    return Fail(index, *message, error, errorNode);
            }

            uint64_t offset = 0;
            for (GraphLayout::Region& region : plan.layout.regions) {
                offset = llvm::alignTo(offset, region.alignment);
                region.offset = offset;
                // An empty region still gets its own address: distinct symbols are distinct objects.
                offset += std::max<uint64_t>(region.size, 1);
                plan.layout.state.alignment = std::max(plan.layout.state.alignment, region.alignment);
            }
            plan.layout.state.size = llvm::alignTo(offset, plan.layout.state.alignment);
            return std::move(plan);
        }

    private:
        std::optional<SlotPlan> Fail(NodeIndex node, const std::string& message, std::string& error, NodeIndex& errorNode) {
            error = message;
            errorNode = node;
            return std::nullopt;
        }

        uint32_t AddSlot(SlotPlan::Slot slot) {
            plan.slots.push_back(std::move(slot));
            return (uint32_t)plan.slots.size() - 1;
        }

        uint32_t AddRegion(const std::string& key, GraphLayout::RegionKind kind, const std::string& symbol, uint64_t size, uint64_t alignment,
                uint64_t signature) {
            GraphLayout::Region region{};
            region.key = key;
            region.kind = kind;
            region.size = size;
            region.alignment = std::max(alignment, minimumAlignment);
            region.symbol = symbol;
            region.signature = signature;
            plan.layout.regions.push_back(std::move(region));
            return (uint32_t)plan.layout.regions.size() - 1;
        }

        uint32_t AddHostSlot(SlotPlan::SlotClass slotClass, VCL::Type* type, const std::string& name, GraphLayout::RegionKind kind,
                const std::string& key, const std::string& symbol, uint64_t size, uint64_t alignment, uint64_t signature) {
            SlotPlan::Slot slot{};
            slot.slotClass = slotClass;
            slot.type = type;
            slot.size = size;
            slot.alignment = std::max(alignment, minimumAlignment);
            slot.name = name;
            slot.region = AddRegion(key, kind, symbol, size, alignment, signature);
            return AddSlot(std::move(slot));
        }

        // Every graph the nodes of `scope` are copied from, with its generation, up to the root:
        // a path only names the same node within these generations (§6.1).
        std::string Lineage(ElaboratedGraph::ScopeIndex scope) const {
            std::string text{};
            for (; scope != ElaboratedGraph::Invalid; scope = graph.GetScopes()[scope].parent) {
                const ElaboratedGraph::Scope& s = graph.GetScopes()[scope];
                text += "g" + std::to_string(s.graph) + "@" + std::to_string(s.generation) + ";";
            }
            return text;
        }

        // The signature of a region (§6.3): a hash of what gives its bytes their meaning.
        static uint64_t Signature(const std::string& text) {
            return llvm::xxh3_64bits(text);
        }

        uint64_t StateSignature(const ElaboratedGraph::Node& node, const VCLG::NodeInterface& interface) const {
            std::string text = "state " + Lineage(node.scope) + " source " + std::to_string(interface.sourceHash)
                + " size " + std::to_string(interface.stateSize) + " align " + std::to_string(interface.stateAlignment);
            for (const VCLG::NodeInterface::StateField& field : interface.stateFields)
                text += " " + std::to_string(field.offset) + "+" + std::to_string(field.size);
            return Signature(text);
        }

        uint64_t PortSignature(const char* kind, const ElaboratedGraph::Node& node, const VCLG::NodeInterface* interface, VCL::Type* type,
                uint64_t size, uint64_t alignment) const {
            std::string text = std::string{ kind } + " " + Lineage(node.scope);
            if (interface != nullptr)
                text += " source " + std::to_string(interface->sourceHash);
            text += " type " + (type ? VCL::TypePrinter::Print(type) : std::string{ "?" })
                + " size " + std::to_string(size) + " align " + std::to_string(alignment);
            return Signature(text);
        }

        std::optional<uint32_t> AddTemporary(VCL::Type* type, const std::string& name, uint32_t position, bool isOutput) {
            std::optional<std::pair<uint64_t, uint64_t>> layout = typeLayout(type);
            if (!layout)
                return std::nullopt;
            SlotPlan::Slot slot{};
            slot.slotClass = SlotPlan::SlotClass::Temporary;
            slot.type = type;
            slot.size = layout->first;
            slot.alignment = std::max(layout->second, minimumAlignment);
            slot.name = name;
            slot.first = position;
            slot.last = position;
            slot.isOutput = isOutput;
            return AddSlot(std::move(slot));
        }

        std::optional<uint32_t> AddConstant(VCL::Type* type, const std::string& name, const std::optional<VCL::ConstantScalar>& value,
                const std::string& defaultValue) {
            std::optional<std::pair<uint64_t, uint64_t>> layout = typeLayout(type);
            if (!layout)
                return std::nullopt;
            SlotPlan::Slot slot{};
            slot.slotClass = SlotPlan::SlotClass::Constant;
            slot.type = type;
            slot.size = layout->first;
            slot.alignment = layout->second;
            slot.name = name;
            slot.value = value;
            slot.defaultValue = value ? std::string{} : defaultValue;
            return AddSlot(std::move(slot));
        }

        // A temporary read at `position` lives until then.
        void Use(uint32_t slot, uint32_t position) {
            SlotPlan::Slot& s = plan.slots[slot];
            if (s.slotClass == SlotPlan::SlotClass::Temporary)
                s.last = std::max(s.last, position);
        }

        // The slot of what `edge` connects: its producer's output. Plain connections only.
        uint32_t Source(const ElaboratedGraph::Edge& edge) const {
            return plan.nodes[edge.node].outputs[edge.output];
        }

        std::optional<std::string> PlanSourceNode(NodeIndex index, uint32_t position) {
            const ElaboratedGraph::Node& node = graph.GetNode(index);
            const VCLG::NodeInterface* interface = interfaces(index);
            if (interface == nullptr || node.definition == nullptr)
                return "source node was not translated";
            llvm::ArrayRef<VCLG::SourcePortDefinition> ports = node.definition->GetPorts();
            if (ports.size() != node.inputs.size() + node.outputs.size() || interface->ports.size() != ports.size())
                return "node ports don't match its definition";
            SlotPlan::NodeSlots& slots = plan.nodes[index];

            // S1: always, for state (§5.3).
            slots.state = AddHostSlot(SlotPlan::SlotClass::HostState, nullptr, node.path, GraphLayout::RegionKind::State,
                GraphLayout::StateKey(node.path), node.path + "#state", interface->stateSize, interface->stateAlignment,
                StateSignature(node, *interface));

            for (uint32_t i = 0; i < node.inputs.size(); ++i) {
                const ElaboratedGraph::Input& input = node.inputs[i];
                std::string name = node.path + "." + ports[i].GetName();
                if (!input.edge) {
                    // S6.
                    std::optional<uint32_t> slot = AddConstant(ElaboratedGraph::TypeOf(input), name, input.initializer,
                        interface->ports[i].defaultValue);
                    if (!slot)
                        return "can't lay out input '" + ports[i].GetName() + "'";
                    slots.inputs.push_back(*slot);
                } else if (input.edge->converter != nullptr) {
                    // S4, filled by the converter right before the call.
                    std::optional<uint32_t> slot = AddTemporary(ElaboratedGraph::TypeOf(input), name, position, false);
                    if (!slot)
                        return "can't lay out input '" + ports[i].GetName() + "'";
                    Use(Source(*input.edge), position);
                    slots.inputs.push_back(*slot);
                } else {
                    uint32_t slot = Source(*input.edge);
                    Use(slot, position);
                    slots.inputs.push_back(slot);
                }
            }

            for (uint32_t o = 0; o < node.outputs.size(); ++o) {
                uint32_t port = (uint32_t)node.inputs.size() + o;
                const std::string& outputName = ports[port].GetName();
                std::string key = GraphLayout::OutputKey(node.path, outputName);
                bool observed = options.observeAllOutputs || options.observedOutputs.contains(key);
                VCL::Type* type = ElaboratedGraph::TypeOf(node.outputs[o]);
                if (!observed && interface->IsAlwaysWritten(port)) {
                    // S4: nobody needs the value after its readers ran.
                    std::optional<uint32_t> slot = AddTemporary(type, key, position, true);
                    if (!slot)
                        return "can't lay out output '" + outputName + "'";
                    slots.outputs.push_back(*slot);
                } else {
                    // S3: it must persist, or the host reads it.
                    const VCLG::NodeInterface::Port& layout = interface->ports[port];
                    slots.outputs.push_back(AddHostSlot(SlotPlan::SlotClass::HostPort, type, key, GraphLayout::RegionKind::Output,
                        key, key + "#output", layout.size, layout.alignment,
                        PortSignature("output", node, interface, type, layout.size, layout.alignment)));
                }
            }
            return std::nullopt;
        }

        // A Subgraph Input or Output: its one output is the value of its one input.
        std::optional<std::string> PlanCopyNode(NodeIndex index, uint32_t position) {
            const ElaboratedGraph::Node& node = graph.GetNode(index);
            SlotPlan::NodeSlots& slots = plan.nodes[index];
            const ElaboratedGraph::Input& input = node.inputs[0];
            std::string name = node.path + "." + node.displayName;
            if (input.edge && input.edge->converter == nullptr) {
                uint32_t slot = Source(*input.edge);
                Use(slot, position);
                slots.inputs.push_back(slot);
                slots.outputs.push_back(slot);
                return std::nullopt;
            }
            if (!input.edge && node.kind == VCLG::Node::NodeKind::SubgraphOutput)
                return "subgraph output is not connected";

            std::optional<uint32_t> slot{};
            if (input.edge) {
                slot = AddTemporary(ElaboratedGraph::TypeOf(input), name, position, false);
                if (slot)
                    Use(Source(*input.edge), position);
            } else {
                slot = AddConstant(ElaboratedGraph::TypeOf(input), name, input.initializer, {});
            }
            if (!slot)
                return "can't lay out '" + node.displayName + "'";
            slots.inputs.push_back(*slot);
            slots.outputs.push_back(*slot);
            return std::nullopt;
        }

        std::optional<uint32_t> FeedbackRegion(NodeIndex feedbackInput) {
            if (auto it = feedbackSlots.find(feedbackInput); it != feedbackSlots.end())
                return it->second;
            const ElaboratedGraph::Node& node = graph.GetNode(feedbackInput);
            VCL::Type* type = ElaboratedGraph::TypeOf(node.inputs[0]);
            std::optional<std::pair<uint64_t, uint64_t>> layout = typeLayout(type);
            if (!layout)
                return std::nullopt;
            std::string key = GraphLayout::FeedbackKey(node.path);
            uint32_t slot = AddHostSlot(SlotPlan::SlotClass::Feedback, type, key, GraphLayout::RegionKind::Feedback, key, key,
                layout->first, layout->second, PortSignature("feedback", node, nullptr, type, layout->first, layout->second));
            feedbackSlots[feedbackInput] = slot;
            return slot;
        }

        std::optional<std::string> PlanFeedbackOutput(NodeIndex index) {
            const ElaboratedGraph::Node& node = graph.GetNode(index);
            if (node.feedbackInput == ElaboratedGraph::Invalid)
                return "feedback output is not linked to a feedback input";
            if (node.outputs.size() != 1)
                return "feedback output has no output";
            // S5: readers see the value the Feedback Input's source had at the end of the previous call.
            std::optional<uint32_t> slot = FeedbackRegion(node.feedbackInput);
            if (!slot)
                return "can't lay out the feedback value";
            plan.nodes[index].outputs.push_back(*slot);
            return std::nullopt;
        }

        std::optional<std::string> PlanFeedbackInput(NodeIndex index, uint32_t end) {
            const ElaboratedGraph::Node& node = graph.GetNode(index);
            const ElaboratedGraph::Input& input = node.inputs[0];
            // Readers run first (the execution order puts a Feedback Input after them).
            auto it = feedbackSlots.find(index);
            if (it == feedbackSlots.end())
                return "feedback is never read: no feedback output is linked to it";
            if (!input.edge)
                return "feedback input is not connected";
            uint32_t from = Source(*input.edge);
            // The copy is at the end of the frame, after every reader of the previous value (§5.6).
            Use(from, end);
            plan.nodes[index].inputs.push_back(from);
            plan.feedbackCopies.push_back({ index, from, it->second });
            return std::nullopt;
        }

        const ElaboratedGraph& graph;
        VCLG::NodeInterfaceFunction interfaces;
        VCLG::TypeLayoutFunction typeLayout;
        uint64_t minimumAlignment;
        const VCLG::SlotPlannerOptions& options;
        SlotPlan plan{};
        std::map<NodeIndex, uint32_t> feedbackSlots{};
    };

    std::string PrintScalar(const VCL::ConstantScalar& value) {
        using Kind = VCL::BuiltinType::Kind;
        std::ostringstream text{};
        switch (value.GetKind()) {
            case Kind::Bool: text << (value.Get<bool>() ? "true" : "false"); break;
            case Kind::Float32: text << value.Get<float>(); break;
            case Kind::Float64: text << value.Get<double>(); break;
            case Kind::Int8: text << (int)value.Get<int8_t>(); break;
            case Kind::Int16: text << value.Get<int16_t>(); break;
            case Kind::Int32: text << value.Get<int32_t>(); break;
            case Kind::Int64: text << value.Get<int64_t>(); break;
            case Kind::UInt8: text << (unsigned)value.Get<uint8_t>(); break;
            case Kind::UInt16: text << value.Get<uint16_t>(); break;
            case Kind::UInt32: text << value.Get<uint32_t>(); break;
            case Kind::UInt64: text << value.Get<uint64_t>(); break;
            default: text << "?"; break;
        }
        return text.str();
    }

    std::string PrintSlot(const ElaboratedGraph& graph, const SlotPlan& plan, uint32_t index) {
        const SlotPlan::Slot& slot = plan.slots[index];
        std::ostringstream text{};
        text << "#" << index << " ";
        std::string type = slot.type ? VCL::TypePrinter::Print(slot.type) + " " : std::string{};
        switch (slot.slotClass) {
            case SlotPlan::SlotClass::HostState: text << "S1 host state "; break;
            case SlotPlan::SlotClass::HostPort: text << "S3 host port " << type; break;
            case SlotPlan::SlotClass::Temporary: text << "S4 temporary " << type; break;
            case SlotPlan::SlotClass::Feedback: text << "S5 feedback " << type; break;
            case SlotPlan::SlotClass::Constant: text << "S6 constant " << type; break;
        }
        text << slot.name;
        if (slot.region != SlotPlan::None) {
            const GraphLayout::Region& region = plan.layout.regions[slot.region];
            text << ": " << region.symbol << " [" << region.offset << ", " << region.offset + region.size << ")";
        } else if (slot.slotClass == SlotPlan::SlotClass::Temporary) {
            text << ": calls " << slot.first << ".." << slot.last;
        } else if (slot.value) {
            text << " = " << PrintScalar(*slot.value);
        } else if (!slot.defaultValue.empty()) {
            text << " = " << slot.defaultValue;
        } else {
            text << " = 0";
        }
        return text.str();
    }

}

std::optional<VCLG::SlotPlan> VCLG::PlanSlots(const ElaboratedGraph& graph, FrameKind frame, NodeInterfaceFunction interfaces,
        TypeLayoutFunction typeLayout, uint64_t minimumAlignment, const SlotPlannerOptions& options,
        std::string& error, ElaboratedGraph::NodeIndex& errorNode) {
    Planner planner{ graph, frame, std::move(interfaces), std::move(typeLayout), minimumAlignment, options };
    return planner.Run(error, errorNode);
}

std::string VCLG::PrintPlan(const ElaboratedGraph& graph, const SlotPlan& plan) {
    std::ostringstream text{};
    for (NodeIndex index : graph.GetExecutionOrder()) {
        const ElaboratedGraph::Node& node = graph.GetNode(index);
        const SlotPlan::NodeSlots& slots = plan.nodes[index];
        std::string name = node.definition != nullptr ? node.definition->GetDisplayName().str() : node.displayName;
        if (name.empty())
            name = llvm::sys::path::stem(node.source).str();
        text << node.path << " " << name << "\n";
        auto portName = [&](uint32_t port, bool isInput) -> std::string {
            if (node.definition != nullptr)
                return node.definition->GetPorts()[port].GetName();
            return isInput ? node.inputs[port].name : node.outputs[port - node.inputs.size()].name;
        };
        if (slots.state != SlotPlan::None)
            text << "  state: #" << slots.state << "\n";
        for (uint32_t i = 0; i < slots.inputs.size(); ++i)
            text << "  in " << portName(i, true) << ": #" << slots.inputs[i] << "\n";
        for (uint32_t o = 0; o < slots.outputs.size(); ++o)
            text << "  out " << portName((uint32_t)node.inputs.size() + o, false) << ": #" << slots.outputs[o] << "\n";
    }
    for (const SlotPlan::FeedbackCopy& copy : plan.feedbackCopies)
        text << "end: #" << copy.to << " <- #" << copy.from << "\n";
    for (uint32_t i = 0; i < plan.slots.size(); ++i)
        text << PrintSlot(graph, plan, i) << "\n";
    text << "state block: " << plan.layout.state.size << " bytes, aligned to " << plan.layout.state.alignment << "\n";
    return text.str();
}
