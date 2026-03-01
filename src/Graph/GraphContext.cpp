#include <VCLG/Graph/GraphContext.hpp>

#include <VCLG/Graph/GraphInstance.hpp>


VCLG::GraphContext::GraphContext(std::shared_ptr<VCL::CompilerInvocation> invocation) : cc{ invocation } {
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
}

std::shared_ptr<VCLG::GraphInstance> VCLG::GraphContext::CreateInstance() {
    return std::make_shared<GraphInstance>(*this);
}