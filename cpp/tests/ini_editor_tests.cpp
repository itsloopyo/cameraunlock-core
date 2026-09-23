// EditIni against the byte fixtures in data/fixtures/ini-editor, which IniEditorTests
// runs through the C# IniEditor as well, so the two implementations are held to the
// same bytes. Each edited document is also read back through ParseIniConfig: the edited
// keys carry their new values and every other key reads exactly as before.

#include <cameraunlock/config/head_tracking_config.h>
#include <cameraunlock/config/ini_editor.h>

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
using cameraunlock::EditIni;
using cameraunlock::IniEdit;
using cameraunlock::IniEditRefusal;
using cameraunlock::IniEditResult;

// CameraUnlock.Core.Config.IniEditRefusal carries the same numbers.
static_assert(static_cast<int>(IniEditRefusal::None) == 0, "IniEditRefusal::None");
static_assert(static_cast<int>(IniEditRefusal::Utf16) == 1, "IniEditRefusal::Utf16");
static_assert(static_cast<int>(IniEditRefusal::InvalidUtf8) == 2, "IniEditRefusal::InvalidUtf8");
static_assert(static_cast<int>(IniEditRefusal::NulByte) == 3, "IniEditRefusal::NulByte");
static_assert(static_cast<int>(IniEditRefusal::LoneCarriageReturn) == 4,
              "IniEditRefusal::LoneCarriageReturn");
static_assert(static_cast<int>(IniEditRefusal::DuplicateSection) == 5,
              "IniEditRefusal::DuplicateSection");
static_assert(static_cast<int>(IniEditRefusal::DuplicateKey) == 6, "IniEditRefusal::DuplicateKey");
static_assert(static_cast<int>(IniEditRefusal::KeyNotFound) == 7, "IniEditRefusal::KeyNotFound");
static_assert(static_cast<int>(IniEditRefusal::AmbiguousWhitespace) == 8,
              "IniEditRefusal::AmbiguousWhitespace");

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

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) throw std::runtime_error("cannot create " + path.string());
    out << bytes;
    out.close();
    if (!out) throw std::runtime_error("cannot write " + path.string());
}

std::vector<std::string> Split(const std::string& text, char separator, size_t max_fields) {
    std::vector<std::string> fields;
    size_t begin = 0;
    while (fields.size() + 1 < max_fields) {
        const size_t at = text.find(separator, begin);
        if (at == std::string::npos) break;
        fields.push_back(text.substr(begin, at - begin));
        begin = at + 1;
    }
    fields.push_back(text.substr(begin));
    return fields;
}

bool EqualsAsciiIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = a[i];
        char y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

struct FixtureCase {
    std::string name;
    bool has_input = false;
    std::string input;
    std::vector<IniEdit> edits;
    std::vector<IniEdit> rejected;
    bool refused = false;
    std::string refusal;
    std::string refused_section;
    std::string refused_key;
    std::vector<int> refused_lines;
    std::string expected;
};

FixtureCase LoadCase(const fs::path& dir) {
    FixtureCase c;
    c.name = dir.filename().string();
    c.has_input = fs::exists(dir / "input.ini");
    if (c.has_input) c.input = ReadBytes(dir / "input.ini");

    for (const std::string& line : Split(ReadBytes(dir / "case.tsv"), '\n', std::string::npos)) {
        if (line.empty() || line[0] == '#') continue;
        const std::string directive = Split(line, '\t', 2)[0];
        if (directive == "set" || directive == "set_or_insert") {
            const auto f = Split(line, '\t', 4);
            if (f.size() != 4) throw std::runtime_error(c.name + ": malformed edit: " + line);
            c.edits.push_back(IniEdit{f[1], f[2], f[3], directive == "set_or_insert"});
        } else if (directive == "rejects") {
            const auto f = Split(line, '\t', 4);
            if (f.size() != 4) throw std::runtime_error(c.name + ": malformed rejection: " + line);
            c.rejected.push_back(IniEdit{f[1], f[2], f[3], true});
        } else if (directive == "flat_duplicate") {
            // The C++ reader returns every occurrence, so the list comparison below
            // already covers a key it sees twice.
        } else if (directive == "refused") {
            const auto f = Split(line, '\t', 5);
            if (f.size() != 5) throw std::runtime_error(c.name + ": malformed refusal: " + line);
            c.refused = true;
            c.refusal = f[1];
            c.refused_section = f[2];
            c.refused_key = f[3];
            if (!f[4].empty()) {
                for (const std::string& n : Split(f[4], ',', std::string::npos)) {
                    c.refused_lines.push_back(std::stoi(n));
                }
            }
        } else {
            throw std::runtime_error(c.name + ": unknown directive '" + directive + "'");
        }
    }
    if (!c.refused) c.expected = ReadBytes(dir / "expected.ini");
    return c;
}

