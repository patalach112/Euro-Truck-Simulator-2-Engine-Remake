#include "dx_hook.h"
#include "hook.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <d3d11.h>
#include <dxgi.h>

#include <atomic>
#include <cstdint>
#include <cwchar>
#include <vector>

namespace dx_hook {
namespace {

using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);

constexpr std::size_t present_index = 8;
constexpr std::uint64_t spike_threshold_ns = 33300000;
constexpr wchar_t dummy_class[] = L"renderer_hook_dummy_window";

hook::InlineHook g_hook;
std::atomic<PresentFn> g_original{nullptr};
std::atomic<unsigned long long> g_frames{0};
LARGE_INTEGER g_frequency{};
thread_local LARGE_INTEGER t_previous_frame{};

using ReleaseFn = ULONG(STDMETHODCALLTYPE*)(IUnknown*);

// COM layout is read through the vtable directly, because MinGW headers expose
// either C-style lpVtbl tables or C++ virtual interfaces depending on the toolchain.
template <typename T>
void* method(T* object, std::size_t index) noexcept {
    auto** table = *reinterpret_cast<void***>(object);
    return table ? table[index] : nullptr;
}

template <typename T>
void release(T* object) noexcept {
    const auto fn = reinterpret_cast<ReleaseFn>(method(object, 2));
    if (fn) fn(reinterpret_cast<IUnknown*>(object));
}

LRESULT CALLBACK dummy_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    return DefWindowProcW(window, message, wparam, lparam);
}

HWND create_dummy_window() noexcept {
    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.lpfnWndProc = dummy_window_proc;
    description.hInstance = GetModuleHandleW(nullptr);
    description.lpszClassName = dummy_class;
    if (!RegisterClassExW(&description)) return nullptr;
    HWND window = CreateWindowExW(0, dummy_class, L"", WS_POPUP, 0, 0, 8, 8, nullptr, nullptr,
                                  description.hInstance, nullptr);
    UnregisterClassW(dummy_class, description.hInstance);
    return window;
}

PresentFn present_from(DXGI_SWAP_CHAIN_DESC& description, HWND window) noexcept {
    description.BufferCount = 1;
    description.BufferDesc.Width = 8;
    description.BufferDesc.Height = 8;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = window;
    description.SampleDesc.Count = 1;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    for (D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        IDXGISwapChain* chain = nullptr;
        const HRESULT created = D3D11CreateDeviceAndSwapChain(nullptr, type, nullptr, 0, levels,
            static_cast<UINT>(sizeof(levels) / sizeof(levels[0])), D3D11_SDK_VERSION, &description,
            &chain, &device, nullptr, &context);
        PresentFn present = nullptr;
        if (SUCCEEDED(created) && chain) present = reinterpret_cast<PresentFn>(method(chain, present_index));
        if (chain) release(chain);
        if (context) release(context);
        if (device) release(device);
        if (present) return present;
    }
    return nullptr;
}

PresentFn resolve_present() noexcept {
    HWND window = create_dummy_window();
    if (!window) return nullptr;
    DXGI_SWAP_CHAIN_DESC description{};
    const PresentFn present = present_from(description, window);
    DestroyWindow(window);
    return present;
}

class parked_threads {
public:
    parked_threads() = default;
    parked_threads(const parked_threads&) = delete;
    parked_threads& operator=(const parked_threads&) = delete;

    ~parked_threads() { release(); }

