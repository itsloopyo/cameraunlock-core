// ParseCanonicalIni and HasCanonicalStamp against data/fixtures/canonical-ini/reader, which
// the C# CanonicalIniFixtures runs through CanonicalIni as well, plus the API around them.

#include <cameraunlock/config/canonical_ini.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using cameraunlock::config::CanonicalDiagnostic;
using cameraunlock::config::CanonicalDiagnosticKind;
using cameraunlock::config::CanonicalIni;
using cameraunlock::config::CanonicalReadStatus;
using cameraunlock::config::CanonicalSection;
using cameraunlock::config::CanonicalValue;
using cameraunlock::config::DescribeCanonicalDiagnostic;
using cameraunlock::config::HasCanonicalStamp;
using cameraunlock::config::ParseCanonicalIni;

// CameraUnlock.Core.Config carries the same numbers.
static_assert(cameraunlock::config::kConfigFormat == 1, "kConfigFormat");
static_assert(static_cast<int>(CanonicalReadStatus::Readable) == 0, "CanonicalReadStatus::Readable");
static_assert(static_cast<int>(CanonicalReadStatus::Utf16) == 1, "CanonicalReadStatus::Utf16");
static_assert(static_cast<int>(CanonicalReadStatus::NulByte) == 2, "CanonicalReadStatus::NulByte");
static_assert(static_cast<int>(CanonicalDiagnosticKind::TextAfterSectionHeader) == 1,
              "CanonicalDiagnosticKind::TextAfterSectionHeader");
static_assert(static_cast<int>(CanonicalDiagnosticKind::UnclosedSectionHeader) == 2,
              "CanonicalDiagnosticKind::UnclosedSectionHeader");
static_assert(static_cast<int>(CanonicalDiagnosticKind::EmptySectionName) == 3,
              "CanonicalDiagnosticKind::EmptySectionName");
static_assert(static_cast<int>(CanonicalDiagnosticKind::EmptyKey) == 4, "CanonicalDiagnosticKind::EmptyKey");
static_assert(static_cast<int>(CanonicalDiagnosticKind::MissingEquals) == 5,
              "CanonicalDiagnosticKind::MissingEquals");
static_assert(static_cast<int>(CanonicalDiagnosticKind::KeyOutsideSection) == 6,
              "CanonicalDiagnosticKind::KeyOutsideSection");
static_assert(static_cast<int>(CanonicalDiagnosticKind::DuplicateKey) == 7,
              "CanonicalDiagnosticKind::DuplicateKey");
static_assert(static_cast<int>(CanonicalDiagnosticKind::ConfigFormatMissing) == 8,
              "CanonicalDiagnosticKind::ConfigFormatMissing");
static_assert(static_cast<int>(CanonicalDiagnosticKind::ConfigFormatInvalid) == 9,
              "CanonicalDiagnosticKind::ConfigFormatInvalid");
static_assert(static_cast<int>(CanonicalDiagnosticKind::ConfigFormatNewer) == 10,
              "CanonicalDiagnosticKind::ConfigFormatNewer");

int g_failures = 0;

void Check(bool cond, const std::string& name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) throw std::runtime_error("cannot open fixture " + path.string());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The byte escape data/fixtures/canonical-ini/README.md defines.
std::string Escape(const std::string& bytes) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string text;
    for (char c : bytes) {
        const unsigned char b = static_cast<unsigned char>(c);
        if (b == '\\') {
            text += "\\\\";
        } else if (b >= 0x20 && b <= 0x7E) {
            text.push_back(c);
        } else {
            text += "\\x";
            text.push_back(kHex[b >> 4]);
            text.push_back(kHex[b & 0xF]);
        }
    }
    return text;
}

std::string LineList(const std::vector<int>& lines) {
    if (lines.empty()) return "-";
    std::string text;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) text.push_back(',');
        text += std::to_string(lines[i]);
    }
    return text;
}

