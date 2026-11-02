#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace core {

// Idempotent. Called from the injected worker and from the SCS telemetry entry points.
void start(HMODULE module) noexcept;

}  // namespace core
