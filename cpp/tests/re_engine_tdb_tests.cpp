// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// Tests for the RE Engine type database reader, Tdb, and what Scene and
// AppEntries answer without a game, over databases built here through Layouts
// of plain numbers. No game version's record structs are used: the layouts
// give values whole, and packed into shared words and split across records as
// the engine's own records keep them.

#include <iostream>

#ifdef CAMERAUNLOCK_TESTS_RE_ENGINE

#include <cameraunlock/re_engine/app_entries.h>
#include <cameraunlock/re_engine/scene.h>
#include <cameraunlock/re_engine/tdb.h>
#include <cameraunlock/re_engine/tdb_reader.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace re = cameraunlock::re_engine;

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    if (ok) {
        std::cout << "  [PASS] " << what << "\n";
    } else {
        std::cout << "  [FAIL] " << what << "\n";
        ++g_failures;
    }
}

template <class E, class F>
std::string Thrown(F f) {
    try {
        f();
    } catch (const E& e) {
        return e.what();
    }
    return {};
}

bool Says(const std::string& message, const std::string& needle) { return message.find(needle) != std::string::npos; }

re::Bits Whole(std::uint32_t at, std::uint32_t bytes, bool isSigned = false) { return {at, 0, bytes * 8, isSigned}; }
re::Value In(re::Record record, re::Bits bits) { return {{record, bits}}; }

void Put(std::vector<std::uint8_t>& to, std::size_t at, const void* from, std::size_t n) {
    if (to.size() < at + n) to.resize(at + n);
    std::memcpy(to.data() + at, from, n);
}
template <class T>
void Put(std::vector<std::uint8_t>& to, std::size_t at, T v) {
    Put(to, at, &v, sizeof(v));
}

// Sets `width` bits from bit `shift` of the bytes from `at`.
void PutBits(std::vector<std::uint8_t>& to, const re::Bits& b, std::uint64_t v) {
    for (std::uint32_t i = 0; i < b.width; ++i) {
        const std::size_t bit = b.at * 8 + b.shift + i;
        if (to.size() <= bit / 8) to.resize(bit / 8 + 1);
        if ((v >> i) & 1)
            to[bit / 8] = static_cast<std::uint8_t>(to[bit / 8] | (1u << (bit % 8)));
        else
            to[bit / 8] = static_cast<std::uint8_t>(to[bit / 8] & ~(1u << (bit % 8)));
    }
}

// A layout of whole little-endian fields, the method's code an address on it.
re::Layout PlainLayout() {
    re::Layout l;
    l.header = {8, 12, 16, 20, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96};
    l.type.size = 40;
    l.type.self = Whole(0, 4);
    l.type.parent = Whole(4, 4);
    l.type.enclosing = Whole(8, 4);
    l.type.elementType = Whole(12, 1);
    l.type.kind = Whole(13, 1);
    l.type.instanceSize = Whole(16, 4);
    l.type.firstMethod = Whole(20, 4);
    l.type.firstField = Whole(24, 4);
    l.type.detail = Whole(28, 4);
    l.type.attributes = Whole(32, 4);
    l.typeDetail.size = 20;
    l.typeDetail.name = Whole(0, 4);
    l.typeDetail.nameSpace = Whole(4, 4);
    l.typeDetail.valueSize = Whole(8, 4, true);
    l.typeDetail.fieldCount = Whole(12, 4);
    l.typeDetail.methodCount = Whole(16, 2);
    l.method.size = 16;
    l.method.detailSize = 8;
    l.method.detail = Whole(0, 4);
    l.method.signature = In(re::Record::Main, Whole(4, 4));
    l.method.code = Whole(8, 8);
    l.method.name = In(re::Record::Detail, Whole(0, 4));
    l.method.attributes = In(re::Record::Detail, Whole(4, 2));
    l.field.size = 16;
    l.field.detailSize = 8;
    l.field.detail = Whole(0, 4);
    l.field.offset = In(re::Record::Main, Whole(4, 4));
    l.field.type = In(re::Record::Main, Whole(8, 4));
    l.field.literal = In(re::Record::Main, Whole(12, 4));
    l.field.name = In(re::Record::Detail, Whole(0, 4));
    l.field.attributes = In(re::Record::Detail, Whole(4, 2));
    l.param.size = 4;
    l.param.type = Whole(0, 4);
    l.literal.size = 4;
    l.literal.blobOffset = Whole(0, 4, true);
    l.signature.paramCount = Whole(0, 2);
    l.signature.returns = Whole(4, 4);
    l.signature.firstParam = Whole(8, 4);
    l.signature.paramStride = 4;
    l.codeIsAddress = true;
    return l;
}

// A database made through a layout. Every table is filled through the layout's
// own Bits, so a reader that read any other place reads the wrong value.
class Db {
public:
    explicit Db(re::Layout layout) : l_(std::move(layout)) {
        String("");
        bytes_.resize(8);
        // Parameter 0 is the empty entry every table starts with.
        Param(0);
    }

    std::uint32_t String(const std::string& s) {
        const auto at = static_cast<std::uint32_t>(strings_.size());
        strings_.insert(strings_.end(), s.begin(), s.end());
        strings_.push_back(0);
        return at;
    }

    std::uint32_t Param(std::uint32_t typeId) {
        const auto at = static_cast<std::uint32_t>(params_.size() / l_.param.size);
        params_.resize(params_.size() + l_.param.size);
        std::vector<std::uint8_t> r(l_.param.size);
        PutBits(r, l_.param.type, typeId);
        std::memcpy(params_.data() + at * l_.param.size, r.data(), r.size());
        return at;
    }

