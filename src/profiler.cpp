#include "profiler.h"
#include "dx_hook.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cwchar>

namespace profiler {
namespace {

DWORD WINAPI tick(void*) noexcept {
    LARGE_INTEGER frequency{};
    LARGE_INTEGER now{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart < 60 || !QueryPerformanceCounter(&now)) {
        OutputDebugStringW(L"renderer_hook: profiler timer is unavailable.\n");
        return 1;
    }
    HANDLE timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (!timer) {
        OutputDebugStringW(L"renderer_hook: profiler could not create its timer.\n");
        return 1;
    }

    const auto period = frequency.QuadPart / 60;
    auto deadline = now.QuadPart + period;
    auto last_report = now.QuadPart;
    unsigned ticks = 0;
    for (;;) {
        LARGE_INTEGER due{};
        const auto remaining = deadline - now.QuadPart;
        if (remaining > 0) {
            due.QuadPart = -((remaining / frequency.QuadPart) * 10000000 +
                (remaining % frequency.QuadPart) * 10000000 / frequency.QuadPart);
            if (due.QuadPart == 0) due.QuadPart = -1;
            if (!SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) ||
                WaitForSingleObject(timer, INFINITE) != WAIT_OBJECT_0 || !QueryPerformanceCounter(&now)) break;
        }
        ++ticks;
        if (now.QuadPart - last_report >= frequency.QuadPart) {
            const double seconds = static_cast<double>(now.QuadPart - last_report) / frequency.QuadPart;
            wchar_t message[192];
            const int written = std::swprintf(message, sizeof(message) / sizeof(message[0]),
                L"renderer_hook: profiler %.1f Hz, %llu presents.\n",
                static_cast<double>(ticks) / seconds, dx_hook::frame_count());
            if (written > 0) OutputDebugStringW(message);
            last_report = now.QuadPart;
            ticks = 0;
        }
        deadline += period;
        if (deadline <= now.QuadPart) deadline = now.QuadPart + period;
    }
    CloseHandle(timer);
    OutputDebugStringW(L"renderer_hook: profiler stopped after a timer error.\n");
    return 0;
}

}  // namespace

void start() noexcept {
    HANDLE thread = CreateThread(nullptr, 0, tick, nullptr, 0, nullptr);
    if (!thread) {
        OutputDebugStringW(L"renderer_hook: profiler thread could not be created.\n");
        return;
    }
    CloseHandle(thread);
}

}  // namespace profiler
