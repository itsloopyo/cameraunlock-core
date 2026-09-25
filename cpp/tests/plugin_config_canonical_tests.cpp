// PluginConfig on the canonical format: the RE table, the RE legacy import against
// PluginConfig::Load on every shipped RE config (data/fixtures/reframework-legacy) and the
// corpus built from each, and the config owner importing each file into CameraUnlock.ini beside
// it and saving the two Writable rows.

#include <cameraunlock/config/canonical_ini.h>
#include <cameraunlock/config/config_owner.h>
#include <cameraunlock/config/testing/ini_mutations.h>
#include <cameraunlock/input/key_bindings.h>
#include <cameraunlock/reframework/plugin_config.h>
#include <cameraunlock/reframework/plugin_config_table.h>

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace cameraunlock::config;
using cameraunlock::reframework::PluginConfig;
using cameraunlock::reframework::PluginConfigLegacyImport;
using cameraunlock::reframework::PluginConfigSchema;
using cameraunlock::reframework::PluginConfigTable;

int g_failures = 0;

void Check(bool cond, const std::string& name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

struct Fixture {
    const char* file;
    PluginConfigSchema schema;
    const char* game;
};

// Each repo's kConfigSchema (src/core/config.h), which every repo initialises positionally.
const Fixture kFixtures[] = {
    {"resident-evil-2/HeadTracking.ini", {"RE2 Head Tracking", true, false, false, 2.0f, "re2"}, "Resident Evil 2"},
    {"resident-evil-3/HeadTracking.ini", {"RE3 Head Tracking", true, false, false, 2.0f, "re3"}, "Resident Evil 3"},
    {"resident-evil-4/HeadTracking.ini", {"RE4 Head Tracking", true, false, false, 2.0f, "re4"}, "Resident Evil 4"},
    {"resident-evil-7/HeadTracking.ini", {"RE7 Head Tracking", true, false, false, 1.0f, "re7"},
     "Resident Evil 7 biohazard"},
    {"resident-evil-village/HeadTracking.ini", {"RE8 Head Tracking", true, false, true, 1.0f, "re8"},
     "Resident Evil Village"},
    {"resident-evil-requiem/HeadTracking.ini", {"RE9 Head Tracking", false, true, false, 1.0f, "re9"},
     "Resident Evil Requiem"},
    {"resident-evil-requiem/seed.ini", {"RE9 Head Tracking", false, true, false, 1.0f, "re9"},
     "Resident Evil Requiem"},
};

const PluginConfigSchema kRe8Schema{"RE8 Head Tracking", true, false, true, 1.0f, "re8"};

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) throw std::runtime_error("cannot open " + path.string());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) throw std::runtime_error("cannot create " + path.string());
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) throw std::runtime_error("cannot write " + path.string());
}

std::vector<std::string> Listing(const fs::path& dir) {
    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(dir)) names.push_back(entry.path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}

// A fresh, empty folder under the test root.
fs::path Fresh(const fs::path& root, const char* name) {
    const fs::path dir = root / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::vector<std::string> SplitCrlf(const std::string& bytes) {
    std::vector<std::string> lines;
    std::size_t begin = 0;
    for (std::size_t at = bytes.find("\r\n"); at != std::string::npos; at = bytes.find("\r\n", begin)) {
        lines.push_back(bytes.substr(begin, at - begin));
        begin = at + 2;
    }
    lines.push_back(bytes.substr(begin));
    return lines;
}

// The one line that differs between two files of the same line count, or "" when there is not
// exactly one.
std::string OnlyChangedLine(const std::string& before, const std::string& after) {
    const std::vector<std::string> a = SplitCrlf(before);
    const std::vector<std::string> b = SplitCrlf(after);
    if (a.size() != b.size()) return "";
    std::string changed;
    int count = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] == b[i]) continue;
        changed = b[i];
        ++count;
    }
    return count == 1 ? changed : "";
}

bool SameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

std::string Bindings(int vk, int chord_letter) {
    using cameraunlock::input::KeyModifiers;
    std::vector<cameraunlock::input::KeyBinding> bindings{{KeyModifiers::kNone, vk}};
    if (chord_letter != 0) bindings.push_back({KeyModifiers::kCtrl | KeyModifiers::kShift, chord_letter});
    return cameraunlock::input::FormatKeyBindings(bindings);
}

std::string Text(bool value) { return value ? "true" : "false"; }

// A pose-shaping float as the import writes it: the canonical float codec's text.
std::string Text(float value) { return FloatCodec().Render(value); }

// The fields the table carries, as `a` and `b` differ in them (floats bitwise).
void CarriedDifferences(const PluginConfig& a, const PluginConfig& b, std::vector<std::string>& out) {
    const auto f = [&](const char* name, float x, float y) {
        if (!SameBits(x, y)) out.push_back(std::string(name) + " " + std::to_string(x) + " vs " + std::to_string(y));
    };
    const auto s = [&](const char* name, const std::string& x, const std::string& y) {
        if (x != y) out.push_back(std::string(name) + " '" + x + "' vs '" + y + "'");
    };
    const auto t = [&](const char* name, bool x, bool y) {
        if (x != y) out.push_back(std::string(name) + " " + Text(x) + " vs " + Text(y));
    };
    if (a.udpPort != b.udpPort) {
        out.push_back("udpPort " + std::to_string(a.udpPort) + " vs " + std::to_string(b.udpPort));
    }
    t("autoEnable", a.autoEnable, b.autoEnable);
    t("worldSpaceYaw", a.worldSpaceYaw, b.worldSpaceYaw);
    f("localSmoothing", a.localSmoothing, b.localSmoothing);
    f("remoteSmoothing", a.remoteSmoothing, b.remoteSmoothing);
    t("positionEnabled", a.positionEnabled, b.positionEnabled);
    f("positionLimitX", a.positionLimitX, b.positionLimitX);
    f("positionLimitY", a.positionLimitY, b.positionLimitY);
    f("positionLimitZ", a.positionLimitZ, b.positionLimitZ);
    f("positionLimitZBack", a.positionLimitZBack, b.positionLimitZBack);
    t("flashlightTracking", a.flashlightTracking, b.flashlightTracking);
    f("flashlightMultiplier", a.flashlightMultiplier, b.flashlightMultiplier);
    s("toggleKeyBindings", a.toggleKeyBindings, b.toggleKeyBindings);
    s("cycleTrackingModeKeyBindings", a.cycleTrackingModeKeyBindings, b.cycleTrackingModeKeyBindings);
    s("yawModeKeyBindings", a.yawModeKeyBindings, b.yawModeKeyBindings);
    s("diagnosticMarkerKeyBindings", a.diagnosticMarkerKeyBindings, b.diagnosticMarkerKeyBindings);
}

