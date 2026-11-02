#pragma once

#include <cstdint>
#include <string_view>

namespace scanner {

// Searches executable code sections of the main module. Tokens are hex bytes or ?/??.
const std::uint8_t* find_main_code(std::string_view signature);

}  // namespace scanner
