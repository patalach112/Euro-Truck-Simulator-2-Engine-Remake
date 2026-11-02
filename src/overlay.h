#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>

namespace overlay {

struct stats {
    double fps;
    double frame_ms;
    unsigned long long presents;
    unsigned back_buffer;
    unsigned buffer_count;
    const char* present_mode;
};

// Draws the telemetry window onto the given render target. Render thread only.
bool render(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
            unsigned width, unsigned height, const stats& values) noexcept;

}  // namespace overlay
