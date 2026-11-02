#include "hook.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace hook {
namespace {

constexpr std::size_t jump_size = 14;
constexpr std::size_t max_stolen = 64;

struct Instruction {
    std::size_t offset = 0;
    std::size_t length = 0;
    std::size_t displacement = 0;
    bool relative_branch = false;
    bool rip_relative = false;
};

void absolute_jump(std::uint8_t* code, std::uintptr_t destination) {
    const std::uint8_t opcode[] = {0xff, 0x25, 0, 0, 0, 0};
    std::memcpy(code, opcode, sizeof(opcode));
    std::memcpy(code + sizeof(opcode), &destination, sizeof(destination));
}

// Only instructions with known lengths and relocation rules are accepted.
bool decode(const std::uint8_t* code, std::size_t available, Instruction& ins) {
    if (!available) return false;
    if (available >= 4 && code[0] == 0xf3 && code[1] == 0x0f && code[2] == 0x1e && code[3] == 0xfa) {
        ins.length = 4;
        return true;
    }
    std::size_t p = 0;
    std::uint8_t rex = 0;
    if ((code[p] & 0xf0) == 0x40) {
        rex = code[p++];
        if (p >= available) return false;
    }
    const auto op = code[p++];
    if ((op >= 0x50 && op <= 0x5f) || op == 0x90) {
        ins.length = p;
        return true;
    }
    if (op >= 0xb8 && op <= 0xbf) {
        const std::size_t immediate = (rex & 8) ? 8 : 4;
        if (available - p < immediate) return false;
        ins.length = p + immediate;
        return true;
    }
    if (op == 0xe8 || op == 0xe9) {
        if (rex || available - p < 4) return false;
        ins.displacement = p;
        ins.relative_branch = true;
        ins.length = p + 4;
        return true;
    }
    bool modrm = false;
    std::size_t immediate = 0;
    if (op == 0x0f) {
        if (p >= available) return false;
        const auto second = code[p++];
        if (second >= 0x80 && second <= 0x8f) {
            if (rex || available - p < 4) return false;
            ins.displacement = p;
            ins.relative_branch = true;
            ins.length = p + 4;
            return true;
        }
        modrm = second == 0xb6 || second == 0xb7 || second == 0xbe || second == 0xbf || second == 0x1f;
    } else {
        switch (op) {
        case 0x01: case 0x03: case 0x29: case 0x2b: case 0x31: case 0x33:
        case 0x39: case 0x3b: case 0x63: case 0x85: case 0x87: case 0x89:
        case 0x8b: case 0x8d:
            modrm = true;
            break;
        case 0x81: case 0xc7:
            modrm = true;
            immediate = 4;
            break;
        case 0x83:
            modrm = true;
            immediate = 1;
            break;
        default:
            return false;
        }
    }
    if (!modrm || p >= available) return false;
    const auto mr = code[p++];
    const auto mod = mr >> 6;
    const auto reg = (mr >> 3) & 7;
    const auto rm = mr & 7;
    if ((op == 0xc7 && reg != 0) || (op == 0x8d && mod == 3)) return false;
    if (mod != 3 && rm == 4) {
        if (p >= available) return false;
        const auto sib = code[p++];
        if (mod == 0 && (sib & 7) == 5) {
            if (available - p < 4) return false;
            p += 4;
        }
    } else if (mod == 0 && rm == 5) {
        if (available - p < 4) return false;
        ins.displacement = p;
        ins.rip_relative = true;
        p += 4;
    }
    if (mod == 1) {
        if (available - p < 1) return false;
        ++p;
    } else if (mod == 2) {
        if (available - p < 4) return false;
        p += 4;
    }
    if (available - p < immediate || p + immediate > 15) return false;
    ins.length = p + immediate;
    return true;
}

bool same_bytes(const void* address, const std::uint8_t* bytes, std::size_t size) {
    std::uint8_t actual[max_stolen];
    SIZE_T read = 0;
    return size <= sizeof(actual) && ReadProcessMemory(GetCurrentProcess(), address, actual, size, &read) &&
        read == size && std::memcmp(actual, bytes, size) == 0;
}

void* allocate_near(std::uintptr_t target) {
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const auto granularity = static_cast<std::uintptr_t>(system.dwAllocationGranularity);
    const auto center = target - target % granularity;
    constexpr std::uintptr_t limit = 0x7fff0000;
    for (std::uintptr_t distance = 0; distance <= limit; distance += granularity) {
        for (int direction = 0; direction < 2; ++direction) {
            if (direction == 1 && distance == 0) continue;
            if ((direction == 0 && center < distance) ||
                (direction == 1 && center > (std::numeric_limits<std::uintptr_t>::max)() - distance)) continue;
            const auto address = direction == 0 ? center - distance : center + distance;
            MEMORY_BASIC_INFORMATION info{};
            if (VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) != sizeof(info) ||
                info.State != MEM_FREE) continue;
            void* memory = VirtualAlloc(reinterpret_cast<void*>(address), 4096,
                                        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (memory) return memory;
        }
    }
    return nullptr;
}

bool relocate(const std::uint8_t* original, std::uint8_t* copy, std::uintptr_t source,
              std::uintptr_t destination, std::size_t stolen,
              const Instruction* instructions, std::size_t count) {
    std::memcpy(copy, original, stolen);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& ins = instructions[i];
        if (!ins.relative_branch && !ins.rip_relative) continue;
        std::int32_t old_displacement = 0;
        std::memcpy(&old_displacement, original + ins.offset + ins.displacement, sizeof(old_displacement));
        const auto old_end = source + ins.offset + ins.length;
        auto referenced = old_end + static_cast<std::uintptr_t>(static_cast<std::int64_t>(old_displacement));
        if (referenced >= source && referenced < source + stolen) {
            if (!ins.relative_branch) return false;
            const auto relative_offset = referenced - source;
            bool boundary = false;
            for (std::size_t j = 0; j < count; ++j) {
                if (instructions[j].offset == relative_offset) boundary = true;
            }
            if (!boundary) return false;
            referenced = destination + relative_offset;
        }
        const auto new_end = destination + ins.offset + ins.length;
        const auto delta = static_cast<std::int64_t>(referenced) - static_cast<std::int64_t>(new_end);
        if (delta < (std::numeric_limits<std::int32_t>::min)() ||
            delta > (std::numeric_limits<std::int32_t>::max)()) return false;
        const auto displacement = static_cast<std::int32_t>(delta);
        std::memcpy(copy + ins.offset + ins.displacement, &displacement, sizeof(displacement));
    }
    absolute_jump(copy + stolen, source + stolen);
    return true;
}

