#pragma once

#include <cstdint>
#include <atomic>


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

        inline void Reset() { next.store(0); }

    private:
        std::atomic<Identity> next{ 0 };
    };

}