// EditIni against the byte fixtures in data/fixtures/canonical-ini/editor, which the C#
// IniEditorFixtures runs through IniEditor as well, so the two implementations are held to
// the same bytes. Each edited document is also read back through ParseCanonicalIni, before
// and after, as data/fixtures/canonical-ini/README.md describes.

#include <cameraunlock/config/canonical_ini.h>
#include <cameraunlock/config/ini_editor.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
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
using cameraunlock::config::CanonicalDiagnostic;
using cameraunlock::config::CanonicalIni;
using cameraunlock::config::ParseCanonicalIni;

// CameraUnlock.Core.Config.IniEditRefusal carries the same numbers.
static_assert(static_cast<int>(IniEditRefusal::None) == 0, "IniEditRefusal::None");
static_assert(static_cast<int>(IniEditRefusal::Utf16) == 1, "IniEditRefusal::Utf16");
static_assert(static_cast<int>(IniEditRefusal::NulByte) == 2, "IniEditRefusal::NulByte");
static_assert(static_cast<int>(IniEditRefusal::KeyNotFound) == 3, "IniEditRefusal::KeyNotFound");

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

// The byte escape data/fixtures/canonical-ini/README.md defines.
std::string Unescape(const std::string& field) {
    std::string bytes;
    for (size_t i = 0; i < field.size(); ++i) {
        if (field[i] != '\\') {
            bytes.push_back(field[i]);
            continue;
        }
        if (i + 1 < field.size() && field[i + 1] == '\\') {
            bytes.push_back('\\');
            ++i;
            continue;
        }
        if (i + 3 < field.size() && field[i + 1] == 'x') {
            bytes.push_back(static_cast<char>(std::stoi(field.substr(i + 2, 2), nullptr, 16)));
            i += 3;
            continue;
        }
        throw std::runtime_error("bad escape in fixture field '" + field + "'");
    }
    return bytes;
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
    std::string input;
    std::vector<IniEdit> edits;
    std::vector<IniEdit> rejected;
    bool first_mode = false;
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
    if (fs::exists(dir / "input.ini")) c.input = ReadBytes(dir / "input.ini");

    for (const std::string& line : Split(ReadBytes(dir / "case.tsv"), '\n', std::string::npos)) {
        if (line.empty() || line[0] == '#') continue;
        const std::string directive = Split(line, '\t', 2)[0];
        if (directive == "set" || directive == "set_or_insert" || directive == "set_first" ||
            directive == "set_or_insert_first" || directive == "rejects") {
            const auto f = Split(line, '\t', 4);
            if (f.size() != 4) throw std::runtime_error(c.name + ": malformed edit: " + line);
            const bool first = directive == "set_first" || directive == "set_or_insert_first";
            const IniEdit edit{Unescape(f[1]), Unescape(f[2]), Unescape(f[3]),
                               directive != "set" && directive != "set_first", first};
            if (directive == "rejects") {
                c.rejected.push_back(edit);
            } else {
                c.edits.push_back(edit);
                c.first_mode = c.first_mode || first;
            }
        } else if (directive == "refused") {
            const auto f = Split(line, '\t', 5);
            if (f.size() != 5) throw std::runtime_error(c.name + ": malformed refusal: " + line);
            c.refused = true;
            c.refusal = f[1];
            c.refused_section = Unescape(f[2]);
            c.refused_key = Unescape(f[3]);
            if (f[4] != "-") {
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

// Each line with its terminator, as the canonical reader splits them: CRLF, LF or a lone
// CR, after a UTF-8 byte order mark at offset 0.
std::vector<std::string> Lines(const std::string& bytes) {
    std::vector<std::string> lines;
    size_t pos = bytes.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
    while (pos < bytes.size()) {
        size_t end = pos;
        while (end < bytes.size() && bytes[end] != '\r' && bytes[end] != '\n') ++end;
        if (end < bytes.size()) {
            end += (bytes[end] == '\r' && end + 1 < bytes.size() && bytes[end + 1] == '\n') ? 2 : 1;
        }
        lines.push_back(bytes.substr(pos, end - pos));
        pos = end;
    }
    return lines;
}

std::string WithoutEnding(const std::string& line) {
    size_t end = line.size();
    while (end > 0 && (line[end - 1] == '\r' || line[end - 1] == '\n')) --end;
    return line.substr(0, end);
}

// An unterminated last line gains the line ending that joins new text onto it.
bool Kept(const std::string& after, const std::string& before) {
    return after == before || (WithoutEnding(before) == before && WithoutEnding(after) == before);
}

std::string Trim(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
    return text.substr(begin, end - begin);
}

std::string Content(const std::string& line) { return Trim(WithoutEnding(line)); }

// The edit a changed key line holds, or nullptr when the line is not a key line.
const IniEdit* EditOnLine(const FixtureCase& c, const std::string& line, std::string& value) {
    const std::string content = Content(line);
    const size_t equals = content.find('=');
    if (content.empty() || content[0] == '[' || content[0] == ';' || content[0] == '#' ||
        equals == std::string::npos) {
        return nullptr;
    }
    const std::string key = Trim(content.substr(0, equals));
    value = Trim(content.substr(equals + 1));
    for (const IniEdit& edit : c.edits) {
        if (EqualsAsciiIgnoreCase(edit.key, key)) return &edit;
    }
    return nullptr;
}

bool Edited(const FixtureCase& c, const std::string& section, const std::string& key) {
    for (const IniEdit& edit : c.edits) {
        if (EqualsAsciiIgnoreCase(edit.section, section) && EqualsAsciiIgnoreCase(edit.key, key)) {
            return true;
        }
    }
    return false;
}

bool TouchesAny(const CanonicalDiagnostic& d, const std::set<int>& lines) {
    for (int line : d.lines) {
        if (lines.count(line) != 0) return true;
    }
    return false;
}

void CheckReadBack(const FixtureCase& c, const std::string& output) {
    const CanonicalIni before = ParseCanonicalIni(c.input);
    const CanonicalIni after = ParseCanonicalIni(output);
    Check(before.IsReadable() && after.IsReadable(), c.name + ": both documents are readable");

    // Lines of `before` the batch replaced: the occurrence each edit targets.
    std::set<int> replaced;
    for (const IniEdit& edit : c.edits) {
        const auto* v = before.Find(edit.section, edit.key);
        if (v == nullptr) continue;
        replaced.insert(edit.first_occurrence_wins && !v->earlier_lines.empty() ? v->earlier_lines.front()
                                                                                : v->line);
    }

    // Walk both line lists: an unchanged line is the same bytes, a replaced one pairs with
    // its replacement, and anything else in `after` was inserted.
    const std::vector<std::string> b = Lines(c.input);
    const std::vector<std::string> a = Lines(output);
    std::map<int, int> moved;
    std::set<int> changed;
    size_t j = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        if (j < b.size() && replaced.count(static_cast<int>(j) + 1) != 0) {
            moved[static_cast<int>(j) + 1] = static_cast<int>(i) + 1;
            changed.insert(static_cast<int>(i) + 1);
            ++j;
        } else if (j < b.size() && Kept(a[i], b[j])) {
            moved[static_cast<int>(j) + 1] = static_cast<int>(i) + 1;
            ++j;
        } else {
            changed.insert(static_cast<int>(i) + 1);
        }
    }
    Check(j == b.size(), c.name + ": every line the batch did not replace is kept, in order");

    std::map<const IniEdit*, int> held;
    bool shaped = true;
    for (int number : changed) {
        const std::string& line = a[static_cast<size_t>(number) - 1];
        std::string value;
        const IniEdit* edit = EditOnLine(c, line, value);
        if (edit != nullptr) {
            shaped = shaped && value == edit->value;
            ++held[edit];
            continue;
        }
        const std::string content = Content(line);
        bool header = false;
        for (const IniEdit& e : c.edits) header = header || content == "[" + e.section + "]";
        shaped = shaped && (content.empty() || header);
    }
    for (const IniEdit& edit : c.edits) shaped = shaped && held[&edit] == 1;
    Check(shaped, c.name + ": each changed line is one edit's key with its new value, or a new "
                           "section's header or blank line");

    bool others = true;
    for (const auto& section : before.sections) {
        for (const auto& v : section.values) {
            if (Edited(c, section.name, v.key)) continue;
            const auto* now = after.Find(section.name, v.key);
            others = others && now != nullptr && now->key == v.key && now->value == v.value;
        }
    }
    for (const auto& section : after.sections) {
        for (const auto& v : section.values) {
            others = others && (Edited(c, section.name, v.key) || before.Find(section.name, v.key) != nullptr);
        }
    }
    Check(others, c.name + ": every key the batch did not edit reads as before");

    if (c.first_mode) return;

    bool reads = true;
    for (const IniEdit& edit : c.edits) {
        const auto* v = after.Find(edit.section, edit.key);
        reads = reads && v != nullptr && v->value == edit.value && changed.count(v->line) != 0;
    }
    Check(reads, c.name + ": each edited key reads its new value from the line the batch changed");

    std::vector<CanonicalDiagnostic> kept_before;
    for (const CanonicalDiagnostic& d : before.diagnostics) {
        if (TouchesAny(d, replaced)) continue;
        CanonicalDiagnostic mapped = d;
        for (int& line : mapped.lines) line = moved[line];
        kept_before.push_back(mapped);
    }
    std::vector<CanonicalDiagnostic> kept_after;
    for (const CanonicalDiagnostic& d : after.diagnostics) {
        if (!TouchesAny(d, changed)) kept_after.push_back(d);
    }
    bool same = kept_before.size() == kept_after.size();
    for (size_t i = 0; same && i < kept_before.size(); ++i) {
        const CanonicalDiagnostic& x = kept_before[i];
        const CanonicalDiagnostic& y = kept_after[i];
        same = x.kind == y.kind && x.lines == y.lines && x.section == y.section && x.key == y.key &&
               x.value == y.value;
    }
    Check(same, c.name + ": the diagnostics differ only on edited lines");
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

void TestPrintableAsciiBoundary() {
    std::cout << "EditIni writes exactly printable ASCII:\n";
    int wrong = 0;
    for (int b = 0; b <= 0xFF; ++b) {
        const std::string byte(1, static_cast<char>(b));
        const bool printable = b >= 0x20 && b <= 0x7E;
        if (Throws({{"General", "A", "x" + byte + "x"}}) != !printable) ++wrong;
        if (Throws({{"General", "K" + byte + "K", "1"}}) != (!printable || b == '=')) ++wrong;
        if (Throws({{"S" + byte + "S", "A", "1"}}) != (!printable || b == ']')) ++wrong;
    }
    Check(wrong == 0, "inside a value every byte from 0x20 to 0x7E is writable and no other is; "
                      "a key refuses '=' as well, and a section ']'");
}

void TestUnwritableEditsThrow() {
    std::cout << "EditIni argument checks:\n";
    Check(Throws({{"General", "A", "1"}, {"general", "a", "2"}}),
          "two edits of one key throw, whatever their case");
    Check(!Throws({{"General", "A", "1"}, {"Other", "A", "2"}}),
          "one key in two sections is two edits");
    Check(!Throws({{"General", "A", ""}}), "an empty value is writable");
    Check(!Throws({{"General", "A", "x;y #1 \"q\" a=b"}}), "';', '#', '=' and quotes are writable in a value");
}

void TestNoEditsKeepsBytes() {
    std::cout << "EditIni with no edits:\n";
    const std::string original = "\xEF\xBB\xBF[General]\r\nA=1\nB = 2 ; c\r\x1A\x80";
    const IniEditResult result = EditIni(original, {});
    Check(result.Succeeded() && result.bytes == original, "no edits returns the input unchanged");
    Check(EditIni("\xFF\xFE", {}).refusal == IniEditRefusal::Utf16,
          "no edits still refuses a document the reader cannot read");
}

void TestRefusalNames() {
    std::cout << "IniEditRefusalName:\n";
    Check(std::string(cameraunlock::IniEditRefusalName(IniEditRefusal::KeyNotFound)) == "KeyNotFound",
          "KeyNotFound is named as the fixtures spell it");
    bool threw = false;
    try {
        cameraunlock::IniEditRefusalName(static_cast<IniEditRefusal>(4));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    Check(threw, "a value with no enumerator throws");
}

}  // namespace

int RunIniEditorTests() {
    std::cout << "\n=== IniEditor Tests ===\n";
    TestFixtures();
    TestPrintableAsciiBoundary();
    TestUnwritableEditsThrow();
    TestNoEditsKeepsBytes();
    TestRefusalNames();
    return g_failures;
}
