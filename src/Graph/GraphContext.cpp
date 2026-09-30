#include <VCLG/Graph/GraphContext.hpp>

#include <VCLG/Graph/GraphInstance.hpp>
#include <VCLG/Translation/VariantCache.hpp>

#include <VCL/CodeGen/CodeGenModule.hpp>
#include <VCL/Sema/ModuleTable.hpp>

#include <llvm/IR/Module.h>


struct VCLG::GraphContext::TypeConverter {
    std::unique_ptr<llvm::Module> module;
    VCL::ModuleTable imports{};
    std::unique_ptr<VCL::CodeGenModule> cgm;
};


VCLG::GraphContext::GraphContext(std::shared_ptr<VCL::CompilerInvocation> invocation) : cc{ invocation }, converters{} {
    cc.CreateDiagnosticEngine();
    cc.CreateSourceManager();
    cc.CreateIdentifierTable();
    cc.CreateAttributeTable();
    cc.CreateDirectiveRegistry();
    cc.CreateTarget();
    cc.CreateTypeCache();
    cc.CreateModuleCache();
    cc.CreateLLVMContext();

    definitionRegistry = llvm::makeIntrusiveRefCnt<DefinitionRegistry>(cc);
    globalASTContext = llvm::makeIntrusiveRefCnt<VCL::ASTContext>(cc.GetTypeCache());
    variantCache = std::make_unique<VariantCache>();
}

VCLG::GraphContext::~GraphContext() = default;

llvm::Type* VCLG::GraphContext::ConvertType(VCL::Type* type) {
    if (!typeConverter) {
        typeConverter = std::make_unique<TypeConverter>();
        cc.GetLLVMContext().withContextDo([&](llvm::LLVMContext* context) {
            typeConverter->module = std::make_unique<llvm::Module>("types", *context);
        });
        typeConverter->cgm = std::make_unique<VCL::CodeGenModule>(*typeConverter->module, *globalASTContext,
            cc.GetDiagnosticReporter(), cc.GetTarget(), typeConverter->imports, cc.GetAttributeTable(), cc.GetIdentifierTable());
    }
    // A fresh conversion each time: its cache is keyed by type address, and the types of a
    // compilation are freed with it.
    VCL::CodeGenTypes types{ *typeConverter->cgm };
    return types.ConvertType(VCL::QualType{ type });
}

void VCLG::GraphContext::AddConverter(Converter* converter) {
    converters.push_back(converter);
    converter->SetGraphContext(this);
}

std::shared_ptr<VCLG::GraphInstance> VCLG::GraphContext::CreateInstance() {
    return std::make_shared<GraphInstance>(*this, identityProvider.Next());
}