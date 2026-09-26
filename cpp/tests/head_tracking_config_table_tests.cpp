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
        {"TrueFreeLook", [b](const H& c) { return b(c.true_free_look); }},
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
        {"TrueFreeLookKey", [k](const H& c) { return k(c.true_free_look_key_name); }},
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

void TestFreshRender() {
    std::cout << "\n[the fresh file with every concept]\n";
    const ConfigTable<HeadTrackingConfig> table = AllConceptsTable();
    const std::string rendered = RenderCanonicalFresh(table, kHeader);
    Check(rendered == ReadBytes(Root() / "all-concepts-fresh.ini"),
          "the table with every concept passes the fresh render's gate and renders as all-concepts-fresh.ini");
    bool clean = false;
    const HeadTrackingConfig reread = Applied(table, rendered, HeadTrackingConfig{}, clean);
    Check(clean && FieldRows(reread) == FieldRows(table.defaults()), "the fresh file reads back as the defaults");
}

// Effective LocalSmoothing and RemoteSmoothing reach the position settings' copy, and CollisionChannel,
// which is not global, keeps the game's own default whatever the effective defaults hold and migrates
// as an Engine row: commented at that default, and as a value away from it. The C# twin is
// HeadTrackingConfigTableFixtures.RunEffectiveDefaults.
void TestEffectiveDefaults() {
    std::cout << "\n[effective defaults]\n";
    const ConfigTable<HeadTrackingConfig> table =
        HeadTrackingConfigTable({Concept::LocalSmoothing, Concept::RemoteSmoothing, Concept::CollisionChannel});
    HeadTrackingConfig effective = table.defaults();
    effective.local_smoothing = 0.25f;
    effective.remote_smoothing = 0.5f;
    effective.collision_channel = 2;
    const std::vector<Concept> from_defaults_ini{Concept::LocalSmoothing, Concept::RemoteSmoothing};
    HeadTrackingConfig config;
    const detail::EffectiveApplyResult result =
        detail::ApplyCanonicalEffective(ParseCanonicalIni(""), table, config, effective, from_defaults_ini);
    Check(config.local_smoothing == 0.25f && config.remote_smoothing == 0.5f && config.position.local_smoothing == 0.25f &&
              config.position.remote_smoothing == 0.5f,
          "the effective defaults reach every field, the position copy included");
    Check(config.collision_channel == 0, "CollisionChannel keeps the game's own default");
    Check(result.sources == std::vector<detail::ValueSource>{detail::ValueSource::kDefaultsIni,
                                                            detail::ValueSource::kDefaultsIni,
                                                            detail::ValueSource::kBuiltIn},
          "the smoothing rows' source is Defaults.ini and CollisionChannel's the table");

    HeadTrackingConfig values = table.defaults();
    values.local_smoothing = 0.25f;
    const std::string migrated = detail::RenderCanonicalMigration(table, values, effective, kHeader);
    Check(Contains(migrated, "\r\nLocalSmoothing=default\r\n") && Contains(migrated, "\r\nRemoteSmoothing=0.15\r\n") &&
              Contains(migrated, "\r\n; CollisionChannel=0\r\n"),
          "the migration render writes default, a value, and the Engine row commented at the game's default");
    values.collision_channel = 4;
    const std::string pinned = detail::RenderCanonicalMigration(table, values, effective, kHeader);
    Check(Contains(pinned, "\r\nCollisionChannel=4\r\n") && !Contains(pinned, "; CollisionChannel"),
          "and CollisionChannel away from it as a value");
    HeadTrackingConfig reread;
    const detail::EffectiveApplyResult back =
        detail::ApplyCanonicalEffective(ParseCanonicalIni(migrated), table, reread, effective, from_defaults_ini);
    Check(back.report.diagnostics.empty() && reread.local_smoothing == 0.25f && reread.remote_smoothing == 0.15f &&
              reread.collision_channel == 0,
          "the migrated file reads back as its values");
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
    const ConfigTable<HeadTrackingConfig> table = HeadTrackingConfigTable(
        {Concept::ToggleKey, Concept::CycleTrackingModeKey, Concept::YawModeKey, Concept::TrueFreeLookKey});
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
    Check(d.true_free_look_key_name == "Insert, Ctrl+Shift+U" &&
              d.true_free_look_key_name == schema::ConceptTraits<Concept::TrueFreeLookKey>::kCanonicalDefault,
          "TrueFreeLookKey defaults to its canonical_default");

    const HeadTrackingConfig flat;
    Check(flat.toggle_key_name == "End" && flat.cycle_tracking_mode_key_name.empty() &&
              flat.yaw_mode_key_name == "PageDown",
          "the flat reader's field defaults are single keys");
    Check(flat.true_free_look_key_name == schema::ConceptTraits<Concept::TrueFreeLookKey>::kCanonicalDefault,
          "true_free_look_key_name's initialiser is its canonical_default");

    const ConfigTable<HeadTrackingConfig> collision = HeadTrackingConfigTable({Concept::CollisionEnabled});
    Check(collision.defaults().collision_enabled &&
              std::string(schema::ConceptTraits<Concept::CollisionEnabled>::kCanonicalDefault) == "true",
          "CollisionEnabled defaults to its canonical_default, true");
    Check(!flat.collision_enabled, "and the flat reader's collision_enabled to false");
}

