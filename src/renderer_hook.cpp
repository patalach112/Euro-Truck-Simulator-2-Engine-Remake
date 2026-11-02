#include "renderer_hook.h"
#include "core.h"

namespace renderer_hook {

DWORD WINAPI initialize(void* instance) noexcept {
    core::start(static_cast<HMODULE>(instance));
    return 0;
}

}  // namespace renderer_hook

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(nullptr, 0, renderer_hook::initialize, instance, 0, nullptr);
        if (!thread) return FALSE;
        CloseHandle(thread);
    }
    return TRUE;
}
