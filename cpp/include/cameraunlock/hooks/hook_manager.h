#pragma once

// Hook manager requires MinHook library to be linked
// Include this header only when MinHook is available

#include <vector>
#include <cstdint>

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

    bool IsInitialized() const { return m_initialized; }

    size_t GetHookCount() const { return m_hooks.size(); }

    // Non-copyable
    HookManager(const HookManager&) = delete;
    HookManager& operator=(const HookManager&) = delete;

private:
    HookManager() = default;
    ~HookManager() = default;

    bool m_initialized = false;
    std::vector<void*> m_hooks;
};

// RAII hook guard - automatically removes hook on destruction
class ScopedHook {
public:
    ScopedHook() = default;
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
