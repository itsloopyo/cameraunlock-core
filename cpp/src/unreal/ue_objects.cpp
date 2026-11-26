// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <cameraunlock/unreal/ue_objects.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <cameraunlock/memory/word_scan.h>

namespace cameraunlock::unreal {

using memory::ForEachQword;
using memory::Memory;
using memory::ModuleMap;
using memory::PlausiblePtr;

namespace {

constexpr std::int32_t kMinObjects = 0x400;
constexpr std::int32_t kMaxObjects = 0x4000000;
// How many of an array's first slots are checked for objects that name their own slot.
constexpr std::int32_t kProbeSlots = 512;
// The longest chain of outers a path name is made from.
constexpr std::size_t kMaxOuters = 32;
// UObjectBase is 0x28 bytes, or 0x30 with case-preserving names.
constexpr std::size_t kHeaderSpan = 0x30;
// How many objects at the head of the array the class and name are read off.
constexpr std::size_t kHeaderSample = 64;
// The class defaults ObjectFlags is read off: the engine makes one per class at
// startup, so a running game has hundreds.
constexpr std::size_t kMinDefaults = 32;
struct ItemShape {
    std::size_t size, object;
};
constexpr ItemShape kItemShapes[] = {{0x18, 0}, {0x18, 8}, {0x10, 0}, {0x20, 0}, {0x20, 8}};

// Of the first slots, how many hold an object naming its own slot. A real
// array scores nearly every occupied slot; anything else scores almost none.
bool SlotsSelfIndex(const Memory& mem, std::uintptr_t items, ItemShape item, std::int32_t num) {
    // Slots 1 and 2 are always occupied by engine packages and classes; most
    // candidates fail here for the price of four reads.
    for (std::int32_t k = 1; k <= 2; ++k) {
        const std::uintptr_t obj = mem.Ptr(items + k * item.size + item.object);
        if (!PlausiblePtr(obj) || mem.Or0<std::int32_t>(obj + kUObjectIndex) != k) return false;
    }
    const std::int32_t probe = std::min(num, kProbeSlots);
    int occupied = 0, good = 0;
    for (std::int32_t k = 0; k < probe; ++k) {
        const std::uintptr_t obj = mem.Ptr(items + k * item.size + item.object);
        if (!obj) continue;
        ++occupied;
        if (PlausiblePtr(obj) && mem.Or0<std::int32_t>(obj + kUObjectIndex) == k) ++good;
    }
    return good >= probe / 2 && good * 20 >= occupied * 19;
}

// FChunkedFixedUObjectArray's counts, in the orders builds lay them out.
struct ChunkedHeader {
    std::size_t numElements, maxElements, numChunks, maxChunks;
};
constexpr ChunkedHeader kChunkedHeaders[] = {
    {0x14, 0x10, 0x1C, 0x18},  // Objects, PreAllocatedObjects, MaxElements, NumElements, MaxChunks, NumChunks
    {0x08, 0x0C, 0x10, 0x14},  // Objects, NumElements, MaxElements, NumChunks, MaxChunks (Subliminal)
};

std::optional<ObjectArray> ChunkedAt(const Memory& mem, std::uintptr_t at) {
    std::uint8_t raw[0x20];
    if (!mem.Read(at, raw, sizeof(raw))) return std::nullopt;
    const auto i32 = [&](std::size_t off) {
        std::int32_t v;
        std::memcpy(&v, raw + off, 4);
        return v;
    };
    std::uintptr_t objects;
    std::memcpy(&objects, raw, 8);
    if (!PlausiblePtr(objects)) return std::nullopt;
    for (const ChunkedHeader& h : kChunkedHeaders) {
        const std::int32_t num = i32(h.numElements), max = i32(h.maxElements);
        const std::int32_t numChunks = i32(h.numChunks), maxChunks = i32(h.maxChunks);
        if (num < kMinObjects || num > kMaxObjects || max < num) continue;
        const auto chunks = static_cast<std::int32_t>((num + kChunkElements - 1) / kChunkElements);
        if (numChunks != chunks || maxChunks < numChunks) continue;
        const std::uintptr_t chunk0 = mem.Ptr(objects);
        if (!PlausiblePtr(chunk0)) return std::nullopt;
        for (const ItemShape item : kItemShapes)
            if (SlotsSelfIndex(mem, chunk0, item, num)) return ObjectArray{true, at, item.size, item.object, h.numElements};
    }
    return std::nullopt;
}

// TStaticIndirectArrayThreadSafeRead (UE 4.9): a fixed table of chunk
// pointers, each chunk an array of UObject pointers, then the counts. The
// table's length is not assumed: the counts are the first value after the
// pointers and the zeros of the unused chunks, and must agree with the chunks
// seen, in either order.
std::optional<ObjectArray> InlineChunksAt(const Memory& mem, std::uintptr_t at, std::uintptr_t chunk0) {
    constexpr ItemShape kPointers{8, 0};
    if (!SlotsSelfIndex(mem, chunk0, kPointers, kProbeSlots)) return std::nullopt;
    constexpr std::size_t kMaxTable = 0x2000;
    std::vector<std::uint64_t> q(kMaxTable / 8 + 1);
    if (!mem.Read(at, q.data(), q.size() * 8)) return std::nullopt;
    std::size_t chunks = 0;
    while (chunks < q.size() && PlausiblePtr(q[chunks])) ++chunks;
    std::size_t counts = chunks;
    while (counts < q.size() && q[counts] == 0) ++counts;
    if (counts == chunks || counts >= q.size()) return std::nullopt;
    const auto lo = static_cast<std::int32_t>(q[counts] & 0xFFFFFFFF), hi = static_cast<std::int32_t>(q[counts] >> 32);
    const auto n = static_cast<std::int32_t>(chunks);
    for (const std::size_t perChunk : {std::size_t{16384}, std::size_t{65536}}) {
        const auto per = static_cast<std::int32_t>(perChunk);
        for (const bool numFirst : {true, false}) {
            const std::int32_t num = numFirst ? lo : hi, numChunks = numFirst ? hi : lo;
            if (numChunks != n || num < kMinObjects || num > (n * per) || num <= (n - 1) * per) continue;
            ObjectArray a{false, at, kPointers.size, kPointers.object, counts * 8 + (numFirst ? 0 : 4)};
            a.inlineChunks = true;
            a.chunkElements = perChunk;
            return a;
        }
    }
    return std::nullopt;
}

std::optional<ObjectArray> FixedAt(const Memory& mem, std::uintptr_t at) {
    // FFixedUObjectArray: Objects, MaxElements, NumElements.
    constexpr std::size_t kNumElements = 0x0C;
    struct {
        std::uintptr_t objects;
        std::int32_t maxElements, numElements;
    } a{};
    if (!mem.Get(at, a) || !PlausiblePtr(a.objects)) return std::nullopt;
    if (a.numElements < kMinObjects || a.numElements > kMaxObjects) return std::nullopt;
    if (a.maxElements < a.numElements) return std::nullopt;
    for (const ItemShape item : kItemShapes)
        if (SlotsSelfIndex(mem, a.objects, item, a.numElements)) return ObjectArray{false, at, item.size, item.object, kNumElements};
    return std::nullopt;
}

// FNames compare without regard to case, and a build can store a name in a
// case other than the source's (Tiny Tina's Wonderlands keeps "player").
std::string Lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

}  // namespace

std::optional<ObjectArray> FindObjectArray(const Memory& mem, const ModuleMap& module) {
    std::optional<ObjectArray> found;
    ForEachQword(mem, module.writable, [&](std::uintptr_t at, std::uint64_t value) {
        if (found || !PlausiblePtr(value)) return;
        found = ChunkedAt(mem, at);
        if (!found) found = FixedAt(mem, at);
        if (!found) found = InlineChunksAt(mem, at, static_cast<std::uintptr_t>(value));
    });
    return found;
}

std::int32_t ObjectCount(const Memory& mem, const ObjectArray& array) {
    return mem.Or0<std::int32_t>(array.objects + array.numOffset);
}

std::uintptr_t ItemAt(const Memory& mem, const ObjectArray& array, std::int32_t index) {
    if (index < 0) return 0;
    std::size_t slot = static_cast<std::size_t>(index);
    std::uintptr_t items = array.inlineChunks ? array.objects : mem.Ptr(array.objects);
    if (array.inlineChunks || array.chunked) {
        items = mem.Ptr(items + (slot / array.chunkElements) * 8);
        slot %= array.chunkElements;
    }
    return PlausiblePtr(items) ? items + slot * array.itemSize : 0;
}

std::uintptr_t ObjectAt(const Memory& mem, const ObjectArray& array, std::int32_t index) {
    const std::uintptr_t item = ItemAt(mem, array, index);
    if (!item) return 0;
    const std::uintptr_t obj = mem.Ptr(item + array.itemObject);
    return PlausiblePtr(obj) ? obj : 0;
}

std::string Objects::PathName(std::uintptr_t obj) const {
    std::vector<std::uintptr_t> chain;
    for (std::uintptr_t o = obj; o && chain.size() < kMaxOuters; o = Outer(o)) chain.push_back(o);
    std::string path;
    for (std::size_t i = chain.size(); i-- > 0;) {
        const std::uintptr_t o = chain[i];
        if (i + 1 < chain.size()) {
            const std::uintptr_t outer = chain[i + 1];
            const bool outerIsPackage = ClassName(outer) == "Package";
            const std::uintptr_t outerOuter = Outer(outer);
            const bool subobject = !outerIsPackage && outerOuter && ClassName(outerOuter) == "Package";
            path += subobject ? ':' : '.';
        }
        path += Name(o);
    }
    return path;
}

std::vector<std::uintptr_t> Objects::FindPaths(const std::vector<std::string>& paths) const {
    std::vector<std::uintptr_t> found(paths.size(), 0);
    std::unordered_multimap<std::string, std::size_t> byLeaf;
    std::vector<std::string> wanted;
    // An object's name is its name entry's string, with "_<number - 1>" after it where the
    // name carries a number (FNameToString). A wanted leaf's entry is then the leaf itself or
    // what stands before its last "_<digits>".
    std::unordered_set<std::string> entries;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        wanted.push_back(Lower(paths[i]));
        const std::size_t cut = wanted[i].find_last_of(".:");
        const std::string leaf = cut == std::string::npos ? wanted[i] : wanted[i].substr(cut + 1);
        byLeaf.emplace(leaf, i);
        entries.insert(leaf);
        const std::size_t number = leaf.find_last_of('_');
        if (number != std::string::npos && number + 1 < leaf.size() &&
            leaf.find_first_not_of("0123456789", number + 1) == std::string::npos)
            entries.insert(leaf.substr(0, number));
    }
    // Each name index is resolved to a string once; after that an object costs a
    // dword read and a lookup.
    std::unordered_map<std::uint32_t, std::string> leafOf;
    std::size_t remaining = paths.size();
    ForEach([&](std::uintptr_t obj) {
        std::uint32_t name[2]{};
        if (!mem->Read(obj + header.name, name, sizeof(name))) return false;
        auto cached = leafOf.find(name[0]);
        if (cached == leafOf.end()) {
            std::string s = Lower(NameString(*mem, names, name[0]));
            cached = leafOf.emplace(name[0], entries.count(s) ? std::move(s) : std::string{}).first;
        }
        if (cached->second.empty()) return false;
        const auto [first, last] = byLeaf.equal_range(Lower(Name(obj)));
        if (first == last) return false;
        const std::string path = Lower(PathName(obj));
        for (auto it = first; it != last; ++it) {
            if (found[it->second] || wanted[it->second] != path) continue;
            found[it->second] = obj;
            --remaining;
        }
        return remaining == 0;
    });
    return found;
}

