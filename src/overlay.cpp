#include "overlay.h"

#include <d3d11.h>

#include <cstdint>
#include <cstring>
#include <cwchar>

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
constexpr std::uint32_t panel_color = 0xc0141414;
constexpr std::uint32_t text_color = 0xff39ff6a;

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

const char vertex_source[] =
    "struct Input { float2 position : POSITION; float2 uv : TEXCOORD0; };\n"
    "struct Output { float4 position : SV_Position; float2 uv : TEXCOORD0; };\n"
    "Output main(Input input) {\n"
    "    Output output;\n"
    "    output.position = float4(input.position, 0.0, 1.0);\n"
    "    output.uv = input.uv;\n"
    "    return output;\n"
    "}\n";

const char pixel_source[] =
    "Texture2D panel : register(t0);\n"
    "SamplerState point_sampler : register(s0);\n"
    "float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
    "    return panel.Sample(point_sampler, uv);\n"
    "}\n";

constexpr D3D11_INPUT_ELEMENT_DESC layout_elements[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

std::uint32_t g_pixels[panel_width * panel_height];

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
ID3D11Texture2D* g_panel = nullptr;
ID3D11ShaderResourceView* g_panel_view = nullptr;
ID3D11VertexShader* g_vertex_shader = nullptr;
ID3D11PixelShader* g_pixel_shader = nullptr;
ID3D11InputLayout* g_input_layout = nullptr;
ID3D11Buffer* g_vertices = nullptr;
ID3D11SamplerState* g_sampler = nullptr;
ID3D11BlendState* g_blend = nullptr;
ID3D11RasterizerState* g_rasterizer = nullptr;
ID3D11DepthStencilState* g_depth = nullptr;
bool g_unavailable = false;
bool g_reported = false;

constexpr GUID iid_id3d11_device = {0xdb6f6ddb, 0xac77, 0x4e88,
    {0x82, 0x53, 0x81, 0x9d, 0xf9, 0xbb, 0xf1, 0x40}};
constexpr GUID iid_id3d11_texture2d = {0x6f15aaf2, 0xd208, 0x4e89,
    {0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c}};

using get_device_fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, const GUID*, void**);
using get_buffer_fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, const GUID*, void**);
using get_desc_fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, DXGI_SWAP_CHAIN_DESC*);
using compile_fn = HRESULT(WINAPI*)(const void*, SIZE_T, const char*, const void*, const void*,
                                    const char*, const char*, UINT, UINT, ID3DBlob**, ID3DBlob**);

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
                         text_color);
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

void compose(double fps, double frame_ms) noexcept {
    fill(0, 0, panel_width, panel_height, panel_color);
    draw_text(L"ETS2 ENGINE REMAKE - CORE ACTIVE", text_x, text_y);
    wchar_t line[64];
    int length = append(line, 0, 63, L"FPS ");
    const auto whole = static_cast<unsigned>(fps < 0.0 ? 0.0 : fps > 99999.0 ? 99999.0 : fps);
    length = append_unsigned(line, length, 63, whole);
    length = append(line, length, 63, L"   FRAME ");
    const auto hundredths = static_cast<unsigned>(
        frame_ms < 0.0 ? 0.0 : frame_ms > 9999.0 ? 999900.0 : frame_ms * 100.0);
    length = append_hundredths(line, length, 63, hundredths);
    length = append(line, length, 63, L" MS");
    line[length] = L'\0';
    draw_text(line, text_x, text_y + line_height);
}

