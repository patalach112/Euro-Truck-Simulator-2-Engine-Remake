#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>

#include <atomic>

namespace overlay {

// Flipped by Insert; while set, the window takes mouse input and draws a software cursor.
extern std::atomic<bool> g_overlay_interactive;

// True once the ImGui context and both backends are alive; the window procedure checks it
// before forwarding messages, because ImGui asserts on input without a context.
bool ready() noexcept;

constexpr int toggle_key = VK_INSERT;

struct stats {
    double fps;
    double frame_ms;
    unsigned long long presents;
    unsigned back_buffer;
    unsigned buffer_count;
    const char* present_mode;
    HWND window;
};

// Draws the telemetry window onto the given render target. Render thread only.
bool render(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
            unsigned width, unsigned height, const stats& values) noexcept;

}  // namespace overlay
