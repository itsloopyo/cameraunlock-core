// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <cameraunlock/unreal/ue_discovery.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <vector>

namespace cameraunlock::unreal {

using memory::CodeMap;
using memory::Memory;
using memory::ModuleMap;
using memory::Range;

namespace {

constexpr std::uint32_t kFuncNative = 0x400;

int BitsSet(std::uint32_t mask) {
    int n = 0;
    for (; mask; mask &= mask - 1) ++n;
    return n;
}

// A test mask that asks for FUNC_Native: the flag alone, or with one other.
// Split Fiction's ProcessEvent (Hazelight's Angelscript engine) tests
// FunctionFlags against 0x410 (its UObject vtable slot 77).
bool AsksNative(std::uint32_t mask) { return (mask & kFuncNative) && BitsSet(mask) <= 2; }

constexpr std::size_t kMaxVtable = 0x400;
constexpr std::size_t kVtableSlot = 8;

int ModRmReg(std::uint8_t modrm) { return (modrm >> 3) & 7; }

// The start of the function `fn` is in, or `fn` itself where the unwind table
// has no entry for it.
std::uintptr_t StartOf(const CodeMap& code, std::uintptr_t fn) {
    const std::uintptr_t root = code.RootOf(fn);
    return root ? root : fn;
}

std::vector<std::uintptr_t> VtableEntries(const Memory& mem, const ModuleMap& module, std::uintptr_t vt,
                                          std::size_t max = kMaxVtable) {
    std::vector<std::uintptr_t> out;
    if (!module.InModule(vt)) return out;
    for (std::size_t i = 0; i < max; ++i) {
        const std::uintptr_t fn = mem.Ptr(vt + i * kVtableSlot);
        if (!module.InCode(fn)) break;
        out.push_back(fn);
    }
    return out;
}

std::vector<std::uint8_t> Bytes(const Memory& mem, Range r) {
    std::vector<std::uint8_t> out(r.end - r.begin);
    if (out.empty() || !mem.Read(r.begin, out.data(), out.size())) out.clear();
    return out;
}

std::uint32_t U32(const std::vector<std::uint8_t>& b, std::size_t at) {
    std::uint32_t v = 0;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}

// The address a RIP-relative disp32 at b[at] names, `begin` being b[0]'s
// address. The displacement is relative to the end of the instruction, which
// for the loads, stores and compares read here is the end of the displacement
// itself.
std::uintptr_t RipTarget(const std::vector<std::uint8_t>& b, std::size_t at, std::uintptr_t begin) {
    std::int32_t d = 0;
    std::memcpy(&d, b.data() + at, 4);
    return begin + at + 4 + static_cast<std::intptr_t>(d);
}

// A memory operand [reg + disp] after an opcode byte at `op`: the ModRM byte,
// an optional SIB, then an 8- or 32-bit displacement. Returns the displacement
// and where the operand ends, or false for any other addressing form.
bool RegDisp(const std::vector<std::uint8_t>& b, std::size_t op, std::uint32_t& disp, std::size_t& end) {
    if (op + 2 >= b.size()) return false;
    const std::uint8_t modrm = b[op + 1];
    const int mod = modrm >> 6, rm = modrm & 7;
    std::size_t at = op + 2 + (rm == 4 ? 1 : 0);
    if (mod == 2 && at + 4 <= b.size()) {
        disp = U32(b, at);
        end = at + 4;
        return true;
    }
    if (mod == 1 && at + 1 <= b.size()) {
        disp = b[at];
        end = at + 1;
        return true;
    }
    return false;
}

// Tests of FunctionFlags & FUNC_Native, in the forms MSVC emits: a test of the
// dword, of its second byte, or a load followed by a register test.
int NativeFlagTests(const std::vector<std::uint8_t>& b, std::uint32_t flagsOff) {
    int hits = 0;
    for (std::size_t i = 0; i + 8 < b.size(); ++i) {
        std::uint32_t disp = 0;
        std::size_t end = 0;
        const int reg = ModRmReg(b[i + 1]);
        if (b[i] == 0xF7 && reg == 0 && RegDisp(b, i, disp, end) && end + 4 <= b.size()) {
            if (disp == flagsOff && AsksNative(U32(b, end))) ++hits;
        } else if (b[i] == 0xF6 && reg == 0 && RegDisp(b, i, disp, end) && end < b.size()) {
            if (disp == flagsOff + 1 && b[end] == (kFuncNative >> 8)) ++hits;
        } else if (b[i] == 0x8B && RegDisp(b, i, disp, end) && disp == flagsOff) {
            for (std::size_t j = end; j + 6 <= b.size() && j < end + 16; ++j) {
                const bool testEax = b[j] == 0xA9 && AsksNative(U32(b, j + 1));
                const bool testReg = b[j] == 0xF7 && (b[j + 1] & 0xF8) == 0xC0 && AsksNative(U32(b, j + 2));
                const bool btReg = b[j] == 0x0F && b[j + 1] == 0xBA && (b[j + 2] & 0xF8) == 0xE0 && b[j + 3] == 10;
                if (testEax || testReg || btReg) {
                    ++hits;
                    break;
                }
            }
        }
    }
    return hits;
}

std::vector<std::uint8_t> FunctionBytes(const Memory& mem, const CodeMap& code, std::uintptr_t fn) {
    std::vector<std::uint8_t> all;
    for (const Range& part : code.Parts(StartOf(code, fn))) {
        const auto bytes = Bytes(mem, part);
        all.insert(all.end(), bytes.begin(), bytes.end());
    }
    return all;
}

// Whether the function reads one of `targets` through a RIP-relative operand.
bool ReferencesAny(const Memory& mem, const CodeMap& code, std::uintptr_t fn,
                   std::initializer_list<std::uintptr_t> targets) {
    for (const Range& part : code.Parts(StartOf(code, fn))) {
        const auto b = Bytes(mem, part);
        for (std::size_t i = 0; i + 4 <= b.size(); ++i) {
            const std::uintptr_t to = RipTarget(b, i, part.begin);
            for (std::uintptr_t t : targets)
                if (to == t) return true;
        }
    }
    return false;
}

struct Candidate {
    std::size_t slot;
    int score;
};

// The slot of the one candidate that outscores every other.
std::optional<std::size_t> OneAhead(const std::vector<Candidate>& candidates) {
    int bestScore = 0, atBest = 0;
    std::size_t best = 0;
    for (const Candidate& c : candidates) {
        if (c.score > bestScore) {
            bestScore = c.score;
            atBest = 1;
            best = c.slot;
        } else if (c.score == bestScore) {
            ++atBest;
        }
    }
    return atBest == 1 ? std::optional<std::size_t>(best) : std::nullopt;
}

}  // namespace

std::uintptr_t FindProcessEvent(const Reflection& refl, const CodeMap& code, const ModuleMap& module,
                                std::string& why, bool& notLoaded) {
    const Objects& o = refl.Objs();
    const Memory& mem = *o.mem;
    const auto cdos = o.FindPaths({"/Script/CoreUObject.Default__Object", "/Script/Engine.Default__Actor",
                                   "/Script/Engine.Default__LocalPlayer", "/Script/CoreUObject.Object"});
    if (!cdos[0] || !cdos[1] || !cdos[2] || !cdos[3]) {
        why = "the Object, Actor or LocalPlayer class default, or the Object class, is not in the object array";
        notLoaded = true;
        return 0;
    }
    const auto object = VtableEntries(mem, module, mem.Ptr(cdos[0]));
    const auto actor = VtableEntries(mem, module, mem.Ptr(cdos[1]));
    // Classes that do not override ProcessEvent: a LocalPlayer and a UClass (the
    // Object class object is one). Unreal ships without RTTI, so nothing marks
    // where UObject's vtable ends and the next class's begins; a slot past the
    // end holds unrelated functions in each of these, and a real UObject
    // virtual they inherit holds the same one.
    const auto localPlayer = VtableEntries(mem, module, mem.Ptr(cdos[2]));
    const auto uclass = VtableEntries(mem, module, mem.Ptr(cdos[3]));
    const auto flagsOff = static_cast<std::uint32_t>(refl.Layout().functionFlags);
    const ObjectArray& array = o.array;

    // ProcessEvent tests FUNC_Native, looks its own object item up in
    // GUObjectArray (InternalIndex against NumElements, then the chunk table),
    // and AActor overrides it. Each is common alone; together they are not.
    std::vector<Candidate> candidates;
    for (std::size_t i = 0; i < object.size(); ++i) {
        if (i >= localPlayer.size() || i >= uclass.size() || localPlayer[i] != object[i] || uclass[i] != object[i])
            continue;
        const int tests = NativeFlagTests(FunctionBytes(mem, code, object[i]), flagsOff);
        if (!tests) continue;
        int score = 1;
        if (ReferencesAny(mem, code, object[i], {array.objects, array.objects + array.numOffset})) score += 4;
        if (i < actor.size() && actor[i] != object[i]) score += 2;
        candidates.push_back({i, score});
    }
    const std::optional<std::size_t> best = OneAhead(candidates);
    if (!best) {
        why = candidates.empty() ? "no UObject virtual tests FUNC_Native" : "UObject virtuals that test FUNC_Native, none ahead (slot:score):";
        for (const Candidate& c : candidates) why += " " + std::to_string(c.slot) + ":" + std::to_string(c.score);
        return 0;
    }
    return object[*best];
}

namespace {

std::string Hex(std::uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(v));
    return buf;
}

}  // namespace

