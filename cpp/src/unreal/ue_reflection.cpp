// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <cameraunlock/unreal/ue_reflection.h>

#include <cstdio>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cameraunlock::unreal {

using memory::Memory;
using memory::ModuleMap;
using memory::PlausiblePtr;

namespace {

constexpr int kMaxChain = 4096;
constexpr int kMaxSuperDepth = 64;
constexpr std::uint32_t kFuncNative = 0x00000400;
constexpr std::uint32_t kFuncBlueprintCallable = 0x04000000;

bool IsRealType(const std::string& type) { return type == "FloatProperty" || type == "DoubleProperty"; }

// Where the property chain hangs off a UStruct and how its links are laid out.
struct Chain {
    PropertyModel model;
    std::size_t child, next, name;
    std::uintptr_t first;  // Vector's first property
    std::size_t className = 0;
};

std::string FFieldName(const Objects& o, std::uintptr_t field, std::size_t nameOff) {
    return FNameToString(*o.mem, o.names, field + nameOff);
}

std::string FFieldClassName(const Objects& o, std::uintptr_t field, std::size_t nameOff) {
    return FNameToString(*o.mem, o.names, o.mem->Ptr(field + 8) + nameOff);
}

std::optional<Chain> FFieldChain(const Objects& o, std::uintptr_t vec, std::size_t super) {
    for (std::size_t child = super + 8; child <= super + 0x48; child += 8) {
        const std::uintptr_t x = o.mem->Ptr(vec + child);
        if (!PlausiblePtr(x)) continue;
        std::optional<std::size_t> className;
        for (const std::size_t c : {std::size_t{0}, std::size_t{8}})
            if (!className && IsRealType(FFieldClassName(o, x, c))) className = c;
        if (!className) continue;
        for (std::size_t name : {0x28, 0x20, 0x30, 0x18}) {
            if (FFieldName(o, x, name) != "X") continue;
            for (std::size_t next = 0x10; next < name; next += 8) {
                const std::uintptr_t y = o.mem->Ptr(x + next);
                if (!PlausiblePtr(y) || FFieldName(o, y, name) != "Y") continue;
                const std::uintptr_t z = o.mem->Ptr(y + next);
                if (PlausiblePtr(z) && FFieldName(o, z, name) == "Z")
                    return Chain{PropertyModel::FField, child, next, name, x, *className};
            }
        }
    }
    return std::nullopt;
}

std::optional<Chain> UPropertyChain(const Objects& o, std::uintptr_t vec, std::size_t super) {
    for (std::size_t child = super + 8; child <= super + 0x48; child += 8) {
        const std::uintptr_t x = o.mem->Ptr(vec + child);
        if (!PlausiblePtr(x) || o.Name(x) != "X" || !IsRealType(o.ClassName(x))) continue;
        for (std::size_t next = o.OuterOffset() + 8; next <= 0x48; next += 8) {
            const std::uintptr_t y = o.mem->Ptr(x + next);
            if (!PlausiblePtr(y) || o.Name(y) != "Y") continue;
            const std::uintptr_t z = o.mem->Ptr(y + next);
            if (PlausiblePtr(z) && o.Name(z) == "Z")
                return Chain{PropertyModel::UProperty, child, next, o.header.name, x};
        }
    }
    return std::nullopt;
}

std::int32_t I32(const Objects& o, std::uintptr_t at) { return o.mem->Or0<std::int32_t>(at); }

// The first dword offset in [from, to) where `test` holds for the three fields.
template <class Test>
std::optional<std::size_t> FirstOffset(std::size_t from, std::size_t to, Test&& test) {
    for (std::size_t d = from; d < to; d += 4)
        if (test(d)) return d;
    return std::nullopt;
}

bool SameCi(const char* a, const char* b, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

}  // namespace

std::uintptr_t Reflection::Super(std::uintptr_t ustruct) const {
    return ustruct ? objects_.mem->Ptr(ustruct + layout_.superStruct) : 0;
}

std::int32_t Reflection::StructSize(std::uintptr_t ustruct) const {
    return objects_.mem->Or0<std::int32_t>(ustruct + layout_.propertiesSize);
}

std::string Reflection::FieldName(std::uintptr_t field) const {
    return layout_.model == PropertyModel::FField ? FFieldName(objects_, field, layout_.fieldName)
                                                  : objects_.Name(field);
}

std::string Reflection::FieldType(std::uintptr_t field) const {
    return layout_.model == PropertyModel::FField ? FFieldClassName(objects_, field, layout_.fieldClassName)
                                                  : objects_.ClassName(field);
}

std::vector<Field> Reflection::Properties(std::uintptr_t ustruct) const {
    std::vector<Field> out;
    const Memory& mem = *objects_.mem;
    std::uintptr_t f = mem.Ptr(ustruct + layout_.childProperties);
    for (int n = 0; PlausiblePtr(f) && n < kMaxChain; ++n, f = mem.Ptr(f + layout_.fieldNext)) {
        Field field;
        field.addr = f;
        field.type = FieldType(f);
        // Before 4.25 the Children chain also carries the struct's functions.
        if (field.type.size() < 8 || field.type.compare(field.type.size() - 8, 8, "Property") != 0) continue;
        field.name = FieldName(f);
        field.offset = I32(objects_, f + layout_.offset);
        field.size = I32(objects_, f + layout_.elementSize) * I32(objects_, f + layout_.arrayDim);
        out.push_back(std::move(field));
    }
    return out;
}

bool NameMatches(const std::string& pattern, const std::string& name) {
    const bool head = !pattern.empty() && pattern.front() == '*';
    const bool tail = pattern.size() > 1 && pattern.back() == '*';
    const char* const core = pattern.data() + (head ? 1 : 0);
    const std::size_t n = pattern.size() - (head ? 1 : 0) - (tail ? 1 : 0);
    if (head && tail) {
        for (std::size_t i = 0; i + n <= name.size(); ++i)
            if (SameCi(name.data() + i, core, n)) return true;
        return false;
    }
    if (head) return name.size() >= n && SameCi(name.data() + name.size() - n, core, n);
    if (tail) return name.size() >= n && SameCi(name.data(), core, n);
    return name.size() == n && SameCi(name.data(), core, n);
}

std::optional<Field> Reflection::Find(std::uintptr_t ustruct, const std::string& name) const {
    for (int depth = 0; ustruct && depth < kMaxSuperDepth; ++depth, ustruct = Super(ustruct))
        for (Field& f : Properties(ustruct))
            if (NameMatches(name, f.name)) return std::move(f);
    return std::nullopt;
}

std::uintptr_t Reflection::PropertyStruct(const Field& f) const {
    return objects_.mem->Ptr(f.addr + layout_.propertyExtra);
}

bool Reflection::BoolBit(const Field& f, std::int32_t& byteOffset, std::uint8_t& mask) const {
    // FBoolProperty: FieldSize, ByteOffset, ByteMask, FieldMask.
    std::uint8_t bytes[4]{};
    if (f.type != "BoolProperty" || !objects_.mem->Read(f.addr + layout_.propertyExtra, bytes, 4)) return false;
    if (bytes[2] == 0) return false;
    byteOffset = f.offset + bytes[1];
    mask = bytes[2];
    return true;
}

std::uint32_t Reflection::FunctionFlags(std::uintptr_t function) const {
    return objects_.mem->Or0<std::uint32_t>(function + layout_.functionFlags);
}

std::uintptr_t Reflection::NativeFunc(std::uintptr_t function) const {
    return objects_.mem->Ptr(function + layout_.functionFunc);
}

bool Reflection::IsA(std::uintptr_t cls, std::uintptr_t base) const {
    for (int depth = 0; cls && depth < kMaxSuperDepth; ++depth, cls = Super(cls))
        if (cls == base) return true;
    return false;
}

std::optional<NetFunctionFlags> FindNetFunctionFlags(const Reflection& refl, std::string& why) {
    const Objects& o = refl.Objs();
    const std::uintptr_t pc = o.FindPath("/Script/Engine.PlayerController");
    if (!pc) {
        why = "PlayerController is not loaded";
        return std::nullopt;
    }
    const std::uintptr_t actor = o.FindPath("/Script/Engine.Actor");
    struct Fn {
        std::uint32_t flags;
        bool serverNamed, clientNamed;
    };
    std::vector<Fn> fns;
    o.ForEach([&](std::uintptr_t obj) {
        const std::uintptr_t outer = o.Outer(obj);
        if ((outer != pc && (!actor || outer != actor)) || o.ClassName(obj) != "Function") return false;
        const std::string name = o.Name(obj);
        fns.push_back({refl.FunctionFlags(obj), name.rfind("Server", 0) == 0, name.rfind("Client", 0) == 0});
        return false;
    });
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> found;  // server bits by net and client
    for (int nb = 0; nb < 32; ++nb) {
        const std::uint32_t net = 1u << nb;
        // The side bits appear only on functions that carry `net`.
        std::uint32_t outside = 0;
        for (const Fn& f : fns)
            if (!(f.flags & net)) outside |= f.flags;
        const std::uint32_t sides = ~outside & ~net;
        for (int sb = 0; sb < 32; ++sb) {
            const std::uint32_t server = 1u << sb;
            if (!(sides & server)) continue;
            for (int cb = 0; cb < 32; ++cb) {
                const std::uint32_t client = 1u << cb;
                if (!(sides & client) || client == server) continue;
                bool split = true;
                int servers = 0, clients = 0;
                for (const Fn& f : fns) {
                    if (!(f.flags & net)) continue;
                    const bool s = f.flags & server, c = f.flags & client;
                    if (s == c || (s && f.clientNamed) || (c && f.serverNamed)) {
                        split = false;
                        break;
                    }
                    servers += s && f.serverNamed;
                    clients += c && f.clientNamed;
                }
                if (split && servers && clients) found[{net, client}] |= server;
            }
        }
    }
    if (found.size() != 1) {
        why = std::to_string(found.size()) + " pairs of bits split PlayerController's remote functions into server and "
              "client ones, not one";
        return std::nullopt;
    }
    return NetFunctionFlags{found.begin()->first.first, found.begin()->first.second, found.begin()->second};
}

std::optional<std::int64_t> EnumValue(const Objects& objects, const std::string& path, const std::string& entry,
                                      std::string& why) {
    const Memory& mem = *objects.mem;
    const std::uintptr_t e = objects.FindPath(path);
    if (!e || objects.ClassName(e) != "Enum") {
        why = path + " is no enum that is loaded";
        return std::nullopt;
    }
    // Names holds TPair<FName, int64> from UE 4.16, TPair<FName, uint8> before,
    // and bare FNames, each worth its index, before that.
    struct Shape {
        std::size_t stride, value, size;
    };
    const std::size_t fname = FNameSize(objects.names);
    const Shape shapes[] = {{((fname + 7) & ~std::size_t{7}) + 8, (fname + 7) & ~std::size_t{7}, 8},
                            {fname + 4, fname, 1},
                            {fname, 0, 0}};
    constexpr std::size_t kFrom = 0x28, kTo = 0x80;
    constexpr std::int32_t kMostEntries = 4096;
    const auto bare = [](const std::string& name) {
        const std::size_t scope = name.rfind("::");
        return scope == std::string::npos ? name : name.substr(scope + 2);
    };
    struct Names {
        std::uintptr_t data;
        std::int32_t num;
        Shape shape;
    };
    std::vector<Names> arrays;
    for (std::size_t at = kFrom; at < kTo; at += 8) {
        const std::uintptr_t data = mem.Ptr(e + at);
        const std::int32_t num = mem.Or0<std::int32_t>(e + at + 8), max = mem.Or0<std::int32_t>(e + at + 12);
        if (!PlausiblePtr(data) || num < 1 || num > max || max > kMostEntries) continue;
        for (const Shape& shape : shapes) {
            const std::string last = bare(FNameToString(mem, objects.names, data + (num - 1) * shape.stride));
            if (last.size() >= 4 && last.compare(last.size() - 4, 4, "_MAX") == 0) arrays.push_back({data, num, shape});
        }
    }
    if (arrays.size() != 1) {
        why = path + " holds " + std::to_string(arrays.size()) + " arrays of names ending in _MAX, not one";
        return std::nullopt;
    }
    const auto [data, num, shape] = arrays[0];
    for (std::int32_t i = 0; i < num; ++i) {
        const std::uintptr_t at = data + i * shape.stride;
        if (bare(FNameToString(mem, objects.names, at)) != entry) continue;
        if (shape.size == 8) return mem.Or0<std::int64_t>(at + shape.value);
        if (shape.size == 1) return mem.Or0<std::uint8_t>(at + shape.value);
        return i;
    }
    why = path + " has no entry " + entry;
    return std::nullopt;
}

std::uint8_t EnumByte(const Objects& objects, const std::string& path, const std::string& entry) {
    std::string why;
    const auto v = EnumValue(objects, path, entry, why);
    if (!v) throw std::runtime_error(why);
    if (*v < 0 || *v > 0xff) throw std::runtime_error(path + "'s " + entry + " is " + std::to_string(*v) + ", not a byte");
    return static_cast<std::uint8_t>(*v);
}

CalibrationTypes FindCalibrationTypes(const Objects& objects) {
    const std::vector<std::string> paths{
        "/Script/CoreUObject.Vector", "/Script/CoreUObject.Rotator", "/Script/CoreUObject.Quat",
        "/Script/CoreUObject.Transform", "/Script/CoreUObject.Object", "/Script/Engine.Actor",
        "/Script/Engine.Controller", "/Script/Engine.PlayerController",
        "/Script/Engine.Controller:GetPlayerViewPoint", "/Script/Engine.Actor:K2_GetActorLocation",
        "/Script/Engine.Actor:K2_DestroyActor", "/Script/Engine.Actor:GetActorForwardVector",
        "/Script/Engine.Actor:K2_GetActorRotation", "/Script/Engine.Actor:GetActorRightVector",
        "/Script/Engine.Actor:GetActorUpVector",
    };
    const auto found = objects.FindPaths(paths);
    CalibrationTypes t;
    t.vector = found[0];
    t.rotator = found[1];
    t.quat = found[2];
    t.transform = found[3];
    t.object = found[4];
    t.actor = found[5];
    t.controller = found[6];
    t.playerController = found[7];
    for (std::size_t i = 8; i < found.size(); ++i) {
        if (found[i]) t.nativeCallables.push_back(found[i]);
        else t.missingCallables.push_back(paths[i]);
    }
    return t;
}

std::optional<ReflectionLayout> CalibrateReflection(const Objects& o, const ModuleMap& module,
                                                    const CalibrationTypes& t, std::string& why, bool& notLoaded) {
    const Memory& mem = *o.mem;
    if (!t.vector || !t.rotator || !t.quat || !t.transform || !t.object || !t.actor ||
        !t.controller || !t.playerController) {
        why = "an engine type calibration needs (Vector, Rotator, Quat, Transform, Object, Actor, "
              "Controller, PlayerController) is not in the object array";
        notLoaded = true;
        return std::nullopt;
    }
    if (t.nativeCallables.size() < 3) {
        why = "fewer than three of the native Actor/Controller functions calibration needs were found; missing:";
        for (const std::string& p : t.missingCallables) why += " " + p;
        // The engine makes its classes before their functions, and then makes
        // the functions one at a time, so a look in between finds the classes
        // and few or none of these, and notLoaded asks the caller to look again.
        notLoaded = true;
        return std::nullopt;
    }

    ReflectionLayout L;
    std::optional<std::size_t> super;
    for (std::size_t s = 0x28; s <= 0x90 && !super; s += 8)
        if (mem.Ptr(t.actor + s) == t.object && mem.Ptr(t.playerController + s) == t.controller &&
            mem.Ptr(t.vector + s) == 0)
            super = s;
    if (!super) {
        why = "no UStruct offset holds Actor->Object and PlayerController->Controller";
        return std::nullopt;
    }
    L.superStruct = *super;

    auto chain = FFieldChain(o, t.vector, L.superStruct);
    if (!chain) chain = UPropertyChain(o, t.vector, L.superStruct);
    if (!chain) {
        why = "Vector's property chain (X -> Y -> Z) was not found as FFields or as UProperties";
        return std::nullopt;
    }
    L.model = chain->model;
    L.childProperties = chain->child;
    L.fieldNext = chain->next;
    L.fieldName = chain->name;
    L.fieldClassName = chain->className;

    const std::uintptr_t x = chain->first;
    const std::uintptr_t y = mem.Ptr(x + L.fieldNext);
    const std::uintptr_t z = mem.Ptr(y + L.fieldNext);
    const std::string realType =
        L.model == PropertyModel::FField ? FFieldClassName(o, x, L.fieldClassName) : o.ClassName(x);
    L.realSize = realType == "DoubleProperty" ? 8 : 4;
    const std::int32_t e = static_cast<std::int32_t>(L.realSize);

    const std::size_t from = (L.model == PropertyModel::FField ? L.fieldName : L.fieldNext) + 8;
    const auto all = [&](std::size_t d, std::int32_t vx, std::int32_t vy, std::int32_t vz) {
        return I32(o, x + d) == vx && I32(o, y + d) == vy && I32(o, z + d) == vz;
    };
    // ArrayDim and ElementSize are adjacent; searched as a pair because the
    // object flags before them can read 1 on all three fields as well.
    const auto arrayDim = FirstOffset(from, from + 0x50, [&](std::size_t d) {
        return all(d, 1, 1, 1) && all(d + 4, e, e, e);
    });
    const auto offset = arrayDim ? FirstOffset(*arrayDim + 8, *arrayDim + 0x60, [&](std::size_t d) {
        return all(d, 0, e, 2 * e);
    }) : std::nullopt;
    if (!arrayDim || !offset) {
        why = "Vector's X/Y/Z do not show ArrayDim 1, a shared ElementSize and offsets 0, e, 2e";
        return std::nullopt;
    }
    L.arrayDim = *arrayDim;
    L.elementSize = *arrayDim + 4;
    L.offset = *offset;

    const auto propertiesSize = FirstOffset(L.childProperties + 8, L.childProperties + 0x40, [&](std::size_t d) {
        return I32(o, t.vector + d) == 3 * e && I32(o, t.rotator + d) == 3 * e && I32(o, t.quat + d) == 4 * e;
    });
    if (!propertiesSize) {
        why = "no UStruct offset holds Vector/Rotator/Quat sizes 3e, 3e, 4e";
        return std::nullopt;
    }
    L.propertiesSize = *propertiesSize;

    // Transform.Rotation is a StructProperty whose struct is Quat; the pointer
    // sits where every property subclass's own data begins.
    Reflection partial(o, L);
    std::optional<Field> rotation;
    for (Field& f : partial.Properties(t.transform))
        if (NameMatches("Rotation", f.name)) rotation = f;
    if (!rotation || rotation->type != "StructProperty") {
        why = "Transform has no Rotation StructProperty";
        return std::nullopt;
    }
    for (std::size_t d = (L.offset + 4 + 7) & ~std::size_t{7}; d <= L.offset + 0x80 && !L.propertyExtra; d += 8)
        if (mem.Ptr(rotation->addr + d) == t.quat) L.propertyExtra = d;
    if (!L.propertyExtra) {
        why = "Transform.Rotation does not point at Quat past its Offset field";
        return std::nullopt;
    }

    const auto flags = FirstOffset(L.propertiesSize + 4, L.propertiesSize + 0xA0, [&](std::size_t d) {
        for (std::uintptr_t fn : t.nativeCallables) {
            const auto v = mem.Or0<std::uint32_t>(fn + d);
            if (!(v & kFuncNative) || !(v & kFuncBlueprintCallable)) return false;
        }
        return true;
    });
    if (!flags) {
        why = "no UFunction offset carries FUNC_Native | FUNC_BlueprintCallable on every native callable";
        return std::nullopt;
    }
    L.functionFlags = *flags;
    for (std::size_t d = (L.functionFlags + 4 + 7) & ~std::size_t{7}; d <= L.functionFlags + 0x80; d += 8) {
        bool allCode = true;
        for (std::uintptr_t fn : t.nativeCallables) allCode = allCode && module.InCode(mem.Ptr(fn + d));
        if (allCode) {
            L.functionFunc = d;
            break;
        }
    }
    if (!L.functionFunc) {
        why = "no UFunction offset past FunctionFlags points into code on every native callable";
        return std::nullopt;
    }
    return L;
}

}  // namespace cameraunlock::unreal
