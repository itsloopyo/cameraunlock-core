// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

// The engine's type database read through a Layout: plain numbers that say
// where each value lies in the database's header and records. Each game's
// database version lays its records out its own way; the consumer fills a
// Layout for the version it meets and hands it in, and nothing here knows any
// version's records.
namespace cameraunlock::re_engine {

// CLI attribute flags the database keeps on its types, methods and fields, as
// ECMA-335 (6th edition, June 2012) Partition II defines them.
// II.23.1.15 TypeAttributes: Interface.
inline constexpr std::uint32_t kTypeAttributesInterface = 0x00000020;
// II.23.1.10 MethodAttributes: Static.
inline constexpr std::uint16_t kMethodAttributesStatic = 0x0010;
// II.23.1.5 FieldAttributes: Static and Literal.
inline constexpr std::uint16_t kFieldAttributesStatic = 0x0010;
inline constexpr std::uint16_t kFieldAttributesLiteral = 0x0040;

// Where a value lies in a record: `width` bits from bit `shift` of the
// little-endian bytes from byte `at`. A signed value is sign-extended from its
// top bit. `shift` + `width` is at most 64.
struct Bits {
    std::uint32_t at = 0;
    std::uint32_t shift = 0;
    std::uint32_t width = 0;
    bool isSigned = false;
};

// A method's or a field's value can lie in its main record, in its detail
// record (the one the main record names by index), or in pieces across both.
enum class Record { Main, Detail };
struct Piece {
    Record record = Record::Main;
    Bits bits;
};
// The pieces, lowest first: each one's bits go above the ones before it.
using Value = std::vector<Piece>;

struct Layout {
    // Byte offsets in the header: 32-bit counts and 64-bit pointers.
    struct Header {
        std::uint32_t typeCount = 0;
        std::uint32_t paramCount = 0;
        std::uint32_t stringBytes = 0;
        std::uint32_t literalCount = 0;
        std::uint32_t typeTable = 0;
        std::uint32_t typeDetailTable = 0;
        std::uint32_t methodTable = 0;
        std::uint32_t methodDetailTable = 0;
        std::uint32_t fieldTable = 0;
        std::uint32_t fieldDetailTable = 0;
        std::uint32_t paramTable = 0;
        std::uint32_t literalTable = 0;
        std::uint32_t strings = 0;
        std::uint32_t blob = 0;
    } header;

    // Each `size` is a record's bytes, the stride of its table.
    struct Type {
        std::uint32_t size = 0;
        Bits self, parent, enclosing, elementType, kind, instanceSize, firstMethod, firstField, detail, attributes;
    } type;
    struct TypeDetail {
        std::uint32_t size = 0;
        Bits name, nameSpace, valueSize, fieldCount, methodCount;
    } typeDetail;
    struct Method {
        std::uint32_t size = 0;
        std::uint32_t detailSize = 0;
        Bits detail;  // in the main record
        Value name, attributes, signature;
        // In the main record: the code's address where codeIsAddress, otherwise
        // the offset the engine's decoder turns into it.
        Bits code;
    } method;
    struct Field {
        std::uint32_t size = 0;
        std::uint32_t detailSize = 0;
        Bits detail;  // in the main record
        Value name, attributes, offset, type, literal;
    } field;
    struct Param {
        std::uint32_t size = 0;
        Bits type;
    } param;
    // The literal table: each entry an offset into the blob where a literal's
    // value is, or a negative one for a string literal.
    struct Literal {
        std::uint32_t size = 0;
        Bits blobOffset;
    } literal;
    // The signature a method's signature value points at in the blob: its
    // parameter count, its return, and its first parameter, each an index into
    // the parameter table, with each next parameter `paramStride` bytes on.
    struct Signature {
        Bits paramCount, returns, firstParam;
        std::uint32_t paramStride = 0;
    } signature;