std::optional<ObjectHeader> FindObjectHeader(const Objects& objects, std::string& why, bool& notLoaded) {
    const Memory& mem = *objects.mem;
    const std::size_t fname = FNameSize(objects.names);
    std::vector<std::uintptr_t> sample;
    const std::int32_t count = ObjectCount(mem, objects.array);
    for (std::int32_t i = 0; i < count && sample.size() < kHeaderSample; ++i)
        if (const std::uintptr_t obj = ObjectAt(mem, objects.array, i)) sample.push_back(obj);
    if (sample.size() < kHeaderSample) {
        why = "the object array holds fewer than " + std::to_string(kHeaderSample) + " objects to read the header off";
        notLoaded = true;
        return std::nullopt;
    }
    // Spans of the header a field may take: clear of the vtable and of InternalIndex.
    const auto clear = [](std::size_t at, std::size_t size) {
        return at >= 8 && at + size <= kHeaderSpan && (at + size <= kUObjectIndex || at >= kUObjectIndex + 4);
    };
    const auto overlaps = [](std::size_t a, std::size_t aSize, std::size_t b, std::size_t bSize) {
        return a < b + bSize && b < a + aSize;
    };

    std::vector<ObjectHeader> found;
    std::uintptr_t meta = 0;
    for (std::size_t cls = 8; cls + 8 <= kHeaderSpan; cls += 8) {
        if (!clear(cls, 8)) continue;
        std::uintptr_t m = 0;
        bool same = true;
        for (const std::uintptr_t obj : sample) {
            const std::uintptr_t c = mem.Ptr(mem.Ptr(obj + cls) + cls);
            if (!PlausiblePtr(c) || mem.Ptr(c + cls) != c || (m && c != m)) {
                same = false;
                break;
            }
            m = c;
        }
        if (!same) continue;
        for (std::size_t name = 8; name + fname <= kHeaderSpan; name += 4) {
            if (!clear(name, fname) || overlaps(name, fname, cls, 8)) continue;
            if (FNameToString(mem, objects.names, m + name) != "Class") continue;
            found.push_back({cls, name});
            meta = m;
        }
    }
    if (found.size() != 1) {
        why = std::to_string(found.size()) + " pairs of UObject offsets hold a class whose class is itself and named "
              "Class, behind every one of the first " + std::to_string(kHeaderSample) + " objects, not one";
        return std::nullopt;
    }
    ObjectHeader h = found[0];

    std::vector<std::size_t> spots;
    for (std::size_t at = 8; at + 4 <= kHeaderSpan; at += 4)
        if (clear(at, 4) && !overlaps(at, 4, h.cls, 8) && !overlaps(at, 4, h.name, fname)) spots.push_back(at);
    std::vector<std::uint32_t> allDefaults(spots.size(), ~0u), anyOther(spots.size(), 0), anyClass(spots.size(), 0);
    std::size_t defaults = 0;
    // Whether a name entry is a class default's, "Default__<class>", by its
    // index; nothing for a name that cannot be read (a wide one), whose object
    // is left out.
    std::unordered_map<std::uint32_t, std::optional<bool>> isDefault;
    objects.ForEach([&](std::uintptr_t obj) {
        std::uint8_t raw[kHeaderSpan];
        if (!mem.Read(obj, raw, sizeof raw)) return false;
        std::uint32_t nameIndex;
        std::memcpy(&nameIndex, raw + h.name, 4);
        auto known = isDefault.find(nameIndex);
        if (known == isDefault.end()) {
            const std::string name = NameString(mem, objects.names, nameIndex);
            known = isDefault.emplace(nameIndex, name.empty() ? std::nullopt : std::optional<bool>(name.rfind("Default__", 0) == 0)).first;
        }
        if (!known->second) return false;
        std::uint64_t cls;
        std::memcpy(&cls, raw + h.cls, 8);
        for (std::size_t i = 0; i < spots.size(); ++i) {
            std::uint32_t word;
            std::memcpy(&word, raw + spots[i], 4);
            if (*known->second) allDefaults[i] &= word;
            else anyOther[i] |= word;
            if (cls == meta) anyClass[i] |= word;
        }
        if (*known->second) ++defaults;
        return false;
    });
    if (defaults < kMinDefaults) {
        why = "the engine has made " + std::to_string(defaults) + " class defaults, too few to read ObjectFlags off";
        notLoaded = true;
        return std::nullopt;
    }
    std::vector<std::pair<std::size_t, std::uint32_t>> cdo;
    for (std::size_t i = 0; i < spots.size(); ++i)
        for (std::uint32_t bit = 1; bit; bit <<= 1)
            if (allDefaults[i] & ~anyOther[i] & bit) cdo.emplace_back(i, bit);
    if (cdo.size() != 1) {
        why = std::to_string(cdo.size()) + " header bits are set on all " + std::to_string(defaults) +
              " class defaults and on no other object, not one (RF_ClassDefaultObject)";
        return std::nullopt;
    }
    const std::size_t at = cdo[0].first;
    const std::uint32_t archetype = allDefaults[at] & ~cdo[0].second & ~anyClass[at];
    if (archetype == 0 || (archetype & (archetype - 1)) != 0) {
        char bits[16];
        std::snprintf(bits, sizeof bits, "0x%x", archetype);
        why = std::string("the bits every class default carries past RF_ClassDefaultObject and no native class does are ") +
              bits + ", not one (RF_ArchetypeObject)";
        return std::nullopt;
    }
    h.flags = spots[at];
    h.classDefault = cdo[0].second;
    h.archetype = archetype;
    return h;
}

}  // namespace cameraunlock::unreal
