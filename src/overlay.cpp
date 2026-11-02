#include "overlay.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"

namespace overlay {
namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
bool g_running = false;
bool g_failed = false;

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
    ImGui::SetNextWindowBgAlpha(0.65f);
    if (ImGui::Begin("ETS2 Engine Remake", nullptr, window_flags)) {
        ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.45f, 1.0f), "CORE ACTIVE");
        ImGui::Separator();
        ImGui::Text("Frame rate   %.1f fps", values.fps);
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
    if (!device || !context || !target || !width || !height || g_failed) return false;
    if (g_device != device) {
        stop();
        if (!start(device, context)) {
            g_failed = true;
            OutputDebugStringW(L"renderer_hook: ImGui Direct3D 11 backend failed to start.\n");
            return false;
        }
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

    // Without a platform backend ImGui has no window size or clock of its own.
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DeltaTime = values.frame_ms > 0.0 ? static_cast<float>(values.frame_ms / 1000.0) : 1.0f / 60.0f;

    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    draw(values);
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, previous_targets, previous_depth);
    if (previous_viewports) context->RSSetViewports(previous_viewports, &previous_viewport);
    for (auto* previous : previous_targets) {
        if (previous) previous->Release();
    }
    if (previous_depth) previous_depth->Release();
    return true;
}

}  // namespace overlay
