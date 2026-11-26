// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <cameraunlock/memory/pe_image.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>

namespace cameraunlock::memory::pe {
namespace {

constexpr std::uint16_t kMz = 0x5A4D;
constexpr std::uint32_t kPe = 0x4550;
constexpr std::uint16_t kMachineI386 = 0x014C;
constexpr std::uint16_t kMachineAmd64 = 0x8664;
constexpr std::uint16_t kMagic32 = 0x10B;
constexpr std::uint16_t kMagic64 = 0x20B;
constexpr unsigned kDirectoryImport = 1;
constexpr unsigned kDirectoryException = 3;
constexpr std::uint8_t kUnwindChained = 0x4;

template <class T>
T ReadAt(const std::uint8_t* base, std::size_t span, std::uint64_t at) {
    if (at > span || sizeof(T) > span - at) throw std::runtime_error("the image's headers run past it");
    T v;
    std::memcpy(&v, base + at, sizeof v);
    return v;
}

}  // namespace

Image::Image(const std::uint8_t* at, std::uint64_t pointerBase) : base_(at), pointerBase_(pointerBase) {
    ReadHeaders(std::numeric_limits<std::size_t>::max());
}

Image::Image(const std::uint8_t* data, std::size_t size, std::uint64_t pointerBase) : base_(data), pointerBase_(pointerBase) {
    ReadHeaders(size);
    if (size_ > size)
        throw std::runtime_error("the image's SizeOfImage (" + std::to_string(size_) + ") is larger than the " + std::to_string(size) +
                                 " bytes given");
}

void Image::ReadHeaders(std::size_t span) {
    if (ReadAt<std::uint16_t>(base_, span, 0) != kMz) throw std::runtime_error("the image has no MZ header");
    const std::uint64_t nt = ReadAt<std::uint32_t>(base_, span, 0x3C);
    if (ReadAt<std::uint32_t>(base_, span, nt) != kPe) throw std::runtime_error("the image has no PE header");
    const auto machine = ReadAt<std::uint16_t>(base_, span, nt + 4);
    const std::uint64_t opt = nt + 24;
    const auto magic = ReadAt<std::uint16_t>(base_, span, opt);
    if (machine == kMachineI386 && magic == kMagic32) is64_ = false;
    else if (machine == kMachineAmd64 && magic == kMagic64) is64_ = true;
    else throw std::runtime_error("the image is neither a 32-bit x86 nor a 64-bit x64 PE");

    size_ = ReadAt<std::uint32_t>(base_, span, opt + 56);
    headerImageBase_ = is64_ ? ReadAt<std::uint64_t>(base_, span, opt + 24) : ReadAt<std::uint32_t>(base_, span, opt + 28);
    const std::uint32_t directories = ReadAt<std::uint32_t>(base_, span, opt + (is64_ ? 108 : 92));
    const std::uint64_t directory = opt + (is64_ ? 112 : 96);
    const auto dir = [&](unsigned index) -> std::pair<std::uint32_t, std::uint32_t> {
        if (index >= directories) return {0, 0};
        return {ReadAt<std::uint32_t>(base_, span, directory + 8ull * index), ReadAt<std::uint32_t>(base_, span, directory + 8ull * index + 4)};
    };

    const auto count = ReadAt<std::uint16_t>(base_, span, nt + 6);
    const std::uint64_t first = opt + ReadAt<std::uint16_t>(base_, span, nt + 20);
    for (unsigned i = 0; i < count; ++i) {
        const std::uint64_t s = first + 40ull * i;
        Section section;
        ReadAt<std::uint64_t>(base_, span, s);  // the name's eight bytes are inside
        const auto* name = reinterpret_cast<const char*>(base_ + s);
        const auto* nul = static_cast<const char*>(std::memchr(name, 0, 8));
        section.name.assign(name, nul ? static_cast<std::size_t>(nul - name) : 8);
        section.size = ReadAt<std::uint32_t>(base_, span, s + 8);
        section.rva = ReadAt<std::uint32_t>(base_, span, s + 12);
        section.characteristics = ReadAt<std::uint32_t>(base_, span, s + 36);
        // The scans read a section's bytes unchecked.
        if (std::uint64_t{section.rva} + section.size > size_)
            throw std::runtime_error("the image's " + section.name + " section runs past its SizeOfImage");
        sections_.push_back(std::move(section));
    }

    const auto imports = dir(kDirectoryImport);
    if (imports.first && std::uint64_t{imports.first} + imports.second <= size_) imports_ = {"", imports.first, imports.second, 0};

    const auto exceptions = dir(kDirectoryException);
    if (is64_ && exceptions.first && exceptions.second >= 12) {
        if (std::uint64_t{exceptions.first} + exceptions.second > size_) throw std::runtime_error("the image's exception directory runs past it");
        functions_.reserve(exceptions.second / 12);
        for (std::uint32_t at = exceptions.first; at + 12 <= exceptions.first + exceptions.second; at += 12) {
            functions_.push_back({ReadAt<std::uint32_t>(base_, size_, at), ReadAt<std::uint32_t>(base_, size_, at + 4),
                                  ReadAt<std::uint32_t>(base_, size_, at + 8)});
        }
        std::sort(functions_.begin(), functions_.end(), [](const Function& a, const Function& b) { return a.begin < b.begin; });
    }
}

const Section* Image::SectionNamed(const std::string& name) const {
    for (const Section& s : sections_)
        if (s.name == name) return &s;
    return nullptr;
}

const Section& Image::Required(const char* name) const {
    const Section* s = SectionNamed(name);
    if (!s || s->size == 0) throw std::runtime_error(std::string("the image has no ") + name + " section");
    return *s;
}

std::uint8_t Image::U8(std::uint32_t rva) const {
    if (rva >= size_) throw std::out_of_range("a read past the image");
    return base_[rva];
}

std::uint16_t Image::U16(std::uint32_t rva) const {
    if (std::uint64_t{rva} + 2 > size_) throw std::out_of_range("a read past the image");
    std::uint16_t v;
    std::memcpy(&v, base_ + rva, 2);
    return v;
}

std::uint32_t Image::U32(std::uint32_t rva) const {
    if (std::uint64_t{rva} + 4 > size_) throw std::out_of_range("a read past the image");
    std::uint32_t v;
    std::memcpy(&v, base_ + rva, 4);
    return v;
}

std::uint64_t Image::U64(std::uint32_t rva) const {
    if (std::uint64_t{rva} + 8 > size_) throw std::out_of_range("a read past the image");
    std::uint64_t v;
    std::memcpy(&v, base_ + rva, 8);
    return v;
}

std::optional<std::uint32_t> Image::RvaOf(std::uint64_t pointer) const {
    if (pointer < pointerBase_ || pointer - pointerBase_ >= size_) return std::nullopt;
    return static_cast<std::uint32_t>(pointer - pointerBase_);
}

const Image::Function* Image::EntryAt(std::uint32_t rva) const {
    auto it = std::upper_bound(functions_.begin(), functions_.end(), rva, [](std::uint32_t r, const Function& f) { return r < f.begin; });
    if (it == functions_.begin()) return nullptr;
    --it;
    return rva < it->end ? &*it : nullptr;
}

std::pair<std::uint32_t, std::uint32_t> Image::FunctionAt(std::uint32_t rva) const {
    if (functions_.empty()) throw std::runtime_error("the image has no exception directory");
    const Function* f = EntryAt(rva);
    return f ? std::pair{f->begin, f->end} : std::pair<std::uint32_t, std::uint32_t>{0, 0};
}

std::uint32_t Image::RootOfEntry(const Function* e) const {
    // A chained entry's UNWIND_INFO ends with the RUNTIME_FUNCTION of its parent.
    for (int depth = 0; e && depth < 32; ++depth) {
        const std::uint32_t info = e->unwind & ~1u;
        if (!((U8(info) >> 3) & kUnwindChained)) return e->begin;
        const std::uint32_t codes = U8(info + 2);
        e = EntryAt(U32(info + 4 + ((codes + 1u) & ~1u) * 2));
    }
    return 0;
}

std::uint32_t Image::RootOf(std::uint32_t rva) const {
    if (functions_.empty()) throw std::runtime_error("the image has no exception directory");
    return RootOfEntry(EntryAt(rva));
}

std::vector<std::pair<std::uint32_t, std::uint32_t>> Image::Parts(std::uint32_t root) const {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> out;
    for (const Function& f : functions_)
        if (RootOfEntry(&f) == root) out.emplace_back(f.begin, f.end);
    return out;
}

Pattern::Pattern(const char* text) {
    for (const char* p = text; *p;) {
        if (*p == ' ') {
            ++p;
            continue;
        }
        if (!p[1]) throw std::invalid_argument(std::string("a pattern ends in half a byte: ") + text);
        if (p[0] == '?' && p[1] == '?') bytes_.push_back(-1);
        else {
            const char pair[3] = {p[0], p[1], 0};
            char* end = nullptr;
            const unsigned long v = std::strtoul(pair, &end, 16);
            if (end != pair + 2) throw std::invalid_argument(std::string("a pattern has a byte that is not hex: ") + text);
            bytes_.push_back(static_cast<std::int16_t>(v));
        }
        p += 2;
    }
    if (bytes_.empty() || bytes_[0] < 0) throw std::invalid_argument(std::string("a pattern must start with a known byte: ") + text);
}

bool Pattern::At(const std::uint8_t* p) const {
    for (std::size_t i = 0; i < bytes_.size(); ++i)
        if (bytes_[i] >= 0 && p[i] != static_cast<std::uint8_t>(bytes_[i])) return false;
    return true;
}

namespace {

template <class Each>
void Scan(const Image& image, std::uint32_t from, std::uint32_t span, const Pattern& pattern, Each each) {
    const std::uint64_t end = (std::min<std::uint64_t>)(std::uint64_t{from} + span, image.SizeOfImage());
    if (end < pattern.Size()) return;
    const std::uint8_t* base = image.At(0);
    for (std::uint64_t at = from; at + pattern.Size() <= end; ++at)
        if (pattern.At(base + at) && !each(static_cast<std::uint32_t>(at))) return;
}

// The NUL-terminated name at `rva`, or "" when it runs off the image.
std::string NameAt(const Image& image, std::uint32_t rva) {
    std::string out;
    for (std::uint32_t at = rva; at < image.SizeOfImage(); ++at) {
        const char c = static_cast<char>(*image.At(at));
        if (!c) return out;
        out += c;
    }
    return {};
}

bool SameIgnoringCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
}

}  // namespace

