#include "optimization.h"
#include "scanner.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <MinHook.h>

#include <cwchar>

namespace optimization {

std::atomic<bool> g_disable_remote_lights{false};

namespace {

// Candidate prologue of the per-vehicle light update pass. Unverified placeholder.
constexpr char light_signature[] = "48 8B C4 55 57 41 54 41 56 41 57";

// Windows x64 fastcall: the detour receives and forwards the four integer argument slots.
using light_update_fn = std::uintptr_t(WINAPI*)(std::uintptr_t, std::uintptr_t,
                                                std::uintptr_t, std::uintptr_t);

light_update_fn g_original = nullptr;
std::atomic<remote_predicate> g_predicate{nullptr};
std::atomic<unsigned long long> g_calls{0};
std::atomic<unsigned long long> g_skipped{0};
std::atomic<bool> g_located{false};
std::atomic<bool> g_hooked{false};

bool is_remote_vehicle(std::uintptr_t entity) noexcept {
    const remote_predicate predicate = g_predicate.load(std::memory_order_acquire);
    return predicate && entity && predicate(entity);
}

std::uintptr_t WINAPI hkUpdateVehicleLights(std::uintptr_t first, std::uintptr_t second,
                                            std::uintptr_t third, std::uintptr_t fourth) noexcept {
    g_calls.fetch_add(1, std::memory_order_relaxed);
    if (g_disable_remote_lights.load(std::memory_order_relaxed) && is_remote_vehicle(first)) {
        g_skipped.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    return g_original ? g_original(first, second, third, fourth) : 0;
}

void log(const wchar_t* text) noexcept {
    OutputDebugStringW(text);
}

}  // namespace

void set_remote_predicate(remote_predicate predicate) noexcept {
    g_predicate.store(predicate, std::memory_order_release);
}

bool install() noexcept {
    const auto* match = scanner::find_main_code(light_signature);
    g_located.store(match != nullptr, std::memory_order_relaxed);
    if (!match) {
        log(L"renderer_hook: vehicle light signature not found in this build; optimization idle.\n");
        return false;
    }

    const MH_STATUS initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) {
        log(L"renderer_hook: MinHook initialization failed for the light hook.\n");
        return false;
    }
    const MH_STATUS status = MH_CreateHook(const_cast<std::uint8_t*>(match),
        reinterpret_cast<LPVOID>(&hkUpdateVehicleLights), reinterpret_cast<LPVOID*>(&g_original));
    if (status != MH_OK || MH_EnableHook(const_cast<std::uint8_t*>(match)) != MH_OK) {
        log(L"renderer_hook: vehicle light routine could not be hooked.\n");
        g_original = nullptr;
        return false;
    }

    g_hooked.store(true, std::memory_order_relaxed);
    wchar_t message[192];
    const int written = std::swprintf(message, sizeof(message) / sizeof(message[0]),
        L"renderer_hook: vehicle light routine hooked at %p; no entity classifier, calls are forwarded.\n",
        static_cast<const void*>(match));
    if (written > 0) log(message);
    return true;
}

report status() noexcept {
    report values{};
    values.located = g_located.load(std::memory_order_relaxed);
    values.hooked = g_hooked.load(std::memory_order_relaxed);
    values.classifier = g_predicate.load(std::memory_order_relaxed) != nullptr;
    values.calls = g_calls.load(std::memory_order_relaxed);
    values.skipped = g_skipped.load(std::memory_order_relaxed);
    return values;
}

}  // namespace optimization
