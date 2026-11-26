// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// Tests for Unreal's pin-free reflection (cameraunlock/unreal/ue_discovery.h and
// the headers it builds on), run against a synthetic process laid out the way
// each engine generation lays it out, and for the UMG crosshair mover's
// refusal of a game without UMG.

#include <cameraunlock/unreal/ue_call.h>
#include <cameraunlock/unreal/ue_discovery.h>
#include <cameraunlock/unreal/ue_names.h>
#include <cameraunlock/unreal/ue_object_sweep.h>
#include <cameraunlock/unreal/ue_objects.h>
#include <cameraunlock/unreal/ue_params.h>
#include <cameraunlock/unreal/ue_reflection.h>
#include <cameraunlock/unreal/umg_crosshair.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace cameraunlock::unreal;
using cameraunlock::memory::CodeMap;
using cameraunlock::memory::Memory;
using cameraunlock::memory::ModuleMap;
using cameraunlock::memory::Range;

int g_failures = 0;

void Check(bool cond, const char* what, int line) {
    if (!cond) {
        ++g_failures;
        std::cout << "  [FAIL] line " << line << ": " << what << "\n";
    }
}

#define CHECK(cond) Check((cond), #cond, __LINE__)

// A flat arena standing in for the process: one module (code then writable
// data) followed by heap.
class Arena final : public Memory, public CodeMap {
public:
    static constexpr std::uintptr_t kBase = 0x140000000ull;
    static constexpr std::size_t kCode = 0x10000, kData = 0x10000, kConst = 0x1000, kHeap = 0x800000;

    Arena() : bytes_(kCode + kData + kConst + kHeap, 0) {
        module.base = kBase;
        module.end = kBase + kCode + kData + kConst;
        module.code.push_back({kBase, kBase + kCode});
        module.writable.push_back({kBase + kCode, kBase + kCode + kData});
        module.readonly.push_back({kBase + kCode + kData, kBase + kCode + kData + kConst});
        codeCursor_ = kBase + 0x100;
        dataCursor_ = kBase + kCode;
        heapCursor_ = kBase + kCode + kData + kConst;
    }

    bool Read(std::uintptr_t addr, void* out, std::size_t size) const override {
        if (addr < kBase || addr + size > kBase + bytes_.size()) return false;
        std::memcpy(out, bytes_.data() + (addr - kBase), size);
        return true;
    }

    template <class T>
    void Put(std::uintptr_t addr, const T& v) {
        std::memcpy(bytes_.data() + (addr - kBase), &v, sizeof(T));
    }
    void PutBytes(std::uintptr_t addr, const std::vector<std::uint8_t>& b) {
        std::memcpy(bytes_.data() + (addr - kBase), b.data(), b.size());
    }

    std::uintptr_t Heap(std::size_t size) { return Bump(heapCursor_, size); }
    std::uintptr_t Data(std::size_t size) { return Bump(dataCursor_, size); }

    // A function of its own unwind entry holding `body`.
    std::uintptr_t Function(const std::vector<std::uint8_t>& body) {
        const std::uintptr_t at = Bump(codeCursor_, body.size() + 16);
        PutBytes(at, body);
        functions_.push_back({at, at + body.size()});
        return at;
    }

    Range PartAt(std::uintptr_t at) const override {
        for (const Range& r : functions_)
            if (r.Contains(at)) return r;
        return {};
    }
    std::uintptr_t RootOf(std::uintptr_t at) const override { return PartAt(at).begin; }
    std::vector<Range> Parts(std::uintptr_t root) const override {
        for (const Range& r : functions_)
            if (r.begin == root) return {r};
        return {};
    }

    ModuleMap module;

private:
    static std::uintptr_t Bump(std::uintptr_t& cursor, std::size_t size) {
        const std::uintptr_t at = cursor;
        cursor += (size + 15) & ~std::size_t{15};
        return at;
    }

    std::vector<std::uint8_t> bytes_;
    std::uintptr_t codeCursor_, dataCursor_, heapCursor_;
    std::vector<Range> functions_;
};

// UObjectBase as every UE4 and UE5 build lays it out, planted by the builder
// and expected back from FindObjectHeader: ObjectFlags, ClassPrivate and
// NamePrivate, RF_Public, RF_Standalone, RF_ClassDefaultObject and RF_ArchetypeObject.
constexpr std::size_t kHeaderFlags = 0x08, kHeaderClass = 0x10, kHeaderName = 0x18;
constexpr std::uint32_t kPublic = 0x1, kStandalone = 0x2, kClassDefault = 0x10, kArchetype = 0x20;
// EFunctionFlags: FUNC_Net, NetServer, NetClient, NetValidate.
constexpr std::uint32_t kFuncNet = 0x40, kFuncNetServer = 0x200000, kFuncNetClient = 0x1000000, kFuncNetValidate = 0x80000000;

// The layout one engine generation uses, planted by the builder and expected back.
struct Generation {
    const char* label;
    bool pool;                // FNamePool, else TNameEntryArray
    bool chunked;
    bool reflectedViewPoint;  // Controller:GetPlayerViewPoint is a UFunction
    std::size_t itemSize;
    std::size_t itemObject;
    PropertyModel model;
    std::size_t realSize;
    std::size_t super, child, propsSize, functionFlags, functionFunc;
    std::size_t next, name, arrayDim, offset, extra;
    bool compactHeader = false;  // the chunked array's counts follow Objects, with no PreAllocatedObjects
    bool inlineChunks = false;   // UE 4.9: ObjObjects is the chunk table, 16384 object pointers a chunk
    std::size_t fieldClassName = 0;  // UE 5.7: FFieldClass opens with a pointer, its name follows
    bool noDestroyActor = false;     // Actor:K2_DestroyActor is not reflected (Tiny Tina's Wonderlands)
    std::uint8_t nativeMaskHigh = 0;  // ProcessEvent tests FUNC_Native with this bit of the low byte too (Split Fiction: 0x10)
    bool casePreserving = false;      // 4-byte ComparisonId before each pool entry, 12-byte FName (Pinball FX)
};

// UE 4.27 as Deep Rock Galactic's build table records it.
const Generation kUE427{"UE 4.27 FField", true, true, true, 0x18, 0, PropertyModel::FField, 4,
                        0x40, 0x50, 0x58, 0xB0, 0xD8, 0x20, 0x28, 0x38, 0x4C, 0x78};
// UE 5.x: 8-byte FFieldVariant, double reals.
const Generation kUE5{"UE 5 FField doubles", true, true, false, 0x18, 0, PropertyModel::FField, 8,
                      0x40, 0x50, 0x58, 0xB0, 0xD8, 0x18, 0x20, 0x30, 0x44, 0x70};
// UE 5.7 packs the item's flags ahead of its object pointer, and FFieldClass's name behind a pointer.
const Generation kUE57{"UE 5.7 object items with flags first", true, true, false, 0x18, 8, PropertyModel::FField, 8,
                       0x40, 0x50, 0x58, 0xB0, 0xD8, 0x18, 0x20, 0x30, 0x44, 0x70, false, false, 8};
const Generation kCompactArray{"object array header without PreAllocatedObjects", true, true, false, 0x18, 8,
                                PropertyModel::FField, 8, 0x40, 0x50, 0x58, 0xB0, 0xD8, 0x18, 0x20, 0x30, 0x44, 0x70, true};
