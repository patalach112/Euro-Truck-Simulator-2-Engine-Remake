#pragma once

#include "thread_pool.h"

#include <atomic>
#include <cstdint>

namespace engine_core {

// One entry of the per-frame entity array, as described by a build-specific adapter.
struct update_view {
    void* items;
    unsigned count;
    unsigned stride;
    void (*step)(void* items, unsigned index, void* context);
    void* step_context;
};

// The engine loop has an unknown argument layout, so the adapter is the only place that knows
// how to find the entity array and how to advance one entity. Without it the detour forwards.
using adapter = bool (*)(std::uintptr_t first_argument, update_view& view) noexcept;

struct report {
    bool located;
    bool hooked;
    bool adapted;
    unsigned workers;
    std::uint64_t updates;
    std::uint64_t items;
    std::uint64_t dispatches;
};

bool install(thread_pool::pool* pool) noexcept;
void set_adapter(adapter fn) noexcept;
bool set_parallel_enabled(bool enabled) noexcept;
report status() noexcept;

}  // namespace engine_core