    struct TypeSpec {
        std::string name, ns;
        std::uint32_t parent = 0, enclosing = 0, elementType = 0, kind = 1, instanceSize = 0, attributes = 0;
        std::int32_t valueSize = 0;
    };
    std::uint32_t AddType(const TypeSpec& t) {
        const auto index = static_cast<std::uint32_t>(types_.size());
        types_.push_back(t);
        typeMethods_.emplace_back();
        typeFields_.emplace_back();
        return index;
    }

    struct MethodSpec {
        std::string name;
        std::uint16_t attributes = 0;
        std::uint64_t code = 0;
        std::uint32_t returns = 0;
        std::vector<std::uint32_t> paramTypes;
        // A parameter whose index is past the parameter table.
        bool paramPastTable = false;
    };
    void AddMethod(std::uint32_t type, const MethodSpec& m) { typeMethods_[type].push_back(m); }

    struct FieldSpec {
        std::string name;
        std::uint16_t attributes = 0;
        std::uint32_t offset = 0;
        std::uint32_t type = 0;
        // A literal's bytes; for a negative entry, or an index past the literal
        // table, the two below.
        std::vector<std::uint8_t> value;
        bool stringLiteral = false;
        std::uint32_t literalPastTable = 0;
    };
    void AddField(std::uint32_t type, const FieldSpec& f) { typeFields_[type].push_back(f); }

    // Lays out the tables and the header; the database lives as long as this.
    const void* Header() {
        std::vector<std::uint8_t> types, typeDetails, methods, methodDetails, fields, fieldDetails;
        std::vector<std::int32_t> init;
        std::uint32_t nMethods = 0, nFields = 0;
        for (std::size_t i = 0; i < types_.size(); ++i) {
            const TypeSpec& t = types_[i];
            std::vector<std::uint8_t> r(l_.type.size);
            PutBits(r, l_.type.self, i);
            PutBits(r, l_.type.parent, t.parent);
            PutBits(r, l_.type.enclosing, t.enclosing);
            PutBits(r, l_.type.elementType, t.elementType);
            PutBits(r, l_.type.kind, t.kind);
            PutBits(r, l_.type.instanceSize, t.instanceSize);
            PutBits(r, l_.type.firstMethod, nMethods);
            PutBits(r, l_.type.firstField, nFields);
            PutBits(r, l_.type.detail, i);
            PutBits(r, l_.type.attributes, t.attributes);
            r.resize(l_.type.size);
            types.insert(types.end(), r.begin(), r.end());

            std::vector<std::uint8_t> ir(l_.typeDetail.size);
            PutBits(ir, l_.typeDetail.name, String(t.name));
            PutBits(ir, l_.typeDetail.nameSpace, String(t.ns));
            PutBits(ir, l_.typeDetail.valueSize, static_cast<std::uint32_t>(t.valueSize));
            PutBits(ir, l_.typeDetail.fieldCount, typeFields_[i].size());
            PutBits(ir, l_.typeDetail.methodCount, typeMethods_[i].size());
            ir.resize(l_.typeDetail.size);
            typeDetails.insert(typeDetails.end(), ir.begin(), ir.end());

            for (const MethodSpec& m : typeMethods_[i]) {
                const auto paramsAt = static_cast<std::uint32_t>(bytes_.size());
                std::vector<std::uint8_t> list;
                PutBits(list, l_.signature.paramCount, m.paramTypes.size() + (m.paramPastTable ? 1 : 0));
                PutBits(list, l_.signature.returns, Param(m.returns));
                for (std::size_t p = 0; p < m.paramTypes.size(); ++p) {
                    re::Bits at = l_.signature.firstParam;
                    at.at += static_cast<std::uint32_t>(p * l_.signature.paramStride);
                    PutBits(list, at, Param(m.paramTypes[p]));
                }
                if (m.paramPastTable) {
                    re::Bits at = l_.signature.firstParam;
                    at.at += static_cast<std::uint32_t>(m.paramTypes.size() * l_.signature.paramStride);
                    PutBits(list, at, 0xFFFF);
                }
                list.resize(list.size() + 8);
                bytes_.insert(bytes_.end(), list.begin(), list.end());
                Pieces(methods, methodDetails, l_.method.size, l_.method.detailSize, l_.method.detail, nMethods,
                       {{&l_.method.name, String(m.name)}, {&l_.method.attributes, m.attributes}, {&l_.method.signature, paramsAt}},
                       &l_.method.code, m.code);
                ++nMethods;
            }
            for (const FieldSpec& f : typeFields_[i]) {
                std::uint32_t literalIndex = 0;
                if (f.literalPastTable) {
                    literalIndex = f.literalPastTable;
                } else if (f.stringLiteral) {
                    literalIndex = static_cast<std::uint32_t>(init.size());
                    init.push_back(-4);
                } else if (!f.value.empty()) {
                    literalIndex = static_cast<std::uint32_t>(init.size());
                    init.push_back(static_cast<std::int32_t>(bytes_.size()));
                    bytes_.insert(bytes_.end(), f.value.begin(), f.value.end());
                    bytes_.resize(bytes_.size() + 8);
                }
                Pieces(fields, fieldDetails, l_.field.size, l_.field.detailSize, l_.field.detail, nFields,
                       {{&l_.field.name, String(f.name)},
                        {&l_.field.attributes, f.attributes},
                        {&l_.field.offset, f.offset},
                        {&l_.field.type, f.type},
                        {&l_.field.literal, literalIndex}},
                       nullptr, 0);
                ++nFields;
            }
        }
        types_.clear();
        tables_ = {types, typeDetails, methods, methodDetails, fields, fieldDetails};
        initTable_.resize(init.size() * l_.literal.size);
        for (std::size_t i = 0; i < init.size(); ++i) {
            std::vector<std::uint8_t> r(l_.literal.size);
            PutBits(r, l_.literal.blobOffset, static_cast<std::uint32_t>(init[i]));
            std::memcpy(initTable_.data() + i * l_.literal.size, r.data(), r.size());
        }
        header_.assign(128, 0);
        Put(header_, 0, std::uint32_t{0x00424454u});
        Put(header_, 4, std::uint32_t{77});
        Put(header_, l_.header.typeCount, static_cast<std::uint32_t>(typeMethods_.size()));
        Put(header_, l_.header.paramCount, static_cast<std::uint32_t>(params_.size() / l_.param.size));
        Put(header_, l_.header.stringBytes, static_cast<std::uint32_t>(strings_.size()));
        Put(header_, l_.header.literalCount, static_cast<std::uint32_t>(init.size()));
        Put(header_, l_.header.typeTable, tables_[0].data());
        Put(header_, l_.header.typeDetailTable, tables_[1].data());
        Put(header_, l_.header.methodTable, tables_[2].data());
        Put(header_, l_.header.methodDetailTable, tables_[3].data());
        Put(header_, l_.header.fieldTable, tables_[4].data());
        Put(header_, l_.header.fieldDetailTable, tables_[5].data());
        Put(header_, l_.header.paramTable, params_.data());
        Put(header_, l_.header.literalTable, initTable_.data());
        Put(header_, l_.header.strings, static_cast<const void*>(strings_.data()));
        Put(header_, l_.header.blob, static_cast<const void*>(bytes_.data()));
        return header_.data();
    }

