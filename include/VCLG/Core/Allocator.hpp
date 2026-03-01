#pragma once

#include <cstddef>
#include <llvm/ADT/SmallVector.h>


namespace VCLG {

    class Allocator {
    public:
        virtual ~Allocator() = default;

        virtual void* Allocate(size_t size, size_t alignment) = 0;
        virtual void Deallocate(void* ptr, size_t size) = 0;

        virtual void Reset() = 0;
    };

    class TLSFAllocator : public Allocator {
    public:
        TLSFAllocator();
        TLSFAllocator(size_t poolSize);
        ~TLSFAllocator() override;
            
        void* Allocate(size_t size, size_t alignment) override;
        void Deallocate(void* ptr, size_t size) override;

        void Reset() override;

    private:
        void CreatePool(size_t poolSize);
        void DestroyPool(size_t poolIdx);

    private:
        void* tlsf;
        size_t poolSize;
        llvm::SmallVector<void*> pools;
    };

}