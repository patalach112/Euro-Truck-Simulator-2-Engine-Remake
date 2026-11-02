#pragma once

namespace dx_hook {

// Resolves IDXGISwapChain::Present and Present1 from a throwaway swap chain and hooks
// both through MinHook. Returns false when DXGI cannot be initialized.
bool install() noexcept;
unsigned long long frame_count() noexcept;

}  // namespace dx_hook