// UE 4.2x before FField: properties are UObjects on Children, a fixed array, GNames.
const Generation kUE418{"UE 4.18 UProperty", false, false, true, 0x18, 0, PropertyModel::UProperty, 4,
                        0x30, 0x38, 0x40, 0x88, 0xB0, 0x28, kHeaderName, 0x30, 0x44, 0x70};
// GetPlayerViewPoint not reflected.
const Generation kUE412{"UE 4.12", false, false, false, 0x18, 0,
                        PropertyModel::UProperty, 4, 0x30, 0x38, 0x40, 0x88, 0xB0, 0x28, kHeaderName, 0x30, 0x50, 0x78};
// UE 4.9 (Ethan Carter Redux): the 4.12 reflection over a table of chunk pointers.
const Generation kUE49{"UE 4.9 inline chunk table", false, false, false, 8, 0, PropertyModel::UProperty, 4,
                       0x30, 0x38, 0x40, 0x88, 0xB0, 0x28, kHeaderName, 0x30, 0x50, 0x78, false, true};
// Tiny Tina's Wonderlands: FNamePool names over UProperty reflection, and of the stock
// calibration natives only K2_GetActorLocation and GetActorForwardVector are reflected.
const Generation kWonderlands{"FNamePool over UProperty, K2_DestroyActor not reflected", true, true, false, 0x18, 0,
                              PropertyModel::UProperty, 4, 0x30, 0x38, 0x40, 0x88, 0xB0, 0x28, kHeaderName, 0x30, 0x44,
                              0x70, false, false, 0, true};
// Split Fiction (Hazelight's Angelscript UE5): ProcessEvent tests FunctionFlags against 0x410.
const Generation kSplitFiction{"UE 5 with ProcessEvent testing 0x410", true, true, false, 0x18, 0, PropertyModel::FField, 8,
                               0x40, 0x50, 0x58, 0xB0, 0xD8, 0x18, 0x20, 0x30, 0x44, 0x70, false, false, 0, false, 0x10};
// Pinball FX (UE 4.27 built with case-preserving names): pool entries open with a
// 4-byte ComparisonId and sit on 4 bytes, an FName is 12 bytes, so UObject grows to
// 0x30 and every UStruct field moves 8 bytes on; FField's fields before its name do not.
const Generation kCasePreserving{"UE 4.27 with case-preserving names", true, true, true, 0x18, 0, PropertyModel::FField, 4,
                                 0x48, 0x58, 0x60, 0xB8, 0xE0, 0x20, 0x28, 0x38, 0x4C, 0x78,
                                 false, false, 0, false, 0, true};

class World {
public:
    // `actorDefault` false leaves Default__Actor out, as in a game the consumer
    // is loaded into before the engine has created its class defaults.
    // `natives` is how many of Actor's native functions exist, with Controller's
    // not made yet; -1 makes them all.
    // `umg` adds the UMG and Slate types and functions the crosshair mover looks up.
    explicit World(const Generation& g, bool actorDefault = true, int natives = -1, bool umg = false)
        : g_(g), actorDefault_(actorDefault), natives_(natives), umg_(umg) {
        BuildNames();
        BuildObjects();
        SealNames();
    }

    // The array holds only its first `n` objects.
    void Truncate(std::int32_t n) {
        if (!countAt_) throw std::logic_error("this generation's object array has no count to lower");
        arena.Put<std::int32_t>(countAt_, n);
    }

    Arena arena;
    std::map<std::string, std::uintptr_t> obj;
    std::int32_t padDefaults = 0;  // the slot of the first of the padding's class defaults
    std::uintptr_t processEvent = 0, namesAt = 0, objectsAt = 0;
    // A name past the first block or chunk, and its index.
    std::uint32_t secondBlockName = 0;
    static constexpr const char* kSecondBlockName = "SecondBlockName";

    std::uint32_t NameId(const std::string& s) { return Name(s); }

private:
    // ---- names ----
    std::uint32_t Name(const std::string& s) {
        if (auto it = nameIds_.find(s); it != nameIds_.end()) return it->second;
        std::uint32_t id;
        if (g_.pool) {
            id = static_cast<std::uint32_t>(poolCursor_ / Stride());
            poolCursor_ += PoolEntry(block0_ + poolCursor_, s, id);
        } else {
            id = static_cast<std::uint32_t>(nameIds_.size());
            const std::uintptr_t entry = arena.Heap(0x20 + s.size());
            arena.Put<std::uint32_t>(entry + 8, id << 1);
            arena.PutBytes(entry + 0x10, std::vector<std::uint8_t>(s.begin(), s.end()));
            arena.Put<std::uint64_t>(chunk0_ + id * 8ull, entry);
        }
        nameIds_[s] = id;
        return id;
    }

    // The pool's entries on 2 bytes, or 4 with case-preserving names.
    std::size_t Stride() const { return g_.casePreserving ? 4 : 2; }

    // A pool entry of `s` at `at`, and the bytes it takes.
    std::size_t PoolEntry(std::uintptr_t at, const std::string& s, std::uint32_t id) {
        const std::size_t prefix = g_.casePreserving ? 4 : 0;
        if (prefix) arena.Put<std::uint32_t>(at, id);
        arena.Put<std::uint16_t>(at + prefix, static_cast<std::uint16_t>(s.size() << (g_.casePreserving ? 1 : 6)));
        arena.PutBytes(at + prefix + 2, std::vector<std::uint8_t>(s.begin(), s.end()));
        return (prefix + 2 + s.size() + Stride() - 1) / Stride() * Stride();
    }

    // Block 0 filled to within one longest name of its 65536 strides, as the
    // engine leaves it when it begins block 1, whose first name is planted.
    // The table's first chunk filled to its 16384 entries and the second begun.
    void SealNames() {
        if (g_.pool) {
            const std::size_t end = Stride() << 16;
            while (end - poolCursor_ > 1200) poolCursor_ += PoolEntry(block0_ + poolCursor_, std::string(1000, 'p'), 0);
            secondBlockName = 1u << 16;
            PoolEntry(block1_, kSecondBlockName, secondBlockName);
            return;
        }
        const auto entry = [&](const std::string& s, std::uint32_t id) {
            const std::uintptr_t e = arena.Heap(0x20 + s.size());
            arena.Put<std::uint32_t>(e + 8, id << 1);
            arena.PutBytes(e + 0x10, std::vector<std::uint8_t>(s.begin(), s.end()));
            return e;
        };
        arena.Put<std::uint64_t>(chunk0_ + 16383 * 8ull, entry("LastOfChunk0", 16383));
        const std::uintptr_t chunk1 = arena.Heap(16384 * 8);
        arena.Put<std::uint64_t>(namesAt + 8, chunk1);
        secondBlockName = 16384;
        arena.Put<std::uint64_t>(chunk1, entry(kSecondBlockName, secondBlockName));
    }

    void BuildNames() {
        if (g_.pool) {
            // A stray copy of the block-0 pointer comes first in data, followed by
            // a non-pointer; the pool proper has two blocks.
            block0_ = arena.Heap(Stride() << 16);
            const std::uintptr_t block1 = block1_ = arena.Heap(0x100);
            const std::uintptr_t stray = arena.Data(0x10);
            arena.Put<std::uint64_t>(stray, block0_);
            arena.Put<std::uint64_t>(stray + 8, 7);
            const std::uintptr_t pool = arena.Data(0x10 + 8 * 8);
            namesAt = pool + 0x10;
            arena.Put<std::uint64_t>(namesAt, block0_);
            arena.Put<std::uint64_t>(namesAt + 8, block1);
        } else {
            chunk0_ = arena.Heap(16384 * 8);
            const std::uintptr_t table = arena.Heap(128 * 8 + 8);
            arena.Put<std::uint64_t>(table, chunk0_);
            const std::uintptr_t global = arena.Data(8);
            arena.Put<std::uint64_t>(global, table);
            namesAt = table;
        }
        Name("None");
        Name("ByteProperty");
    }

