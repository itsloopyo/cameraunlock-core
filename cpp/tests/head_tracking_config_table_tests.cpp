// HeadTrackingConfigTable against data/fixtures/canonical-ini/head-tracking, which the C#
// HeadTrackingConfigTableFixtures runs too, plus the argument checks, the hotkey defaults and a
// table over a Config derived from HeadTrackingConfig.

#include <cameraunlock/config/head_tracking_config_table.h>

#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace cameraunlock::config;
using cameraunlock::HeadTrackingConfig;
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

template <class F>
std::string Thrown(F&& f) {
    try {
        f();
    } catch (const std::invalid_argument& e) {
        return e.what();
    }
    return "(nothing thrown)";
}

bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

fs::path Root() { return fs::path(CAMERAUNLOCK_CANONICAL_INI_FIXTURES) / "head-tracking"; }

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

// "Name=value" per field row of an expected.tsv, in file order.
std::vector<std::string> ExpectedFields(const fs::path& path) {
    std::vector<std::string> fields;
    for (const std::string& line : SplitAt(ReadBytes(path), '\n')) {
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> row = SplitAt(line, '\t');
        if (row.size() != 3 || row[0] != "field") throw std::runtime_error("malformed row in " + path.string());
        fields.push_back(row[1] + "=" + Unescape(row[2]));
    }
    return fields;
}

// Each field a concept reaches, read straight off the struct rather than through the table, in
// the order of expected.tsv.
struct FieldRead {
    const char* name;
    std::function<std::string(const HeadTrackingConfig&)> render;
};

std::vector<FieldRead> Fields() {
    using H = HeadTrackingConfig;
    const auto b = [](bool v) { return BoolCodec().Render(v); };
    const auto i = [](int v) { return IntCodec<int>().Render(v); };
    const auto f = [](float v) { return FloatCodec().Render(v); };
    const auto k = [](const std::string& v) { return HotkeyCodec().Render(v); };
    return {
        {"UdpPort", [i](const H& c) { return i(c.udp_port); }},
        {"EnableOnStartup", [b](const H& c) { return b(c.enable_on_startup); }},
        {"LocalSmoothing", [f](const H& c) { return f(c.local_smoothing); }},
        {"RemoteSmoothing", [f](const H& c) { return f(c.remote_smoothing); }},
        {"WorldSpaceYaw", [b](const H& c) { return b(c.world_space_yaw); }},
        {"AimDecoupling", [b](const H& c) { return b(c.aim_decoupling_enabled); }},
        {"RotationEnabled", [b](const H& c) { return b(c.rotation_enabled); }},
        {"DataFreshnessMs", [i](const H& c) { return i(c.data_freshness_ms); }},
        {"PositionEnabled", [b](const H& c) { return b(c.position_enabled); }},
        {"PositionAllowed", [b](const H& c) { return b(c.position_allowed); }},
        {"PositionLimitX", [f](const H& c) { return f(c.position.limit_x); }},
        {"PositionLimitY", [f](const H& c) { return f(c.position.limit_y); }},
        {"PositionLimitYDown", [f](const H& c) { return f(c.position.limit_y_down); }},
        {"PositionLimitZ", [f](const H& c) { return f(c.position.limit_z); }},
        {"PositionLimitZBack", [f](const H& c) { return f(c.position.limit_z_back); }},
        {"CollisionEnabled", [b](const H& c) { return b(c.collision_enabled); }},
        {"CollisionMargin", [f](const H& c) { return f(c.lean_clamp.skin); }},
        {"CollisionChannel", [i](const H& c) { return i(c.collision_channel); }},
        {"CollisionReleaseSmoothing", [f](const H& c) { return f(c.lean_clamp.release_smoothing); }},
        {"TrackerPivotForward", [f](const H& c) { return f(c.tracker_pivot_forward); }},
        {"TrackerPivotUp", [f](const H& c) { return f(c.tracker_pivot_up); }},
        {"ToggleKey", [k](const H& c) { return k(c.toggle_key_name); }},
        {"CycleTrackingModeKey", [k](const H& c) { return k(c.cycle_tracking_mode_key_name); }},
        {"YawModeKey", [k](const H& c) { return k(c.yaw_mode_key_name); }},
        {"LightFollowsHead", [b](const H& c) { return b(c.light.follows_head); }},
        {"LightMultiplier", [f](const H& c) { return f(c.light.multiplier); }},
        {"PositionLocalSmoothing", [f](const H& c) { return f(c.position.local_smoothing); }},
        {"PositionRemoteSmoothing", [f](const H& c) { return f(c.position.remote_smoothing); }},
    };
}