    const re::Layout& GetLayout() const { return l_; }
    // Bytes in the blob before the signatures and literals.
    void PadBlob(std::size_t n) { bytes_.resize(bytes_.size() + n); }

private:
    // One method's or field's main and detail records: detail i belongs to
    // main record i, and each value goes in its pieces.
    void Pieces(std::vector<std::uint8_t>& mains, std::vector<std::uint8_t>& details, std::uint32_t mainSize, std::uint32_t detailSize,
                const re::Bits& detailIndex, std::uint32_t index, const std::vector<std::pair<const re::Value*, std::uint64_t>>& values,
                const re::Bits* code, std::uint64_t codeValue) {
        std::vector<std::uint8_t> main(mainSize), detail(detailSize);
        PutBits(main, detailIndex, index);
        for (const auto& [value, v] : values) {
            std::uint32_t shift = 0;
            for (const re::Piece& p : *value) {
                PutBits(p.record == re::Record::Main ? main : detail, p.bits, v >> shift);
                shift += p.bits.width;
            }
        }
        if (code) PutBits(main, *code, codeValue);
        main.resize(mainSize);
        detail.resize(detailSize);
        mains.insert(mains.end(), main.begin(), main.end());
        details.insert(details.end(), detail.begin(), detail.end());
    }

    re::Layout l_;
    std::vector<TypeSpec> types_;
    std::vector<std::vector<MethodSpec>> typeMethods_;
    std::vector<std::vector<FieldSpec>> typeFields_;
    std::vector<char> strings_;
    std::vector<std::uint8_t> bytes_;
    std::vector<std::uint8_t> params_;
    std::vector<std::uint8_t> initTable_;
    std::vector<std::vector<std::uint8_t>> tables_;
    std::vector<std::uint8_t> header_;
};

struct Bitfields {
    std::uint32_t lo : 13;
    std::uint32_t hi : 19;
    std::int64_t wide : 28;
    std::uint64_t rest : 36;
    std::uint16_t whole;
};

void TestBitsOf() {
    const re::Bits lo = re::BitsOf<Bitfields>([](Bitfields& b) { b.lo = 0x1FFF; });
    Check(lo.at == 0 && lo.shift == 0 && lo.width == 13 && !lo.isSigned, "BitsOf finds a bitfield at the start of its word");
    const re::Bits hi = re::BitsOf<Bitfields>([](Bitfields& b) { b.hi = 0x7FFFF; });
    Check(hi.at == 1 && hi.shift == 5 && hi.width == 19, "BitsOf finds a bitfield that starts inside a byte");
    const re::Bits wide = re::BitsOf<Bitfields>([](Bitfields& b) { b.wide = -1; }, true);
    Check(wide.at == 8 && wide.shift == 0 && wide.width == 28 && wide.isSigned, "BitsOf finds a signed bitfield and keeps its sign");
    const re::Bits whole = re::BitsOf<Bitfields>([](Bitfields& b) { b.whole = 0xFFFF; });
    Check(whole.at == offsetof(Bitfields, whole) && whole.shift == 0 && whole.width == 16, "BitsOf finds a whole member at its offset");
    Check(Says(Thrown<std::runtime_error>([] { re::BitsOf<Bitfields>([](Bitfields&) {}); }), "set no bits"),
          "BitsOf refuses a setter that sets nothing");
    Check(Says(Thrown<std::runtime_error>([] {
              re::BitsOf<Bitfields>([](Bitfields& b) {
                  b.lo = 1;
                  b.whole = 1;
              });
          }),
               "with a gap"),
          "BitsOf refuses bits that are not one run");
}

