#pragma once

#include <VCL/Definition.hpp>

#include <memory>
#include <functional>


namespace VCLG {

    class Port {
    public:
        struct Storage {
            void* ptr = nullptr;
            size_t size = 0;
            std::function<void(Storage*, void*, size_t)> copyTo = nullptr;
        };

    public:
        Port() = delete;
        Port(const std::string& name, std::shared_ptr<VCL::TypeInfo> typeInfo);
        ~Port();

        const std::string& GetName();
        std::shared_ptr<VCL::TypeInfo> GetTypeInfo();

        bool CanConnect(Port* port);

        void ResetStorage();
        void InitializeStorage(size_t size);
        void InitializeStorage(size_t size, std::function<void(Storage*, void*, size_t)> copyTo);
        Storage GetStorage();

    private:
        std::string name{};
        std::shared_ptr<VCL::TypeInfo> typeInfo{};
        Storage storage{};
    };

}