// What the import should give for a file PluginConfig::Load read as `loaded`: every carried field
// as Load read it, the hotkey codes with the legacy chords, every other field at SetDefaults, the
// sensitivities and the inversions Read reads for the schema as Load read them beside their
// SetDefaults values, folded where the two are equal, and those Load holds away from SetDefaults
// as dropped.
std::vector<std::string> ImportDifferences(const PluginConfigSchema& schema, const PluginConfig& loaded,
                                           const PluginConfig& imported, const ImportResult& result) {
    std::vector<std::string> out;
    if (result.status != ImportStatus::Imported) out.push_back("status " + std::to_string(static_cast<int>(result.status)));

    PluginConfig shipped;
    shipped.SetDefaults(schema);
    PluginConfig expected = shipped;
    expected.udpPort = loaded.udpPort;
    expected.autoEnable = loaded.autoEnable;
    expected.worldSpaceYaw = loaded.worldSpaceYaw;
    expected.localSmoothing = loaded.localSmoothing;
    expected.remoteSmoothing = loaded.remoteSmoothing;
    expected.positionEnabled = loaded.positionEnabled;
    expected.positionLimitX = loaded.positionLimitX;
    expected.positionLimitY = loaded.positionLimitY;
    expected.positionLimitZ = loaded.positionLimitZ;
    expected.positionLimitZBack = loaded.positionLimitZBack;
    expected.flashlightTracking = loaded.flashlightTracking;
    expected.flashlightMultiplier = loaded.flashlightMultiplier;
    expected.toggleKeyBindings = Bindings(loaded.toggleKey, 'Y');
    expected.cycleTrackingModeKeyBindings = Bindings(loaded.positionToggleKey, 'G');
    expected.yawModeKeyBindings = Bindings(loaded.yawModeKey, 'H');
    expected.diagnosticMarkerKeyBindings = Bindings(loaded.diagnosticMarkerKey, 0);
    CarriedDifferences(expected, imported, out);

    const auto same = [&](const char* name, bool equal) {
        if (!equal) out.push_back(std::string(name) + " is not its SetDefaults value");
    };
    same("yawMultiplier", SameBits(imported.yawMultiplier, shipped.yawMultiplier));
    same("pitchMultiplier", SameBits(imported.pitchMultiplier, shipped.pitchMultiplier));
    same("rollMultiplier", SameBits(imported.rollMultiplier, shipped.rollMultiplier));
    same("positionSensitivityX", SameBits(imported.positionSensitivityX, shipped.positionSensitivityX));
    same("positionSensitivityY", SameBits(imported.positionSensitivityY, shipped.positionSensitivityY));
    same("positionSensitivityZ", SameBits(imported.positionSensitivityZ, shipped.positionSensitivityZ));
    same("positionInvertX", imported.positionInvertX == shipped.positionInvertX);
    same("positionInvertY", imported.positionInvertY == shipped.positionInvertY);
    same("positionInvertZ", imported.positionInvertZ == shipped.positionInvertZ);
    same("toggleKey", imported.toggleKey == shipped.toggleKey);
    same("positionToggleKey", imported.positionToggleKey == shipped.positionToggleKey);
    same("yawModeKey", imported.yawModeKey == shipped.yawModeKey);
    same("diagnosticMarkerKey", imported.diagnosticMarkerKey == shipped.diagnosticMarkerKey);
    same("configVersion", imported.configVersion == shipped.configVersion);

    std::vector<std::string> drops;
    std::vector<std::string> pose;
    const auto shaping = [&](const char* section, const char* key, const std::string& value, const std::string& was) {
        const bool folded = value == was;
        pose.push_back(std::string(section) + " " + key + "=" + value + " shipped " + was + (folded ? " folded" : ""));
        if (!folded) drops.push_back(std::string(section) + " " + key + "=" + value);
    };
    shaping("Sensitivity", "YawMultiplier", Text(loaded.yawMultiplier), Text(shipped.yawMultiplier));
    shaping("Sensitivity", "PitchMultiplier", Text(loaded.pitchMultiplier), Text(shipped.pitchMultiplier));
    shaping("Sensitivity", "RollMultiplier", Text(loaded.rollMultiplier), Text(shipped.rollMultiplier));
    shaping("Position", "SensitivityX", Text(loaded.positionSensitivityX), Text(shipped.positionSensitivityX));
    shaping("Position", "SensitivityY", Text(loaded.positionSensitivityY), Text(shipped.positionSensitivityY));
    shaping("Position", "SensitivityZ", Text(loaded.positionSensitivityZ), Text(shipped.positionSensitivityZ));
    if (schema.positionInvertKeys) {
        shaping("Position", "InvertX", Text(loaded.positionInvertX), Text(shipped.positionInvertX));
        shaping("Position", "InvertY", Text(loaded.positionInvertY), Text(shipped.positionInvertY));
        shaping("Position", "InvertZ", Text(loaded.positionInvertZ), Text(shipped.positionInvertZ));
    }
    std::vector<std::string> got;
    for (const DroppedValue& d : result.dropped) {
        if (d.rule != DropRule::PoseShaping) out.push_back("dropped with rule " + std::to_string(static_cast<int>(d.rule)));
        got.push_back(d.section + " " + d.key + "=" + d.value);
    }
    std::vector<std::string> got_pose;
    for (const PoseShapingValue& p : result.pose_shaping) {
        got_pose.push_back(p.section + " " + p.key + "=" + p.value + " shipped " + p.shipped + (p.folded ? " folded" : ""));
    }
    const auto compare = [&](const char* what, const std::vector<std::string>& actual,
                             const std::vector<std::string>& expected) {
        if (actual == expected) return;
        std::string text = std::string(what) + " [";
        for (const std::string& d : actual) text += d + "; ";
        text += "] expected [";
        for (const std::string& d : expected) text += d + "; ";
        out.push_back(text + "]");
    };
    compare("dropped", got, drops);
    compare("pose shaping", got_pose, pose);
    return out;
}

