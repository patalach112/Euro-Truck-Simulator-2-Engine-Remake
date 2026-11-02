#include "renderer_hook.h"
#include "scanner.h"

#include <cstdint>
#include <cwchar>

namespace renderer_hook {
namespace {

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

void profile() noexcept {
    LARGE_INTEGER frequency{};
    LARGE_INTEGER now{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart < 60 || !QueryPerformanceCounter(&now)) {
        OutputDebugStringW(L"renderer_hook: performance counter is unavailable.\n");
        return;
    }
    HANDLE timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (!timer) {
        OutputDebugStringW(L"renderer_hook: could not create the profiler timer.\n");
        return;
    }

    const auto period = frequency.QuadPart / 60;
    auto deadline = now.QuadPart + period;
    auto last_report = now.QuadPart;
    unsigned ticks = 0;
    for (;;) {
        if (!QueryPerformanceCounter(&now)) break;
        if (now.QuadPart < deadline) {
            const auto remaining = deadline - now.QuadPart;
            LARGE_INTEGER due{};
            due.QuadPart = -((remaining / frequency.QuadPart) * 10000000 +
                (remaining % frequency.QuadPart) * 10000000 / frequency.QuadPart);
            if (due.QuadPart == 0) due.QuadPart = -1;
            if (!SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) ||
                WaitForSingleObject(timer, INFINITE) != WAIT_OBJECT_0 || !QueryPerformanceCounter(&now)) break;
        }
        ++ticks;
        if (now.QuadPart - last_report >= frequency.QuadPart) {
            const double seconds = static_cast<double>(now.QuadPart - last_report) / frequency.QuadPart;
            wchar_t message[256];
            const int written = std::swprintf(message, sizeof(message) / sizeof(message[0]),
                L"renderer_hook: profiler %.1f Hz; simulated frame 16.67 ms, draw calls 0, wait states 0.\n",
                static_cast<double>(ticks) / seconds);
            if (written > 0) OutputDebugStringW(message);
            last_report = now.QuadPart;
            ticks = 0;
        }
        deadline += period;
        if (deadline <= now.QuadPart) deadline = now.QuadPart + period;
    }
    CloseHandle(timer);
    OutputDebugStringW(L"renderer_hook: profiler stopped after a timer error.\n");
}

}  // namespace

DWORD WINAPI initialize(void* instance) noexcept {
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(instance), &pinned)) {
        OutputDebugStringW(L"renderer_hook: could not pin the DLL; profiler not started.\n");
        return 1;
    }
    OutputDebugStringW(L"renderer_hook.dll loaded inside the target process.\n");
    if (!is_game_process()) {
        OutputDebugStringW(L"renderer_hook: target is not eurotrucks2.exe; profiler not started.\n");
        return 1;
    }

    try {
        // This example signature is not a verified engine symbol and is never dereferenced.
        const auto match = scanner::find_main_code("E8 ? ? ? ? 48 8B D8");
        wchar_t message[192];
        const int written = std::swprintf(message, sizeof(message) / sizeof(message[0]),
            L"renderer_hook: example code signature %ls (%p); no engine markers configured.\n",
            match ? L"found" : L"not found", static_cast<const void*>(match));
        if (written > 0) OutputDebugStringW(message);
    } catch (...) {
        OutputDebugStringW(L"renderer_hook: signature scan failed; continuing with simulated telemetry.\n");
    }
    profile();
    return 0;
}

}  // namespace renderer_hook

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(nullptr, 0, renderer_hook::initialize, instance, 0, nullptr);
        if (!thread) return FALSE;
        CloseHandle(thread);
    }
    return TRUE;
}
