// Config tables against data/fixtures/canonical-ini/table, which the C# ConfigTableFixtures runs
// too over the same fixture table, plus the construction checks, the modifiers and the parts
// only C++ has: field types the concept traits accept or refuse, getter and setter rows, and
// the native hotkey dialect.

#include <cameraunlock/config/config_key_schema.g.h>
#include <cameraunlock/config/config_table.h>

#include <algorithm>
#include <array>
#include <cstdint>
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

// The message of the std::invalid_argument f throws, or "(nothing thrown)".
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

// The rows of an expected.tsv or values.tsv, notes and empty lines dropped.
std::vector<std::vector<std::string>> TsvRows(const fs::path& path) {
    std::vector<std::vector<std::string>> rows;
    for (const std::string& line : SplitAt(ReadBytes(path), '\n')) {
        if (line.empty() || line[0] == '#') continue;
        rows.push_back(SplitAt(line, '\t'));
    }
    return rows;
}

// The fixture Config and table, defined identically in ConfigTableFixtures.cs. The C++ field
// types differ where C++ has more than C#: UdpPort is std::uint16_t, LocalSmoothing a double
// reached through a getter and a setter.
enum class CameraMode { kControlRotation, kUpdateCamera };

struct FixtureSmoothing {
    double local = 0.0;
};

struct FixtureConfig {
    std::string toggle_key = "End, Ctrl+Shift+Y";
    std::uint16_t udp_port = 4242;
    bool position_enabled = true;
    bool rotation_enabled = true;
    bool enable_on_startup = true;
    FixtureSmoothing smoothing;
    float position_limit_x = 0.3f;
    int collision_channel = 3;
    std::string cycle_key = "PageUp, Ctrl+Shift+G";
    CameraMode mode = CameraMode::kUpdateCamera;
    int lean_delay_ms = 50;
    float lean_trace_length = 1.0f;
    double near_clip = 0.1;
    int update_camera_slot = 196;
    std::uint32_t pov_offset = 0x404;
    std::uint64_t clean_camera_reader = 0x1402A0B10;
    std::vector<std::uint32_t> hook_offsets{0x10, 0x2A};
    std::vector<std::uint64_t> aim_callers;
    std::vector<std::string> widget_names{"Crosshair", "Compass"};
    std::array<float, 4> marker_color{1.0f, 0.5f, 0.0f, 1.0f};
    std::string log_path = "HeadTracking.log";
    bool write_log = false;
    std::string reload_key = "F10";
    int not_in_table = 7;
};

EnumCodec<CameraMode> ModeCodec() {
    return EnumCodec<CameraMode>(
        {{"ControlRotation", CameraMode::kControlRotation}, {"UpdateCamera", CameraMode::kUpdateCamera}});
}

double GetLocalSmoothing(const FixtureConfig& c) { return c.smoothing.local; }
void SetLocalSmoothing(FixtureConfig& c, double v) { c.smoothing.local = v; }

ConfigTable<FixtureConfig> FixtureTable() {
    using F = FixtureConfig;
    ConfigTable<F> table;
    table.Concept<Concept::ToggleKey>(&F::toggle_key)
        .Writable()
        .Concept<Concept::UdpPort>(&F::udp_port)
        .Concept<Concept::PositionEnabled>(&F::position_enabled)
        .Writable()
        .Concept<Concept::RotationEnabled>(&F::rotation_enabled)
        .Writable()
        .Concept<Concept::EnableOnStartup>(&F::enable_on_startup)
        .Concept<Concept::LocalSmoothing>(GetLocalSmoothing, SetLocalSmoothing)
        .Concept<Concept::PositionLimitX>(&F::position_limit_x)
        .Comment("How far, in metres, leaning sideways moves the view.\nThe fixture's own wording.")
        .Concept<Concept::CollisionChannel>(&F::collision_channel)
        .Engine()
        .PerGame()
        .Concept<Concept::CycleTrackingModeKey>(&F::cycle_key)
        .Local("Camera", "Mode", &F::mode, ModeCodec(), "ControlRotation or UpdateCamera (decoupled).")
        .Local("Position", "LeanDelayMs", &F::lean_delay_ms, IntCodec<int>(),
               "Milliseconds before a lean starts, and the metres its wall trace reaches.")
        .Range(0, 1000)
        .Local("Position", "LeanTraceLength", &F::lean_trace_length, FloatCodec(), "")
        .Range(0, 2)
        .Local("Camera", "NearClip", &F::near_clip, DoubleCodec(), "Near clip distance, in the game's units.")
        .Local("Camera", "UpdateCameraSlot", &F::update_camera_slot, IntCodec<int>(),
               "Engine values. The commented lines show the built-in values.\nDelete the ; to pin your own.")
        .Engine()
        .Local("Camera", "PovOffset", &F::pov_offset, Hex32Codec(), "")
        .Engine()
        .Local("Camera", "CleanCameraReader", &F::clean_camera_reader, Hex64Codec(), "")
        .Engine()
        .Local("Camera", "HookOffsets", &F::hook_offsets, Hex32ListCodec(), "Offsets the camera hook patches.")
        .Local("Camera", "AimCallers", &F::aim_callers, Hex64ListCodec(),
               "Return addresses whose aim is left alone. Empty for none.")
        .Local("Camera", "WidgetNames", &F::widget_names, StringListCodec(), "Widgets that follow the head.")
        .Local("Camera", "MarkerColor", &F::marker_color, ColorCodec(),
               "Marker colour: red, green, blue and opacity, each 0 to 1.")
        .Local("Logging", "LogPath", &F::log_path, StringCodec(), "Log file, beside the game's executable.")
        .Local("Logging", "WriteLog", &F::write_log, BoolCodec(), "true: write the log.")
        .Engine()
        .Local("Logging", "ReloadKey", &F::reload_key, HotkeyCodec(), "Reads this file again.");
    return table;
}

