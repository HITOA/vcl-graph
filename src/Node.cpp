#include <VCLG/Node.hpp>

#include <VCL/Parser.hpp>

#include "NodeMetadata.hpp"


VCLG::Node::Node(std::shared_ptr<VCL::Source> source, std::shared_ptr<VCL::Logger> logger) : logger{ logger } {
    UpdateSource(source);
}

void VCLG::Node::UpdateSource(std::shared_ptr<VCL::Source> source) {
    this->source = source;
}

void VCLG::Node::Reset() {
    std::unique_ptr<VCL::Parser> parser = VCL::Parser::Create(logger);
    program = parser->Parse(source);

    NodeMetadata visitor{};
    visitor.Process(this);

    if (inputs.empty() && outputs.empty())
        throw std::runtime_error{ "Node doesn't have any inputs nor outputs port." };

    if (entrypoint == nullptr)
        throw std::runtime_error{ "Node doesn't have any entry point." };
}

std::unique_ptr<VCL::ASTProgram> VCLG::Node::MoveProgram() {
    return std::move(program);
}

const std::vector<std::shared_ptr<VCLG::Port>>& VCLG::Node::GetInputs() {
    return inputs;
}

const std::vector<std::shared_ptr<VCLG::Port>>& VCLG::Node::GetOutputs() {
    return outputs;
}

VCL::ASTFunctionDeclaration* VCLG::Node::GetEntrypoint() {
    return entrypoint;
}