const char* StageName(ReflectionStage stage) {
    switch (stage) {
        case ReflectionStage::Names: return "name table";
        case ReflectionStage::Objects: return "object array";
        case ReflectionStage::Header: return "UObject header";
        case ReflectionStage::Types: return "engine types";
        case ReflectionStage::Reflection: return "reflection layout";
        case ReflectionStage::ProcessEvent: return "ProcessEvent";
        case ReflectionStage::Done: return "done";
    }
    return "?";
}

ReflectionDiscoveryResult DiscoverReflection(const Memory& mem, const ModuleMap& module, const CodeMap& code,
                                             const std::function<void(ReflectionStage, long long)>& stageDone) {
    ReflectionDiscoveryResult r;
    auto mark = std::chrono::steady_clock::now();
    const auto done = [&](ReflectionStage stage) {
        const auto now = std::chrono::steady_clock::now();
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - mark).count();
        r.timings.emplace_back(stage, ms);
        mark = now;
        if (stageDone) stageDone(stage, ms);
    };
    const auto fail = [&](ReflectionStage stage, std::string why) {
        r.reached = stage;
        r.why = std::move(why);
        return r;
    };

    std::string why;
    const auto names = FindNameTable(mem, module, why);
    if (!names) return fail(ReflectionStage::Names, why);
    done(ReflectionStage::Names);
    const auto array = FindObjectArray(mem, module);
    if (!array) return fail(ReflectionStage::Objects, "no structure in writable data holds self-indexed UObjects");
    done(ReflectionStage::Objects);

    ReflectionDiscovery d;
    d.objects = Objects{&mem, *names, *array};
    const auto header = FindObjectHeader(d.objects, why, r.notLoaded);
    if (!header) return fail(ReflectionStage::Header, why);
    d.objects.header = *header;
    r.partial = d.objects;
    done(ReflectionStage::Header);

    const CalibrationTypes types = FindCalibrationTypes(d.objects);
    if (!types.vector || !types.playerController)
        return fail(ReflectionStage::Types, "CoreUObject.Vector or Engine.PlayerController is not loaded yet");
    done(ReflectionStage::Types);

    const auto layout = CalibrateReflection(d.objects, module, types, why, r.notLoaded);
    if (!layout) return fail(ReflectionStage::Reflection, why);
    done(ReflectionStage::Reflection);
    d.layout = *layout;
    const Reflection refl(d.objects, d.layout);

    d.processEvent = FindProcessEvent(refl, code, module, why, r.notLoaded);
    if (!d.processEvent) return fail(ReflectionStage::ProcessEvent, why);
    done(ReflectionStage::ProcessEvent);

    r.found = std::move(d);
    r.reached = ReflectionStage::Done;
    return r;
}

