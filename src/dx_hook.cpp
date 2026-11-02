#include "dx_hook.h"
#include "allocator.h"
#include "overlay.h"
#include "imgui.h"
#include "imgui_impl_win32.h"

// Declared here per the backend's own instructions; the header keeps it commented out.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message,
                                                             WPARAM wparam, LPARAM lparam);

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <MinHook.h>

#include <atomic>
#include <cstdint>
#include <cwchar>
#include <initializer_list>

namespace dx_hook {
namespace {

using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(WINAPI*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);

constexpr std::size_t present_index = 8;
constexpr std::size_t present1_index = 22;
constexpr std::uint64_t spike_threshold_ns = 33300000;
constexpr wchar_t dummy_class[] = L"renderer_hook_dummy_window";
constexpr GUID iid_id3d11_device = {0xdb6f6ddb, 0xac77, 0x4e88,
    {0x82, 0x53, 0x81, 0x9d, 0xf9, 0xbb, 0xf1, 0x40}};
constexpr GUID iid_id3d11_texture2d = {0x6f15aaf2, 0xd208, 0x4e89,
    {0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c}};

PresentFn g_present = nullptr;
Present1Fn g_present1 = nullptr;
std::atomic<unsigned long long> g_frames{0};
LARGE_INTEGER g_frequency{};
bool g_reported = false;
bool g_device_reported = false;

HWND g_window = nullptr;
WNDPROC g_original_wndproc = nullptr;

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain3* g_chain3 = nullptr;
IDXGISwapChain* g_chain3_owner = nullptr;

thread_local struct {
    LARGE_INTEGER tick{};
    double fps = 0.0;
    double frame_ms = 0.0;
} t_frame;

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

void* method_address(void* object, std::size_t index) noexcept {
    auto** table = *reinterpret_cast<void***>(object);
    return table ? table[index] : nullptr;
}

const char* swap_effect_name(DXGI_SWAP_EFFECT effect) noexcept {
    switch (effect) {
    case DXGI_SWAP_EFFECT_DISCARD: return "discard";
    case DXGI_SWAP_EFFECT_SEQUENTIAL: return "sequential";
    case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL: return "flip sequential";
    case DXGI_SWAP_EFFECT_FLIP_DISCARD: return "flip discard";
    default: return "unknown";
    }
}

struct swap_chain_targets {
    void* present = nullptr;
    void* present1 = nullptr;
};

swap_chain_targets resolve_targets() noexcept {
    swap_chain_targets targets;
    HWND window = create_dummy_window();
    if (!window) return targets;

    DXGI_SWAP_CHAIN_DESC description{};
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
        if (SUCCEEDED(created) && chain) {
            targets.present = method_address(chain, present_index);
            IDXGISwapChain1* chain1 = nullptr;
            if (SUCCEEDED(chain->QueryInterface(IID_IDXGISwapChain1, reinterpret_cast<void**>(&chain1))) && chain1) {
                targets.present1 = method_address(chain1, present1_index);
                chain1->Release();
            }
            chain->Release();
        }
        if (context) context->Release();
        if (device) device->Release();
        if (targets.present) break;
    }
    DestroyWindow(window);
    return targets;
}

void report(const wchar_t* text) noexcept {
    OutputDebugStringW(text);
}

void sample_frame() noexcept {
    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now)) return;
    const auto frame = g_frames.fetch_add(1, std::memory_order_relaxed) + 1;
    const auto previous = t_frame.tick.QuadPart;
    t_frame.tick = now;
    if (previous == 0 || g_frequency.QuadPart <= 0) return;

    const auto delta = static_cast<std::uint64_t>(now.QuadPart - previous);
    const auto nanoseconds = delta * 1000000000ull / static_cast<std::uint64_t>(g_frequency.QuadPart);
    const double milliseconds = static_cast<double>(nanoseconds) / 1000000.0;
    const double instantaneous = milliseconds > 0.0 ? 1000.0 / milliseconds : 0.0;
    t_frame.fps = t_frame.fps > 0.0 ? t_frame.fps * 0.9 + instantaneous * 0.1 : instantaneous;
    t_frame.frame_ms = milliseconds;
    if (nanoseconds <= spike_threshold_ns) return;

    wchar_t message[192];
    const int written = std::swprintf(message, sizeof(message) / sizeof(message[0]),
        L"renderer_hook: frame spike %.2f ms (frame %llu).\n", milliseconds, frame);
    if (written > 0) OutputDebugStringW(message);
}