    // ---- objects ----
    std::uintptr_t NewObject(const std::string& name, std::uintptr_t cls, std::uintptr_t outer, std::size_t size = 0x200) {
        const std::uintptr_t o = arena.Heap(size);
        const auto index = static_cast<std::int32_t>(slots_.size());
        arena.Put<std::int32_t>(o + kUObjectIndex, index);
        arena.Put<std::uint64_t>(o + kHeaderClass, cls);
        arena.Put<std::uint32_t>(o + kHeaderName, Name(name));
        // Every class default is an archetype too; its default subobjects are archetypes only.
        if (name.rfind("Default__", 0) == 0) arena.Put<std::uint32_t>(o + kHeaderFlags, kPublic | kClassDefault | kArchetype);
        else if (outer && arena.Or0<std::uint32_t>(outer + kHeaderFlags) & kClassDefault)
            arena.Put<std::uint32_t>(o + kHeaderFlags, kPublic | kArchetype);
        arena.Put<std::uint64_t>(o + (g_.casePreserving ? 0x28 : 0x20), outer);
        slots_.push_back(o);
        return o;
    }

    std::uintptr_t FieldClass(const std::string& type) {
        if (auto it = fieldClasses_.find(type); it != fieldClasses_.end()) return it->second;
        const std::uintptr_t fc = arena.Heap(0x40);
        if (g_.fieldClassName) arena.Put<std::uint64_t>(fc, Arena::kBase + 0x100);
        arena.Put<std::uint32_t>(fc + g_.fieldClassName, Name(type));
        return fieldClasses_[type] = fc;
    }

    // One property, linked after `prev` (or as the struct's first).
    std::uintptr_t Property(std::uintptr_t owner, std::uintptr_t prev, const std::string& name,
                            const std::string& type, std::int32_t offset, std::int32_t size,
                            std::uintptr_t extra = 0) {
        std::uintptr_t p;
        if (g_.model == PropertyModel::FField) {
            p = arena.Heap(0x100);
            arena.Put<std::uint64_t>(p + 8, FieldClass(type));
            arena.Put<std::uint32_t>(p + g_.name, Name(name));
            arena.Put<std::uint32_t>(p + g_.name + (g_.casePreserving ? 12 : 8), 1);  // FlagsPrivate = RF_Public
        } else {
            p = NewObject(name, obj.at(type), owner, 0x100);
        }
        arena.Put<std::int32_t>(p + g_.arrayDim, 1);
        arena.Put<std::int32_t>(p + g_.arrayDim + 4, size);
        arena.Put<std::int32_t>(p + g_.offset, offset);
        if (extra) arena.Put<std::uint64_t>(p + g_.extra, extra);
        if (prev) arena.Put<std::uint64_t>(prev + g_.next, p);
        else arena.Put<std::uint64_t>(owner + g_.child, p);
        return p;
    }

    void Reals(std::uintptr_t s, std::initializer_list<const char*> names) {
        const auto e = static_cast<std::int32_t>(g_.realSize);
        const char* type = g_.realSize == 8 ? "DoubleProperty" : "FloatProperty";
        std::uintptr_t prev = 0;
        std::int32_t at = 0;
        for (const char* n : names) {
            prev = Property(s, prev, n, type, at, e);
            at += e;
        }
        arena.Put<std::int32_t>(s + g_.propsSize, at);
    }

    std::uintptr_t Vtable(std::initializer_list<std::uintptr_t> fns) {
        const std::uintptr_t vt = arena.Data(fns.size() * 8 + 8);
        std::size_t i = 0;
        for (std::uintptr_t f : fns) arena.Put<std::uint64_t>(vt + 8 * i++, f);
        return vt;
    }

    std::vector<std::uint8_t> Ret() { return {0xC3}; }

    // FBoolProperty's FieldSize, ByteOffset, ByteMask and FieldMask for a one-byte field.
    static std::uintptr_t BoolBits(std::uint8_t mask) {
        return 1u | (static_cast<std::uintptr_t>(mask) << 16) | (static_cast<std::uintptr_t>(mask) << 24);
    }

