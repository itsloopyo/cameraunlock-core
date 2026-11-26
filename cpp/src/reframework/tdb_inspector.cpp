#include <cameraunlock/reframework/tdb_inspector.h>
#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/reframework/log_callback.h>

#include <cstring>
#include <cstdio>
#include <string>

namespace cameraunlock::reframework {

FieldKind ClassifyFieldType(::reframework::API::TypeDefinition* td) {
    if (!td) return FieldKind::Unknown;
    const char* ns = td->get_namespace();
    const char* nm = td->get_name();
    if (!nm) return FieldKind::Unknown;

    auto is_ns = [&](const char* expected) { return ns && strcmp(ns, expected) == 0; };
    auto is_nm = [&](const char* expected) { return strcmp(nm, expected) == 0; };

    if (is_ns("System")) {
        if (is_nm("String"))  return FieldKind::String;
        if (is_nm("Boolean")) return FieldKind::Bool;
        if (is_nm("Int32") || is_nm("SByte") || is_nm("Int16")) return FieldKind::Int32;
        if (is_nm("UInt32") || is_nm("Byte") || is_nm("UInt16")) return FieldKind::UInt32;
        if (is_nm("Int64"))   return FieldKind::Int64;
        if (is_nm("UInt64"))  return FieldKind::UInt64;
        if (is_nm("Single"))  return FieldKind::Float;
        if (is_nm("Double"))  return FieldKind::Double;
    }
    if (is_ns("via")) {
        if (is_nm("Float2") || is_nm("vec2") || is_nm("Vec2")) return FieldKind::Vec2;
        if (is_nm("Float3") || is_nm("vec3") || is_nm("Vec3")) return FieldKind::Vec3;
        if (is_nm("Float4") || is_nm("vec4") || is_nm("Vec4") || is_nm("Position")) return FieldKind::Vec4;
    }
    return FieldKind::Reference;
}

void DumpFieldsForType(
    ::reframework::API::TypeDefinition* type,
    ::reframework::API::ManagedObject* obj,
    int indent)
{
    if (!type || !obj) return;
    const char* tns = type->get_namespace();
    const char* tnm = type->get_name();

    auto fields = type->get_fields();
    if (fields.empty()) return;

    Log(LogLevel::Info, "%*s[fields of %s.%s]", indent, "",
        tns ? tns : "", tnm ? tnm : "?");

    char line[512];
    char nameBuf[256];

    for (auto f : fields) {
        if (!f) continue;
        if (f->is_static() || f->is_literal()) continue;
        const char* fname = f->get_name();
        if (!fname) continue;
        auto ftype = f->get_type();
        const char* ftns = ftype ? ftype->get_namespace() : "";
        const char* ftnm = ftype ? ftype->get_name() : "?";
        FieldKind kind = ClassifyFieldType(ftype);

        void* dataRefRaw = f->get_data_raw(obj, false);
        void* dataValRaw = f->get_data_raw(obj, true);

        switch (kind) {
            case FieldKind::String: {
                void* strPtr = dataRefRaw ? *reinterpret_cast<void**>(dataRefRaw) : nullptr;
                ReadManagedString(strPtr, nameBuf, sizeof(nameBuf));
                snprintf(line, sizeof(line), "%*s  %s : String = \"%s\"", indent, "", fname, nameBuf);
                break;
            }
            case FieldKind::Bool: {
                uint8_t v = dataValRaw ? *reinterpret_cast<uint8_t*>(dataValRaw) : 0;
                snprintf(line, sizeof(line), "%*s  %s : Boolean = %s", indent, "", fname, v ? "true" : "false");
                break;
            }
            case FieldKind::Int32: {
                int32_t v = dataValRaw ? *reinterpret_cast<int32_t*>(dataValRaw) : 0;
                snprintf(line, sizeof(line), "%*s  %s : %s = %d", indent, "", fname, ftnm ? ftnm : "int", v);
                break;
            }
            case FieldKind::UInt32: {
                uint32_t v = dataValRaw ? *reinterpret_cast<uint32_t*>(dataValRaw) : 0;
                snprintf(line, sizeof(line), "%*s  %s : %s = %u (0x%X)", indent, "", fname, ftnm ? ftnm : "uint", v, v);
                break;
            }
            case FieldKind::Int64: {
                int64_t v = dataValRaw ? *reinterpret_cast<int64_t*>(dataValRaw) : 0;
                snprintf(line, sizeof(line), "%*s  %s : Int64 = %lld", indent, "", fname, (long long)v);
                break;
            }
            case FieldKind::UInt64: {
                uint64_t v = dataValRaw ? *reinterpret_cast<uint64_t*>(dataValRaw) : 0;
                snprintf(line, sizeof(line), "%*s  %s : UInt64 = %llu", indent, "", fname, (unsigned long long)v);
                break;
            }
            case FieldKind::Float: {
                float v = dataValRaw ? *reinterpret_cast<float*>(dataValRaw) : 0.0f;
                snprintf(line, sizeof(line), "%*s  %s : Single = %.4f", indent, "", fname, v);
                break;
            }
            case FieldKind::Double: {
                double v = dataValRaw ? *reinterpret_cast<double*>(dataValRaw) : 0.0;
                snprintf(line, sizeof(line), "%*s  %s : Double = %.4f", indent, "", fname, v);
                break;
            }
            case FieldKind::Vec2: {
                float* v = reinterpret_cast<float*>(dataValRaw);
                if (v) snprintf(line, sizeof(line), "%*s  %s : vec2 = (%.3f, %.3f)", indent, "", fname, v[0], v[1]);
                else   snprintf(line, sizeof(line), "%*s  %s : vec2 = (null)", indent, "", fname);
                break;
            }
            case FieldKind::Vec3: {
                float* v = reinterpret_cast<float*>(dataValRaw);
                if (v) snprintf(line, sizeof(line), "%*s  %s : vec3 = (%.3f, %.3f, %.3f)", indent, "", fname, v[0], v[1], v[2]);
                else   snprintf(line, sizeof(line), "%*s  %s : vec3 = (null)", indent, "", fname);
                break;
            }
            case FieldKind::Vec4: {
                float* v = reinterpret_cast<float*>(dataValRaw);
                if (v) snprintf(line, sizeof(line), "%*s  %s : vec4 = (%.3f, %.3f, %.3f, %.3f)", indent, "", fname, v[0], v[1], v[2], v[3]);
                else   snprintf(line, sizeof(line), "%*s  %s : vec4 = (null)", indent, "", fname);
                break;
            }
            case FieldKind::Reference:
            default: {
                void* refPtr = dataRefRaw ? *reinterpret_cast<void**>(dataRefRaw) : nullptr;
                snprintf(line, sizeof(line), "%*s  %s : %s.%s = %p", indent, "", fname,
                    ftns ? ftns : "", ftnm ? ftnm : "?", refPtr);
                break;
            }
        }
        Log(LogLevel::Info, "%s", line);
    }
}

void DumpFieldsRecursive(
    ::reframework::API::TypeDefinition* type,
    ::reframework::API::ManagedObject* obj,
    int indent)
{
    int depth = 0;
    auto cur = type;
    while (cur && depth < 6) {
        const char* ns = cur->get_namespace();
        if (ns && strcmp(ns, "System") == 0) break;
        DumpFieldsForType(cur, obj, indent);
        cur = cur->get_parent_type();
        depth++;
    }
}

void EnumerateMethods(const char* typeName, const std::vector<const char*>& substrings) {
    const auto& api = ::reframework::API::get();
    auto tdb = api->tdb();
    auto type = tdb->find_type(typeName);
    if (!type) {
        Log(LogLevel::Info, "  [enum] %s: <type not found>", typeName);
        return;
    }
    auto methods = type->get_methods();
    Log(LogLevel::Info, "  [enum] %s: %zu methods", typeName, methods.size());
    int matched = 0;
    for (auto m : methods) {
        if (!m) continue;
        const char* name = m->get_name();
        if (!name) continue;
        bool keep = substrings.empty();
        for (auto sub : substrings) {
            if (strstr(name, sub)) { keep = true; break; }
        }
        if (!keep) continue;
        auto retType = m->get_return_type();
        const char* retName = retType ? retType->get_name() : "?";
        Log(LogLevel::Info, "    %s -> %s", name, retName ? retName : "?");
        matched++;
    }
    Log(LogLevel::Info, "  [enum] %s: %d matched", typeName, matched);
}

void LogInheritanceChain(const char* typeName) {
    const auto& api = ::reframework::API::get();
    auto tdb = api->tdb();
    auto type = tdb->find_type(typeName);
    if (!type) {
        Log(LogLevel::Info, "  [chain] %s: <not found>", typeName);
        return;
    }
    Log(LogLevel::Info, "  [chain] %s:", typeName);
    auto current = type;
    for (int depth = 0; depth < 8 && current; depth++) {
        const char* ns = current->get_namespace();
        const char* nm = current->get_name();
        Log(LogLevel::Info, "    depth=%d  %s.%s", depth,
            ns ? ns : "", nm ? nm : "?");
        current = current->get_parent_type();
    }
}

void LogMethodOverloads(const char* typeName, const char* methodName) {
    const auto& api = ::reframework::API::get();
    auto type = api->tdb()->find_type(typeName);
    if (!type) {
        Log(LogLevel::Info, "  [overloads] %s.%s: <type not found>", typeName, methodName);
        return;
    }
    int found = 0;
    for (auto m : type->get_methods()) {
        if (!m) continue;
        const char* name = m->get_name();
        if (!name || strcmp(name, methodName) != 0) continue;
        auto params = m->get_params();
        auto retType = m->get_return_type();
        const char* retName = retType ? retType->get_name() : "?";
        std::string sig;
        for (size_t i = 0; i < params.size(); i++) {
            if (i > 0) sig += ", ";
            auto* paramType = reinterpret_cast<::reframework::API::TypeDefinition*>(params[i].t);
            const char* pTypeName = paramType ? paramType->get_name() : "?";
            sig += pTypeName ? pTypeName : "?";
            sig += " ";
            sig += params[i].name ? params[i].name : "?";
        }
        Log(LogLevel::Info, "  [overloads] %s.%s(%s) -> %s  [params=%u]",
            typeName, methodName, sig.c_str(), retName ? retName : "?", m->get_num_params());
        found++;
    }
    if (found == 0) {
        Log(LogLevel::Info, "  [overloads] %s.%s: <no method found>", typeName, methodName);
    }
}

void LogObjectIdentity(const char* label, ::reframework::API::ManagedObject* mo) {
    if (!mo) {
        Log(LogLevel::Info, "  %s: null", label);
        return;
    }
    auto td = mo->get_type_definition();
    const char* tns = (td && td->get_namespace()) ? td->get_namespace() : "";
    const char* tnm = (td && td->get_name()) ? td->get_name() : "?";
    char goName[128] = "?";
    auto goRet = mo->invoke("get_GameObject", EmptyArgs());
    if (!goRet.exception_thrown && goRet.ptr) {
        auto go = reinterpret_cast<::reframework::API::ManagedObject*>(goRet.ptr);
        auto nameRet = go->invoke("get_Name", EmptyArgs());
        if (!nameRet.exception_thrown && nameRet.ptr) {
            ReadManagedString(nameRet.ptr, goName, sizeof(goName));
        }
    }
    Log(LogLevel::Info, "  %s: type=%s.%s  GO=%s", label, tns, tnm, goName);
}

void LogGetterString(::reframework::API::ManagedObject* obj, const char* method, const char* label) {
    if (!obj) return;
    auto ret = obj->invoke(method, EmptyArgs());
    if (ret.exception_thrown) return;
    char buf[256];
    ReadManagedString(ret.ptr, buf, sizeof(buf));
    Log(LogLevel::Info, "  %s = \"%s\"", label, buf);
}

void LogGetterBool(::reframework::API::ManagedObject* obj, const char* method, const char* label) {
    if (!obj) return;
    auto ret = obj->invoke(method, EmptyArgs());
    if (ret.exception_thrown) return;
    Log(LogLevel::Info, "  %s = %s", label, ret.byte ? "true" : "false");
}

void LogGetterU32(::reframework::API::ManagedObject* obj, const char* method, const char* label) {
    if (!obj) return;
    auto ret = obj->invoke(method, EmptyArgs());
    if (ret.exception_thrown) return;
    Log(LogLevel::Info, "  %s = %u", label, ret.dword);
}

void LogGetterPtr(::reframework::API::ManagedObject* obj, const char* method, const char* label) {
    if (!obj) return;
    auto ret = obj->invoke(method, EmptyArgs());
    if (ret.exception_thrown) return;
    if (!ret.ptr) { Log(LogLevel::Info, "  %s = null", label); return; }
    auto mo = reinterpret_cast<::reframework::API::ManagedObject*>(ret.ptr);
    auto td = mo->get_type_definition();
    Log(LogLevel::Info, "  %s = %p (type=%s.%s)", label, ret.ptr,
        td && td->get_namespace() ? td->get_namespace() : "",
        td && td->get_name() ? td->get_name() : "?");
}

} // namespace cameraunlock::reframework