std::vector<std::pair<std::string, std::string>> ReadFlat(const std::string& bytes,
                                                          const std::string& tag) {
    const fs::path path = fs::temp_directory_path() / ("cameraunlock_ini_editor_" + tag + ".ini");
    WriteBytes(path, bytes);
    auto values = cameraunlock::ParseIniConfig(path.string());
    fs::remove(path);
    return values;
}

std::vector<std::string> ValuesOf(const std::vector<std::pair<std::string, std::string>>& pairs,
                                  const std::string& key) {
    std::vector<std::string> values;
    for (const auto& pair : pairs) {
        if (EqualsAsciiIgnoreCase(pair.first, key)) values.push_back(pair.second);
    }
    return values;
}

// The edited key reads back with its new value: in place of one earlier value when it was
// replaced, or as one more occurrence when it was inserted. Its other occurrences, which a
// flat reader also sees, are unchanged.
bool ReadsBackEdited(const std::vector<std::string>& before, const std::vector<std::string>& after,
                     const std::string& value) {
    if (std::find(after.begin(), after.end(), value) == after.end()) return false;
    if (after.size() == before.size()) {
        int differing = 0;
        for (size_t i = 0; i < after.size(); ++i) {
            if (after[i] != before[i]) {
                if (after[i] != value) return false;
                ++differing;
            }
        }
        return differing <= 1;
    }
    if (after.size() != before.size() + 1) return false;
    for (size_t skip = 0; skip < after.size(); ++skip) {
        if (after[skip] != value) continue;
        std::vector<std::string> rest = after;
        rest.erase(rest.begin() + static_cast<std::ptrdiff_t>(skip));
        if (rest == before) return true;
    }
    return false;
}

void CheckReadBack(const FixtureCase& c, const std::string& output) {
    const auto before = ReadFlat(c.input, c.name + "_in");
    const auto after = ReadFlat(output, c.name + "_out");

    auto edited = [&](const std::string& key) {
        for (const IniEdit& edit : c.edits) {
            if (EqualsAsciiIgnoreCase(edit.key, key)) return true;
        }
        return false;
    };
    std::vector<std::pair<std::string, std::string>> before_rest;
    std::vector<std::pair<std::string, std::string>> after_rest;
    for (const auto& pair : before) {
        if (!edited(pair.first)) before_rest.push_back(pair);
    }
    for (const auto& pair : after) {
        if (!edited(pair.first)) after_rest.push_back(pair);
    }
    Check(before_rest == after_rest, c.name + ": every unedited key reads back unchanged");

    for (const IniEdit& edit : c.edits) {
        Check(ReadsBackEdited(ValuesOf(before, edit.key), ValuesOf(after, edit.key), edit.value),
              c.name + ": " + edit.key + " reads back as '" + edit.value + "'");
    }
}

