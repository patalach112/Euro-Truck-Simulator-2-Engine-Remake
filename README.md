# Renderer hook for ETS2 experiments

A Windows x64 DLL that is injected into `eurotrucks2.exe` by a small companion injector. The DLL contains a signature scanner, an inline hook engine and a 60 Hz profiler that writes to the debug output. The profiler currently reports simulated numbers; no engine offsets are shipped, because they change with every game build.

Build with MSVC from the repository root:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

Or with an x64 MinGW-w64 toolchain, which links the C and C++ runtimes statically so the DLL has no extra runtime DLL dependencies:

```powershell
cmake -S . -B build-mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-mingw
```

The MinGW build puts the executable and DLL directly in `build-mingw` rather than a `Release` subdirectory. Start the game, then inject with:

```powershell
.\build\Release\Injector.exe eurotrucks2.exe "C:\path\to\build\Release\renderer_hook.dll"
```

The injector finds the first process with that name, opens it with full access, writes the DLL path and calls `LoadLibraryW` through a remote thread. It converts its narrow arguments with the active ANSI code page, so characters outside that code page are lost. A zero remote thread exit code is ambiguous because only 32 bits are reported; check the debug output if in doubt.

## Components

`src/scanner.*` walks the executable sections of the main module and matches byte patterns such as `"E8 ? ? ? ? 48 8B D8"`. Sections are mapped with `VirtualQuery`, non-readable, guarded or non-image pages are skipped, and windows are copied with `ReadProcessMemory` before matching, so an unreadable page can never fault the process. Matching uses a Boyer-Moore-Horspool skip table anchored on the last fixed byte. Patterns and their tokens are validated; a malformed pattern returns `nullptr` instead of searching.

`src/hook.*` installs a 14-byte absolute jump (`FF 25 00 00 00 00` plus the destination address). The stolen prologue is copied into a trampoline page allocated within 2 GB of the target, relative branches and RIP-relative operands are relocated, and the trampoline returns to the first untouched instruction. Only a subset of prologues is decoded on purpose: `call`/`jmp`/`jcc rel32`, RIP-relative and standard `modrm` forms of a few common opcodes, pushes and `mov`/`moffs` immediates. Anything else makes `install` fail rather than producing a half-understood copy. Installation rejects pages that are not committed, executable and readable, verifies that the prologue still matches, switches it to `PAGE_EXECUTE_READWRITE`, patches, flushes the instruction cache and restores the previous protection. Ensure no thread can execute the target, the detour or the trampoline while a hook is installed or removed; this engine does not suspend threads.

`src/dx_hook.*` resolves `IDXGISwapChain::Present` (vtable index 8) without hardcoded offsets. It creates a hidden popup window and a throwaway D3D11 swap chain, first on the hardware driver and then on WARP, reads the method pointer straight from the swap chain vtable - which is the same DXGI entry that DirectX 12 titles and their wrappers use - and releases every dummy object. The address is then patched by the trampoline engine. Because the render thread is already running, the install suspends the process's other threads, checks that no instruction pointer lies inside the prologue span, patches, and resumes them; if any thread cannot be inspected or sits inside that span, the hook is skipped and the game keeps rendering. `hkPresent` samples `QueryPerformanceCounter`, counts presents atomically, smooths the frame rate, draws the overlay and then writes a one-line warning through `OutputDebugStringW` when a frame exceeds 33.3 ms, using stack buffers only. Draw calls and wait states are still simulated; the panel shows the smoothed FPS and the current frame time from the real timer.

`src/overlay.*` draws the diagnostic panel without shaders, a font DLL or per-frame allocation. A 5x7 bitmap alphabet is compiled into the module, rasterised into a static 396x64 pixel buffer (dark panel, bright green glyphs), uploaded to one dynamic texture through `Map`/`Unmap` and copied onto the current back buffer with `CopySubresourceRegion` at (16, 16). The text layer therefore uses only the swap chain's own `ID3D11Device`, which is fetched once from the chain; D3D12 chains, other back buffer formats and panels larger than the frame disable the overlay with a debug message and leave rendering untouched. The back buffer is fetched and released every frame on purpose, because holding a reference would make the game's `ResizeBuffers` fail.

`src/renderer_hook.*` pins the DLL, checks that the host process is `eurotrucks2.exe`, runs the example scan, installs the Present hook and starts a waitable-timer loop at 60 Hz with a fixed stack buffer for output, so reporting does not allocate. Move the scan results into real engine markers before trusting the numbers.

The example signature in `renderer_hook.cpp` is intentionally unverified and is only compared, never called. A third-party overlay that installs its own Present hook before this DLL loads will be bypassed by the trampoline, and an overlay that hooks afterwards may stack on top of this one; both are fine for measurement but worth remembering when the numbers look odd. Actual offsets and detour logic belong to the build you are targeting. Keep the DLL loaded while a hook or worker is active, and only use any of this with software and processes you are allowed to modify.
