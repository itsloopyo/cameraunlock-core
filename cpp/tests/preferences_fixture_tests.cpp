// data/fixtures/canonical-ini/preferences, which the C# PreferencesFixtures runs too: what a
// launcher reads for each of its four preferences, what the mod runs on, and what the mod's own
// Save writes for a change.

#include <cameraunlock/config/config_owner.h>
#include <cameraunlock/config/head_tracking_config_table.h>
#include <cameraunlock/tracking/tracking_mode.h>

#include "preference_modes.g.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <random>
#endif

namespace {

namespace fs = std::filesystem;
using namespace cameraunlock::config;
using cameraunlock::HeadTrackingConfig;
using cameraunlock::TrackingMode;
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

void Require(bool cond, const std::string& what) {
    if (!cond) throw std::runtime_error(what);
}

fs::path Root() { return fs::path(CAMERAUNLOCK_CANONICAL_INI_FIXTURES) / "preferences"; }

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) throw std::runtime_error("cannot open " + path.string());
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

const char kInvalid[] = "invalid";
const char kMissing[] = "missing";

struct Case {
    bool three_state = false;
    std::vector<std::string> preferences;
    std::optional<std::pair<std::string, std::string>> change;
};

Case ReadCase(const fs::path& path) {
    Case fixture;
    bool binds = false;
    for (const std::string& line : SplitAt(ReadBytes(path), '\n')) {
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> row = SplitAt(line, '\t');
        if (!binds) {
            Require(row.size() == 2 && row[0] == "binds" && (row[1] == "three-state" || row[1] == "two-state"),
                    "the first row of " + path.string() + " is not binds three-state or two-state");
            fixture.three_state = row[1] == "three-state";
            binds = true;
        } else if (row.size() == 4 && row[0] == "preference" && !fixture.change) {
            fixture.preferences.push_back(row[1] + "\t" + row[2] + "\t" + row[3]);
        } else if (row.size() == 3 && row[0] == "change" && !fixture.change) {
            fixture.change = std::make_pair(row[1], row[2]);
        } else {
            throw std::runtime_error("malformed row in " + path.string() + ": " + line);
        }
    }
    Require(binds, path.string() + " has no binds row");
    return fixture;
}

// The rows a fixture mod binds; the Writable table is the one its owner saves through.
ConfigTable<HeadTrackingConfig> Table(bool three_state, bool writable) {
    ConfigTable<HeadTrackingConfig> table =
        three_state ? HeadTrackingConfigTable({Concept::EnableOnStartup, Concept::WorldSpaceYaw,
                                               Concept::RotationEnabled, Concept::PositionEnabled,
                                               Concept::TrueFreeLook})
                    : HeadTrackingConfigTable({Concept::EnableOnStartup, Concept::WorldSpaceYaw,
                                               Concept::PositionEnabled, Concept::TrueFreeLook});
    if (!writable) return table;
    table.Select(Concept::EnableOnStartup).Writable()
        .Select(Concept::WorldSpaceYaw).Writable()
        .Select(Concept::PositionEnabled).Writable()
        .Select(Concept::TrueFreeLook).Writable();
    if (three_state) table.Select(Concept::RotationEnabled).Writable();
    return table;
}

HeadTrackingConfig Applied(bool three_state, const std::string& bytes) {
    const ConfigTable<HeadTrackingConfig> table = Table(three_state, false);
    HeadTrackingConfig config = table.defaults();
    ApplyCanonical(ParseCanonicalIni(bytes), table, config);
    return config;
}

std::string BoolName(bool value) { return value ? "true" : "false"; }

std::string RawBool(const CanonicalIni& doc, Concept id) {
    const schema::ConceptInfo& info = schema::kConcepts[static_cast<std::size_t>(id)];
    const CanonicalValue* value = doc.Find(info.section, info.key);
    if (value == nullptr) return kMissing;
    const CodecParseResult<bool> parsed = BoolCodec().Parse(value->value);
    return parsed.ok() ? BoolName(parsed.value) : kInvalid;
}

