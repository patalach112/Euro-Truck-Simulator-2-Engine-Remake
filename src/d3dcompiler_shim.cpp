#include <windows.h>
#include <d3dcompiler.h>

namespace {

using compile_fn = HRESULT(WINAPI*)(const void*, SIZE_T, const char*, const D3D_SHADER_MACRO*, ID3DInclude*,
                                    const char*, const char*, UINT, UINT, ID3DBlob**, ID3DBlob**);

compile_fn resolve() noexcept {
    static const compile_fn compile = []() noexcept -> compile_fn {
        // MinGW ships no d3dcompiler import library, so the compiler DLL is opened at runtime.
        HMODULE module = LoadLibraryW(L"d3dcompiler_47.dll");
        if (!module) return nullptr;
        return reinterpret_cast<compile_fn>(
            reinterpret_cast<void*>(GetProcAddress(module, "D3DCompile")));
    }();
    return compile;
}

}  // namespace

HRESULT WINAPI D3DCompile(const void* data, SIZE_T data_size, const char* filename,
                          const D3D_SHADER_MACRO* defines, ID3DInclude* includes,
                          const char* entrypoint, const char* target, UINT flags,
                          UINT effect_flags, ID3DBlob** shader, ID3DBlob** error_messages) {
    const compile_fn compile = resolve();
    if (!compile) return E_FAIL;
    return compile(data, data_size, filename, defines, includes, entrypoint, target, flags,
                   effect_flags, shader, error_messages);
}