    void BuildObjects() {
        const std::uintptr_t core = NewObject("/Script/CoreUObject", 0, 0);
        const std::uintptr_t engine = NewObject("/Script/Engine", 0, 0);
        const std::uintptr_t cls = NewObject("Class", 0, core);
        arena.Put<std::uint64_t>(cls + kHeaderClass, cls);
        const std::uintptr_t package = NewObject("Package", cls, core);
        arena.Put<std::uint64_t>(core + kHeaderClass, package);
        arena.Put<std::uint64_t>(engine + kHeaderClass, package);
        const std::uintptr_t scriptStruct = NewObject("ScriptStruct", cls, core);
        const std::uintptr_t function = NewObject("Function", cls, core);
        obj["Class"] = cls;

        const auto Cls = [&](const char* name, std::uintptr_t pkg, std::uintptr_t super) {
            const std::uintptr_t c = NewObject(name, cls, pkg);
            arena.Put<std::uint32_t>(c + kHeaderFlags, kPublic | kStandalone);
            arena.Put<std::uint64_t>(c + g_.super, super);
            return obj[name] = c;
        };
        const std::uintptr_t object = Cls("Object", core, 0);
        const std::uintptr_t actor = Cls("Actor", engine, object);
        const std::uintptr_t controller = Cls("Controller", engine, actor);
        const std::uintptr_t playerController = Cls("PlayerController", engine, controller);
        const std::uintptr_t player = Cls("Player", engine, object);
        const std::uintptr_t localPlayer = Cls("LocalPlayer", engine, player);
        if (g_.model == PropertyModel::UProperty)
            for (const char* t : {"FloatProperty", "DoubleProperty", "StructProperty", "BoolProperty", "IntProperty",
                                  "ObjectProperty", "ArrayProperty"})
                Cls(t, core, 0);

        const auto Struct = [&](const char* name, std::uintptr_t pkg) { return obj[name] = NewObject(name, scriptStruct, pkg); };
        Reals(Struct("Vector", core), {"X", "Y", "Z"});
        Reals(Struct("Rotator", core), {"Pitch", "Yaw", "Roll"});
        Reals(Struct("Quat", core), {"X", "Y", "Z", "W"});
        const auto e = static_cast<std::int32_t>(g_.realSize);
        const std::uintptr_t transform = Struct("Transform", core);
        std::uintptr_t p = Property(transform, 0, "Rotation", "StructProperty", 0, 4 * e, obj["Quat"]);
        p = Property(transform, p, "Translation", "StructProperty", 4 * e, 3 * e, obj["Vector"]);
        Property(transform, p, "Scale3D", "StructProperty", 7 * e, 3 * e, obj["Vector"]);

        // Two enums, one plain and one scoped, each with its Names at 0x40
        // after a CppType string that is an array too.
        const std::uintptr_t enumCls = NewObject("Enum", cls, core);
        const auto Enum = [&](const char* name, std::initializer_list<std::pair<const char*, std::int64_t>> entries) {
            const std::uintptr_t en = obj[name] = NewObject(name, enumCls, engine);
            const std::uintptr_t cppType = arena.Heap(0x200);  // 8 wide chars, in a buffer no Names stride reads past
            arena.Put<std::uint64_t>(en + 0x30, cppType);
            arena.Put<std::int32_t>(en + 0x38, 8);
            arena.Put<std::int32_t>(en + 0x3C, 8);
            const std::size_t fname = g_.casePreserving ? 12 : 8;
            const std::size_t stride = ((fname + 7) & ~std::size_t{7}) + 8;
            const std::uintptr_t names = arena.Heap(entries.size() * stride);
            std::size_t i = 0;
            for (const auto& [entry, value] : entries) {
                arena.Put<std::uint32_t>(names + i * stride, Name(entry));
                if (g_.casePreserving) arena.Put<std::uint32_t>(names + i * stride + 4, Name(entry));
                arena.Put<std::int64_t>(names + i * stride + stride - 8, value);
                ++i;
            }
            arena.Put<std::uint64_t>(en + 0x40, names);
            arena.Put<std::int32_t>(en + 0x48, static_cast<std::int32_t>(entries.size()));
            arena.Put<std::int32_t>(en + 0x4C, static_cast<std::int32_t>(entries.size()));
        };
        Enum("EPhysBodyOp", {{"PBO_None", 0}, {"PBO_Term", 1}, {"PBO_MAX", 2}});
        Enum("ESlateVisibility", {{"ESlateVisibility::Visible", 0},
                                  {"ESlateVisibility::Collapsed", 1},
                                  {"ESlateVisibility::ESlateVisibility_MAX", 5}});

        if (umg_) {
            // UMG as the crosshair mover reads it: Vector2D, SlateCore's
            // WidgetTransform under Widget.RenderTransform, and the functions
            // it calls with their parameter blocks.
            const std::uintptr_t umgPkg = NewObject("/Script/UMG", package, 0);
            const std::uintptr_t slatePkg = NewObject("/Script/SlateCore", package, 0);
            const std::uintptr_t vector2D = Struct("Vector2D", core);
            Reals(vector2D, {"X", "Y"});
            const std::uintptr_t widgetTransform = Struct("WidgetTransform", slatePkg);
            p = Property(widgetTransform, 0, "Translation", "StructProperty", 0, 2 * e, vector2D);
            p = Property(widgetTransform, p, "Scale", "StructProperty", 2 * e, 2 * e, vector2D);
            p = Property(widgetTransform, p, "Shear", "StructProperty", 4 * e, 2 * e, vector2D);
            Property(widgetTransform, p, "Angle", g_.realSize == 8 ? "DoubleProperty" : "FloatProperty", 6 * e, e);
            const std::uintptr_t geometry = Struct("Geometry", slatePkg);
            const std::uintptr_t widget = Cls("Widget", umgPkg, object);
            Property(widget, 0, "RenderTransform", "StructProperty", 0x100, 7 * e, widgetTransform);
            const std::uintptr_t userWidget = Cls("UserWidget", umgPkg, widget);
            const std::uintptr_t layoutLibrary = Cls("WidgetLayoutLibrary", umgPkg, object);
            const std::uintptr_t slateLibrary = Cls("SlateBlueprintLibrary", umgPkg, object);
            NewObject("Default__WidgetLayoutLibrary", layoutLibrary, umgPkg);
            NewObject("Default__SlateBlueprintLibrary", slateLibrary, umgPkg);
            struct Param {
                const char* name;
                const char* type;
                std::int32_t size;
                std::uintptr_t extra;
            };
            const auto Func = [&](const char* name, std::uintptr_t owner, std::initializer_list<Param> params) {
                const std::uintptr_t fn = NewObject(name, function, owner);
                std::uintptr_t prev = 0;
                std::int32_t at = 0;
                for (const Param& q : params) {
                    prev = Property(fn, prev, q.name, q.type, at, q.size, q.extra);
                    at += q.size;
                }
                arena.Put<std::int32_t>(fn + g_.propsSize, at);
            };
            const auto v2 = [&](const char* n) { return Param{n, "StructProperty", 2 * e, vector2D}; };
            const Param context{"WorldContextObject", "ObjectProperty", 8, 0};
            const Param geo{"Geometry", "StructProperty", 0x38, geometry};
            Func("SetRenderTranslation", widget, {v2("Translation")});
            Func("GetCachedGeometry", widget, {{"ReturnValue", "StructProperty", 0x38, geometry}});
            Func("GetOwningPlayer", userWidget, {{"ReturnValue", "ObjectProperty", 8, 0}});
            Func("GetViewportSize", layoutLibrary, {context, v2("ReturnValue")});
            Func("GetViewportScale", layoutLibrary, {context, {"ReturnValue", "FloatProperty", 4, 0}});
            Func("LocalToViewport", slateLibrary,
                 {context, geo, v2("LocalCoordinate"), v2("PixelPosition"), v2("ViewportPosition")});
            Func("LocalToAbsolute", slateLibrary, {geo, v2("LocalCoordinate"), v2("ReturnValue")});
            Func("ProjectWorldLocationToScreen", playerController,
                 {{"WorldLocation", "StructProperty", 3 * e, obj["Vector"]}, v2("ScreenLocation"),
                  {"bPlayerViewportRelative", "BoolProperty", 1, BoolBits(0x01)}, {"ReturnValue", "BoolProperty", 1, BoolBits(0x01)}});
        }

        // A game's own classes and instances.
        const std::uintptr_t game = NewObject("/Script/TestGame", package, 0);
        const std::uintptr_t aim = Struct("TestAim", game);
        Property(aim, 0, "Zoom", "FloatProperty", 4, 4);
        const std::uintptr_t weaponCls = Cls("TestWeapon", game, actor);
        Property(weaponCls, 0, "bAiming", "BoolProperty", 0x300, 1, BoolBits(0x01));
        const std::uintptr_t pawnCls = Cls("TestPawn", game, actor);
        p = Property(pawnCls, 0, "bIsDead", "BoolProperty", 0x300, 1, BoolBits(0x04));
        p = Property(pawnCls, p, "Health", "IntProperty", 0x304, 4);
        p = Property(pawnCls, p, "Weapon", "ObjectProperty", 0x308, 8);
        p = Property(pawnCls, p, "Aim", "StructProperty", 0x310, 8, aim);
        Property(pawnCls, p, "Players", "ArrayProperty", 0x320, 16);
        // bool GetState(bool& Playing): ReturnValue at 0, the out parameter at 1.
        const std::uintptr_t getState = NewObject("GetState", function, pawnCls);
        p = Property(getState, 0, "Playing", "BoolProperty", 1, 1, BoolBits(0x01));
        Property(getState, p, "ReturnValue", "BoolProperty", 0, 1, BoolBits(0x01));
        arena.Put<std::int32_t>(getState + g_.propsSize, 2);
        obj["weapon"] = NewObject("TestWeapon_0", weaponCls, game, 0x400);
        obj["pawn"] = NewObject("TestPawn_0", pawnCls, game, 0x400);
        arena.Put<std::uint8_t>(obj["pawn"] + 0x300, 0x04);
        arena.Put<std::int32_t>(obj["pawn"] + 0x304, 75);
        arena.Put<std::uint64_t>(obj["pawn"] + 0x308, obj["weapon"]);

        // Native callables: FUNC_Native | FUNC_BlueprintCallable, Func into code.
        const auto Native = [&](const char* name, std::uintptr_t owner, std::uintptr_t func) {
            const std::uintptr_t f = NewObject(name, function, owner);
            arena.Put<std::uint32_t>(f + g_.functionFlags, 0x04020401u);
            arena.Put<std::uint64_t>(f + g_.functionFunc, func);
            return f;
        };
        int actorNatives = 0;
        const auto ActorNative = [&](const char* name) {
            if (natives_ < 0 || actorNatives++ < natives_) Native(name, actor, arena.Function(Ret()));
        };
        if (natives_ < 0 && g_.reflectedViewPoint) Native("GetPlayerViewPoint", controller, arena.Function(Ret()));
        ActorNative("K2_GetActorLocation");
        if (!g_.noDestroyActor) ActorNative("K2_DestroyActor");
        ActorNative("GetActorForwardVector");
        ActorNative("K2_GetActorRotation");
        // PlayerController's functions and Actor's Blueprint event with the
        // flags The Ascent's and Hogwarts Legacy's UHT-generated function
        // parameters hold: two remote server functions, a remote client one,
        // a remote client one not named Client..., local ones named Server...
        // and Client..., and a local getter.
        for (const auto& [name, flags] : std::initializer_list<std::pair<const char*, std::uint32_t>>{
                 {"ServerUpdateCamera", 0x80220c40u},
                 {"ServerExecRPC", 0x80220cc0u},
                 {"ServerExec", 0x00020601u},
                 {"ClientSetHUD", 0x05020cc0u},
                 {"OnServerStartedVisualLogger", 0x01020cc0u},
                 {"ClientTravel", 0x00820401u},
                 {"GetHUD", 0x54020401u}})
            arena.Put<std::uint32_t>(NewObject(name, function, playerController) + g_.functionFlags, flags);
        arena.Put<std::uint32_t>(NewObject("ReceiveBeginPlay", function, actor) + g_.functionFlags, 0x08080800u);

        // UObject vtable: filler, ProcessEvent (tests FunctionFlags & FUNC_Native), filler.
        const auto f = static_cast<std::uint32_t>(g_.functionFlags);
        // The cmp's displacement is patched once the array exists.
        processEvent = arena.Function({0x41, 0xF7, 0x86, static_cast<std::uint8_t>(f), static_cast<std::uint8_t>(f >> 8), 0, 0,
                                       g_.nativeMaskHigh, 0x04, 0x00, 0x00,  // test dword [r14+FunctionFlags], 0x400
                                       0x3B, 0x05, 0, 0, 0, 0,               // cmp eax, [rip+NumElements]
                                       0xC3});                               // ret
        // A decoy: another overridden virtual testing FUNC_Native, with no object array lookup.
        const std::uintptr_t decoy = arena.Function({0xF7, 0x81, static_cast<std::uint8_t>(f), static_cast<std::uint8_t>(f >> 8), 0, 0,
                                                     0x00, 0x04, 0x00, 0x00,  // test dword [rcx+FunctionFlags], 0x400
                                                     0xC3});                  // ret
        const std::uintptr_t actorProcessEvent = arena.Function(Ret());
        const std::uintptr_t filler = arena.Function(Ret());
        const std::uintptr_t actorDecoy = arena.Function(Ret());
        // Past the end of UObject's vtable sits the next class's (no RTTI marks
        // the boundary): a function there that looks just like ProcessEvent.
        pastTheEnd_ = arena.Function({0x41, 0xF7, 0x86, static_cast<std::uint8_t>(f), static_cast<std::uint8_t>(f >> 8), 0, 0,
                                      0x00, 0x04, 0x00, 0x00,  // test dword [r14+FunctionFlags], 0x400
                                      0x3B, 0x05, 0, 0, 0, 0,  // cmp eax, [rip+NumElements]
                                      0xC3});                  // ret
        const std::uintptr_t localPlayerFn = arena.Function(Ret());
        const std::uintptr_t objectVt = Vtable({filler, decoy, processEvent, filler, pastTheEnd_});
        const std::uintptr_t actorVt = Vtable({filler, actorDecoy, actorProcessEvent, filler, filler});
        const std::uintptr_t classVt = Vtable({filler, decoy, processEvent, filler, filler});
        const std::uintptr_t localPlayerVt = Vtable({filler, decoy, processEvent, filler, localPlayerFn});
        arena.Put<std::uint64_t>(object, classVt);
        const auto Cdo = [&](const char* name, std::uintptr_t c, std::uintptr_t pkg, std::uintptr_t vt) {
            const std::uintptr_t o = NewObject(name, c, pkg);
            arena.Put<std::uint64_t>(o, vt);
            return o;
        };
        Cdo("Default__Object", object, core, objectVt);
        if (actorDefault_) Cdo("Default__Actor", actor, engine, actorVt);
        Cdo("Default__LocalPlayer", localPlayer, engine, localPlayerVt);

        // Pad the array past the minimum discovery accepts, then with class
        // defaults enough to read the flags off.
        while (slots_.size() < 0x500) NewObject("Pad" + std::to_string(slots_.size()), object, core, 0x40);
        padDefaults = static_cast<std::int32_t>(slots_.size());
        for (int i = 0; i < 40; ++i) NewObject("Default__Pad" + std::to_string(i), object, core, 0x40);
        NewObject("PadComponent", object, slots_.back(), 0x40);
        BuildArray();
        for (std::uintptr_t fn : {processEvent, pastTheEnd_}) {
            const std::uintptr_t disp = fn + 13;
            arena.Put<std::int32_t>(disp, static_cast<std::int32_t>(objectsAt + (g_.inlineChunks ? kInlineCounts : g_.chunked ? (g_.compactHeader ? 0x08 : 0x14) : 0x0C) - (disp + 4)));
        }
    }

