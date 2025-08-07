#pragma once

#include <VCLG/Port.hpp>

#include <VCL/Source.hpp>
#include <VCL/AST.hpp>
#include <VCL/Logger.hpp>
#include <VCL/Meta.hpp>


namespace VCLG {

    class Node {
    public:
        Node(std::shared_ptr<VCL::Source> source, std::shared_ptr<VCL::Logger> logger = nullptr);

        void UpdateSource(std::shared_ptr<VCL::Source> source);
        void Reset(std::shared_ptr<VCL::DirectiveRegistry> registry = nullptr, bool compile = false);
        std::unique_ptr<VCL::ASTProgram> MoveProgram();

        const std::vector<std::shared_ptr<Port>>& GetInputs();
        const std::vector<std::shared_ptr<Port>>& GetOutputs();
        VCL::ASTFunctionDeclaration* GetEntrypoint();

        virtual void OnMetaState(std::shared_ptr<VCL::MetaState> state) {};
        
    private:
        std::shared_ptr<VCL::Source> source;
        std::shared_ptr<VCL::Logger> logger;
        std::unique_ptr<VCL::ASTProgram> program;

        std::vector<std::shared_ptr<Port>> inputs;
        std::vector<std::shared_ptr<Port>> outputs;

        VCL::ASTFunctionDeclaration* entrypoint;

        friend class NodeMetadata;
        friend class NodeProcessor;
    };

}