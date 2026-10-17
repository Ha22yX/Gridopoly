#pragma once
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include <atomic>
namespace host {
inline std::atomic<unsigned> violations{0}, locks{0};
inline thread_local unsigned depth = 0;
inline std::thread::id mainThread;
inline std::vector<std::string> lines;
inline int capacity = 64;
inline void io() { if (depth || std::this_thread::get_id() != mainThread) ++violations; }
}
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
inline void portENTER_CRITICAL(portMUX_TYPE *m) { m->lock(); ++host::depth; ++host::locks; }
inline void portEXIT_CRITICAL(portMUX_TYPE *m) { --host::depth; m->unlock(); }
struct SerialSink {
    int availableForWrite() { host::io(); return host::capacity; }
    size_t write(const uint8_t *p, size_t n) { host::io(); host::lines.emplace_back(reinterpret_cast<const char *>(p), n); return n; }
};
inline SerialSink Serial;