std::vector<std::string> FieldRows(const HeadTrackingConfig& config) {
    std::vector<std::string> rows;
    for (const FieldRead& field : Fields()) rows.push_back(std::string(field.name) + "=" + field.render(config));
    return rows;
}

template <std::size_t... I>
ConfigTable<HeadTrackingConfig> TableOf(std::index_sequence<I...>) {
    return HeadTrackingConfigTable({schema::kConcepts[I].id...});
}

ConfigTable<HeadTrackingConfig> AllConceptsTable() { return TableOf(std::make_index_sequence<schema::kConceptCount>{}); }

const RenderHeader kHeader{"Fixture Game"};

const char* const kCases[] = {"apply-empty", "apply-position-off", "apply-values"};

// Fields no row binds, set before Apply so a test can see they are left alone.
HeadTrackingConfig Marked(HeadTrackingConfig config) {
    config.recenter_key_name = "F1";
    config.position.sensitivity_x = 2.0f;
    config.position.invert_y = true;
    return config;
}

bool MarksKept(const HeadTrackingConfig& config) {
    return config.recenter_key_name == "F1" && config.position.sensitivity_x == 2.0f && config.position.invert_y;
}

HeadTrackingConfig Applied(const ConfigTable<HeadTrackingConfig>& table, const std::string& bytes,
                           HeadTrackingConfig start, bool& clean) {
    const CanonicalIni doc = ParseCanonicalIni(bytes);
    const ApplyReport report = ApplyCanonical(doc, table, start);
    clean = doc.diagnostics.empty() && report.diagnostics.empty();
    return start;
}

void TestFixtures() {
    std::cout << "\n[head-tracking fixtures]\n";
    const ConfigTable<HeadTrackingConfig> table = AllConceptsTable();
    const std::vector<std::string> defaults = FieldRows(table.defaults());

    Check(RenderCanonical(table, table.defaults(), kHeader) == ReadBytes(Root() / "all-concepts.ini"),
          "the defaults with every concept render as all-concepts.ini");

    bool clean = false;
    const HeadTrackingConfig moved =
        Applied(table, ReadBytes(Root() / "apply-values" / "input.ini"), Marked(HeadTrackingConfig{}), clean);

    std::vector<bool> covered(defaults.size(), false);
    for (const char* name : kCases) {
        const fs::path dir = Root() / name;
        const std::vector<std::string> expected = ExpectedFields(dir / "expected.tsv");
        const std::string input = ReadBytes(dir / "input.ini");
        const std::string label = std::string(name) + ": ";

        const HeadTrackingConfig from_defaults = Applied(table, input, Marked(HeadTrackingConfig{}), clean);
        Check(clean, label + "reads with no diagnostic");
        Check(FieldRows(from_defaults) == expected, label + "applied onto the defaults gives expected.tsv");
        Check(MarksKept(from_defaults), label + "fields no row binds keep their values");

        const HeadTrackingConfig from_moved = Applied(table, input, moved, clean);
        Check(FieldRows(from_moved) == expected, label + "applied onto apply-values' result gives expected.tsv");
        Check(MarksKept(from_moved), label + "fields no row binds keep their values over apply-values' result");

        const std::string rendered = RenderCanonical(table, from_defaults, kHeader);
        const HeadTrackingConfig reread = Applied(table, rendered, HeadTrackingConfig{}, clean);
        Check(clean && FieldRows(reread) == expected && RenderCanonical(table, reread, kHeader) == rendered,
              label + "its render reads back as itself");

        if (expected.size() == defaults.size()) {
            for (std::size_t i = 0; i < expected.size(); ++i) covered[i] = covered[i] || expected[i] != defaults[i];
        }
        if (std::string(name) == "apply-empty") Check(expected == defaults, label + "expected.tsv is the defaults");
    }
    for (std::size_t i = 0; i < defaults.size(); ++i) {
        Check(covered[i], "a case moves " + defaults[i].substr(0, defaults[i].find('=')) + " off its default");
    }
}

void TestEveryConceptBound() {
    std::cout << "\n[every canonical concept has a binding]\n";
    std::string error;
    try {
        AllConceptsTable();
    } catch (const std::exception& e) {
        error = e.what();
    }
    Check(error.empty(), "HeadTrackingConfigTable binds every canonical concept" + (error.empty() ? "" : ": " + error));
}

