# Renderer hook for ETS2 experiments

A Windows x64 DLL that can be injected into `eurotrucks2.exe` or loaded through the game's SCS plugin path. It resolves its own signature, hooks the DirectX presentation path with MinHook and draws a Dear ImGui telemetry window over the frame.

## Build

With MSVC:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

With an x64 MinGW-w64 toolchain, which links the runtimes statically:

```powershell
cmake -S . -B build-mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-mingw
```

The MinGW build writes the executable and DLL directly into `build-mingw`. The DLL depends only on Windows components: `kernel32`, `user32`, `shell32`, `d3d11` and the universal CRT, which ships with Windows 10 and later. `d3dcompiler_47.dll`, used by the ImGui Direct3D 11 backend, is loaded at runtime by `src/d3dcompiler_shim.cpp` because MinGW has no import library for it.

## Injection

```powershell
.\build\Release\Injector.exe eurotrucks2.exe "C:\path\to\build\Release\renderer_hook.dll"
```

The injector finds the first process with that name, writes the DLL path and calls `LoadLibraryW` through a remote thread. Its narrow arguments are converted with the active ANSI code page, so characters outside that code page are lost. A zero remote thread exit code is ambiguous because only 32 bits are reported.

## Architecture

`src/core.*` is the single entry point. It pins the DLL, verifies that the host is `eurotrucks2.exe`, runs the signature scan, installs the presentation hooks and starts the profiler. It runs once, whether the DLL arrives through injection or through the plugin export.

`src/dx_hook.*` resolves `IDXGISwapChain::Present` (vtable slot 8) and `IDXGISwapChain1::Present1` (slot 22) from a throwaway D3D11 swap chain on a hidden window, then hands both addresses to MinHook, which owns the trampolines. The detours sample `QueryPerformanceCounter`, log a warning through `OutputDebugStringW` when a frame exceeds 33.3 ms, draw the overlay and call the trampoline. `Present1` is skipped with a log line if the platform does not expose it.

`src/overlay.*` drives Dear ImGui with its Direct3D 11 backend. On the first frame it queries the device and immediate context from the intercepted swap chain, creates the ImGui context without a platform backend (no input, no cursor change, no ini file) and lets the backend build its shaders and font texture. Press Insert to toggle overlay interaction. While interactive, the window drops `ImGuiWindowFlags_NoInputs` and takes focus, and the frame loop feeds ImGui the cursor position and left button through `AddMousePosEvent`/`AddMouseButtonEvent`, since there is no platform backend to do it. The pointer you see is ImGui's own software cursor (`MouseDrawCursor`), because the game hides the hardware cursor with `ShowCursor` and balancing that count from the DLL would fight the engine. Press Insert again to return to the passive overlay. Mouse clicks still reach the game while interacting, because the swap chain's window procedure is not hooked; only keyboard input is unaffected.

Every present is drawn, with no session or input gate: it binds a render target view for the buffer that `IDXGISwapChain3::GetCurrentBackBufferIndex` reports, sets a full-frame viewport, feeds ImGui its display size and delta time, renders a fixed 10,10 telemetry window and restores the previous targets and viewport. A failed backend start is retried at most five times, so a broken device cannot rebuild the font atlas and shaders at frame rate, and the first drawn frame reports its vertex count so an empty draw is distinguishable from an invisible one. The drawing itself is the backend's own shader pipeline, which saves and restores the device context state it touches.

Scanning does not run on the loader thread: `core` installs the presentation hooks first, then hands all signature work to one short-lived thread created at `THREAD_PRIORITY_LOWEST`, which scans, hooks what it found and exits. Nothing on the present path scans memory.

`src/optimization.*` scans for a candidate per-vehicle light update prologue (`48 8B C4 55 57 41 54 41 56 41 57`), hooks it with MinHook and exposes a checkbox in the overlay bound to `g_disable_remote_lights`. The detour forwards the four integer argument slots of the Windows x64 calling convention to the trampoline. Two things are deliberately inert until someone supplies a build-specific classifier: the signature is a placeholder that may match an unrelated function, and a call is only skipped when `set_remote_predicate` has provided a predicate that identifies a remote vehicle. Without that predicate the detour always forwards, because returning early from a routine whose return value and side effects are unknown can corrupt engine state. The overlay shows the call and skip counters so both the hook and the classifier can be verified before enabling the checkbox.

`src/profiler.*` keeps the 60 Hz waitable-timer thread that reports present counts once per second.

`src/scanner.*` resolves the `.text` section once from the PE headers, caches its bounds and scans only that contiguous block, skipping regions that `VirtualQuery` reports as unreadable, guarded or not image-backed. A query-performance deadline (two seconds by default) aborts the pass with a single log line instead of running without bound, and matching uses a Boyer-Moore-Horspool skip table anchored on the last fixed byte. Sections are mapped with `VirtualQuery`, unreadable or guarded pages are skipped and candidates are copied with `ReadProcessMemory`, so scanning cannot fault the process. The pattern shipped in `core.cpp` is a placeholder that is only matched, never called.

`src/plugin.cpp` exports `scs_telemetry_init` and `scs_telemetry_shutdown` for the SCS telemetry API version 1, so the same DLL can be dropped into `bin/win_x64/plugins/` and started by the game instead of being injected. It validates the version, logs through the API's own callback, starts the core and returns success; channel registration is not implemented yet.

`third_party/` contains MinHook 1.3.4 and Dear ImGui 1.91.5, both unchanged and compiled into the DLL. Versions and licences are listed in `third_party/README.md`.

## Limits

The overlay is display-only and uses the default ImGui font, so it needs no external files. Direct3D 12 swap chains have no immediate context for the backend and disable the overlay with a debug message. Nothing here is tied to a specific game build beyond the placeholder signature; real engine markers and any detour logic belong to the build you target. Only use this with software and processes you are allowed to modify.
