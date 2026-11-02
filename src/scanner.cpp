#include "scanner.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace scanner {
namespace {

constexpr std::size_t chunk_size = 64 * 1024;
constexpr std::size_t max_pattern = 4096;

struct byte {
    std::uint8_t value;
    bool wildcard;
};

struct extent {
    std::uintptr_t image;
    std::uintptr_t begin;
    std::uintptr_t end;
};

struct deadline {
    LARGE_INTEGER frequency{};
    LARGE_INTEGER start{};
    LARGE_INTEGER ticks{};
    bool valid = false;

    explicit deadline(unsigned milliseconds) noexcept {
        if (!QueryPerformanceFrequency(&frequency) || !frequency.QuadPart) return;
        valid = QueryPerformanceCounter(&start) != FALSE;
        if (valid) ticks.QuadPart = static_cast<LONGLONG>(milliseconds) * frequency.QuadPart / 1000;
    }

    bool expired() noexcept {
        if (!valid) return false;
        LARGE_INTEGER now{};
        if (!QueryPerformanceCounter(&now)) return true;
        return now.QuadPart - start.QuadPart >= ticks.QuadPart;
    }
};

bool readable(DWORD protection) {
    if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
    switch (protection & 0xff) {
    case PAGE_READONLY:
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return true;
    default:
        return false;
    }
}

bool read_image(std::uintptr_t address, void* destination, std::size_t size,
                std::uintptr_t image_base) {
    if (size > (std::numeric_limits<std::uintptr_t>::max)() - address) return false;
    const auto end = address + size;
    for (auto cursor = address; cursor < end;) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) != sizeof(info) ||
            info.State != MEM_COMMIT || info.Type != MEM_IMAGE ||
            reinterpret_cast<std::uintptr_t>(info.AllocationBase) != image_base || !readable(info.Protect)) {
            return false;
        }
        const auto region_end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (region_end <= cursor) return false;
        cursor = (std::min)(end, region_end);
    }
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), destination,
                             size, &copied) && copied == size;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parse(std::string_view text, std::vector<byte>& pattern) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) ++pos;
        if (pos == text.size()) break;
        const auto start = pos;
        while (pos < text.size() && text[pos] != ' ' && text[pos] != '\t') ++pos;
        const auto token = text.substr(start, pos - start);
        if (token == "?" || token == "??") {
            pattern.push_back({0, true});
        } else if (token.size() == 2 && hex_digit(token[0]) >= 0 && hex_digit(token[1]) >= 0) {
            pattern.push_back({static_cast<std::uint8_t>(hex_digit(token[0]) * 16 + hex_digit(token[1])), false});
        } else {
            return false;
        }
        if (pattern.size() > max_pattern) return false;
    }
    return !pattern.empty();
}