std::optional<std::uint32_t> Find(const Image& image, std::uint32_t from, std::uint32_t span, const Pattern& pattern) {
    std::optional<std::uint32_t> found;
    Scan(image, from, span, pattern, [&](std::uint32_t at) {
        found = at;
        return false;
    });
    return found;
}

std::vector<std::uint32_t> FindAll(const Image& image, std::uint32_t from, std::uint32_t span, const Pattern& pattern) {
    std::vector<std::uint32_t> out;
    Scan(image, from, span, pattern, [&](std::uint32_t at) {
        out.push_back(at);
        return true;
    });
    return out;
}

std::optional<std::uint32_t> FindLast(const Image& image, std::uint32_t from, std::uint32_t span, const Pattern& pattern) {
    const std::vector<std::uint32_t> all = FindAll(image, from, span, pattern);
    if (all.empty()) return std::nullopt;
    return all.back();
}

std::uint32_t Rel32(const Image& image, std::uint32_t operand) {
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(operand) + 4 + image.I32(operand));
}

std::uint32_t Through(const Image& image, std::uint32_t function) {
    for (int hops = 0; hops < 8 && image.U8(function) == 0xE9; ++hops) function = Rel32(image, function + 1);
    return function;
}

std::vector<std::uint32_t> CallsTo(const Image& image, std::uint32_t target) {
    std::vector<std::uint32_t> out;
    const Section& t = image.Text();
    const std::uint8_t* base = image.At(0);
    for (std::uint32_t at = t.rva; at + 5 <= t.rva + t.size; ++at)
        if (base[at] == 0xE8 && Rel32(image, at + 1) == target) out.push_back(at);
    return out;
}

