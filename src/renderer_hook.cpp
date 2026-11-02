#include "renderer_hook.h"

namespace renderer_hook {

void log_loaded() noexcept {
    OutputDebugStringA("renderer_hook.dll loaded inside the target process.\n");
}

DWORD WINAPI initialize(void*) noexcept {
    log_loaded();

    // Install renderer hooks here, outside the loader lock.
    return 0;
}

}  // namespace renderer_hook

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);

        HANDLE thread = CreateThread(nullptr, 0, renderer_hook::initialize, nullptr, 0, nullptr);
        if (thread == nullptr) {
            return FALSE;
        }
        CloseHandle(thread);
    }
    return TRUE;
}