void TestLayoutChecks() {
    {
        re::Layout l = PlainLayout();
        l.type.parent.width = 0;
        Check(Says(Thrown<std::runtime_error>([&] { re::TdbReader r(l, nullptr); }), "the type's parent 0 bits"),
              "a layout that gives a value no bits is refused, naming it");
    }
    {
        re::Layout l = PlainLayout();
        l.typeDetail.methodCount = Whole(19, 2);
        Check(Says(Thrown<std::runtime_error>([&] { re::TdbReader r(l, nullptr); }), "the type's method count at byte 19, past the 20 bytes"),
              "a layout that puts a value past its record is refused");
    }
    {
        re::Layout l = PlainLayout();
        l.method.attributes = In(re::Record::Detail, Whole(4, 4));
        Check(Says(Thrown<std::runtime_error>([&] { re::TdbReader r(l, nullptr); }), "the method's attributes piece 0 32 bits; it takes 1 to 16"),
              "a layout that gives flags more bits than they are read into is refused");
    }
    {
        re::Layout l = PlainLayout();
        l.method.code = Whole(8, 4);
        Check(Says(Thrown<std::runtime_error>([&] { re::TdbReader r(l, nullptr); }), "code address in 32 bits"),
              "a layout that keeps a code address in fewer than 64 bits is refused");
    }
    {
        re::Layout l = PlainLayout();
        l.codeIsAddress = false;
        Check(Says(Thrown<std::runtime_error>([&] { re::TdbReader r(l, nullptr); }), "encoded code offset in 64 bits"),
              "a layout that keeps an encoded code offset in more than 32 bits is refused");
    }
    {
        re::Layout l = PlainLayout();
        l.field.literal = {{re::Record::Main, Whole(12, 2, true)}, {re::Record::Detail, Whole(6, 1)}};
        Check(Says(Thrown<std::runtime_error>([&] { re::TdbReader r(l, nullptr); }), "the field's literal piece 0 signed"),
              "a layout with a signed piece in a value of several is refused");
    }
    {
        re::Layout l = PlainLayout();
        l.signature.paramStride = 0;
        Check(Says(Thrown<std::runtime_error>([&] { re::TdbReader r(l, nullptr); }), "no parameter stride"),
              "a layout with no parameter stride is refused");
    }
    {
        re::Layout l = PlainLayout();
        l.field.detailSize = 0;
        Check(Says(Thrown<std::runtime_error>([&] { re::TdbReader r(l, nullptr); }), "field detail record no size"),
              "a layout with a record of no size is refused");
    }
}

// A layout of bitfields that share words and values in pieces across a
// definition and its implementation, the method's code an encoded offset.
re::Layout PackedLayout() {
    re::Layout l = PlainLayout();
    l.type.size = 24;
    l.type.self = {0, 0, 19};
    l.type.parent = {2, 3, 19};
    l.type.enclosing = {4, 6, 19};
    l.type.elementType = {7, 1, 7};
    l.type.kind = {8, 0, 3};
    l.type.instanceSize = {8, 3, 29};
    l.type.firstMethod = {12, 0, 22};
    l.type.firstField = {14, 6, 20};
    l.type.detail = {17, 2, 18};
    l.type.attributes = {20, 0, 32};
    l.typeDetail.size = 16;
    l.typeDetail.name = {0, 0, 28, true};
    l.typeDetail.nameSpace = {3, 4, 28, true};
    l.typeDetail.valueSize = Whole(8, 4, true);
    l.typeDetail.fieldCount = {12, 0, 20};
    l.typeDetail.methodCount = {14, 4, 12};
    l.method.size = 12;
    l.method.detailSize = 8;
    l.method.detail = {0, 0, 19};
    l.method.signature = {{re::Record::Main, {2, 3, 13}}, {re::Record::Main, {4, 0, 13}}};
    l.method.code = Whole(8, 4, true);
    l.method.name = In(re::Record::Detail, Whole(4, 4));
    l.method.attributes = In(re::Record::Detail, Whole(0, 2));
    l.field.size = 8;
    l.field.detailSize = 12;
    l.field.detail = {0, 0, 19};
    l.field.type = In(re::Record::Main, {2, 3, 19});
    l.field.literal = {{re::Record::Detail, {8, 0, 6}}, {re::Record::Detail, {8, 6, 4}}, {re::Record::Main, {4, 6, 6}}};
    l.field.offset = In(re::Record::Detail, Whole(4, 4));
    l.field.name = In(re::Record::Detail, Whole(0, 4));
    l.field.attributes = In(re::Record::Detail, {9, 2, 15});
    l.codeIsAddress = false;
    return l;
}