std::vector<std::string> Rows(const std::string& input) {
    const CanonicalIni doc = ParseCanonicalIni(input);
    std::vector<std::string> rows;
    std::string status = std::string("status\t") + cameraunlock::config::CanonicalReadStatusName(doc.status);
    if (doc.status == CanonicalReadStatus::NulByte) status += "\t" + std::to_string(doc.unreadable_line);
    rows.push_back(status);
    rows.push_back("format\t" + std::to_string(doc.format_version));
    rows.push_back(std::string("stamp\t") + (HasCanonicalStamp(input) ? "true" : "false"));

    std::vector<std::pair<int, std::string>> keys;
    for (const CanonicalSection& section : doc.sections) {
        for (const CanonicalValue& value : section.values) {
            keys.emplace_back(value.line, "key\t" + Escape(section.name) + "\t" + Escape(value.key) + "\t" +
                                              Escape(value.value) + "\t" + std::to_string(value.line) + "\t" +
                                              LineList(value.earlier_lines));
        }
    }
    std::sort(keys.begin(), keys.end());
    for (const auto& key : keys) rows.push_back(key.second);

    for (const CanonicalDiagnostic& d : doc.diagnostics) {
        rows.push_back(std::string("diagnostic\t") + cameraunlock::config::CanonicalDiagnosticKindName(d.kind) +
                       "\t" + LineList(d.lines));
    }
    return rows;
}

std::vector<std::string> ExpectedRows(const std::string& tsv) {
    std::vector<std::string> rows;
    size_t begin = 0;
    while (begin < tsv.size()) {
        size_t end = tsv.find('\n', begin);
        if (end == std::string::npos) end = tsv.size();
        const std::string line = tsv.substr(begin, end - begin);
        if (!line.empty() && line[0] != '#') rows.push_back(line);
        begin = end + 1;
    }
    return rows;
}

void RunFixture(const fs::path& dir) {
    const std::string name = dir.filename().string();
    const std::string input = ReadBytes(dir / "input.ini");
    const std::vector<std::string> expected = ExpectedRows(ReadBytes(dir / "expected.tsv"));
    const std::vector<std::string> actual = Rows(input);
    const bool same = actual == expected;
    Check(same, name + ": reads as expected.tsv");
    if (!same) {
        std::cout << "    expected:\n";
        for (const std::string& row : expected) std::cout << "      " << row << "\n";
        std::cout << "    actual:\n";
        for (const std::string& row : actual) std::cout << "      " << row << "\n";
    }

    const CanonicalIni doc = ParseCanonicalIni(input);
    if (doc.IsReadable()) {
        Check(HasCanonicalStamp(input) == (doc.FindSection("CameraUnlock") != nullptr),
              name + ": the stamp is exactly a [CameraUnlock] section the reader opens");
    }
}

void TestFixtures() {
    std::cout << "Canonical INI reader fixtures:\n";
    const fs::path root = fs::path(CAMERAUNLOCK_CANONICAL_INI_FIXTURES) / "reader";
    std::vector<fs::path> dirs;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (entry.is_directory()) dirs.push_back(entry.path());
    }
    std::sort(dirs.begin(), dirs.end());
    Check(!dirs.empty(), "fixtures found under " + root.string());
    for (const fs::path& dir : dirs) RunFixture(dir);
}

