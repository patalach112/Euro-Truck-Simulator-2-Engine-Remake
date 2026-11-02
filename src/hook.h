#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace hook {

// The caller must keep all threads out of the target, detour and trampoline while
// installing or removing a hook. This object must outlive the installed hook.
// Upper bound of the bytes an installation may replace. Callers that park threads
// over a shared entry point use this to decide which instruction pointers are unsafe.
inline constexpr std::size_t max_patch_span = 32;

class InlineHook {
public:
    InlineHook() = default;
    InlineHook(const InlineHook&) = delete;
    InlineHook& operator=(const InlineHook&) = delete;

    bool install(void* target, void* detour) noexcept;
    bool remove() noexcept;
    void* trampoline() const noexcept { return trampoline_; }
    bool installed() const noexcept { return target_ != nullptr; }

private:
    void* target_ = nullptr;
    void* trampoline_ = nullptr;
    std::size_t length_ = 0;
    std::array<std::uint8_t, 64> original_{};
    std::array<std::uint8_t, 64> patch_{};
};

}  // namespace hook