    // Returns false if the process is already unsafe to patch. Look at unsafe().
    bool park(const void* region, std::size_t size) noexcept {
        const auto process = GetCurrentProcessId();
        const auto self = GetCurrentThreadId();
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        bool completed = Thread32First(snapshot, &entry) != FALSE;
        while (completed) {
            if (entry.th32OwnerProcessID == process && entry.th32ThreadID != self) {
                HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                           THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
                if (!thread) {
                    unsafe_ = true;
                    completed = false;
                    break;
                }
                if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
                    CloseHandle(thread);
                    completed = false;
                    break;
                }
                threads_.push_back(thread);
                CONTEXT context{};
                context.ContextFlags = CONTEXT_CONTROL;
                if (GetThreadContext(thread, &context)) {
                    const auto instruction = reinterpret_cast<std::uintptr_t>(context.Rip);
                    const auto begin = reinterpret_cast<std::uintptr_t>(region);
                    if (instruction >= begin && instruction < begin + size) unsafe_ = true;
                }
            }
            entry.dwSize = sizeof(entry);
            completed = Thread32Next(snapshot, &entry) != FALSE;
        }
        CloseHandle(snapshot);
        if (!completed) {
            unsafe_ = true;
            return false;
        }
        return true;
    }

    bool unsafe() const noexcept { return unsafe_; }

    void release() noexcept {
        for (HANDLE thread : threads_) {
            ResumeThread(thread);
            CloseHandle(thread);
        }
        threads_.clear();
    }

private:
    std::vector<HANDLE> threads_;
    bool unsafe_ = false;
};

void report(const wchar_t* text) noexcept {
    OutputDebugStringW(text);
}

void sample_frame() noexcept {
    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now)) return;
    const auto frame = g_frames.fetch_add(1, std::memory_order_relaxed) + 1;
    const auto previous = t_previous_frame.QuadPart;
    t_previous_frame = now;
    if (previous == 0 || g_frequency.QuadPart <= 0) return;

    const auto delta = static_cast<std::uint64_t>(now.QuadPart - previous);
    const auto nanoseconds = delta * 1000000000ull / static_cast<std::uint64_t>(g_frequency.QuadPart);
    if (nanoseconds <= spike_threshold_ns) return;

    wchar_t message[192];
    const int written = std::swprintf(message, sizeof(message) / sizeof(message[0]),
        L"renderer_hook: frame spike %.2f ms (frame %llu).\n",
        static_cast<double>(nanoseconds) / 1000000.0, frame);
    if (written > 0) OutputDebugStringW(message);
}

HRESULT WINAPI hkPresent(IDXGISwapChain* swap_chain, UINT interval, UINT flags) noexcept {
    const PresentFn original = g_original.load(std::memory_order_acquire);
    if (!original) return E_FAIL;
    sample_frame();
    return original(swap_chain, interval, flags);
}

}  // namespace

unsigned long long frame_count() noexcept {
    return g_frames.load(std::memory_order_relaxed);
}

bool install() noexcept {
    if (g_original.load(std::memory_order_acquire)) return true;
    if (!QueryPerformanceFrequency(&g_frequency) || g_frequency.QuadPart <= 0) {
        report(L"renderer_hook: performance counter is unavailable; Present not hooked.\n");
        return false;
    }
    PresentFn present = resolve_present();
    if (!present) {
        report(L"renderer_hook: DXGI swap chain creation failed; Present not hooked.\n");
        return false;
    }

    try {
        parked_threads parked;
        const void* target = reinterpret_cast<const void*>(present);
        const bool parked_ok = parked.park(target, hook::max_patch_span);
        const bool installed = parked_ok && !parked.unsafe() &&
            g_hook.install(const_cast<void*>(target), reinterpret_cast<void*>(&hkPresent));
        parked.release();
        if (!installed) {
            report(parked.unsafe()
                ? L"renderer_hook: a thread was inside the Present prologue; hook skipped.\n"
                : L"renderer_hook: Present prologue could not be hooked.\n");
            return false;
        }
    } catch (...) {
        report(L"renderer_hook: thread parking failed; Present not hooked.\n");
        return false;
    }

    g_original.store(reinterpret_cast<PresentFn>(g_hook.trampoline()), std::memory_order_release);
    report(L"renderer_hook: IDXGISwapChain::Present hooked.\n");
    return true;
}

}  // namespace dx_hook