bool compile_shaders(ID3D11Device* device) noexcept {
    HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!compiler) {
        OutputDebugStringW(L"renderer_hook: d3dcompiler_47.dll is missing; overlay disabled.\n");
        return false;
    }
    const auto compile = reinterpret_cast<compile_fn>(
        reinterpret_cast<void*>(GetProcAddress(compiler, "D3DCompile")));
    if (!compile) {
        OutputDebugStringW(L"renderer_hook: D3DCompile is unavailable; overlay disabled.\n");
        return false;
    }

    ID3DBlob* vertex_blob = nullptr;
    ID3DBlob* pixel_blob = nullptr;
    ID3DBlob* errors = nullptr;
    constexpr UINT flags = 1u << 15;
    if (FAILED(compile(vertex_source, sizeof(vertex_source) - 1, nullptr, nullptr, nullptr, "main",
                       "vs_4_0", flags, 0, &vertex_blob, &errors))) {
        if (errors) errors->Release();
        OutputDebugStringW(L"renderer_hook: overlay vertex shader failed to compile.\n");
        return false;
    }
    if (errors) {
        errors->Release();
        errors = nullptr;
    }
    if (FAILED(compile(pixel_source, sizeof(pixel_source) - 1, nullptr, nullptr, nullptr, "main",
                       "ps_4_0", flags, 0, &pixel_blob, &errors))) {
        if (errors) errors->Release();
        vertex_blob->Release();
        OutputDebugStringW(L"renderer_hook: overlay pixel shader failed to compile.\n");
        return false;
    }
    if (errors) errors->Release();

    HRESULT created = device->CreateVertexShader(vertex_blob->GetBufferPointer(),
                                                 vertex_blob->GetBufferSize(), nullptr, &g_vertex_shader);
    if (SUCCEEDED(created)) {
        created = device->CreateInputLayout(layout_elements,
            static_cast<UINT>(sizeof(layout_elements) / sizeof(layout_elements[0])),
            vertex_blob->GetBufferPointer(), vertex_blob->GetBufferSize(), &g_input_layout);
    }
    if (SUCCEEDED(created)) {
        created = device->CreatePixelShader(pixel_blob->GetBufferPointer(),
                                            pixel_blob->GetBufferSize(), nullptr, &g_pixel_shader);
    }
    vertex_blob->Release();
    pixel_blob->Release();
    if (FAILED(created)) {
        OutputDebugStringW(L"renderer_hook: overlay shader objects could not be created.\n");
        return false;
    }
    return true;
}

bool create_states(ID3D11Device* device) noexcept {
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&sampler, &g_sampler))) return false;

    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device->CreateBlendState(&blend, &g_blend))) return false;

    D3D11_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_NONE;
    rasterizer.ScissorEnable = FALSE;
    rasterizer.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizer, &g_rasterizer))) return false;

    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    depth.StencilEnable = FALSE;
    return SUCCEEDED(device->CreateDepthStencilState(&depth, &g_depth));
}

bool create_panel(ID3D11Device* device) noexcept {
    D3D11_TEXTURE2D_DESC description{};
    description.Width = panel_width;
    description.Height = panel_height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateTexture2D(&description, nullptr, &g_panel))) return false;
    return SUCCEEDED(device->CreateShaderResourceView(g_panel, nullptr, &g_panel_view));
}

bool create_geometry(ID3D11Device* device) noexcept {
    D3D11_BUFFER_DESC description{};
    description.ByteWidth = 4 * 4 * sizeof(float);
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(device->CreateBuffer(&description, nullptr, &g_vertices));
}

bool initialize(IDXGISwapChain* chain) noexcept {
    if (g_unavailable) return false;
    const auto get_device = com_method<get_device_fn>(chain, 7);
    if (!get_device ||
        FAILED(get_device(chain, &iid_id3d11_device, reinterpret_cast<void**>(&g_device))) || !g_device) {
        g_unavailable = true;
        OutputDebugStringW(L"renderer_hook: swap chain has no ID3D11Device; overlay disabled.\n");
        return false;
    }
    g_device->GetImmediateContext(&g_context);
    // D3D12 and other backends hand out no immediate context; drawing there is out of scope.
    if (!g_context || !compile_shaders(g_device) || !create_states(g_device) ||
        !create_panel(g_device) || !create_geometry(g_device)) {
        g_unavailable = true;
        OutputDebugStringW(L"renderer_hook: overlay resources could not be created.\n");
        return false;
    }
    return true;
}

struct pipeline_state {
    ID3D11InputLayout* layout = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11Buffer* vertex_buffer = nullptr;
    UINT vertex_stride = 0;
    UINT vertex_offset = 0;
    ID3D11VertexShader* vertex_shader = nullptr;
    ID3D11PixelShader* pixel_shader = nullptr;
    ID3D11RasterizerState* rasterizer = nullptr;
    ID3D11BlendState* blend = nullptr;
    FLOAT blend_factor[4]{};
    UINT sample_mask = 0;
    ID3D11DepthStencilState* depth = nullptr;
    UINT stencil_reference = 0;
    ID3D11RenderTargetView* render_targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView* depth_target = nullptr;
    ID3D11GeometryShader* geometry_shader = nullptr;
    ID3D11Buffer* stream_outputs[D3D11_SO_BUFFER_SLOT_COUNT]{};
    ID3D11ShaderResourceView* ps_resource = nullptr;
    ID3D11SamplerState* ps_sampler = nullptr;
    UINT viewport_count = 1;
    D3D11_VIEWPORT viewport{};
    UINT scissor_count = 1;
    D3D11_RECT scissor{};

