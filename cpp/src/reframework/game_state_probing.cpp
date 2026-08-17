#include <cameraunlock/reframework/game_state_probing.h>
#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/reframework/log_callback.h>

#include <windows.h>
#include <cstring>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>

namespace cameraunlock::reframework {

// Default namespace candidates covering RE2 through RE9
static const char* s_defaultCandidates[] = {
    "app", "app.gui", "app.ropeway", "app.ropeway.gui",
    "requiem", "requiem.gui", "chainsaw", "chainsaw.gui",
};
static const char* const* s_candidates = s_defaultCandidates;
static int s_candidateCount = sizeof(s_defaultCandidates) / sizeof(s_defaultCandidates[0]);

void SetNamespaceCandidates(const char* const* candidates, int count) {
    s_candidates = candidates;
    s_candidateCount = count;
}

::reframework::API::TypeDefinition* FindType(
    ::reframework::API::TDB* tdb, const char* baseName) {
    // Try without prefix first (e.g., "via.Application")
    auto type = tdb->find_type(baseName);
    if (type) return type;

    for (int i = 0; i < s_candidateCount; i++) {
        std::string fullName = std::string(s_candidates[i]) + "." + baseName;
        type = tdb->find_type(fullName.c_str());
        if (type) return type;
    }
    return nullptr;
}

const char* FindSingleton(const ::reframework::API* api, const char* baseName) {
    static char buf[256];
    for (int i = 0; i < s_candidateCount; i++) {
        snprintf(buf, sizeof(buf), "%s.%s", s_candidates[i], baseName);
        if (api->get_managed_singleton(buf)) return buf;
    }
    return nullptr;
}

// Backing store for MethodCheck::singletonName. A deque never relocates the
// elements it already holds, so every pointer handed out stays valid for the
// process lifetime. The 16-entry ring this replaced silently repointed an
// earlier check's name at a different singleton on the 17th probe, and
// InvokeBool/InvokeInt resolve the singleton by that name on every call - so
// the check then invoked a method resolved for one type against an unrelated
// object.
static const char* InternSingletonName(const char* name) {
    static std::deque<std::string> store;
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& stored : store) {
        if (stored == name) return stored.c_str();
    }
    store.emplace_back(name);
    return store.back().c_str();
}

::reframework::API::Method* FindMethod(
    ::reframework::API::TypeDefinition* type, const char* names[], int count) {
    for (int i = 0; i < count; i++) {
        auto m = type->find_method(names[i]);
        if (m) return m;
    }
    return nullptr;
}

bool ProbeManager(
    ::reframework::API::TDB* tdb,
    const ::reframework::API* api,
    const char* typeName,
    const char* methodNames[],
    int methodCount,
    MethodCheck& out,
    const char* label) {

    auto type = FindType(tdb, typeName);
    if (!type) return false;

    auto method = FindMethod(type, methodNames, methodCount);
    if (!method) return false;

    auto singleton = FindSingleton(api, typeName);
    if (!singleton) return false;

    out.method = method;
    out.singletonName = InternSingletonName(singleton);

    Log(LogLevel::Info, "Probe OK: %s -> %s (singleton: %s)", label, methodNames[0], out.singletonName);
    return true;
}

bool InvokeBool(const ::reframework::API* api, void* vmCtx,
                MethodCheck& check, bool diag, const char* label) {
    if (!check.method || check.failed) return false;
    void* singleton = api->get_managed_singleton(check.singletonName);
    if (!singleton) return false;
    __try {
        auto ret = check.method->invoke(
            reinterpret_cast<::reframework::API::ManagedObject*>(singleton), EmptyArgs());
        if (diag) Log(LogLevel::Info, "Diag: %s byte=%u dword=%u", label, ret.byte, ret.dword);
        return ret.byte != 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        check.failed = true;
        Log(LogLevel::Warning, "Diag: %s crashed, disabling", label);
        return false;
    }
}

uint32_t InvokeInt(const ::reframework::API* api, void* vmCtx,
                   MethodCheck& check, bool diag, const char* label) {
    if (!check.method || check.failed) return 0;
    void* singleton = api->get_managed_singleton(check.singletonName);
    if (!singleton) return 0;
    __try {
        auto ret = check.method->invoke(
            reinterpret_cast<::reframework::API::ManagedObject*>(singleton), EmptyArgs());
        if (diag) Log(LogLevel::Info, "Diag: %s dword=%u", label, ret.dword);
        return ret.dword;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        check.failed = true;
        Log(LogLevel::Warning, "Diag: %s crashed, disabling", label);
        return 0;
    }
}

void* InvokePointer(const ::reframework::API* api, void* vmCtx,
                    MethodCheck& check, bool diag, const char* label) {
    if (!check.method || check.failed) return (void*)1;
    void* singleton = api->get_managed_singleton(check.singletonName);
    if (!singleton) return (void*)1;
    __try {
        auto ret = check.method->invoke(
            reinterpret_cast<::reframework::API::ManagedObject*>(singleton), EmptyArgs());
        if (diag) Log(LogLevel::Info, "Diag: %s ptr=%p", label, ret.ptr);
        return ret.ptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        check.failed = true;
        Log(LogLevel::Warning, "Diag: %s crashed, disabling", label);
        return (void*)1;
    }
}

} // namespace cameraunlock::reframework