const RenderHeader kHeader{"Fixture Game"};

// Each fixture field by its key, as its row's codec reads and writes it.
struct FieldAccess {
    std::string name;
    std::function<std::string(const FixtureConfig&)> render;
    std::function<void(FixtureConfig&, const std::string&)> parse;
};

template <class Codec, class Field>
FieldAccess FieldOf(const char* name, Codec codec, Field FixtureConfig::*field) {
    return {name, [codec, field](const FixtureConfig& c) { return codec.Render(c.*field); },
            [codec, field, name](FixtureConfig& c, const std::string& text) {
                auto read = codec.Parse(text);
                if (!read.ok()) throw std::runtime_error(std::string("fixture value for ") + name + ": " + read.error);
                c.*field = read.value;
            }};
}

std::vector<FieldAccess> Fields() {
    using F = FixtureConfig;
    return {
        FieldOf("ToggleKey", HotkeyCodec(), &F::toggle_key),
        FieldOf("UdpPort", IntCodec<std::uint16_t>(), &F::udp_port),
        FieldOf("PositionEnabled", BoolCodec(), &F::position_enabled),
        FieldOf("RotationEnabled", BoolCodec(), &F::rotation_enabled),
        FieldOf("EnableOnStartup", BoolCodec(), &F::enable_on_startup),
        {"LocalSmoothing", [](const F& c) { return DoubleCodec().Render(c.smoothing.local); },
         [](F& c, const std::string& text) { c.smoothing.local = DoubleCodec().Parse(text).value; }},
        FieldOf("PositionLimitX", FloatCodec(), &F::position_limit_x),
        FieldOf("CollisionChannel", IntCodec<int>(), &F::collision_channel),
        FieldOf("CycleTrackingModeKey", HotkeyCodec(), &F::cycle_key),
        FieldOf("Mode", ModeCodec(), &F::mode),
        FieldOf("LeanDelayMs", IntCodec<int>(), &F::lean_delay_ms),
        FieldOf("LeanTraceLength", FloatCodec(), &F::lean_trace_length),
        FieldOf("NearClip", DoubleCodec(), &F::near_clip),
        FieldOf("UpdateCameraSlot", IntCodec<int>(), &F::update_camera_slot),
        FieldOf("PovOffset", Hex32Codec(), &F::pov_offset),
        FieldOf("CleanCameraReader", Hex64Codec(), &F::clean_camera_reader),
        FieldOf("HookOffsets", Hex32ListCodec(), &F::hook_offsets),
        FieldOf("AimCallers", Hex64ListCodec(), &F::aim_callers),
        FieldOf("WidgetNames", StringListCodec(), &F::widget_names),
        FieldOf("MarkerColor", ColorCodec(), &F::marker_color),
        FieldOf("LogPath", StringCodec(), &F::log_path),
        FieldOf("WriteLog", BoolCodec(), &F::write_log),
        FieldOf("ReloadKey", HotkeyCodec(), &F::reload_key),
    };
}

std::vector<std::string> FieldRows(const FixtureConfig& c) {
    std::vector<std::string> rows;
    for (const FieldAccess& field : Fields()) rows.push_back(field.name + "=" + field.render(c));
    return rows;
}

std::vector<std::string> DiagnosticRows(const ApplyReport& report) {
    std::vector<std::string> rows;
    for (const CanonicalDiagnostic& d : report.diagnostics) {
        std::string lines;
        for (std::size_t i = 0; i < d.lines.size(); ++i) lines += (i ? "," : "") + std::to_string(d.lines[i]);
        rows.push_back(std::string(CanonicalDiagnosticKindName(d.kind)) + " " + lines + " " +
                       DescribeCanonicalDiagnostic(d));
    }
    return rows;
}

std::string JoinRows(const std::vector<std::string>& rows) {
    std::string text;
    for (const std::string& row : rows) text += "\n      " + row;
    return text;
}

void CheckRows(const std::vector<std::string>& actual, const std::vector<std::string>& expected,
               const std::string& name) {
    Check(actual == expected, name);
    if (actual != expected) std::cout << "    expected:" << JoinRows(expected) << "\n    actual:" << JoinRows(actual) << "\n";
}