std::vector<std::uint32_t> CallTargetsIn(const Image& image, std::uint32_t from, std::uint32_t span) {
    std::set<std::uint32_t> out;
    const Section& t = image.Text();
    const std::uint8_t* base = image.At(0);
    for (std::uint32_t at = t.rva; at + 5 <= t.rva + t.size; ++at) {
        if (base[at] != 0xE8) continue;
        const std::uint32_t target = Rel32(image, at + 1);
        if (target >= from && target - from < span) out.insert(target);
    }
    return {out.begin(), out.end()};
}

std::vector<std::uint32_t> Pushes(const Image& image, std::uint32_t rva) {
    if (image.Is64()) throw std::invalid_argument("Pushes reads push imm32 of an absolute address, which only an x86 image has");
    std::vector<std::uint32_t> out;
    const auto value = static_cast<std::uint32_t>(image.PointerOf(rva));
    const Section& t = image.Text();
    const std::uint8_t* base = image.At(0);
    for (std::uint32_t at = t.rva; at + 5 <= t.rva + t.size; ++at) {
        if (base[at] != 0x68) continue;
        std::uint32_t v;
        std::memcpy(&v, base + at + 1, 4);
        if (v == value) out.push_back(at);
    }
    return out;
}

std::optional<std::uint32_t> UniqueString(const Image& image, const std::string& text, StringStart start) {
    const Section& r = image.Rdata();
    const std::uint8_t* base = image.At(0);
    const std::size_t n = text.size() + 1;
    const bool aligned = start == StringStart::AfterNulOrAligned4;
    std::optional<std::uint32_t> found;
    for (std::uint32_t at = r.rva + 1; at + n <= r.rva + r.size; ++at) {
        if ((base[at - 1] != 0 && !(aligned && at % 4 == 0)) || std::memcmp(base + at, text.c_str(), n) != 0) continue;
        if (found) return std::nullopt;
        found = at;
    }
    return found;
}

