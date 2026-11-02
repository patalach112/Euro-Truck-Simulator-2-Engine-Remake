#pragma once

namespace dx_hook {

// Resolves IDXGISwapChain::Present through a temporary device and detours it.
// Returns false when DXGI is unavailable; the caller then continues unhooked.
bool install() noexcept;
unsigned long long frame_count() noexcept;

}  // namespace dx_hook