// Rendering what a rendered file applies to gives the same bytes and the same fields, with no
// diagnostic from either the reader or the table.
void CheckRoundTrip(const ConfigTable<FixtureConfig>& table, const FixtureConfig& values, const std::string& name) {
    const std::string rendered = RenderCanonical(table, values, kHeader);
    const CanonicalIni doc = ParseCanonicalIni(rendered);
    FixtureConfig applied;
    applied.not_in_table = 99;
    const ApplyReport report = ApplyCanonical(doc, table, applied);
    Check(doc.diagnostics.empty() && report.diagnostics.empty(), name + ": a rendered file reads with no diagnostic");
    CheckRows(FieldRows(applied), FieldRows(values), name + ": a rendered file reads back as the values");
    Check(applied.not_in_table == 99, name + ": a field no row binds is left alone");
    Check(RenderCanonical(table, applied, kHeader) == rendered, name + ": rendering what it read gives the same bytes");
}

// The defaults with effective.tsv's field rows, and the concepts its defaults_ini rows name.
FixtureConfig Effective(const fs::path& dir, std::vector<Concept>& from_defaults_ini) {
    const std::vector<FieldAccess> fields = Fields();
    FixtureConfig effective;
    for (const std::vector<std::string>& row : TsvRows(dir / "effective.tsv")) {
        if (row.size() < 2) throw std::runtime_error("malformed row in " + (dir / "effective.tsv").string());
        const auto field = std::find_if(fields.begin(), fields.end(), [&](const FieldAccess& f) { return f.name == row[1]; });
        if (row[0] == "field" && row.size() == 3 && field != fields.end()) {
            field->parse(effective, Unescape(row[2]));
            continue;
        }
        const auto concept_info = std::find_if(std::begin(schema::kConcepts), std::end(schema::kConcepts),
                                               [&](const schema::ConceptInfo& info) { return row[1] == info.name; });
        if (row[0] != "defaults_ini" || row.size() != 2 || concept_info == std::end(schema::kConcepts)) {
            throw std::runtime_error("malformed row in " + (dir / "effective.tsv").string());
        }
        from_defaults_ini.push_back(concept_info->id);
    }
    return effective;
}

std::vector<std::string> SourceRows(const std::vector<detail::ValueSource>& sources) {
    const std::vector<FieldAccess> fields = Fields();
    if (sources.size() != fields.size()) throw std::runtime_error("one source per row expected");
    std::vector<std::string> rows;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const char* source = sources[i] == detail::ValueSource::kFile          ? "file"
                             : sources[i] == detail::ValueSource::kDefaultsIni ? "defaults_ini"
                                                                                : "built_in";
        rows.push_back(fields[i].name + "=" + source);
    }
    return rows;
}

FixtureConfig Values(const fs::path& dir) {
    const std::vector<FieldAccess> fields = Fields();
    FixtureConfig values;
    for (const std::vector<std::string>& row : TsvRows(dir / "values.tsv")) {
        if (row[0] != "field" || row.size() != 3) throw std::runtime_error("malformed row in " + dir.string());
        const auto field = std::find_if(fields.begin(), fields.end(), [&](const FieldAccess& f) { return f.name == row[1]; });
        if (field == fields.end()) throw std::runtime_error("unknown field " + row[1] + " in " + dir.string());
        field->parse(values, Unescape(row[2]));
    }
    return values;
}

void CheckBytes(const std::string& rendered, const fs::path& path, const std::string& name) {
    const std::string expected = ReadBytes(path);
    Check(rendered == expected, name);
    if (rendered != expected) std::cout << "    rendered:\n" << rendered << "\n";
}

void RunApplyCase(const ConfigTable<FixtureConfig>& table, const fs::path& dir) {
    const std::string name = dir.filename().string();
    std::vector<std::string> expected_fields;
    std::vector<std::string> expected_sources;
    std::vector<std::string> expected_diagnostics;
    for (const std::vector<std::string>& row : TsvRows(dir / "expected.tsv")) {
        if (row[0] == "field" && row.size() == 3) {
            expected_fields.push_back(row[1] + "=" + Unescape(row[2]));
        } else if (row[0] == "source" && row.size() == 3) {
            expected_sources.push_back(row[1] + "=" + row[2]);
        } else if (row[0] == "diagnostic" && row.size() == 4) {
            expected_diagnostics.push_back(row[1] + " " + row[2] + " " + Unescape(row[3]));
        } else {
            throw std::runtime_error("malformed row in " + (dir / "expected.tsv").string());
        }
    }

    const CanonicalIni doc = ParseCanonicalIni(ReadBytes(dir / "input.ini"));
    FixtureConfig config;
    config.not_in_table = 99;
    config.udp_port = 1;
    config.write_log = true;
    config.hook_offsets.clear();
    ApplyReport report;
    if (fs::exists(dir / "effective.tsv")) {
        std::vector<Concept> from_defaults_ini;
        const FixtureConfig effective = Effective(dir, from_defaults_ini);
        const detail::EffectiveApplyResult result =
            detail::ApplyCanonicalEffective(doc, table, config, effective, from_defaults_ini);
        report = result.report;
        CheckRows(SourceRows(result.sources), expected_sources, name + ": sources");
    } else {
        if (!expected_sources.empty()) throw std::runtime_error(name + " lists sources and has no effective.tsv");
        report = ApplyCanonical(doc, table, config);
    }
    CheckRows(FieldRows(config), expected_fields, name + ": field values");
    CheckRows(DiagnosticRows(report), expected_diagnostics, name + ": diagnostics");
    Check(config.not_in_table == 99, name + ": a field no row binds is left alone");
    CheckRoundTrip(table, config, name);
}

