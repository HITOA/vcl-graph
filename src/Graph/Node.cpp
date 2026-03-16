#include <VCLG/Graph/Node.hpp>



llvm::ArrayRef<VCLG::Port*> VCLG::Node::GetNodeInputs(Node* node) {
    switch (node->GetNodeClass()) {
        case Node::SourceNodeClass:
            return ((SourceNode*)node)->GetInputs();
        default:
            return {};
    }
}

llvm::ArrayRef<VCLG::Port*> VCLG::Node::GetNodeOutputs(Node* node) {
    switch (node->GetNodeClass()) {
        case Node::SourceNodeClass:
            return ((SourceNode*)node)->GetOutputs();
        default:
            return {};
    }
}
