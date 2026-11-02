cmake_minimum_required(VERSION 3.20)
project(RendererHook LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "This project requires Windows and the MSVC compiler.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT MSVC_CXX_ARCHITECTURE_ID STREQUAL "x64")
    message(FATAL_ERROR "This project requires an x64 build. Configure with -A x64.")
endif()

add_library(renderer_hook SHARED src/renderer_hook.cpp src/renderer_hook.h)
add_executable(Injector src/injector.cpp)

target_compile_features(renderer_hook PRIVATE cxx_std_17)
target_compile_features(Injector PRIVATE cxx_std_17)
target_compile_options(renderer_hook PRIVATE /W4 /permissive-)
target_compile_options(Injector PRIVATE /W4 /permissive-)
