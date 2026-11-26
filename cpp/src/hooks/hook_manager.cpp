#include <cameraunlock/hooks/hook_manager.h>

// MinHook must be available when compiling this file
// Users should ensure MinHook is linked when using this module
#include <MinHook.h>

#include <algorithm>

namespace cameraunlock::hooks {

namespace {

HookStatus FromMHStatus(MH_STATUS status) {
    switch (status) {
        case MH_OK: return HookStatus::Ok;
        case MH_ERROR_ALREADY_INITIALIZED: return HookStatus::ErrorAlreadyInitialized;
        case MH_ERROR_NOT_INITIALIZED: return HookStatus::ErrorNotInitialized;
        case MH_ERROR_ALREADY_CREATED: return HookStatus::ErrorAlreadyCreated;
        case MH_ERROR_NOT_CREATED: return HookStatus::ErrorNotCreated;
        case MH_ERROR_ENABLED: return HookStatus::ErrorEnabled;
        case MH_ERROR_DISABLED: return HookStatus::ErrorDisabled;
        case MH_ERROR_NOT_EXECUTABLE: return HookStatus::ErrorNotExecutable;
        case MH_ERROR_UNSUPPORTED_FUNCTION: return HookStatus::ErrorUnsupportedFunction;
        case MH_ERROR_MEMORY_ALLOC: return HookStatus::ErrorMemoryAlloc;
        case MH_ERROR_MEMORY_PROTECT: return HookStatus::ErrorMemoryProtect;
        case MH_ERROR_MODULE_NOT_FOUND: return HookStatus::ErrorModuleNotFound;
        case MH_ERROR_FUNCTION_NOT_FOUND: return HookStatus::ErrorFunctionNotFound;
        default: return HookStatus::Unknown;
    }
}

} // anonymous namespace

const char* HookStatusToString(HookStatus status) {
    switch (status) {
        case HookStatus::Ok: return "Ok";
        case HookStatus::ErrorAlreadyInitialized: return "Already initialized";
        case HookStatus::ErrorNotInitialized: return "Not initialized";
        case HookStatus::ErrorAlreadyCreated: return "Hook already created";
        case HookStatus::ErrorNotCreated: return "Hook not created";
        case HookStatus::ErrorEnabled: return "Hook enabled";
        case HookStatus::ErrorDisabled: return "Hook disabled";
        case HookStatus::ErrorNotExecutable: return "Target not executable";
        case HookStatus::ErrorUnsupportedFunction: return "Unsupported function";
        case HookStatus::ErrorMemoryAlloc: return "Memory allocation failed";
        case HookStatus::ErrorMemoryProtect: return "Memory protection change failed";
        case HookStatus::ErrorModuleNotFound: return "Module not found";
        case HookStatus::ErrorFunctionNotFound: return "Function not found";
        default: return "Unknown error";
    }
}

HookManager& HookManager::Instance() {
    static HookManager instance;
    return instance;
}

HookStatus HookManager::Initialize() {
    if (m_initialized) {
        return HookStatus::ErrorAlreadyInitialized;
    }

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) {
        return FromMHStatus(status);
    }

    m_initialized = true;
    return HookStatus::Ok;
}

void HookManager::Shutdown() {
    if (!m_initialized) {
        return;
    }

    DisableAllHooks();
    MH_Uninitialize();

    m_hooks.clear();
    m_initialized = false;
}

HookStatus HookManager::CreateHook(void* target, void* detour, void** original) {
    if (!m_initialized) {
        return HookStatus::ErrorNotInitialized;
    }

    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status != MH_OK) {
        return FromMHStatus(status);
    }

    m_hooks.push_back(target);
    return HookStatus::Ok;
}

HookStatus HookManager::RemoveHook(void* target) {
    if (!m_initialized) {
        return HookStatus::ErrorNotInitialized;
    }

    MH_STATUS status = MH_RemoveHook(target);
    if (status != MH_OK) {
        return FromMHStatus(status);
    }

    auto it = std::find(m_hooks.begin(), m_hooks.end(), target);
    if (it != m_hooks.end()) {
        m_hooks.erase(it);
    }

    return HookStatus::Ok;
}

HookStatus HookManager::EnableHook(void* target) {
    if (!m_initialized) {
        return HookStatus::ErrorNotInitialized;
    }

    return FromMHStatus(MH_EnableHook(target));
}

HookStatus HookManager::DisableHook(void* target) {
    if (!m_initialized) {
        return HookStatus::ErrorNotInitialized;
    }

    return FromMHStatus(MH_DisableHook(target));
}

HookStatus HookManager::EnableAllHooks() {
    if (!m_initialized) {
        return HookStatus::ErrorNotInitialized;
    }

    return FromMHStatus(MH_EnableHook(MH_ALL_HOOKS));
}

HookStatus HookManager::DisableAllHooks() {
    if (!m_initialized) {
        return HookStatus::ErrorNotInitialized;
    }

    return FromMHStatus(MH_DisableHook(MH_ALL_HOOKS));
}

void HookManager::RemoveAllHooks() {
    if (!m_initialized) {
        return;
    }

    for (void* target : m_hooks) {
        MH_DisableHook(target);
        MH_RemoveHook(target);
    }
    m_hooks.clear();
}


ScopedHook::ScopedHook(void* target, void* detour, void** original) {
    Create(target, detour, original);
}

ScopedHook::~ScopedHook() {
    if (m_target) {
        HookManager::Instance().DisableHook(m_target);
        HookManager::Instance().RemoveHook(m_target);
    }
}

ScopedHook::ScopedHook(ScopedHook&& other) noexcept
    : m_target(other.m_target) {
    other.m_target = nullptr;
}

ScopedHook& ScopedHook::operator=(ScopedHook&& other) noexcept {
    if (this != &other) {
        if (m_target) {
            HookManager::Instance().DisableHook(m_target);
            HookManager::Instance().RemoveHook(m_target);
        }
        m_target = other.m_target;
        other.m_target = nullptr;
    }
    return *this;
}

HookStatus ScopedHook::Create(void* target, void* detour, void** original) {
    if (m_target) {
        return HookStatus::ErrorAlreadyCreated;
    }

    auto status = HookManager::Instance().CreateHook(target, detour, original);
    if (status != HookStatus::Ok) {
        return status;
    }

    status = HookManager::Instance().EnableHook(target);
    if (status != HookStatus::Ok) {
        HookManager::Instance().RemoveHook(target);
        return status;
    }

    m_target = target;
    return HookStatus::Ok;
}

void* ScopedHook::Release() {
    void* target = m_target;
    m_target = nullptr;
    return target;
}

} // namespace cameraunlock::hooks
