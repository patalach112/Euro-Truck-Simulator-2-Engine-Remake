# Renderer hook starter

This is a small Windows x64 DLL injection starter for experimenting with a renderer hook. The injector finds a running process by executable name and asks it to load `renderer_hook.dll`. The DLL starts a worker thread and writes a confirmation to the Windows debug output; it doesn't actually hook a renderer yet. You can see the message with a debugger or a tool like DebugView.

Build it on Windows with Visual Studio's MSVC C++ tools and CMake installed. From the repository root, run:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

For MinGW-w64, use an x64 toolchain and a MinGW-compatible CMake generator instead:

```powershell
cmake -S . -B build-mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-mingw
```

The MinGW build puts the executable and DLL directly in `build-mingw` rather than a `Release` subdirectory.

Start the 64-bit target first, then pass its executable name and the absolute DLL path to the injector. For example:

```powershell
.\build\Release\Injector.exe eurotrucks2.exe "C:\path\to\build\Release\renderer_hook.dll"
```

If there are several processes with the same name, the injector uses the first one it finds. It requests full access to the target, so you may need to run it with appropriate permissions. Only use it with software and processes you're allowed to modify. The injector converts its narrow command-line arguments using the active Windows code page before calling the Unicode APIs, so characters that cannot be represented in that code page are not supported.

The worker is deliberately just a starting point: add hook setup there, outside `DllMain`'s loader lock. Keep the DLL loaded while its worker or any installed hooks are active. The injector uses the local `LoadLibraryW` address in the remote process, as in the usual same-architecture injection pattern; unusual process layouts or security policies can prevent that from working. Windows reports a remote thread's exit code as 32 bits, so a zero result is ambiguous for a 64-bit module handle—check the debug output if that happens.
