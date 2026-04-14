#pragma once

#include <reframework/API.hpp>
#include <cstdint>

namespace cameraunlock::reframework {

// Resolved method + singleton name pair for runtime game state checks.
struct MethodCheck {
    ::reframework::API::Method* method = nullptr;
    const char* singletonName = nullptr;
    bool failed = false;  // permanently disabled on SEH crash
};

// Set the namespace candidates used by FindType/FindSingleton.
// Default: "app", "app.gui", "app.ropeway", "app.ropeway.gui",
//          "requiem", "requiem.gui", "chainsaw", "chainsaw.gui"
void SetNamespaceCandidates(const char* const* candidates, int count);

// Try to find a type across all namespace candidates.
::reframework::API::TypeDefinition* FindType(
    ::reframework::API::TDB* tdb, const char* baseName);

// Try to find a managed singleton across namespace candidates.
// Returns pointer to a static buffer — valid until next call.
const char* FindSingleton(const ::reframework::API* api, const char* baseName);

// Try to find a method on a type, trying multiple name variants.
::reframework::API::Method* FindMethod(
    ::reframework::API::TypeDefinition* type, const char* names[], int count);

// Probe a manager: find type, find method, find singleton.
// On success, populates `out` and returns true.
bool ProbeManager(
    ::reframework::API::TDB* tdb,
    const ::reframework::API* api,
    const char* typeName,
    const char* methodNames[],
    int methodCount,
    MethodCheck& out,
    const char* label);

// SEH-protected method invocation helpers.
// InvokeBool: returns the bool result (false if method missing/crashed).
// InvokeInt:  returns the uint32 result (0 if missing/crashed).
// InvokePointer: returns the pointer result ((void*)1 if missing/crashed — non-null sentinel).
bool InvokeBool(const ::reframework::API* api, void* vmCtx,
                MethodCheck& check, bool diag, const char* label);
uint32_t InvokeInt(const ::reframework::API* api, void* vmCtx,
                   MethodCheck& check, bool diag, const char* label);
void* InvokePointer(const ::reframework::API* api, void* vmCtx,
                    MethodCheck& check, bool diag, const char* label);

} // namespace cameraunlock::reframework
