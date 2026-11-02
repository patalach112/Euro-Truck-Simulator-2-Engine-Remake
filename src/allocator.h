#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace allocator {

struct stats {
    std::size_t capacity;
    std::size_t used;
    std::size_t peak;
    std::size_t live;
    std::uint64_t allocations;
    std::uint64_t fallbacks;
    std::uint64_t resets;
};

// Reserves one contiguous region from the OS. Repeated calls are ignored.
bool create(std::size_t bytes = 256u * 1024u * 1024u) noexcept;
void destroy() noexcept;

// Bump allocation with a 16-byte header. Returns nullptr when the request does not fit.
void* allocate(std::size_t size, std::size_t alignment = 16) noexcept;

// True for pointers that came from this arena, including the aligned interior address.
bool owns(const void* pointer) noexcept;
std::size_t usable_size(const void* pointer) noexcept;

// Releases a pointer into the arena accounting. The bytes are reclaimed only by reset().
bool release(void* pointer) noexcept;

// Invalidates every live allocation at once. Only call with the engine idle.
std::size_t reset() noexcept;

stats snapshot() noexcept;

}  // namespace allocator
