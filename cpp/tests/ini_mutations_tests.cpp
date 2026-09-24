// GenerateIniMutations against data/fixtures/canonical-ini/mutations, which the C# IniMutations
// runs too: every output's name and SHA-256, in order. Plus the argument checks.

#include <cameraunlock/config/testing/ini_mutations.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace cameraunlock::config::testing;

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
std::string Thrown(F&& f) {
    try {
        f();
    } catch (const std::invalid_argument& e) {
        return e.what();
    }
    return "(nothing thrown)";
}

fs::path Root() { return fs::path(CAMERAUNLOCK_CANONICAL_INI_FIXTURES) / "mutations"; }

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) throw std::runtime_error("cannot open fixture " + path.string());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<std::string> SplitAt(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = text.find(separator, start);
        if (end == std::string::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, end - start));
        start = end + 1;
    }
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

std::vector<std::string> Rows(const fs::path& path) {
    std::vector<std::string> rows;
    for (const std::string& line : SplitAt(ReadBytes(path), '\n')) {
        if (!line.empty() && line[0] != '#') rows.push_back(line);
    }
    return rows;
}

std::string Sha256(const std::string& data) {
    static constexpr std::array<std::uint32_t, 64> k = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::array<std::uint32_t, 8> h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::string message = data;
    const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
    message.push_back(static_cast<char>(0x80));
    while (message.size() % 64 != 56) message.push_back('\0');
    for (int i = 7; i >= 0; --i) message.push_back(static_cast<char>((bits >> (i * 8)) & 0xFF));
    const auto rotr = [](std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (std::size_t block = 0; block < message.size(); block += 64) {
        std::array<std::uint32_t, 64> w{};
        for (int i = 0; i < 16; ++i) {
            w[i] = 0;
            for (int j = 0; j < 4; ++j) {
                w[i] = (w[i] << 8) | static_cast<unsigned char>(message[block + static_cast<std::size_t>(i * 4 + j)]);
            }
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::array<std::uint32_t, 8> v = h;
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25);
            const std::uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
            const std::uint32_t t1 = v[7] + s1 + ch + k[i] + w[i];
            const std::uint32_t s0 = rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22);
            const std::uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            const std::uint32_t t2 = s0 + maj;
            v = {t1 + t2, v[0], v[1], v[2], v[3] + t1, v[4], v[5], v[6]};
        }
        for (int i = 0; i < 8; ++i) h[i] += v[i];
    }
    std::string hex;
    char buffer[9];
    for (const std::uint32_t word : h) {
        std::snprintf(buffer, sizeof(buffer), "%08x", word);
        hex += buffer;
    }
    return hex;
}

std::vector<MutationKey> ReadKeys(const fs::path& path) {
    std::vector<MutationKey> keys;
    for (const std::string& row : Rows(path)) {
        const std::vector<std::string> f = SplitAt(row, '\t');
        if (f[0] == "key" && f.size() == 5) {
            MutationKey key;
            key.section = Unescape(f[1]);
            key.key = Unescape(f[2]);
            key.alternate = Unescape(f[3]);
            key.hotkey = f[4] == "true";
            keys.push_back(key);
        } else if (f[0] == "range" && f.size() == 2 && !keys.empty()) {
            keys.back().out_of_range.push_back(Unescape(f[1]));
        } else if (f[0] == "chord" && f.size() == 5 && !keys.empty()) {
            keys.back().chords.push_back({Unescape(f[1]), Unescape(f[2]), Unescape(f[3]), Unescape(f[4])});
        } else {
            throw std::runtime_error("bad keys.tsv row: " + row);
        }
    }
    return keys;
}

