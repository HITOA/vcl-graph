#include <VCLG/ExecutionContext.hpp>



VCLG::ExecutionContext::ExecutionContext(std::unique_ptr<VCL::ASTProgram> program) {
    session = VCL::ExecutionSession::Create();

    std::unique_ptr<VCL::Module> module = session->CreateModule(std::move(program));
    module->Emit();
    module->Verify();
    module->Optimize();

    moduleInfo = module->GetModuleInfo();
    
    session->SubmitModule(std::move(module));

    portsAddresses.resize(moduleInfo->GetVariables().size());
    for (int i = 0; i < moduleInfo->GetVariables().size(); ++i) {
        auto& varInfo = moduleInfo->GetVariables()[i];
        if (varInfo->typeinfo->IsExtern()) {
            std::shared_ptr<uint8_t> current{ (uint8_t*)aligned_alloc(
                varInfo->typeinfo->rtInfo.alignmentInBytes, 
                varInfo->typeinfo->rtInfo.sizeInBytes) };
            allocatedPorts.push_back(current);
            portsAddresses[i] = current.get();
            session->DefineExternSymbolPtr(varInfo->name, current.get());
        }
    }

    for (int i = 0; i < moduleInfo->GetVariables().size(); ++i) {
        auto& varInfo = moduleInfo->GetVariables()[i];
        if (!varInfo->typeinfo->IsExtern()) {
            portsAddresses[i] = session->Lookup(varInfo->name);
        }
    }

    entrypoint = (void(*)())session->Lookup("Main");
}

VCLG::ExecutionContext::~ExecutionContext() {
    if (userDataDestroyer)
        userDataDestroyer(userData);
}

void VCLG::ExecutionContext::SetUserData(std::function<void*(ExecutionContext*)> userDataConstructor, std::function<void(void*)> userDataDestroyer) {
    if (userDataDestroyer)
        userDataDestroyer(userData);
    userData = userDataConstructor(this);
    this->userDataDestroyer = userDataDestroyer;
}

std::function<void()> VCLG::ExecutionContext::GetEntrypoint() {
    return entrypoint;
}

const std::vector<void*> VCLG::ExecutionContext::GetPortsAddresses() {
    return portsAddresses;
}

std::shared_ptr<VCL::ModuleInfo> VCLG::ExecutionContext::GetModuleInfo() {
    return moduleInfo;
}

void* VCLG::ExecutionContext::GetUserData() {
    return userData;
}