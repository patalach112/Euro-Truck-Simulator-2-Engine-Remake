#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi.h>

namespace overlay {

// Draws the diagnostic panel onto the swap chain back buffer. Returns false when
// the swap chain is not backed by an ID3D11Device or the panel does not fit.
bool render(IDXGISwapChain* swap_chain, double fps, double frame_ms) noexcept;

}  // namespace overlay