bool ThrowsInvalidArgument(const std::string& input, const IniEdit& edit) {
    try {
        EditIni(input, {edit});
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

void RunFixture(const FixtureCase& c) {
    for (const IniEdit& rejected : c.rejected) {
        Check(ThrowsInvalidArgument(c.input, rejected),
              c.name + ": [" + rejected.section + "] " + rejected.key + "=" + rejected.value +
                  " is rejected");
    }
    const IniEditResult result = EditIni(c.input, c.edits);
    if (c.refused) {
        Check(!result.Succeeded(), c.name + ": refused");
        Check(std::string(cameraunlock::IniEditRefusalName(result.refusal)) == c.refusal,
              c.name + ": refusal is " + c.refusal + " (got " +
                  cameraunlock::IniEditRefusalName(result.refusal) + ")");
        Check(result.section == c.refused_section && result.key == c.refused_key,
              c.name + ": refusal names [" + c.refused_section + "] " + c.refused_key);
        Check(result.lines == c.refused_lines, c.name + ": refusal names the expected lines");
        Check(result.bytes.empty(), c.name + ": a refusal produces no bytes");
        return;
    }
    Check(result.Succeeded(), c.name + ": succeeds (refusal " +
                                  cameraunlock::IniEditRefusalName(result.refusal) + ")");
    Check(result.bytes == c.expected, c.name + ": output matches expected.ini byte for byte");
    if (result.Succeeded()) CheckReadBack(c, result.bytes);
}

void TestFixtures() {
    std::cout << "EditIni byte fixtures:\n";
    const fs::path root(CAMERAUNLOCK_INI_EDITOR_FIXTURES);
    std::vector<fs::path> dirs;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (entry.is_directory()) dirs.push_back(entry.path());
    }
    std::sort(dirs.begin(), dirs.end());
    Check(!dirs.empty(), "fixtures found under " + root.string());
    for (const fs::path& dir : dirs) RunFixture(LoadCase(dir));
}

bool Throws(const std::vector<IniEdit>& edits) {
    try {
        EditIni("[General]\nA=1\n", edits);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

void TestUnwritableEditsThrow() {
    std::cout << "EditIni argument checks:\n";
    Check(Throws({{"", "A", "1"}}), "empty section throws");
    Check(Throws({{"General", "", "1"}}), "empty key throws");
    Check(Throws({{" General", "A", "1"}}), "section with leading whitespace throws");
    Check(Throws({{"General", "A\t", "1"}}), "key with trailing whitespace throws");
    Check(Throws({{"Gen]eral", "A", "1"}}), "section holding ] throws");
    Check(Throws({{"General", "A=B", "1"}}), "key holding = throws");
    Check(Throws({{"General", ";A", "1"}}), "key starting ; throws");
    Check(Throws({{"General", "#A", "1"}}), "key starting # throws");
    Check(Throws({{"General", "[A", "1"}}), "key starting [ throws");
    Check(Throws({{"General", "A", "1\n2"}}), "value holding LF throws");
    Check(Throws({{"General", "A", "1\r"}}), "value holding CR throws");
    Check(Throws({{"General", "A", std::string("1\0", 2)}}), "value holding NUL throws");
    Check(Throws({{"General", "A", "\xC3"}}), "value holding invalid UTF-8 throws");
    Check(Throws({{"General", "A", "1"}, {"general", "a", "2"}}),
          "two edits of one key throw, whatever their case");
    Check(!Throws({{"General", "A", ""}}), "an empty value is writable");
}

// Every value either throws or reads back through ParseIniConfig as itself, and editing
// the output again with the same value changes nothing.
void TestAcceptedValuesReadBackAndReapplyUnchanged() {
    std::cout << "EditIni value round trip:\n";
    const std::vector<std::string> inputs = {"[S]\nKey=1\n", "[S]\nKey = 1 ; comment\n",
                                             "[S]\nKey=\"a;b\"#c"};
    const std::vector<std::string> alphabet = {"a", " ", ";", "#", "\"", "'", "=", "\xC2\xA0"};
    std::vector<std::string> values = {""};
    for (size_t start = 0, length = 1; length <= 4; ++length) {
        const size_t end = values.size();
        for (size_t i = start; i < end; ++i) {
            for (const std::string& c : alphabet) values.push_back(values[i] + c);
        }
        start = end;
    }

    int accepted = 0;
    int wrong = 0;
    for (const std::string& input : inputs) {
        for (const std::string& value : values) {
            const std::vector<IniEdit> edit = {{"S", "Key", value, false}};
            std::string once;
            try {
                once = EditIni(input, edit).bytes;
            } catch (const std::invalid_argument&) {
                continue;
            }
            ++accepted;
            const auto read = ReadFlat(once, "round_trip");
            const bool reads_back = read.size() == 1 && read[0].second == value;
            const bool stable = EditIni(once, edit).bytes == once;
            if (!reads_back || !stable) {
                std::cout << "    value '" << value << "' on '" << input << "' gives '" << once
                          << "'\n";
                ++wrong;
            }
        }
    }
    Check(accepted > 100, "more than 100 values accepted (" + std::to_string(accepted) + ")");
    Check(wrong == 0, "every accepted value reads back as itself and re-applies unchanged");
}

void TestNoEditsKeepsBytes() {
    std::cout << "EditIni with no edits:\n";
    const std::string original = "\xEF\xBB\xBF[General]\r\nA=1\nB = 2 ; c";
    const IniEditResult result = EditIni(original, {});
    Check(result.Succeeded() && result.bytes == original, "no edits returns the input unchanged");
    Check(EditIni("\xFF\xFE", {}).refusal == IniEditRefusal::Utf16,
          "no edits still refuses an unsupported encoding");
}

}  // namespace

int RunIniEditorTests() {
    std::cout << "\n=== IniEditor Tests ===\n";
    TestFixtures();
    TestUnwritableEditsThrow();
    TestAcceptedValuesReadBackAndReapplyUnchanged();
    TestNoEditsKeepsBytes();
    return g_failures;
}