void RunCase(const fs::path& dir) {
    const std::string name = dir.filename().string();
    const std::string input = ReadBytes(dir / "input.ini");
    const std::vector<MutationKey> keys = ReadKeys(dir / "keys.tsv");
    const std::vector<IniMutation> outputs = GenerateIniMutations(input, keys);
    const std::vector<std::string> expected = Rows(dir / "expected.tsv");

    std::vector<std::string> got;
    for (const IniMutation& m : outputs) got.push_back(m.name + "\t" + Sha256(m.bytes));
    std::vector<std::string> want;
    for (const std::string& row : expected) {
        const std::vector<std::string> f = SplitAt(row, '\t');
        want.push_back(Unescape(f[0]) + "\t" + f[1]);
    }
    std::size_t first_difference = 0;
    while (first_difference < got.size() && first_difference < want.size() && got[first_difference] == want[first_difference]) {
        ++first_difference;
    }
    const bool same = got == want;
    if (!same) {
        std::cout << "    first difference at output " << first_difference << ": got '"
                  << (first_difference < got.size() ? got[first_difference] : "(none)") << "', expected '"
                  << (first_difference < want.size() ? want[first_difference] : "(none)") << "'\n";
    }
    Check(same, name + ": " + std::to_string(want.size()) + " outputs, names and SHA-256 as expected.tsv");

    std::set<std::string> names;
    for (const IniMutation& m : outputs) names.insert(m.name);
    Check(names.size() == outputs.size(), name + ": no name repeats");

    const std::vector<IniMutation> again = GenerateIniMutations(input, keys);
    bool equal = again.size() == outputs.size();
    for (std::size_t i = 0; equal && i < again.size(); ++i) {
        equal = again[i].name == outputs[i].name && again[i].bytes == outputs[i].bytes;
    }
    Check(equal, name + ": a second run gives the same outputs");
}