std::string Join(const std::vector<std::string>& items) {
    std::string text;
    for (const std::string& item : items) text += (text.empty() ? "" : "; ") + item;
    return text;
}

// What PluginConfig::Read gives, written out by hand. The import is compared with Load, which
// calls Read, so a change inside Read, SetDefaults or PluginConfig's initialisers moves both sides
// of that comparison; only literal values catch it.
struct ReadPin {
    bool found = true;
    int udpPort = 4242;
    float yaw = 1.0f, pitch = 1.0f, roll = 1.0f;
    float local = 0.0f, remote = 0.15f;
    int toggleKey = 0x23, positionToggleKey = 0x21, yawModeKey = 0x22, diagnosticMarkerKey = 0x78;
    float sensX = 1.0f, sensY = 1.0f, sensZ = 1.0f;
    float limitX = 0.3f, limitY = 0.2f, limitZ = 0.4f, limitZBack = 0.1f;
    bool invertX = false, invertY = false, invertZ = false;
    bool positionEnabled = true;
    bool flashlightTracking = true;
    float flashlightMultiplier = 1.5f;
    bool autoEnable = true, worldSpaceYaw = true;
    int configVersion = 0;
};

ReadPin PinWithSensitivity(float sensitivity) {
    ReadPin pin;
    pin.sensX = pin.sensY = pin.sensZ = sensitivity;
    return pin;
}

std::vector<std::string> ReadPinDifferences(const PluginConfig& c, bool found, const ReadPin& p) {
    std::vector<std::string> out;
    const auto f = [&](const char* name, float got, float want) {
        if (!SameBits(got, want)) out.push_back(std::string(name) + " " + std::to_string(got) + " not " + std::to_string(want));
    };
    const auto i = [&](const char* name, int got, int want) {
        if (got != want) out.push_back(std::string(name) + " " + std::to_string(got) + " not " + std::to_string(want));
    };
    const auto t = [&](const char* name, bool got, bool want) {
        if (got != want) out.push_back(std::string(name) + " " + Text(got) + " not " + Text(want));
    };
    t("found", found, p.found);
    i("udpPort", c.udpPort, p.udpPort);
    f("yawMultiplier", c.yawMultiplier, p.yaw);
    f("pitchMultiplier", c.pitchMultiplier, p.pitch);
    f("rollMultiplier", c.rollMultiplier, p.roll);
    f("localSmoothing", c.localSmoothing, p.local);
    f("remoteSmoothing", c.remoteSmoothing, p.remote);
    i("toggleKey", c.toggleKey, p.toggleKey);
    i("positionToggleKey", c.positionToggleKey, p.positionToggleKey);
    i("yawModeKey", c.yawModeKey, p.yawModeKey);
    i("diagnosticMarkerKey", c.diagnosticMarkerKey, p.diagnosticMarkerKey);
    f("positionSensitivityX", c.positionSensitivityX, p.sensX);
    f("positionSensitivityY", c.positionSensitivityY, p.sensY);
    f("positionSensitivityZ", c.positionSensitivityZ, p.sensZ);
    f("positionLimitX", c.positionLimitX, p.limitX);
    f("positionLimitY", c.positionLimitY, p.limitY);
    f("positionLimitZ", c.positionLimitZ, p.limitZ);
    f("positionLimitZBack", c.positionLimitZBack, p.limitZBack);
    t("positionInvertX", c.positionInvertX, p.invertX);
    t("positionInvertY", c.positionInvertY, p.invertY);
    t("positionInvertZ", c.positionInvertZ, p.invertZ);
    t("positionEnabled", c.positionEnabled, p.positionEnabled);
    t("flashlightTracking", c.flashlightTracking, p.flashlightTracking);
    f("flashlightMultiplier", c.flashlightMultiplier, p.flashlightMultiplier);
    t("autoEnable", c.autoEnable, p.autoEnable);
    t("worldSpaceYaw", c.worldSpaceYaw, p.worldSpaceYaw);
    i("configVersion", c.configVersion, p.configVersion);
    return out;
}

void CheckRead(PluginConfig& config, const fs::path& file, const PluginConfigSchema& schema, const ReadPin& want,
               const std::string& name) {
    const bool found = config.Read(file.string().c_str(), schema);
    const std::vector<std::string> differences = ReadPinDifferences(config, found, want);
    Check(differences.empty(), "Read pin: " + name + " " + Join(differences));
}

