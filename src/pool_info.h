#pragma once

#include "thread_pool.h"

namespace thread_pool {

// The single pool owned by the DLL. Created lazily on first use, never destroyed.
pool* shared() noexcept;

}  // namespace thread_pool
