#include <VCLG/Port.hpp>



VCLG::Port::Port(const std::string& name, std::shared_ptr<VCL::TypeInfo> typeInfo) :
    name{ name }, typeInfo{ typeInfo } {
    
}

const std::string& VCLG::Port::GetName() {
    return name;
}

std::shared_ptr<VCL::TypeInfo> VCLG::Port::GetTypeInfo() {
    return typeInfo;
}

bool VCLG::Port::CanConnect(Port* port) {
    return *(port->typeInfo) == *typeInfo;
}