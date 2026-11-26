// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <cameraunlock/re_engine/tdb_reader.h>

#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace cameraunlock::re_engine {
namespace {

std::uint32_t BytesOf(const Bits& b) { return (b.shift + b.width + 7) / 8; }

std::uint64_t ReadBits(const std::uint8_t* record, const Bits& b) {
    std::uint64_t v = 0;
    std::memcpy(&v, record + b.at, BytesOf(b));
    v >>= b.shift;
    if (b.width < 64) {
        v &= (std::uint64_t{1} << b.width) - 1;
        if (b.isSigned && ((v >> (b.width - 1)) & 1)) v |= ~std::uint64_t{0} << b.width;
    }
    return v;
}

std::uint64_t ReadValue(const std::uint8_t* main, const std::uint8_t* detail, const Value& value) {
    std::uint64_t v = 0;
    std::uint32_t shift = 0;
    for (const Piece& p : value) {
        v |= ReadBits(p.record == Record::Main ? main : detail, p.bits) << shift;
        shift += p.bits.width;
    }
    return v;
}

// `most` is the widest the reader hands the value out as.
void CheckBits(const Bits& b, std::uint32_t recordSize, std::uint32_t most, const std::string& name) {
    if (b.width == 0 || b.width > most)
        throw std::runtime_error("the type database layout gives " + name + " " + std::to_string(b.width) + " bits; it takes 1 to " +
                                 std::to_string(most));
    if (b.shift > 7) throw std::runtime_error("the type database layout starts " + name + " at bit " + std::to_string(b.shift) + " of a byte");
    if (b.shift + b.width > 64)
        throw std::runtime_error("the type database layout gives " + name + " " + std::to_string(b.width) + " bits from bit " +
                                 std::to_string(b.shift) + ", past the 64 bits read");
    if (recordSize && b.at + BytesOf(b) > recordSize)
        throw std::runtime_error("the type database layout puts " + name + " at byte " + std::to_string(b.at) + ", past the " +
                                 std::to_string(recordSize) + " bytes of its record");
}

void CheckValue(const Value& value, std::uint32_t mainSize, std::uint32_t detailSize, std::uint32_t most, const std::string& name) {
    if (value.empty()) throw std::runtime_error("the type database layout gives " + name + " no pieces");
    std::uint32_t width = 0;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const Piece& p = value[i];
        const std::string piece = name + " piece " + std::to_string(i);
        if (value.size() > 1 && p.bits.isSigned) throw std::runtime_error("the type database layout makes " + piece + " signed");
        CheckBits(p.bits, p.record == Record::Main ? mainSize : detailSize, most, piece);
        width += p.bits.width;
    }
    if (width > most)
        throw std::runtime_error("the type database layout gives " + name + " " + std::to_string(width) + " bits; it takes up to " +
                                 std::to_string(most));
}

void CheckSize(std::uint32_t size, const char* record) {
    if (size == 0) throw std::runtime_error(std::string("the type database layout gives the ") + record + " record no size");
}