void TestReadPinned(const fs::path& root) {
    const ReadPin kPins[] = {
        PinWithSensitivity(2.0f), PinWithSensitivity(2.0f), PinWithSensitivity(2.0f), ReadPin{},
        [] {
            ReadPin village;
            village.invertX = true;
            return village;
        }(),
        ReadPin{}, PinWithSensitivity(2.0f),
    };
    static_assert(std::size(kPins) == std::size(kFixtures), "one pin per fixture");
    for (std::size_t n = 0; n < std::size(kFixtures); ++n) {
        PluginConfig config;
        CheckRead(config, fs::path(CAMERAUNLOCK_REFRAMEWORK_LEGACY_FIXTURES) / kFixtures[n].file, kFixtures[n].schema,
                  kPins[n], kFixtures[n].file);
    }

    const std::string odd =
        "[Network]\nUDPPort=80\n"
        "[Sensitivity]\nYawMultiplier=9\nPitchMultiplier=-1\nRollMultiplier=abc\n"
        "[Smoothing]\nLocalSmoothing=0,15\nRemoteSmoothing=1.5\n"
        "[Hotkeys]\nToggleKey=0x230\nPositionToggleKey=0x11\nYawModeKey=zz\nDiagnosticMarkerKey=0x7A\n"
        "[Position]\nSensitivityX=11\nSensitivityY=0.5 ; half\nSensitivityZ=nan\nLimitX=3\nLimitY=0.25\n"
        "LimitZ=0x1\nLimitZBack=-0.5\nInvertX=yes\nInvertY=maybe\nInvertZ=1\nEnabled=off\n"
        "[Flashlight]\nEnabled=false\nMultiplier=7\n"
        "[General]\nAutoEnable=0\nWorldSpaceYaw=False\nConfigVersion=3\n";
    const fs::path dir = Fresh(root, "read-pin");
    const fs::path file = dir / "HeadTracking.ini";
    WriteBytes(file, odd);

    const PluginConfigSchema every{"Pin", true, true, true, 2.0f, "pin"};
    ReadPin all;
    all.yaw = 5.0f;
    all.pitch = 0.0f;
    all.remote = 1.0f;
    all.diagnosticMarkerKey = 0x7A;
    all.sensX = 10.0f;
    all.sensY = 0.5f;
    all.sensZ = 2.0f;
    all.limitX = 2.0f;
    all.limitY = 0.25f;
    all.limitZBack = 0.0f;
    all.invertX = true;
    all.invertZ = true;
    all.positionEnabled = false;
    all.flashlightTracking = false;
    all.flashlightMultiplier = 5.0f;
    all.autoEnable = false;
    all.worldSpaceYaw = false;
    all.configVersion = 3;
    PluginConfig config;
    CheckRead(config, file, every, all,
              "out-of-range values clamp, unparseable ones and unpollable keys keep the defaults");

    ReadPin none = all;
    none.diagnosticMarkerKey = 0x78;
    none.sensZ = 1.0f;
    none.invertX = none.invertZ = false;
    none.flashlightTracking = true;
    none.flashlightMultiplier = 1.5f;
    PluginConfig bare;
    CheckRead(bare, file, PluginConfigSchema{"Pin", false, false, false, 1.0f, ""}, none,
              "a schema without the optional keys leaves them at their defaults");

    ReadPin absent = PinWithSensitivity(2.0f);
    absent.found = false;
    CheckRead(config, dir / "missing.ini", every, absent, "no file gives the defaults over a filled config");

    WriteBytes(file, "");
    CheckRead(config, file, every, PinWithSensitivity(2.0f), "an empty file is found, on the defaults");
}

// The corpus description of every key the import reads, with the alternates taken against what
// Read gives the shipped file.
std::vector<testing::MutationKey> CorpusKeys(const std::vector<LegacyKey>& reads, const PluginConfig& base) {
    std::vector<testing::MutationKey> keys;
    for (const LegacyKey& read : reads) {
        testing::MutationKey k;
        k.section = read.section;
        k.key = read.key;
        const std::string& s = read.section;
        const std::string& key = read.key;
        if (s == "Network" && key == "UDPPort") {
            k.alternate = "5555";
            k.out_of_range = {"80", "70000"};
        } else if (s == "Sensitivity") {
            k.alternate = "1.5";
            k.out_of_range = {"6", "-1"};
        } else if (s == "Smoothing") {
            k.alternate = "0.3";
            k.out_of_range = {"1.5", "-0.5"};
        } else if (s == "Position" && key == "Smoothing") {
            k.alternate = "0.5";
        } else if (s == "Hotkeys") {
            k.hotkey = true;
            k.alternate = key == "ToggleKey" ? "0x24" : key == "PositionToggleKey" ? "0x2D" : key == "YawModeKey" ? "0x2E" : "0x79";
            k.out_of_range = {"0x11"};
        } else if (s == "Position" && key.rfind("Sensitivity", 0) == 0) {
            k.alternate = "1.5";
            k.out_of_range = {"11", "-1"};
        } else if (s == "Position" && key.rfind("Limit", 0) == 0) {
            k.alternate = "0.5";
            k.out_of_range = {"2.5", "-0.1"};
        } else if (s == "Position" && key == "InvertX") {
            k.alternate = Text(!base.positionInvertX);
        } else if (s == "Position" && key == "InvertY") {
            k.alternate = Text(!base.positionInvertY);
        } else if (s == "Position" && key == "InvertZ") {
            k.alternate = Text(!base.positionInvertZ);
        } else if (s == "Position" && key == "Enabled") {
            k.alternate = Text(!base.positionEnabled);
        } else if (s == "Flashlight" && key == "Enabled") {
            k.alternate = Text(!base.flashlightTracking);
        } else if (s == "Flashlight" && key == "Multiplier") {
            k.alternate = "2.5";
            k.out_of_range = {"6", "-1"};
        } else if (s == "General" && key == "AutoEnable") {
            k.alternate = Text(!base.autoEnable);
        } else if (s == "General" && key == "WorldSpaceYaw") {
            k.alternate = Text(!base.worldSpaceYaw);
        } else if (s == "General" && key == "ConfigVersion") {
            k.alternate = "1";
        } else {
            throw std::logic_error("no corpus description for [" + s + "] " + key);
        }
        keys.push_back(std::move(k));
    }
    return keys;
}

// The owner PluginMod builds with canonicalConfig: CameraUnlock.ini in `dir`, importing the
// legacy HeadTracking.ini beside it.
ConfigOwnerOptions<PluginConfig> OwnerOptions(const Fixture& f, const fs::path& dir) {
    ConfigOwnerOptions<PluginConfig> options;
    options.path = (dir / "CameraUnlock.ini").wstring();
    options.table = PluginConfigTable(f.schema);
    options.import = PluginConfigLegacyImport(f.schema);
    options.legacy_path = (dir / "HeadTracking.ini").wstring();
    options.header.display_name = f.game;
    return options;
}

