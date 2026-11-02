#include "overlay.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "optimization.h"

#include <atomic>
#include <cwchar>

namespace overlay {

std::atomic<bool> g_overlay_interactive{false};

namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
bool g_running = false;
unsigned g_failures = 0;
bool g_skip_reported = false;
bool g_draw_reported = false;
constexpr unsigned max_start_attempts = 5;
bool g_nav_seeded = false;
bool g_was_interactive = false;

constexpr ImGuiWindowFlags window_flags =
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
    ImGuiWindowFlags_NoSavedSettings;

void stop() noexcept {
    if (g_running) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_running = false;
    }
    if (g_context) {
        g_context->Release();
        g_context = nullptr;
    }
    if (g_device) {
        g_device->Release();
        g_device = nullptr;
    }
}

bool start(ID3D11Device* device, ID3D11DeviceContext* context, HWND window) noexcept {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // No configuration file is written next to the game.
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    // Navigation is keyboard-only: the engine owns the pointer and the WndProc layer
    // never lets mouse messages through to the overlay.
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigNavEscapeClearFocusItem = true;
    io.ConfigNavMoveSetMousePos = false;
    io.BackendFlags &= ~ImGuiBackendFlags_HasMouseCursors;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 4.0f;
    ImGui::GetStyle().WindowBorderSize = 1.0f;
    if (!ImGui_ImplWin32_Init(window)) {
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplDX11_Init(device, context)) {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    // The backend builds its shaders lazily; failing here is reported instead of an empty frame.
    if (!ImGui_ImplDX11_CreateDeviceObjects()) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    device->AddRef();
    context->AddRef();
    g_device = device;
    g_context = context;
    g_running = true;
    return true;
}

void draw_optimization(bool interactive) noexcept {
    const optimization::report state = optimization::status();
    ImGui::Separator();
    bool disable = optimization::g_disable_remote_lights.load(std::memory_order_relaxed);
    if (interactive && !g_nav_seeded) {
        // A freshly focused window has no nav item; selecting the checkbox makes the first
        // arrow or space press land on something visible.
        ImGui::SetKeyboardFocusHere();
        g_nav_seeded = true;
    }
    if (ImGui::Checkbox("Optimize Crowd FPS (Disable Remote Lights/Beacons)", &disable)) {
        optimization::g_disable_remote_lights.store(disable, std::memory_order_relaxed);
    }
    if (!state.classifier) {
        ImGui::TextDisabled("no entity classifier: calls are forwarded");
    }
    ImGui::Text("Light calls  %llu", state.calls);
    ImGui::Text("Skipped      %llu", state.skipped);
}

void draw(const stats& values, bool interactive) noexcept {
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowCollapsed(false, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.65f);
    if (interactive) {
        // Outside interaction the window is inert and must not hold focus, otherwise its
        // nav state would swallow arrow keys meant for the truck.
        ImGui::SetNextWindowFocus();
    }
    ImGuiWindowFlags flags = window_flags;
    if (!interactive) flags |= ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("ETS2 Engine Remake", nullptr, flags)) {
        ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.45f, 1.0f), "ETS2 ENGINE REMAKE - CORE ACTIVE");
        ImGui::Separator();
        ImGui::Text("FPS          %.1f", values.fps);
        ImGui::Text("Frame time   %.2f ms", values.frame_ms);
        ImGui::Text("Presents     %llu", values.presents);
        ImGui::Text("Swap effect  %s", values.present_mode);
        ImGui::Text("Back buffer  %u of %u", values.back_buffer, values.buffer_count);
        draw_optimization(interactive);
        ImGui::TextDisabled(interactive ? "Arrows navigate, Space or Enter toggles, Insert closes"
                                        : "Insert: keyboard navigation");
    }
    ImGui::End();
}

}  // namespace

bool ready() noexcept {
    return g_running;
}

bool render(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
            unsigned width, unsigned height, const stats& values) noexcept {
    if (!device || !context || !target || !width || !height) {
        if (!g_skip_reported) {
            g_skip_reported = true;
            OutputDebugStringW(L"renderer_hook: overlay skipped, the swap chain has no drawable target.\n");
        }
        return false;
    }
    if (g_device != device) {
        stop();
        // Retried a bounded number of times: re-creating the backend every present would
        // rebuild the font atlas and shaders at frame rate.
        if (g_failures >= max_start_attempts) return false;
        if (!start(device, context, values.window)) {
            if (++g_failures == 1) {
                OutputDebugStringW(L"renderer_hook: ImGui Direct3D 11 backend failed to start.\n");
            }
            if (g_failures == max_start_attempts) {
                OutputDebugStringW(L"renderer_hook: overlay disabled after repeated backend failures.\n");
            }
            return false;
        }
        g_failures = 0;
    }

    ID3D11RenderTargetView* previous_targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView* previous_depth = nullptr;
    context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, previous_targets, &previous_depth);
    UINT previous_viewports = 1;
    D3D11_VIEWPORT previous_viewport{};
    context->RSGetViewports(&previous_viewports, &previous_viewport);

    D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    context->OMSetRenderTargets(1, &target, nullptr);
    context->RSSetViewports(1, &viewport);

    const bool interactive = g_overlay_interactive.load(std::memory_order_relaxed);
    if (interactive != g_was_interactive) {
        g_was_interactive = interactive;
        g_nav_seeded = false;
        if (!interactive) ImGui::GetIO().ClearInputKeys();
    }

    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplWin32_NewFrame();
    // The back buffer is authoritative for the draw scale, whatever the window reports.
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    const float delta = values.frame_ms > 0.0 ? static_cast<float>(values.frame_ms / 1000.0) : 1.0f / 60.0f;
    io.DeltaTime = delta > 0.0f && delta < 1.0f ? delta : 1.0f / 60.0f;
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    // Dropping focus keeps nav state from consuming arrow keys while the overlay is passive.
    if (!interactive) ImGui::SetWindowFocus(nullptr);
    draw(values, interactive);
    ImGui::Render();

    ImDrawData* commands = ImGui::GetDrawData();
    if (!g_draw_reported) {
        g_draw_reported = true;
        const unsigned vertices = commands ? static_cast<unsigned>(commands->TotalVtxCount) : 0u;
        const unsigned lists = commands ? static_cast<unsigned>(commands->CmdListsCount) : 0u;
        wchar_t message[160];
        const int written = std::swprintf(message, sizeof(message) / sizeof(message[0]),
            L"renderer_hook: ImGui issued %u vertices in %u lists.\n", vertices, lists);
        if (written > 0) OutputDebugStringW(message);
    }
    ImGui_ImplDX11_RenderDrawData(commands);

    context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, previous_targets, previous_depth);
    if (previous_viewports) context->RSSetViewports(previous_viewports, &previous_viewport);
    for (auto* previous : previous_targets) {
        if (previous) previous->Release();
    }
    if (previous_depth) previous_depth->Release();
    return true;
}

}  // namespace overlay
