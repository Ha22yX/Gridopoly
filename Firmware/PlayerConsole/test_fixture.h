#pragma once
#include <new>
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#endif

// Test state must not permanently consume the internal RAM needed by RGB DMA.
// Each fixture owns temporary storage and releases it before physical UI tests.
template<class T> class TestFixture {
public:
    TestFixture() {
#if defined(ARDUINO_ARCH_ESP32)
        void *storage = heap_caps_malloc(sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (storage != nullptr) value_ = new (storage) T{};
#else
        value_ = new (std::nothrow) T{};
#endif
    }
    ~TestFixture() {
#if defined(ARDUINO_ARCH_ESP32)
        if (value_ != nullptr) {
            value_->~T();
            heap_caps_free(value_);
        }
#else
        delete value_;
#endif
    }
    TestFixture(const TestFixture &) = delete;
    TestFixture &operator=(const TestFixture &) = delete;
    explicit operator bool() const { return value_ != nullptr; }
    T &operator*() const { return *value_; }
private:
    T *value_ = nullptr;
};
