#pragma once

// Hook manager requires MinHook library to be linked
// Include this header only when MinHook is available

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace cameraunlock::hooks {

// Error codes matching MinHook's MH_STATUS
enum class HookStatus {
    Unknown = -1,
    Ok = 0,
    ErrorAlreadyInitialized,
    ErrorNotInitialized,
    ErrorAlreadyCreated,
    ErrorNotCreated,
    ErrorEnabled,
    ErrorDisabled,
    ErrorNotExecutable,
    ErrorUnsupportedFunction,
    ErrorMemoryAlloc,
    ErrorMemoryProtect,
    ErrorModuleNotFound,
    ErrorFunctionNotFound
};

// Convert HookStatus to string for logging
const char* HookStatusToString(HookStatus status);

// Wrapper around MinHook for managing function hooks
// This class requires MinHook to be linked to the application
//
// Every member may be called from any thread: the list of hooks is guarded by
// a lock of the manager's own, held across the MinHook call so the list and
// MinHook agree. The lock is recursive, so a call back in on the thread
// holding it that does not reach MinHook (GetHookCount, IsInitialized) returns
// instead of deadlocking. One that does reach MinHook still hangs, on
// MinHook's own lock, which is not re-entrant (vendor/minhook/src/hook.c,
// EnterSpinLock): a detour on a Win32 function MinHook calls (VirtualProtect,
// SuspendThread) must not make, enable or remove hooks.
//
// A caller that needs several calls to go in as one (make every
// detour before any is enabled, and take them out again on a failure) still
// holds a lock of its own round them.
class HookManager {
public:
    static HookManager& Instance();

    // Initialize MinHook - must be called before creating any hooks
    HookStatus Initialize();

    // Shutdown MinHook - disables and removes all hooks
    void Shutdown();

    // Create a hook at target address
    // target: address of function to hook
    // detour: address of detour function
    // original: receives address of trampoline to call original function
    HookStatus CreateHook(void* target, void* detour, void** original);

    HookStatus RemoveHook(void* target);

    HookStatus EnableHook(void* target);

    HookStatus DisableHook(void* target);

    HookStatus EnableAllHooks();

    HookStatus DisableAllHooks();

    void RemoveAllHooks();

    bool IsInitialized() const { return m_initialized.load(); }

    size_t GetHookCount() const;

    // Non-copyable
    HookManager(const HookManager&) = delete;
    HookManager& operator=(const HookManager&) = delete;

private:
    HookManager() = default;
    ~HookManager() = default;

    mutable std::recursive_mutex m_mutex;
    std::atomic<bool> m_initialized{false};
    std::vector<void*> m_hooks;
};

// Throwing forms over HookManager::Instance(). Each throws std::runtime_error
// naming `what` and the MinHook status:
//   "MinHook did not initialise: <status>"
//   "no detour on <what>: <status>"
//   "detour on <what> not enabled: <status>"

// Starts MinHook, or finds HookManager already started it. Throws when MinHook
// was started by something else, since HookManager then refuses every hook.
void InitializeHooksOrThrow();

// Makes the detour without enabling it, so several can be made before any is
// on. `original` receives the trampoline.
void CreateHookOrThrow(void* target, void* detour, void** original, const char* what);

void EnableHookOrThrow(void* target, const char* what);

// InitializeHooksOrThrow, CreateHookOrThrow and EnableHookOrThrow.
void InstallHookOrThrow(void* target, void* detour, void** original, const char* what);

// The same for a target given as an address or a pointer, and a detour and an
// original of their own function types (a 32-bit thiscall original is detoured
// by a fastcall function, so the two types differ).
template <class Target, class Detour, class Original>
void CreateHookOrThrow(Target target, Detour detour, Original* original, const char* what) {
    CreateHookOrThrow(reinterpret_cast<void*>(target), reinterpret_cast<void*>(detour),
                      reinterpret_cast<void**>(original), what);
}

template <class Target>
void EnableHookOrThrow(Target target, const char* what) {
    EnableHookOrThrow(reinterpret_cast<void*>(target), what);
}

template <class Target, class Detour, class Original>
void InstallHookOrThrow(Target target, Detour detour, Original* original, const char* what) {
    InstallHookOrThrow(reinterpret_cast<void*>(target), reinterpret_cast<void*>(detour),
                       reinterpret_cast<void**>(original), what);
}

// RAII hook guard - automatically removes hook on destruction
class ScopedHook {
public:
    ScopedHook() = default;
    // A failure shows only as IsValid() false. Create() answers why.
    ScopedHook(void* target, void* detour, void** original);
    ~ScopedHook();

    // Move-only
    ScopedHook(ScopedHook&& other) noexcept;
    ScopedHook& operator=(ScopedHook&& other) noexcept;
    ScopedHook(const ScopedHook&) = delete;
    ScopedHook& operator=(const ScopedHook&) = delete;

    // Create and enable hook
    HookStatus Create(void* target, void* detour, void** original);

    bool IsValid() const { return m_target != nullptr; }
    explicit operator bool() const { return IsValid(); }

    void* GetTarget() const { return m_target; }

    // Release ownership without removing hook
    void* Release();

private:
    void* m_target = nullptr;
};

} // namespace cameraunlock::hooks