    // Whether a method holds its code's address instead of an offset the
    // engine's decoder turns into one.
    bool codeIsAddress = false;
};

// The bits `set` writes when it sets one value of an R to all ones, from an
// R of zeroes: the Bits a Layout gives for a member or a bitfield of a
// consumer's own record struct. Throws when `set` writes nothing or bits that
// are not one run.
template <class R, class Set>
Bits BitsOf(Set set, bool isSigned = false) {
    R r;
    std::memset(static_cast<void*>(&r), 0, sizeof(R));
    set(r);
    std::uint8_t bytes[sizeof(R)];
    std::memcpy(bytes, &r, sizeof(R));
    std::size_t first = sizeof(R) * 8, last = 0;
    for (std::size_t bit = 0; bit < sizeof(R) * 8; ++bit) {
        if (!((bytes[bit / 8] >> (bit % 8)) & 1)) continue;
        if (first == sizeof(R) * 8) first = bit;
        last = bit;
    }
    if (first == sizeof(R) * 8) throw std::runtime_error("BitsOf: setting the value to all ones set no bits");
    for (std::size_t bit = first; bit <= last; ++bit)
        if (!((bytes[bit / 8] >> (bit % 8)) & 1))
            throw std::runtime_error("BitsOf: setting the value to all ones set bits " + std::to_string(first) + " to " +
                                     std::to_string(last) + " with a gap at " + std::to_string(bit));
    return {static_cast<std::uint32_t>(first / 8), static_cast<std::uint32_t>(first % 8), static_cast<std::uint32_t>(last - first + 1),
            isSigned};
}

// A type record in the database. Its layout is the Layout's, so it is read
// only through a TdbReader.
struct Type;

// What a type's records hold, whatever the version lays them out as.
struct TypeView {
    std::uint32_t self = 0;
    std::uint32_t parent = 0;
    std::uint32_t enclosing = 0;
    std::uint32_t elementType = 0;  // the CLI element type, for a primitive or an enum
    std::uint32_t kind = 0;  // the engine's kind of object, as the database numbers it
    std::uint32_t instanceSize = 0;
    std::uint32_t firstMethod = 0;
    std::uint32_t firstField = 0;
    std::uint32_t detail = 0;
    std::uint32_t attributes = 0;  // System.Reflection.TypeAttributes
};

struct TypeDetailView {
    std::uint32_t name = 0;  // an offset into the strings
    std::uint32_t nameSpace = 0;
    std::int32_t valueSize = 0;
    std::uint32_t fieldCount = 0;
    std::uint32_t methodCount = 0;
};

struct MethodView {
    std::uint32_t name = 0;
    std::uint16_t attributes = 0;
    std::uint32_t signature = 0;  // the signature's offset in the blob
    std::int32_t encodedCode = 0;  // without codeIsAddress: what the decoder turns into the code's address
    void* code = nullptr;  // with codeIsAddress: the code's address itself
};

struct FieldView {
    std::uint32_t name = 0;
    std::uint16_t attributes = 0;
    std::uint32_t offset = 0;
    std::uint32_t type = 0;
    // Where a literal's value is: an index into the literal table.
    std::uint32_t literal = 0;
};

struct SignatureView {
    std::uint16_t paramCount = 0;
    std::uint32_t returns = 0;
};

// A type database at `header`, read through `layout`. Reads go to the database
// each time; nothing is copied out of it.
class TdbReader {
public:
    // Throws naming the first value that does not fit its record.
    TdbReader(Layout layout, const void* header);

    const Layout& GetLayout() const { return layout_; }

    std::uint32_t TypeCount() const;
    std::uint32_t ParamCount() const;
    std::uint32_t StringBytes() const;
    const char* Strings() const;
    const std::uint8_t* Blob() const;
    const Type* TypeAt(std::uint32_t index) const;
    TypeView ReadType(const Type& t) const;
    TypeDetailView ReadTypeDetail(std::uint32_t detail) const;
    MethodView ReadMethod(std::uint32_t index) const;
    FieldView ReadField(std::uint32_t index) const;
    std::uint32_t ParamType(std::uint32_t param) const;
    std::uint32_t LiteralCount() const;
    std::int32_t LiteralAt(std::uint32_t index) const;
    bool CodeIsAddress() const { return layout_.codeIsAddress; }
    // The signature at `signature` in the blob, and its parameter `i`.
    SignatureView ReadSignature(std::uint32_t signature) const;
    std::uint32_t ParamAt(std::uint32_t signature, std::uint32_t i) const;

private:
    const std::uint8_t* Pointer(std::uint32_t at) const;
    std::uint32_t Count(std::uint32_t at) const;

    Layout layout_;
    const std::uint8_t* header_;
};

}  // namespace cameraunlock::re_engine