// Every corpus input: Load on one copy against the import on another, the import's copy
// unchanged and alone in its folder. With `convert`, the owner then imports a third copy into
// CameraUnlock.ini with the import's values, leaving that copy as it was and nothing but the
// two files in its folder; that pass costs about 16 s a fixture, so it runs
// where RE8's and Requiem's schemas between them bind every row of the table.
void TestCorpus(const fs::path& root, const Fixture& f, bool convert) {
    const std::string base = ReadBytes(fs::path(CAMERAUNLOCK_REFRAMEWORK_LEGACY_FIXTURES) / f.file);
    const LegacyImport<PluginConfig> import = PluginConfigLegacyImport(f.schema);
    const ConfigTable<PluginConfig> table = PluginConfigTable(f.schema);

    const fs::path shipped_dir = Fresh(root, "shipped");
    WriteBytes(shipped_dir / "HeadTracking.ini", base);
    PluginConfig shipped_read;
    shipped_read.Read((shipped_dir / "HeadTracking.ini").string().c_str(), f.schema);

    std::vector<testing::IniMutation> inputs{{"shipped", base}};
    const std::vector<testing::IniMutation> corpus =
        testing::GenerateIniMutations(base, import.keys, CorpusKeys(import.keys, shipped_read));
    inputs.insert(inputs.end(), corpus.begin(), corpus.end());

    const auto started = std::chrono::steady_clock::now();
    int load_mismatches = 0;
    int changed_copies = 0;
    int unconverted = 0;
    int changed_legacy = 0;
    for (const testing::IniMutation& input : inputs) {
        const fs::path load_file = Fresh(root, "load") / "HeadTracking.ini";
        const fs::path import_dir = Fresh(root, "import");
        const fs::path import_file = import_dir / "HeadTracking.ini";
        WriteBytes(load_file, input.bytes);
        WriteBytes(import_file, input.bytes);

        PluginConfig loaded;
        loaded.Load(load_file.string().c_str(), f.schema);

        PluginConfig imported = table.defaults();
        const ImportResult result = import.run(detail::OwnerLegacyInput(import_file.wstring()), imported);
        const std::vector<std::string> differences = ImportDifferences(f.schema, loaded, imported, result);
        if (!differences.empty()) {
            ++load_mismatches;
            std::cout << "  [FAIL] " << f.file << " " << input.name << ": " << Join(differences) << "\n";
        }
        if (ReadBytes(import_file) != input.bytes || Listing(import_dir) != std::vector<std::string>{"HeadTracking.ini"}) {
            ++changed_copies;
            std::cout << "  [FAIL] " << f.file << " " << input.name << ": the import changed its folder\n";
        }

        if (!convert) continue;
        const fs::path owner_dir = Fresh(root, "owner");
        WriteBytes(owner_dir / "HeadTracking.ini", input.bytes);
        ConfigOwner<PluginConfig> owner(OwnerOptions(f, owner_dir));
        const ConfigLoadResult<PluginConfig> converted = owner.Load();
        std::vector<std::string> owner_differences;
        CarriedDifferences(imported, converted.config, owner_differences);
        if (converted.status != ConfigLoadStatus::Migrated || !owner_differences.empty()) {
            ++unconverted;
            std::cout << "  [FAIL] " << f.file << " " << input.name << ": the owner gave "
                      << ConfigLoadStatusName(converted.status) << " " << Join(owner_differences) << " "
                      << Join(converted.log) << "\n";
        }
        if (ReadBytes(owner_dir / "HeadTracking.ini") != input.bytes ||
            Listing(owner_dir) != std::vector<std::string>{"CameraUnlock.ini", "HeadTracking.ini"}) {
            ++changed_legacy;
            std::cout << "  [FAIL] " << f.file << " " << input.name
                      << ": the owner changed HeadTracking.ini or left more than the two files\n";
        }
    }
    const auto seconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count() / 1000.0;
    const std::string label = std::string(f.file) + ", " + std::to_string(inputs.size()) + " inputs";
    Check(load_mismatches == 0, label + ": the import gives the fields PluginConfig::Load gives");
    Check(changed_copies == 0, label + ": the import leaves its copy and its folder unchanged");
    if (convert) {
        Check(unconverted == 0, label + ": the owner imports each with the import's values");
        Check(changed_legacy == 0,
              label + ": into CameraUnlock.ini, leaving HeadTracking.ini as it was and nothing else beside them");
    }
    std::cout << "  " << label << ": " << seconds << " s\n";
}