void RunRenderCase(const ConfigTable<FixtureConfig>& table, const fs::path& dir) {
    const std::string name = dir.filename().string();
    const FixtureConfig values = Values(dir);
    CheckBytes(RenderCanonical(table, values, kHeader), dir / "expected.ini", name + ": rendered bytes");
    CheckRoundTrip(table, values, name);
}

// The fresh file reads back as the defaults with no diagnostic.
void RunFreshCase(const ConfigTable<FixtureConfig>& table, const fs::path& dir) {
    const std::string name = dir.filename().string();
    const std::string rendered = RenderCanonicalFresh(table, kHeader);
    CheckBytes(rendered, dir / "fresh.ini", name + ": fresh bytes");
    const CanonicalIni doc = ParseCanonicalIni(rendered);
    FixtureConfig applied;
    applied.udp_port = 1;
    const ApplyReport report = ApplyCanonical(doc, table, applied);
    Check(doc.diagnostics.empty() && report.diagnostics.empty(), name + ": the fresh file reads with no diagnostic");
    CheckRows(FieldRows(applied), FieldRows(FixtureConfig{}), name + ": the fresh file reads back as the defaults");
}

// The migrated file reads back, over the same effective defaults, as the values it was written from.
void RunMigrationCase(const ConfigTable<FixtureConfig>& table, const fs::path& dir) {
    const std::string name = dir.filename().string();
    std::vector<Concept> from_defaults_ini;
    const FixtureConfig effective = Effective(dir, from_defaults_ini);
    const FixtureConfig values = Values(dir);
    const std::string rendered = detail::RenderCanonicalMigration(table, values, effective, kHeader);
    CheckBytes(rendered, dir / "migration.ini", name + ": migration bytes");
    const CanonicalIni doc = ParseCanonicalIni(rendered);
    FixtureConfig applied;
    applied.udp_port = 1;
    const detail::EffectiveApplyResult result =
        detail::ApplyCanonicalEffective(doc, table, applied, effective, from_defaults_ini);
    Check(doc.diagnostics.empty() && result.report.diagnostics.empty(),
          name + ": the migrated file reads with no diagnostic");
    CheckRows(FieldRows(applied), FieldRows(values), name + ": the migrated file reads back as the values");
}

void TestFixtures() {
    std::cout << "\nConfig table fixtures:\n";
    const ConfigTable<FixtureConfig> table = FixtureTable();
    const fs::path root = fs::path(CAMERAUNLOCK_CANONICAL_INI_FIXTURES) / "table";
    std::vector<fs::path> cases;
    for (const fs::directory_entry& entry : fs::directory_iterator(root)) cases.push_back(entry.path());
    std::sort(cases.begin(), cases.end());
    Check(cases.size() == 23, "twenty-three table fixture cases");
    for (const fs::path& dir : cases) {
        if (fs::exists(dir / "input.ini")) {
            RunApplyCase(table, dir);
            continue;
        }
        bool ran = false;
        if (fs::exists(dir / "fresh.ini")) {
            RunFreshCase(table, dir);
            ran = true;
        }
        if (fs::exists(dir / "expected.ini")) {
            RunRenderCase(table, dir);
            ran = true;
        }
        if (fs::exists(dir / "migration.ini")) {
            RunMigrationCase(table, dir);
            ran = true;
        }
        if (!ran) throw std::runtime_error(dir.string() + " holds no input.ini, fresh.ini, expected.ini or migration.ini");
    }
}

// A concept's field type is checked at compile time.
static_assert(FieldHoldsConcept<Concept::UdpPort, std::uint16_t>());
static_assert(FieldHoldsConcept<Concept::UdpPort, int>());
static_assert(!FieldHoldsConcept<Concept::UdpPort, std::int16_t>());
static_assert(!FieldHoldsConcept<Concept::UdpPort, bool>());
static_assert(!FieldHoldsConcept<Concept::UdpPort, float>());
static_assert(!FieldHoldsConcept<Concept::DataFreshnessMs, std::uint16_t>());
static_assert(FieldHoldsConcept<Concept::DataFreshnessMs, std::uint32_t>());
static_assert(FieldHoldsConcept<Concept::DataFreshnessMs, int>());
static_assert(!FieldHoldsConcept<Concept::CollisionChannel, std::uint32_t>());
static_assert(FieldHoldsConcept<Concept::CollisionChannel, int>());
static_assert(FieldHoldsConcept<Concept::CollisionChannel, long long>());
static_assert(FieldHoldsConcept<Concept::LocalSmoothing, double>());
static_assert(FieldHoldsConcept<Concept::LocalSmoothing, float>());
static_assert(!FieldHoldsConcept<Concept::LocalSmoothing, int>());
static_assert(FieldHoldsConcept<Concept::EnableOnStartup, bool>());
static_assert(!FieldHoldsConcept<Concept::EnableOnStartup, int>());
static_assert(FieldHoldsConcept<Concept::ToggleKey, std::string>());
static_assert(!FieldHoldsConcept<Concept::ToggleKey, int>());