    void BuildArray() {
        // An inline table's first chunk is a whole one: the slots past the
        // objects are read as free.
        const std::uintptr_t items = arena.Heap((g_.inlineChunks ? 16384 : slots_.size()) * g_.itemSize);
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            if (g_.itemObject) arena.Put<std::uint64_t>(items + i * g_.itemSize, 0x8000000000000001ull);
            arena.Put<std::uint64_t>(items + i * g_.itemSize + g_.itemObject, slots_[i]);
        }
        const auto num = static_cast<std::int32_t>(slots_.size());
        if (g_.inlineChunks) {
            // Two chunks of 16384 in a 512-entry table, the second a quarter full.
            const std::uintptr_t chunk1 = arena.Heap(16384 * 8);
            objectsAt = arena.Data(kInlineCounts + 8);
            arena.Put<std::uint64_t>(objectsAt, items);
            arena.Put<std::uint64_t>(objectsAt + 8, chunk1);
            arena.Put<std::int32_t>(objectsAt + kInlineCounts, 16384 + 4096);
            arena.Put<std::int32_t>(objectsAt + kInlineCounts + 4, 2);
        } else if (g_.chunked) {
            const std::uintptr_t chunks = arena.Heap(8);
            arena.Put<std::uint64_t>(chunks, items);
            const std::uintptr_t a = arena.Data(0x40);
            objectsAt = a + 0x10;
            arena.Put<std::uint64_t>(objectsAt, chunks);
            const std::size_t numAt = g_.compactHeader ? 0x08 : 0x14;
            arena.Put<std::int32_t>(objectsAt + (g_.compactHeader ? 0x0C : 0x10), num + 100);
            arena.Put<std::int32_t>(objectsAt + numAt, num);
            countAt_ = objectsAt + numAt;
            arena.Put<std::int32_t>(objectsAt + (g_.compactHeader ? 0x14 : 0x18), 4);
            arena.Put<std::int32_t>(objectsAt + (g_.compactHeader ? 0x10 : 0x1C), 1);
        } else {
            const std::uintptr_t a = arena.Data(0x40);
            objectsAt = a + 0x10;
            arena.Put<std::uint64_t>(objectsAt, items);
            arena.Put<std::int32_t>(objectsAt + 8, num + 100);
            arena.Put<std::int32_t>(objectsAt + 12, num);
            countAt_ = objectsAt + 12;
        }
    }

    static constexpr std::size_t kInlineCounts = 512 * 8;
    const Generation& g_;
    const bool actorDefault_;
    const int natives_;
    const bool umg_;
    std::uintptr_t countAt_ = 0;
    std::uintptr_t pastTheEnd_ = 0;
    std::map<std::string, std::uint32_t> nameIds_;
    std::map<std::string, std::uintptr_t> fieldClasses_;
    std::vector<std::uintptr_t> slots_;
    std::uintptr_t block0_ = 0, block1_ = 0, chunk0_ = 0;
    std::size_t poolCursor_ = 0;
};

