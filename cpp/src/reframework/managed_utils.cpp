#include <cameraunlock/reframework/managed_utils.h>

namespace cameraunlock::reframework {

static const std::vector<void*> s_emptyArgs{};

const std::vector<void*>& EmptyArgs() {
    return s_emptyArgs;
}

void ReadManagedString(void* stringPtr, char* out, size_t outSize) {
    if (!stringPtr || outSize == 0) { if (out) out[0] = 0; return; }
    const wchar_t* wstr = reinterpret_cast<const wchar_t*>(
        reinterpret_cast<uint8_t*>(stringPtr) + 0x14);
    size_t i = 0;
    for (; i + 1 < outSize && wstr[i]; i++) {
        wchar_t c = wstr[i];
        out[i] = (c >= 32 && c < 127) ? (char)c : '?';
    }
    out[i] = 0;
}

void* CallMethod(::reframework::API::Method* method, void* obj) {
    auto ret = method->invoke(reinterpret_cast<::reframework::API::ManagedObject*>(obj), s_emptyArgs);
    return ret.ptr;
}

::reframework::API::ManagedObject* ArrayGetValue(
    ::reframework::API::ManagedObject* arr, int i) {
    if (!arr) return nullptr;
    std::vector<void*> idxArgs = { (void*)(uintptr_t)i };
    auto ret = arr->invoke("GetValue", idxArgs);
    if (ret.exception_thrown) return nullptr;
    return reinterpret_cast<::reframework::API::ManagedObject*>(ret.ptr);
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

} // namespace cameraunlock::reframework
