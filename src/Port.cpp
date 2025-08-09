#include <VCLG/Port.hpp>

#include <cstring>


VCLG::Port::Port(const std::string& name, std::shared_ptr<VCL::TypeInfo> typeInfo) :
    name{ name }, typeInfo{ typeInfo } {
    
}

VCLG::Port::~Port() {
    ResetStorage();
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

void VCLG::Port::ResetStorage() {
    if (storage.ptr != nullptr)
        free(storage.ptr);
    storage.ptr = nullptr;
    storage.size = 0;
    storage.copyTo = nullptr;
}

void VCLG::Port::InitializeStorage(size_t size) {
    ResetStorage();
    storage.ptr = malloc(size);
    storage.size = size;
    storage.copyTo = [](Storage* storage, void* dest, size_t size) {
        memcpy(dest, storage->ptr, size > storage->size ? storage->size : size);
    };
    memset(storage.ptr, 0x0, storage.size);
}

void VCLG::Port::InitializeStorage(size_t size, std::function<void(Storage*, void*, size_t)> copyTo) {
    ResetStorage();
    storage.ptr = malloc(size);
    storage.size = size;
    storage.copyTo = copyTo;
    memset(storage.ptr, 0x0, storage.size);
}

VCLG::Port::Storage VCLG::Port::GetStorage() {
    return storage;
}