// The shipped file through the owner: imported once into CameraUnlock.ini, which reads back as the
// import gave it, the legacy HeadTracking.ini left byte for byte as it was, nothing written by a
// second load, and each Writable row saved as one line.
void TestOwnerConvertsShippedFile(const fs::path& root, const Fixture& f) {
    const std::string base = ReadBytes(fs::path(CAMERAUNLOCK_REFRAMEWORK_LEGACY_FIXTURES) / f.file);
    const std::string name = f.file;
    const fs::path import_file = Fresh(root, "import") / "HeadTracking.ini";
    WriteBytes(import_file, base);
    const ConfigTable<PluginConfig> table = PluginConfigTable(f.schema);
    PluginConfig imported = table.defaults();
    PluginConfigLegacyImport(f.schema).run(detail::OwnerLegacyInput(import_file.wstring()), imported);

    const fs::path dir = Fresh(root, "convert");
    const fs::path file = dir / "CameraUnlock.ini";
    const fs::path legacy = dir / "HeadTracking.ini";
    const std::vector<std::string> both{"CameraUnlock.ini", "HeadTracking.ini"};
    WriteBytes(legacy, base);
    ConfigOwner<PluginConfig> owner(OwnerOptions(f, dir));
    const ConfigLoadResult<PluginConfig> loaded = owner.Load();
    Check(loaded.status == ConfigLoadStatus::Migrated, name + ": imported");

    const std::string converted = ReadBytes(file);
    const CanonicalIni doc = ParseCanonicalIni(converted);
    PluginConfig reread = table.defaults();
    const ApplyReport report = ApplyCanonical(doc, table, reread);
    std::vector<std::string> differences;
    CarriedDifferences(imported, reread, differences);
    Check(HasCanonicalStamp(converted) && doc.IsReadable() && doc.diagnostics.empty() && report.diagnostics.empty(),
          name + ": the new file is stamped and reads with no diagnostics");
    Check(differences.empty(), name + ": and reads back as the import gave it " + Join(differences));
    Check(Contains(converted, "\r\nToggleKey=" + imported.toggleKeyBindings + "\r\n") &&
              Contains(imported.toggleKeyBindings, "Ctrl+Shift+Y") &&
              Contains(converted, "\r\nCycleTrackingModeKey=" + imported.cycleTrackingModeKeyBindings + "\r\n") &&
              Contains(imported.cycleTrackingModeKeyBindings, "Ctrl+Shift+G") &&
              Contains(converted, "\r\nYawModeKey=" + imported.yawModeKeyBindings + "\r\n") &&
              Contains(imported.yawModeKeyBindings, "Ctrl+Shift+H"),
          name + ": its hotkey lists carry the chords");
    Check(!Contains(converted, "ConfigVersion=1") && !Contains(converted, "PositionToggleKey") &&
              !Contains(converted, "Sensitivity") && !Contains(converted, "Invert") &&
              !Contains(converted, "RotationEnabled") && !Contains(converted, "PositionLimitYDown"),
          name + ": and holds no legacy, pose-shaping or two-mode keys");
    Check(ReadBytes(legacy) == base, name + ": HeadTracking.ini is left byte for byte as it was");
    Check(Listing(dir) == both, name + ": and nothing else is beside them");

    ConfigOwner<PluginConfig> again(OwnerOptions(f, dir));
    const ConfigLoadResult<PluginConfig> second = again.Load();
    Check(second.status == ConfigLoadStatus::Canonical && second.log.size() == 1 &&
              Contains(second.log[0], "CameraUnlock.ini: settings are read from this file. ") &&
              Contains(second.log[0], "HeadTracking.ini is left as it was and is not read."),
          name + ": a second load reads CameraUnlock.ini as canonical and says HeadTracking.ini is not read");
    Check(ReadBytes(file) == converted && ReadBytes(legacy) == base && Listing(dir) == both, name + ": and writes nothing");

    const ConfigSaveResult position = again.Save([](PluginConfig& c) { c.positionEnabled = false; });
    const std::string after_position = ReadBytes(file);
    Check(position.status == ConfigSaveStatus::Saved &&
              OnlyChangedLine(converted, after_position) == "PositionEnabled=false",
          name + ": saving PositionEnabled edits that one row");
    const ConfigSaveResult yaw = again.Save([](PluginConfig& c) { c.worldSpaceYaw = false; });
    Check(yaw.status == ConfigSaveStatus::Saved && OnlyChangedLine(after_position, ReadBytes(file)) == "WorldSpaceYaw=false",
          name + ": saving WorldSpaceYaw edits that one row");
}

void TestConversionLog(const fs::path& root) {
    const auto convert = [&](const Fixture& f) {
        const fs::path dir = Fresh(root, "log");
        const std::string base = ReadBytes(fs::path(CAMERAUNLOCK_REFRAMEWORK_LEGACY_FIXTURES) / f.file);
        WriteBytes(dir / "HeadTracking.ini", base);
        ConfigOwner<PluginConfig> owner(OwnerOptions(f, dir));
        const ConfigLoadResult<PluginConfig> loaded = owner.Load();
        Check(loaded.status == ConfigLoadStatus::Migrated && ReadBytes(dir / "HeadTracking.ini") == base &&
                  Listing(dir) == std::vector<std::string>{"CameraUnlock.ini", "HeadTracking.ini"},
              std::string(f.file) + ": imported into CameraUnlock.ini, HeadTracking.ini left as it was");
        return Join(loaded.log);
    };
    const std::string re2 = convert(kFixtures[0]);
    Check(Contains(re2, "not carried: [Hotkeys] ReticleToggleKey=0x2D") && Contains(re2, "not carried: [Reticle] Enabled=true"),
          "RE2: the reticle toggle's lines are logged as not carried");
    const std::string village = convert(kFixtures[4]);
    Check(!Contains(village, "InvertX"), "RE8: the shipped InvertX=true is corrected, not dropped");
    // Requiem's position sensitivity is 1.0, what v0.4.0's installer ships; the 2.0 its v0.4.0
    // launcher seed carried was drift (data/config-format.json conversion_notes).
    const std::string seed = convert(kFixtures[6]);
    Check(Contains(seed, "not carried: [Position] SensitivityX=2.0, ") &&
              Contains(seed, "not carried: [Position] SensitivityY=2.0, ") &&
              Contains(seed, "not carried: [Position] SensitivityZ=2.0, "),
          "Requiem's launcher seed: each position sensitivity of 2 is logged as dropped");
    Check(!Contains(convert(kFixtures[5]), "[Position] Sensitivity"), "Requiem's shipped 1.0 drops nothing");
}

