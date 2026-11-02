#include "overlay.h"

#include <d3d11.h>

#include <cstdint>
#include <cstring>

namespace overlay {
namespace {

constexpr int panel_x = 16;
constexpr int panel_y = 16;
constexpr int panel_width = 396;
constexpr int panel_height = 64;
constexpr int glyph_width = 5;
constexpr int glyph_height = 7;
constexpr int glyph_scale = 2;
constexpr int advance = 12;
constexpr int line_height = 20;
constexpr int text_x = 12;
constexpr int text_y = 10;
constexpr std::uint32_t background = 0xe0141414;
constexpr std::uint32_t foreground = 0xff39ff6a;

struct glyph {
    wchar_t code;
    std::uint8_t rows[glyph_height];
};

const glyph g_glyphs[] = {
    {L'A', {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}},
    {L'B', {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110}},
    {L'C', {0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110}},
    {L'D', {0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110}},
    {L'E', {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111}},
    {L'F', {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000}},
    {L'G', {0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111}},
    {L'H', {0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}},
    {L'I', {0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}},
    {L'J', {0b00111, 0b00010, 0b00010, 0b00010, 0b00010, 0b10010, 0b01100}},
    {L'K', {0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001}},
    {L'L', {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111}},
    {L'M', {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001}},
    {L'N', {0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001}},
    {L'O', {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}},
    {L'P', {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000}},
    {L'Q', {0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101}},
    {L'R', {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001}},
    {L'S', {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110}},
    {L'T', {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100}},
    {L'U', {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}},
    {L'V', {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100}},
    {L'W', {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b11011, 0b10001}},
    {L'X', {0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001}},
    {L'Y', {0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100}},
    {L'Z', {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111}},
    {L'0', {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110}},
    {L'1', {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}},
    {L'2', {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111}},
    {L'3', {0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110}},
    {L'4', {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}},
    {L'5', {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110}},
    {L'6', {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110}},
    {L'7', {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}},
    {L'8', {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}},
    {L'9', {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100}},
    {L'-', {0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000, 0b00000}},
    {L'.', {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00110, 0b00110}},
    {L':', {0b00000, 0b00110, 0b00110, 0b00000, 0b00110, 0b00110, 0b00000}},
    {L'/', {0b00001, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b10000}},
};

std::uint32_t g_pixels[panel_width * panel_height];

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
ID3D11Texture2D* g_texture = nullptr;
DXGI_FORMAT g_texture_format = DXGI_FORMAT_UNKNOWN;
bool g_bgra = true;
bool g_unavailable = false;

constexpr GUID iid_id3d11_device = {0xdb6f6ddb, 0xac77, 0x4e88,
    {0x82, 0x53, 0x81, 0x9d, 0xf9, 0xbb, 0xf1, 0x40}};
constexpr GUID iid_id3d11_texture2d = {0x6f15aaf2, 0xd208, 0x4e89,
    {0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c}};

using get_device_fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, const GUID*, void**);
using get_buffer_fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, const GUID*, void**);
using get_desc_fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, DXGI_SWAP_CHAIN_DESC*);

template <typename T>
T com_method(IDXGISwapChain* chain, std::size_t index) noexcept {
    auto** table = *reinterpret_cast<void***>(chain);
    return table ? reinterpret_cast<T>(table[index]) : nullptr;
}

void fill(int x, int y, int width, int height, std::uint32_t color) noexcept {
    const int left = x < 0 ? 0 : x;
    const int top = y < 0 ? 0 : y;
    const int right = x + width > panel_width ? panel_width : x + width;
    const int bottom = y + height > panel_height ? panel_height : y + height;
    for (int row = top; row < bottom; ++row) {
        std::uint32_t* target = g_pixels + row * panel_width;
        for (int column = left; column < right; ++column) target[column] = color;
    }
}

void draw_text(const wchar_t* text, int x, int y) noexcept {
    for (; *text; ++text, x += advance) {
        wchar_t code = *text;
        if (code >= L'a' && code <= L'z') code = static_cast<wchar_t>(code - 32);
        if (code == L' ') continue;
        for (const auto& candidate : g_glyphs) {
            if (candidate.code != code) continue;
            for (int row = 0; row < glyph_height; ++row) {
                for (int column = 0; column < glyph_width; ++column) {
                    if (!(candidate.rows[row] & (1 << (glyph_width - 1 - column)))) continue;
                    fill(x + column * glyph_scale, y + row * glyph_scale, glyph_scale, glyph_scale,
                         foreground);
                }
            }
            break;
        }
    }
}

int append(wchar_t* target, int length, int capacity, const wchar_t* text) noexcept {
    for (; *text && length < capacity; ++text) target[length++] = *text;
    return length;
}

int append_unsigned(wchar_t* target, int length, int capacity, unsigned value) noexcept {
    wchar_t digits[16];
    int count = 0;
    do {
        digits[count++] = static_cast<wchar_t>(L'0' + value % 10);
        value /= 10;
    } while (value && count < 16);
    while (count > 0 && length < capacity) target[length++] = digits[--count];
    return length;
}

int append_hundredths(wchar_t* target, int length, int capacity, unsigned hundredths) noexcept {
    length = append_unsigned(target, length, capacity, hundredths / 100);
    if (length < capacity) target[length++] = L'.';
    if (length < capacity) target[length++] = static_cast<wchar_t>(L'0' + hundredths / 10 % 10);
    if (length < capacity) target[length++] = static_cast<wchar_t>(L'0' + hundredths % 10);
    return length;
}

