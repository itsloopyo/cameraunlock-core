// Tests for the ConfigVersion stamp and the shipped-default migration it gates.
//
// The case that drives this is RE8's [Position] InvertX, which shipped true and
// mirrored the lateral lean. Neither delivery path reached an existing user with
// the corrected value: the launcher seeds a config only when the file is absent,
// and Load writes the file only when it could not read one. So Load has to
// correct the key in place, and it has to do that without disturbing the port,
// sensitivities, limits and hotkeys the user set - which is what most of this
// file checks.

#include <cameraunlock/reframework/log_callback.h>
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
        : m_name(name), m_path((std::filesystem::temp_directory_path() / name).string()) {}
    ~TempIni() {
        std::error_code ignored;
        std::filesystem::permissions(m_path, std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::add, ignored);
        std::remove(m_path.c_str());
    }

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

    bool Exists() const { return std::filesystem::exists(m_path); }

    // The checked writer's temporaries are named "<file name>.<32 hex digits>.tmp".
    int TemporariesBeside() const {
        int count = 0;
        const std::string prefix = m_name + ".";
        for (const auto& entry :
             std::filesystem::directory_iterator(std::filesystem::temp_directory_path())) {
            const std::string file = entry.path().filename().string();
            if (file.size() == prefix.size() + 32 + 4 && file.compare(0, prefix.size(), prefix) == 0 &&
                file.compare(file.size() - 4, 4, ".tmp") == 0) {
                ++count;
            }
        }
        return count;
    }

private:
    std::string m_name;
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

// What the plugin logs at error level while one of these is alive.
class ErrorLog {
public:
    ErrorLog() {
        Lines().clear();
        cameraunlock::reframework::SetLogCallback(&Record);
    }
    ~ErrorLog() { cameraunlock::reframework::SetLogCallback(nullptr); }

    ErrorLog(const ErrorLog&) = delete;
    ErrorLog& operator=(const ErrorLog&) = delete;

    bool Has(const char* text) const {
        for (const std::string& line : Lines()) {
            if (Contains(line, text)) return true;
        }
        return false;
    }

private:
    static std::vector<std::string>& Lines() {
        static std::vector<std::string> lines;
        return lines;
    }
    static void Record(cameraunlock::reframework::LogLevel level, const char* message) {
        if (level == cameraunlock::reframework::LogLevel::Error) Lines().push_back(message);
    }
};

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

    std::string expected = before;
    expected.replace(expected.find("InvertX=true\n"), std::string("InvertX=true\n").size(),
                     "InvertX=false\n");
    expected += "ConfigVersion=1\n";
    Check(after == expected, "the migrated file is the original with those two lines, byte for byte");

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
    const std::string stamped =
        "[Position]\n"
        "InvertX=true\n"
        "\n"
        "[General]\n"
        "ConfigVersion=1\n";
    ini.Write(stamped);

    PluginConfig config;
    config.Load(ini.Path(), kRe8Schema);
    Check(config.positionInvertX, "a stamped config keeps InvertX=true in memory");
    Check(ini.Read() == stamped, "a stamped config is not rewritten at all");
}

// Migrates `input` under RE8's schema and checks the whole file it leaves behind,
// then loads that file again: the stamp has to stick and nothing may move twice.
void CheckRe8Migration(const char* name, const std::string& input, const std::string& expected) {
    std::cout << "  -- " << name << "\n";
    TempIni ini(name);
    ini.Write(input);

    PluginConfig config;
    Check(config.Load(ini.Path(), kRe8Schema), "the unstamped config loads");
    Check(!config.positionInvertX, "InvertX is corrected in memory");
    Check(config.configVersion == kPluginConfigVersion, "the load reports the new stamp");
    Check(ini.Read() == expected, "the migrated file is exactly the expected bytes");
    Check(ini.TemporariesBeside() == 0, "no temporary is left beside it");

    PluginConfig reloaded;
    reloaded.Load(ini.Path(), kRe8Schema);
    Check(reloaded.configVersion == kPluginConfigVersion, "the stamp reads back");
    Check(!reloaded.positionInvertX, "InvertX reads back false");
    Check(ini.Read() == expected, "a second load leaves the file alone");

    // Where the migration wrote InvertX=false, a user who wanted the mirrored lean
    // sets it back, and the stamp keeps that choice.
    const size_t at = expected.find("InvertX=false");
    if (at == std::string::npos) return;
    std::string reinstated = expected;
    reinstated.replace(at, std::string("InvertX=false").size(), "InvertX=true");
    ini.Write(reinstated);
    PluginConfig deliberate;
    deliberate.Load(ini.Path(), kRe8Schema);
    Check(deliberate.positionInvertX, "a true set back after the migration is in effect");
    Check(ini.Read() == reinstated, "and the file is left as the user wrote it");
}

