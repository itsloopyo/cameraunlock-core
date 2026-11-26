// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <cameraunlock/re_engine/tdb_reader.h>

// The engine's script VM and its type database, found from inside the game.
namespace cameraunlock::re_engine {

struct Method {
    const char* name = nullptr;
    void* fn = nullptr;  // null for an interface or abstract method
    std::uint16_t numParams = 0;
    bool isStatic = false;
    // The first parameter's type; null for a method that takes none. This and
    // the return type are null when the type database gives an index past its
    // tables.
    const Type* firstParam = nullptr;
    // System.Void for a method that returns nothing.
    const Type* returnType = nullptr;
    // Where the signature sits in the blob, for Tdb::ParamTypes.
    std::uint32_t paramsAt = 0;
};

struct Field {
    int offset = -1;  // from the object's field base (see FieldAddress); negative when there is none
    // Bytes the value takes when it is a bool, an integer or an enum over one;
    // 0 for any other type.
    std::uint32_t integerSize = 0;
    std::string typeName;
};

// The Layout for the type database version the header gives. Throws for a
// version the consumer does not read.
using LayoutFor = std::function<Layout(std::uint32_t version)>;

class Tdb {
public:
    static constexpr int kMostParents = 64;

    // Finds the VM and its type database, read through what `layoutFor` gives
    // for its version. Throws naming what was not found, and NotReady while the
    // engine has not made them yet.
    static void Discover(const LayoutFor& layoutFor);
    static bool Discovered();
    static const Tdb& Get();
    // Finds the engine's method code decoder; Methods and FindMethod hand out null
    // code pointers until it has run.
    static void ResolveMethodCode();

    // A type database at `header` read through `layout`, with no VM: for a tool
    // or a test over a database that is not the running game's. ThreadContext,
    // RuntimeType and GetType are refused on it.
    Tdb(Layout layout, const void* header);

    std::uint32_t Version() const { return version_; }
    std::uint32_t NumTypes() const { return reader_.TypeCount(); }
    const Type& TypeAt(std::uint32_t index) const { return *reader_.TypeAt(index); }
    // Whether `t` is one of the table's types, as an object's info names one.
    bool Holds(const Type* t) const {
        const auto at = reinterpret_cast<std::uintptr_t>(t), first = reinterpret_cast<std::uintptr_t>(reader_.TypeAt(0));
        const std::uintptr_t stride = reader_.GetLayout().type.size;
        return at >= first && (at - first) % stride == 0 && (at - first) / stride < NumTypes();
    }

    // The engine's kind of object, as the type database numbers it.
    std::uint32_t ObjectType(const Type& t) const { return reader_.ReadType(t).kind; }
    // An interface: TypeAttributes.Interface. The type database lists a class's
    // parents, not the interfaces it implements.
    bool IsInterface(const Type& t) const { return (reader_.ReadType(t).attributes & kTypeAttributesInterface) != 0; }
    // The bytes an instance takes.
    std::uint32_t Size(const Type& t) const { return reader_.ReadType(t).instanceSize; }

    // The VM context the calling thread passes as every method's first argument.
    void* ThreadContext() const;

    // A type by its full name ("via.Transform", "app.Foo.Bar" for a nested
    // type), or null. Throws when more than one type carries the name. The name
    // map is built on the first call.
    const Type* FindType(std::string_view fullName) const;
    // The one type whose name after the last '.' is `shortName`, or null. Throws
    // when more than one type carries it, naming them.
    const Type* FindTypeByShortName(std::string_view shortName) const;
    std::string FullName(const Type& t) const;
    const char* Name(const Type& t) const;
    const Type* Parent(const Type& t) const;
    // Whether `t` is `base` or has it among its parents, kMostParents up at most;
    // false for null.
    bool Derives(const Type* t, const Type& base) const;

    // Every method the type itself declares.
    std::vector<Method> Methods(const Type& t) const;
    // The method named `name` with `numParams` parameters (any count when
    // negative), searched in the type then its parents. `fn` null when not
    // found. Throws when one type declares more than one that fits.
    Method FindMethod(const Type& t, std::string_view name, int numParams = -1) const;
    // The one-parameter method named `name` whose parameter is the type named
    // `paramType` (full name), searched as FindMethod searches, and refused as
    // it refuses.
    Method FindMethodTaking(const Type& t, std::string_view name, std::string_view paramType) const;
    // Each parameter's type in order, null for one the type database gives an
    // index past its tables.
    std::vector<const Type*> ParamTypes(const Method& m) const;

    // An instance field's offset from the object's field base (see FieldAddress),
    // searched in the type then its parents; negative when there is none.
    int FieldOffset(const Type& t, std::string_view name) const;
    // The same search, with what the field holds.
    Field FindField(const Type& t, std::string_view name) const;
    // Every field the type itself declares, statics included (offset -1 for one).
    std::vector<std::pair<std::string, Field>> Fields(const Type& t) const;
    // The value of the enum `t`'s constant `name`, read from the type database.
    // Throws when `t` has no such constant or it is no integer.
    std::int64_t EnumConstant(const Type& t, std::string_view name) const;
    // Bytes a bool, an integer or an enum over one takes; 0 for any other type.
    std::uint32_t IntegerSize(const Type& t) const;
    // The bytes a value type's fields take: what a method handing one back
    // writes through its out pointer.
    std::uint32_t ValueSize(const Type& t) const { return static_cast<std::uint32_t>(Detail(t).valueSize); }
    // Throws unless the value type opens with the System.Single fields `names`,
    // 4 bytes apart from its start, and takes no more than `bytes`: the float
    // array the layer hands the engine for it.
    void RequireFloats(const Type& t, const std::vector<std::string>& names, std::size_t bytes) const;

    // The type's System.Type object, as System.Object.GetType hands it out.
    // Game thread.
    void* RuntimeType(void* ctx, const Type& t) const;
    // System.Object.GetType called on `object`. Game thread.
    void* GetType(void* ctx, void* object) const;

private:
    const char* String(std::uint32_t offset) const;
    void BuildNames() const;
    void BuildIntegerSizes() const;
    void FindDecoder();
    TypeDetailView Detail(const Type& t) const { return reader_.ReadTypeDetail(reader_.ReadType(t).detail); }

    TdbReader reader_;
    void** vm_ = nullptr;
    void* (*getThreadContext_)(void* vm, int which) = nullptr;
    std::uint32_t version_ = 0;
    void* (*decode_)(std::int32_t encoded) = nullptr;
    std::uint32_t stringMask_ = 0;
    mutable std::unordered_map<std::string, const Type*> byName_;
    // Full names more than one type carries (generic instances print alike).
    mutable std::unordered_set<std::string> sharedNames_;
    // A CLI element type's size in bytes, for the integer primitives.
    mutable std::unordered_map<std::uint32_t, std::uint32_t> integerSizes_;
};

// A managed object's type: its object info's first word.
inline const Type* TypeOf(void* object) {
    return **reinterpret_cast<const Type* const* const*>(object);
}

// Where an instance field of `object` is: the object's field base, read from
// just before its class's vtable, plus the field's offset.
inline void* FieldAddress(void* object, int offset) {
    const auto vt = *static_cast<std::uintptr_t*>(object);
    const auto base = *reinterpret_cast<const std::int32_t*>(vt - sizeof(void*));
    return static_cast<std::uint8_t*>(object) + base + offset;
}

}  // namespace cameraunlock::re_engine
