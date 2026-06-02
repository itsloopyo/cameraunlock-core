#include <cameraunlock/reframework/managed_utils.h>

#include <windows.h>
#include <span>

namespace cameraunlock::reframework {

static const std::vector<void*> s_emptyArgs{};

const std::vector<void*>& EmptyArgs() {
    return s_emptyArgs;
}

void ReadManagedString(void* stringPtr, char* out, size_t outSize) {
    if (!stringPtr || outSize == 0) { if (out) out[0] = 0; return; }
    out[0] = 0;
    // System.String layout: int32 length at +0x10, UTF-16 char data at +0x14.
    constexpr size_t kStringLengthOffset = 0x10;
    constexpr size_t kStringCharsOffset = 0x14;
    __try {
        auto* raw = reinterpret_cast<uint8_t*>(stringPtr);
        uint32_t strLen = *reinterpret_cast<uint32_t*>(raw + kStringLengthOffset);
        if (strLen > outSize - 1) strLen = static_cast<uint32_t>(outSize - 1);
        auto* chars = reinterpret_cast<const uint16_t*>(raw + kStringCharsOffset);
        for (uint32_t i = 0; i < strLen; i++) {
            uint16_t c = chars[i];
            out[i] = (c >= 32 && c < 127) ? (char)c : '?';
        }
        out[strLen] = 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        out[0] = 0;
    }
}

void* CallMethod(::reframework::API::Method* method, void* obj) {
    auto ret = method->invoke(reinterpret_cast<::reframework::API::ManagedObject*>(obj), s_emptyArgs);
    if (ret.exception_thrown) return nullptr;
    return ret.ptr;
}

bool CallMethodBool(::reframework::API::Method* method, void* obj) {
    auto ret = method->invoke(reinterpret_cast<::reframework::API::ManagedObject*>(obj), s_emptyArgs);
    if (ret.exception_thrown) return false;
    return ret.byte != 0;
}

void* CallMethodArg(::reframework::API::Method* method, void* obj, void* arg) {
    auto ret = InvokeMethodWithArg(
        method, reinterpret_cast<::reframework::API::ManagedObject*>(obj), arg);
    if (ret.exception_thrown) return nullptr;
    return ret.ptr;
}

::reframework::InvokeRet InvokeMethodWithArg(
    ::reframework::API::Method* method, ::reframework::API::ManagedObject* obj, void* arg) {
    void* args[1] = { arg };
    return method->invoke(obj, std::span<void*>(args, 1));
}

::reframework::InvokeRet CachedGetter::Invoke(::reframework::API::ManagedObject* obj) {
    auto t = obj->get_type_definition();
    if (!t) return {};
    if (t != td) {
        td = t;
        method = t->find_method(name);
    }
    if (!method) return {};
    return method->invoke(obj, s_emptyArgs);
}

std::vector<::reframework::API::TypeDefinition*> FindTypesByShortName(const char* shortName) {
    std::vector<::reframework::API::TypeDefinition*> matches;
    const auto& api = ::reframework::API::get();
    auto tdb = api->tdb();
    auto numTypes = tdb->get_num_types();
    for (uint32_t i = 0; i < numTypes; i++) {
        auto type = tdb->get_type(i);
        if (!type) continue;
        const char* name = type->get_name();
        if (name && strcmp(name, shortName) == 0) matches.push_back(type);
    }
    return matches;
}

::reframework::API::ManagedObject* ArrayGetValue(
    ::reframework::API::ManagedObject* arr, int i) {
    if (!arr) return nullptr;
    // GetValue lives on System.Array, so the resolved Method* is shared across
    // every managed array type. Cache it against the last seen array type to
    // skip the cross-DLL string method search on tight per-frame array loops,
    // and pass the index through a stack span to avoid a per-call heap vector.
    static ::reframework::API::TypeDefinition* s_td = nullptr;
    static ::reframework::API::Method* s_getValue = nullptr;
    auto td = arr->get_type_definition();
    if (!td) return nullptr;
    if (td != s_td) {
        s_td = td;
        s_getValue = td->find_method("GetValue");
    }
    if (!s_getValue) return nullptr;
    void* idxArgs[1] = { (void*)(uintptr_t)i };
    auto ret = s_getValue->invoke(arr, std::span<void*>(idxArgs, 1));
    if (ret.exception_thrown) return nullptr;
    return reinterpret_cast<::reframework::API::ManagedObject*>(ret.ptr);
}

::reframework::InvokeRet InvokeCached(::reframework::API::ManagedObject* obj,
                                      ::reframework::API::Method*& cache,
                                      const char* methodName,
                                      const std::vector<void*>& args) {
    if (!cache) {
        auto td = obj->get_type_definition();
        if (td) cache = td->find_method(methodName);
        if (!cache) return obj->invoke(methodName, args);
    }
    return cache->invoke(obj, args);
}

::reframework::API::Method* FindMethodByParamCount(
    const char* typeName, const char* methodName, uint32_t paramCount) {
    const auto& api = ::reframework::API::get();
    auto type = api->tdb()->find_type(typeName);
    if (!type) return nullptr;
    for (auto m : type->get_methods()) {
        if (!m) continue;
        const char* name = m->get_name();
        if (!name || strcmp(name, methodName) != 0) continue;
        if (m->get_num_params() != paramCount) continue;
        return m;
    }
    return nullptr;
}

::reframework::API::Method* FindMethodByParamTypeName(
    const char* typeName, const char* methodName, const char* paramTypeShortName) {
    const auto& api = ::reframework::API::get();
    auto type = api->tdb()->find_type(typeName);
    if (!type) return nullptr;
    for (auto m : type->get_methods()) {
        if (!m) continue;
        const char* name = m->get_name();
        if (!name || strcmp(name, methodName) != 0) continue;
        if (m->get_num_params() != 1) continue;
        auto params = m->get_params();
        if (params.size() != 1 || !params[0].t) continue;
        auto pt = reinterpret_cast<::reframework::API::TypeDefinition*>(params[0].t);
        const char* ptName = pt ? pt->get_name() : nullptr;
        if (ptName && strcmp(ptName, paramTypeShortName) == 0) return m;
    }
    return nullptr;
}

} // namespace cameraunlock::reframework