// CRLF throughout, as Notepad and the shipped INIs have it. The key keeps the
// user's spelling and spacing, and the stamp goes after [General]'s last setting
// rather than below the user's closing comment.
void TestCrlfFileWithComments() {
    CheckRe8Migration(
        "plugin_config_migration_crlf.ini",
        "; RE8 Head Tracking Configuration\r\n"
        "; Delete this file to reset to defaults\r\n"
        "\r\n"
        "[Position]\r\n"
        "; Invert position axes\r\n"
        "invertx = true\r\n"
        "InvertY=false\r\n"
        "\r\n"
        "[General]\r\n"
        "; Yaw mode: false = camera-local, true = horizon-locked (default)\r\n"
        "WorldSpaceYaw=true\r\n"
        "; my note: I turned AutoEnable off once, it was annoying\r\n"
        "\r\n",
        "; RE8 Head Tracking Configuration\r\n"
        "; Delete this file to reset to defaults\r\n"
        "\r\n"
        "[Position]\r\n"
        "; Invert position axes\r\n"
        "invertx = false\r\n"
        "InvertY=false\r\n"
        "\r\n"
        "[General]\r\n"
        "; Yaw mode: false = camera-local, true = horizon-locked (default)\r\n"
        "WorldSpaceYaw=true\r\n"
        "ConfigVersion=1\r\n"
        "; my note: I turned AutoEnable off once, it was annoying\r\n"
        "\r\n");
}

// GetPrivateProfileIntA reads "0 ; ..." as 0, so a commented stamp is still
// migrated, and the comment stays on the line.
void TestInlineCommentOnTheStampSurvives() {
    CheckRe8Migration(
        "plugin_config_migration_inline.ini",
        "[Position]\n"
        "InvertX=true\n"
        "\n"
        "[General]\n"
        "ConfigVersion=0 ; I lowered this to get the fix again\n"
        "AutoEnable=true\n",
        "[Position]\n"
        "InvertX=false\n"
        "\n"
        "[General]\n"
        "ConfigVersion=1 ; I lowered this to get the fix again\n"
        "AutoEnable=true\n");
}

// The stamp is inserted after a last line that has no newline. It must go on a
// line of its own, and the file still ends without a newline.
void TestNoFinalNewlineInsertsOnItsOwnLine() {
    CheckRe8Migration(
        "plugin_config_migration_unterminated.ini",
        "[Position]\n"
        "InvertX=true\n"
        "\n"
        "[General]\n"
        "AutoEnable=true",
        "[Position]\n"
        "InvertX=false\n"
        "\n"
        "[General]\n"
        "AutoEnable=true\n"
        "ConfigVersion=1");
}

// The unterminated last line is the one being corrected, and [General] has to be
// added after it.
void TestNoFinalNewlineCrlfWithoutGeneral() {
    CheckRe8Migration(
        "plugin_config_migration_unterminated_crlf.ini",
        "[Network]\r\n"
        "UDPPort=5555\r\n"
        "\r\n"
        "[Position]\r\n"
        "InvertX=true",
        "[Network]\r\n"
        "UDPPort=5555\r\n"
        "\r\n"
        "[Position]\r\n"
        "InvertX=false\r\n"
        "\r\n"
        "[General]\r\n"
        "ConfigVersion=1");
}

