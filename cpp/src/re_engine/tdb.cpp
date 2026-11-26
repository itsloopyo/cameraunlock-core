// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <cameraunlock/re_engine/tdb.h>

#include <Windows.h>

#include <cstring>
#include <deque>
#include <map>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include <cameraunlock/logging/file_log.h>
#include <cameraunlock/re_engine/discovery.h>
#include <cameraunlock/re_engine/exe.h>

namespace cameraunlock::re_engine {
namespace {

using discovery::Hex;

// "TDB\0", the type database header's first four bytes; its version follows.
constexpr std::uint32_t kTdbMagic = 0x00424454u;

std::optional<Tdb> g_tdb;

// The global VM pointer is loaded into rcx before a call to the function that
// hands out a thread's VM context, all over the executable.
discovery::Vm FindVm() {
    std::vector<discovery::Load> loads;
    for (const std::uintptr_t hit : ScanAll("48 8B 0D ? ? ? ? BA FF FF FF FF E8 ? ? ? ?"))
        loads.push_back({RipTarget(hit + 3), RipTarget(hit + 13)});
    return discovery::PickVm(loads);
}

std::uint32_t VersionAt(const void* header) { return static_cast<const std::uint32_t*>(header)[1]; }

}  // namespace

Tdb::Tdb(Layout layout, const void* header) : reader_(std::move(layout), header), version_(VersionAt(header)) {
    std::uint32_t mask = 1;
    while (mask < reader_.StringBytes()) mask <<= 1;
    stringMask_ = mask - 1;
}

// A method's code is wherever the engine's decoder puts its encoded offset.
// REFramework finds the decoder by its bytes, which Requiem's build does not
// have. Every invoke thunk calls it the same way: it loads the method
// definition (mov rax, [rcx+10h]), its encoded offset (+8) into ecx, and calls
// the decoder. Requiem's thunks do the two back to back; Resident Evil 4's
// move rcx and rdx into other registers between them (6 bytes), so up to
// kThunkGap bytes may sit there. The call target nearly all of them share is
// the decoder.
void Tdb::FindDecoder() {
    constexpr std::size_t kThunkGap = 8;
    constexpr std::uint8_t kCall[] = {0x8B, 0x48, 0x08, 0xE8};
    std::map<std::uintptr_t, int> targets;
    for (const std::uintptr_t hit : ScanAll("48 8B 41 10")) {
        std::uint8_t after[4 + kThunkGap + sizeof(kCall) + sizeof(std::int32_t)] = {};
        if (!SafeRead(hit, after, sizeof(after))) continue;
        for (std::size_t gap = 0; gap <= kThunkGap; ++gap) {
            if (std::memcmp(after + 4 + gap, kCall, sizeof(kCall)) != 0) continue;
            const std::size_t disp = 4 + gap + sizeof(kCall);
            std::int32_t rel = 0;
            std::memcpy(&rel, after + disp, sizeof(rel));
            ++targets[hit + disp + sizeof(rel) + rel];
            break;
        }
    }
    std::uintptr_t best = 0;
    int bestVotes = 0, total = 0;
    for (const auto& [target, n] : targets) {
        total += n;
        if (n > bestVotes) bestVotes = n, best = target;
    }
    if (bestVotes < 100 || bestVotes * 10 < total * 9)
        throw std::runtime_error("no method code decoder: the most called target of " + std::to_string(total) +
                                 " invoke thunk calls (48 8B 41 10, up to 8 bytes, 8B 48 08 E8) takes " + std::to_string(bestVotes));
    decode_ = reinterpret_cast<void* (*)(std::int32_t)>(best);
    logging::Line("tdb: method code decoder at exe+0x%llx, called by %d of %d invoke thunks",
                  static_cast<unsigned long long>(best - Game().base), bestVotes, total);
}

void Tdb::Discover(const LayoutFor& layoutFor) {
    // The loads are code, which does not change while the VM is being made, so
    // the executable is scanned once however many times discovery is retried.
    static const discovery::Vm ref = FindVm();
    const auto vm = Read<std::uintptr_t>(ref.global);
    if (!vm || !*vm) throw NotReady("the script VM at " + Hex(ref.global) + " is not made yet");

    // Each type database the VM points at, and where it first points at it.
    std::map<std::uintptr_t, std::uintptr_t> headers;
    for (std::uintptr_t off = 0; off < 0x20000; off += 8) {
        const auto p = Read<std::uintptr_t>(*vm + off);
        if (!p || !*p || (*p & 7)) continue;
        const auto magic = Read<std::uint32_t>(*p);
        if (magic && *magic == kTdbMagic) headers.emplace(*p, off);
    }
    if (headers.empty()) throw NotReady("no \"TDB\" in the first 0x20000 bytes of the script VM at " + Hex(*vm));
    if (headers.size() > 1) {
        std::string at;
        for (const auto& [h, off] : headers) at += " " + Hex(h) + " (VM+" + Hex(off) + ")";
        throw std::runtime_error("the script VM at " + Hex(*vm) + " points at " + std::to_string(headers.size()) +
                                 " type databases:" + at);
    }
    const auto [headerAt, headerSlot] = *headers.begin();
    const auto* header = reinterpret_cast<const void*>(headerAt);
    Tdb t(layoutFor(VersionAt(header)), header);
    t.vm_ = reinterpret_cast<void**>(ref.global);
    t.getThreadContext_ = reinterpret_cast<void* (*)(void*, int)>(ref.getThreadContext);
    logging::Line("tdb: script VM global at exe+0x%llx, thread contexts from exe+0x%llx, type database at VM+0x%llx",
                  static_cast<unsigned long long>(ref.global - Game().base),
                  static_cast<unsigned long long>(ref.getThreadContext - Game().base), static_cast<unsigned long long>(headerSlot));
    g_tdb.emplace(std::move(t));
}

void Tdb::ResolveMethodCode() {
    if (!g_tdb->reader_.CodeIsAddress()) g_tdb->FindDecoder();
}

bool Tdb::Discovered() { return g_tdb.has_value(); }

const Tdb& Tdb::Get() {
    if (!g_tdb) throw std::logic_error("Tdb::Get before Tdb::Discover");
    return *g_tdb;
}

void* Tdb::ThreadContext() const {
    if (!vm_) throw std::logic_error("Tdb::ThreadContext on a type database read without the game's VM");
    return getThreadContext_(*vm_, -1);
}

const char* Tdb::String(std::uint32_t offset) const {
    offset &= stringMask_;
    return offset < reader_.StringBytes() ? reader_.Strings() + offset : "";
}

const char* Tdb::Name(const Type& t) const { return String(Detail(t).name); }

const Type* Tdb::Parent(const Type& t) const {
    const TypeView v = reader_.ReadType(t);
    if (v.parent == 0 || v.parent == v.self || v.parent >= reader_.TypeCount()) return nullptr;
    return reader_.TypeAt(v.parent);
}

bool Tdb::Derives(const Type* t, const Type& base) const {
    const Type* at = t;
    for (int depth = 0; at && depth < kMostParents; ++depth, at = Parent(*at))
        if (at == &base) return true;
    return false;
}

std::string Tdb::FullName(const Type& t) const {
    std::deque<std::string> parts;
    const Type* at = &t;
    std::unordered_set<const Type*> seen;
    while (at && seen.insert(at).second) {
        parts.push_front(Name(*at));
        const TypeView v = reader_.ReadType(*at);
        const bool nested = v.enclosing != 0 && v.enclosing != v.self && v.enclosing < reader_.TypeCount();
        if (!nested) {
            const char* ns = String(Detail(*at).nameSpace);
            if (*ns) parts.push_front(ns);
            break;
        }
        at = reader_.TypeAt(v.enclosing);
    }
    std::string out;
    for (const std::string& part : parts) {
        if (!out.empty()) out += '.';
        out += part;
    }
    return out;
}

void Tdb::BuildNames() const {
    if (!byName_.empty()) return;
    for (std::uint32_t i = 0; i < reader_.TypeCount(); ++i) {
        const Type* t = reader_.TypeAt(i);
        std::string name = FullName(*t);
        if (!byName_.emplace(name, t).second) sharedNames_.insert(std::move(name));
    }
}

const Type* Tdb::FindType(std::string_view fullName) const {
    BuildNames();
    const std::string name(fullName);
    if (sharedNames_.count(name)) throw std::runtime_error("more than one type is named " + name);
    const auto it = byName_.find(name);
    return it == byName_.end() ? nullptr : it->second;
}

const Type* Tdb::FindTypeByShortName(std::string_view shortName) const {
    BuildNames();
    std::vector<std::pair<std::string, const Type*>> found;
    for (const auto& [name, t] : byName_) {
        const std::size_t dot = name.rfind('.');
        if (std::string_view(name).substr(dot == std::string::npos ? 0 : dot + 1) != shortName) continue;
        if (sharedNames_.count(name)) throw std::runtime_error("more than one type is named " + name);
        found.emplace_back(name, t);
    }
    if (found.size() > 1) {
        std::string names;
        for (const auto& [name, t] : found) names += (names.empty() ? "" : ", ") + name;
        throw std::runtime_error("more than one type is named " + std::string(shortName) + ": " + names);
    }
    return found.empty() ? nullptr : found[0].second;
}

std::vector<Method> Tdb::Methods(const Type& t) const {
    std::vector<Method> out;
    const std::uint32_t count = Detail(t).methodCount;
    const std::uint32_t first = reader_.ReadType(t).firstMethod;
    for (std::uint32_t i = 0; i < count; ++i) {
        const MethodView m = reader_.ReadMethod(first + i);
        const SignatureView params = reader_.ReadSignature(m.signature);
        // The return and each parameter index the parameter table, which
        // name the type.
        const auto typeOf = [this](std::uint32_t param) -> const Type* {
            if (param >= reader_.ParamCount()) return nullptr;
            const std::uint32_t typeId = reader_.ParamType(param);
            return typeId < reader_.TypeCount() ? reader_.TypeAt(typeId) : nullptr;
        };
        void* code = m.code;
        if (!reader_.CodeIsAddress()) code = m.encodedCode && decode_ ? decode_(m.encodedCode) : nullptr;
        const Type* firstParam = params.paramCount > 0 ? typeOf(reader_.ParamAt(m.signature, 0)) : nullptr;
        out.push_back({String(m.name), code, params.paramCount, (m.attributes & kMethodAttributesStatic) != 0, firstParam,
                       typeOf(params.returns), m.signature});
    }
    return out;
}

std::vector<const Type*> Tdb::ParamTypes(const Method& m) const {
    const SignatureView params = reader_.ReadSignature(m.paramsAt);
    std::vector<const Type*> out;
    for (std::uint16_t i = 0; i < params.paramCount; ++i) {
        const std::uint32_t param = reader_.ParamAt(m.paramsAt, i);
        const std::uint32_t typeId = param < reader_.ParamCount() ? reader_.ParamType(param) : reader_.TypeCount();
        out.push_back(typeId < reader_.TypeCount() ? reader_.TypeAt(typeId) : nullptr);
    }
    return out;
}

Method Tdb::FindMethod(const Type& t, std::string_view name, int numParams) const {
    for (const Type* at = &t; at; at = Parent(*at)) {
        std::vector<Method> fits;
        for (const Method& m : Methods(*at))
            if (name == m.name && (numParams < 0 || m.numParams == numParams)) fits.push_back(m);
        if (fits.size() > 1)
            throw std::runtime_error(FullName(*at) + " declares " + std::to_string(fits.size()) + " methods named " + std::string(name) +
                                     (numParams < 0 ? "" : " taking " + std::to_string(numParams)) + "; the layer does not pick one");
        if (!fits.empty()) return fits[0];
    }
    return {};
}

Method Tdb::FindMethodTaking(const Type& t, std::string_view name, std::string_view paramType) const {
    for (const Type* at = &t; at; at = Parent(*at)) {
        std::vector<Method> fits;
        for (const Method& m : Methods(*at))
            if (name == m.name && m.numParams == 1 && m.firstParam && FullName(*m.firstParam) == paramType) fits.push_back(m);
        if (fits.size() > 1)
            throw std::runtime_error(FullName(*at) + " declares " + std::to_string(fits.size()) + " methods named " + std::string(name) +
                                     " taking a " + std::string(paramType) + "; the layer does not pick one");
        if (!fits.empty()) return fits[0];
    }
    return {};
}

// A primitive's or an enum's `underlying` is its CLI element type, and an enum
// carries its integer's. Each integer primitive's code is read off its own type.
void Tdb::BuildIntegerSizes() const {
    if (!integerSizes_.empty()) return;
    constexpr std::pair<const char*, std::uint32_t> kIntegers[] = {
        {"System.Boolean", 1}, {"System.SByte", 1}, {"System.Byte", 1},  {"System.Char", 2},  {"System.Int16", 2},
        {"System.UInt16", 2},  {"System.Int32", 4}, {"System.UInt32", 4}, {"System.Int64", 8}, {"System.UInt64", 8}};
    std::unordered_map<std::uint32_t, std::uint32_t> sizes;
    for (const auto& [name, size] : kIntegers) {
        const Type* t = FindType(name);
        if (!t) throw std::runtime_error(std::string("the type database has no ") + name);
        const std::uint32_t underlying = reader_.ReadType(*t).elementType;
        if (underlying == 0) throw std::runtime_error(std::string("the type database gives ") + name + " no element type");
        if (!sizes.emplace(underlying, size).second)
            throw std::runtime_error(std::string("the type database gives ") + name + " the element type " +
                                     std::to_string(underlying) + ", which another integer type has too");
    }
    integerSizes_ = std::move(sizes);
}

int Tdb::FieldOffset(const Type& t, std::string_view name) const { return FindField(t, name).offset; }

Field Tdb::FindField(const Type& t, std::string_view name) const {
    for (const Type* at = &t; at; at = Parent(*at)) {
        const std::uint32_t count = Detail(*at).fieldCount;
        const std::uint32_t first = reader_.ReadType(*at).firstField;
        for (std::uint32_t i = 0; i < count; ++i) {
            const FieldView f = reader_.ReadField(first + i);
            if (f.attributes & kFieldAttributesStatic) continue;
            if (name != String(f.name)) continue;
            const Type& type = *reader_.TypeAt(f.type);
            return {static_cast<int>(f.offset), IntegerSize(type), FullName(type)};
        }
    }
    return {};
}

std::vector<std::pair<std::string, Field>> Tdb::Fields(const Type& t) const {
    std::vector<std::pair<std::string, Field>> out;
    const std::uint32_t count = Detail(t).fieldCount;
    const std::uint32_t first = reader_.ReadType(t).firstField;
    for (std::uint32_t i = 0; i < count; ++i) {
        const FieldView f = reader_.ReadField(first + i);
        const Type& type = *reader_.TypeAt(f.type);
        const bool isStatic = (f.attributes & kFieldAttributesStatic) != 0;
        out.push_back({String(f.name), {isStatic ? -1 : static_cast<int>(f.offset), IntegerSize(type), FullName(type)}});
    }
    return out;
}

std::int64_t Tdb::EnumConstant(const Type& t, std::string_view name) const {
    const std::uint32_t size = IntegerSize(t);
    if (!size) throw std::runtime_error(FullName(t) + " is no enum over an integer, so it has no constant " + std::string(name));
    const std::uint32_t count = Detail(t).fieldCount;
    const std::uint32_t first = reader_.ReadType(t).firstField;
    constexpr std::uint16_t kConstant = kFieldAttributesStatic | kFieldAttributesLiteral;
    for (std::uint32_t i = 0; i < count; ++i) {
        const FieldView f = reader_.ReadField(first + i);
        if ((f.attributes & kConstant) != kConstant) continue;
        if (name != String(f.name)) continue;
        if (f.literal >= reader_.LiteralCount())
            throw std::runtime_error(FullName(t) + "." + std::string(name) + "'s value is at init data " + std::to_string(f.literal) + " of " +
                                     std::to_string(reader_.LiteralCount()));
        const std::int32_t at = reader_.LiteralAt(f.literal);
        if (at < 0) throw std::runtime_error(FullName(t) + "." + std::string(name) + "'s value is a string, not an integer");
        std::uint64_t bits = 0;
        std::memcpy(&bits, reader_.Blob() + at, size);
        // Sign-extended, so an enum over a signed integer keeps its -1.
        const unsigned shift = 64 - size * 8;
        return shift ? static_cast<std::int64_t>(bits << shift) >> shift : static_cast<std::int64_t>(bits);
    }
    throw std::runtime_error(FullName(t) + " has no constant " + std::string(name));
}

std::uint32_t Tdb::IntegerSize(const Type& t) const {
    BuildIntegerSizes();
    const auto size = integerSizes_.find(reader_.ReadType(t).elementType);
    return size == integerSizes_.end() ? 0 : size->second;
}

void Tdb::RequireFloats(const Type& t, const std::vector<std::string>& names, std::size_t bytes) const {
    for (std::size_t i = 0; i < names.size(); ++i) {
        const Field f = FindField(t, names[i]);
        if (f.typeName != "System.Single" || f.offset != static_cast<int>(i * sizeof(float)))
            throw std::runtime_error(FullName(t) + "." + names[i] + " is not the System.Single at +" + std::to_string(i * sizeof(float)) +
                                     " the layer hands the engine (" + (f.typeName.empty() ? "no such field" : f.typeName + " at +" +
                                     std::to_string(f.offset)) + ")");
    }
    if (ValueSize(t) > bytes)
        throw std::runtime_error(FullName(t) + " takes " + std::to_string(ValueSize(t)) + " bytes, more than the " + std::to_string(bytes) +
                                 " the layer hands the engine");
}

void* Tdb::RuntimeType(void* ctx, const Type& t) const {
    // GetType reads only the object's info, whose first word is the type: a
    // stand-in object with the type in that place gets the type's System.Type.
    const Type* info = &t;
    const Type* const* stand = &info;
    return GetType(ctx, &stand);
}

void* Tdb::GetType(void* ctx, void* object) const {
    if (!vm_) throw std::logic_error("Tdb::GetType on a type database read without the game's VM");
    static const Method getType = [this] {
        const Type* object = FindType("System.Object");
        if (!object) throw std::runtime_error("the type database has no System.Object");
        const Method m = FindMethod(*object, "GetType", 0);
        if (!m.fn) throw std::runtime_error("System.Object has no GetType with code");
        return m;
    }();
    using Call = void* (*)(void* ctx, void* self);
    return reinterpret_cast<Call>(getType.fn)(ctx, object);
}

}  // namespace cameraunlock::re_engine