struct Small {
    bool rotation = true;
    bool position = true;
    int value = 5;
    int other = 1;
    float scale = 1.0f;
    std::string text = "a";
    std::string key = "End";
    bool flag = false;
};

void TestConstructionChecks() {
    std::cout << "\nConfig table construction checks:\n";
    using S = Small;

    Check(Contains(Thrown([] {
                       ConfigTable<S>()
                           .Local("Camera", "Offset", &S::value, IntCodec<int>(), "One.")
                           .Local("Debug", "OFFSET", &S::other, IntCodec<int>(), "Two.");
                   }),
                   "a key name is used once in the file"),
          "two rows with one key name in different sections throw");
    Check(Contains(Thrown([] {
                       ConfigTable<S>()
                           .Concept<Concept::RotationEnabled>(&S::rotation)
                           .Concept<Concept::RotationEnabled>(&S::position);
                   }),
                   "a key name is used once in the file"),
          "a concept bound twice throws");
    for (const char* key : {"ConfigFormat", "Configformat"}) {
        Check(Contains(Thrown([&] { ConfigTable<S>().Local("Debug", key, &S::value, IntCodec<int>(), "One."); }),
                       "[CameraUnlock] ConfigFormat already has that key, and a key name is used once in the file"),
              std::string("a local key named ") + key + " throws");
    }
    Check(Contains(Thrown([] { ConfigTable<S>().Local("CameraUnlock", "Offset", &S::value, IntCodec<int>(), "One."); }),
                   "[CameraUnlock] belongs to core"),
          "a local row in [CameraUnlock] throws");
    Check(Contains(Thrown([] { ConfigTable<S>().Local("cameraunlock", "Offset", &S::value, IntCodec<int>(), "One."); }),
                   "PascalCase"),
          "a lower-case section throws");
    for (const char* section : {"Sensitivity", "Inversion", "Reticle"}) {
        Check(Contains(Thrown([&] { ConfigTable<S>().Local(section, "Deadzone", &S::scale, FloatCodec(), "One."); }),
                       "holds none of the settings a canonical file writes"),
              std::string("a local row in [") + section + "] throws");
    }
    for (const char* section : {"Deadzone", "DeadZone"}) {
        Check(Contains(Thrown([&] { ConfigTable<S>().Local(section, "YawDegrees", &S::scale, FloatCodec(), "One."); }),
                       "] holds only settings a canonical file does not carry, so it has no rows: The mod applies the "
                       "head pose as the tracker sends it, with no deadzone of its own."),
              std::string("a local row in [") + section + "] throws");
    }
    Check(Thrown([] { ConfigTable<S>().Local("Position", "LeanTraceLength", &S::scale, FloatCodec(), "One."); }) ==
              "(nothing thrown)",
          "a local row in [Position] is allowed");
    Check(Contains(Thrown([] { ConfigTable<S>().Local("POSITION", "LeanTraceLength", &S::scale, FloatCodec(), "One."); }),
                   "the schema spells this section [Position]"),
          "a schema section in other letter case throws");
    Check(Contains(Thrown([] {
                       ConfigTable<S>()
                           .Local("Camera", "Offset", &S::value, IntCodec<int>(), "One.")
                           .Local("CAMERA", "Other", &S::other, IntCodec<int>(), "Two.");
                   }),
                   "spells this section [Camera]"),
          "a local section in other letter case throws");
    for (const char* key : {"Port", "UdpPort", "YawSensitivity", "InvertX", "Smoothing", "RecenterKey", "LimitYDown"}) {
        Check(Contains(Thrown([&] { ConfigTable<S>().Local("Camera", key, &S::value, IntCodec<int>(), "One."); }),
                       "is the key or an alias of the schema concept"),
              std::string("a local key named ") + key + " throws");
    }
    for (const char* key : {"Deadzone", "DeadzoneDeg", "YawDeadzone", "EnableDeadzone", "ResponseCurve", "RollCurve",
                            "DeadzoneMin", "CurveStrength", "SignYaw", "RotScale", "YawGain", "PositionSensitivity",
                            "Sensitivity", "LeanScale"}) {
        Check(Contains(Thrown([&] { ConfigTable<S>().Local("Camera", key, &S::value, IntCodec<int>(), "One."); }),
                       "names a setting a canonical file does not carry, so a game-local row cannot use it: The mod "
                       "applies the head pose as the tracker sends it"),
              std::string("a local key named ") + key + " throws");
    }
    for (const char* key : {"PositionScale", "WorldScale", "UnitsPerMeter"}) {
        Check(Contains(Thrown([&] { ConfigTable<S>().Local("Camera", key, &S::value, IntCodec<int>(), "One."); }),
                       "names a setting a canonical file does not carry, so a game-local row cannot use it: The mod "
                       "converts your head movement to the game's units itself"),
              std::string("a local key named ") + key + " throws");
    }
    for (const schema::NonCanonicalKey& other : schema::kNonCanonicalKeys) {
        Check(cameraunlock::ResolveConfigKey(other.normalized) == nullptr,
              std::string("the flat readers' alias table does not know ") + other.normalized);
    }
    Check(Contains(Thrown([] { ConfigTable<S>().Local("Camera", "cb_size", &S::value, IntCodec<int>(), "One."); }),
                   "PascalCase"),
          "a key that is not PascalCase throws");
    Check(Contains(Thrown([] { ConfigTable<S>().Local("Camera", "Offset", &S::value, IntCodec<int>(), ""); }),
                   "needs a comment"),
          "a first local row with no comment throws");
    Check(Contains(Thrown([] {
                       ConfigTable<S>()
                           .Local("Camera", "Offset", &S::value, IntCodec<int>(), "One.")
                           .Local("Debug", "Other", &S::other, IntCodec<int>(), "");
                   }),
                   "needs a comment"),
          "a row with no comment after a row of another section throws");
    Check(Contains(Thrown([] {
                       ConfigTable<S>()
                           .Local("Position", "LeanTraceLength", &S::scale, FloatCodec(), "One.")
                           .Concept<Concept::PositionEnabled>(&S::position)
                           .Local("Camera", "Offset", &S::value, IntCodec<int>(), "");
                   }),
                   "needs a comment"),
          "a local row after a concept row of another section still needs a comment");
    Check(Contains(Thrown([] {
                       ConfigTable<S>()
                           .Concept<Concept::PositionEnabled>(&S::position)
                           .Local("Position", "LeanTraceLength", &S::scale, FloatCodec(), "");
                   }),
                   "needs a comment"),
          "a concept row's comment does not cover a local row");
    Check(Thrown([] {
              ConfigTable<S>()
                  .Local("Camera", "Offset", &S::value, IntCodec<int>(), "One.")
                  .Local("Debug", "Flag", &S::flag, BoolCodec(), "Two.")
                  .Local("Camera", "Other", &S::other, IntCodec<int>(), "");
          }) == "(nothing thrown)",
          "a row with no comment after an earlier local row of its section is allowed");
    for (const char* comment : {"one\n\ntwo", " lead", "trail ", "tab\there", "caf\xC3\xA9"}) {
        std::string shown;
        for (const char c : std::string(comment)) shown += c == '\n' ? std::string("\\n") : std::string(1, c);
        Check(Contains(Thrown([&] { ConfigTable<S>().Local("Camera", "Offset", &S::value, IntCodec<int>(), comment); }),
                       "comment line"),
              "comment '" + shown + "' throws");
    }
    Check(Contains(Thrown([] { ConfigTable<S>().Local("Camera", "Offset", &S::value, IntCodec<int>(1, 3), "One."); }),
                   "has a default it cannot write"),
          "a local default outside its codec's range throws");
    Check(Contains(Thrown([] {
                       S s;
                       s.value = 0;
                       ConfigTable<S>(s).Concept<Concept::UdpPort>(&S::value);
                   }),
                   "[Network] UdpPort has a default it cannot write"),
          "a concept default outside the schema range throws");
    Check(Contains(Thrown([] {
                       S s;
                       s.key = "end";
                       ConfigTable<S>(s).Concept<Concept::ToggleKey>(&S::key);
                   }),
                   "expected 'End'"),
          "a hotkey default that is not canonical text throws");
    Check(Contains(Thrown([] {
                       S s;
                       s.rotation = false;
                       s.position = false;
                       ConfigTable<S>(s)
                           .Concept<Concept::RotationEnabled>(&S::rotation)
                           .Concept<Concept::PositionEnabled>(&S::position);
                   }),
                   "not a tracking mode"),
          "both mode switches defaulting to false throws");
    Check(Contains(Thrown([] {
                       EnumCodec<CameraMode>({{"controlRotation", CameraMode::kControlRotation}});
                   }),
                   "PascalCase"),
          "an enum token that is not PascalCase throws");

    ConfigTable<S> table;
    table.Local("Camera", "Offset", &S::value, IntCodec<int>(), "One.");
    Check(Contains(Thrown([&] { table.Local("Camera", "OFFSET", &S::other, IntCodec<int>(), "Two."); }),
                   "a key name is used once"),
          "a failed row throws");
    Check(RenderCanonical(table, S{}, RenderHeader{"G"}).find("OFFSET") == std::string::npos,
          "and leaves the table as it was");
}

