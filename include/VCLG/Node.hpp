#pragma once

#include <VCLG/Port.hpp>

#include <VCL/Source.hpp>
#include <VCL/AST.hpp>
#include <VCL/Logger.hpp>


namespace VCLG {

    class Node {
    public:
        Node(std::shared_ptr<VCL::Source> source, std::shared_ptr<VCL::Logger> logger = nullptr);

        void UpdateSource(std::shared_ptr<VCL::Source> source);
        std::unique_ptr<VCL::ASTProgram> Reset();

        const std::vector<std::shared_ptr<Port>>& GetInputs();
        const std::vector<std::shared_ptr<Port>>& GetOutputs();
        
    private:
        std::shared_ptr<VCL::Source> source;
        std::shared_ptr<VCL::Logger> logger;
        std::unique_ptr<VCL::ASTProgram> program;

        std::vector<std::shared_ptr<Port>> inputs;
        std::vector<std::shared_ptr<Port>> outputs;

        friend class NodeMetadata;
        friend class NodeProcessor;
    };

}