void Check(const Layout& l) {
    CheckSize(l.type.size, "type");
    CheckSize(l.typeDetail.size, "type detail");
    CheckSize(l.method.size, "method");
    CheckSize(l.method.detailSize, "method detail");
    CheckSize(l.field.size, "field");
    CheckSize(l.field.detailSize, "field detail");
    CheckSize(l.param.size, "parameter");
    CheckSize(l.literal.size, "literal");
    if (l.signature.paramStride == 0) throw std::runtime_error("the type database layout gives the signature no parameter stride");

    const Layout::Type& t = l.type;
    const std::pair<const Bits*, const char*> typeBits[] = {
        {&t.self, "the type's index"},
        {&t.parent, "the type's parent"},
        {&t.enclosing, "the type's enclosing type"},
        {&t.elementType, "the type's element type"},
        {&t.kind, "the type's kind"},
        {&t.instanceSize, "the type's size"},
        {&t.firstMethod, "the type's first method"},
        {&t.firstField, "the type's first field"},
        {&t.detail, "the type's detail"},
        {&t.attributes, "the type's attributes"}};
    for (const auto& [bits, name] : typeBits) CheckBits(*bits, t.size, 32, name);
    const Layout::TypeDetail& td = l.typeDetail;
    const std::pair<const Bits*, const char*> detailBits[] = {{&td.name, "the type's name"},
                                                              {&td.nameSpace, "the type's namespace"},
                                                              {&td.valueSize, "the type's value size"},
                                                              {&td.fieldCount, "the type's field count"},
                                                              {&td.methodCount, "the type's method count"}};
    for (const auto& [bits, name] : detailBits) CheckBits(*bits, td.size, 32, name);

    const Layout::Method& m = l.method;
    CheckBits(m.detail, m.size, 32, "the method's detail");
    CheckValue(m.name, m.size, m.detailSize, 32, "the method's name");
    CheckValue(m.attributes, m.size, m.detailSize, 16, "the method's attributes");
    CheckValue(m.signature, m.size, m.detailSize, 32, "the method's signature");
    CheckBits(m.code, m.size, 64, "the method's code");
    if (l.codeIsAddress && m.code.width != 64)
        throw std::runtime_error("the type database layout keeps each method's code address in " + std::to_string(m.code.width) +
                                 " bits, not 64");
    if (!l.codeIsAddress && m.code.width > 32)
        throw std::runtime_error("the type database layout keeps each method's encoded code offset in " + std::to_string(m.code.width) +
                                 " bits, more than 32");

    const Layout::Field& f = l.field;
    CheckBits(f.detail, f.size, 32, "the field's detail");
    CheckValue(f.name, f.size, f.detailSize, 32, "the field's name");
    CheckValue(f.attributes, f.size, f.detailSize, 16, "the field's attributes");
    CheckValue(f.offset, f.size, f.detailSize, 32, "the field's offset");
    CheckValue(f.type, f.size, f.detailSize, 32, "the field's type");
    CheckValue(f.literal, f.size, f.detailSize, 32, "the field's literal");

    CheckBits(l.param.type, l.param.size, 32, "the parameter's type");
    CheckBits(l.literal.blobOffset, l.literal.size, 32, "the literal's blob offset");
    CheckBits(l.signature.paramCount, 0, 16, "the signature's parameter count");
    CheckBits(l.signature.returns, 0, 32, "the signature's return");
    CheckBits(l.signature.firstParam, 0, 32, "the signature's first parameter");
}

}  // namespace

TdbReader::TdbReader(Layout layout, const void* header) : layout_(std::move(layout)), header_(static_cast<const std::uint8_t*>(header)) {
    Check(layout_);
}

const std::uint8_t* TdbReader::Pointer(std::uint32_t at) const {
    const std::uint8_t* p = nullptr;
    std::memcpy(&p, header_ + at, sizeof(p));
    return p;
}

std::uint32_t TdbReader::Count(std::uint32_t at) const {
    std::uint32_t n = 0;
    std::memcpy(&n, header_ + at, sizeof(n));
    return n;
}

std::uint32_t TdbReader::TypeCount() const { return Count(layout_.header.typeCount); }
std::uint32_t TdbReader::ParamCount() const { return Count(layout_.header.paramCount); }
std::uint32_t TdbReader::StringBytes() const { return Count(layout_.header.stringBytes); }
std::uint32_t TdbReader::LiteralCount() const { return Count(layout_.header.literalCount); }
const char* TdbReader::Strings() const { return reinterpret_cast<const char*>(Pointer(layout_.header.strings)); }
const std::uint8_t* TdbReader::Blob() const { return Pointer(layout_.header.blob); }

const Type* TdbReader::TypeAt(std::uint32_t index) const {
    return reinterpret_cast<const Type*>(Pointer(layout_.header.typeTable) + static_cast<std::size_t>(index) * layout_.type.size);
}