void TestSha256() {
    Check(Sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 of nothing");
    Check(Sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 of abc");
    Check(Sha256(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "SHA-256 of a million a's");
}

void TestArguments() {
    const std::string base = "[General]\nToggleKey=0x23\n";
    const MutationKey good{"General", "ToggleKey", "0x24", {}, true, {}};
    Check(Thrown([&] { GenerateIniMutations(base, {}); }) == "the corpus needs at least one key", "no keys throws");
    Check(Thrown([&] { GenerateIniMutations(base, {good, MutationKey{"general", "togglekey", "0x25", {}, true, {}}}); }) ==
              "[general] togglekey is listed twice",
          "a key listed twice, in other case, throws");
    const auto fails = [&](MutationKey key) {
        return Thrown([&] { GenerateIniMutations(base, {key}); }) != "(nothing thrown)";
    };
    Check(fails({"", "ToggleKey", "1", {}, false, {}}), "an empty section throws");
    Check(fails({"General", "", "1", {}, false, {}}), "an empty key throws");
    Check(fails({" General", "ToggleKey", "1", {}, false, {}}), "a section starting with a space throws");
    Check(fails({"General", "ToggleKey ", "1", {}, false, {}}), "a key ending with a space throws");
    Check(fails({"Gen]eral", "ToggleKey", "1", {}, false, {}}), "a section holding ']' throws");
    Check(fails({"General", "Toggle=Key", "1", {}, false, {}}), "a key holding '=' throws");
    Check(fails({"General", ";ToggleKey", "1", {}, false, {}}), "a key starting ';' throws");
    Check(fails({"General", "#ToggleKey", "1", {}, false, {}}), "a key starting '#' throws");
    Check(fails({"General", "[ToggleKey", "1", {}, false, {}}), "a key starting '[' throws");
    Check(fails({"General", "ToggleKey", "1\t2", {}, false, {}}), "a tab in the alternate throws");
    Check(fails({"General", "ToggleKey", "caf\xE9", {}, false, {}}), "a byte above 0x7E in the alternate throws");
    Check(fails({"General", "ToggleKey", "1", {"\x7F"}, false, {}}), "DEL in an out-of-range value throws");
    Check(fails({"General", "ToggleKey", "1", {}, true, {{"Hotkeys", "Chord=", "1", "0"}}}), "a bad chord key throws");
    Check(fails({"General", "ToggleKey", "1", {}, true, {{"Hotkeys", "Chord", "1", "\n"}}}), "a bad chord value throws");
    Check(Thrown([&] { GenerateIniMutations("[General]\nToggleKey=" + std::string(190, 'x') + "\n", {good}); }) ==
              "[General] ToggleKey=" + std::string(190, 'x') + " is longer than 199 characters",
          "a first key too long for the 199-character line throws");
    Check(Thrown([&] { GenerateIniMutations(base, {good}); }) == "(nothing thrown)", "a valid key is accepted");
}

void TestEmptyBase() {
    const std::vector<IniMutation> out = GenerateIniMutations("", {MutationKey{"General", "Enabled", "false", {}, false, {}}});
    Check(out.size() == 62 && out[0].name == "[General] Enabled: removed" && out[0].bytes == "[General]\r\n",
          "an empty base gains the key in a new section, with CRLF endings");
    Check(out.back().name == "file: cp1252 byte in a value" && out.back().bytes == "[General]\r\nEnabled=false\xE9\r\n",
          "the last output is the cp1252 byte in a value");
}

// The UTF-16 output of `; <comment>` above a one-key section.
std::string Utf16Of(const std::string& comment) {
    const std::vector<IniMutation> out =
        GenerateIniMutations("; " + comment + "\n[Main]\nK=1\n", {MutationKey{"Main", "K", "2", {}, false, {}}});
    for (const IniMutation& m : out) {
        if (m.name == "file: UTF-16 LE with a mark") return m.bytes;
    }
    throw std::runtime_error("no UTF-16 output");
}

std::string Utf16Expected(const std::vector<unsigned>& comment_units) {
    std::vector<unsigned> units = {';', ' '};
    units.insert(units.end(), comment_units.begin(), comment_units.end());
    for (const char c : std::string("\n[Main]\nK=1\n")) units.push_back(static_cast<unsigned char>(c));
    std::string bytes = "\xFF\xFE";
    for (const unsigned u : units) {
        bytes.push_back(static_cast<char>(u & 0xFF));
        bytes.push_back(static_cast<char>(u >> 8));
    }
    return bytes;
}

void TestUtf16Decoding() {
    Check(Utf16Of("\xE2\x82\xAC") == Utf16Expected({0x20AC}), "UTF-8 bytes are decoded as UTF-8");
    Check(Utf16Of("\xF0\x9F\x8E\xAE") == Utf16Expected({0xD83C, 0xDFAE}), "a code point above U+FFFF becomes a surrogate pair");
    Check(Utf16Of("\xC0\xAF") == Utf16Expected({0x00C0, 0x00AF}), "an overlong form is not UTF-8, so code page 1252");
    Check(Utf16Of("\xED\xA0\x80") == Utf16Expected({0x00ED, 0x00A0, 0x20AC}), "an encoded surrogate is not UTF-8");
    Check(Utf16Of("\xF4\x90\x80\x80") == Utf16Expected({0x00F4, 0x0090, 0x20AC, 0x20AC}), "a code point above U+10FFFF is not UTF-8");
    Check(Utf16Of("\xE2\x82") == Utf16Expected({0x00E2, 0x201A}), "a cut-off sequence is not UTF-8");

    // What .NET Framework's Encoding.GetEncoding(1252), which is Windows' table, gives for 0x80-0x9F.
    static const unsigned kWindows1252[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
        0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
        0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};
    std::string high;
    std::vector<unsigned> units;
    for (unsigned b = 0x80; b <= 0xFF; ++b) {
        high.push_back(static_cast<char>(b));
        units.push_back(b <= 0x9F ? kWindows1252[b - 0x80] : b);
    }
    Check(Utf16Of(high) == Utf16Expected(units), "every byte from 0x80 to 0xFF decodes as Windows code page 1252 does");
}

}  // namespace

int RunIniMutationsTests() {
    g_failures = 0;
    std::cout << "\nINI mutation corpus tests\n";
    try {
        TestSha256();
        std::vector<fs::path> cases;
        for (const fs::directory_entry& entry : fs::directory_iterator(Root())) {
            if (entry.is_directory()) cases.push_back(entry.path());
        }
        std::sort(cases.begin(), cases.end());
        Check(!cases.empty(), "the mutation fixtures are found");
        for (const fs::path& dir : cases) RunCase(dir);
        TestArguments();
        TestEmptyBase();
        TestUtf16Decoding();
    } catch (const std::exception& e) {
        std::cout << "  [FAIL] threw: " << e.what() << "\n";
        ++g_failures;
    }
    return g_failures;
}