void TestImportDropsAndCorrection(const fs::path& root) {
    const std::string text =
        "[Sensitivity]\nYawMultiplier=1.4\nPitchMultiplier=0.8\n[Position]\nSensitivityY=2.5\n"
        "InvertX=true\nInvertZ=true\n[Hotkeys]\nToggleKey=0x59\nDiagnosticMarkerKey=0x7A\n";
    const LegacyImport<PluginConfig> import = PluginConfigLegacyImport(kRe8Schema);
    const fs::path file = Fresh(root, "drops") / "HeadTracking.ini";

    WriteBytes(file, text);
    PluginConfig out = PluginConfigTable(kRe8Schema).defaults();
    ImportResult result = import.run(detail::OwnerLegacyInput(file.wstring()), out);
    std::vector<std::string> lines;
    for (const DroppedValue& d : result.dropped) lines.push_back(DescribeDroppedValue(d));
    Check(result.status == ImportStatus::Imported && lines.size() == 4 &&
              lines[0].rfind("not carried: [Sensitivity] YawMultiplier=1.4, ", 0) == 0 &&
              lines[1].rfind("not carried: [Sensitivity] PitchMultiplier=0.8, ", 0) == 0 &&
              lines[2].rfind("not carried: [Position] SensitivityY=2.5, ", 0) == 0 &&
              lines[3].rfind("not carried: [Position] InvertZ=true, ", 0) == 0,
          "an unstamped RE8 file: sensitivities and InvertZ dropped, InvertX corrected " + Join(lines));
    const auto entry = [&](std::size_t i) {
        const PoseShapingValue& p = result.pose_shaping.at(i);
        return p.section + " " + p.key + "=" + p.value + " shipped " + p.shipped + (p.folded ? " folded" : "");
    };
    Check(result.pose_shaping.size() == 9 && entry(0) == "Sensitivity YawMultiplier=1.4 shipped 1.0" &&
              entry(2) == "Sensitivity RollMultiplier=1.0 shipped 1.0 folded" &&
              entry(4) == "Position SensitivityY=2.5 shipped 1.0" &&
              entry(6) == "Position InvertX=false shipped false folded" &&
              entry(8) == "Position InvertZ=true shipped false",
          "the import lists the nine pose-shaping values beside their SetDefaults values, the corrected InvertX folded");
    Check(out.toggleKeyBindings == "Y, Ctrl+Shift+Y" && out.diagnosticMarkerKeyBindings == "F11",
          "a key that is also the chord's letter lists both, and the marker key has no chord");

    WriteBytes(file, text + "[General]\nConfigVersion=1\n");
    out = PluginConfigTable(kRe8Schema).defaults();
    result = import.run(detail::OwnerLegacyInput(file.wstring()), out);
    Check(result.dropped.size() == 5 && result.dropped[3].key == "InvertX" && result.dropped[3].value == "true",
          "a stamped RE8 file keeps InvertX=true, so it is dropped");
}

std::string KeysText(const PluginConfigSchema& schema) {
    std::string text;
    for (const LegacyKey& k : PluginConfigLegacyImport(schema).keys) text += "[" + k.section + "] " + k.key + "\n";
    return text;
}

// Each shipped file holds SetDefaults' shaping, which PluginMod applies, so the import folds every
// pose-shaping value and drops none. Requiem's launcher seed is not a shipped file: its drifted
// position sensitivities are dropped (TestConversionLog).
void TestShippedShapingIsFolded(const fs::path& root) {
    for (const Fixture& f : kFixtures) {
        const std::string name = f.file;
        if (name == "resident-evil-requiem/seed.ini") continue;
        const fs::path file = Fresh(root, "folded") / "HeadTracking.ini";
        WriteBytes(file, ReadBytes(fs::path(CAMERAUNLOCK_REFRAMEWORK_LEGACY_FIXTURES) / f.file));
        PluginConfig out = PluginConfigTable(f.schema).defaults();
        const ImportResult result =
            PluginConfigLegacyImport(f.schema).run(detail::OwnerLegacyInput(file.wstring()), out);
        const bool all_folded = std::all_of(result.pose_shaping.begin(), result.pose_shaping.end(),
                                            [](const PoseShapingValue& p) { return p.folded && p.value == p.shipped; });
        const std::size_t read = f.schema.positionInvertKeys ? 9 : 6;
        Check(result.pose_shaping.size() == read && all_folded && result.dropped.empty(),
              name + ": every pose-shaping value Read reads is the shipped one, folded, and nothing is dropped");
    }
}

void TestImportKeysAreWhatReadReads() {
    const std::string common_head =
        "[Network] UDPPort\n[Sensitivity] YawMultiplier\n[Sensitivity] PitchMultiplier\n[Sensitivity] RollMultiplier\n"
        "[Smoothing] LocalSmoothing\n[Smoothing] RemoteSmoothing\n[Position] Smoothing\n"
        "[Hotkeys] ToggleKey\n[Hotkeys] PositionToggleKey\n[Hotkeys] YawModeKey\n";
    const std::string limits =
        "[Position] SensitivityX\n[Position] SensitivityY\n[Position] SensitivityZ\n[Position] LimitX\n"
        "[Position] LimitY\n[Position] LimitZ\n[Position] LimitZBack\n";
    const std::string tail = "[General] AutoEnable\n[General] WorldSpaceYaw\n[General] ConfigVersion\n";
    Check(KeysText(kRe8Schema) == common_head + "[Hotkeys] DiagnosticMarkerKey\n" + limits +
                                      "[Position] InvertX\n[Position] InvertY\n[Position] InvertZ\n[Position] Enabled\n" +
                                      tail,
          "RE8's import lists every key Read reads for its schema, in Read's order");
    Check(KeysText(kFixtures[5].schema) ==
              common_head + limits + "[Position] Enabled\n[Flashlight] Enabled\n[Flashlight] Multiplier\n" + tail,
          "and Requiem's");
}

void TestImportAbsent(const fs::path& root) {
    const LegacyImport<PluginConfig> import = PluginConfigLegacyImport(kRe8Schema);
    const ConfigTable<PluginConfig> table = PluginConfigTable(kRe8Schema);
    const fs::path dir = Fresh(root, "absent");

    PluginConfig out = table.defaults();
    ImportResult result = import.run(detail::OwnerLegacyInput((dir / "HeadTracking.ini").wstring()), out);
    std::vector<std::string> differences;
    CarriedDifferences(table.defaults(), out, differences);
    Check(result.status == ImportStatus::Absent && result.dropped.empty() && differences.empty() &&
              out.toggleKeyBindings == "End, Ctrl+Shift+Y",
          "no file: Absent on the defaults, the chords included " + Join(differences));

    WriteBytes(dir / "HeadTracking.ini", "[Network]\nUDPPort=5555\n");
    LegacyInput lossy = detail::OwnerLegacyInput((dir / "HeadTracking.ini").wstring());
    lossy.ansi_path = (dir / "?" / "HeadTracking.ini").string();
    lossy.ansi_lossy = true;
    out = table.defaults();
    result = import.run(lossy, out);
    Check(result.status == ImportStatus::Absent && out.udpPort == 4242,
          "an ANSI path that lost a character finds no file, as the legacy build found none");
}

