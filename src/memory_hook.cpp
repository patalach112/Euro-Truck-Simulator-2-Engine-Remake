#include "memory_hook.h"
#include "allocator.h"
#include "scanner.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <MinHook.h>

#include <cwchar>

namespace memory_hook {
namespace {

// Candidate prologues of the engine's general allocate/free pair. Unverified placeholders.
constexpr char allocate_signature[] = "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B D9";
constexpr char free_signature[] = "48 85 C9 74 18 53 48 83 EC 20 48 8B D9 48 8B 49 08";

// Anything larger is left to the engine heap: a streaming batch should not monopolise the arena.
constexpr std::size_t direct_limit = 256u * 1024u;

// The engine's calling conventions are not exported, so the detours take the four integer
// argument slots and only interpret the first one.
using memory_fn = std::uintptr_t(WINAPI*)(std::uintptr_t, std::uintptr_t,
                                          std::uintptr_t, std::uintptr_t);

memory_fn g_allocate = nullptr;
memory_fn g_free = nullptr;
std::atomic<bool> g_redirect{false};
std::atomic<bool> g_allocate_located{false};
std::atomic<bool> g_free_located{false};
std::atomic<bool> g_hooked{false};
std::atomic<std::uint64_t> g_served{0};
std::atomic<std::uint64_t> g_rejected{0};
std::atomic<std::uint64_t> g_freed{0};

void log(const wchar_t* text) noexcept {
    OutputDebugStringW(text);
}

std::uintptr_t WINAPI hkAllocate(std::uintptr_t size, std::uintptr_t second,
                                 std::uintptr_t third, std::uintptr_t fourth) noexcept {
    if (g_redirect.load(std::memory_order_relaxed) && size && size <= direct_limit) {
        if (void* block = allocator::allocate(static_cast<std::size_t>(size))) {
            g_served.fetch_add(1, std::memory_order_relaxed);
            return reinterpret_cast<std::uintptr_t>(block);
        }
        g_rejected.fetch_add(1, std::memory_order_relaxed);
    }
    return g_allocate ? g_allocate(size, second, third, fourth) : 0;
}

std::uintptr_t WINAPI hkFree(std::uintptr_t block, std::uintptr_t second,
                             std::uintptr_t third, std::uintptr_t fourth) noexcept {
    if (allocator::owns(reinterpret_cast<void*>(block))) {
        // The bytes stay committed for the next reset; only the bookkeeping changes.
        if (allocator::release(reinterpret_cast<void*>(block))) {
            g_freed.fetch_add(1, std::memory_order_relaxed);
        }
        return 0;
    }
    return g_free ? g_free(block, second, third, fourth) : 0;
}

bool hook(std::uint8_t* target, void* detour, void** original) noexcept {
    return MH_CreateHook(target, detour, original) == MH_OK && MH_EnableHook(target) == MH_OK;
}

}  // namespace

bool install(std::size_t arena_capacity) noexcept {
    const scanner::result allocate_scan = scanner::find_main_code(allocate_signature);
    const scanner::result free_scan = scanner::find_main_code(free_signature);
    g_allocate_located.store(allocate_scan.address != nullptr, std::memory_order_relaxed);
    g_free_located.store(free_scan.address != nullptr, std::memory_order_relaxed);
    if (!allocate_scan.address || !free_scan.address) {
        log(allocate_scan.timed_out || free_scan.timed_out
            ? L"renderer_hook: allocation scan hit its time budget; arena stays unattached.\n"
            : L"renderer_hook: allocation pair not found in this build; arena stays unattached.\n");
        return false;
    }
    if (!allocator::create(arena_capacity)) {
        log(L"renderer_hook: arena reservation failed; allocations keep using the engine heap.\n");
        return false;
    }

    const MH_STATUS initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) {
        log(L"renderer_hook: MinHook initialization failed for the allocator hooks.\n");
        return false;
    }
    if (!hook(const_cast<std::uint8_t*>(allocate_scan.address),
              reinterpret_cast<void*>(&hkAllocate), reinterpret_cast<void**>(&g_allocate)) ||
        !hook(const_cast<std::uint8_t*>(free_scan.address),
             reinterpret_cast<void*>(&hkFree), reinterpret_cast<void**>(&g_free))) {
        log(L"renderer_hook: allocation pair could not be hooked; arena stays unattached.\n");
        return false;
    }

    g_hooked.store(true, std::memory_order_relaxed);
    wchar_t message[224];
    const int written = std::swprintf(message, sizeof(message) / sizeof(message[0]),
        L"renderer_hook: allocation pair hooked at %p/%p, arena %llu KB idle until enabled.\n",
        static_cast<const void*>(allocate_scan.address), static_cast<const void*>(free_scan.address),
        static_cast<unsigned long long>(arena_capacity / 1024));
    if (written > 0) log(message);
    return true;
}

bool set_redirect_enabled(bool enabled) noexcept {
    if (enabled && !g_hooked.load(std::memory_order_acquire)) return false;
    g_redirect.store(enabled, std::memory_order_release);
    log(enabled ? L"renderer_hook: allocations redirected to the arena.\n"
                : L"renderer_hook: allocations returned to the engine heap.\n");
    return true;
}

report status() noexcept {
    report values{};
    values.allocate_located = g_allocate_located.load(std::memory_order_relaxed);
    values.free_located = g_free_located.load(std::memory_order_relaxed);
    values.hooked = g_hooked.load(std::memory_order_relaxed);
    values.redirecting = g_redirect.load(std::memory_order_relaxed);
    values.served = g_served.load(std::memory_order_relaxed);
    values.rejected = g_rejected.load(std::memory_order_relaxed);
    values.freed = g_freed.load(std::memory_order_relaxed);
    return values;
}

}  // namespace memory_hook
