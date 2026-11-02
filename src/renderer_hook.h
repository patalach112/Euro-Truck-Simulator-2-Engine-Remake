#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace renderer_hook {

void log_loaded() noexcept;
DWORD WINAPI initialize(void* parameter) noexcept;

}  // namespace renderer_hook
