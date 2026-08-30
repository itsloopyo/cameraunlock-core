// Tests for the ConfigVersion stamp and the shipped-default migration it gates.
//
// The case that drives this is RE8's [Position] InvertX, which shipped true and
// mirrored the lateral lean. Neither delivery path reached an existing user with
// the corrected value: the launcher seeds a config only when the file is absent,
// and Load writes the file only when it could not read one. So Load has to
// correct the key in place, and it has to do that without disturbing the port,
// sensitivities, limits and hotkeys the user set - which is what most of this
// file checks.

#include <cameraunlock/reframework/plugin_config.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

using cameraunlock::reframework::kPluginConfigVersion;
using cameraunlock::reframework::PluginConfig;
using cameraunlock::reframework::PluginConfigSchema;

// RE8's schema as resident-evil-village-headtracking/src/core/config.h builds it.
const PluginConfigSchema kRe8Schema{
    /*title*/ "RE8 Head Tracking",
    /*positionInvertKeys*/ true,
    /*flashlight*/ false,
    /*diagnosticMarkerKey*/ true,
    /*positionSensitivity*/ 1.0f,
    /*modId*/ "re8",
};

// RE4's. Stands in for the other five RE mods, whose shipped values did not
// change, so no migration is keyed on their mod id.
const PluginConfigSchema kRe4Schema{
    /*title*/ "RE4 Head Tracking",
    /*positionInvertKeys*/ true,
    /*flashlight*/ false,
    /*diagnosticMarkerKey*/ false,
    /*positionSensitivity*/ 2.0f,
    /*modId*/ "re4",
};

// Absolute, because IniReader is GetPrivateProfileStringA on Windows and that
// resolves a relative path against the Windows directory rather than the working
// one - every read comes back as the default and the migration looks inert.
class TempIni {
public:
    explicit TempIni(const char* name)
        : m_path((std::filesystem::temp_directory_path() / name).string()) {}
    ~TempIni() { std::remove(m_path.c_str()); }

    TempIni(const TempIni&) = delete;
    TempIni& operator=(const TempIni&) = delete;

    const char* Path() const { return m_path.c_str(); }

    void Write(const std::string& content) const {
        std::ofstream out(m_path, std::ios::binary | std::ios::trunc);
        out << content;
    }

