#include "overlay.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"

#include <cwchar>

namespace overlay {
namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
bool g_running = false;
unsigned g_failures = 0;
bool g_skip_reported = false;
bool g_draw_reported = false;

constexpr ImGuiWindowFlags window_flags =
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

void stop() noexcept {
    if (g_running) {
        ImGui_ImplDX11_Shutdown();
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

bool start(ID3D11Device* device, ID3D11DeviceContext* context) noexcept {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // The overlay is display-only: no input backend, no cursor capture, no ini file on disk.
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 4.0f;
    ImGui::GetStyle().WindowBorderSize = 1.0f;
    if (!ImGui_ImplDX11_Init(device, context)) {
        ImGui::DestroyContext();
        return false;
    }
    // The backend builds its shaders lazily; failing here is reported instead of an empty frame.
    if (!ImGui_ImplDX11_CreateDeviceObjects()) {
        ImGui_ImplDX11_Shutdown();
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

void draw(const stats& values) noexcept {
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowCollapsed(false, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.65f);
    if (ImGui::Begin("ETS2 Engine Remake", nullptr, window_flags)) {
        ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.45f, 1.0f), "ETS2 ENGINE REMAKE - CORE ACTIVE");
        ImGui::Separator();
        ImGui::Text("FPS          %.1f", values.fps);
        ImGui::Text("Frame time   %.2f ms", values.frame_ms);
        ImGui::Text("Presents     %llu", values.presents);
        ImGui::Text("Swap effect  %s", values.present_mode);
        ImGui::Text("Back buffer  %u of %u", values.back_buffer, values.buffer_count);
    }
    ImGui::End();
}

}  // namespace

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
        // A partial failure is retried on the next frame instead of silencing the overlay for good.
        if (!start(device, context)) {
            if (++g_failures <= 3) {
                OutputDebugStringW(L"renderer_hook: ImGui Direct3D 11 backend failed to start.\n");
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

    // Without a platform backend ImGui has no window size, clock or input of its own.
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    const float delta = values.frame_ms > 0.0 ? static_cast<float>(values.frame_ms / 1000.0) : 1.0f / 60.0f;
    io.DeltaTime = delta > 0.0f && delta < 1.0f ? delta : 1.0f / 60.0f;

    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    draw(values);
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
