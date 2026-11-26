#pragma once
#include <windows.h>
typedef enum MH_STATUS {
    MH_UNKNOWN = -1, MH_OK = 0, MH_ERROR_ALREADY_INITIALIZED,
    MH_ERROR_NOT_INITIALIZED, MH_ERROR_ALREADY_CREATED, MH_ERROR_NOT_CREATED,
    MH_ERROR_ENABLED, MH_ERROR_DISABLED, MH_ERROR_NOT_EXECUTABLE,
    MH_ERROR_UNSUPPORTED_FUNCTION, MH_ERROR_MEMORY_ALLOC, MH_ERROR_MEMORY_PROTECT
} MH_STATUS;
#define MH_ALL_HOOKS NULL
inline MH_STATUS MH_Initialize(void) { return MH_OK; }
inline MH_STATUS MH_Uninitialize(void) { return MH_OK; }
// Real MinHook takes LPVOID; MSVC allows fn-ptr -> void* as an extension, gcc
// does not, so the stub takes anything.
template <typename D> inline MH_STATUS MH_CreateHook(LPVOID, D, LPVOID*) { return MH_OK; }
inline MH_STATUS MH_RemoveHook(LPVOID) { return MH_OK; }
inline MH_STATUS MH_EnableHook(LPVOID) { return MH_OK; }
inline MH_STATUS MH_DisableHook(LPVOID) { return MH_OK; }
