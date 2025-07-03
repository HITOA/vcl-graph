#pragma once

#include <VCL/ExecutionSession.hpp>

#include <atomic>
#include <functional>
#include <unordered_map>


namespace VCLG {

    class ExecutionContext {
    public:
        ExecutionContext() = delete;
        ExecutionContext(std::unique_ptr<VCL::ASTProgram> program);
        ~ExecutionContext();

        void SetUserData(std::function<void*(ExecutionContext*)> userDataConstructor, std::function<void(void*)> userDataDestroyer);

        std::function<void()> GetEntrypoint();
        const std::vector<void*> GetPortsAddresses();
        std::shared_ptr<VCL::ModuleInfo> GetModuleInfo();
        void* GetUserData();

    private:
        std::unique_ptr<VCL::ExecutionSession> session;
        std::shared_ptr<VCL::ModuleInfo> moduleInfo;
        
        std::vector<std::shared_ptr<void>> allocatedPorts;

        std::function<void()> entrypoint;
        std::vector<void*> portsAddresses;

        std::function<void(void*)> userDataDestroyer;
        void* userData;
    };

}