// After the migration a user sets InvertX back to true on the CRLF file. The file
// is stamped, so that choice is kept and the file is not touched again.
void TestDeliberateInversionAfterMigrationOnCrlf() {
    TempIni ini("plugin_config_migration_deliberate_crlf.ini");
    ini.Write(
        "[Position]\r\n"
        "InvertX=true\r\n"
        "\r\n"
        "[General]\r\n"
        "AutoEnable=true\r\n");

    PluginConfig first;
    first.Load(ini.Path(), kRe8Schema);
    const std::string migrated =
        "[Position]\r\n"
        "InvertX=false\r\n"
        "\r\n"
        "[General]\r\n"
        "AutoEnable=true\r\n"
        "ConfigVersion=1\r\n";
    Check(ini.Read() == migrated, "the CRLF config is migrated once");

    const std::string reinstated =
        "[Position]\r\n"
        "InvertX=true\r\n"
        "\r\n"
        "[General]\r\n"
        "AutoEnable=true\r\n"
        "ConfigVersion=1\r\n";
    ini.Write(reinstated);

    PluginConfig second;
    second.Load(ini.Path(), kRe8Schema);
    Check(second.positionInvertX, "the true set back after the migration is in effect");
    Check(second.configVersion == kPluginConfigVersion, "and the config reads as stamped");
    Check(ini.Read() == reinstated, "and the file is left exactly as the user wrote it");
}

// A byte order mark in front of a comment hides nothing from GetPrivateProfileStringA.
// The mark is kept, the file is migrated once, and a true set back afterwards stays.
void TestBomFileKeepsADeliberateInversion() {
    TempIni ini("plugin_config_migration_bom.ini");
    ini.Write(
        "\xEF\xBB\xBF; RE8 Head Tracking Configuration\r\n"
        "\r\n"
        "[Position]\r\n"
        "InvertX=true\r\n"
        "\r\n"
        "[General]\r\n"
        "AutoEnable=true\r\n");

    PluginConfig first;
    first.Load(ini.Path(), kRe8Schema);
    Check(!first.positionInvertX, "the BOM config is corrected in memory");
    Check(first.configVersion == kPluginConfigVersion, "and reports the stamp");
    Check(ini.Read() ==
              "\xEF\xBB\xBF; RE8 Head Tracking Configuration\r\n"
              "\r\n"
              "[Position]\r\n"
              "InvertX=false\r\n"
              "\r\n"
              "[General]\r\n"
              "AutoEnable=true\r\n"
              "ConfigVersion=1\r\n",
          "the BOM config is migrated once, keeping the mark");

    const std::string reinstated =
        "\xEF\xBB\xBF; RE8 Head Tracking Configuration\r\n"
        "\r\n"
        "[Position]\r\n"
        "InvertX=true\r\n"
        "\r\n"
        "[General]\r\n"
        "AutoEnable=true\r\n"
        "ConfigVersion=1\r\n";
    ini.Write(reinstated);

    PluginConfig second;
    second.Load(ini.Path(), kRe8Schema);
    Check(second.configVersion == kPluginConfigVersion, "the stamp behind the mark reads back");
    Check(second.positionInvertX, "the true set back after the migration is in effect");
    Check(ini.Read() == reinstated, "and the file is left exactly as the user wrote it");
}

// The mark hides the first header from GetPrivateProfileStringA, but the migration
// edits neither that section nor anything under it.
void TestBomBeforeAnUneditedSection() {
    CheckRe8Migration(
        "plugin_config_migration_bom_network.ini",
        "\xEF\xBB\xBF[Network]\n"
        "UDPPort=5555\n"
        "\n"
        "[Position]\n"
        "InvertX=true\n"
        "\n"
        "[General]\n"
        "AutoEnable=true\n",
        "\xEF\xBB\xBF[Network]\n"
        "UDPPort=5555\n"
        "\n"
        "[Position]\n"
        "InvertX=false\n"
        "\n"
        "[General]\n"
        "AutoEnable=true\n"
        "ConfigVersion=1\n");
}

// GetPrivateProfileStringA reads the file as ANSI text, so a byte that is not UTF-8 is
// just a character to it, and the migration keeps it as it is.
void TestAnsiBytesAreKept() {
    CheckRe8Migration(
        "plugin_config_migration_ansi.ini",
        "; caf\xE9 settings\n"
        "[Network]\n"
        "UDPPort=5555\n"
        "[Position]\n"
        "InvertX=true\n"
        "[General]\n"
        "Name=Jos\xE9\n",
        "; caf\xE9 settings\n"
        "[Network]\n"
        "UDPPort=5555\n"
        "[Position]\n"
        "InvertX=false\n"
        "[General]\n"
        "Name=Jos\xE9\n"
        "ConfigVersion=1\n");
}

