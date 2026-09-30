#pragma once

#include <VCLG/Core/IdentityProvider.hpp>
#include <VCLG/Graph/Definition.hpp>
#include <VCLG/Graph/Converter.hpp>

#include <VCL/Frontend/CompilerContext.hpp>

#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/IR/Type.h>

#include <memory>


namespace VCLG {
    class GraphInstance;
    class VariantCache;

    class GraphContext {
    public:
        GraphContext() = delete;
        GraphContext(std::shared_ptr<VCL::CompilerInvocation> invocation);
        GraphContext(const GraphContext& other) = delete;
        GraphContext(GraphContext&& other) = delete;
        ~GraphContext();

        GraphContext& operator=(const GraphContext& other) = delete;
        GraphContext& operator=(GraphContext&& other) = delete;

        inline VCL::CompilerContext& GetCompilerContext() { return cc; }
        inline VCLG::DefinitionRegistry& GetDefinitionRegistry() { return *definitionRegistry; }
        inline VCL::ASTContext& GetGlobalASTContext() { return *globalASTContext; }
        inline std::vector<Converter*>& GetConverters() { return converters; }
        /** The compiled variants of source nodes, kept between compiles (`state-as-data.md` §7.3). */
        inline VariantCache& GetVariantCache() { return *variantCache; }

        void AddConverter(Converter* converter);

        /**
         * The LLVM type VCL emits for `type`, in this context's LLVM context and for its target
         * (converters use it to know what they read and write). Null, with an error reported, if
         * the type can't be emitted.
         */
        llvm::Type* ConvertType(VCL::Type* type);

        std::shared_ptr<GraphInstance> CreateInstance();

    private:
        VCL::CompilerContext cc;
        llvm::IntrusiveRefCntPtr<VCLG::DefinitionRegistry> definitionRegistry;

        llvm::IntrusiveRefCntPtr<VCL::ASTContext> globalASTContext;

        std::vector<Converter*> converters;

        IdentityProvider identityProvider;

        // Its modules live in the compiler context's LLVM context: destroyed before it.
        std::unique_ptr<VariantCache> variantCache;

        // A code generator on a module of its own, only used for its type conversion; created on
        // first use. Last: it refers to the compiler context and the global AST context.
        struct TypeConverter;
        std::unique_ptr<TypeConverter> typeConverter;
    };

}