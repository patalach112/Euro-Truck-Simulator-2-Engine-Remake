# Vendored dependencies

- `minhook` - MinHook 1.3.4, BSD 2-Clause, https://github.com/TsudaKageyu/minhook. x64 trampoline engine; `LICENSE.txt` is unchanged.
- `imgui` - Dear ImGui 1.91.5, MIT, https://github.com/ocornut/imgui. Core plus the Direct3D 11 backend only; the demo, examples and other backends are not vendored.

Both are compiled into the DLL. `src/d3dcompiler_shim.cpp` provides the `D3DCompile` symbol that the ImGui backend calls, resolved from `d3dcompiler_47.dll` at runtime, because MinGW ships no import library for it.
