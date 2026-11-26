// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <cameraunlock/re_engine/scene.h>

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

#include <cameraunlock/logging/file_log.h>
#include <cameraunlock/memory/safe_memory.h>

namespace cameraunlock::re_engine {
namespace {

// The type an object's info names, or null when the object or its info does not read.
const Type* TypeRead(void* object) {
    std::uintptr_t info = 0, type = 0;
    if (!object || !memory::SafeRead(reinterpret_cast<std::uintptr_t>(object), info) || !info ||
        !memory::SafeRead(info, type))
        return nullptr;
    return reinterpret_cast<const Type*>(type);
}

// Whether a managed array's object info reads. Its type is not checked against
// the type database's table, which the layer has not seen hold array types.
bool Headed(void* array) { return TypeRead(array) != nullptr; }

const Type& Need(const Tdb& tdb, const char* name) {
    const Type* t = tdb.FindType(name);
    if (!t) throw std::runtime_error(std::string("the type database has no ") + name);
    return *t;
}

Method Lookup(const char* type, const char* name, int numParams, const char* returns) {
    const Tdb& tdb = Tdb::Get();
    const Method m = tdb.FindMethod(Need(tdb, type), name, numParams);
    if (!m.fn) throw std::runtime_error(std::string(type) + " has no " + name + " with code");
    if (returns && (!m.returnType || tdb.FullName(*m.returnType) != returns))
        throw std::runtime_error(std::string(type) + "." + name + " returns " + (m.returnType ? tdb.FullName(*m.returnType) : "a type past the type database") +
                                 ", not the " + returns + " the layer reads");
    return m;
}

}  // namespace

void* MethodCode(const char* type, const char* name, int numParams, const char* returns) {
    return Lookup(type, name, numParams, returns).fn;
}

void* StaticMethodCode(const char* type, const char* name, int numParams, const char* returns) {
    const Method m = Lookup(type, name, numParams, returns);
    if (!m.isStatic) throw std::runtime_error(std::string(type) + "." + name + " takes an object, so it is not called as a static");
    return m.fn;
}

void* MethodCodeTaking(const std::string& who, const char* type, const char* name, const char* paramType) {
    const Tdb& tdb = Tdb::Get();
    const Method m = tdb.FindMethodTaking(Need(tdb, type), name, paramType);
    if (!m.fn) throw std::runtime_error(who + ": " + type + " has no " + name + "(" + paramType + ") with code");
    return m.fn;
}

const Type& TypeNamed(const std::string& who, const std::string& name) {
    const Tdb& tdb = Tdb::Get();
    const Type* type = tdb.FindType(name);
    if (!type) type = tdb.FindTypeByShortName(name);
    if (!type) throw std::runtime_error(who + ": the type database has no type named " + name);
    return *type;
}

std::wstring Wide(std::string_view ascii) { return std::wstring(ascii.begin(), ascii.end()); }

std::string Narrow(std::wstring_view name) {
    std::string out;
    out.reserve(name.size());
    for (const wchar_t c : name) out += c < 0x80 ? static_cast<char>(c) : '?';
    return out;
}

Scene::Scene() {
    const Tdb& tdb = Tdb::Get();
    getCurrentScene_ = reinterpret_cast<Static>(StaticMethodCode("via.SceneManager", "get_CurrentScene", 0));
    findComponents_ = reinterpret_cast<Find>(MethodCode("via.Scene", "findComponents", 1));
    getGameObject_ = reinterpret_cast<Getter>(MethodCode("via.Component", "get_GameObject", 0));
    getTransform_ = reinterpret_cast<Getter>(MethodCode("via.GameObject", "get_Transform", 0));
    worldMatrixOf_ = reinterpret_cast<MatrixGetter>(MethodCode("via.Transform", "get_WorldMatrix", 0, "via.mat4"));
    gameObjectType_ = &Need(tdb, "via.GameObject");
    componentType_ = &Need(tdb, "via.Component");
    transformType_ = &Need(tdb, "via.Transform");
    std::vector<std::string> cells;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column) cells.push_back("m" + std::to_string(row) + std::to_string(column));
    tdb.RequireFloats(Need(tdb, "via.mat4"), cells, sizeof(float) * 16);
    // System.Activator.CreateInstance(System.Type), static: (ctx, type).
    createInstance_ = reinterpret_cast<Getter>(StaticMethodCode("System.Activator", "CreateInstance", 1));

    const Type& array = Need(tdb, "System.Array");
    arrayLength_ = reinterpret_cast<Length>(MethodCode("System.Array", "get_Length", 0, "System.Int32"));
    const Method at = tdb.FindMethodTaking(array, "GetValue", "System.Int32");
    if (!at.fn || !at.returnType || tdb.FullName(*at.returnType) != "System.Object")
        throw std::runtime_error("System.Array has no GetValue(System.Int32) returning System.Object with code");
    arrayAt_ = reinterpret_cast<At>(at.fn);

