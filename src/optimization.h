#pragma once

#include <atomic>
#include <cstdint>

namespace optimization {

// Set from the overlay checkbox and read on the render thread; relaxed ordering is enough
// because the value only decides whether a call is skipped, never which memory is touched.
extern std::atomic<bool> g_disable_remote_lights;

struct report {
    bool located;
    bool hooked;
    bool classifier;
    unsigned long long calls;
    unsigned long long skipped;
};

// Locates the candidate light update routine through a code signature and detours it.
bool install() noexcept;
report status() noexcept;

// A build-specific entity classifier has to be supplied before any call is skipped.
// Returning false means the detour always forwards to the original function.
using remote_predicate = bool (*)(std::uintptr_t entity) noexcept;
void set_remote_predicate(remote_predicate predicate) noexcept;

}  // namespace optimization
