cmake_minimum_required(VERSION 3.20)
project(RendererHook LANGUAGES CXX)

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

add_library(renderer_hook SHARED
    src/renderer_hook.cpp src/renderer_hook.h
    src/scanner.cpp src/scanner.h
    src/hook.cpp src/hook.h
    src/dx_hook.cpp src/dx_hook.h)
set_target_properties(renderer_hook PROPERTIES PREFIX "")
target_link_libraries(renderer_hook PRIVATE d3d11 dxgi)
add_executable(Injector src/injector.cpp)

target_compile_features(renderer_hook PRIVATE cxx_std_17)
target_compile_features(Injector PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(renderer_hook PRIVATE /W4 /permissive-)
    target_compile_options(Injector PRIVATE /W4 /permissive-)
else()
    target_link_options(renderer_hook PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_options(Injector PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(renderer_hook PRIVATE -Wall -Wextra)
    target_compile_options(Injector PRIVATE -Wall -Wextra)
endif()
