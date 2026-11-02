cmake_minimum_required(VERSION 3.20)
project(RendererHook LANGUAGES C CXX)

if(NOT WIN32 OR NOT (MSVC OR MINGW))
    message(FATAL_ERROR "This project requires Windows with MSVC or MinGW-w64.")
endif()

include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
#if defined(_M_ARM64EC) || (!defined(_M_X64) && !defined(__x86_64__))
#error This project requires x64.
#endif
int main() { return 0; }
" RENDERER_HOOK_X64)
if(NOT RENDERER_HOOK_X64)
    message(FATAL_ERROR "This project requires an x64 toolchain.")
endif()

add_library(minhook STATIC
    third_party/minhook/src/buffer.c
    third_party/minhook/src/hook.c
    third_party/minhook/src/trampoline.c
    third_party/minhook/src/hde/hde32.c
    third_party/minhook/src/hde/hde64.c)
target_include_directories(minhook PUBLIC third_party/minhook/include)

add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_dx11.cpp
    third_party/imgui/backends/imgui_impl_win32.cpp)
target_include_directories(imgui PUBLIC third_party/imgui third_party/imgui/backends)
target_link_libraries(imgui PUBLIC d3d11 user32 dwmapi)

add_library(renderer_hook SHARED
    src/renderer_hook.cpp src/renderer_hook.h
    src/core.cpp src/core.h
    src/profiler.cpp src/profiler.h
    src/plugin.cpp
    src/scanner.cpp src/scanner.h
    src/dx_hook.cpp src/dx_hook.h
    src/overlay.cpp src/overlay.h
    src/optimization.cpp src/optimization.h
    src/engine_core.cpp src/engine_core.h
    src/allocator.cpp src/allocator.h
    src/memory_hook.cpp src/memory_hook.h
    src/thread_pool.cpp src/thread_pool.h src/pool_info.h
    src/d3dcompiler_shim.cpp
    src/renderer_hook.def)
set_target_properties(renderer_hook PROPERTIES PREFIX "")
target_link_libraries(renderer_hook PRIVATE minhook imgui d3d11 dxgi)

add_executable(Injector src/injector.cpp)

target_compile_features(renderer_hook PRIVATE cxx_std_17)
target_compile_features(Injector PRIVATE cxx_std_17)
target_compile_features(imgui PRIVATE cxx_std_17)
find_package(Threads REQUIRED)
target_link_libraries(renderer_hook PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(renderer_hook PRIVATE /W4 /permissive-)
    target_compile_options(Injector PRIVATE /W4 /permissive-)
else()
    target_link_options(renderer_hook PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_options(Injector PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(renderer_hook PRIVATE -Wall -Wextra)
    target_compile_options(Injector PRIVATE -Wall -Wextra)
endif()
