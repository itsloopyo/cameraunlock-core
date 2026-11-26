// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// The runtime layer's choices from what it read in the game: which global is the
// script VM, where via.Application's stage list lies, where via.Transform keeps
// its world matrix. Nothing here reads the game: the callers hand in the hits,
// the bytes or a reader, so the tests run it over hand-made data.
namespace cameraunlock::re_engine::discovery {

// `v` as the layer's messages write an address or an offset: 0x and lower case hex.
std::string Hex(std::uintptr_t v);

// One hit of the code that loads the script VM before asking for a thread's
// context: the global it loads and the function it calls.
struct Load {
    std::uintptr_t global = 0;
    std::uintptr_t call = 0;
};

struct Vm {
    std::uintptr_t global = 0;
    std::uintptr_t getThreadContext = 0;
};

// The global loaded most often, which has to be loaded more than ten times and
// more than twice as often as any other, and the function nine in ten of its
// loads call. Throws naming what it saw otherwise.
Vm PickVm(const std::vector<Load>& loads);

// The globals a function reads a whole pointer from in its first instructions,
// up to its first return or jump: `mov r64, [rip+d]` and `cmp qword [rip+d], imm`,
// each as an absolute address, leaving out the /GS cookie. Instructions that
// start in the first `size` bytes of `code`, read from `at`, are walked;
// `code` holds kReadAhead bytes past them for hde64, which reads up to that
// much of an instruction whatever its length.
inline constexpr std::size_t kReadAhead = 32;
std::vector<std::uintptr_t> PointerReads(const std::uint8_t* code, std::size_t size, std::uintptr_t at);

// The global that holds a native singleton's instance, from what each of the
// type's static methods reads (PointerReads): the one read by more than half
// of the methods that read any, and by at least three of them. Throws naming
// what it saw otherwise.
struct InstanceGlobal {
    std::uintptr_t global = 0;
    int readers = 0;  // the methods that read it
    int reading = 0;  // the methods that read any global
};
InstanceGlobal PickInstanceGlobal(const std::vector<std::vector<std::uintptr_t>>& reads);

// via.Application's stage list: entry i's description pointer is at
// first + i * stride, and the function the engine calls for it at that plus
// func. Offsets are from the start of the object.
struct EntryList {
    std::size_t first = 0;
    std::size_t stride = 0;
    std::size_t count = 0;
    std::ptrdiff_t func = 0;

    bool operator==(const EntryList& o) const { return first == o.first && stride == o.stride && count == o.count && func == o.func; }
    bool operator!=(const EntryList& o) const { return !(*this == o); }
};

// Reads an object a pointer at a time: the text the pointer at `offset` points
// at (empty for none), and whether it points at code.
struct ObjectReader {
    std::function<std::string(std::size_t offset)> text;
    std::function<bool(std::size_t offset)> code;
};

// The list in an object of `size` bytes, found from the descriptions of the
// three rendering stages. Each has to be named once. They have to lie on one
// unbroken run of descriptions a fixed stride apart, and the smallest stride
// that does it is taken. The function pointer is the one place, at a fixed
// distance from each description, where every entry points at code. nullopt
// while any of the three is named nowhere; throws on anything else that does
// not come out as exactly one answer.
std::optional<EntryList> FindEntryList(std::size_t size, const ObjectReader& read);

// Where a type keeps the matrix a getter hands back: the 4-byte aligned offset
// in its objects that has held the getter's 64 bytes on every frame seen. Two
// places can hold the same matrix on one frame, so each frame keeps only the
// places that held it on every frame before.
class MatrixPlace {
public:
    // One frame's object bytes and the matrix its getter gave. The place once
    // exactly one is left; nullopt while more than one is. Throws when none is.
    std::optional<std::size_t> See(const std::uint8_t* object, std::size_t size, const float (&matrix)[16]);
    // The places left, for the log.
    std::size_t Left() const { return places_ ? places_->size() : 0; }

private:
    std::optional<std::vector<std::size_t>> places_;
    int frames_ = 0;
};

}  // namespace cameraunlock::re_engine::discovery