void ExpectReads(re::Layout layout, const std::string& label) {
    const std::string v = label + ": ";
    Db db(layout);
    db.AddType({});
    // Members ahead of Foo's, so its first method and field are not 0.
    const std::uint32_t a = db.AddType({"A", "app"});
    db.AddMethod(a, {"a0", 0, 0, a, {}});
    db.AddMethod(a, {"a1", 0, 0, a, {}});
    db.AddField(a, {"_a0", 0, 0, a, {1, 0, 0, 0}});
    db.AddField(a, {"_a1", 0, 0, a});
    db.AddField(a, {"_a2", 0, 0, a});
    db.AddType({"B", "app"});
    const std::uint32_t foo = db.AddType({"Foo", "app", 1, 2, 6, 3, 0x40, 0x20, 0x18});
    db.AddMethod(foo, {"get_Bar", 0x10, 0x1234, foo, {foo}});
    db.AddField(foo, {"_baz", 0, 0x24, foo, {7, 0, 0, 0}});
    const re::TdbReader r(layout, db.Header());
    Check(r.TypeCount() == 4, v + "four types");
    const re::TypeView t = r.ReadType(*r.TypeAt(foo));
    Check(t.self == 3 && t.parent == 1 && t.enclosing == 2 && t.elementType == 6, v + "the type's index, parent, enclosing and element type");
    Check(t.kind == 3 && t.instanceSize == 0x40, v + "the type's kind and size");
    Check(t.attributes == 0x20, v + "the type's attributes");
    Check(t.firstMethod == 2 && t.firstField == 3 && t.detail == 3, v + "the type's members and detail");
    const re::TypeDetailView detail = r.ReadTypeDetail(t.detail);
    Check(std::string(r.Strings() + detail.name) == "Foo" && std::string(r.Strings() + detail.nameSpace) == "app",
          v + "the type's name and namespace, through the strings");
    Check(detail.methodCount == 1 && detail.fieldCount == 1 && detail.valueSize == 0x18, v + "the type's member counts and value size");
    const re::MethodView m = r.ReadMethod(2);
    Check(std::string(r.Strings() + m.name) == "get_Bar" && m.attributes == 0x10, v + "the method's name and attributes");
    Check(r.CodeIsAddress() ? m.code == reinterpret_cast<void*>(0x1234) && m.encodedCode == 0 : m.encodedCode == 0x1234 && m.code == nullptr,
          v + "the method's code");
    const re::SignatureView sig = r.ReadSignature(m.signature);
    Check(sig.paramCount == 1, v + "the signature's parameter count");
    Check(r.ParamType(sig.returns) == foo && r.ParamType(r.ParamAt(m.signature, 0)) == foo, v + "the return's and the parameter's types");
    const re::FieldView f = r.ReadField(3);
    Check(std::string(r.Strings() + f.name) == "_baz" && f.offset == 0x24 && f.type == foo && f.attributes == 0,
          v + "the field's name, offset, type and attributes");
    Check(r.LiteralCount() == 2 && f.literal == 1 && r.Blob()[r.LiteralAt(f.literal)] == 7, v + "the field's literal");
}

void TestReader() {
    ExpectReads(PlainLayout(), "whole fields, code address");
    ExpectReads(PackedLayout(), "bitfields in pieces, encoded code");
}

void TestReaderPieces() {
    // Values wide enough to fill every piece, so a piece read from the wrong
    // place or shifted wrong shows.
    const re::Layout l = PackedLayout();
    Db db(l);
    db.AddType({});
    const std::uint32_t foo = db.AddType({"Foo", "app"});
    for (int i = 0; i < 3; ++i) db.AddType({"Pad" + std::to_string(i), "app"});
    db.AddMethod(foo, {"m", 0xBEEF, static_cast<std::uint64_t>(static_cast<std::uint32_t>(-8)), foo, {}});
    const re::TdbReader r(l, db.Header());
    Check(r.ReadMethod(0).encodedCode == -8, "a negative encoded code offset reads back negative");
    Check(r.ReadMethod(0).attributes == 0xBEEF, "a method's attributes read back whole");

    // A signed name offset that reads with its top bit set is sign-extended.
    std::vector<std::uint8_t> rec(16);
    PutBits(rec, l.typeDetail.name, 0x0FFFFFFF);
    std::vector<std::uint8_t> header(128);
    Put(header, l.header.typeDetailTable, rec.data());
    const re::TdbReader signedRead(l, header.data());
    Check(signedRead.ReadTypeDetail(0).name == 0xFFFFFFFFu, "a signed value with its top bit set is sign-extended");

    // A field whose literal index fills its three pieces.
    Db wide(l);
    wide.AddType({});
    const std::uint32_t bar = wide.AddType({"Bar", "app"});
    wide.AddField(bar, {"_wide", 0x7FFF, 0x12345678, bar, {}, false, 0xABCD});
    wide.AddMethod(bar, {"far", 0, 0, bar, {bar}});
    wide.PadBlob(0x3000);
    const re::TdbReader wr(l, wide.Header());
    Check(wr.ReadField(0).literal == 0xABCD, "a literal index spread over three pieces in two records reads back whole");
    const std::uint32_t farAt = wr.ReadMethod(0).signature;
    Check(farAt >= 0x3000 && wr.ReadSignature(farAt).paramCount == 1 && wr.ParamType(wr.ParamAt(farAt, 0)) == bar,
          "a signature offset past its low piece's 13 bits reads back whole");
    Check(wr.ReadField(0).attributes == 0x7FFF && wr.ReadField(0).offset == 0x12345678, "a field's 15-bit attributes and offset read back whole");
}

// A database with the integer primitives, an object hierarchy, a nested type,
// an enum and a value type, in a given layout.
struct World {
    Db db;
    std::uint32_t object = 0, int32 = 0, single = 0, foo = 0, derived = 0, inner = 0, mode = 0, vec = 0, voidType = 0, iface = 0;
    const void* header = nullptr;

