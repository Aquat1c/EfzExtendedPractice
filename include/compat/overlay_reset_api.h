#pragma once

#include <stdint.h>
#include <windows.h>

// Shared DisplayManager/overlay C ABI, 16 bytes on Win32. Keep the provider loaded from query
// through the last notification. Calls are synchronous on the observed render thread, outside a frame.
typedef struct EfzOverlayResetApiV1 {
    uint32_t size;
    uint32_t version;
    HRESULT (__cdecl *beforeReset)(void* device, uint32_t epoch);
    HRESULT (__cdecl *afterReset)(void* device, uint32_t epoch, uint32_t ready);
} EfzOverlayResetApiV1;

// beforeReset S_OK covers only the provider's own backend. afterReset ready=1 only after a real
// Reset and renderer recovery, or to cancel before any Reset. See displaymanager-reset-contract.md.
#ifdef __cplusplus
extern "C" {
#endif
__declspec(dllexport) const EfzOverlayResetApiV1* __cdecl EFZ_QueryOverlayResetApiV1(void);
#ifdef __cplusplus
}
#if defined(_WIN32) && !defined(_WIN64)
static_assert(sizeof(EfzOverlayResetApiV1) == 16, "overlay reset ABI must be 16 bytes on x86");
#endif
#endif

// MSVC x86 otherwise exports the C symbol with its leading underscore.
#if defined(_MSC_VER) && defined(_M_IX86)
#pragma comment(linker, "/EXPORT:EFZ_QueryOverlayResetApiV1=_EFZ_QueryOverlayResetApiV1")
#endif