    void capture(ID3D11DeviceContext* context) noexcept {
        context->IAGetInputLayout(&layout);
        context->IAGetPrimitiveTopology(&topology);
        context->IAGetVertexBuffers(0, 1, &vertex_buffer, &vertex_stride, &vertex_offset);
        context->VSGetShader(&vertex_shader, nullptr, nullptr);
        context->PSGetShader(&pixel_shader, nullptr, nullptr);
        context->RSGetState(&rasterizer);
        context->OMGetBlendState(&blend, blend_factor, &sample_mask);
        context->OMGetDepthStencilState(&depth, &stencil_reference);
        context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, render_targets, &depth_target);
        context->GSGetShader(&geometry_shader, nullptr, nullptr);
        context->SOGetTargets(D3D11_SO_BUFFER_SLOT_COUNT, stream_outputs);
        context->PSGetShaderResources(0, 1, &ps_resource);
        context->PSGetSamplers(0, 1, &ps_sampler);
        viewport_count = 1;
        context->RSGetViewports(&viewport_count, &viewport);
        scissor_count = 1;
        context->RSGetScissorRects(&scissor_count, &scissor);
    }

    void restore(ID3D11DeviceContext* context) noexcept {
        context->IASetInputLayout(layout);
        context->IASetPrimitiveTopology(topology);
        context->IASetVertexBuffers(0, 1, &vertex_buffer, &vertex_stride, &vertex_offset);
        context->VSSetShader(vertex_shader, nullptr, 0);
        context->PSSetShader(pixel_shader, nullptr, 0);
        context->RSSetState(rasterizer);
        context->OMSetBlendState(blend, blend_factor, sample_mask);
        context->OMSetDepthStencilState(depth, stencil_reference);
        context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, render_targets, depth_target);
        context->GSSetShader(geometry_shader, nullptr, 0);
        context->SOSetTargets(D3D11_SO_BUFFER_SLOT_COUNT, stream_outputs, nullptr);
        context->PSSetShaderResources(0, 1, &ps_resource);
        context->PSSetSamplers(0, 1, &ps_sampler);
        if (viewport_count) context->RSSetViewports(viewport_count, &viewport);
        if (scissor_count) context->RSSetScissorRects(scissor_count, &scissor);
        if (layout) layout->Release();
        if (vertex_buffer) vertex_buffer->Release();
        if (vertex_shader) vertex_shader->Release();
        if (pixel_shader) pixel_shader->Release();
        if (rasterizer) rasterizer->Release();
        if (blend) blend->Release();
        if (depth) depth->Release();
        for (auto* target : render_targets) {
            if (target) target->Release();
        }
        if (geometry_shader) geometry_shader->Release();
        for (auto* output : stream_outputs) {
            if (output) output->Release();
        }
        if (ps_resource) ps_resource->Release();
        if (ps_sampler) ps_sampler->Release();
        if (depth_target) depth_target->Release();
        *this = pipeline_state{};
    }
};

}  // namespace