    explicit World(re::Layout l) : db(std::move(l)) {
        db.AddType({});
        object = db.AddType({"Object", "System", 0, 0, 0, 1, 0x10});
        const char* ints[] = {"Boolean", "SByte", "Byte", "Char", "Int16", "UInt16", "Int32", "UInt32", "Int64", "UInt64"};
        for (std::uint32_t i = 0; i < 10; ++i) {
            const std::uint32_t t = db.AddType({ints[i], "System", object, 0, 2 + i, 5});
            if (std::string(ints[i]) == "Int32") int32 = t;
        }
        single = db.AddType({"Single", "System", object, 0, 12, 5});
        voidType = db.AddType({"Void", "System", object, 0, 1, 5});
        foo = db.AddType({"Foo", "app", object, 0, 0, 1, 0x40});
        derived = db.AddType({"Derived", "app", foo, 0, 0, 1, 0x48});
        inner = db.AddType({"Inner", "", object, foo, 0, 1, 0x20});
        mode = db.AddType({"Mode", "app", object, 0, 2 + 6, 5});
        vec = db.AddType({"vec2", "via", object, 0, 0, 5, 0x18, 0, 8});
        iface = db.AddType({"IThing", "app", 0, 0, 0, 1, 0, re::kTypeAttributesInterface});
        db.AddType({"Dup", "a"});
        db.AddType({"Dup", "b"});
        db.AddType({"Twice", "app"});
        db.AddType({"Twice", "app"});

        db.AddMethod(object, {"GetType", 0, 0x9000, object, {}});
        db.AddMethod(foo, {"get_Bar", re::kMethodAttributesStatic, 0x1000, foo, {int32}});
        db.AddMethod(foo, {"Take", 0, 0x1100, voidType, {foo}});
        db.AddMethod(foo, {"Take", 0, 0x1200, voidType, {int32}});
        db.AddMethod(foo, {"Two", 0, 0x1300, voidType, {int32, foo}});
        db.AddMethod(foo, {"Two", 0, 0x1400, voidType, {foo, int32}});
        db.AddMethod(foo, {"Odd", 0, 0x1500, voidType, {int32}, true});
        db.AddMethod(foo, {"Abstract", 0, 0, voidType, {}});
        db.AddMethod(derived, {"Own", 0, 0x2000, int32, {}});

        db.AddField(foo, {"_count", 0, 0x10, int32});
        db.AddField(foo, {"s_shared", re::kFieldAttributesStatic, 0x99, int32});
        db.AddField(foo, {"_name", 0, 0x18, object});
        db.AddField(derived, {"_extra", 0, 0x40, single});
        const std::uint16_t constant = re::kFieldAttributesStatic | re::kFieldAttributesLiteral;
        db.AddField(mode, {"value__", 0, 0, int32});
        db.AddField(mode, {"Off", constant, 0, mode, {0xFF, 0xFF, 0xFF, 0xFF}});
        db.AddField(mode, {"On", constant, 0, mode, {2, 0, 0, 0}});
        db.AddField(mode, {"Label", constant, 0, mode, {}, true});
        db.AddField(mode, {"Lost", constant, 0, mode, {}, false, 1000});
        db.AddField(vec, {"x", 0, 0, single});
        db.AddField(vec, {"y", 0, 4, single});
        db.AddField(vec, {"z", 0, 12, single});

        header = db.Header();
    }

    re::Tdb Make() const { return re::Tdb(db.GetLayout(), header); }
};

void TestTdbTypes() {
    World w(PlainLayout());
    const re::Tdb tdb = w.Make();
    Check(tdb.NumTypes() == 24 && tdb.Version() == 77, "Tdb counts the database's types and reads its version after the magic");
    const re::Type* foo = tdb.FindType("app.Foo");
    Check(foo == &tdb.TypeAt(w.foo), "FindType finds a type by its full name");
    Check(tdb.FindType("app.Missing") == nullptr, "FindType answers null for a name no type carries");
    Check(tdb.FullName(tdb.TypeAt(w.inner)) == "app.Foo.Inner", "a nested type's full name runs through its enclosing type");
    Check(tdb.FindType("app.Foo.Inner") == &tdb.TypeAt(w.inner), "FindType finds a nested type");
    Check(tdb.FindTypeByShortName("Inner") == &tdb.TypeAt(w.inner), "FindTypeByShortName finds a type by its last name");
    Check(tdb.FindTypeByShortName("Nothing") == nullptr, "FindTypeByShortName answers null for a name no type carries");
    Check(Says(Thrown<std::runtime_error>([&] { tdb.FindTypeByShortName("Dup"); }), "more than one type is named Dup: a.Dup, b.Dup") ||
              Says(Thrown<std::runtime_error>([&] { tdb.FindTypeByShortName("Dup"); }), "more than one type is named Dup: b.Dup, a.Dup"),
          "FindTypeByShortName refuses a name two types carry, naming them");
    Check(Says(Thrown<std::runtime_error>([&] { tdb.FindType("app.Twice"); }), "more than one type is named app.Twice"),
          "FindType refuses a full name two types carry");
    Check(std::string(tdb.Name(*foo)) == "Foo", "Name is the type's own name");
    Check(tdb.Parent(tdb.TypeAt(w.derived)) == foo, "Parent is the type's parent");
    Check(tdb.Parent(tdb.TypeAt(w.iface)) == nullptr, "a type with parent 0 has no parent");
    Check(tdb.Derives(&tdb.TypeAt(w.derived), *foo) && tdb.Derives(foo, *foo), "Derives through the parents and of the type itself");
    Check(!tdb.Derives(foo, tdb.TypeAt(w.derived)) && !tdb.Derives(nullptr, *foo), "Derives is false for a base asked of its child, and for null");
    Check(tdb.IsInterface(tdb.TypeAt(w.iface)) && !tdb.IsInterface(*foo), "IsInterface reads TypeAttributes.Interface");
    Check(tdb.Size(*foo) == 0x40 && tdb.ObjectType(*foo) == 1 && tdb.ObjectType(tdb.TypeAt(w.vec)) == 5,
          "Size and ObjectType are the type's own");
    Check(tdb.IntegerSize(tdb.TypeAt(w.int32)) == 4 && tdb.IntegerSize(tdb.TypeAt(w.mode)) == 4 && tdb.IntegerSize(*foo) == 0,
          "IntegerSize of an integer, an enum over one, and a class");
    Check(tdb.ValueSize(tdb.TypeAt(w.vec)) == 8, "ValueSize is the value type's field size");
    Check(tdb.Holds(foo) && tdb.Holds(&tdb.TypeAt(0)) && tdb.Holds(&tdb.TypeAt(23)), "Holds each of the table's types");
    const auto* past = reinterpret_cast<const re::Type*>(reinterpret_cast<const std::uint8_t*>(&tdb.TypeAt(23)) + 40);
    const auto* inside = reinterpret_cast<const re::Type*>(reinterpret_cast<const std::uint8_t*>(foo) + 8);
    const auto* before = reinterpret_cast<const re::Type*>(reinterpret_cast<const std::uint8_t*>(&tdb.TypeAt(0)) - 40);
    Check(!tdb.Holds(past) && !tdb.Holds(inside) && !tdb.Holds(before) && !tdb.Holds(nullptr),
          "Holds nothing past, inside or before the table's types");
}

