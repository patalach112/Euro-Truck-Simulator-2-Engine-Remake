#include "core.h"
#include "dx_hook.h"
#include "engine_core.h"
#include "memory_hook.h"
#include "optimization.h"
#include "pool_info.h"
#include "profiler.h"
#include "scanner.h"

#include <atomic>
#include <cwchar>

namespace core {
namespace {

std::atomic<bool> g_started{false};

bool is_game_process() noexcept {
    wchar_t path[32768];
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(sizeof(path) / sizeof(path[0])));
    if (length == 0 || length >= sizeof(path) / sizeof(path[0])) return false;
    const wchar_t* name = path;
    for (const wchar_t* cursor = path; *cursor; ++cursor) {
        if (*cursor == L'\\' || *cursor == L'/') name = cursor + 1;
    }
    return _wcsicmp(name, L"eurotrucks2.exe") == 0;
}

void pin(HMODULE module) noexcept {
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(module), &pinned)) {
        OutputDebugStringW(L"renderer_hook: could not pin the DLL; hooks not installed.\n");
    }
}

// Runs once at the lowest priority and exits; nothing here touches the present path.
DWORD WINAPI scan_thread(void*) noexcept {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    const scanner::result scan = scanner::find_main_code("E8 ? ? ? ? 48 8B D8");
    wchar_t message[192];
    const int written = std::swprintf(message, sizeof(message) / sizeof(message[0]),
        L"renderer_hook: example code signature %ls (%p)%ls.\n",
        scan.address ? L"found" : L"not found", static_cast<const void*>(scan.address),
        scan.timed_out ? L" within the time budget" : L"");
    if (written > 0) OutputDebugStringW(message);
    optimization::install();
    engine_core::install(thread_pool::shared());
    memory_hook::install(256u * 1024u * 1024u);
    return 0;
}

}  // namespace

void start(HMODULE module) noexcept {
    if (g_started.exchange(true)) return;
    pin(module);
    OutputDebugStringW(L"renderer_hook.dll loaded inside the target process.\n");
    if (!is_game_process()) {
        OutputDebugStringW(L"renderer_hook: target is not eurotrucks2.exe; hooks not installed.\n");
        return;
    }

    if (!dx_hook::install()) {
        OutputDebugStringW(L"renderer_hook: continuing without the render overlay.\n");
    }
    profiler::start();

    HANDLE scan = CreateThread(nullptr, 0, scan_thread, nullptr, 0, nullptr);
    if (!scan) {
        OutputDebugStringW(L"renderer_hook: scan thread could not be created.\n");
        return;
    }
    CloseHandle(scan);
}

}  // namespace core