void TestModifiers() {
    std::cout << "\nConfig table modifiers:\n";
    using S = Small;

    Check(Contains(Thrown([] { ConfigTable<S>().Engine(); }), "Engine needs a row"), "a modifier with no row throws");
    Check(Contains(Thrown([] {
                       ConfigTable<S>().Local("Camera", "Offset", &S::value, IntCodec<int>(), "One.").Comment("Two.");
                   }),
                   "carries its comment in Local"),
          "Comment on a local row throws");
    Check(Contains(Thrown([] { ConfigTable<S>().Concept<Concept::EnableOnStartup>(&S::flag).Comment(""); }),
                   "needs a comment"),
          "an empty Comment throws");
    Check(Contains(Thrown([] { ConfigTable<S>().Concept<Concept::DataFreshnessMs>(&S::value).Range(1, 2); }),
                   "has the schema's range"),
          "Range on a concept row throws");
    Check(Contains(Thrown([] {
                       ConfigTable<S>().Local("Camera", "Text", &S::text, StringCodec(), "One.").Range(0, 1);
                   }),
                   "Range applies to an int, float or double row"),
          "Range on a string row throws");
    Check(Contains(Thrown([] {
                       ConfigTable<S>().Local("Camera", "Offset", &S::value, IntCodec<int>(), "One.").Range(0.5, 10);
                   }),
                   "not two whole numbers"),
          "Range(0.5, 10) on an int row throws");
    Check(Contains(Thrown([] {
                       ConfigTable<S>().Local("Camera", "Offset", &S::value, IntCodec<int>(), "One.").Range(0, 3e9);
                   }),
                   "not two whole numbers"),
          "Range past int on an int row throws");
    Check(Contains(Thrown([] {
                       ConfigTable<S>().Local("Camera", "Offset", &S::value, IntCodec<int>(), "One.").Range(6, 10);
                   }),
                   "has a default it cannot write"),
          "a Range that excludes the default throws");
    Check(Contains(Thrown([] { ConfigTable<S>().Select(Concept::UdpPort); }), "no row for UdpPort"),
          "Select of an unbound concept throws");

    // Select reaches a concept row a helper added before the local rows.
    auto helper = [] {
        ConfigTable<S> table;
        table.Concept<Concept::EnableOnStartup>(&S::flag).Concept<Concept::RotationEnabled>(&S::rotation);
        return table;
    };
    ConfigTable<S> table = helper();
    table.Local("Camera", "Offset", &S::value, IntCodec<int>(), "One.")
        .Select(Concept::EnableOnStartup)
        .Comment("Starts it.")
        .Engine();
    S values;
    const std::string rendered = RenderCanonical(table, values, RenderHeader{"G"});
    Check(Contains(rendered, "\r\n[General]\r\n; Starts it.\r\n; EnableOnStartup=false\r\n; true: turning"),
          "Select then Comment and Engine change the selected row");
    values.flag = true;
    Check(Contains(RenderCanonical(table, values, RenderHeader{"G"}), "; Starts it.\r\nEnableOnStartup=true\r\n"),
          "an Engine row away from its default is an active line");
    Check(!Contains(rendered, "Hotkeys are key names"), "no hotkey line without a hotkey row");
    Check(rendered.rfind("; G head tracking settings.\r\n; Comments start with", 0) == 0, "the header names the game");
}

