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
//          "offline", "offline.gui",
//          "requiem", "requiem.gui", "chainsaw", "chainsaw.gui"
void SetNamespaceCandidates(const char* const* candidates, int count);

// Try to find a type across all namespace candidates.
::reframework::API::TypeDefinition* FindType(
    ::reframework::API::TDB* tdb, const char* baseName);

// Try to find a managed singleton across namespace candidates.
// Returns pointer to a static buffer - valid until next call.
const char* FindSingleton(const ::reframework::API* api, const char* baseName);

// Try to find a method on a type, trying multiple name variants. `boundName`,
// when given, receives the entry of `names` that actually resolved - the log
// line used to name names[0] whichever variant bound, so a title exposing only
// the third spelling was reported as exposing the first.
::reframework::API::Method* FindMethod(
    ::reframework::API::TypeDefinition* type, const char* names[], int count,
    const char** boundName = nullptr);

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
// InvokePointer: returns the pointer result ((void*)1 if missing/crashed - non-null sentinel).
bool InvokeBool(const ::reframework::API* api, void* vmCtx,
                MethodCheck& check, bool diag, const char* label);
uint32_t InvokeInt(const ::reframework::API* api, void* vmCtx,
                   MethodCheck& check, bool diag, const char* label);
void* InvokePointer(const ::reframework::API* api, void* vmCtx,
                    MethodCheck& check, bool diag, const char* label);

// The same two calls, reporting whether the call happened at all. Return false
// when the method never resolved, has already been disabled by a fault, the
// singleton is momentarily absent, or the invoke raised; `out` is untouched then.
//
// A check whose SUPPRESSING answer is the falsy one needs these. InvokeBool
// returns false both for "the game says false" and "the call could not be made",
// and InvokeInt returns 0 for both - so a loading screen with no singleton yet
// reads as "the player has no control" or "game flow is 0" and suppresses
// tracking for a reason that was never measured.
bool TryInvokeBool(const ::reframework::API* api, void* vmCtx,
                   MethodCheck& check, bool diag, const char* label, bool& out);
bool TryInvokeInt(const ::reframework::API* api, void* vmCtx,
                  MethodCheck& check, bool diag, const char* label, uint32_t& out);

} // namespace cameraunlock::reframework