void TestGeneration(const Generation& g) {
    std::cout << "  -- " << g.label << "\n";
    World w(g);
    int heard = 0;
    const ReflectionDiscoveryResult r =
        DiscoverReflection(w.arena, w.arena.module, w.arena, [&](ReflectionStage, long long) { ++heard; });
    if (!r.found) std::cout << "     stopped at " << StageName(r.reached) << ": " << r.why << "\n";
    CHECK(r.found.has_value());
    if (!r.found) return;
    CHECK(r.reached == ReflectionStage::Done);
    CHECK(heard == 6 && r.timings.size() == 6);
    const ReflectionDiscovery& d = *r.found;
    CHECK(d.objects.names.kind == (g.pool ? NameTableKind::Pool : NameTableKind::Array));
    CHECK(d.objects.names.blocks == w.namesAt);
    if (g.pool) CHECK(d.objects.names.blockShift == 16);
    else CHECK(d.objects.names.chunkEntries == 16384);
    CHECK(NameString(w.arena, d.objects.names, w.secondBlockName) == World::kSecondBlockName);
    CHECK(d.objects.array.objects == w.objectsAt);
    CHECK(d.objects.array.chunked == g.chunked);
    CHECK(d.objects.array.itemSize == g.itemSize);
    CHECK(d.objects.array.itemObject == g.itemObject);
    CHECK(d.objects.header.cls == kHeaderClass);
    CHECK(d.objects.header.name == kHeaderName);
    CHECK(d.objects.header.flags == kHeaderFlags);
    CHECK(d.objects.header.classDefault == kClassDefault);
    CHECK(d.objects.header.archetype == kArchetype);
    const ReflectionLayout& L = d.layout;
    CHECK(L.model == g.model);
    CHECK(L.realSize == g.realSize);
    CHECK(L.superStruct == g.super);
    CHECK(L.childProperties == g.child);
    CHECK(L.propertiesSize == g.propsSize);
    CHECK(L.fieldNext == g.next);
    CHECK(L.fieldName == g.name);
    if (g.model == PropertyModel::FField) CHECK(L.fieldClassName == g.fieldClassName);
    CHECK(L.arrayDim == g.arrayDim);
    CHECK(L.elementSize == g.arrayDim + 4);
    CHECK(L.offset == g.offset);
    CHECK(L.propertyExtra == g.extra);
    CHECK(L.functionFlags == g.functionFlags);
    CHECK(L.functionFunc == g.functionFunc);
    CHECK(d.processEvent == w.processEvent);
    const std::vector<std::string> lines = Describe(d, w.arena.module);
    CHECK(lines.size() == 5 && lines.back() == "ProcessEvent at RVA 0x" + [&] {
        char hex[32];
        std::snprintf(hex, sizeof hex, "%llx", static_cast<unsigned long long>(w.processEvent - Arena::kBase));
        return std::string(hex);
    }());

    const auto e = static_cast<std::int32_t>(g.realSize);
    const Reflection refl(d.objects, d.layout);
    CHECK(d.objects.PathName(w.obj["Actor"]) == "/Script/Engine.Actor");
    CHECK(refl.IsA(w.obj["PlayerController"], w.obj["Actor"]));
    CHECK(!refl.IsA(w.obj["LocalPlayer"], w.obj["Actor"]));
    const auto scale = refl.Find(w.obj["Transform"], "Scale3D");
    CHECK(scale.has_value() && scale->offset == 7 * e && scale->type == "StructProperty");
    CHECK(scale.has_value() && refl.PropertyStruct(*scale) == w.obj["Vector"]);
    // A property is found on the class that declares it, through a subclass's super chain.
    const auto health = refl.Find(w.obj["TestPawn"], "health");
    CHECK(health.has_value() && health->offset == 0x304 && health->size == 4 && health->type == "IntProperty");
    const auto dead = refl.Find(w.obj["TestPawn"], "bIsDead");
    std::int32_t byteOffset = 0;
    std::uint8_t mask = 0;
    CHECK(dead.has_value() && refl.BoolBit(*dead, byteOffset, mask) && byteOffset == 0x300 && mask == 0x04);
    CHECK(ClassMatches(d.objects, refl, w.obj["TestPawn"], {"Act*"}));
    CHECK(!ClassMatches(d.objects, refl, w.obj["TestPawn"], {"Controller"}));
    const std::uintptr_t pawn = w.obj["pawn"];
    const std::int32_t pawnSlot = w.arena.Or0<std::int32_t>(pawn + kUObjectIndex);
    CHECK(Live(d.objects, pawn, pawnSlot));
    CHECK(!Live(d.objects, pawn, pawnSlot + 1));

    // An object whose name carries a number is found by the name as it prints.
    const std::uintptr_t numbered = w.obj["LocalPlayer"];
    const std::string unnumbered = d.objects.PathName(numbered);
    CHECK(d.objects.FindPath(unnumbered) == numbered);
    w.arena.Put<std::uint32_t>(numbered + kHeaderName + (g.casePreserving ? 8 : 4), 4);
    CHECK(d.objects.PathName(numbered) == unnumbered + "_3");
    CHECK(d.objects.FindPath(unnumbered + "_3") == numbered);
    CHECK(d.objects.FindPath(unnumbered) == 0);

    // An index whose block is past the most blocks a pool holds is no name,
    // whatever lies past the table.
    if (g.pool) {
        w.arena.Put<std::uint64_t>(w.namesAt + 8192 * 8, w.arena.Ptr(w.namesAt));
        CHECK(NameString(w.arena, d.objects.names, 8192u << d.objects.names.blockShift).empty());
    }
}

// A torn object count is walked no further than kMaxObjects.
void TestTornObjectCount() {
    std::cout << "  -- a torn object count\n";
    World w(kUE418);
    const ReflectionDiscoveryResult r = DiscoverReflection(w.arena, w.arena.module, w.arena);
    CHECK(r.found.has_value());
    if (!r.found) return;
    w.Truncate(0x7FFFFFFF);
    std::int64_t asked = 0;
    r.found->objects.ForEach([&](std::uintptr_t) {
        ++asked;
        return false;
    });
    CHECK(asked > 0 && asked <= kMaxObjects);
}

