#pragma once
#include <cstdlib>
#include <cstdio>
#include <unordered_map>
constexpr unsigned MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
inline std::unordered_map<void *, size_t> avatarLiveAllocations;
inline size_t avatarAllocationLimit = static_cast<size_t>(-1);
inline size_t avatarLiveBytes = 0;
inline void *heap_caps_malloc(size_t bytes, unsigned) {
    if (bytes > avatarAllocationLimit) return nullptr;
    void *result = std::malloc(bytes);
    if (result) { avatarLiveAllocations[result] = bytes; avatarLiveBytes += bytes; }
    return result;
}
inline void heap_caps_free(void *pointer) {
    if (!pointer) return;
    const auto found = avatarLiveAllocations.find(pointer);
    if (found == avatarLiveAllocations.end()) {
        std::fprintf(stderr, "FAIL untracked/double free: %p\n", pointer);
        std::abort();
    }
    avatarLiveBytes -= found->second;
    avatarLiveAllocations.erase(found);
    std::free(pointer);
}