void TestHotkeyDefaults() {
    std::cout << "\n[hotkey defaults]\n";
    const ConfigTable<HeadTrackingConfig> table =
        HeadTrackingConfigTable({Concept::ToggleKey, Concept::CycleTrackingModeKey, Concept::YawModeKey});
    const HeadTrackingConfig& d = table.defaults();
    Check(d.toggle_key_name == "End, Ctrl+Shift+Y" &&
              d.toggle_key_name == schema::ConceptTraits<Concept::ToggleKey>::kCanonicalDefault,
          "ToggleKey defaults to its canonical_default");
    Check(d.cycle_tracking_mode_key_name == "PageUp, Ctrl+Shift+G" &&
              d.cycle_tracking_mode_key_name ==
                  schema::ConceptTraits<Concept::CycleTrackingModeKey>::kCanonicalDefault,
          "CycleTrackingModeKey defaults to its canonical_default");
    Check(d.yaw_mode_key_name == "PageDown, Ctrl+Shift+H" &&
              d.yaw_mode_key_name == schema::ConceptTraits<Concept::YawModeKey>::kCanonicalDefault,
          "YawModeKey defaults to its canonical_default");

    const HeadTrackingConfig flat;
    Check(flat.toggle_key_name == "End" && flat.cycle_tracking_mode_key_name.empty() &&
              flat.yaw_mode_key_name == "PageDown",
          "the flat reader's field defaults are unchanged");
}

void TestArguments() {
    std::cout << "\n[arguments]\n";
    Check(Contains(Thrown([] { HeadTrackingConfigTable({}); }), "needs the concepts"), "an empty list throws");
    Check(Contains(Thrown([] { HeadTrackingConfigTable({Concept::UdpPort, Concept::ToggleKey, Concept::UdpPort}); }),
                   "names UdpPort twice"),
          "a concept named twice throws");
    Check(Contains(Thrown([] { HeadTrackingConfigTable({static_cast<Concept>(schema::kConceptCount)}); }),
                   "is not a canonical concept"),
          "a value that is no concept throws");
}

void TestOnlyNamedConcepts() {
    std::cout << "\n[only the named concepts]\n";
    const ConfigTable<HeadTrackingConfig> table = HeadTrackingConfigTable({Concept::UdpPort, Concept::EnableOnStartup});
    const std::string rendered = RenderCanonical(table, table.defaults(), kHeader);
    Check(Contains(rendered, "\r\n[Network]\r\n") && Contains(rendered, "\r\n[General]\r\n") &&
              !Contains(rendered, "[Hotkeys]") && !Contains(rendered, "[Light]") &&
              !Contains(rendered, "Hotkeys are key names"),
          "a table of two concepts writes only their sections");

    const std::string file = "[Light]\r\nLightMultiplier=2.0\r\n";
    HeadTrackingConfig config;
    const ApplyReport report = ApplyCanonical(ParseCanonicalIni(file), table, config);
    Check(config.light.multiplier == 1.5f && report.diagnostics.size() == 1 &&
              report.diagnostics[0].kind == CanonicalDiagnosticKind::UnknownSection,
          "a concept the game does not name is not read");
}

struct ModConfig : HeadTrackingConfig {
    bool debug_camera = false;
};

void TestDerivedConfig() {
    std::cout << "\n[a Config derived from HeadTrackingConfig]\n";
    ConfigTable<ModConfig> table = HeadTrackingConfigTable<ModConfig>({Concept::UdpPort, Concept::ToggleKey});
    table.Local("Debug", "DebugCamera", &ModConfig::debug_camera, BoolCodec(), "true: log the camera once a second.");
    Check(table.defaults().toggle_key_name == "End, Ctrl+Shift+Y", "the derived defaults carry the hotkey list");

    ModConfig config;
    const CanonicalIni doc = ParseCanonicalIni("[Network]\r\nUdpPort=5000\r\n[Debug]\r\nDebugCamera=true\r\n");
    const ApplyReport report = ApplyCanonical(doc, table, config);
    Check(report.diagnostics.empty() && config.udp_port == 5000 && config.debug_camera &&
              config.toggle_key_name == "End, Ctrl+Shift+Y",
          "core's rows and the game's local row read into one config");
    Check(Contains(RenderCanonical(table, config, kHeader), "\r\n[Debug]\r\n; true: log the camera once a second.\r\n"
                                                            "DebugCamera=true\r\n"),
          "the local row renders after core's sections");
}

}  // namespace

int RunHeadTrackingConfigTableTests() {
    std::cout << "\n=== HeadTrackingConfigTable ===\n";
    g_failures = 0;
    try {
        TestEveryConceptBound();
        TestFixtures();
        TestHotkeyDefaults();
        TestArguments();
        TestOnlyNamedConcepts();
        TestDerivedConfig();
    } catch (const std::exception& e) {
        std::cout << "  [FAIL] threw: " << e.what() << "\n";
        ++g_failures;
    }
    return g_failures;
}
