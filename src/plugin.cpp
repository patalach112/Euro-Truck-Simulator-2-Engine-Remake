#include "core.h"

#include <cstdint>

// Subset of the SCS telemetry SDK version 1 ABI, matching bin/win_x64/plugins loading.
namespace {

using scs_u32_t = std::uint32_t;
using scs_s32_t = std::int32_t;
using scs_log_t = void (*)(scs_u32_t type, const char* message);

struct scs_telemetry_init_params_v100_t {
    struct {
        scs_u32_t version;
        scs_log_t log;
    } common;
    const void* register_for_channel;
    const void* unregister_from_channel;
};

constexpr scs_s32_t result_ok = 0;
constexpr scs_s32_t result_unsupported = -1;
constexpr scs_s32_t result_invalid_parameter = -2;
constexpr scs_u32_t log_type_message = 0;

HMODULE module() noexcept {
    HMODULE handle = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&module), &handle);
    return handle;
}

}  // namespace

extern "C" {

__declspec(dllexport) scs_s32_t scs_telemetry_init(scs_u32_t version,
                                                   const scs_telemetry_init_params_v100_t* params) {
    if (version != 1) return result_unsupported;
    if (!params) return result_invalid_parameter;
    if (params->common.log) {
        params->common.log(log_type_message,
            "renderer_hook: telemetry API version 1 accepted; channel registration is not implemented.");
    }
    core::start(module());
    return result_ok;
}

__declspec(dllexport) void scs_telemetry_shutdown() {
    OutputDebugStringW(L"renderer_hook: telemetry shutdown requested.\n");
}

}