// The header is refused rather than guessed: while the engine has made few
// class defaults it is asked for again, and a flag bit that a live object
// shares with the class defaults is no RF_ClassDefaultObject.
void TestObjectHeaderRefusals() {
    std::cout << "  -- UObject header refusals\n";
    World few(kUE427);
    few.Truncate(few.padDefaults);
    const ReflectionDiscoveryResult early = DiscoverReflection(few.arena, few.arena.module, few.arena);
    CHECK(!early.found.has_value());
    CHECK(early.reached == ReflectionStage::Header);
    CHECK(early.notLoaded);

    World shared(kUE427);
    shared.arena.Put<std::uint32_t>(shared.obj["pawn"] + kHeaderFlags, kClassDefault);
    const ReflectionDiscoveryResult r = DiscoverReflection(shared.arena, shared.arena.module, shared.arena);
    CHECK(!r.found.has_value());
    CHECK(r.reached == ReflectionStage::Header);
    CHECK(!r.notLoaded);
}

// Loaded before the engine created Default__Actor: discovery stops at
// ProcessEvent and says the object is not there yet, so the consumer asks again.
void TestClassDefaultNotYetCreated() {
    std::cout << "  -- class default not created yet\n";
    World w(kUE427, false);
    const ReflectionDiscoveryResult r = DiscoverReflection(w.arena, w.arena.module, w.arena);
    CHECK(!r.found.has_value());
    CHECK(r.reached == ReflectionStage::ProcessEvent);
    CHECK(r.notLoaded);
    CHECK(r.partial.has_value());
    World full(kUE427);
    const ReflectionDiscoveryResult done = DiscoverReflection(full.arena, full.arena.module, full.arena);
    CHECK(done.found.has_value());
    CHECK(!done.notLoaded);
}

// Loaded after the engine made its classes and before it made their native
// functions (Poppy Playtime Chapter 4, 45 s in), or while it makes them one by
// one: calibration stops and says they are not there yet.
void TestNativeFunctionsNotYetMade() {
    std::cout << "  -- native functions not made yet\n";
    for (const Generation* g : {&kUE427, &kUE5}) {
        for (int made : {0, 2}) {
            World w(*g, true, made);
            const ReflectionDiscoveryResult r = DiscoverReflection(w.arena, w.arena.module, w.arena);
            CHECK(!r.found.has_value());
            CHECK(r.reached == ReflectionStage::Reflection);
            CHECK(r.notLoaded);
        }
    }
}

void TestNameMatches() {
    std::cout << "  -- name patterns\n";
    CHECK(NameMatches("CineCamera*", "CineCameraActor"));
    CHECK(NameMatches("*Camera", "CineCamera"));
    CHECK(NameMatches("*Cine*", "BP_CineThing_C"));
    CHECK(!NameMatches("CineCamera", "CineCameraActor"));
    CHECK(NameMatches("player", "Player"));
    CHECK(NameMatches("FOV_*", "FOV_12_8A3BC1D24E5F67A8B9C0D1E2F3A4B5C6"));
    CHECK(!NameMatches("FOV_*", "FO"));
}

void TestEnumValue() {
    std::cout << "  -- enum values read off the UEnum\n";
    for (const Generation* g : {&kUE427, &kUE418, &kCasePreserving}) {
        World w(*g);
        const ReflectionDiscoveryResult r = DiscoverReflection(w.arena, w.arena.module, w.arena);
        CHECK(r.found.has_value());
        if (!r.found) continue;
        const Objects& o = r.found->objects;
        std::string why;
        const auto none = EnumValue(o, "/Script/Engine.EPhysBodyOp", "PBO_None", why);
        CHECK(none.has_value() && *none == 0);
        const auto term = EnumValue(o, "/Script/Engine.EPhysBodyOp", "PBO_Term", why);
        CHECK(term.has_value() && *term == 1);
        CHECK(EnumByte(o, "/Script/Engine.EPhysBodyOp", "PBO_Term") == 1);
        const auto collapsed = EnumValue(o, "/Script/Engine.ESlateVisibility", "Collapsed", why);
        CHECK(collapsed.has_value() && *collapsed == 1);
        CHECK(!EnumValue(o, "/Script/Engine.EPhysBodyOp", "PBO_Disable", why) &&
              why.find("no entry PBO_Disable") != std::string::npos);
        CHECK(!EnumValue(o, "/Script/Engine.Actor", "PBO_None", why) && why.find("no enum") != std::string::npos);
        bool threw = false;
        try {
            EnumByte(o, "/Script/Engine.EPhysBodyOp", "PBO_Disable");
        } catch (const std::runtime_error&) {
            threw = true;
        }
        CHECK(threw);
        const std::uintptr_t en = w.obj["EPhysBodyOp"];
        // Before UE 4.16 an entry's value is a byte after its name, and before
        // that it is the entry's index.
        if (!g->casePreserving) {
            const std::uintptr_t pairs = w.arena.Heap(3 * 12), names = w.arena.Heap(3 * 8);
            const char* entries[] = {"PBO_None", "PBO_Term", "PBO_MAX"};
            for (int i = 0; i < 3; ++i) {
                w.arena.Put<std::uint32_t>(pairs + i * 12, w.NameId(entries[i]));
                w.arena.Put<std::uint8_t>(pairs + i * 12 + 8, static_cast<std::uint8_t>(i));
                w.arena.Put<std::uint32_t>(names + i * 8, w.NameId(entries[i]));
            }
            const std::uintptr_t original = w.arena.Ptr(en + 0x40);
            w.arena.Put<std::uint64_t>(en + 0x40, pairs);
            const auto byteTerm = EnumValue(o, "/Script/Engine.EPhysBodyOp", "PBO_Term", why);
            CHECK(byteTerm.has_value() && *byteTerm == 1);
            w.arena.Put<std::uint64_t>(en + 0x40, names);
            const auto indexTerm = EnumValue(o, "/Script/Engine.EPhysBodyOp", "PBO_Term", why);
            CHECK(indexTerm.has_value() && *indexTerm == 1);
            w.arena.Put<std::uint64_t>(en + 0x40, original);
        }
        // A second array ending in _MAX leaves the Names unknown.
        w.arena.Put<std::uint64_t>(en + 0x50, w.arena.Ptr(en + 0x40));
        w.arena.Put<std::uint64_t>(en + 0x58, w.arena.Or0<std::uint64_t>(en + 0x48));
        CHECK(!EnumValue(o, "/Script/Engine.EPhysBodyOp", "PBO_None", why) &&
              why.find("2 arrays") != std::string::npos);
    }
}

void TestNetFunctionFlags() {
    std::cout << "  -- remote function flags\n";
    for (const Generation* g : {&kUE427, &kUE418}) {
        World w(*g);
        const ReflectionDiscoveryResult r = DiscoverReflection(w.arena, w.arena.module, w.arena);
        CHECK(r.found.has_value());
        if (!r.found) continue;
        const Reflection refl(r.found->objects, r.found->layout);
        std::string why;
        const auto net = FindNetFunctionFlags(refl, why);
        CHECK(net.has_value() && net->net == kFuncNet && net->client == kFuncNetClient &&
              net->server == (kFuncNetServer | kFuncNetValidate));
    }
}