std::optional<std::uint32_t> ImportSlot(const Image& image, const std::string& dll, const std::string& function) {
    const Section& d = image.Imports();
    if (!d.size) return std::nullopt;
    const std::uint32_t width = image.PointerSize();
    const std::uint64_t ordinal = image.Is64() ? 0x8000000000000000ull : 0x80000000ull;
    // IMAGE_IMPORT_DESCRIPTOR: OriginalFirstThunk, TimeDateStamp, ForwarderChain, Name, FirstThunk.
    for (std::uint32_t at = d.rva; at + 20 <= image.SizeOfImage(); at += 20) {
        const std::uint32_t lookup = image.U32(at), name = image.U32(at + 12), slots = image.U32(at + 16);
        if (!name) break;
        if (!SameIgnoringCase(NameAt(image, name), dll)) continue;
        // The loader overwrites the address table; the lookup table keeps the names.
        const std::uint32_t names = lookup ? lookup : slots;
        for (std::uint32_t i = 0;; ++i) {
            const std::uint64_t entry = image.Pointer(names + width * i);
            if (!entry) break;
            if (entry & ordinal) continue;
            if (NameAt(image, static_cast<std::uint32_t>(entry) + 2) == function) return slots + width * i;
        }
    }
    return std::nullopt;
}

std::uint32_t FunctionStart(const Image& image, std::uint32_t inside, std::uint32_t reach) {
    const std::uint8_t* base = image.At(0);
    const std::uint32_t text = image.Text().rva;
    for (std::uint32_t at = inside; at > text + 4 && inside - at < reach; --at) {
        if (base[at - 1] != 0xCC || base[at] == 0xCC) continue;
        std::uint32_t run = at - 1;
        while (run > text + 3 && base[run - 1] == 0xCC) --run;
        // One int3 is also a byte of some immediate now and then; a run of
        // them, or one after a ret, is padding.
        if (at - run >= 2 || base[run - 1] == 0xC3 || base[run - 3] == 0xC2) return at;
    }
    throw std::runtime_error("no function start within " + std::to_string(reach) + " bytes before a call site");
}

}  // namespace cameraunlock::memory::pe