    std::string Read() const {
        std::ifstream in(m_path, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

private:
    std::string m_path;
};

// An INI as an RE8 user who has been running the mod for a while has it: the
// pre-fix InvertX, values they changed themselves across four sections, a
// comment they added, and a key this build knows nothing about.
std::string UserEditedRe8Ini() {
    return
        "; RE8 Head Tracking Configuration\n"
        "; Delete this file to reset to defaults\n"
        "\n"
        "[Network]\n"
        "; UDP port for OpenTrack data (default: 4242)\n"
        "UDPPort=5555\n"
        "\n"
        "[Sensitivity]\n"
        "YawMultiplier=1.4\n"
        "PitchMultiplier=0.8\n"
        "RollMultiplier=0.0\n"
        "\n"
        "[Smoothing]\n"
        "LocalSmoothing=0.25\n"
        "RemoteSmoothing=0.4\n"
        "\n"
        "[Position]\n"
        "; my phone sits low so Y needs help\n"
        "SensitivityX=1.5\n"
        "SensitivityY=2.5\n"
        "SensitivityZ=1.5\n"
        "LimitX=0.45\n"
        "LimitY=0.35\n"
        "LimitZ=0.60\n"
        "LimitZBack=0.15\n"
        "; Invert position axes\n"
        "InvertX=true\n"
        "InvertY=false\n"
        "InvertZ=true\n"
        "Enabled=true\n"
        "SomeKeyThisBuildDoesNotKnow=keep me\n"
        "\n"
        "[Hotkeys]\n"
        "; Virtual key codes (hex)\n"
        "ToggleKey=0x24\n"
        "PositionToggleKey=0x21\n"
        "YawModeKey=0x22\n"
        "DiagnosticMarkerKey=0x79\n"
        "\n"
        "[General]\n"
        "AutoEnable=false\n"
        "WorldSpaceYaw=false\n";
}

bool Contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

void TestMigratesOnlyTheOneKey() {
    TempIni ini("plugin_config_migration_re8.ini");
    const std::string before = UserEditedRe8Ini();
    ini.Write(before);

    PluginConfig config;
    Check(config.Load(ini.Path(), kRe8Schema), "an existing RE8 config still loads");
    Check(!config.positionInvertX, "InvertX is corrected in memory");
    Check(config.configVersion == kPluginConfigVersion, "the loaded config reports the new stamp");

    const std::string after = ini.Read();

    // The migrated file line by line against the original: exactly two lines may
    // differ, the InvertX value and the appended stamp. Everything the user set,
    // every comment and the key this build does not understand come through
    // untouched, which is the whole point of editing in place.
    std::vector<std::string> beforeLines;
    std::vector<std::string> afterLines;
    for (int pass = 0; pass < 2; ++pass) {
        std::istringstream stream(pass == 0 ? before : after);
        std::string line;
        while (std::getline(stream, line)) {
            (pass == 0 ? beforeLines : afterLines).push_back(line);
        }
    }

    Check(afterLines.size() == beforeLines.size() + 1, "the migration adds exactly one line");

    size_t differing = 0;
    for (size_t i = 0; i < beforeLines.size() && i < afterLines.size(); ++i) {
        if (beforeLines[i] != afterLines[i]) ++differing;
    }
    Check(differing == 1, "exactly one existing line changed");

    Check(Contains(after, "\nInvertX=false\n"), "InvertX is false on disk");
    Check(!Contains(after, "InvertX=true"), "the old InvertX value is gone");
    Check(Contains(after, "\nInvertY=false\n"), "InvertY is untouched");
    Check(Contains(after, "\nInvertZ=true\n"), "InvertZ keeps the user's true");
    Check(Contains(after, "\nUDPPort=5555\n"), "the user's port survives");
    Check(Contains(after, "\nSensitivityY=2.5\n"), "the user's sensitivity survives");
    Check(Contains(after, "\nLimitZ=0.60\n"), "the user's limit survives");
    Check(Contains(after, "\nToggleKey=0x24\n"), "the user's hotkey survives");
    Check(Contains(after, "\nAutoEnable=false\n"), "the user's General values survive");
    Check(Contains(after, "; my phone sits low so Y needs help"), "the user's comment survives");
    Check(Contains(after, "\nSomeKeyThisBuildDoesNotKnow=keep me\n"),
          "a key this build does not read survives");
    Check(Contains(after, "\nConfigVersion=1\n"), "the stamp is written");
    Check(after.find("ConfigVersion") > after.find("[General]"),
          "the stamp lands in [General]");

    // The values the migrated file now holds have to read back the same way.
    PluginConfig reloaded;
    reloaded.Load(ini.Path(), kRe8Schema);
    Check(reloaded.udpPort == 5555, "reload keeps the port");
    Check(reloaded.positionInvertZ, "reload keeps InvertZ");
    Check(reloaded.toggleKey == 0x24, "reload keeps the hotkey");
    Check(!reloaded.autoEnable, "reload keeps AutoEnable");
    Check(ini.Read() == after, "a second load leaves the stamped file byte for byte");
}

void TestStampedConfigKeepsADeliberateTrue() {
    TempIni ini("plugin_config_migration_deliberate.ini");
    ini.Write(
        "[Position]\n"
        "InvertX=true\n"
        "\n"
        "[General]\n"
        "ConfigVersion=1\n");

    PluginConfig config;
    config.Load(ini.Path(), kRe8Schema);
    Check(config.positionInvertX, "a stamped config keeps InvertX=true in memory");
    Check(Contains(ini.Read(), "InvertX=true"), "a stamped config keeps InvertX=true on disk");
}

// The migration is stamped as well as applied, so the flip happens once: a user
// who wanted the mirrored lean sets it back on a file that is now at the current
// version and keeps it.
void TestReinstatedTrueSurvivesTheNextLoad() {
    TempIni ini("plugin_config_migration_reinstated.ini");
    ini.Write(
        "[Position]\n"
        "InvertX=true\n"
        "\n"
        "[General]\n"
        "AutoEnable=true\n");

    PluginConfig first;
    first.Load(ini.Path(), kRe8Schema);
    Check(!first.positionInvertX, "the unstamped config is migrated once");

    std::string text = ini.Read();
    const size_t at = text.find("InvertX=false");
    Check(at != std::string::npos, "the migrated file holds InvertX=false to edit back");
    if (at == std::string::npos) return;
    text.replace(at, std::string("InvertX=false").size(), "InvertX=true");
    ini.Write(text);

    PluginConfig second;
    second.Load(ini.Path(), kRe8Schema);
    Check(second.positionInvertX, "the value set back after the migration is kept");
}

void TestOtherModsAreNotTouched() {
    TempIni ini("plugin_config_migration_re4.ini");
    const std::string before =
        "; RE4 Head Tracking Configuration\n"
        "\n"
        "[Position]\n"
        "InvertX=true\n"
        "InvertY=false\n"
        "InvertZ=false\n"
        "\n"
        "[General]\n"
        "AutoEnable=true\n";
    ini.Write(before);

    PluginConfig config;
    config.Load(ini.Path(), kRe4Schema);
    Check(config.positionInvertX, "a mod whose shipped value did not change keeps InvertX");
    Check(config.configVersion == 0, "and is not stamped");
    Check(ini.Read() == before, "and its config is not rewritten");
}

void TestSaveStampsWhatItWrites() {
    TempIni ini("plugin_config_migration_save.ini");
    PluginConfig config;
    config.SetDefaults(kRe8Schema);
    Check(config.Save(ini.Path(), kRe8Schema), "Save writes");
    // Save writes in text mode, so the line endings are the platform's rather
    // than the LF the in-place editor preserves.
    Check(Contains(ini.Read(), "ConfigVersion=1"), "a generated config carries the stamp");

    PluginConfig reloaded;
    reloaded.Load(ini.Path(), kRe8Schema);
    Check(reloaded.configVersion == kPluginConfigVersion, "and reads back at the current version");
    Check(ini.Read().find("InvertX=false") != std::string::npos,
          "and needs no migration");
}

} // namespace

int RunPluginConfigMigrationTests() {
    std::cout << "\n=== Plugin config migration tests ===\n";
    TestMigratesOnlyTheOneKey();
    TestStampedConfigKeepsADeliberateTrue();
    TestReinstatedTrueSurvivesTheNextLoad();
    TestOtherModsAreNotTouched();
    TestSaveStampsWhatItWrites();
    return g_failures;
}