std::vector<std::string> Describe(const ReflectionDiscovery& d, const ModuleMap& module) {
    const auto rva = [&](std::uintptr_t a) { return module.InModule(a) ? Hex(a - module.base) : Hex(a) + " (outside module)"; };
    const NameTable& n = d.objects.names;
    const ObjectArray& a = d.objects.array;
    const ReflectionLayout& L = d.layout;
    std::vector<std::string> out;
    out.push_back(std::string("names: ") + (n.kind == NameTableKind::Pool ? "FNamePool" : "TNameEntryArray") +
                  " blocks at RVA " + rva(n.blocks) +
                  (n.kind == NameTableKind::Pool ? ", length shift " + std::to_string(n.lenShift) +
                                                       ", block shift " + std::to_string(n.blockShift) +
                                                       (n.poolHeader ? ", case-preserving (12-byte FName, entries on 4 bytes)" : "")
                                                  : ", chars at " + Hex(n.entryChars) + ", index at " + Hex(n.entryIndex) +
                                                        ", " + std::to_string(n.chunkEntries) + " names a chunk"));
    const ObjectHeader& h = d.objects.header;
    out.push_back("UObject: ClassPrivate " + Hex(h.cls) + ", NamePrivate " + Hex(h.name) + ", ObjectFlags " + Hex(h.flags) +
                  " with RF_ClassDefaultObject " + Hex(h.classDefault) + " and RF_ArchetypeObject " + Hex(h.archetype));
    out.push_back(std::string("objects: ") + (a.inlineChunks ? "inline chunk table" : a.chunked ? "chunked" : "fixed") + " ObjObjects at RVA " + rva(a.objects) +
                  ", item size " + Hex(a.itemSize) + " with the object at +" + Hex(a.itemObject) + ", " + std::to_string(ObjectCount(*d.objects.mem, a)) + " objects");
    out.push_back(std::string("reflection: ") + (L.model == PropertyModel::FField ? "FField" : "UProperty") +
                  ", reals " + std::to_string(L.realSize * 8) + "-bit" +
                  ", SuperStruct " + Hex(L.superStruct) + ", ChildProperties " + Hex(L.childProperties) +
                  ", PropertiesSize " + Hex(L.propertiesSize) + ", Next " + Hex(L.fieldNext) +
                  ", Name " + Hex(L.fieldName) +
                  (L.model == PropertyModel::FField ? ", FFieldClass name " + Hex(L.fieldClassName) : std::string{}) +
                  ", ArrayDim " + Hex(L.arrayDim) +
                  ", ElementSize " + Hex(L.elementSize) + ", Offset " + Hex(L.offset) +
                  ", property data " + Hex(L.propertyExtra) + ", FunctionFlags " + Hex(L.functionFlags) +
                  ", Func " + Hex(L.functionFunc));
    out.push_back("ProcessEvent at RVA " + rva(d.processEvent));
    return out;
}

}  // namespace cameraunlock::unreal
