#include "core.h"
#include "dx_hook.h"
#include "optimization.h"
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

}  // namespace

void start(HMODULE module) noexcept {
    if (g_started.exchange(true)) return;
    pin(module);
    OutputDebugStringW(L"renderer_hook.dll loaded inside the target process.\n");
    if (!is_game_process()) {
        OutputDebugStringW(L"renderer_hook: target is not eurotrucks2.exe; hooks not installed.\n");
        return;
    }

    const auto match = scanner::find_main_code("E8 ? ? ? ? 48 8B D8");
    wchar_t message[192];
    int written = std::swprintf(message, sizeof(message) / sizeof(message[0]),
        L"renderer_hook: example code signature %ls (%p); no engine markers configured.\n",
        match ? L"found" : L"not found", static_cast<const void*>(match));
    if (written > 0) OutputDebugStringW(message);

    optimization::install();
    if (!dx_hook::install()) {
        OutputDebugStringW(L"renderer_hook: continuing without the render overlay.\n");
    }
    profiler::start();
}

}  // namespace core
