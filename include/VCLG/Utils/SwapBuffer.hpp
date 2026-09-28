#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>


namespace VCLG {

    template<typename T, size_t Count>
    class SwapBuffer;

    template<typename T>
    class SwapHandle {
    public:
        SwapHandle() = default;
        SwapHandle(T* ptr, std::atomic_uint32_t* counter) : ptr{ ptr }, counter{ counter } {
            if (counter != nullptr)
                counter->fetch_add(1);
        }
        SwapHandle(const SwapHandle& other) : ptr{ other.ptr }, counter{ other.counter } {
            if (counter != nullptr)
                counter->fetch_add(1);
        }
        SwapHandle(SwapHandle&& other) : ptr{ other.ptr }, counter{ other.counter } {
            other.ptr = nullptr;
            other.counter = nullptr;
        }
        ~SwapHandle() { Release(); }

        SwapHandle& operator=(const SwapHandle& other) {
            if (this == &other)
                return *this;
            if (other.counter != nullptr)
                other.counter->fetch_add(1);
            Release();
            ptr = other.ptr;
            counter = other.counter;
            return *this;
        }
        SwapHandle& operator=(SwapHandle&& other) {
            if (this == &other)
                return *this;
            Release();
            ptr = other.ptr;
            counter = other.counter;
            other.ptr = nullptr;
            other.counter = nullptr;
            return *this;
        }

        inline T* operator->() { return ptr; }
        inline operator bool() { return ptr != nullptr; }

    private:
        template<typename, size_t>
        friend class SwapBuffer;

        struct AdoptTag {};

        // Takes over a reference the caller already added to `counter`.
        SwapHandle(T* ptr, std::atomic_uint32_t* counter, AdoptTag) : ptr{ ptr }, counter{ counter } {}

        void Release() {
            if (counter != nullptr)
                counter->fetch_sub(1);
            ptr = nullptr;
            counter = nullptr;
        }

    private:
        T* ptr = nullptr;
        std::atomic_uint32_t* counter = nullptr;
    };

    /**
     * Single-reader-friendly double (or N) buffer. `Front()` is lock-free and wait-free in practice,
     * so it can be called from a realtime thread. `EmplaceFront()` may block (it waits for readers
     * to release the slot it is about to overwrite) and must never be called from a realtime thread.
     */
    template<typename T, size_t Count = 2>
    class SwapBuffer {
    public:
        SwapBuffer() = default;
        ~SwapBuffer() {
            for (uint32_t i = 0; i < Count; ++i) {
                WaitForCounterToBeZero(i);
                if (elements[i].ptr != nullptr) {
                    delete elements[i].ptr;
                    elements[i].ptr = nullptr;
                }
            }
        }

        template<typename... Args>
        void EmplaceFront(Args&&... args) {
            std::lock_guard<std::mutex> guard{ writeMutex };
            uint32_t newFrontIndex = (currentIndex.load() + 1) % Count;
            WaitForCounterToBeZero(newFrontIndex);
            if (elements[newFrontIndex].ptr != nullptr) {
                delete elements[newFrontIndex].ptr;
                elements[newFrontIndex].ptr = nullptr;
            }
            elements[newFrontIndex].ptr = new T{ std::forward<Args>(args)... };
            currentIndex.store(newFrontIndex);
        }

        SwapHandle<T> Front() {
            // Reference the slot *before* reading its pointer, then confirm it is still the front.
            // Otherwise a writer could recycle the slot between our index load and our increment.
            // Writers only ever recycle non-front slots, and they check the counter before doing so.
            while (true) {
                uint32_t index = currentIndex.load();
                elements[index].counter.fetch_add(1);
                if (currentIndex.load() == index)
                    return SwapHandle<T>{ elements[index].ptr, &elements[index].counter, typename SwapHandle<T>::AdoptTag{} };
                elements[index].counter.fetch_sub(1);
            }
        }

    private:
        void WaitForCounterToBeZero(uint32_t index) {
            // Readers release from the realtime thread, where we don't want to pay for a futex
            // wake (atomic::notify). Poll instead; the wait is at most one audio block long.
            uint32_t spins = 0;
            while (elements[index].counter.load() != 0) {
                if (++spins < 64)
                    std::this_thread::yield();
                else
                    std::this_thread::sleep_for(std::chrono::microseconds{ 100 });
            }
        }

    private:
        struct Element {
            T* ptr = nullptr;
            std::atomic_uint32_t counter = 0;
        } elements[Count] = {};
        std::atomic_uint32_t currentIndex = 0;
        std::mutex writeMutex{};
    };

}