    // Where a string keeps its length and characters: the type database's
    // _stringLength and _firstChar where it gives them (Requiem). Resident Evil
    // 4's gives System.String no instance fields, so there CheckFields finds the
    // length in strings it makes, and the characters follow it.
    const Type& string = Need(tdb, "System.String");
    stringType_ = &string;
    const Field length = tdb.FindField(string, "_stringLength");
    const Field chars = tdb.FindField(string, "_firstChar");
    const bool named = length.offset >= 0 || chars.offset >= 0;
    if (named && (length.offset < 0 || length.integerSize != 4 || chars.offset < 0 || chars.typeName != "System.Char"))
        throw std::runtime_error("System.String has no 4-byte _stringLength and System.Char _firstChar fields");
    if (named) {
        stringLength_ = length.offset;
        stringChars_ = chars.offset;
    } else {
        logging::Line("scene: the type database gives System.String no _stringLength or _firstChar, so strings made on the "
                      "first frame show where they are");
    }
    // A string `length` long: FastAllocateString where the database has it
    // (Requiem). Resident Evil 4's has none, so there the engine's version
    // string is cut to nothing with Substring(0, 0) and padded out with
    // PadLeft(length, ' '), the way REFramework's create_managed_string makes one.
    const Method allocate = tdb.FindMethodTaking(string, "FastAllocateString", "System.Int32");
    if (allocate.fn) {
        if (!allocate.isStatic) throw std::runtime_error("System.String.FastAllocateString(System.Int32) takes an object");
        allocateString_ = reinterpret_cast<Allocate>(allocate.fn);
        return;
    }
    const Method cut = tdb.FindMethod(string, "Substring", 2);
    const Method pad = tdb.FindMethod(string, "PadLeft", 2);
    const auto answersString = [&tdb](const Method& m) { return m.returnType && tdb.FullName(*m.returnType) == "System.String"; };
    if (!cut.fn || cut.isStatic || !answersString(cut) || !pad.fn || pad.isStatic || !answersString(pad))
        throw std::runtime_error("System.String has neither a static FastAllocateString(System.Int32) nor a Substring(System.Int32, "
                                 "System.Int32) and a PadLeft(System.Int32, System.Char) answering a string, with code");
    seedString_ = reinterpret_cast<Static>(StaticMethodCode("via.version", "getVersionString", 0, "System.String"));
    substring_ = reinterpret_cast<Cut>(cut.fn);
    padLeft_ = reinterpret_cast<Pad>(pad.fn);
    logging::Line("scene: System.String has no FastAllocateString, so strings are made by padding via.version.getVersionString "
                  "cut to nothing with Substring(0, 0) with PadLeft");
}

bool Scene::MakeString(void* ctx, std::int32_t length, void*& out) const {
    out = nullptr;
    if (allocateString_) {
        out = allocateString_(ctx, length);
        return true;
    }
    void* seed = seedString_(ctx);
    void* none = nullptr;
    const char* failed = nullptr;
    if (!seed)
        failed = "via.version.getVersionString answered null";
    else if (!Is(seed, stringType_))
        failed = "via.version.getVersionString answered what does not read as a System.String";
    else if (!(none = substring_(ctx, seed, 0, 0)))
        failed = "Substring(0, 0) answered null";
    else if (!Is(none, stringType_))
        failed = "Substring(0, 0) answered what does not read as a System.String";
    else
        out = padLeft_(ctx, none, length, L' ');
    if (failed && !saidMakeFailed_) {
        saidMakeFailed_ = true;
        logging::Line("scene: making a System.String %d long: %s", length, failed);
    }
    return !failed;
}

void* Scene::RuntimeType(void* ctx, const Type& type) {
    void*& runtimeType = runtimeTypes_[&type];
    if (!runtimeType) {
        runtimeType = Tdb::Get().RuntimeType(ctx, type);
        if (!runtimeType) throw std::runtime_error("System.Object.GetType gave no type for " + Tdb::Get().FullName(type));
    }
    return runtimeType;
}

void* Scene::FirstComponent(void* ctx, const Type& type) {
    // Only the first element is read, not the whole array as Components does.
    void* array = Search(ctx, RuntimeType(ctx, type));
    if (!Headed(array) || arrayLength_(ctx, array) <= 0) return nullptr;
    return arrayAt_(ctx, array, 0);
}

std::vector<void*> Scene::Components(void* ctx, const Type& type) { return Elements(ctx, Search(ctx, RuntimeType(ctx, type))); }

std::vector<void*> Scene::Elements(void* ctx, void* array) const {
    if (!Headed(array)) return {};
    const std::int32_t n = arrayLength_(ctx, array);
    if (n < 0) return {};
    std::vector<void*> out(static_cast<std::size_t>(n));
    for (std::int32_t i = 0; i < n; ++i) out[static_cast<std::size_t>(i)] = arrayAt_(ctx, array, i);
    return out;
}

std::wstring Scene::Text(void* systemString) const {
    if (!systemString) return {};
    if (stringLength_ < 0) throw std::logic_error("Scene::Text before CheckFields found where strings keep their length");
    const std::int32_t length = *static_cast<const std::int32_t*>(FieldAddress(systemString, stringLength_));
    if (length < 0 || length > 4096) throw std::runtime_error("a System.String read with length " + std::to_string(length));
    return std::wstring(static_cast<const wchar_t*>(FieldAddress(systemString, stringChars_)), static_cast<std::size_t>(length));
}