bool Throws(const std::function<void()>& fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

// A reflected function's parameter block, laid out from its own properties,
// without calling it: there is no engine behind the arena.
void TestScriptFunction() {
    std::cout << "  -- a reflected function's parameters\n";
    World w(kUE5);
    const ReflectionDiscoveryResult r = DiscoverReflection(w.arena, w.arena.module, w.arena);
    CHECK(r.found.has_value());
    if (!r.found) return;
    const Reflection refl(r.found->objects, r.found->layout);
    const ScriptFunction fn(refl, r.found->processEvent, "/Script/TestGame.TestPawn:GetState");
    CHECK(fn.Path() == "/Script/TestGame.TestPawn:GetState");
    CHECK(fn.Param("playing").offset == 1);
    CHECK(fn.Param("ReturnValue", 1).offset == 0);
    CHECK(Throws([&] { fn.Param("Missing"); }));
    CHECK(Throws([&] { fn.Param("Playing", 4); }));
    ScriptFunction::Frame frame(fn);
    frame.Set<bool>("Playing", true);
    CHECK(frame.Get<bool>("Playing") && !frame.Get<bool>("ReturnValue"));
    CHECK(Throws([&] { frame.Set<std::int32_t>("Playing", 1); }));
    CHECK(Throws([&] { ScriptFunction(refl, r.found->processEvent, "/Script/TestGame.TestPawn:Nothing"); }));
    CHECK(Throws([&] { RealOf(fn, "Playing", 2); }));
}

// A game with no UMG loaded: the crosshair mover refuses it where it is built.
void TestCrosshairMoverNeedsUmg() {
    std::cout << "  -- crosshair mover without UMG\n";
    World w(kUE427);
    const ReflectionDiscoveryResult r = DiscoverReflection(w.arena, w.arena.module, w.arena);
    CHECK(r.found.has_value());
    if (!r.found) return;
    const Reflection refl(r.found->objects, r.found->layout);
    std::string what;
    try {
        UmgCrosshairMover mover(refl, r.found->processEvent, CrosshairWidgets{});
    } catch (const std::runtime_error& e) {
        what = e.what();
    }
    CHECK(what == "/Script/UMG.Widget:SetRenderTranslation is not loaded");
}

// A game with UMG: the mover is built off reflection alone, and refuses a
// player outside the ones it keeps before it touches the game.
void TestCrosshairMoverPlayers() {
    std::cout << "  -- crosshair mover players\n";
    for (const Generation* g : {&kUE427, &kUE5, &kUE418}) {
        World w(*g, true, -1, true);
        const ReflectionDiscoveryResult r = DiscoverReflection(w.arena, w.arena.module, w.arena);
        CHECK(r.found.has_value());
        if (!r.found) continue;
        const Reflection refl(r.found->objects, r.found->layout);
        const auto outOfRange = [](const std::function<void()>& fn) {
            try {
                fn();
            } catch (const std::out_of_range&) {
                return true;
            }
            return false;
        };
        std::string why;
        try {
            UmgCrosshairMover mover(refl, r.found->processEvent, CrosshairWidgets{});
            for (int player : {-1, UmgCrosshairMover::kMaxPlayers}) {
                CHECK(outOfRange([&] { mover.See(player, 0); }));
                CHECK(outOfRange([&] { mover.Release(player); }));
                CHECK(outOfRange([&] { mover.Update(player, 0, FVector{}, FVector{}); }));
            }
            CHECK(!outOfRange([&] { mover.See(UmgCrosshairMover::kMaxPlayers - 1, 0); }));
            CHECK(!outOfRange([&] { mover.Release(0); }));
        } catch (const std::runtime_error& e) {
            why = e.what();
        }
        if (!why.empty()) std::cout << "     " << g->label << ": " << why << "\n";
        CHECK(why.empty());
    }
}

void TestObjectAt() {
    std::cout << "  -- object array slots\n";
    Arena a;
    const std::uintptr_t one = a.Heap(0x30), two = a.Heap(0x30), five = a.Heap(0x30);
    constexpr std::size_t kItem = 0x18;

    // Fixed: ObjObjects points at the items.
    const std::uintptr_t items = a.Heap(kItem * 8), fixedMember = a.Data(16);
    a.Put(fixedMember, items);
    a.Put(items + kItem * 1, one);
    a.Put(items + kItem * 2 + 8, two);
    const ObjectArray fixed{false, fixedMember, kItem, 0, 0x0C};
    CHECK(ObjectAt(a, fixed, 1) == one);
    CHECK(ObjectAt(a, fixed, 0) == 0);
    CHECK(ObjectAt(a, fixed, -1) == 0);
    // The object pointer 8 bytes into its item.
    const ObjectArray packed{false, fixedMember, kItem, 8, 0x0C};
    CHECK(ObjectAt(a, packed, 2) == two);

    // Chunked: ObjObjects points at a table of chunks of items.
    const std::uintptr_t chunk0 = a.Heap(kItem * 4), chunk1 = a.Heap(kItem * 4), table = a.Heap(32), chunkedMember = a.Data(16);
    a.Put(chunkedMember, table);
    a.Put(table, chunk0);
    a.Put(table + 8, chunk1);
    a.Put(chunk1 + kItem * 1, five);
    ObjectArray chunked{true, chunkedMember, kItem, 0, 0x14};
    chunked.chunkElements = 4;
    CHECK(ObjectAt(a, chunked, 5) == five);
    CHECK(ObjectAt(a, chunked, 4) == 0);
    // A slot in a chunk the table does not have.
    CHECK(ObjectAt(a, chunked, 9) == 0);

    // Inline: ObjObjects is the table itself, of chunks of object pointers.
    const std::uintptr_t pointers0 = a.Heap(8 * 4), pointers1 = a.Heap(8 * 4), inlineTable = a.Data(32);
    a.Put(inlineTable, pointers0);
    a.Put(inlineTable + 8, pointers1);
    a.Put(pointers1 + 8 * 1, five);
    ObjectArray inlined{false, inlineTable, 8, 0, 0x10};
    inlined.inlineChunks = true;
    inlined.chunkElements = 4;
    CHECK(ObjectAt(a, inlined, 5) == five);
    CHECK(ObjectAt(a, inlined, 1) == 0);
    // Inline is asked first: the chunked flag changes nothing for it.
    inlined.chunked = true;
    CHECK(ObjectAt(a, inlined, 5) == five);

    // An ObjObjects member that does not read.
    const ObjectArray nowhere{true, 0x1000, kItem, 0, 0x14};
    CHECK(ObjectAt(a, nowhere, 0) == 0);
}

}  // namespace

int RunUnrealReflectionTests() {
    g_failures = 0;
    std::cout << "Unreal reflection:\n";
    for (const Generation* g : {&kUE427, &kUE5, &kUE57, &kCompactArray, &kUE418, &kUE412, &kUE49, &kWonderlands,
                                &kSplitFiction, &kCasePreserving})
        TestGeneration(*g);
    TestObjectHeaderRefusals();
    TestClassDefaultNotYetCreated();
    TestNativeFunctionsNotYetMade();
    TestNameMatches();
    TestEnumValue();
    TestNetFunctionFlags();
    TestScriptFunction();
    TestCrosshairMoverNeedsUmg();
    TestCrosshairMoverPlayers();
    TestTornObjectCount();
    TestObjectAt();
    std::cout << (g_failures ? "  [FAIL] " : "  [PASS] ") << "Unreal reflection, " << g_failures << " failed\n";
    return g_failures;
}
