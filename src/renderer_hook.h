#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace renderer_hook {

DWORD WINAPI initialize(void* instance) noexcept;

}  // namespace renderer_hook