void TestLookup() {
    std::cout << "Canonical INI lookup:\n";
    const CanonicalIni doc = ParseCanonicalIni(
        "[General]\r\nToggleKey=End\r\n[]\r\nA=1\r\n[Caf\xC3\xA9]\r\n\xC3\xA9t\xC3\xA9=1\r\nToggleKey=Home\r\n"
        "togglekey=Insert\r\n");
    const CanonicalValue* toggle = doc.Find("gEnErAl", "TOGGLEKEY");
    Check(toggle != nullptr && toggle->value == "End" && toggle->line == 2,
          "section and key match ASCII case-insensitively");
    Check(doc.Find("General", "Toggle") == nullptr, "a prefix of a key does not match");
    Check(doc.Find("General", "ToggleKey ") == nullptr, "names are not trimmed at lookup");
    Check(doc.FindSection("") == nullptr, "an empty section name matches nothing, not even []");
    Check(doc.FindSection("CAF\xC3\xA9") != nullptr, "the ASCII letters of a non-ASCII name fold");
    Check(doc.FindSection("CAF\xC3\x89") == nullptr, "a non-ASCII letter does not fold");
    Check(doc.Find("Caf\xC3\xA9", "\xC3\xA9t\xC3\xA9") != nullptr &&
              doc.Find("Caf\xC3\xA9", "\xC3\x89t\xC3\xA9") == nullptr,
          "non-ASCII key bytes compare exactly");
    const CanonicalValue* last = doc.Find("caf\xC3\xA9", "ToggleKey");
    Check(last != nullptr && last->key == "togglekey" && last->value == "Insert" && last->line == 8 &&
              last->earlier_lines == std::vector<int>{7},
          "a repeated key keeps its last occurrence's spelling, value and line, and the earlier lines");
    const CanonicalSection* general = doc.FindSection("general");
    Check(general != nullptr && general->name == "General" && general->Find("togglekey") == toggle,
          "a section finds its own keys the same way");
    Check(doc.sections.size() == 2, "[] opens no section");
}

void TestUnreadable() {
    std::cout << "Canonical INI unreadable documents:\n";
    const CanonicalIni utf16 = ParseCanonicalIni(std::string("\xFF\xFE[\0G\0]\0", 8));
    Check(utf16.status == CanonicalReadStatus::Utf16 && !utf16.IsReadable() && utf16.format_version == 0 &&
              utf16.unreadable_line == 0 && utf16.sections.empty() && utf16.diagnostics.empty(),
          "UTF-16 is unreadable and nothing in it is interpreted");
    const std::string with_nul("[General]\nA=1\n=\n\0", 17);
    const CanonicalIni nul = ParseCanonicalIni(with_nul);
    Check(nul.status == CanonicalReadStatus::NulByte && nul.unreadable_line == 4 && nul.format_version == 0 &&
              nul.sections.empty() && nul.diagnostics.empty(),
          "a NUL makes the document unreadable, names its line and reports nothing else");
    Check(ParseCanonicalIni("").IsReadable() && ParseCanonicalIni("").format_version == 1,
          "an empty document is readable and reads as the current format");
}

CanonicalDiagnostic Only(const std::string& input, CanonicalDiagnosticKind kind) {
    const CanonicalIni doc = ParseCanonicalIni(input);
    for (const CanonicalDiagnostic& d : doc.diagnostics) {
        if (d.kind == kind) return d;
    }
    throw std::runtime_error(std::string("no ") + cameraunlock::config::CanonicalDiagnosticKindName(kind) +
                             " diagnostic for " + input);
}

bool Is(const CanonicalDiagnostic& d, std::vector<int> lines, const std::string& section, const std::string& key,
        const std::string& value) {
    return d.lines == lines && d.section == section && d.key == key && d.value == value;
}

