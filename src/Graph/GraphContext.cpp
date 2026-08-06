#include <VCLG/Graph/GraphContext.hpp>

#include <VCLG/Graph/GraphInstance.hpp>


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
}

void VCLG::GraphContext::AddConverter(Converter* converter) {
    converters.push_back(converter);
    converter->SetGraphContext(this);
}

std::shared_ptr<VCLG::GraphInstance> VCLG::GraphContext::CreateInstance(
            std::shared_ptr<GraphUserDataTrailAllocator> userDataTailAllocator,
            std::unique_ptr<Allocator> allocator) {
    return std::make_shared<GraphInstance>(*this, identityProvider.Next(), userDataTailAllocator, std::move(allocator));
}