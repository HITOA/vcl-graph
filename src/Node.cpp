#include <VCLG/Node.hpp>

#include <VCL/Parser.hpp>

#include "NodeMetadata.hpp"


VCLG::Node::Node(std::shared_ptr<VCL::Source> source, std::shared_ptr<VCL::Logger> logger) : logger{ logger } {
    UpdateSource(source);
}

void VCLG::Node::UpdateSource(std::shared_ptr<VCL::Source> source) {
    this->source = source;
}

std::unique_ptr<VCL::ASTProgram> VCLG::Node::Reset() {
    inputs.clear();
    outputs.clear();

    std::unique_ptr<VCL::ASTProgram> oldProgram = std::move(program);

    std::unique_ptr<VCL::Parser> parser = VCL::Parser::Create(logger);
    program = parser->Parse(source);

    NodeMetadata visitor{};
    visitor.Process(this);

    return std::move(oldProgram);
}

const std::vector<std::shared_ptr<VCLG::Port>>& VCLG::Node::GetInputs() {
    return inputs;
}

const std::vector<std::shared_ptr<VCLG::Port>>& VCLG::Node::GetOutputs() {
    return outputs;
}