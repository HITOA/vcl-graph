#include <VCLG/Graph/Node.hpp>



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