TypeView TdbReader::ReadType(const Type& type) const {
    const auto* r = reinterpret_cast<const std::uint8_t*>(&type);
    const Layout::Type& t = layout_.type;
    const auto read = [r](const Bits& b) { return static_cast<std::uint32_t>(ReadBits(r, b)); };
    return {read(t.self),         read(t.parent),      read(t.enclosing),  read(t.elementType), read(t.kind),
            read(t.instanceSize), read(t.firstMethod), read(t.firstField), read(t.detail),      read(t.attributes)};
}

TypeDetailView TdbReader::ReadTypeDetail(std::uint32_t detail) const {
    const Layout::TypeDetail& t = layout_.typeDetail;
    const std::uint8_t* r = Pointer(layout_.header.typeDetailTable) + static_cast<std::size_t>(detail) * t.size;
    const auto read = [r](const Bits& b) { return static_cast<std::uint32_t>(ReadBits(r, b)); };
    return {read(t.name), read(t.nameSpace), static_cast<std::int32_t>(ReadBits(r, t.valueSize)), read(t.fieldCount), read(t.methodCount)};
}

MethodView TdbReader::ReadMethod(std::uint32_t index) const {
    const Layout::Method& m = layout_.method;
    const std::uint8_t* main = Pointer(layout_.header.methodTable) + static_cast<std::size_t>(index) * m.size;
    const auto detailIndex = static_cast<std::uint32_t>(ReadBits(main, m.detail));
    const std::uint8_t* detail = Pointer(layout_.header.methodDetailTable) + static_cast<std::size_t>(detailIndex) * m.detailSize;
    MethodView v;
    v.name = static_cast<std::uint32_t>(ReadValue(main, detail, m.name));
    v.attributes = static_cast<std::uint16_t>(ReadValue(main, detail, m.attributes));
    v.signature = static_cast<std::uint32_t>(ReadValue(main, detail, m.signature));
    const std::uint64_t code = ReadBits(main, m.code);
    if (layout_.codeIsAddress)
        v.code = reinterpret_cast<void*>(static_cast<std::uintptr_t>(code));
    else
        v.encodedCode = static_cast<std::int32_t>(code);
    return v;
}

FieldView TdbReader::ReadField(std::uint32_t index) const {
    const Layout::Field& f = layout_.field;
    const std::uint8_t* main = Pointer(layout_.header.fieldTable) + static_cast<std::size_t>(index) * f.size;
    const auto detailIndex = static_cast<std::uint32_t>(ReadBits(main, f.detail));
    const std::uint8_t* detail = Pointer(layout_.header.fieldDetailTable) + static_cast<std::size_t>(detailIndex) * f.detailSize;
    const auto read = [main, detail](const Value& v) { return static_cast<std::uint32_t>(ReadValue(main, detail, v)); };
    return {read(f.name), static_cast<std::uint16_t>(ReadValue(main, detail, f.attributes)), read(f.offset), read(f.type), read(f.literal)};
}

std::uint32_t TdbReader::ParamType(std::uint32_t param) const {
    const std::uint8_t* r = Pointer(layout_.header.paramTable) + static_cast<std::size_t>(param) * layout_.param.size;
    return static_cast<std::uint32_t>(ReadBits(r, layout_.param.type));
}

std::int32_t TdbReader::LiteralAt(std::uint32_t index) const {
    const std::uint8_t* r = Pointer(layout_.header.literalTable) + static_cast<std::size_t>(index) * layout_.literal.size;
    return static_cast<std::int32_t>(ReadBits(r, layout_.literal.blobOffset));
}

SignatureView TdbReader::ReadSignature(std::uint32_t signature) const {
    const std::uint8_t* r = Blob() + signature;
    return {static_cast<std::uint16_t>(ReadBits(r, layout_.signature.paramCount)),
            static_cast<std::uint32_t>(ReadBits(r, layout_.signature.returns))};
}

std::uint32_t TdbReader::ParamAt(std::uint32_t signature, std::uint32_t i) const {
    const std::uint8_t* r = Blob() + signature + static_cast<std::size_t>(i) * layout_.signature.paramStride;
    return static_cast<std::uint32_t>(ReadBits(r, layout_.signature.firstParam));
}

}  // namespace cameraunlock::re_engine