void* Scene::NewString(void* ctx, std::wstring_view text) const {
    void* s = nullptr;
    if (!MakeString(ctx, static_cast<std::int32_t>(text.size()), s) || !s) return nullptr;
    std::memcpy(FieldAddress(s, stringChars_), text.data(), text.size() * sizeof(wchar_t));
    return s;
}

bool Scene::CheckFields(void* ctx) {
    constexpr std::int32_t kLength = 37;
    if (stringLength_ < 0) {
        // Two strings of other lengths: the length is the one 4-byte place in
        // their first bytes from the field base that holds each one's own, and
        // the characters start right after it.
        constexpr std::int32_t kLengths[] = {41, 53};
        constexpr int kLook = 0x20;
        void* made[2] = {};
        for (int i = 0; i < 2; ++i)
            if (!MakeString(ctx, kLengths[i], made[i]) || !made[i]) return false;
        std::vector<int> places;
        for (int at = 0; at + 4 <= kLook; at += 4) {
            bool both = true;
            for (int i = 0; i < 2; ++i) both = both && *static_cast<const std::int32_t*>(FieldAddress(made[i], at)) == kLengths[i];
            if (both) places.push_back(at);
        }
        if (places.size() != 1)
            throw std::runtime_error("strings made 41 and 53 long hold both lengths at " + std::to_string(places.size()) +
                                     " places in their first 0x20 bytes past the field base, so where a string keeps its length is not known");
        stringLength_ = places[0];
        stringChars_ = places[0] + 4;
        logging::Line("scene: a System.String keeps its length at +%d past the field base and its characters after it",
                      stringLength_);
    }
    void* s = nullptr;
    if (!MakeString(ctx, kLength, s) || !s) return false;
    const std::int32_t length = *static_cast<const std::int32_t*>(FieldAddress(s, stringLength_));
    if (length != kLength)
        throw std::runtime_error("a System.String made " + std::to_string(kLength) + " long reads " + std::to_string(length) +
                                 " at _stringLength, so managed fields are not where the layer reads them");
    if (!Is(s, stringType_))
        throw std::runtime_error("a System.String made " + std::to_string(kLength) +
                                 " long does not read as System.String through the first word of its object info, so an object's type is not where the layer reads it");
    if (Tdb::Get().GetType(ctx, s) != RuntimeType(ctx, *stringType_))
        throw std::runtime_error("System.Object.GetType on a made System.String hands back another System.Type than a stand-in object with "
                                 "System.String in its info's first word gets, so the object info is not laid out as the layer reads it");
    logging::Line("scene: a System.String made %d long reads its length back at _stringLength and its type through its object info, "
                  "and GetType agrees, so managed fields and types are where the layer reads them",
                  kLength);
    return true;
}

void* Scene::Create(void* ctx, const Type& type) { return createInstance_(ctx, RuntimeType(ctx, type)); }


bool Scene::Is(void* object, const Type* type) { return type && TypeRead(object) == type; }

bool Scene::IsA(void* object, const Type* type) {
    const Tdb& tdb = Tdb::Get();
    const Type* t = TypeRead(object);
    return t && tdb.Holds(t) && tdb.Derives(t, *type);
}

bool Scene::Live(void* object) {
    const Type* t = TypeRead(object);
    return t && Tdb::Get().Holds(t);
}

void* Scene::GameObjectOf(void* ctx, void* component) const {
    return IsA(component, componentType_) ? getGameObject_(ctx, component) : nullptr;
}

void* Scene::Search(void* ctx, void* runtimeType) const {
    void* scene = getCurrentScene_(ctx);
    return scene ? findComponents_(ctx, scene, runtimeType) : nullptr;
}

void* Scene::TransformOf(void* ctx, void* object) const {
    void* gameObject = Is(object, gameObjectType_) ? object : GameObjectOf(ctx, object);
    return gameObject ? getTransform_(ctx, gameObject) : nullptr;
}

void* Scene::WorldMatrix(void* ctx, void* transform) {
    if (!worldMatrix_) {
        const std::size_t size = Tdb::Get().Size(*transformType_);
        alignas(16) float matrix[16] = {};
        std::vector<std::uint8_t> bytes(size);
        if (!IsA(transform, transformType_)) return nullptr;
        worldMatrixOf_(matrix, ctx, transform);
        if (!memory::GuardedMemcpy(bytes.data(), transform, size)) return nullptr;
        worldMatrix_ = worldMatrixPlace_.See(bytes.data(), size, matrix);
        if (!worldMatrix_) {
            if (!saidPlaces_)
                logging::Line("transform: %zu places in via.Transform hold its world matrix; the next frames tell them apart",
                              worldMatrixPlace_.Left());
            saidPlaces_ = true;
            return nullptr;
        }
        logging::Line("transform: via.Transform keeps the world matrix it is drawn with at +0x%zx of its 0x%zx bytes",
                      *worldMatrix_, size);
    }
    return static_cast<std::uint8_t*>(transform) + *worldMatrix_;
}

}  // namespace cameraunlock::re_engine