void TestTdbMethods() {
    World w(PlainLayout());
    const re::Tdb tdb = w.Make();
    const re::Type& foo = tdb.TypeAt(w.foo);
    const std::vector<re::Method> methods = tdb.Methods(foo);
    Check(methods.size() == 7, "Methods lists what the type itself declares");
    const re::Method bar = tdb.FindMethod(foo, "get_Bar");
    Check(bar.fn == reinterpret_cast<void*>(0x1000) && bar.isStatic && bar.numParams == 1, "FindMethod finds a static method and its code");
    Check(bar.firstParam == &tdb.TypeAt(w.int32) && bar.returnType == &foo, "a method's first parameter and return type");
    Check(tdb.FindMethod(tdb.TypeAt(w.derived), "get_Bar", 1).fn == bar.fn, "FindMethod searches the parents");
    Check(tdb.FindMethod(tdb.TypeAt(w.derived), "Own", 0).returnType == &tdb.TypeAt(w.int32), "FindMethod finds the type's own method");
    Check(tdb.FindMethod(foo, "get_Bar", 0).fn == nullptr, "FindMethod by a count no method takes finds nothing");
    Check(tdb.FindMethod(foo, "Abstract").fn == nullptr && tdb.FindMethod(foo, "Abstract").name != nullptr,
          "a method with no code is found with null code");
    Check(Says(Thrown<std::runtime_error>([&] { tdb.FindMethod(foo, "Take", 1); }), "app.Foo declares 2 methods named Take taking 1"),
          "FindMethod refuses two methods that fit");
    Check(tdb.FindMethodTaking(foo, "Take", "System.Int32").fn == reinterpret_cast<void*>(0x1200) &&
              tdb.FindMethodTaking(foo, "Take", "app.Foo").fn == reinterpret_cast<void*>(0x1100),
          "FindMethodTaking tells overloads apart by their parameter's type");
    Check(tdb.FindMethodTaking(foo, "Take", "System.Single").fn == nullptr, "FindMethodTaking finds nothing for a type no overload takes");
    const std::vector<const re::Type*> two = tdb.ParamTypes(methods[3]);
    Check(two.size() == 2 && two[0] == &tdb.TypeAt(w.int32) && two[1] == &foo, "ParamTypes lists each parameter's type in order");
    const re::Method odd = tdb.FindMethod(foo, "Odd");
    const std::vector<const re::Type*> oddTypes = tdb.ParamTypes(odd);
    Check(odd.numParams == 2 && oddTypes.size() == 2 && oddTypes[0] == &tdb.TypeAt(w.int32) && oddTypes[1] == nullptr,
          "a parameter past the parameter table has a null type");
    Check(Says(Thrown<std::runtime_error>([&] { tdb.FindMethod(foo, "Two"); }), "declares 2 methods named Two; the layer does not pick one"),
          "FindMethod with any count refuses two that fit");

    World packed(PackedLayout());
    const re::Tdb encoded = packed.Make();
    const re::Method encodedBar = encoded.FindMethod(encoded.TypeAt(packed.foo), "get_Bar");
    Check(encodedBar.name != nullptr && encodedBar.numParams == 1 && encodedBar.fn == nullptr,
          "with encoded code offsets, methods have null code until the decoder is found");
}

