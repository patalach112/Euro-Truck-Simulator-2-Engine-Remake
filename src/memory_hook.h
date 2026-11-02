#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace memory_hook {

struct report {
    bool allocate_located;
    bool free_located;
    bool hooked;
    bool redirecting;
    std::uint64_t served;
    std::uint64_t rejected;
    std::uint64_t freed;
};

// Hooks the candidate allocate/free pair and binds the arena. Redirecting stays off until
// both entries are hooked, because freeing arena memory through the engine heap would fault.
bool install(std::size_t arena_capacity) noexcept;

// Refused while either entry point is missing from this build.
bool set_redirect_enabled(bool enabled) noexcept;

report status() noexcept;

}  // namespace memory_hook
