#pragma once

#include <VCL/Definition.hpp>


namespace VCLG {

    class Port {
    public:
        Port() = delete;
        Port(const std::string& name, std::shared_ptr<VCL::TypeInfo> typeInfo);
        ~Port() = default;

        const std::string& GetName();
        std::shared_ptr<VCL::TypeInfo> GetTypeInfo();

        bool CanConnect(Port* port);

    private:
        std::string name;
        std::shared_ptr<VCL::TypeInfo> typeInfo;
    };

}