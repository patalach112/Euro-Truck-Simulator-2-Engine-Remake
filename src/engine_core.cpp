#include "engine_core.h"
#include "scanner.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <MinHook.h>

#include <cwchar>

namespace engine_core {
namespace {

// Candidate prologue of the sequential entity update pass. Unverified placeholder.
constexpr char update_signature[] = "40 53 48 83 EC 20 48 8B D9 E8 ? ? ? ? 48 8B 03";

using loop_fn = std::uintptr_t(WINAPI*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);

struct item_job {
    update_view* view;
};

loop_fn g_original = nullptr;
thread_pool::pool* g_pool = nullptr;
std::atomic<adapter> g_adapter{nullptr};
std::atomic<bool> g_parallel{false};
std::atomic<bool> g_located{false};
std::atomic<bool> g_hooked{false};
std::atomic<std::uint64_t> g_updates{0};
std::atomic<std::uint64_t> g_items{0};

void step_item(void* context, unsigned index) noexcept {
    auto* view = static_cast<update_view*>(context);
    view->step(view->items, index, view->step_context);
}

void log(const wchar_t* text) noexcept {
    OutputDebugStringW(text);
}

std::uintptr_t WINAPI hkEntityUpdate(std::uintptr_t first, std::uintptr_t second,
                                     std::uintptr_t third, std::uintptr_t fourth) noexcept {
    const adapter resolve = g_adapter.load(std::memory_order_acquire);
    update_view view{};
    // Nothing is skipped here: without an adapter and a pool the original call runs unchanged.
    if (resolve && g_pool && g_parallel.load(std::memory_order_relaxed) &&
        resolve(first, view) && view.items && view.count && view.step && view.stride) {
        g_updates.fetch_add(1, std::memory_order_relaxed);
        if (g_pool->dispatch(&step_item, &view, view.count)) {
            g_items.fetch_add(view.count, std::memory_order_relaxed);
            return 0;
        }
    }
    return g_original ? g_original(first, second, third, fourth) : 0;
}

}  // namespace

void set_adapter(adapter fn) noexcept {
    g_adapter.store(fn, std::memory_order_release);
    g_parallel.store(fn != nullptr, std::memory_order_release);
}

bool set_parallel_enabled(bool enabled) noexcept {
    if (enabled && !g_adapter.load(std::memory_order_acquire)) return false;
    g_parallel.store(enabled, std::memory_order_release);
    return true;
}

bool install(thread_pool::pool* pool) noexcept {
    g_pool = pool;
    const scanner::result scan = scanner::find_main_code(update_signature);
    g_located.store(scan.address != nullptr, std::memory_order_relaxed);
    if (!scan.address) {
        log(scan.timed_out
            ? L"renderer_hook: entity loop scan hit its time budget; pool stays idle.\n"
            : L"renderer_hook: entity loop signature not found in this build; pool stays idle.\n");
        return false;
    }

    const MH_STATUS initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) {
        log(L"renderer_hook: MinHook initialization failed for the entity loop.\n");
        return false;
    }
    if (MH_CreateHook(const_cast<std::uint8_t*>(scan.address),
                      reinterpret_cast<LPVOID>(&hkEntityUpdate),
                      reinterpret_cast<LPVOID*>(&g_original)) != MH_OK ||
        MH_EnableHook(const_cast<std::uint8_t*>(scan.address)) != MH_OK) {
        log(L"renderer_hook: entity loop could not be hooked.\n");
        g_original = nullptr;
        return false;
    }

    g_hooked.store(true, std::memory_order_relaxed);
    wchar_t message[224];
    const int written = std::swprintf(message, sizeof(message) / sizeof(message[0]),
        L"renderer_hook: entity loop hooked at %p with %u workers; no adapter, updates forwarded.\n",
        static_cast<const void*>(scan.address), pool ? pool->workers() : 0u);
    if (written > 0) log(message);
    return true;
}

report status() noexcept {
    report values{};
    values.located = g_located.load(std::memory_order_relaxed);
    values.hooked = g_hooked.load(std::memory_order_relaxed);
    values.adapted = g_adapter.load(std::memory_order_relaxed) != nullptr;
    values.workers = g_pool ? g_pool->workers() : 0u;
    values.updates = g_updates.load(std::memory_order_relaxed);
    values.items = g_items.load(std::memory_order_relaxed);
    values.dispatches = g_pool ? g_pool->dispatches() : 0u;
    return values;
}

}  // namespace engine_core