void TestRenderAndApply() {
    std::cout << "\nConfig table render and apply:\n";
    using S = Small;
    ConfigTable<S> table;
    table.Local("Camera", "Offset", &S::value, IntCodec<int>(), "One.")
        .Range(0, 10)
        .Concept<Concept::ToggleKey>(&S::key);

    for (const char* name : {"", " G", "G ", "Caf\xC3\xA9", "A\tB"}) {
        Check(Contains(Thrown([&] { RenderCanonical(table, S{}, RenderHeader{name}); }), "display name"),
              "display name '" + std::string(name) + "' throws");
    }
    S bad;
    bad.value = 11;
    Check(Contains(Thrown([&] { RenderCanonical(table, bad, RenderHeader{"G"}); }), "[Camera] Offset: 11 is outside"),
          "a value outside its range throws naming the row");
    bad.value = 1;
    bad.key = "end";
    Check(Contains(Thrown([&] { RenderCanonical(table, bad, RenderHeader{"G"}); }), "[Hotkeys] ToggleKey: 'end'"),
          "a hotkey value that is not canonical text throws naming the row");

    S s;
    Check(Contains(Thrown([&] { ApplyCanonical(ParseCanonicalIni(std::string("[A]\0", 4)), table, s); }), "NulByte"),
          "Apply of an unreadable document throws");

    const ApplyReport report =
        ApplyCanonical(ParseCanonicalIni("[Hotkeys]\nToggleKey=0x23, ctrl+0xBA\n[Camera]\nOffset=4\n"), table, s);
    Check(report.diagnostics.empty() && s.key == "End, Ctrl+0xBA" && s.value == 4,
          "the native dialect reads a code and writes a named one by its name");
    Check(HotkeyCodec().Parse("0xFF").error ==
              "'0xFF' is not a code from 0x01 to 0xFE: expected 0x and one or two hex digits",
          "a code outside 0x01-0xFE is refused");
}

const char* const kFreshRowTail =
    ". A fresh file writes default on this row, which takes Defaults.ini's value, so the row's own default must be the "
    "schema's, or the row must be marked PerGame().";