// The .text extent is resolved once and reused; the header is not re-read per signature.
bool text_extent(extent& out) noexcept {
    static extent cached{};
    static bool resolved = false;
    static bool valid = false;
    if (resolved) {
        out = cached;
        return valid;
    }
    resolved = true;

    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (!base) return false;
    IMAGE_DOS_HEADER dos{};
    if (!read_image(base, &dos, sizeof(dos), base) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < 0 || dos.e_lfanew > 1024 * 1024) return false;
    IMAGE_NT_HEADERS64 nt{};
    const auto nt_offset = static_cast<std::uintptr_t>(dos.e_lfanew);
    if (nt_offset > (std::numeric_limits<std::uintptr_t>::max)() - base ||
        !read_image(base + nt_offset, &nt, sizeof(nt), base) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
        nt.FileHeader.NumberOfSections > 96 || nt.OptionalHeader.SizeOfImage == 0) return false;

    const auto table = nt_offset + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
    const auto table_size = static_cast<std::uintptr_t>(nt.FileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
    if (table > nt.OptionalHeader.SizeOfImage || table_size > nt.OptionalHeader.SizeOfImage - table) return false;

    IMAGE_SECTION_HEADER fallback{};
    bool have_fallback = false;
    for (WORD i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        if (!read_image(base + table + i * sizeof(section), &section, sizeof(section), base)) return false;
        if ((section.Characteristics & (IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE)) !=
            (IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE)) continue;
        if (std::memcmp(section.Name, ".text", 5) == 0) {
            cached.begin = base + section.VirtualAddress;
            cached.end = cached.begin + (section.Misc.VirtualSize ? section.Misc.VirtualSize : section.SizeOfRawData);
            cached.image = base;
            valid = cached.end > cached.begin;
            out = cached;
            return valid;
        }
        if (!have_fallback) {
            fallback = section;
            have_fallback = true;
        }
    }
    if (!have_fallback) return false;
    cached.image = base;
    cached.begin = base + fallback.VirtualAddress;
    cached.end = cached.begin + (fallback.Misc.VirtualSize ? fallback.Misc.VirtualSize : fallback.SizeOfRawData);
    valid = cached.end > cached.begin;
    out = cached;
    return valid;
}

const std::uint8_t* search_range(std::uintptr_t begin, std::uintptr_t end, std::uintptr_t image_base,
                                 const std::vector<byte>& pattern, deadline& limit, bool& exhausted) {
    if (end - begin < pattern.size()) return nullptr;

    std::size_t anchor = pattern.size();
    while (anchor > 0 && pattern[anchor - 1].wildcard) --anchor;
    if (anchor == 0) return reinterpret_cast<const std::uint8_t*>(begin);
    --anchor;

    std::array<std::size_t, 256> shifts;
    shifts.fill(anchor + 1);
    for (std::size_t i = 0; i < anchor; ++i) {
        const auto distance = anchor - i;
        if (pattern[i].wildcard) {
            for (auto& shift : shifts) shift = (std::min)(shift, distance);
        } else {
            shifts[pattern[i].value] = (std::min)(shifts[pattern[i].value], distance);
        }
    }

    std::vector<std::uint8_t> buffer(chunk_size + pattern.size() - 1);
    for (auto cursor = begin; cursor <= end - pattern.size();) {
        if (limit.expired()) {
            exhausted = true;
            return nullptr;
        }
        const auto count = static_cast<std::size_t>((std::min)(end - cursor,
            static_cast<std::uintptr_t>(buffer.size())));
        if (!read_image(cursor, buffer.data(), count, image_base)) return nullptr;
        const auto candidates = count - pattern.size() + 1;
        for (std::size_t i = 0; i < candidates;) {
            const auto value = buffer[i + anchor];
            if (value == pattern[anchor].value) {
                std::size_t j = 0;
                while (j < pattern.size() && (pattern[j].wildcard || pattern[j].value == buffer[i + j])) ++j;
                if (j == pattern.size()) return reinterpret_cast<const std::uint8_t*>(cursor + i);
            }
            i += shifts[value];
        }
        cursor += candidates;
    }
    return nullptr;
}

}  // namespace

result find_main_code(std::string_view signature, unsigned budget_ms) noexcept {
    std::vector<byte> pattern;
    if (!parse(signature, pattern)) return {nullptr, false};

    extent range{};
    if (!text_extent(range)) return {nullptr, false};

    deadline limit(budget_ms);
    bool exhausted = false;
    for (auto cursor = range.begin; cursor < range.end;) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) != sizeof(info)) break;
        const auto region_end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (region_end <= cursor) break;
        const auto next = (std::min)(range.end, region_end);
        const bool can_read = info.State == MEM_COMMIT && info.Type == MEM_IMAGE &&
            reinterpret_cast<std::uintptr_t>(info.AllocationBase) == range.image && readable(info.Protect);
        if (can_read) {
            if (auto match = search_range(cursor, next, range.image, pattern, limit, exhausted)) {
                return {match, false};
            }
            if (exhausted) return {nullptr, true};
        }
        cursor = next;
    }
    return {nullptr, exhausted};
}

}  // namespace scanner