// GetPrivateProfileStringA reads the first of a repeated key, so that is the one the
// migration corrects and stamps. The later copies stay as they are.
void TestRepeatedKeysEditTheFirst() {
    CheckRe8Migration(
        "plugin_config_migration_repeated.ini",
        "[Position]\n"
        "InvertX=true\n"
        "InvertX=true\n"
        "\n"
        "[General]\n"
        "ConfigVersion=0\n"
        "ConfigVersion=0\n",
        "[Position]\n"
        "InvertX=false\n"
        "InvertX=true\n"
        "\n"
        "[General]\n"
        "ConfigVersion=1\n"
        "ConfigVersion=0\n");
}

// GetPrivateProfileStringA reads only the first of a repeated section header, so the
// stamp goes under that one.
void TestRepeatedSectionStampsTheFirst() {
    CheckRe8Migration(
        "plugin_config_migration_repeated_section.ini",
        "[General]\n"
        "AutoEnable=true\n"
        "[Position]\n"
        "InvertX=true\n"
        "[General]\n"
        "WorldSpaceYaw=true\n",
        "[General]\n"
        "AutoEnable=true\n"
        "ConfigVersion=1\n"
        "[Position]\n"
        "InvertX=false\n"
        "[General]\n"
        "WorldSpaceYaw=true\n");
}

// GetPrivateProfileStringA reads a UTF-8 byte order mark as part of the first line, so
// a header right behind it is not a header and the keys under it belong to no section.
// The stamp goes in a [General] the reader can see, appended at the end.
void TestBomHidesTheFirstHeader() {
    CheckRe8Migration(
        "plugin_config_migration_bom_header.ini",
        "\xEF\xBB\xBF[General]\r\n"
        "AutoEnable=true\r\n"
        "[Position]\r\n"
        "InvertX=true\r\n",
        "\xEF\xBB\xBF[General]\r\n"
        "AutoEnable=true\r\n"
        "[Position]\r\n"
        "InvertX=false\r\n"
        "\r\n"
        "[General]\r\n"
        "ConfigVersion=1\r\n");
    CheckRe8Migration(
        "plugin_config_migration_bom_indented_header.ini",
        "\xEF\xBB\xBF  [General]\n"
        "AutoEnable=true\n"
        "[Position]\n"
        "InvertX=true\n",
        "\xEF\xBB\xBF  [General]\n"
        "AutoEnable=true\n"
        "[Position]\n"
        "InvertX=false\n"
        "\n"
        "[General]\n"
        "ConfigVersion=1\n");
}

// A file holding nothing but the mark. The new [General] must not land right behind it.
void TestBomOnlyFile() {
    CheckRe8Migration(
        "plugin_config_migration_bom_only.ini",
        "\xEF\xBB\xBF",
        "\xEF\xBB\xBF\r\n"
        "\r\n"
        "[General]\r\n"
        "ConfigVersion=1");
}

// Load returns false on a missing file and the caller writes the defaults; the
// migration never creates a file of its own.
void TestMissingFileIsNotCreated() {
    TempIni ini("plugin_config_migration_missing.ini");
    PluginConfig config;
    Check(!config.Load(ini.Path(), kRe8Schema), "a missing config does not load");
    Check(config.configVersion == 0, "and reports no stamp");
    Check(!ini.Exists(), "and Load does not create one");
}

// A file the editor refuses is left byte for byte. The correction still applies
// for the session, the stamp is not claimed, and the next launch tries again.
void CheckRefusedFileIsLeftAlone(const char* name, const std::string& input, const char* refusal) {
    std::cout << "  -- " << name << "\n";
    TempIni ini(name);
    ini.Write(input);

    ErrorLog log;
    PluginConfig config;
    Check(config.Load(ini.Path(), kRe8Schema), "the config still loads");
    Check(!config.positionInvertX, "InvertX is false in memory");
    Check(config.configVersion == 0, "the stamp is not claimed");
    Check(ini.Read() == input, "the file is untouched");
    Check(ini.TemporariesBeside() == 0, "no temporary is left beside it");
    Check(log.Has(refusal) && log.Has("the file is unchanged"),
          "the error log names the refusal and says the file is unchanged");
}

