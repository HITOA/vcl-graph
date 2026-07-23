#pragma once

#include <cstdint>
#include <atomic>


#define INVALID_IDENTITY (Identity(0))

namespace VCLG {
    using Identity = uint32_t;

    class IdentityProvider {
    public:
        inline Identity Next() {
            return next.fetch_add(1, std::memory_order_relaxed);
        }

        inline Identity Peek() {
            return next.load(std::memory_order_relaxed);
        }

        inline void Reset() { next.store(1); }

    private:
        std::atomic<Identity> next{ 1 };
    };

}