void restore_protection(void* address, std::size_t length, DWORD protection) {
    DWORD ignored = 0;
    if (!VirtualProtect(address, length, protection, &ignored)) {
        OutputDebugStringW(L"renderer_hook: failed to restore code page protection.\n");
    }
}

}  // namespace

bool InlineHook::install(void* target, void* detour) noexcept {
    if (installed() || !target || !detour || target == detour) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(target, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
    const auto protection = info.Protect & 0xff;
    if (protection != PAGE_EXECUTE_READ && protection != PAGE_EXECUTE_READWRITE &&
        protection != PAGE_EXECUTE_WRITECOPY) return false;
    const auto source = reinterpret_cast<std::uintptr_t>(target);
    const auto region_end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    if (region_end <= source) return false;
    const auto available = (std::min)(static_cast<std::size_t>(region_end - source), jump_size + 15);
    SIZE_T read = 0;
    if (available < jump_size || !ReadProcessMemory(GetCurrentProcess(), target, original_.data(), available, &read) ||
        read != available) return false;

    Instruction instructions[max_stolen]{};
    std::size_t count = 0;
    std::size_t stolen = 0;
    while (stolen < jump_size) {
        auto& ins = instructions[count];
        ins.offset = stolen;
        if (!decode(original_.data() + stolen, available - stolen, ins)) return false;
        stolen += ins.length;
        ++count;
    }

    void* trampoline = allocate_near(source);
    if (!trampoline) return false;
    auto* code = static_cast<std::uint8_t*>(trampoline);
    if (!relocate(original_.data(), code, source, reinterpret_cast<std::uintptr_t>(trampoline),
                  stolen, instructions, count)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(trampoline, stolen + jump_size, PAGE_EXECUTE_READ, &old)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), trampoline, stolen + jump_size);
    std::fill_n(patch_.data(), stolen, static_cast<std::uint8_t>(0x90));
    absolute_jump(patch_.data(), reinterpret_cast<std::uintptr_t>(detour));
    if (!same_bytes(target, original_.data(), stolen) ||
        !VirtualProtect(target, stolen, PAGE_EXECUTE_READWRITE, &old)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    std::memcpy(target, patch_.data(), stolen);
    FlushInstructionCache(GetCurrentProcess(), target, stolen);
    restore_protection(target, stolen, old);
    target_ = target;
    trampoline_ = trampoline;
    length_ = stolen;
    return true;
}

bool InlineHook::remove() noexcept {
    if (!installed() || !same_bytes(target_, patch_.data(), length_)) return false;
    DWORD old = 0;
    if (!VirtualProtect(target_, length_, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(target_, original_.data(), length_);
    FlushInstructionCache(GetCurrentProcess(), target_, length_);
    restore_protection(target_, length_, old);
    VirtualFree(trampoline_, 0, MEM_RELEASE);
    target_ = nullptr;
    trampoline_ = nullptr;
    length_ = 0;
    return true;
}

}  // namespace hook
