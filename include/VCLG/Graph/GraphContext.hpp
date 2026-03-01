#pragma once

#include <VCLG/Graph/Definition.hpp>

#include <VCL/Frontend/CompilerContext.hpp>

#include <llvm/ADT/IntrusiveRefCntPtr.h>

#include <memory>


namespace VCLG {
    class GraphInstance;

    class GraphContext {
    public:
        GraphContext() = delete;
        GraphContext(std::shared_ptr<VCL::CompilerInvocation> invocation);
        GraphContext(const GraphContext& other) = delete;
        GraphContext(GraphContext&& other) = delete;
        ~GraphContext() = default;

        GraphContext& operator=(const GraphContext& other) = delete;
        GraphContext& operator=(GraphContext&& other) = delete;

        inline VCL::CompilerContext& GetCompilerContext() { return cc; }
        inline VCLG::DefinitionRegistry& GetDefinitionRegistry() { return *definitionRegistry; }

        std::shared_ptr<GraphInstance> CreateInstance();

    private:
        VCL::CompilerContext cc;
        llvm::IntrusiveRefCntPtr<VCLG::DefinitionRegistry> definitionRegistry;
    };

}