void TestTdbFields() {
    World w(PlainLayout());
    const re::Tdb tdb = w.Make();
    const re::Type& foo = tdb.TypeAt(w.foo);
    const re::Field count = tdb.FindField(foo, "_count");
    Check(count.offset == 0x10 && count.integerSize == 4 && count.typeName == "System.Int32", "FindField gives the offset, size and type");
    Check(tdb.FindField(foo, "_name").integerSize == 0 && tdb.FindField(foo, "_name").typeName == "System.Object",
          "a field of a class type has no integer size");
    Check(tdb.FindField(foo, "s_shared").offset < 0, "FindField leaves out statics");
    Check(tdb.FieldOffset(tdb.TypeAt(w.derived), "_count") == 0x10 && tdb.FieldOffset(tdb.TypeAt(w.derived), "_extra") == 0x40,
          "FieldOffset searches the type then its parents");
    Check(tdb.FieldOffset(foo, "_missing") < 0 && tdb.FindField(foo, "_missing").typeName.empty(), "a missing field has no offset");
    const auto fields = tdb.Fields(foo);
    Check(fields.size() == 3 && fields[1].first == "s_shared" && fields[1].second.offset == -1 && fields[0].second.offset == 0x10,
          "Fields lists the type's own fields, statics at -1");

    const re::Type& mode = tdb.TypeAt(w.mode);
    Check(tdb.EnumConstant(mode, "Off") == -1, "an enum constant over a signed integer keeps its -1");
    Check(tdb.EnumConstant(mode, "On") == 2, "an enum constant reads its value from the blob");
    Check(Says(Thrown<std::runtime_error>([&] { tdb.EnumConstant(mode, "Nope"); }), "app.Mode has no constant Nope"),
          "EnumConstant refuses a constant the enum has not");
    Check(Says(Thrown<std::runtime_error>([&] { tdb.EnumConstant(mode, "Label"); }), "app.Mode.Label's value is a string"),
          "EnumConstant refuses a string literal");
    Check(Says(Thrown<std::runtime_error>([&] { tdb.EnumConstant(mode, "Lost"); }), "app.Mode.Lost's value is at init data 1000 of 3"),
          "EnumConstant refuses an init data index past the table");
    Check(Says(Thrown<std::runtime_error>([&] { tdb.EnumConstant(foo, "On"); }), "app.Foo is no enum over an integer"),
          "EnumConstant refuses a type that is no enum");

    const re::Type& vec = tdb.TypeAt(w.vec);
    tdb.RequireFloats(vec, {"x", "y"}, 8);
    Check(true, "RequireFloats takes floats 4 bytes apart that fit");
    Check(Says(Thrown<std::runtime_error>([&] { tdb.RequireFloats(vec, {"x", "y", "z"}, 16); }), "via.vec2.z is not the System.Single at +8"),
          "RequireFloats refuses a float out of place");
    Check(Says(Thrown<std::runtime_error>([&] { tdb.RequireFloats(vec, {"x", "w"}, 16); }), "(no such field)"),
          "RequireFloats refuses a missing field");
    Check(Says(Thrown<std::runtime_error>([&] { tdb.RequireFloats(vec, {"x"}, 4); }), "via.vec2 takes 8 bytes, more than the 4"),
          "RequireFloats refuses a value type bigger than what is handed over");
}

void TestTdbWithoutVm() {
    World w(PlainLayout());
    const re::Tdb tdb = w.Make();
    Check(Says(Thrown<std::logic_error>([&] { tdb.ThreadContext(); }), "without the game's VM"),
          "ThreadContext is refused on a database read without the VM");
    Check(Says(Thrown<std::logic_error>([&] { tdb.RuntimeType(nullptr, tdb.TypeAt(w.foo)); }), "without the game's VM"),
          "RuntimeType is refused on a database read without the VM");
    Check(!re::Tdb::Discovered(), "no database is discovered in a test");
    Check(Says(Thrown<std::logic_error>([] { re::Tdb::Get(); }), "Tdb::Get before Tdb::Discover"), "Tdb::Get before Discover is refused");
    Check(Says(Thrown<std::logic_error>([] { re::AppEntries::Get(); }), "AppEntries::Get before AppEntries::Discover"),
          "AppEntries::Get before Discover is refused");
    Check(Says(Thrown<std::logic_error>([] { re::MethodCode("via.Scene", "findComponents", 1); }), "Tdb::Get before Tdb::Discover"),
          "MethodCode before the database is discovered is refused");
}

void TestTdbTypeObjects() {
    // TypeOf reads the first word of the object info an object's first word
    // points at; FieldAddress adds the field base kept just before the vtable
    // the object's first word points at.
    const re::Type* type = reinterpret_cast<const re::Type*>(0x1234);
    const re::Type* info[1] = {type};
    void* object[1] = {info};
    Check(re::TypeOf(object) == type, "TypeOf reads the type from the object info's first word");

    std::uint8_t holder[16] = {};
    const std::int32_t base = 0x10;
    std::memcpy(holder, &base, sizeof(base));
    void* withBase[8] = {holder + sizeof(void*)};
    Check(re::FieldAddress(withBase, 8) == reinterpret_cast<std::uint8_t*>(withBase) + 0x18,
          "FieldAddress adds the field base before the vtable");
}

void TestWideNarrow() {
    Check(re::Wide("via.Transform") == L"via.Transform", "Wide widens ASCII byte for byte");
    Check(re::Narrow(L"Leon\x00e9") == "Leon?", "Narrow keeps ASCII and shows anything wider as ?");
    Check(!re::Scene::Live(nullptr), "Scene::Live is false for null");
}

}  // namespace

int RunReEngineTdbTests() {
    std::cout << "\n=== RE Engine Type Database Tests ===\n";
    TestBitsOf();
    TestLayoutChecks();
    TestReader();
    TestReaderPieces();
    TestTdbTypes();
    TestTdbMethods();
    TestTdbFields();
    TestTdbWithoutVm();
    TestTdbTypeObjects();
    TestWideNarrow();
    return g_failures;
}

#else

int RunReEngineTdbTests() { return 0; }

#endif