// The same checks, with the same messages, as ConfigTableFixtures.RunGlobalChecks.
void TestGlobalChecks() {
    std::cout << "\nConfig table fresh render, PerGame and the default token:\n";
    using S = Small;
    const RenderHeader header{"G"};

    Check(Thrown([&] { RenderCanonicalFresh(ConfigTable<S>().Concept<Concept::UdpPort>(&S::value), header); }) ==
              std::string("[Network] UdpPort defaults to 5, and the schema to 4242") + kFreshRowTail,
          "an int row off the schema's default throws from the fresh render, naming it");
    Check(Thrown([&] { RenderCanonicalFresh(ConfigTable<S>().Concept<Concept::ToggleKey>(&S::key), header); }) ==
              std::string("[Hotkeys] ToggleKey defaults to End, and the schema to End, Ctrl+Shift+Y") + kFreshRowTail,
          "a hotkey row off the schema's list throws");
    Check(Thrown([&] { RenderCanonicalFresh(ConfigTable<S>().Concept<Concept::LocalSmoothing>(&S::scale), header); }) ==
              std::string("[Smoothing] LocalSmoothing defaults to 1.0, and the schema to 0.0") + kFreshRowTail,
          "a float row off the schema's default throws");

    const std::string per_game =
        RenderCanonicalFresh(ConfigTable<S>().Concept<Concept::UdpPort>(&S::value).PerGame(), header);
    Check(Contains(per_game, "\r\nUdpPort=5\r\n") && !Contains(per_game, "Defaults.ini"),
          "the same row marked PerGame renders its value, and no other concept row means no Defaults.ini lines");

    Check(Thrown([&] { RenderCanonicalFresh(ConfigTable<S>().Concept<Concept::RotationEnabled>(&S::rotation), header); }) ==
              "the table binds [General] RotationEnabled without [Position] PositionEnabled, and the tracking mode is "
              "the two of them together",
          "RotationEnabled without PositionEnabled throws");
    Check(Thrown([&] { RenderCanonicalFresh(ConfigTable<S>().Concept<Concept::PositionEnabled>(&S::position), header); }) ==
              "(nothing thrown)",
          "PositionEnabled alone renders");

    Check(Thrown([] { ConfigTable<S>().Local("Camera", "Offset", &S::value, IntCodec<int>(), "One.").PerGame(); }) ==
              "[Camera] Offset is a local row, which never takes a value from Defaults.ini",
          "PerGame on a local row throws");

    S keys_defaults;
    keys_defaults.text = "End, Ctrl+Shift+Y";
    ConfigTable<S> keyed(keys_defaults);
    keyed.Concept<Concept::ToggleKey>(&S::text)
        .Concept<Concept::RotationEnabled>(&S::rotation)
        .Concept<Concept::PositionEnabled>(&S::position)
        .PerGame();
    S config;
    config.text = "F1";
    const ApplyReport report = ApplyCanonical(ParseCanonicalIni("[Hotkeys]\r\nToggleKey=End, default\r\n"), keyed, config);
    Check(report.diagnostics.size() == 1 && report.diagnostics[0].kind == CanonicalDiagnosticKind::InvalidValue &&
              config.text == "End, Ctrl+Shift+Y",
          "End, default is a key list with an item that is no key, not the token");

    const CanonicalIni empty = ParseCanonicalIni("");
    ConfigTable<S> pair;
    pair.Concept<Concept::RotationEnabled>(&S::rotation).Concept<Concept::PositionEnabled>(&S::position);
    S both_off;
    both_off.rotation = false;
    both_off.position = false;
    S target;
    Check(Contains(Thrown([&] { detail::ApplyCanonicalEffective(empty, pair, target, both_off, {}); }),
                   "both start false"),
          "effective defaults that name no tracking mode throw");
    Check(Contains(Thrown([&] {
                       detail::ApplyCanonicalEffective(empty, keyed, target, keyed.defaults(), {Concept::PositionEnabled});
                   }),
                   "PositionEnabled is not a row of this table that follows Defaults.ini"),
          "a PerGame row named as coming from Defaults.ini throws");
    Check(Contains(Thrown([&] {
                       detail::ApplyCanonicalEffective(empty, keyed, target, keyed.defaults(), {Concept::UdpPort});
                   }),
                   "UdpPort is not a row of this table that follows Defaults.ini"),
          "a concept the table does not bind named as coming from Defaults.ini throws");
    S effective = keyed.defaults();
    effective.position = false;
    S applied;
    applied.rotation = false;
    applied.position = false;
    const detail::EffectiveApplyResult result =
        detail::ApplyCanonicalEffective(empty, keyed, applied, effective, {Concept::RotationEnabled});
    Check(applied.rotation && applied.position && result.sources[1] == detail::ValueSource::kDefaultsIni &&
              result.sources[2] == detail::ValueSource::kBuiltIn,
          "a PerGame row starts from the table's default, whatever the effective defaults hold");
}

}  // namespace

int RunConfigTableTests() {
    std::cout << "\n=== Config table tests ===\n";
    g_failures = 0;
    try {
        TestFixtures();
        TestConstructionChecks();
        TestModifiers();
        TestRenderAndApply();
        TestGlobalChecks();
    } catch (const std::exception& e) {
        std::cout << "  [FAIL] unexpected exception: " << e.what() << "\n";
        ++g_failures;
    }
    return g_failures;
}