std::string ModeName(std::optional<TrackingMode> mode) {
    if (!mode) return kInvalid;
    for (const auto& expectation : preference_modes::kTrackingModes) {
        if (expectation.mode == *mode) return expectation.name;
    }
    throw std::runtime_error("no preference_modes name for a tracking mode");
}

TrackingMode ModeOf(const std::string& name, bool three_state) {
    for (const auto& expectation : preference_modes::kTrackingModes) {
        if (name == expectation.name && (three_state || expectation.rotation_enabled)) return expectation.mode;
    }
    throw std::runtime_error(name + " is not a tracking mode of a " + (three_state ? "three" : "two") + "-state mod");
}

bool BoolOf(const std::string& name) {
    if (name == "true") return true;
    if (name == "false") return false;
    throw std::runtime_error(name + " is not true or false");
}

// One "preference, raw, mod" row per preference, in case.tsv's order.
std::vector<std::string> Rows(bool three_state, const std::string& bytes, const HeadTrackingConfig& mod) {
    const CanonicalIni doc = ParseCanonicalIni(bytes);
    Require(doc.IsReadable(), "the file is unreadable");
    std::string raw_mode;
    const std::string position = RawBool(doc, Concept::PositionEnabled);
    if (three_state) {
        const std::string rotation = RawBool(doc, Concept::RotationEnabled);
        if (rotation == kInvalid || position == kInvalid) {
            raw_mode = kInvalid;
        } else if (rotation == kMissing || position == kMissing) {
            raw_mode = kMissing;
        } else {
            raw_mode = ModeName(cameraunlock::DecodeTrackingMode(rotation == "true", position == "true"));
        }
    } else {
        raw_mode = position == kInvalid || position == kMissing
                       ? position
                       : ModeName(cameraunlock::DecodeTrackingMode(true, position == "true"));
    }
    return {
        "tracking_mode\t" + raw_mode + "\t" +
            ModeName(cameraunlock::DecodeTrackingMode(mod.rotation_enabled, mod.position_enabled)),
        "world_space_yaw\t" + RawBool(doc, Concept::WorldSpaceYaw) + "\t" + BoolName(mod.world_space_yaw),
        "true_free_look\t" + RawBool(doc, Concept::TrueFreeLook) + "\t" + BoolName(mod.true_free_look),
        "launch_enabled\t" + RawBool(doc, Concept::EnableOnStartup) + "\t" + BoolName(mod.enable_on_startup),
    };
}

void SameRows(const std::vector<std::string>& actual, const std::vector<std::string>& expected,
              const std::string& what) {
    if (actual == expected) return;
    std::string message = what + " differs.\n  expected:";
    for (const std::string& row : expected) message += "\n    " + row;
    message += "\n  actual:";
    for (const std::string& row : actual) message += "\n    " + row;
    throw std::runtime_error(message);
}

// The preference rows with the changed one's raw and mod values both the new value. The new value
// has to differ from what the mod runs on, or the owner's Save would write nothing.
std::vector<std::string> ChangedRows(const Case& fixture) {
    const std::string& preference = fixture.change->first;
    const std::string& value = fixture.change->second;
    std::vector<std::string> rows;
    bool found = false;
    for (const std::string& row : fixture.preferences) {
        const std::vector<std::string> fields = SplitAt(row, '\t');
        if (fields[0] != preference) {
            rows.push_back(row);
            continue;
        }
        Require(fields[2] != value, "the change sets " + preference + " to the value the mod runs on");
        rows.push_back(preference + "\t" + value + "\t" + value);
        found = true;
    }
    Require(found, "the change names " + preference + ", which is not a preference");
    return rows;
}

