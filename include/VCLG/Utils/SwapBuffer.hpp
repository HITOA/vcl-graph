#pragma once

#include <cstddef>
#include <atomic>


namespace VCLG {

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
        SwapHandle(SwapHandle&& other) : ptr{ std::move(other.ptr) }, counter{ std::move(other.counter) } {
            other.ptr = nullptr;
            other.counter = nullptr;
        }
        ~SwapHandle() {
            if (counter != nullptr)
                counter->fetch_sub(1);
        }

        SwapHandle& operator=(const SwapHandle& other) {
            ptr = other.ptr;
            counter = other.counter;
            if (counter != nullptr)
                counter->fetch_add(1);
            return *this;
        }
        SwapHandle& operator=(SwapHandle&& other) {
            ptr = std::move(other.ptr);
            counter = std::move(other.counter);
            other.ptr = nullptr;
            other.counter = nullptr;
            return *this;
        }

        inline T* operator->() { return ptr; }
        inline operator bool() { return ptr != nullptr; }

    private:
        T* ptr = nullptr;
        std::atomic_uint32_t* counter = nullptr;
    };

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
            uint32_t index = currentIndex.load();
            return SwapHandle<T>{ elements[index].ptr, &elements[index].counter }; 
        }

        SwapHandle<T> Back() {
            uint32_t index = (currentIndex.load() + 1) % Count;
            return SwapHandle<T>{ 
                elements[index].ptr,
                &elements[index].counter
            };
        }

    private:
        void WaitForCounterToBeZero(uint32_t index) {
            uint32_t oldUseCount = elements[index].counter.load();
            while (oldUseCount != 0) {
                elements[index].counter.wait(oldUseCount);
                oldUseCount = elements[index].counter.load();
            }
        }

    private:
        struct Element {
            T* ptr = nullptr;
            std::atomic_uint32_t counter = 0;
        } elements[Count] = {};
        std::atomic_uint32_t currentIndex = 0;
    };

}