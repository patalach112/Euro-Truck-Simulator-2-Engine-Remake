#pragma once

#include <cstdint>
#include <string_view>

namespace scanner {

struct result {
    const std::uint8_t* address;
    bool timed_out;
};

// Scans the .text section of the main module once per call. Tokens are hex bytes or ?/??.
// The budget aborts the pass instead of letting a pathological pattern run without bound.
result find_main_code(std::string_view signature, unsigned budget_ms = 2000) noexcept;

}  // namespace scanner
