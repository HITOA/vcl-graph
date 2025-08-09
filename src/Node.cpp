#include <VCLG/Node.hpp>

#include <VCL/Parser.hpp>
#include <VCL/ExecutionSession.hpp>

#include "NodeMetadata.hpp"


VCLG::Node::Node(std::shared_ptr<VCL::Source> source, std::shared_ptr<VCL::Logger> logger) : logger{ logger } {
    UpdateSource(source);
}

void VCLG::Node::UpdateSource(std::shared_ptr<VCL::Source> source) {
    this->source = source;
}

void VCLG::Node::Reset(std::shared_ptr<VCL::DirectiveRegistry> registry, bool compile) {
    std::unique_ptr<VCL::Parser> parser = VCL::Parser::Create(logger);
    parser->SetDirectiveRegistry(registry);
    program = parser->Parse(source);
    
    // Compiling node with temporary program to verify and get metadata
    if (compile) {
        NodeMetadata visitor{};
        visitor.Process(this);

        if (inputs.empty() && outputs.empty())
            throw std::runtime_error{ "Node doesn't have any inputs nor outputs port." };

        std::unique_ptr<VCL::ExecutionSession> session = VCL::ExecutionSession::Create(logger);
        std::unique_ptr<VCL::Module> module = session->CreateModule(std::move(program));

        std::shared_ptr<VCL::MetaState> state = VCL::MetaState::Create();

        module->SetDirectiveRegistry(registry);
        module->SetMetaState(state);
        module->Emit();
        module->Verify();

        OnMetaState(state);

        program = parser->Parse(source);
    }
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