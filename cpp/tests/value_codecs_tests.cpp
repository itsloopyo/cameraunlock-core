// The canonical value codecs against data/fixtures/canonical-ini/codecs/cases.tsv, which the
// C# ValueCodecFixtures runs too, plus a round-trip sweep over floats and doubles and the
// parts only C++ has: integral types other than int, and strings kept as bytes.

#include <cameraunlock/config/value_codecs.h>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace cameraunlock::config;

int g_failures = 0;

void Check(bool cond, const std::string& name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

template <class F>
bool Throws(F&& f) {
    try {
        f();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) throw std::runtime_error("cannot open fixture " + path.string());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The byte escape data/fixtures/canonical-ini/README.md defines.
std::string Unescape(const std::string& field) {
    std::string bytes;
    for (std::size_t i = 0; i < field.size(); ++i) {
        if (field[i] != '\\') {
            bytes.push_back(field[i]);
        } else if (i + 1 < field.size() && field[i + 1] == '\\') {
            bytes.push_back('\\');
            ++i;
        } else if (i + 3 < field.size() && field[i + 1] == 'x') {
            bytes.push_back(static_cast<char>(std::stoi(field.substr(i + 2, 2), nullptr, 16)));
            i += 3;
        } else {
            throw std::runtime_error("bad escape in fixture field " + field);
        }
    }
    return bytes;
}

std::vector<std::string> SplitTabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    for (;;) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            fields.push_back(line.substr(start));
            return fields;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
}

std::uint32_t BitsOf(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

std::uint64_t BitsOf(double value) {
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

template <class F>
F FromBits(typename std::conditional<std::is_same<F, float>::value, std::uint32_t, std::uint64_t>::type bits) {
    F value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

template <class F>
std::string HexBits(F value) {
    char buffer[24];
    if constexpr (std::is_same_v<F, float>) {
        std::snprintf(buffer, sizeof buffer, "0x%08llX", static_cast<unsigned long long>(BitsOf(value)));
    } else {
        std::snprintf(buffer, sizeof buffer, "0x%016llX", static_cast<unsigned long long>(BitsOf(value)));
    }
    return buffer;
}

enum class FixtureMode { kNever, kMenusOnly, kAllDialogue, kAllOverlays };

const EnumCodec<FixtureMode>& FixtureEnum() {
    static const EnumCodec<FixtureMode> codec({{"Never", FixtureMode::kNever},
                                               {"MenusOnly", FixtureMode::kMenusOnly},
                                               {"AllDialogue", FixtureMode::kAllDialogue},
                                               {"AllOverlays", FixtureMode::kAllOverlays}});
    return codec;
}

template <class Codec>
void RunRow(const Codec& codec, const std::vector<std::string>& fields, const std::string& line) {
    using Value = typename Codec::Value;
    constexpr bool kFloating = std::is_same_v<Value, float> || std::is_same_v<Value, double>;
    const CodecParseResult<Value> parsed = codec.Parse(Unescape(fields[1]));

    if (fields[2] == "invalid") {
        if (fields.size() != 3) throw std::runtime_error("malformed row " + line);
        Check(!parsed.ok() && codec.Equal(parsed.value, Value{}), "invalid: " + line);
        return;
    }
    if (fields[2] != "canonical" || fields.size() != (kFloating ? 5u : 4u)) {
        throw std::runtime_error("malformed row " + line);
    }
    if (!parsed.ok()) {
        Check(false, line + " (error: " + parsed.error + ")");
        return;
    }

    const std::string canonical = Unescape(fields[3]);
    const std::string rendered = codec.Render(parsed.value);
    const CodecParseResult<Value> again = codec.Parse(rendered);
    bool good = rendered == canonical && again.ok() && codec.Equal(again.value, parsed.value) &&
                codec.Render(again.value) == rendered;
    std::string note = rendered == canonical ? "" : " (got " + rendered + ")";
    if constexpr (kFloating) {
        good = good && HexBits(parsed.value) == fields[4];
        if (HexBits(parsed.value) != fields[4]) note += " (read " + HexBits(parsed.value) + ")";
    }
    Check(good, "canonical: " + line + note);
}

void TestFixtures() {
    std::cout << "Value codec fixtures:\n";
    const std::string tsv = ReadBytes(fs::path(CAMERAUNLOCK_CANONICAL_INI_FIXTURES) / "codecs" / "cases.tsv");

    std::map<std::string, int> rows;
    std::size_t start = 0;
    while (start < tsv.size()) {
        std::size_t end = tsv.find('\n', start);
        if (end == std::string::npos) end = tsv.size();
        const std::string line = tsv.substr(start, end - start);
        start = end + 1;
        if (line.empty() || line[0] == '#') continue;

        const std::vector<std::string> fields = SplitTabs(line);
        if (fields.size() < 3) throw std::runtime_error("malformed row " + line);
        const std::string& codec = fields[0];
        ++rows[codec];
        if (codec == "bool") {
            RunRow(BoolCodec{}, fields, line);
        } else if (codec == "int") {
            RunRow(IntCodec<std::int32_t>{}, fields, line);
        } else if (codec == "int[1,65535]") {
            RunRow(IntCodec<std::int32_t>(1, 65535), fields, line);
        } else if (codec == "hex32") {
            RunRow(Hex32Codec{}, fields, line);
        } else if (codec == "hex64") {
            RunRow(Hex64Codec{}, fields, line);
        } else if (codec == "float") {
            RunRow(FloatCodec{}, fields, line);
        } else if (codec == "float[0,1]") {
            RunRow(FloatCodec(0.0f, 1.0f), fields, line);
        } else if (codec == "double") {
            RunRow(DoubleCodec{}, fields, line);
        } else if (codec == "string") {
            RunRow(StringCodec{}, fields, line);
        } else if (codec == "enum") {
            RunRow(FixtureEnum(), fields, line);
        } else if (codec == "color") {
            RunRow(ColorCodec{}, fields, line);
        } else if (codec == "list<hex32>") {
            RunRow(Hex32ListCodec{}, fields, line);
        } else if (codec == "list<hex64>") {
            RunRow(Hex64ListCodec{}, fields, line);
        } else if (codec == "list<string>") {
            RunRow(StringListCodec{}, fields, line);
        } else {
            throw std::runtime_error("unknown codec in row " + line);
        }
    }
    Check(rows.size() == 14, "the fixture file covers all 14 codec names (" + std::to_string(rows.size()) + ")");
}

std::uint64_t SplitMix64(std::uint64_t& state) {
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

template <class F>
std::vector<F> SweepValues(int random_count) {
    using L = std::numeric_limits<F>;
    std::vector<F> values = {F(0), -F(0), L::denorm_min(), -L::denorm_min(), L::min(), -L::min(), L::max(),
                             L::lowest(), L::epsilon(), F(1), F(-1), F(0.15), F(0.1), F(1e-05), F(1) / F(3),
                             F(2) / F(3), F(10), F(100), F(1e6), F(16777216), F(9007199254740992.0)};
    for (int exponent = L::min_exponent10 - 8; exponent <= L::max_exponent10; ++exponent) {
        const F power = static_cast<F>(std::pow(10.0L, exponent));
        if (!std::isfinite(power) || power == F(0)) continue;
        values.push_back(power);
        values.push_back(std::nextafter(power, L::max()));
        values.push_back(std::nextafter(power, F(0)));
    }
    const F extra[] = {L::denorm_min(), L::min(), L::max()};
    for (F v : extra) {
        F up = v;
        F down = v;
        for (int i = 0; i < 64; ++i) {
            up = std::nextafter(up, L::max());
            down = std::nextafter(down, F(0));
            values.push_back(up);
            values.push_back(down);
        }
    }
    std::uint64_t state = 0xC0DEC5EEDull;
    while (random_count > 0) {
        const std::uint64_t bits = SplitMix64(state);
        F value;
        if constexpr (std::is_same_v<F, float>) {
            value = FromBits<float>(static_cast<std::uint32_t>(bits >> 32));
        } else {
            value = FromBits<double>(bits);
        }
        if (!std::isfinite(value)) continue;
        values.push_back(value);
        --random_count;
    }
    return values;
}

// Reading its own render gives the same bits for every value swept, and std::to_chars at
// each precision writes what printf %.Ng writes in the C locale this process runs in.
template <class F>
void TestSweep(const char* name, int random_count) {
    const FloatingCodec<F> codec;
    const std::vector<F> values = SweepValues<F>(random_count);
    const int max_precision = std::is_same_v<F, float> ? 9 : 17;
    int round_trip_failures = 0;
    int printf_mismatches = 0;
    for (F value : values) {
        const std::string text = codec.Render(value);
        const CodecParseResult<F> read = codec.Parse(text);
        const bool shaped = text.find('.') != std::string::npos || text.find('e') != std::string::npos;
        if (!read.ok() || BitsOf(read.value) != BitsOf(value) || !shaped) {
            if (++round_trip_failures <= 5) std::cout << "    " << HexBits(value) << " rendered " << text << "\n";
        }
        for (int precision = 1; precision <= max_precision; ++precision) {
            char chars[64];
            const std::to_chars_result written =
                std::to_chars(chars, chars + sizeof chars, value, std::chars_format::general, precision);
            char printed[64];
            std::snprintf(printed, sizeof printed, "%.*g", precision, static_cast<double>(value));
            if (std::string(chars, written.ptr) != printed) {
                if (++printf_mismatches <= 5) {
                    std::cout << "    " << HexBits(value) << " %." << precision << "g: to_chars "
                              << std::string(chars, written.ptr) << ", printf " << printed << "\n";
                }
            }
        }
    }
    Check(round_trip_failures == 0, std::string(name) + ": " + std::to_string(values.size()) +
                                        " values read back from their render bit for bit (" +
                                        std::to_string(round_trip_failures) + " failed)");
    Check(printf_mismatches == 0, std::string(name) + ": std::to_chars general precision N equals printf %.Ng (" +
                                      std::to_string(printf_mismatches) + " differed)");
}

void TestIntegralTypes() {
    std::cout << "Integral types:\n";
    const IntCodec<std::uint16_t> port;
    Check(port.Parse("65535").ok() && port.Parse("65535").value == 65535, "uint16_t reads 65535");
    Check(!port.Parse("65536").ok() && !port.Parse("-1").ok(), "uint16_t refuses 65536 and -1");
    Check(port.Parse("-0").ok() && port.Parse("-0").value == 0, "uint16_t reads -0 as 0");
    Check(port.Parse("-1").error == "expected a whole number from 0 to 65535", "uint16_t names its range");

    const IntCodec<std::int64_t> wide;
    Check(wide.Parse("-9223372036854775808").value == std::numeric_limits<std::int64_t>::min() &&
              wide.Parse("9223372036854775807").value == std::numeric_limits<std::int64_t>::max(),
          "int64_t reads both ends");
    Check(!wide.Parse("9223372036854775808").ok() && !wide.Parse("-9223372036854775809").ok(),
          "int64_t refuses one past each end");
    Check(wide.Render(std::numeric_limits<std::int64_t>::min()) == "-9223372036854775808", "int64_t writes its minimum");

    const IntCodec<std::uint64_t> offset;
    Check(offset.Parse("18446744073709551615").value == std::numeric_limits<std::uint64_t>::max(),
          "uint64_t reads its maximum");
    Check(!offset.Parse("18446744073709551616").ok() && !offset.Parse("99999999999999999999999").ok(),
          "uint64_t refuses what passes it");
    Check(offset.Render(std::numeric_limits<std::uint64_t>::max()) == "18446744073709551615", "uint64_t writes its maximum");

    const IntCodec<std::int8_t> tiny;
    Check(tiny.Parse("-128").value == -128 && tiny.Parse("127").value == 127 && !tiny.Parse("128").ok() &&
              !tiny.Parse("-129").ok(),
          "int8_t reads -128 to 127");
    Check(tiny.Render(-128) == "-128", "int8_t writes a number, not a character");

    const IntCodec<unsigned char> byte;
    Check(byte.Parse("255").value == 255 && !byte.Parse("256").ok(), "unsigned char reads 0 to 255");

    const IntCodec<int> port_range(1, 65535);
    Check(port_range.min() == 1 && port_range.max() == 65535, "the range is kept");
    Check(Throws([] { IntCodec<int>(5, 1); }), "a range with min above max throws");
    Check(Throws([&] { (void)port_range.Render(0); }) && Throws([&] { (void)port_range.Render(65536); }),
          "writing a number outside the range throws");
    Check(port_range.Render(4242) == "4242" && IntCodec<int>().Render(-7) == "-7", "numbers are written in decimal");
}

void TestFloatingApi() {
    std::cout << "Float and double API:\n";
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    Check(Throws([&] { DoubleCodec(nan, 1.0); }) && Throws([&] { DoubleCodec(0.0, inf); }) &&
              Throws([] { FloatCodec(1.0f, 0.0f); }),
          "a range with a bound that is not finite, or min above max, throws");
    Check(Throws([&] { (void)DoubleCodec().Render(nan); }) && Throws([&] { (void)DoubleCodec().Render(inf); }) &&
              Throws([&] { (void)DoubleCodec().Render(-inf); }),
          "writing a value that is not finite throws");
    Check(Throws([] { (void)FloatCodec(0.0f, 1.0f).Render(1.5f); }) &&
              Throws([] { (void)FloatCodec(0.0f, 1.0f).Render(-0.25f); }),
          "writing a value outside the range throws");
    Check(FloatCodec(0.0f, 1.0f).Render(-0.0f) == "-0.0", "-0 is inside a range from 0");
    Check(FloatCodec().Equal(0.0f, 0.0f) && !FloatCodec().Equal(0.0f, -0.0f) && !DoubleCodec().Equal(0.0, -0.0),
          "equality is bitwise");
    Check(FloatCodec().min() == std::numeric_limits<float>::lowest() && FloatCodec().max() == std::numeric_limits<float>::max(),
          "the default range is every finite value");
    Check(FloatCodec().Render(0.1f) == "0.1" && DoubleCodec().Render(static_cast<double>(0.1f)) == "0.10000000149011612",
          "a float and the double holding it are written for their own type");

    // Kept out of the shared fixtures because .NET Framework disagrees: it writes these two
    // floats 1234.5678 and 3451485.3, reads the float text below as 1.0, and net35 reads
    // 3e-324 as 0.
    Check(FloatCodec().Render(1234.5677490234375f) == "1234.5677", "%.8g of 1234.5677490234375 is 1234.5677");
    Check(FloatCodec().Render(3451485.25f) == "3451485.2", "an exact tie at the last digit rounds to even");
    const CodecParseResult<double> denormal = DoubleCodec().Parse("3e-324");
    Check(denormal.ok() && BitsOf(denormal.value) == 1, "3e-324 reads as the smallest denormal");
    const CodecParseResult<float> above_tie = FloatCodec().Parse("1.00000005960464477539062500001");
    Check(above_tie.ok() && BitsOf(above_tie.value) == 0x3F800001u,
          "a float text just above the midpoint of 1.0 and the next float reads as the next float");
}

void TestErrorsNameTheExpectation() {
    std::cout << "Errors name the expectation:\n";
    Check(BoolCodec().Parse("maybe").error == "expected true or false", "bool");
    Check(IntCodec<int>(1, 65535).Parse("0").error == "expected a whole number from 1 to 65535" &&
              IntCodec<int>(1, 65535).Parse("x").error == "expected a whole number from 1 to 65535",
          "int");
    Check(Hex32Codec().Parse("404").error == "expected 0x and 1 to 8 hex digits, such as 0x404" &&
              Hex64Codec().Parse("404").error == "expected 0x and 1 to 16 hex digits, such as 0x404",
          "hex32 and hex64");
    Check(FloatCodec().Parse(".5").error == "expected a number such as 1.0, 0.15 or 1e-05", "float syntax");
    Check(FloatCodec(0.0f, 1.0f).Parse(".5").error == "expected a number from 0.0 to 1.0" &&
              FloatCodec(0.0f, 1.0f).Parse("1.5").error == "expected a number from 0.0 to 1.0",
          "float range");
    Check(FloatCodec().Parse("1e39").error == "expected a number from -3.4028235e+38 to 3.4028235e+38" &&
              DoubleCodec().Parse("-1e309").error ==
                  "expected a number from -1.7976931348623157e+308 to 1.7976931348623157e+308",
          "float and double too large");
    Check(FloatCodec().Parse("1e-46").error == "expected 0.0 or a number no closer to zero than 1e-45" &&
              DoubleCodec().Parse("-1e-400").error == "expected 0.0 or a number no closer to zero than 5e-324",
          "float and double too close to zero");
    Check(FloatCodec(0.5f, 1.0f).Parse("1e-46").error == "expected a number from 0.5 to 1.0",
          "too close to zero for a range without zero names the range");
    Check(StringCodec().Parse("a\x01z").error == "holds the control byte 0x01: expected text with no control character but tab",
          "string");
    Check(FixtureEnum().Parse("Sometimes").error == "expected Never, MenusOnly, AllDialogue or AllOverlays", "enum");
    Check(EnumCodec<FixtureMode>({{"Never", FixtureMode::kNever}, {"MenusOnly", FixtureMode::kMenusOnly}})
                  .Parse("x")
                  .error == "expected Never or MenusOnly",
          "enum with two tokens");
    Check(ColorCodec().Parse("1,1,1").error ==
              "expected four numbers from 0.0 to 1.0 separated by commas, such as 1.0, 0.5, 0.0, 1.0",
          "color count");
    Check(ColorCodec().Parse("1, 2, 1, 1").error == "item 2 '2': expected a number from 0.0 to 1.0", "color item");
    Check(Hex32ListCodec().Parse("0x1, ,0x2").error == "item 2 is empty: expected a value between commas",
          "list empty item");
    Check(Hex32ListCodec().Parse("0x1, zz").error == "item 2 'zz': expected 0x and 1 to 8 hex digits, such as 0x404",
          "list item");
}

void TestStringsKeepTheirBytes() {
    std::cout << "Strings:\n";
    const std::string cp1252 = "\xE9t\xE9";
    const CodecParseResult<std::string> read = StringCodec().Parse(cp1252);
    Check(read.ok() && read.value == cp1252 && StringCodec().Render(read.value) == cp1252,
          "bytes that are not UTF-8 are read and written unchanged");
    Check(StringListCodec().Render({"\xE9t\xE9", "x"}) == "\xE9t\xE9, x", "list items keep their bytes too");
    Check(Throws([] { (void)StringCodec().Render(" a"); }) && Throws([] { (void)StringCodec().Render("a\t"); }) &&
              Throws([] { (void)StringCodec().Render(std::string("a\0b", 3)); }) &&
              Throws([] { (void)StringCodec().Render("a\nb"); }),
          "writing a surrounding space or tab, or a control byte, throws");
    Check(StringCodec().Render("a\tb") == "a\tb" && StringCodec().Render("") == "", "a tab inside, and empty text, are written");
}

void TestEnumConstruction() {
    std::cout << "Enum construction:\n";
    Check(Throws([] { EnumCodec<FixtureMode>({}); }), "an empty token list throws");
    Check(Throws([] { EnumCodec<FixtureMode>({{"menusOnly", FixtureMode::kMenusOnly}}); }) &&
              Throws([] { EnumCodec<FixtureMode>({{"Menus_Only", FixtureMode::kMenusOnly}}); }) &&
              Throws([] { EnumCodec<FixtureMode>({{"", FixtureMode::kMenusOnly}}); }) &&
              Throws([] { EnumCodec<FixtureMode>({{"1st", FixtureMode::kMenusOnly}}); }),
          "a token that is not PascalCase throws");
    Check(Throws([] { EnumCodec<FixtureMode>({{"Never", FixtureMode::kNever}, {"NEVER", FixtureMode::kMenusOnly}}); }),
          "two tokens that read the same throw");
    Check(Throws([] { EnumCodec<FixtureMode>({{"Never", FixtureMode::kNever}, {"Off", FixtureMode::kNever}}); }),
          "two tokens for one value throw");
    const EnumCodec<FixtureMode> two({{"Never", FixtureMode::kNever}, {"MenusOnly", FixtureMode::kMenusOnly}});
    Check(Throws([&] { (void)two.Render(FixtureMode::kAllOverlays); }), "writing a value with no token throws");
    Check(two.tokens().size() == 2 && two.tokens()[1].token == "MenusOnly", "the tokens are kept in order");
}

void TestListsAndColors() {
    std::cout << "Lists and colors:\n";
    Check(Throws([] { (void)StringListCodec().Render({"a,b"}); }) && Throws([] { (void)StringListCodec().Render({""}); }) &&
              Throws([] { (void)StringListCodec().Render({"a", " b"}); }),
          "a string item holding ',' or a surrounding space, or an empty one, throws");
    Check(StringListCodec().Render({}) == "" && Hex64ListCodec().Render({0x10, 0x7FF6A0B21000ull}) == "0x10, 0x7FF6A0B21000",
          "lists are joined by ', '");
    Check(!Hex32ListCodec().Equal({1, 2}, {1}) && !Hex32ListCodec().Equal({1, 2}, {2, 1}) &&
              Hex32ListCodec().Equal({1, 2}, {1, 2}),
          "list equality is item by item, in order");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Check(Throws([] { (void)ColorCodec().Render({1.0f, 1.5f, 0.0f, 1.0f}); }) &&
              Throws([&] { (void)ColorCodec().Render({nan, 0.0f, 0.0f, 1.0f}); }),
          "writing a component outside [0,1] throws");
    Check(ColorCodec().Render({1.0f, 0.5f, 0.0f, 0.15f}) == "1.0, 0.5, 0.0, 0.15", "a color is written r, g, b, a");
    Check(!ColorCodec().Equal({0.0f, 0.0f, 0.0f, 1.0f}, {-0.0f, 0.0f, 0.0f, 1.0f}), "color equality is bitwise");
}

}  // namespace

int RunValueCodecsTests() {
    std::cout << "\n=== Value codec tests ===\n";
    g_failures = 0;
    TestFixtures();
    TestSweep<float>("float", 200000);
    TestSweep<double>("double", 200000);
    TestIntegralTypes();
    TestFloatingApi();
    TestErrorsNameTheExpectation();
    TestStringsKeepTheirBytes();
    TestEnumConstruction();
    TestListsAndColors();
    return g_failures;
}