void SetPreference(HeadTrackingConfig& config, bool three_state, const std::string& preference,
                   const std::string& value) {
    if (preference == "tracking_mode") {
        const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(ModeOf(value, three_state));
        if (three_state) config.rotation_enabled = channels.rotation_enabled;
        config.position_enabled = channels.position_enabled;
    } else if (preference == "world_space_yaw") {
        config.world_space_yaw = BoolOf(value);
    } else if (preference == "true_free_look") {
        config.true_free_look = BoolOf(value);
    } else if (preference == "launch_enabled") {
        config.enable_on_startup = BoolOf(value);
    } else {
        throw std::runtime_error(preference + " is not a preference");
    }
}

#ifdef _WIN32

void RunOwner(const Case& fixture, const std::string& input, const fs::path& dir, const fs::path& scratch) {
    const fs::path target = scratch / L"HeadTracking.ini";
    {
        std::ofstream out(target, std::ios::binary);
        out.write(input.data(), static_cast<std::streamsize>(input.size()));
        Require(out.good(), "cannot write " + target.string());
    }
    ConfigOwnerOptions<HeadTrackingConfig> options;
    options.path = target.wstring();
    options.table = Table(fixture.three_state, true);
    options.header.display_name = "Fixture Game";
    ConfigOwner<HeadTrackingConfig> owner(std::move(options));

    const ConfigLoadResult<HeadTrackingConfig> loaded = owner.Load();
    Require(loaded.status == ConfigLoadStatus::Canonical, "the owner does not load the file as Canonical");
    Require(ReadBytes(target) == input, "the owner's Load wrote the file");
    SameRows(Rows(fixture.three_state, input, loaded.config), fixture.preferences, "input.ini through the owner's Load");

    if (!fixture.change) return;
    const std::vector<std::string> after = ChangedRows(fixture);
    const ConfigSaveResult saved = owner.Save([&fixture](HeadTrackingConfig& c) {
        SetPreference(c, fixture.three_state, fixture.change->first, fixture.change->second);
    });
    Require(saved.status == ConfigSaveStatus::Saved, "the change is not saved: " + saved.reason);
    const std::string expected = ReadBytes(dir / "expected.ini");
    const std::string written = ReadBytes(target);
    Require(written == expected, "the owner's Save writes other bytes than expected.ini:\n" + written);
    SameRows(Rows(fixture.three_state, expected, Applied(fixture.three_state, expected)), after,
             "expected.ini through the table");
}

fs::path CreateScratch() {
    std::random_device random;
    const fs::path dir =
        fs::temp_directory_path() / (L"cu-preferences-" + std::to_wstring(random()) + std::to_wstring(random()));
    fs::create_directories(dir);
    return dir;
}

#endif  // _WIN32

void RunCase(const std::string& name) {
    const fs::path dir = Root() / name;
    const Case fixture = ReadCase(dir / "case.tsv");
    const std::string input = ReadBytes(dir / "input.ini");
    Require(fs::exists(dir / "expected.ini") == fixture.change.has_value(),
            "expected.ini must exist exactly when case.tsv has a change row");
    SameRows(Rows(fixture.three_state, input, Applied(fixture.three_state, input)), fixture.preferences,
             "input.ini through the table");
#ifdef _WIN32
    const fs::path scratch = CreateScratch();
    try {
        RunOwner(fixture, input, dir, scratch);
    } catch (...) {
        fs::remove_all(scratch);
        throw;
    }
    fs::remove_all(scratch);
#endif
}

}  // namespace

int RunPreferencesFixtureTests() {
    std::cout << "\n=== Preferences fixtures ===\n";
    g_failures = 0;
    std::vector<std::string> names;
    for (const fs::directory_entry& entry : fs::directory_iterator(Root())) {
        if (entry.is_directory()) names.push_back(entry.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    Check(!names.empty(), "the preferences directory holds cases");
    for (const std::string& name : names) {
        try {
            RunCase(name);
            Check(true, name);
        } catch (const std::exception& e) {
            Check(false, name + ": " + e.what());
        }
    }
    return g_failures;
}
