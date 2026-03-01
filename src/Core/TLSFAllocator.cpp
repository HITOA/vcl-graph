#include <VCLG/Core/Allocator.hpp>
#include <tlsf.h>
#include <cstdlib>


VCLG::TLSFAllocator::TLSFAllocator() : tlsf{ nullptr }, poolSize{ 1048576 }, pools{} {
    tlsf = malloc(tlsf_size());
    tlsf_create(tlsf);
    CreatePool(poolSize);
}

VCLG::TLSFAllocator::TLSFAllocator(size_t poolSize) : tlsf{ nullptr }, poolSize{ poolSize }, pools{} {
    tlsf = malloc(tlsf_size());
    tlsf_create(tlsf);
    CreatePool(poolSize);
}

VCLG::TLSFAllocator::~TLSFAllocator() {
    while (!pools.empty())
        DestroyPool(0);
    free(tlsf);
}

void* VCLG::TLSFAllocator::Allocate(size_t size, size_t alignment) {
    void* ptr = tlsf_memalign(tlsf, alignment, size);
    if (!ptr) {
        CreatePool(poolSize);
        ptr = tlsf_memalign(tlsf, alignment, size);
    }
    return ptr;
}

void VCLG::TLSFAllocator::Deallocate(void* ptr, size_t size) {
    tlsf_free(tlsf, ptr);
}

void VCLG::TLSFAllocator::Reset() {
    while (!pools.empty())
        DestroyPool(0);
    CreatePool(poolSize);
}

void VCLG::TLSFAllocator::CreatePool(size_t poolSize) {
    pool_t pool = malloc(poolSize);
    tlsf_add_pool(tlsf, pool, poolSize);
    pools.push_back(pool);
}

void VCLG::TLSFAllocator::DestroyPool(size_t poolIdx) {
    pool_t pool = pools[poolIdx];
    tlsf_remove_pool(tlsf, pool);
    pools.erase(pools.begin() + poolIdx);
    free(pool);
}
