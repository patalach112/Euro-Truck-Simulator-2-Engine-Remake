#include "allocator.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>

namespace allocator {
namespace {

constexpr std::size_t minimum_alignment = 16;
constexpr std::size_t header_size = 16;
constexpr std::uint32_t live_magic = 0x524d4b41;  // "AKMR"
constexpr std::uint32_t dead_magic = 0x44414544;

struct header {
    std::uint32_t magic;
    std::uint32_t flags;
    std::uint64_t size;
};

static_assert(sizeof(header) == header_size, "header must keep 16-byte alignment");

std::uint8_t* g_base = nullptr;
std::size_t g_capacity = 0;
std::atomic<std::size_t> g_offset{0};
std::atomic<std::size_t> g_peak{0};
std::atomic<std::size_t> g_live{0};
std::atomic<std::uint64_t> g_allocations{0};
std::atomic<std::uint64_t> g_fallbacks{0};
std::atomic<std::uint64_t> g_resets{0};

std::size_t align_up(std::size_t value, std::size_t alignment) noexcept {
    return (value + alignment - 1) & ~(alignment - 1);
}

bool usable_alignment(std::size_t alignment) noexcept {
    return alignment >= minimum_alignment && alignment <= 4096 &&
        (alignment & (alignment - 1)) == 0;
}

}  // namespace

bool create(std::size_t bytes) noexcept {
    if (g_base) return true;
    if (bytes < 1024 * 1024) return false;
    void* memory = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!memory) return false;
    g_base = static_cast<std::uint8_t*>(memory);
    g_capacity = bytes;
    g_offset.store(0, std::memory_order_relaxed);
    return true;
}

void destroy() noexcept {
    if (!g_base) return;
    VirtualFree(g_base, 0, MEM_RELEASE);
    g_base = nullptr;
    g_capacity = 0;
    g_offset.store(0, std::memory_order_relaxed);
    g_live.store(0, std::memory_order_relaxed);
}

void* allocate(std::size_t size, std::size_t alignment) noexcept {
    if (!g_base || !size || !usable_alignment(alignment)) {
        g_fallbacks.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    const std::size_t request = align_up(size, minimum_alignment);
    // The header sits before the returned pointer, so the alignment applies to the payload.
    std::size_t current = g_offset.load(std::memory_order_relaxed);
    std::size_t start = 0;
    for (;;) {
        start = align_up(current + header_size, alignment) - header_size;
        if (start > g_capacity || request > g_capacity - start ||
            header_size > g_capacity - start - request) {
            g_fallbacks.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }
        if (g_offset.compare_exchange_weak(current, start + header_size + request,
                                           std::memory_order_acq_rel,
                                           std::memory_order_relaxed)) {
            break;
        }
    }

    auto* entry = reinterpret_cast<header*>(g_base + start);
    entry->magic = live_magic;
    entry->flags = 0;
    entry->size = size;

    const std::size_t used = start + header_size + request;
    std::size_t peak = g_peak.load(std::memory_order_relaxed);
    while (used > peak && !g_peak.compare_exchange_weak(peak, used, std::memory_order_relaxed)) {
    }
    g_live.fetch_add(1, std::memory_order_relaxed);
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    return g_base + start + header_size;
}

bool owns(const void* pointer) noexcept {
    if (!g_base || !pointer) return false;
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    const auto begin = reinterpret_cast<std::uintptr_t>(g_base);
    return address >= begin + header_size && address < begin + g_offset.load(std::memory_order_relaxed);
}

std::size_t usable_size(const void* pointer) noexcept {
    if (!owns(pointer)) return 0;
    const auto* entry = reinterpret_cast<const header*>(static_cast<const std::uint8_t*>(pointer) - header_size);
    return entry->magic == live_magic ? static_cast<std::size_t>(entry->size) : 0;
}

bool release(void* pointer) noexcept {
    if (!owns(pointer)) return false;
    auto* entry = reinterpret_cast<header*>(static_cast<std::uint8_t*>(pointer) - header_size);
    if (entry->magic != live_magic) return false;
    entry->magic = dead_magic;
    g_live.fetch_sub(1, std::memory_order_relaxed);
    return true;
}

std::size_t reset() noexcept {
    const std::size_t used = g_offset.exchange(0, std::memory_order_acq_rel);
    g_live.store(0, std::memory_order_relaxed);
    g_resets.fetch_add(1, std::memory_order_relaxed);
    return used;
}

stats snapshot() noexcept {
    stats values{};
    values.capacity = g_capacity;
    values.used = g_offset.load(std::memory_order_relaxed);
    values.peak = g_peak.load(std::memory_order_relaxed);
    values.live = g_live.load(std::memory_order_relaxed);
    values.allocations = g_allocations.load(std::memory_order_relaxed);
    values.fallbacks = g_fallbacks.load(std::memory_order_relaxed);
    values.resets = g_resets.load(std::memory_order_relaxed);
    return values;
}

}  // namespace allocator
