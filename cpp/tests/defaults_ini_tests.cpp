// Defaults.ini against data/fixtures/canonical-ini/global, which the C# DefaultsIniFixtures runs
// too, plus the key names the header lists against data/keys.json.

#include <cameraunlock/config/defaults_ini.h>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace cameraunlock::config;
using detail::DefaultsIniSnapshot;
using detail::DefaultsIniValue;
using detail::DefaultsIniValueState;
using schema::Concept;

int g_failures = 0;

void Check(bool cond, const std::string& name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

fs::path Root() { return fs::path(CAMERAUNLOCK_CANONICAL_INI_FIXTURES); }

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) throw std::runtime_error("cannot open " + path.string());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<std::string> SplitAt(const std::string& text, const std::string& separator) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = text.find(separator, start);
        if (end == std::string::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, end - start));
        start = end + separator.size();
    }
}

// The byte escape data/fixtures/canonical-ini/README.md defines.
std::string Escape(const std::string& bytes) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string text;
    for (const char c : bytes) {
        const auto b = static_cast<unsigned char>(c);
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

std::vector<std::string> Rows(const DefaultsIniSnapshot& snapshot) {
    std::vector<std::string> rows;
    if (snapshot.unreadable) {
        rows.push_back("unreadable\t" + Escape(*snapshot.unreadable));
        return rows;
    }
    if (snapshot.format_line) rows.push_back("format\t" + Escape(*snapshot.format_line));

    const DefaultsIniSnapshot built_ins = detail::ReadDefaultsIni(detail::RenderDefaultsIni());
    std::vector<std::string> lines;
    for (const schema::ConceptInfo& info : schema::kConcepts) {
        const DefaultsIniValue& value = snapshot.Value(info.id);
        const std::string row = std::string("value\t") + info.name + "\t";
        if (value.state == DefaultsIniValueState::kAbsent) continue;
        if (value.state == DefaultsIniValueState::kAccepted) {
            rows.push_back(row + "accepted\t" + std::to_string(value.line) + "\t" + Escape(value.value));
            continue;
        }
        rows.push_back(row + "refused\t" + std::to_string(value.line) + "\t" + Escape(value.section) + "\t" +
                       Escape(value.key) + "\t" + Escape(value.value) + "\t" + Escape(value.reason));
        const bool pair_row = info.id == Concept::RotationEnabled || info.id == Concept::PositionEnabled;
        if (snapshot.pair_refused && pair_row) continue;
        lines.push_back(std::string("line\t") + info.name + "\t" +
                        Escape(detail::DefaultsIniRefusedLine(value, built_ins.Value(info.id).value)));
    }
    rows.insert(rows.end(), lines.begin(), lines.end());
    if (snapshot.pair_refused) {
        rows.push_back("pair\t" + Escape(detail::DefaultsIniPairLine(snapshot,
                                                                     built_ins.Value(Concept::RotationEnabled).value,
                                                                     built_ins.Value(Concept::PositionEnabled).value)));
    }
    return rows;
}

std::vector<std::string> ExpectedRows(const fs::path& path) {
    std::vector<std::string> rows;
    for (const std::string& line : SplitAt(ReadBytes(path), "\n")) {
        if (!line.empty() && line[0] != '#') rows.push_back(line);
    }
    return rows;
}

std::string Joined(const std::vector<std::string>& rows) {
    std::string text;
    for (const std::string& row : rows) text += "\n      " + row;
    return text;
}

void TestRender() {
    std::cout << "\n[the global table renders global/Defaults.ini]\n";
    const std::string rendered = detail::RenderDefaultsIni();
    Check(rendered == ReadBytes(Root() / "global" / "Defaults.ini"), "RenderDefaultsIni gives global/Defaults.ini");

    const DefaultsIniSnapshot snapshot = detail::ReadDefaultsIni(rendered);
    Check(!snapshot.unreadable && !snapshot.format_line && !snapshot.pair_refused,
          "Defaults.ini reads back with nothing to say");
    const ConfigTable<cameraunlock::HeadTrackingConfig> table = detail::DefaultsIniTable();
    const std::string values = RenderCanonical(table, table.defaults(), RenderHeader{"Fixture Game"});
    bool accepted = true;
    bool absent = true;
    for (const schema::ConceptInfo& info : schema::kConcepts) {
        const DefaultsIniValue& value = snapshot.Value(info.id);
        if (!info.global) {
            absent = absent && value.state == DefaultsIniValueState::kAbsent && rendered.find(info.key) == std::string::npos;
            continue;
        }
        const std::string row = std::string(info.key) + "=" + value.value + "\r\n";
        accepted = accepted && value.state == DefaultsIniValueState::kAccepted && values.find("\r\n" + row) != std::string::npos;
    }
    Check(accepted, "every global concept reads back accepted, at the global table's default");
    Check(absent, "a concept that is not global has no line and reads back absent");

    cameraunlock::HeadTrackingConfig config;
    const CanonicalIni doc = ParseCanonicalIni(rendered);
    const ApplyReport report = ApplyCanonical(doc, table, config);
    Check(doc.diagnostics.empty() && report.diagnostics.empty(),
          "Defaults.ini draws no diagnostic from the reader or the global table");
}

void TestCases() {
    std::cout << "\n[global/read-* fixtures]\n";
    std::vector<std::string> names;
    for (const fs::directory_entry& entry : fs::directory_iterator(Root() / "global")) {
        const std::string name = entry.path().filename().string();
        if (entry.is_directory() && name.rfind("read-", 0) == 0) names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    Check(!names.empty(), "there are global/read-* fixtures");
    for (const std::string& name : names) {
        const fs::path dir = Root() / "global" / name;
        const std::vector<std::string> expected = ExpectedRows(dir / "expected.tsv");
        const std::vector<std::string> actual = Rows(detail::ReadDefaultsIni(ReadBytes(dir / "input.ini")));
        Check(actual == expected,
              name + (actual == expected ? "" : "\n    expected:" + Joined(expected) + "\n    actual:" + Joined(actual)));
    }
}

void TestLineArguments() {
    std::cout << "\n[the line functions refuse what is not refused]\n";
    const DefaultsIniSnapshot snapshot = detail::ReadDefaultsIni("[Network]\r\nUdpPort=5000\r\n");
    bool threw = false;
    try {
        detail::DefaultsIniRefusedLine(snapshot.Value(Concept::UdpPort), "4242");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    Check(threw, "DefaultsIniRefusedLine throws for an accepted value");
    threw = false;
    try {
        detail::DefaultsIniPairLine(snapshot, "true", "true");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    Check(threw, "DefaultsIniPairLine throws for a snapshot whose pair is not refused");
}

// The key names the rendered header lists, with each "X to Y" range written out: A to Z by letter,
// the others by the number after a shared prefix.
std::vector<std::string> HeaderKeyNames(const std::string& rendered) {
    std::string all;
    for (const std::string& line : SplitAt(rendered, "\r\n")) {
        if (line.empty()) break;
        all += line.substr(2) + " ";
    }
    const std::string start_text = "Only these key names are read here: ";
    const std::size_t start = all.find(start_text);
    const std::size_t end = all.find(". A value holding any other key");
    if (start == std::string::npos || end == std::string::npos || end < start) {
        throw std::runtime_error("the header lists no key names");
    }

    std::vector<std::string> names;
    for (const std::string& item : SplitAt(all.substr(start + start_text.size(), end - start - start_text.size()), ", ")) {
        const std::vector<std::string> range = SplitAt(item, " to ");
        if (range.size() == 1) {
            names.push_back(item);
        } else if (range[0].size() == 1 && range[1].size() == 1) {
            for (char c = range[0][0]; c <= range[1][0]; ++c) names.push_back(std::string(1, c));
        } else {
            const std::string prefix = range[0].substr(0, range[0].find_first_of("0123456789"));
            if (prefix.empty() || range[1].rfind(prefix, 0) != 0) {
                throw std::runtime_error("'" + item + "' is not a range of letters or of numbered names");
            }
            const int first = std::stoi(range[0].substr(prefix.size()));
            const int last = std::stoi(range[1].substr(prefix.size()));
            for (int n = first; n <= last; ++n) names.push_back(prefix + std::to_string(n));
        }
    }
    return names;
}

// data/keys.json's key names, split into the ones Defaults.ini takes, those with a Windows
// virtual-key code that are not a Ctrl, Shift or Alt key, and the rest with every alias, none of
// which the header lists.
struct KeyTable {
    std::set<std::string> taken;
    std::set<std::string> not_taken;
};

KeyTable ReadKeyTable() {
    const std::string json = ReadBytes(Root().parent_path().parent_path() / "keys.json");
    const std::size_t keys = json.find("\"keys\"");
    if (keys == std::string::npos) throw std::runtime_error("data/keys.json has no keys array");
    const std::size_t modifiers = json.find("\"modifiers\"");
    if (modifiers == std::string::npos || modifiers > keys) {
        throw std::runtime_error("data/keys.json has no modifiers before its keys");
    }
    std::set<std::string> modifier_keys;
    const std::string modifier_text = json.substr(modifiers, keys - modifiers);
    const std::regex sides(R"re("unity"\s*:\s*\[\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\])re");
    for (std::sregex_iterator it(modifier_text.begin(), modifier_text.end(), sides), end; it != end; ++it) {
        modifier_keys.insert((*it)[1].str());
        modifier_keys.insert((*it)[2].str());
    }
    if (modifier_keys.size() != 6) throw std::runtime_error("data/keys.json's modifiers do not name six keys");

    KeyTable table;
    const std::regex entry(R"re(\{\s*"name"\s*:\s*"([^"]+)"([^}]*)\})re");
    const std::regex alias(R"re("([^"]+)")re");
    const std::string rest = json.substr(keys);
    for (std::sregex_iterator it(rest.begin(), rest.end(), entry), end; it != end; ++it) {
        const std::string name = (*it)[1].str();
        const std::string fields = (*it)[2].str();
        const bool vk = fields.find("\"vk\"") != std::string::npos;
        (vk && modifier_keys.count(name) == 0 ? table.taken : table.not_taken).insert(name);
        const std::size_t aliases = fields.find("\"aliases\"");
        if (aliases == std::string::npos) continue;
        const std::string list = fields.substr(aliases + 9);
        for (std::sregex_iterator a(list.begin(), list.end(), alias), none; a != none; ++a) {
            table.not_taken.insert((*a)[1].str());
        }
    }
    return table;
}

DefaultsIniValue ToggleKey(const std::string& keys) {
    return detail::ReadDefaultsIni("[Hotkeys]\r\nToggleKey=Ctrl+" + keys + "\r\n").Value(Concept::ToggleKey);
}

void TestKeyNames() {
    std::cout << "\n[the key names Defaults.ini takes are data/keys.json's names with a virtual-key code, "
                 "less the Ctrl, Shift and Alt keys]\n";
    const KeyTable keys = ReadKeyTable();
    const std::vector<std::string> listed = HeaderKeyNames(detail::RenderDefaultsIni());
    const std::set<std::string> listed_set(listed.begin(), listed.end());
    Check(keys.taken.size() == 98 && keys.not_taken.size() > 200,
          "data/keys.json reads as 98 names with a code that are not a Ctrl, Shift or Alt key");
    Check(listed.size() == 98 && listed_set.size() == listed.size(), "the header lists 98 names, none twice");
    Check(listed_set == keys.taken,
          "the header lists exactly the names with a Windows virtual-key code that are not a Ctrl, Shift or Alt key");

    bool read = true;
    for (const std::string& name : keys.taken) read = read && ToggleKey(name).state == DefaultsIniValueState::kAccepted;
    Check(read, "every name the header lists is read");
    Check(keys.not_taken.count("LeftShift") == 1 && keys.not_taken.count("RightAlt") == 1,
          "the Ctrl, Shift and Alt keys are among the names not taken");
    bool refused = true;
    for (const std::string& name : keys.not_taken) {
        const DefaultsIniValue value = ToggleKey(name);
        refused = refused && value.state == DefaultsIniValueState::kRefused &&
                  value.reason == name + " is not one of the key names this file takes";
    }
    Check(refused, "every other name, the Ctrl, Shift and Alt keys among them, and every alias is refused as not one "
                   "of the key names this file takes");
}

}  // namespace

int RunDefaultsIniTests() {
    std::cout << "\n=== Defaults.ini ===\n";
    g_failures = 0;
    try {
        TestRender();
        TestCases();
        TestLineArguments();
        TestKeyNames();
    } catch (const std::exception& e) {
        std::cout << "  [FAIL] threw: " << e.what() << "\n";
        ++g_failures;
    }
    return g_failures;
}