bool acquire_device(IDXGISwapChain* chain) noexcept {
    ID3D11Device* device = nullptr;
    if (FAILED(chain->GetDevice(iid_id3d11_device, reinterpret_cast<void**>(&device))) || !device) {
        return false;
    }
    if (device != g_device) {
        if (g_context) {
            g_context->Release();
            g_context = nullptr;
        }
        if (g_device) {
            g_device->Release();
            g_device = nullptr;
        }
        device->GetImmediateContext(&g_context);
        g_device = device;
    } else {
        device->Release();
    }
    return g_context != nullptr;
}

unsigned current_buffer(IDXGISwapChain* chain, unsigned buffer_count) noexcept {
    if (g_chain3_owner != chain) {
        if (g_chain3) {
            g_chain3->Release();
            g_chain3 = nullptr;
        }
        g_chain3_owner = chain;
        if (FAILED(chain->QueryInterface(IID_IDXGISwapChain3, reinterpret_cast<void**>(&g_chain3)))) {
            g_chain3 = nullptr;
        }
    }
    if (!g_chain3) return 0;
    const unsigned index = g_chain3->GetCurrentBackBufferIndex();
    return index < buffer_count ? index : 0;
}

// Keyboard messages belong to the menu while it is open; mouse messages are left to the
// engine, which owns the pointer and the camera damping that goes with it.
bool is_keyboard_message(UINT message) noexcept {
    switch (message) {
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_DEADCHAR:
        return true;
    default:
        return false;
    }
}

LRESULT CALLBACK hkWndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    // Insert belongs to the overlay in both modes and never reaches the engine.
    if (message == WM_KEYDOWN && wparam == static_cast<WPARAM>(overlay::toggle_key) &&
        (lparam & 0x40000000) == 0) {
        const bool interactive = !overlay::g_overlay_interactive.load(std::memory_order_relaxed);
        overlay::g_overlay_interactive.store(interactive, std::memory_order_relaxed);
        OutputDebugStringW(interactive
            ? L"renderer_hook: overlay interaction enabled.\n"
            : L"renderer_hook: overlay interaction disabled.\n");
        return 0;
    }

    if (overlay::g_overlay_interactive.load(std::memory_order_relaxed) && overlay::ready() &&
        is_keyboard_message(message)) {
        ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam);
        return 0;
    }

    if (g_original_wndproc) return CallWindowProcW(g_original_wndproc, window, message, wparam, lparam);
    return DefWindowProcW(window, message, wparam, lparam);
}

void subclass_window(HWND window) noexcept {
    if (!window || window == g_window) return;
    if (g_window && g_original_wndproc) {
        SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_original_wndproc));
        g_window = nullptr;
        g_original_wndproc = nullptr;
    }
    const auto previous = SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&hkWndProc));
    if (!previous || previous == reinterpret_cast<LONG_PTR>(&hkWndProc)) {
        OutputDebugStringW(L"renderer_hook: could not subclass the game window.\n");
        return;
    }
    g_window = window;
    g_original_wndproc = reinterpret_cast<WNDPROC>(previous);
    OutputDebugStringW(L"renderer_hook: game window subclassed for overlay input.\n");
}