void TestDiagnostics() {
    std::cout << "Canonical INI diagnostics:\n";
    using K = CanonicalDiagnosticKind;
    struct Case {
        std::string input;
        K kind;
        std::vector<int> lines;
        std::string section;
        std::string key;
        std::string value;
        std::string sentence;
    };
    const std::vector<Case> cases = {
        {"[General]  ; note\n", K::TextAfterSectionHeader, {1}, "General", "", "; note",
         "Line 1: \"; note\" after [General] is ignored. Comments go on their own line."},
        {"[General]\n [Position \n", K::UnclosedSectionHeader, {2}, "", "", "[Position",
         "Line 2: \"[Position\" has no closing ], so the settings below it are ignored up to the next section "
         "header."},
        {"[ ]\n", K::EmptySectionName, {1}, "", "", "[ ]",
         "Line 1: \"[ ]\" names no section, so the settings below it are ignored up to the next section header."},
        {"[General]\n = 5\n", K::EmptyKey, {2}, "", "", "= 5",
         "Line 2: \"= 5\" has no setting name before the =, so it is ignored."},
        {"[General]\n\tJust text\t\n", K::MissingEquals, {2}, "", "", "Just text",
         "Line 2: \"Just text\" is not a setting (it has no =), so it is ignored."},
        {"ToggleKey = End\n", K::KeyOutsideSection, {1}, "", "ToggleKey", "End",
         "Line 1: ToggleKey is not under a section header, so it is ignored."},
        {"[General]\nA=1\nA=2\n[general]\na=3\n", K::DuplicateKey, {2, 3, 5}, "General", "a", "3",
         "[General] a is set on lines 2, 3 and 5. Line 5 is used."},
        {"[General]\nA=1\n[General]\nA=2\n", K::DuplicateKey, {2, 4}, "General", "A", "2",
         "[General] A is set on lines 2 and 4. Line 4 is used."},
        {"; x\n[cameraUnlock]\n", K::ConfigFormatMissing, {2}, "cameraUnlock", "", "",
         "Line 2: [cameraUnlock] has no ConfigFormat, so the file is read as format 1."},
        {"[CameraUnlock]\nconfigformat = v1\n", K::ConfigFormatInvalid, {2}, "CameraUnlock", "configformat", "v1",
         "Line 2: configformat=v1 is not a format number, so the file is read as format 1."},
        {"[CameraUnlock]\nConfigFormat=7\n", K::ConfigFormatNewer, {2}, "CameraUnlock", "ConfigFormat", "7",
         "Line 2: ConfigFormat=7 was written by a newer version of the mod. This version reads format 1."},
    };
    for (const Case& c : cases) {
        const std::string name = cameraunlock::config::CanonicalDiagnosticKindName(c.kind);
        const CanonicalDiagnostic d = Only(c.input, c.kind);
        Check(Is(d, c.lines, c.section, c.key, c.value), name + ": lines, section, key and value");
        const std::string sentence = DescribeCanonicalDiagnostic(d);
        Check(sentence == c.sentence, name + ": " + sentence);
    }

    const CanonicalIni newer = ParseCanonicalIni("[CameraUnlock]\nConfigFormat=7\n");
    Check(newer.format_version == 7, "a newer format is kept as read");

    bool threw = false;
    try {
        cameraunlock::config::CanonicalDiagnosticKindName(static_cast<CanonicalDiagnosticKind>(0));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    Check(threw, "a kind with no name throws rather than inventing one");
}

void TestStamp() {
    std::cout << "Canonical INI stamp:\n";
    Check(HasCanonicalStamp("[General]\n[CameraUnlock]\n"), "a stamp after another section counts");
    Check(!HasCanonicalStamp("[CameraUnlocked]\n"), "a longer name is not the stamp");
    Check(!HasCanonicalStamp("CameraUnlock=1\n"), "a key named CameraUnlock is not the stamp");
    Check(HasCanonicalStamp(std::string("[CameraUnlock]\n\0\0", 17)), "a NUL does not hide the stamp");
    Check(HasCanonicalStamp(std::string("\xFF\xFE[\0C\0a\0m\0e\0r\0a\0U\0n\0l\0o\0c\0k\0]\0", 30)),
          "the stamp is found in UTF-16 LE");
    Check(!HasCanonicalStamp(std::string("\xFE\xFF[\0C\0a\0m\0e\0r\0a\0U\0n\0l\0o\0c\0k\0]\0", 30)),
          "little-endian units under a big-endian mark are not the stamp");
    Check(!HasCanonicalStamp("\xFF\xFE"), "a bare UTF-16 mark is not stamped");
}

}  // namespace

int RunCanonicalIniTests() {
    std::cout << "\n=== Canonical INI Tests ===\n";
    TestFixtures();
    TestLookup();
    TestUnreadable();
    TestDiagnostics();
    TestStamp();
    return g_failures;
}
