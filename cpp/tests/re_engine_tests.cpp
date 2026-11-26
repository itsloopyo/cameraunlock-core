// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// Tests for the RE Engine runtime (cameraunlock/re_engine/): its choices from
// what it reads in a game, run over hand-made data, and its reads of the
// executable it runs in, run over this test executable.

#include <iostream>

#ifdef CAMERAUNLOCK_TESTS_RE_ENGINE

#include <cameraunlock/re_engine/discovery.h>
#include <cameraunlock/re_engine/exe.h>

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace re = cameraunlock::re_engine;
namespace d = cameraunlock::re_engine::discovery;

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    if (ok) {
        std::cout << "  [PASS] " << what << "\n";
    } else {
        std::cout << "  [FAIL] " << what << "\n";
        ++g_failures;
    }
}

// The message `f` throws, or empty when it does not.
template <class F>
std::string Thrown(F f) {
    try {
        f();
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    return {};
}

bool Says(const std::string& message, const std::string& needle) { return message.find(needle) != std::string::npos; }

std::vector<d::Load> Loads(std::uintptr_t global, std::uintptr_t call, int n) { return std::vector<d::Load>(n, {global, call}); }

void Append(std::vector<d::Load>& to, const std::vector<d::Load>& more) { to.insert(to.end(), more.begin(), more.end()); }

void TestPickVm() {
    std::vector<d::Load> loads = Loads(0xA000, 0xC000, 40);
    Append(loads, Loads(0xB000, 0xD000, 3));
    const d::Vm vm = d::PickVm(loads);
    Check(vm.global == 0xA000 && vm.getThreadContext == 0xC000, "the VM is the global loaded most often, and the call its loads make");

    // One stray call target among the first hits does not decide the function.
    loads = Loads(0xA000, 0xE000, 1);
    Append(loads, Loads(0xA000, 0xC000, 60));
    Check(d::PickVm(loads).getThreadContext == 0xC000, "the thread context function is the one nearly every load calls");

    Check(Says(Thrown([] { d::PickVm(Loads(0xA000, 0xC000, 10)); }), "more than ten"), "ten loads are too few to name a VM");
    Check(Says(Thrown([] { d::PickVm({}); }), "0 loads seen"), "no loads at all is said as such");

    loads = Loads(0xA000, 0xC000, 30);
    Append(loads, Loads(0xB000, 0xD000, 15));
    Check(Says(Thrown([&] { d::PickVm(loads); }), "too close to tell"), "a global loaded only twice as often as another is refused");
    loads = Loads(0xA000, 0xC000, 31);
    Append(loads, Loads(0xB000, 0xD000, 15));
    Check(d::PickVm(loads).global == 0xA000, "a global loaded more than twice as often as any other is the VM");

    loads = Loads(0xA000, 0xC000, 18);
    Append(loads, Loads(0xA000, 0xD000, 2));
    Check(d::PickVm(loads).getThreadContext == 0xC000, "nine in ten loads calling one function name it");
    loads = Loads(0xA000, 0xC000, 17);
    Append(loads, Loads(0xA000, 0xD000, 3));
    Check(Says(Thrown([&] { d::PickVm(loads); }), "the most called by 17"), "fewer than nine in ten loads calling one function are refused");

    loads = Loads(0xA000, 0xC000, 20);
    Append(loads, Loads(0xA000, 0xD000, 20));
    Check(Says(Thrown([&] { d::PickVm(loads); }), "call 2 functions"), "a VM whose loads call two functions is refused");
}

// via.Application as the tests lay it out: offsets that hold text, and offsets
// whose pointer points at code.
struct FakeApp {
    std::size_t size = 0x4000;
    std::map<std::size_t, std::string> text;
    std::set<std::size_t> code;

    d::ObjectReader Reader() const {
        return {[this](std::size_t at) {
                    const auto it = text.find(at);
                    return it == text.end() ? std::string() : it->second;
                },
                [this](std::size_t at) { return code.count(at) != 0; }};
    }
};

constexpr std::size_t kFirst = 0x828;
constexpr std::size_t kStride = 0xC8;
const std::vector<std::string> kStages = {"Initialize", "WaitRendering", "Update", "BeginRendering", "EndRendering", "Finalize"};

// Entries of 0xC8 bytes from +0x818: the object the stage is called with at +0,
// its function at +8, its description at +0x10.
FakeApp Entries() {
    FakeApp app;
    for (std::size_t i = 0; i < kStages.size(); ++i) {
        const std::size_t description = kFirst + i * kStride;
        app.text[description] = kStages[i];
        app.code.insert(description - 8);
    }
    // A pointer to code in some entries but not all, and text outside the list.
    app.code.insert(kFirst + 0x30);
    app.code.insert(kFirst + kStride + 0x30);
    app.text[0x100] = "via.Application";
    app.text[kFirst + kStages.size() * kStride + 0x20] = "Stray";
    return app;
}

void TestFindEntryList() {
    const FakeApp app = Entries();
    const auto list = d::FindEntryList(app.size, app.Reader());
    Check(list && list->first == kFirst && list->stride == kStride && list->count == kStages.size() && list->func == -8,
          "the stage list is found where the rendering stages' descriptions lie, with its function 8 bytes before each");

    FakeApp early = Entries();
    early.text.erase(kFirst + 4 * kStride);
    Check(!d::FindEntryList(early.size, early.Reader()), "a list that names no EndRendering yet is not there yet");

    FakeApp twice = Entries();
    twice.text[0x200] = "BeginRendering";
    Check(Says(Thrown([&] { d::FindEntryList(twice.size, twice.Reader()); }), "names BeginRendering 2 times"),
          "a rendering stage named twice is refused");

    FakeApp both = Entries();
    for (std::size_t i = 0; i < kStages.size(); ++i) both.code.insert(kFirst + i * kStride + 0x18);
    Check(Says(Thrown([&] { d::FindEntryList(both.size, both.Reader()); }), "2 places beside the description point at code"),
          "two places that point at code in every entry are refused");

    FakeApp none = Entries();
    none.code.clear();
    Check(Says(Thrown([&] { d::FindEntryList(none.size, none.Reader()); }), "0 places"), "a list with no function pointer is refused");

    FakeApp gap = Entries();
    gap.code.erase(kFirst + 2 * kStride - 8);
    Check(Says(Thrown([&] { d::FindEntryList(gap.size, gap.Reader()); }), "0 places"), "an entry whose function is not code is refused");

    // Text one stride past the last entry runs the list on, and the entry it
    // makes has no function, so the list is refused rather than cut short.
    FakeApp past = Entries();
    past.text[kFirst + kStages.size() * kStride] = "Trailing";
    Check(Says(Thrown([&] { d::FindEntryList(past.size, past.Reader()); }), "7 stages"),
          "text one stride past the list with no function beside it is refused");

    FakeApp atEnd;
    atEnd.size = kFirst + (kStages.size() - 1) * kStride + 8;
    for (std::size_t i = 0; i < kStages.size(); ++i) {
        atEnd.text[kFirst + i * kStride] = kStages[i];
        atEnd.code.insert(kFirst + i * kStride - 8);
    }
    const auto ending = d::FindEntryList(atEnd.size, atEnd.Reader());
    Check(ending && ending->count == kStages.size() && ending->func == -8, "a list that runs to the end of the object is read whole");

    FakeApp broken = Entries();
    broken.text.erase(kFirst + 2 * kStride);
    Check(Says(Thrown([&] { d::FindEntryList(broken.size, broken.Reader()); }), "no stride"),
          "rendering stages not on one unbroken run of descriptions are refused");

    FakeApp other;
    for (std::size_t i = 0; i < kStages.size(); ++i) {
        const std::size_t description = 0x1000 + i * 0xD0;
        other.text[description] = kStages[i];
        other.code.insert(description + 0x10);
    }
    const auto read = d::FindEntryList(other.size, other.Reader());
    Check(read && read->first == 0x1000 && read->stride == 0xD0 && read->count == kStages.size() && read->func == 0x10,
          "another layout, 0xD0 apart with the function after the description, is read as it lies");

    // A second text half way along every entry makes a run at half the stride;
    // only every other one of its entries has code beside it, so it is refused,
    // not taken as entries half the size.
    FakeApp halves = other;
    for (std::size_t i = 0; i < kStages.size(); ++i) halves.text[0x1000 + i * 0xD0 + 0x68] = "note";
    Check(Says(Thrown([&] { d::FindEntryList(halves.size, halves.Reader()); }), "0x68 apart: 0 places"),
          "a text half way along each entry is refused rather than read as entries half the size");
}

struct Matrix {
    float m[16];
};

Matrix Moved(float by) {
    Matrix out;
    for (int i = 0; i < 16; ++i) out.m[i] = by + static_cast<float>(i);
    return out;
}

std::vector<std::uint8_t> Holding(std::initializer_list<std::pair<std::size_t, Matrix>> at) {
    std::vector<std::uint8_t> bytes(0x100, 0);
    for (const auto& [offset, matrix] : at) std::memcpy(bytes.data() + offset, matrix.m, sizeof(matrix.m));
    return bytes;
}

void TestMatrixPlace() {
    const Matrix a = Moved(0.5f), b = Moved(2.5f);

    d::MatrixPlace one;
    const auto first = Holding({{0x80, a}});
    Check(one.See(first.data(), first.size(), a.m) == std::optional<std::size_t>(0x80), "a matrix in one place is found there");

    // The world matrix and a copy of it agree until the camera moves and only
    // the world matrix follows.
    d::MatrixPlace two;
    const auto still = Holding({{0x20, a}, {0x80, a}});
    Check(!two.See(still.data(), still.size(), a.m) && two.Left() == 2, "a matrix in two places waits for another frame");
    const auto moved = Holding({{0x20, a}, {0x80, b}});
    Check(two.See(moved.data(), moved.size(), b.m) == std::optional<std::size_t>(0x80), "the next frame tells the two apart");

    // A place that held the matrix once and then not is out for good, even when
    // a later frame holds the matrix there again.
    d::MatrixPlace back;
    const auto both = Holding({{0x20, a}, {0x80, a}});
    back.See(both.data(), both.size(), a.m);
    const auto onlyAt80 = Holding({{0x80, b}});
    back.See(onlyAt80.data(), onlyAt80.size(), b.m);
    const auto onlyAt20 = Holding({{0x20, a}});
    Check(Says(Thrown([&] { back.See(onlyAt20.data(), onlyAt20.size(), a.m); }), "it was at +0x80 until then"),
          "a matrix that leaves the one place it held is refused");

    d::MatrixPlace none;
    const auto empty = Holding({});
    Check(Says(Thrown([&] { none.See(empty.data(), empty.size(), a.m); }), "on frame 1"), "a matrix in no place is refused");

    d::MatrixPlace end;
    const auto last = Holding({{0x100 - sizeof(a.m), a}});
    Check(end.See(last.data(), last.size(), a.m) == std::optional<std::size_t>(0x100 - sizeof(a.m)),
          "a matrix at the very end of the object is found");
}

// Hand-assembled code, padded with int3 to the 0x40 bytes the layer walks and
// the read-ahead past them.
std::vector<std::uint8_t> Code(std::vector<std::uint8_t> bytes) {
    bytes.resize(0x40 + d::kReadAhead, 0xCC);
    return bytes;
}

std::vector<std::uintptr_t> ReadsOf(const std::vector<std::uint8_t>& code, std::uintptr_t at) {
    return d::PointerReads(code.data(), 0x40, at);
}

void TestPointerReads() {
    constexpr std::uintptr_t at = 0x140001000;
    // mov rax, [rip+0x1000]; movss xmm0, [rax+0x348]; ret
    const auto getter = Code({0x48, 0x8B, 0x05, 0x00, 0x10, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x80, 0x48, 0x03, 0x00, 0x00, 0xC3});
    Check(ReadsOf(getter, at) == std::vector<std::uintptr_t>{at + 7 + 0x1000}, "a getter's mov from a global is read");
    // cmp qword [rip+0x2000], 0; setne al; ret
    const auto has = Code({0x48, 0x83, 0x3D, 0x00, 0x20, 0x00, 0x00, 0x00, 0x0F, 0x95, 0xC0, 0xC3});
    Check(ReadsOf(has, at) == std::vector<std::uintptr_t>{at + 8 + 0x2000}, "a compare of a global with zero is read, from past its immediate");
    // push rbx; sub rsp, 0x20; mov rbx, [rip-0x10]; call +0; ret
    const auto framed = Code({0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x1D, 0xF0, 0xFF, 0xFF, 0xFF, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xC3});
    Check(ReadsOf(framed, at) == std::vector<std::uintptr_t>{at + 13 - 0x10}, "a load after a prologue is read, and a call does not end the walk");
    // mov eax, [rip+0x1000]; ret
    Check(ReadsOf(Code({0x8B, 0x05, 0x00, 0x10, 0x00, 0x00, 0xC3}), at).empty(), "a 32-bit load is not a pointer read");
    // jmp +0; mov rax, [rip+0x1000]
    Check(ReadsOf(Code({0xE9, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x00, 0x10, 0x00, 0x00}), at).empty(),
          "nothing past a jump is read");
    // sub rsp, 0x28; mov rax, [rip+0x100]; xor rax, rsp; mov rcx, [rip+0x200]; ret
    const auto guarded = Code({0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x05, 0x00, 0x01, 0x00, 0x00, 0x48, 0x33, 0xC4,
                               0x48, 0x8B, 0x0D, 0x00, 0x02, 0x00, 0x00, 0xC3});
    Check(ReadsOf(guarded, at) == std::vector<std::uintptr_t>{at + 21 + 0x200}, "the /GS cookie is left out");
    // 0x3E bytes of nop, then mov rax, [rip+0x1000] starting at 0x3E and running past the walk
    std::vector<std::uint8_t> late(0x3E, 0x90);
    const std::uint8_t load[] = {0x48, 0x8B, 0x05, 0x00, 0x10, 0x00, 0x00};
    late.insert(late.end(), std::begin(load), std::end(load));
    Check(ReadsOf(Code(late), at) == std::vector<std::uintptr_t>{at + 0x45 + 0x1000}, "an instruction that starts in the walk is read whole");
    // mov rax, [rcx+0x10]; ret
    Check(ReadsOf(Code({0x48, 0x8B, 0x41, 0x10, 0xC3}), at).empty(), "a load through a register is not a global");
}

void TestPickInstanceGlobal() {
    constexpr std::uintptr_t g = 0x1000, h = 0x2000;
    const d::InstanceGlobal picked = d::PickInstanceGlobal({{g}, {g, g}, {g, h}, {h}, {}, {g}});
    Check(picked.global == g && picked.readers == 4 && picked.reading == 5, "the global most static methods read is the instance's");
    Check(Says(Thrown([&] { d::PickInstanceGlobal({{g}, {g}, {}, {}}); }), "is read by 2"), "two readers are too few");
    Check(Says(Thrown([&] { d::PickInstanceGlobal({{g}, {g}, {g}, {h}, {h}, {h}}); }), "(0x1000) is read by 3"),
          "a global read by half the readers is refused");
    Check(Says(Thrown([&] { d::PickInstanceGlobal({{}, {}}); }), "0 of them read"), "no reads are refused");
}

void TestHex() {
    Check(d::Hex(0) == "0x0" && d::Hex(0x140001000) == "0x140001000" && d::Hex(0xABCDEF) == "0xabcdef",
          "an address is written 0x and lower case hex");
}

// Bytes that sit in this executable's image once, for ScanAll to find.
alignas(16) const volatile std::uint8_t kMarker[] = {0x5A, 0xC3, 0x17, 0xE9, 0x4B, 0x00, 0x8D, 0x71,
                                                     0x2F, 0xB6, 0x93, 0x0C, 0xD4, 0x68, 0xA1, 0x3E};

void TestGame() {
    const re::Exe& exe = re::Game();
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    Check(exe.base == base && exe.end == base + nt->OptionalHeader.SizeOfImage, "Game is the process's executable, base to SizeOfImage");
    Check(&re::Game() == &exe, "Game is read once");
    const auto marker = reinterpret_cast<std::uintptr_t>(&kMarker[0]);
    Check(exe.Contains(marker) && exe.Contains(exe.base) && !exe.Contains(exe.end) && !exe.Contains(exe.base - 1),
          "Contains holds from the base up to, not including, the end");
    Check(!re::Exe{}.Contains(0) && !re::Exe{}.Contains(exe.base), "an empty Exe contains nothing");
}

void TestSafeRead() {
    const std::uint64_t value = 0x1122334455667788ull;
    const auto at = reinterpret_cast<std::uintptr_t>(&value);
    std::uint64_t out = 0;
    Check(re::SafeRead(at, &out, sizeof(out)) && out == value, "a mapped address reads");
    Check(re::Read<std::uint64_t>(at) == std::optional<std::uint64_t>(value), "Read hands back the value");
    Check(!re::SafeRead(0, &out, sizeof(out)) && !re::SafeRead(0xFFFF, &out, 1), "nothing below 0x10000 reads");
    Check(!re::SafeRead(~std::uintptr_t{0} - 3, &out, sizeof(out)), "a read that wraps past the top of memory does not read");
    Check(!re::Read<std::uint32_t>(8), "Read hands back nothing for an address that does not read");

    void* page = VirtualAlloc(nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!page) throw std::runtime_error("VirtualAlloc for the SafeRead test failed");
    DWORD was = 0;
    if (!VirtualProtect(static_cast<std::uint8_t*>(page) + 0x1000, 0x1000, PAGE_NOACCESS, &was))
        throw std::runtime_error("VirtualProtect for the SafeRead test failed");
    const auto edge = reinterpret_cast<std::uintptr_t>(page) + 0x1000 - 4;
    std::uint32_t four = 0;
    Check(re::SafeRead(edge, &four, sizeof(four)), "the last bytes before an unreadable page read");
    Check(!re::SafeRead(edge, &out, sizeof(out)), "a read that runs into an unreadable page does not read");
    Check(!re::Read<std::uint8_t>(edge + 4), "an unreadable page does not read");
    VirtualFree(page, 0, MEM_RELEASE);
}

void TestScanAll() {
    const auto marker = reinterpret_cast<std::uintptr_t>(&kMarker[0]);
    const auto has = [marker](const std::vector<std::uintptr_t>& hits, std::uintptr_t at) {
        for (const std::uintptr_t h : hits)
            if (h == at) return true;
        return false;
    };
    const auto exact = re::ScanAll("5A C3 17 E9 4B 00 8D 71 2F B6 93 0C D4 68 A1 3E");
    Check(exact.size() == 1 && exact[0] == marker, "a pattern of exact bytes is found once, where they lie");
    Check(has(re::ScanAll("5A C3 ? E9 4B ?? 8D 71 2F B6 93 0C D4 68 A1 3E"), marker), "? and ?? match any byte");
    Check(has(re::ScanAll("? C3 17 E9 4B 00 8D 71 2F B6 93 0C D4 68 A1 3E"), marker), "a pattern may open with ?");
    Check(has(re::ScanAll("8D 71 2F B6 93 0C D4 68 A1 3E"), marker + 6), "a hit is where the pattern's first byte is");
    Check(Says(Thrown([] { re::ScanAll("5A C3 1"); }), "ends half a byte in"), "a pattern that ends half a byte in is refused");
    Check(Says(Thrown([] { re::ScanAll("5A 4 C3"); }), "'4 ', which is no hex byte"), "a one-digit byte is refused");
    Check(Says(Thrown([] { re::ScanAll("1G"); }), "which is no hex byte"), "a byte that is not hex is refused");
    Check(Says(Thrown([] { re::ScanAll(""); }), "has no bytes") && Says(Thrown([] { re::ScanAll("   "); }), "has no bytes"),
          "a pattern with no bytes is refused");
    Check(re::ScanAll("5A C3 17 E9 4B 00 8D 71 2F B6 93 0C D4 68 A1 3F 5A C3 17 E9 4B 00 8D 71 2F B6 93 0C D4 68 A1 3F").empty(),
          "a pattern that matches nothing finds nothing");
    Check(static_cast<std::uint8_t>(kMarker[0]) == 0x5A, "the marker is kept");
}

void TestRipTarget() {
    std::uint8_t code[16] = {};
    const std::int32_t disp = 0x100;
    std::memcpy(code + 3, &disp, sizeof(disp));
    const auto at = reinterpret_cast<std::uintptr_t>(code + 3);
    Check(re::RipTarget(at) == at + 4 + 0x100, "a forward displacement is read from past the operand");
    const std::int32_t back = -0x20;
    std::memcpy(code + 3, &back, sizeof(back));
    Check(re::RipTarget(at) == at + 4 - 0x20, "a backward displacement is read from past the operand");
}

void TestNotReady() {
    try {
        throw re::NotReady("the script VM is not made yet");
    } catch (const std::runtime_error& e) {
        Check(std::string(e.what()) == "the script VM is not made yet", "NotReady is a runtime_error with its message");
    } catch (...) {
        Check(false, "NotReady is a runtime_error with its message");
    }
}

void TestEntryListEquality() {
    const d::EntryList a{0x828, 0xC8, 6, -8};
    d::EntryList b = a;
    Check(a == b && !(a != b), "two lists read alike are equal");
    const std::optional<d::EntryList> was = a;
    Check(!(was != std::optional<d::EntryList>(b)), "an optional list read alike is not different");
    for (int field = 0; field < 4; ++field) {
        b = a;
        if (field == 0) b.first += 8;
        if (field == 1) b.stride += 8;
        if (field == 2) b.count += 1;
        if (field == 3) b.func = 0x10;
        Check(a != b && !(a == b), "lists that differ in field " + std::to_string(field) + " are not equal");
    }
}

}  // namespace

int RunReEngineTests() {
    std::cout << "\n=== RE Engine Runtime Tests ===\n";
    TestPickVm();
    TestFindEntryList();
    TestMatrixPlace();
    TestPointerReads();
    TestPickInstanceGlobal();
    TestHex();
    TestGame();
    TestSafeRead();
    TestScanAll();
    TestRipTarget();
    TestNotReady();
    TestEntryListEquality();
    return g_failures;
}

#else

int RunReEngineTests() { return 0; }

#endif