void draw_overlay(IDXGISwapChain* chain) noexcept {
    if (!acquire_device(chain)) {
        if (!g_device_reported) {
            g_device_reported = true;
            report(L"renderer_hook: no ID3D11Device on the swap chain, overlay idle.\n");
        }
        return;
    }
    DXGI_SWAP_CHAIN_DESC description{};
    if (FAILED(chain->GetDesc(&description))) return;
    subclass_window(description.OutputWindow);
    const unsigned buffer_count = description.BufferCount ? description.BufferCount : 1;
    const unsigned index = current_buffer(chain, buffer_count);

    ID3D11Texture2D* buffer = nullptr;
    if (FAILED(chain->GetBuffer(index, iid_id3d11_texture2d, reinterpret_cast<void**>(&buffer))) || !buffer) return;
    D3D11_TEXTURE2D_DESC texture{};
    buffer->GetDesc(&texture);
    ID3D11RenderTargetView* target = nullptr;
    const HRESULT viewed = g_device->CreateRenderTargetView(buffer, nullptr, &target);
    buffer->Release();
    if (FAILED(viewed) || !target) return;

    overlay::stats values{};
    values.fps = t_frame.fps;
    values.frame_ms = t_frame.frame_ms;
    values.presents = g_frames.load(std::memory_order_relaxed);
    values.back_buffer = index;
    values.buffer_count = buffer_count;
    values.present_mode = swap_effect_name(description.SwapEffect);
    values.window = description.OutputWindow;
    overlay::render(g_device, g_context, target, texture.Width, texture.Height, values);
    target->Release();

    if (!g_reported) {
        g_reported = true;
        wchar_t message[192];
        const int written = std::swprintf(message, sizeof(message) / sizeof(message[0]),
            L"renderer_hook: overlay drawing into buffer %u of %u at %ux%u.\n",
            index, buffer_count, texture.Width, texture.Height);
        if (written > 0) OutputDebugStringW(message);
    }
}

HRESULT WINAPI hkPresent(IDXGISwapChain* chain, UINT interval, UINT flags) noexcept {
    // Unconditional: every present is a frame in which the overlay is redrawn.
    sample_frame();
    draw_overlay(chain);
    // The frame is complete at this point, so the bank that has been idle since the previous
    // frame can be handed back to the allocator.
    allocator::end_frame();
    return g_present ? g_present(chain, interval, flags) : E_FAIL;
}

HRESULT WINAPI hkPresent1(IDXGISwapChain1* chain, UINT interval, UINT flags,
                          const DXGI_PRESENT_PARAMETERS* parameters) noexcept {
    sample_frame();
    draw_overlay(chain);
    allocator::end_frame();
    return g_present1 ? g_present1(chain, interval, flags, parameters) : E_FAIL;
}

}  // namespace

unsigned long long frame_count() noexcept {
    return g_frames.load(std::memory_order_relaxed);
}

bool install() noexcept {
    if (g_present) return true;
    if (!QueryPerformanceFrequency(&g_frequency) || g_frequency.QuadPart <= 0) {
        report(L"renderer_hook: performance counter is unavailable; Present not hooked.\n");
        return false;
    }
    const swap_chain_targets targets = resolve_targets();
    if (!targets.present) {
        report(L"renderer_hook: DXGI swap chain creation failed; Present not hooked.\n");
        return false;
    }

    const MH_STATUS initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) {
        report(L"renderer_hook: MinHook initialization failed; Present not hooked.\n");
        return false;
    }
    MH_STATUS status = MH_CreateHook(targets.present, reinterpret_cast<LPVOID>(&hkPresent),
                                     reinterpret_cast<LPVOID*>(&g_present));
    if (status != MH_OK) {
        report(L"renderer_hook: MinHook could not hook IDXGISwapChain::Present.\n");
        return false;
    }
    if (targets.present1) {
        status = MH_CreateHook(targets.present1, reinterpret_cast<LPVOID>(&hkPresent1),
                               reinterpret_cast<LPVOID*>(&g_present1));
        if (status != MH_OK) {
            report(L"renderer_hook: IDXGISwapChain1::Present1 is unavailable; Present only.\n");
            g_present1 = nullptr;
        }
    }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        report(L"renderer_hook: MinHook could not enable the Present hooks.\n");
        g_present = nullptr;
        g_present1 = nullptr;
        return false;
    }
    report(g_present1 ? L"renderer_hook: Present and Present1 hooked through MinHook.\n"
                      : L"renderer_hook: Present hooked through MinHook.\n");
    return true;
}

}  // namespace dx_hook