void TestRefusedFilesAreLeftAlone() {
    // To GetPrivateProfileStringA this is the [Position] header and InvertX=true under it.
    CheckRefusedFileIsLeftAlone(
        "plugin_config_migration_lone_cr.ini",
        "[Position]\r"
        "InvertX=true\n"
        "[General]\n"
        "AutoEnable=true\n",
        "line 1 holds a CR with no LF after it");
    const char utf16[] =
        "\xFF\xFE[\0G\0e\0n\0e\0r\0a\0l\0]\0\r\0\n\0A\0u\0t\0o\0E\0n\0a\0b\0l\0e\0=\0t\0r\0u\0e\0\r\0\n\0";
    CheckRefusedFileIsLeftAlone("plugin_config_migration_utf16.ini",
                                std::string(utf16, sizeof(utf16) - 1), "(Utf16)");
}

// GetPrivateProfileStringA skips every control byte but tab, LF and CR before or after a
// key, before a header, just inside its brackets and at the start of a value, where the
// canonical grammar keeps it as part of the line. So any of them anywhere refuses the file.
void TestSkippedControlBytesAreRefused() {
    // To GetPrivateProfileStringA this is InvertX=true, so inserting InvertX=false after
    // it would stamp the file and leave the lean mirrored.
    CheckRefusedFileIsLeftAlone(
        "plugin_config_migration_soh_key.ini",
        "[Position]\n"
        "\x01InvertX=true\n"
        "[General]\n"
        "AutoEnable=true\n",
        "line 2 holds the control byte 0x01");
    // To GetPrivateProfileStringA this is [General], so a stamp under an appended
    // [General] would never be read.
    CheckRefusedFileIsLeftAlone(
        "plugin_config_migration_vertical_tab_header.ini",
        "[Position]\n"
        "InvertX=true\n"
        "[General\x0B]\n"
        "ConfigVersion=0\n",
        "line 3 holds the control byte 0x0B");
    CheckRefusedFileIsLeftAlone(
        "plugin_config_migration_sub.ini",
        "[Position]\n"
        "InvertX=true\n"
        "[General]\n"
        "AutoEnable=true\n"
        "; saved by an old editor\x1A\n",
        "line 5 holds the control byte 0x1A");
    CheckRefusedFileIsLeftAlone(
        "plugin_config_migration_vertical_tab.ini",
        "[Position]\n"
        "InvertX=true\n"
        "[General]\n"
        "\x0B"
        "AutoEnable=true\n",
        "line 4 holds the control byte 0x0B");
    CheckRefusedFileIsLeftAlone(
        "plugin_config_migration_form_feed_header.ini",
        "[Position]\n"
        "InvertX=true\n"
        "  \x0C[General]\n"
        "AutoEnable=true\n",
        "line 3 holds the control byte 0x0C");
    CheckRefusedFileIsLeftAlone(
        "plugin_config_migration_form_feed_key.ini",
        "[Position]\n"
        "InvertX=true\n"
        "[General]\n"
        "AutoEnable\x0C=true\n",
        "line 4 holds the control byte 0x0C");
    CheckRefusedFileIsLeftAlone(
        "plugin_config_migration_unit_separator_value.ini",
        "[Position]\n"
        "InvertX=true\n"
        "[General]\n"
        "ConfigVersion=\x1F" "0\n",
        "line 4 holds the control byte 0x1F");
}

// GetPrivateProfileStringA opens a section at a '[' line with no ']'; the canonical
// grammar opens none there.
void TestUnclosedHeaderIsRefused() {
    CheckRefusedFileIsLeftAlone(
        "plugin_config_migration_unclosed_header.ini",
        "[Position]\n"
        "InvertX=true\n"
        "[General\n"
        "AutoEnable=true\n",
        "the section header on line 3 has no ']'");
}