// CollisionMargin and CollisionChannel are the only concepts that are not global. Their rows are
// Engine rows: a fresh file comments them at the game's default, and any other value is written.
// The C# twin is HeadTrackingConfigTableTests.CollisionMarginAndChannelAreEngineRowsOutsideDefaultsIni.
void TestEngineCollisionRows() {
    std::cout << "\n[CollisionMargin and CollisionChannel are Engine rows outside Defaults.ini]\n";
    for (const schema::ConceptInfo& info : schema::kConcepts) {
        const bool engine = info.id == Concept::CollisionMargin || info.id == Concept::CollisionChannel;
        Check(info.global != engine, std::string(info.name) + (engine ? " is not global" : " is global"));
    }
    static_assert(!schema::ConceptTraits<Concept::CollisionMargin>::kGlobal &&
                  !schema::ConceptTraits<Concept::CollisionChannel>::kGlobal &&
                  schema::ConceptTraits<Concept::CollisionEnabled>::kGlobal);

    const ConfigTable<HeadTrackingConfig> table =
        HeadTrackingConfigTable({Concept::CollisionEnabled, Concept::CollisionMargin, Concept::CollisionChannel});
    const std::string fresh = RenderCanonicalFresh(table, kHeader);
    Check(Contains(fresh, "\r\nCollisionEnabled=default\r\n") && Contains(fresh, "\r\n; CollisionMargin=0.1\r\n") &&
              Contains(fresh, "\r\n; CollisionChannel=0\r\n"),
          "the fresh file holds CollisionEnabled=default and comments the two engine rows at their defaults");
    HeadTrackingConfig pinned = table.defaults();
    pinned.collision_enabled = false;
    pinned.lean_clamp.skin = 10.0f;
    pinned.collision_channel = 3;
    std::string expected = fresh;
    for (const auto& [from, to] : std::vector<std::pair<std::string, std::string>>{
             {"CollisionEnabled=default", "CollisionEnabled=false"},
             {"; CollisionMargin=0.1", "CollisionMargin=10.0"},
             {"; CollisionChannel=0", "CollisionChannel=3"}}) {
        expected.replace(expected.find(from), from.size(), to);
    }
    Check(RenderCanonical(table, pinned, kHeader) == expected, "values away from the defaults are written as values");
}

void TestTrueFreeLookSpellings() {
    std::cout << "\n[TrueFreeLook is read under its own spelling only]\n";
    const ConfigTable<HeadTrackingConfig> table = HeadTrackingConfigTable({Concept::TrueFreeLook});
    HeadTrackingConfig config;
    const ApplyReport report =
        ApplyCanonical(ParseCanonicalIni("[Position]\r\ntrue_free_look=true\r\n"), table, config);
    Check(!config.true_free_look && report.diagnostics.size() == 1 &&
              report.diagnostics[0].kind == CanonicalDiagnosticKind::MisplacedKey,
          "a canonical file's true_free_look is misplaced and not read");

    HeadTrackingConfig flat;
    std::vector<std::string> log;
    flat.ApplyValues({{"TrueFreeLook", "true"}, {"true_free_look", "true"}, {"TrueFreeLookKey", "F8"}},
                     [&log](const std::string& line) { log.push_back(line); });
    Check(!flat.true_free_look && flat.true_free_look_key_name == "Insert, Ctrl+Shift+U" && log.empty(),
          "the flat reader reads neither TrueFreeLook concept");
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
        TestFreshRender();
        TestEffectiveDefaults();
        TestHotkeyDefaults();
        TestEngineCollisionRows();
        TestTrueFreeLookSpellings();
        TestArguments();
        TestOnlyNamedConcepts();
        TestDerivedConfig();
    } catch (const std::exception& e) {
        std::cout << "  [FAIL] threw: " << e.what() << "\n";
        ++g_failures;
    }
    return g_failures;
}
