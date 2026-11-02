#include "allocator.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <cwchar>

namespace allocator {
namespace {

constexpr std::size_t minimum_alignment = 16;
constexpr std::size_t header_size = 16;
constexpr unsigned bank_count = 2;
constexpr std::uint32_t live_magic = 0x524d4b41;
constexpr std::uint32_t dead_magic = 0x44414544;

struct header {
    std::uint32_t magic;
    std::uint32_t bank;
    std::uint64_t size;
};

static_assert(sizeof(header) == header_size, "header must keep 16-byte alignment");

struct bank {
    std::uint8_t* base = nullptr;
    std::size_t capacity = 0;
    std::atomic<std::size_t> offset{0};
    std::atomic<std::size_t> live{0};
};

bank g_banks[bank_count];
std::size_t g_capacity = 0;
std::atomic<unsigned> g_active{0};
std::atomic<std::size_t> g_peak{0};
std::atomic<std::uint64_t> g_allocations{0};
std::atomic<std::uint64_t> g_fallbacks{0};
std::atomic<std::uint64_t> g_frames{0};
std::atomic<std::uint64_t> g_resets{0};
std::atomic<std::uint64_t> g_escaped{0};
std::atomic<bool> g_recycling{true};

std::size_t align_up(std::size_t value, std::size_t alignment) noexcept {
    return (value + alignment - 1) & ~(alignment - 1);
}

bool usable_alignment(std::size_t alignment) noexcept {
    return alignment >= minimum_alignment && alignment <= 4096 && (alignment & (alignment - 1)) == 0;
}

bank* bank_of(const void* pointer) noexcept {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    for (auto& candidate : g_banks) {
        if (!candidate.base) continue;
        const auto begin = reinterpret_cast<std::uintptr_t>(candidate.base);
        if (address > begin && address < begin + candidate.capacity) return &candidate;
    }
    return nullptr;
}

}  // namespace

bool create(std::size_t bytes) noexcept {
    if (g_banks[0].base) return true;
    if (bytes < 2u * 1024u * 1024u) return false;
    const std::size_t per_bank = bytes / bank_count;
    for (unsigned index = 0; index < bank_count; ++index) {
        void* memory = VirtualAlloc(nullptr, per_bank, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!memory) {
            for (auto& bank : g_banks) {
                if (bank.base) VirtualFree(bank.base, 0, MEM_RELEASE);
                bank.base = nullptr;
                bank.capacity = 0;
                bank.offset.store(0, std::memory_order_relaxed);
                bank.live.store(0, std::memory_order_relaxed);
            }
            return false;
        }
        g_banks[index].base = static_cast<std::uint8_t*>(memory);
        g_banks[index].capacity = per_bank;
        g_banks[index].offset.store(0, std::memory_order_relaxed);
        g_banks[index].live.store(0, std::memory_order_relaxed);
    }
    g_capacity = per_bank * bank_count;
    g_active.store(0, std::memory_order_relaxed);
    g_recycling.store(true, std::memory_order_relaxed);
    return true;
}

void destroy() noexcept {
    for (auto& bank : g_banks) {
        if (bank.base) VirtualFree(bank.base, 0, MEM_RELEASE);
        bank.base = nullptr;
        bank.capacity = 0;
        bank.offset.store(0, std::memory_order_relaxed);
        bank.live.store(0, std::memory_order_relaxed);
    }
    g_capacity = 0;
}

void* allocate(std::size_t size, std::size_t alignment, lifetime kind) noexcept {
    if (!g_banks[0].base || !size || !usable_alignment(alignment) || kind == lifetime::persistent) {
        g_fallbacks.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    const std::size_t request = align_up(size, minimum_alignment);
    std::size_t current = 0;
    std::size_t start = 0;
    bank* target = nullptr;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        target = &g_banks[g_active.load(std::memory_order_acquire)];
        current = target->offset.load(std::memory_order_relaxed);
        start = align_up(current + header_size, alignment) - header_size;
        if (start > target->capacity || request > target->capacity - start ||
            header_size > target->capacity - start - request) {
            g_fallbacks.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }
        if (target->offset.compare_exchange_weak(current, start + header_size + request,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_relaxed)) {
            break;
        }
        target = nullptr;
    }
    if (!target) {
        g_fallbacks.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }

    auto* entry = reinterpret_cast<header*>(target->base + start);
    entry->magic = live_magic;
    entry->bank = static_cast<std::uint32_t>(target - g_banks);
    entry->size = size;

    const std::size_t used = start + header_size + request;
    std::size_t peak = g_peak.load(std::memory_order_relaxed);
    while (used > peak && !g_peak.compare_exchange_weak(peak, used, std::memory_order_relaxed)) {
    }
    target->live.fetch_add(1, std::memory_order_relaxed);
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    return target->base + start + header_size;
}

bool owns(const void* pointer) noexcept {
    return bank_of(pointer) != nullptr;
}

std::size_t usable_size(const void* pointer) noexcept {
    if (!bank_of(pointer)) return 0;
    const auto* entry = reinterpret_cast<const header*>(static_cast<const std::uint8_t*>(pointer) - header_size);
    return entry->magic == live_magic ? static_cast<std::size_t>(entry->size) : 0;
}

bool release(void* pointer) noexcept {
    bank* owner = bank_of(pointer);
    if (!owner) return false;
    auto* entry = reinterpret_cast<header*>(static_cast<std::uint8_t*>(pointer) - header_size);
    if (entry->magic != live_magic) return false;
    entry->magic = dead_magic;
    owner->live.fetch_sub(1, std::memory_order_relaxed);
    return true;
}

bool end_frame() noexcept {
    if (!g_banks[0].base) return false;
    const unsigned active = g_active.load(std::memory_order_relaxed);
    const unsigned idle = active ^ 1u;
    bank& recycled = g_banks[idle];

    // Blocks still marked live in the bank that has been idle for a whole frame mean the
    // engine kept a pointer past its grace period. Recycling would overwrite live data.
    const std::size_t retained = recycled.live.load(std::memory_order_acquire);
    if (retained != 0) {
        g_escaped.fetch_add(retained, std::memory_order_relaxed);
        g_recycling.store(false, std::memory_order_release);
        wchar_t message[192];
        const int written = std::swprintf(message, sizeof(message) / sizeof(message[0]),
            L"renderer_hook: %llu arena blocks outlived their frame; recycling stopped.\n",
            static_cast<unsigned long long>(retained));
        if (written > 0) OutputDebugStringW(message);
        g_frames.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    recycled.offset.store(0, std::memory_order_relaxed);
    recycled.live.store(0, std::memory_order_relaxed);
    g_resets.fetch_add(1, std::memory_order_relaxed);
    g_active.store(idle, std::memory_order_release);
    g_frames.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool accepting() noexcept {
    return g_banks[0].base != nullptr && g_frames.load(std::memory_order_relaxed) > 0 &&
        g_recycling.load(std::memory_order_relaxed);
}

stats snapshot() noexcept {
    stats values{};
    values.capacity = g_capacity;
    for (const auto& bank : g_banks) values.used += bank.offset.load(std::memory_order_relaxed);
    values.peak = g_peak.load(std::memory_order_relaxed);
    for (const auto& bank : g_banks) values.live += bank.live.load(std::memory_order_relaxed);
    values.allocations = g_allocations.load(std::memory_order_relaxed);
    values.fallbacks = g_fallbacks.load(std::memory_order_relaxed);
    values.frames = g_frames.load(std::memory_order_relaxed);
    values.resets = g_resets.load(std::memory_order_relaxed);
    values.escaped = g_escaped.load(std::memory_order_relaxed);
    values.bank = g_active.load(std::memory_order_relaxed);
    values.recycling = g_recycling.load(std::memory_order_relaxed);
    return values;
}

}  // namespace allocator