// GetPrivateProfileStringA reads only the first block of a repeated section, so a stamp
// set under the second one reads as absent. Editing it there would change nothing the
// reader sees.
void TestKeyUnderALaterRepeatedHeaderIsRefused() {
    CheckRefusedFileIsLeftAlone(
        "plugin_config_migration_stamp_in_second_block.ini",
        "[Position]\n"
        "InvertX=true\n"
        "[General]\n"
        "AutoEnable=true\n"
        "[Network]\n"
        "UDPPort=5555\n"
        "[General]\n"
        "ConfigVersion=0\n",
        "[General] ConfigVersion is set only on line 8, under a repeated [General] header "
        "that GetPrivateProfileStringA does not read");
}

// The comment after a replaced value is written back with it, and the editor writes
// only printable ASCII with no space at the end. GetPrivateProfileIntA reads each of
// these stamps as 0.
void TestCommentTheEditorCannotWriteIsRefused() {
    CheckRefusedFileIsLeftAlone(
        "plugin_config_migration_comment_ansi.ini",
        "[Position]\n"
        "InvertX=true\n"
        "[General]\n"
        "ConfigVersion=0 ; caf\xE9\n",
        "the comment after [General] ConfigVersion on line 4 holds the byte 0xE9, which the "
        "editor cannot write back");
    CheckRefusedFileIsLeftAlone(
        "plugin_config_migration_comment_tab.ini",
        "[Position]\n"
        "InvertX=true\n"
        "[General]\n"
        "ConfigVersion=0\t; lowered\n",
        "the comment after [General] ConfigVersion on line 4 holds the byte 0x09, which the "
        "editor cannot write back");
    CheckRefusedFileIsLeftAlone(
        "plugin_config_migration_comment_trailing_space.ini",
        "[Position]\n"
        "InvertX=true\n"
        "[General]\n"
        "ConfigVersion=0 ; lowered \n",
        "the comment after [General] ConfigVersion on line 4 ends in a space, which the editor "
        "cannot write back");
}

// The checked writer replaces the file rather than opening it for writing, so a
// read-only config fails the migration without being truncated or losing the
// attribute.
void TestReadOnlyFileIsLeftAlone() {
    TempIni ini("plugin_config_migration_readonly.ini");
    const std::string input =
        "[Position]\n"
        "InvertX=true\n"
        "\n"
        "[General]\n"
        "AutoEnable=true\n";
    ini.Write(input);
    // MSVC's std::filesystem sets FILE_ATTRIBUTE_READONLY only once every write bit is gone.
    std::filesystem::permissions(ini.Path(),
                                 std::filesystem::perms::owner_write | std::filesystem::perms::group_write |
                                     std::filesystem::perms::others_write,
                                 std::filesystem::perm_options::remove);

    ErrorLog log;
    PluginConfig config;
    Check(config.Load(ini.Path(), kRe8Schema), "a read-only config loads");
    Check(!config.positionInvertX, "InvertX is corrected in memory");
    Check(config.configVersion == 0, "the stamp is not claimed");
    Check(ini.Read() == input, "the read-only file is untouched");
    Check((std::filesystem::status(ini.Path()).permissions() & std::filesystem::perms::owner_write) ==
              std::filesystem::perms::none,
          "and still read-only");
    Check(log.Has("the Commit step failed with Windows error 5; the file is unchanged"),
          "the error log names the failed step and says the file is unchanged");
    Check(ini.TemporariesBeside() == 0, "no temporary is left beside it");
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
    TestCrlfFileWithComments();
    TestInlineCommentOnTheStampSurvives();
    TestNoFinalNewlineInsertsOnItsOwnLine();
    TestNoFinalNewlineCrlfWithoutGeneral();
    TestDeliberateInversionAfterMigrationOnCrlf();
    TestBomFileKeepsADeliberateInversion();
    TestBomBeforeAnUneditedSection();
    TestAnsiBytesAreKept();
    TestRepeatedKeysEditTheFirst();
    TestRepeatedSectionStampsTheFirst();
    TestBomHidesTheFirstHeader();
    TestBomOnlyFile();
    TestMissingFileIsNotCreated();
    TestRefusedFilesAreLeftAlone();
    TestSkippedControlBytesAreRefused();
    TestUnclosedHeaderIsRefused();
    TestKeyUnderALaterRepeatedHeaderIsRefused();
    TestCommentTheEditorCannotWriteIsRefused();
    TestReadOnlyFileIsLeftAlone();
    return g_failures;
}
