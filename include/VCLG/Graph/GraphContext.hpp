#pragma once

#include <VCLG/Core/Allocator.hpp>
#include <VCLG/Graph/Definition.hpp>
#include <VCLG/Graph/GraphUserDataTailAllocator.hpp>
#include <VCLG/Graph/Converter.hpp>

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
        inline VCL::ASTContext& GetGlobalASTContext() { return *globalASTContext; }
        inline std::vector<Converter*>& GetConverters() { return converters; }

        void AddConverter(Converter* converter);

        std::shared_ptr<GraphInstance> CreateInstance(
            std::shared_ptr<GraphUserDataTailAllocator> userDataTailAllocator = std::make_shared<GraphUserDataTailAllocator>(),
            std::unique_ptr<Allocator> allocator = std::make_unique<TLSFAllocator>());

    private:
        VCL::CompilerContext cc;
        llvm::IntrusiveRefCntPtr<VCLG::DefinitionRegistry> definitionRegistry;

        llvm::IntrusiveRefCntPtr<VCL::ASTContext> globalASTContext;

        std::vector<Converter*> converters;
    };

}