bool create_texture(DXGI_FORMAT format) noexcept {
    if (g_texture) {
        g_texture->Release();
        g_texture = nullptr;
    }
    D3D11_TEXTURE2D_DESC description{};
    description.Width = panel_width;
    description.Height = panel_height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = format;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(g_device->CreateTexture2D(&description, nullptr, &g_texture))) {
        g_texture = nullptr;
        g_unavailable = true;
        OutputDebugStringW(L"renderer_hook: overlay texture creation failed.\n");
        return false;
    }
    g_texture_format = format;
    return true;
}

bool initialize(IDXGISwapChain* chain, const DXGI_SWAP_CHAIN_DESC& description) noexcept {
    if (g_unavailable) return false;
    const auto format = description.BufferDesc.Format;
    if (format != DXGI_FORMAT_B8G8R8A8_UNORM && format != DXGI_FORMAT_R8G8B8A8_UNORM) {
        g_unavailable = true;
        OutputDebugStringW(L"renderer_hook: unsupported back buffer format; overlay disabled.\n");
        return false;
    }
    g_bgra = format == DXGI_FORMAT_B8G8R8A8_UNORM;
    const auto get_device = com_method<get_device_fn>(chain, 7);
    if (!get_device ||
        FAILED(get_device(chain, &iid_id3d11_device, reinterpret_cast<void**>(&g_device))) || !g_device) {
        g_unavailable = true;
        OutputDebugStringW(L"renderer_hook: swap chain has no ID3D11Device; overlay disabled.\n");
        return false;
    }
    g_device->GetImmediateContext(&g_context);
    if (!g_context) {
        g_unavailable = true;
        OutputDebugStringW(L"renderer_hook: no immediate context; overlay disabled.\n");
        return false;
    }
    return create_texture(format);
}

void forget_device() noexcept {
    if (g_texture) {
        g_texture->Release();
        g_texture = nullptr;
    }
    if (g_context) {
        g_context->Release();
        g_context = nullptr;
    }
    if (g_device) {
        g_device->Release();
        g_device = nullptr;
    }
    g_texture_format = DXGI_FORMAT_UNKNOWN;
}

std::uint32_t encode(std::uint32_t color) noexcept {
    if (g_bgra) return color;
    return (color & 0xff00ff00) | ((color & 0xff) << 16) | ((color >> 16) & 0xff);
}

}  // namespace

bool render(IDXGISwapChain* swap_chain, double fps, double frame_ms) noexcept {
    if (!swap_chain || g_unavailable) return false;
    const auto get_desc = com_method<get_desc_fn>(swap_chain, 12);
    if (!get_desc) return false;
    DXGI_SWAP_CHAIN_DESC description{};
    if (FAILED(get_desc(swap_chain, &description))) return false;
    const auto width = description.BufferDesc.Width ? description.BufferDesc.Width
                                                    : static_cast<UINT>(panel_width + 2 * panel_x);
    const auto height = description.BufferDesc.Height ? description.BufferDesc.Height
                                                      : static_cast<UINT>(panel_height + 2 * panel_y);
    if (width < panel_x + panel_width || height < panel_y + panel_height) return false;

    if (!g_device && !initialize(swap_chain, description)) return false;
    if (g_texture_format != description.BufferDesc.Format &&
        !create_texture(description.BufferDesc.Format)) return false;

    // Never retain the back buffer: ResizeBuffers fails while a reference is alive.
    ID3D11Texture2D* back_buffer = nullptr;
    const auto get_buffer = com_method<get_buffer_fn>(swap_chain, 9);
    if (!get_buffer || FAILED(get_buffer(swap_chain, 0, &iid_id3d11_texture2d,
                                         reinterpret_cast<void**>(&back_buffer))) || !back_buffer) {
        return false;
    }

    fill(0, 0, panel_width, panel_height, background);
    draw_text(L"ETS2 ENGINE REMAKE - CORE ACTIVE", text_x, text_y);
    wchar_t line[64];
    int length = append(line, 0, 63, L"FPS ");
    const auto whole_fps = static_cast<unsigned>(fps < 0.0 ? 0.0 : fps > 99999.0 ? 99999.0 : fps);
    length = append_unsigned(line, length, 63, whole_fps);
    length = append(line, length, 63, L"   FRAME ");
    const auto hundredths = static_cast<unsigned>(
        frame_ms < 0.0 ? 0.0 : frame_ms > 9999.0 ? 999900.0 : frame_ms * 100.0);
    length = append_hundredths(line, length, 63, hundredths);
    length = append(line, length, 63, L" MS");
    line[length] = L'\0';
    draw_text(line, text_x, text_y + line_height);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(g_context->Map(g_texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        back_buffer->Release();
        forget_device();
        return false;
    }
    for (int row = 0; row < panel_height; ++row) {
        auto* destination = static_cast<std::uint32_t*>(
            static_cast<void*>(static_cast<std::uint8_t*>(mapped.pData) + row * mapped.RowPitch));
        const std::uint32_t* source = g_pixels + row * panel_width;
        for (int column = 0; column < panel_width; ++column) destination[column] = encode(source[column]);
    }
    g_context->Unmap(g_texture, 0);
    g_context->CopySubresourceRegion(back_buffer, 0, panel_x, panel_y, 0, g_texture, 0, nullptr);
    back_buffer->Release();
    return true;
}

}  // namespace overlay