std::string RenderDefaults(const PluginConfigSchema& schema, const char* game) {
    const ConfigTable<PluginConfig> table = PluginConfigTable(schema);
    return RenderCanonical(table, table.defaults(), RenderHeader{game});
}

void TestTableRendersTheGamesRows() {
    const std::string expected =
        "; Resident Evil Village head tracking settings.\r\n"
        "; Comments start with ; and go on their own line. Text after a value is part of the value.\r\n"
        "; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty "
        "for none.\r\n"
        "\r\n"
        "[CameraUnlock]\r\n"
        "; Written by the mod. Leave this section in place.\r\n"
        "ConfigFormat=1\r\n"
        "\r\n"
        "[Network]\r\n"
        "; UDP port the mod receives tracker data on (OpenTrack protocol).\r\n"
        "UdpPort=4242\r\n"
        "\r\n"
        "[General]\r\n"
        "; true: head tracking is on when the game starts. ToggleKey turns it on and off.\r\n"
        "EnableOnStartup=true\r\n"
        "; true: yaw turns around the world's up axis. false: around the camera's own up axis.\r\n"
        "WorldSpaceYaw=true\r\n"
        "\r\n"
        "[Smoothing]\r\n"
        "; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.\r\n"
        "LocalSmoothing=0.0\r\n"
        "; Smoothing when the tracker is another device on the network, such as a phone.\r\n"
        "; 0 is the least, 1 the most.\r\n"
        "RemoteSmoothing=0.15\r\n"
        "\r\n"
        "[Position]\r\n"
        "; true: moving your head moves the view.\r\n"
        "; Tracking mode at startup. The mode hotkey turns it on and off and saves it here.\r\n"
        "PositionEnabled=true\r\n"
        "; How far, in metres, leaning left or right can move the view.\r\n"
        "PositionLimitX=0.3\r\n"
        "; How far, in metres, raising or lowering your head can move the view.\r\n"
        "PositionLimitY=0.2\r\n"
        "; How far, in metres, leaning forward can move the view.\r\n"
        "PositionLimitZ=0.4\r\n"
        "; How far, in metres, leaning back can move the view.\r\n"
        "PositionLimitZBack=0.1\r\n"
        "\r\n"
        "[Hotkeys]\r\n"
        "; Turns head tracking on and off.\r\n"
        "ToggleKey=End, Ctrl+Shift+Y\r\n"
        "; Changes the tracking mode: rotation and position, or rotation only.\r\n"
        "CycleTrackingModeKey=PageUp, Ctrl+Shift+G\r\n"
        "; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).\r\n"
        "YawModeKey=PageDown, Ctrl+Shift+H\r\n"
        "; Hides and shows the game's world-anchored markers.\r\n"
        "DiagnosticMarkerKey=F9\r\n";
    const std::string village = RenderDefaults(kRe8Schema, "Resident Evil Village");
    Check(village == expected, "RE8's defaults render as the canonical file above");
    if (village != expected) std::cout << village;

    const std::string requiem = RenderDefaults(kFixtures[5].schema, "Resident Evil Requiem");
    Check(Contains(requiem, "\r\n[Light]\r\n") && Contains(requiem, "\r\nLightFollowsHead=true\r\n") &&
              Contains(requiem, "\r\nLightMultiplier=1.5\r\n") && !Contains(requiem, "DiagnosticMarkerKey"),
          "Requiem's defaults carry [Light] and no marker key");
    Check(!Contains(RenderDefaults(kFixtures[0].schema, "Resident Evil 2"), "[Light]"), "RE2's carry no [Light]");
}

void TestLoadIgnoresCanonicalConfig(const fs::path& root) {
    const std::string base = ReadBytes(fs::path(CAMERAUNLOCK_REFRAMEWORK_LEGACY_FIXTURES) / kFixtures[4].file);
    PluginConfigSchema canonical = kRe8Schema;
    canonical.canonicalConfig = true;
    const fs::path a = Fresh(root, "flag-off") / "HeadTracking.ini";
    const fs::path b = Fresh(root, "flag-on") / "HeadTracking.ini";
    WriteBytes(a, base);
    WriteBytes(b, base);
    PluginConfig off;
    PluginConfig on;
    off.Load(a.string().c_str(), kRe8Schema);
    on.Load(b.string().c_str(), canonical);
    std::vector<std::string> differences;
    CarriedDifferences(off, on, differences);
    Check(differences.empty() && off.toggleKey == on.toggleKey && off.positionInvertX == on.positionInvertX &&
              off.configVersion == on.configVersion && ReadBytes(a) == ReadBytes(b),
          "PluginConfig::Load reads and migrates the same with canonicalConfig set or not");
}

}  // namespace

int RunPluginConfigCanonicalTests() {
    std::cout << "\n=== Plugin config canonical tests ===\n";
    const fs::path root = fs::temp_directory_path() / ("cameraunlock_re_canonical_" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(root);
    fs::create_directories(root);

    TestReadPinned(root);
    TestTableRendersTheGamesRows();
    TestImportKeysAreWhatReadReads();
    TestImportDropsAndCorrection(root);
    TestShippedShapingIsFolded(root);
    TestImportAbsent(root);
    TestLoadIgnoresCanonicalConfig(root);
    TestConversionLog(root);
    for (const Fixture& f : kFixtures) TestOwnerConvertsShippedFile(root, f);
    for (const Fixture& f : kFixtures) {
        const std::string file = f.file;
        TestCorpus(root, f, file == "resident-evil-village/HeadTracking.ini" || file == "resident-evil-requiem/HeadTracking.ini");
    }

    fs::remove_all(root);
    return g_failures;
}
