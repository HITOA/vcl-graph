#include <VCLG/Graph/Node.hpp>

#include <VCLG/Graph/Port.hpp>
#include <VCLG/Graph/Parameter.hpp>


llvm::ArrayRef<VCLG::Port*> VCLG::Node::GetNodeInputs(Node* node) {
    switch (node->GetNodeClass()) {
        case Node::SourceNodeClass:
            return ((SourceNode*)node)->GetInputs();
        case Node::TransientNodeClass:
            return ((TransientNode*)node)->GetInputs();
        default:
            return {};
    }
}

llvm::ArrayRef<VCLG::Port*> VCLG::Node::GetNodeOutputs(Node* node) {
    switch (node->GetNodeClass()) {
        case Node::SourceNodeClass:
            return ((SourceNode*)node)->GetOutputs();
        case Node::TransientNodeClass:
            return ((TransientNode*)node)->GetOutputs();
        default:
            return {};
    }
}

void VCLG::Node::NormalizePortDisplayNameLength(Node* node) {
    llvm::ArrayRef<VCLG::Port*> inputs = GetNodeInputs(node);
    llvm::ArrayRef<VCLG::Port*> outputs = GetNodeOutputs(node);

    int longestInputDisplayName = 0;
    int longestOutputDisplayName = 0;

    for (VCLG::Port* input : inputs)
        longestInputDisplayName = std::max<int>(longestInputDisplayName, (int)input->GetDisplayName().size());

    for (VCLG::Port* output : outputs)
        longestOutputDisplayName = std::max<int>(longestOutputDisplayName, (int)output->GetDisplayName().size());

    for (VCLG::Port* input : inputs) {
        if (input->GetDisplayName().size() >= longestInputDisplayName)
            continue;
        std::string displayName = input->GetDisplayName();
        for (size_t i = displayName.size(); i < longestInputDisplayName; ++i)
            displayName += " ";
        input->SetDisplayName(displayName);
    }

    for (VCLG::Port* output : outputs) {
        if (output->GetDisplayName().size() >= longestOutputDisplayName)
            continue;
        std::string displayName = "";
        for (size_t i = displayName.size() + output->GetDisplayName().size(); i < longestOutputDisplayName; ++i)
            displayName += " ";
        displayName += output->GetDisplayName();
        output->SetDisplayName(displayName);
    }
}

void VCLG::SourceNode::NormalizePortAndParameterDisplayNameLength() {
    int longestInputDisplayName = 0;
    int longestOutputDisplayName = 0;
    int longestParameterDisplayName = 0;

    for (VCLG::Port* input : inPorts)
        longestInputDisplayName = std::max<int>(longestInputDisplayName, (int)input->GetDisplayName().size());

    for (VCLG::Port* output : outPorts)
        longestOutputDisplayName = std::max<int>(longestOutputDisplayName, (int)output->GetDisplayName().size());

    for (VCLG::Parameter* parameter : parameters)
        longestParameterDisplayName = std::max<int>(longestParameterDisplayName, (int)parameter->GetDisplayName().size());

    for (VCLG::Port* input : inPorts) {
        if (input->GetDisplayName().size() >= longestInputDisplayName)
            continue;
        std::string displayName = input->GetDisplayName();
        for (size_t i = displayName.size(); i < longestInputDisplayName; ++i)
            displayName += " ";
        input->SetDisplayName(displayName);
    }

    for (VCLG::Port* output : outPorts) {
        if (output->GetDisplayName().size() >= longestOutputDisplayName)
            continue;
        std::string displayName = "";
        for (size_t i = displayName.size() + output->GetDisplayName().size(); i < longestOutputDisplayName; ++i)
            displayName += " ";
        displayName += output->GetDisplayName();
        output->SetDisplayName(displayName);
    }

    for (VCLG::Parameter* parameter : parameters) {
        if (parameter->GetDisplayName().size() >= longestParameterDisplayName)
            continue;
        std::string displayName = parameter->GetDisplayName();
        for (size_t i = displayName.size(); i < longestParameterDisplayName; ++i)
            displayName += " ";
        parameter->SetDisplayName(displayName);
    }
}