bool render(IDXGISwapChain* swap_chain, double fps, double frame_ms) noexcept {
    if (!swap_chain || g_unavailable) return false;
    const auto get_buffer = com_method<get_buffer_fn>(swap_chain, 9);
    ID3D11Texture2D* buffer = nullptr;
    if (!get_buffer || FAILED(get_buffer(swap_chain, 0, &iid_id3d11_texture2d,
                                         reinterpret_cast<void**>(&buffer))) || !buffer) return false;
    if (!g_device && !initialize(swap_chain)) {
        buffer->Release();
        return false;
    }

    // The swap chain description can carry stale or zero dimensions; the texture is authoritative.
    D3D11_TEXTURE2D_DESC texture{};
    buffer->GetDesc(&texture);
    UINT width = texture.Width;
    UINT height = texture.Height;
    if (!width || !height) {
        const auto get_desc = com_method<get_desc_fn>(swap_chain, 12);
        DXGI_SWAP_CHAIN_DESC description{};
        if (get_desc && SUCCEEDED(get_desc(swap_chain, &description))) {
            width = description.BufferDesc.Width;
            height = description.BufferDesc.Height;
        }
    }
    if (!width || !height || width < static_cast<UINT>(panel_x + panel_width) ||
        height < static_cast<UINT>(panel_y + panel_height)) {
        buffer->Release();
        return false;
    }

    ID3D11RenderTargetView* target = nullptr;
    const HRESULT viewed = g_device->CreateRenderTargetView(buffer, nullptr, &target);
    buffer->Release();
    if (FAILED(viewed) || !target) {
        g_unavailable = true;
        OutputDebugStringW(L"renderer_hook: no render target view for the back buffer; overlay disabled.\n");
        return false;
    }

    compose(fps, frame_ms);
    const auto left = -1.0f + 2.0f * static_cast<float>(panel_x) / static_cast<float>(width);
    const auto top = 1.0f - 2.0f * static_cast<float>(panel_y) / static_cast<float>(height);
    const auto right = left + 2.0f * static_cast<float>(panel_width) / static_cast<float>(width);
    const auto bottom = top - 2.0f * static_cast<float>(panel_height) / static_cast<float>(height);
    const float vertices[] = {
        left, top, 0.0f, 0.0f,
        right, top, 1.0f, 0.0f,
        left, bottom, 0.0f, 1.0f,
        right, bottom, 1.0f, 1.0f,
    };

    pipeline_state saved;
    saved.capture(g_context);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(g_context->Map(g_vertices, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, vertices, sizeof(vertices));
        g_context->Unmap(g_vertices, 0);
    }
    bool uploaded = false;
    if (SUCCEEDED(g_context->Map(g_panel, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        for (int row = 0; row < panel_height; ++row) {
            std::memcpy(static_cast<std::uint8_t*>(mapped.pData) + row * mapped.RowPitch,
                        g_pixels + row * panel_width, panel_width * sizeof(std::uint32_t));
        }
        g_context->Unmap(g_panel, 0);
        uploaded = true;
    }

    if (uploaded) {
        const UINT stride = 4 * sizeof(float);
        const UINT offset = 0;
        // The quad is expressed in clip space for the whole buffer, so the viewport has to
        // span the buffer as well; a panel-sized viewport would rescale and shift it.
        D3D11_VIEWPORT viewport{};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        g_context->IASetInputLayout(g_input_layout);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        g_context->IASetVertexBuffers(0, 1, &g_vertices, &stride, &offset);
        g_context->GSSetShader(nullptr, nullptr, 0);
        g_context->SOSetTargets(0, nullptr, nullptr);
        g_context->VSSetShader(g_vertex_shader, nullptr, 0);
        g_context->PSSetShader(g_pixel_shader, nullptr, 0);
        g_context->PSSetShaderResources(0, 1, &g_panel_view);
        g_context->PSSetSamplers(0, 1, &g_sampler);
        g_context->RSSetState(g_rasterizer);
        g_context->RSSetViewports(1, &viewport);
        // The rasterizer state has scissor testing off; clearing the list keeps it out of the way
        // even if another state object with scissor enabled is bound later in the frame.
        g_context->RSSetScissorRects(0, nullptr);
        g_context->OMSetDepthStencilState(g_depth, 0);
        g_context->OMSetBlendState(g_blend, nullptr, 0xffffffff);
        // A predicate left bound by the engine silently discards draws that fail it.
        g_context->SetPredication(nullptr, FALSE);
        g_context->OMSetRenderTargets(1, &target, nullptr);
        g_context->Draw(4, 0);

        if (!g_reported) {
            g_reported = true;
            wchar_t message[160];
            const int written = std::swprintf(message, sizeof(message) / sizeof(message[0]),
                L"renderer_hook: overlay drawing at %ux%u, panel column %d.\n", width, height, panel_x);
            if (written > 0) OutputDebugStringW(message);
        }
    }
    saved.restore(g_context);
    target->Release();
    return uploaded;
}

}  // namespace overlay
