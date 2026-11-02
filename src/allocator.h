#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace allocator {

enum class lifetime {
    frame,       // scratch that dies with the frame it was allocated in
    persistent,  // survives frames; never served from the arena
};

struct stats {
    std::size_t capacity;
    std::size_t used;
    std::size_t peak;
    std::size_t live;
    std::uint64_t allocations;
    std::uint64_t fallbacks;
    std::uint64_t frames;
    std::uint64_t resets;
    std::uint64_t escaped;
    unsigned bank;
    bool recycling;
};

// Reserves two banks totalling the requested bytes. Repeated calls are ignored.
bool create(std::size_t bytes = 256u * 1024u * 1024u) noexcept;
void destroy() noexcept;

// Bump allocation with a 16-byte header. Returns nullptr when the request does not fit or
// when the caller declares it persistent, which routes it back to the engine heap.
void* allocate(std::size_t size, std::size_t alignment = 16,
               lifetime kind = lifetime::frame) noexcept;

bool owns(const void* pointer) noexcept;
std::size_t usable_size(const void* pointer) noexcept;
bool release(void* pointer) noexcept;

// Advances the frame rotation: the bank idle since the previous frame is cleared and the
// next frame starts in it. Every pointer handed out therefore stays valid for one full
// frame after its own. Returns false, and stops recycling, if blocks outlived that grace.
bool end_frame() noexcept;

// Allocations are only served from the arena once frames have started rotating.
bool accepting() noexcept;
stats snapshot() noexcept;

}  // namespace allocator
