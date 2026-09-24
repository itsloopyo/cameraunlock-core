// ConfigOwner against real files and real Windows handles, scenario for scenario with the C#
// ConfigOwnerScenarios, plus what only the C++ owner does: the held-handle test with the Win32
// and CRT readers imports call, a path the ANSI code page cannot hold, and killed-child runs
// through `--config-owner-interrupt <step>`.

#include <cameraunlock/config/config_owner.h>
#include <cameraunlock/config/head_tracking_config_table.h>

#include <iostream>
#include <string>

#ifdef _WIN32

#include <windows.h>

#include <aclapi.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace std::string_literals;
using cameraunlock::CheckedWriteStep;
using cameraunlock::HeadTrackingConfig;
using namespace cameraunlock::config;
using schema::Concept;

static_assert(static_cast<int>(ConfigLoadStatus::Canonical) == 0, "Canonical");
static_assert(static_cast<int>(ConfigLoadStatus::Migrated) == 1, "Migrated");
static_assert(static_cast<int>(ConfigLoadStatus::Created) == 2, "Created");
static_assert(static_cast<int>(ConfigLoadStatus::Deferred) == 3, "Deferred");
static_assert(static_cast<int>(ConfigLoadStatus::LegacyRefused) == 4, "LegacyRefused");
static_assert(static_cast<int>(ConfigLoadStatus::Unreadable) == 5, "Unreadable");
static_assert(static_cast<int>(ConfigSaveStatus::Saved) == 0, "Saved");
static_assert(static_cast<int>(ConfigSaveStatus::NotSaved) == 1, "NotSaved");
static_assert(static_cast<int>(ConfigSaveStatus::Uncertain) == 2, "Uncertain");
static_assert(static_cast<int>(ConfigReloadStatus::Unchanged) == 0, "Unchanged");
static_assert(static_cast<int>(ConfigReloadStatus::Applied) == 1, "Applied");
static_assert(static_cast<int>(ConfigReloadStatus::LegacyReadOnly) == 2, "LegacyReadOnly");
static_assert(static_cast<int>(ConfigReloadStatus::Unreadable) == 3, "Unreadable");

using Owner = ConfigOwner<HeadTrackingConfig>;
using Load = ConfigLoadResult<HeadTrackingConfig>;
using Reload = ConfigReloadResult<HeadTrackingConfig>;

constexpr wchar_t kFileName[] = L"HeadTracking.ini";
constexpr char kFileNameText[] = "HeadTracking.ini";
constexpr char kDisplay[] = "Test Game";
constexpr int kKilledExitCode = 3;

// Line 5 holds a key the legacy reader does not read.
const std::string kLegacyText =
    "; tuned by hand\r\n[General]\r\nPort = 5555\r\nYawWorld = false\r\nSmoothng = 0.3\r\n[Position]\r\nPosition = false\r\n";

int g_failures = 0;
std::string g_scenario;

void Check(bool cond, const std::string& name) {
    if (cond) {
        std::cout << "  [PASS] " << g_scenario << ": " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << g_scenario << ": " << name << "\n";
        ++g_failures;
    }
}

bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

std::string Utf8(const std::wstring& text) { return detail::OwnerUtf8(text); }

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) throw std::runtime_error("cannot open " + Utf8(path.wstring()));
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) throw std::runtime_error("cannot create " + Utf8(path.wstring()));
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) throw std::runtime_error("cannot write " + Utf8(path.wstring()));
}

bool HoldsBytes(const fs::path& path, const std::string& bytes) { return fs::exists(path) && ReadBytes(path) == bytes; }

bool ListingIs(const fs::path& dir, std::vector<std::wstring> names) {
    std::vector<std::wstring> actual;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()) actual.push_back(entry.path().filename().wstring());
    }
    std::sort(actual.begin(), actual.end());
    std::sort(names.begin(), names.end());
    return actual == names;
}

std::string Utf16(const std::string& ascii) {
    std::string bytes = "\xFF\xFE";
    for (char c : ascii) {
        bytes.push_back(c);
        bytes.push_back('\0');
    }
    return bytes;
}

std::string Replace(std::string text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    if (at == std::string::npos) throw std::runtime_error("'" + from + "' is not in the text");
    return text.replace(at, from.size(), to);
}

ConfigTable<HeadTrackingConfig> Table() {
    ConfigTable<HeadTrackingConfig> table = HeadTrackingConfigTable<HeadTrackingConfig>(
        {Concept::UdpPort, Concept::EnableOnStartup, Concept::WorldSpaceYaw, Concept::RotationEnabled,
         Concept::PositionEnabled, Concept::ToggleKey, Concept::LightMultiplier});
    table.Select(Concept::WorldSpaceYaw).Writable().Select(Concept::RotationEnabled).Writable()
        .Select(Concept::PositionEnabled).Writable();
    return table;
}

HeadTrackingConfig Defaults() { return Table().defaults(); }

std::string Render(const HeadTrackingConfig& config) { return RenderCanonical(Table(), config, RenderHeader{kDisplay}); }

HeadTrackingConfig MigratedConfig() {
    HeadTrackingConfig config = Defaults();
    config.udp_port = 5555;
    config.world_space_yaw = false;
    config.rotation_enabled = true;
    config.position_enabled = false;
    return config;
}

std::string MigratedBytes() { return Render(MigratedConfig()); }

// The rows' values: rendering reads every row with its codec, floats included bit for bit.
bool Same(const HeadTrackingConfig& a, const HeadTrackingConfig& b) { return Render(a) == Render(b); }

std::string WithoutStamp(const std::string& canonical) {
    return Replace(canonical, "[CameraUnlock]\r\n; Written by the mod. Leave this section in place.\r\nConfigFormat=1\r\n\r\n",
                   "");
}

std::string Trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' || text[begin] == '\n')) ++begin;
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' || text[end - 1] == '\n')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

// A flat legacy reader of the kind the fleet's native imports freeze: the CRT in text mode on
// the ANSI path, one section header per line, the published build's defaults, and N2 on the
// light multiplier. It reports Absent when the file cannot be opened.
struct Legacy {
    std::vector<LegacyInput> inputs;
    std::function<ImportResult(HeadTrackingConfig&)> result;
    std::function<void(const LegacyInput&)> during;

    LegacyImport<HeadTrackingConfig> Import() {
        LegacyImport<HeadTrackingConfig> import;
        import.run = [this](const LegacyInput& input, HeadTrackingConfig& config) { return Run(input, config); };
        import.keys = {{"General", "Port"}, {"General", "YawWorld"}, {"Position", "Position"}, {"", "Light"}};
        return import;
    }

    int Runs() const { return static_cast<int>(inputs.size()); }

    ImportResult Run(const LegacyInput& input, HeadTrackingConfig& config) {
        inputs.push_back(input);
        if (during) during(input);
        if (result) return result(config);

        int port = 4242;
        bool yaw_world = true;
        bool position = true;
        float light = cameraunlock::effects::kDefaultLightMultiplier;
        FILE* file = std::fopen(input.ansi_path.c_str(), "r");
        if (file != nullptr) {
            std::string section;
            char buffer[512];
            while (std::fgets(buffer, sizeof(buffer), file) != nullptr) {
                const std::string line = Trim(buffer);
                if (!line.empty() && line.front() == '[' && line.back() == ']') {
                    section = line.substr(1, line.size() - 2);
                    continue;
                }
                const std::size_t equals = line.find('=');
                if (equals == std::string::npos || line.front() == ';') continue;
                const std::string key = Trim(line.substr(0, equals));
                const std::string value = Trim(line.substr(equals + 1));
                if (section == "General" && key == "Port") port = std::stoi(value);
                if (section == "General" && key == "YawWorld") yaw_world = value == "true";
                if (section == "Position" && key == "Position") position = value == "true";
                if (key == "Light") light = std::strtof(value.c_str(), nullptr);
            }
            std::fclose(file);
        }

        std::vector<DroppedValue> dropped;
        config.udp_port = port;
        config.world_space_yaw = yaw_world;
        config.rotation_enabled = true;
        config.position_enabled = position;
        config.light.multiplier =
            LegacyFiniteOrDefault(light, cameraunlock::effects::kDefaultLightMultiplier, "Light", "LightMultiplier", dropped);
        return file != nullptr ? ImportResult::Imported(std::move(dropped)) : ImportResult::Absent(std::move(dropped));
    }
};

struct Rig {
    fs::path path;
    std::vector<std::string> sink;
    std::shared_ptr<Legacy> legacy = std::make_shared<Legacy>();
    std::function<std::uint32_t(const std::string&, const std::wstring&)> hook;
    bool with_import = true;

    explicit Rig(const fs::path& dir) : path(dir / kFileName) {}

    std::string Text() const { return Utf8(path.wstring()); }

    ConfigOwnerOptions<HeadTrackingConfig> Options() {
        ConfigOwnerOptions<HeadTrackingConfig> options;
        options.path = path.wstring();
        options.table = Table();
        options.header = RenderHeader{kDisplay};
        if (with_import) options.import = legacy->Import();
        options.status_sink = [this](const std::string& message) { sink.push_back(message); };
        return options;
    }

    std::unique_ptr<Owner> Make() {
        return std::make_unique<Owner>(Options(), [this](const std::string& label, const std::wstring& at) -> std::uint32_t {
            return hook ? hook(label, at) : 0;
        });
    }
};

bool HasLine(const std::vector<std::string>& log, const std::string& line) {
    return std::find(log.begin(), log.end(), line) != log.end();
}

int CountContaining(const std::vector<std::string>& log, const std::string& part) {
    return static_cast<int>(std::count_if(log.begin(), log.end(), [&](const std::string& l) { return Contains(l, part); }));
}

std::string Joined(const std::vector<std::string>& lines) {
    std::string text;
    for (const std::string& line : lines) text += "\n    " + line;
    return text;
}

void ExpectStatus(const Load& load, ConfigLoadStatus status) {
    Check(load.status == status, std::string("Load is ") + ConfigLoadStatusName(status) + ", got " +
                                     ConfigLoadStatusName(load.status) + " (" + load.reason + ")" + Joined(load.log));
}

void ExpectLogLine(const std::vector<std::string>& log, const std::string& line) {
    Check(HasLine(log, line), "the log holds \"" + line + "\"" + (HasLine(log, line) ? "" : Joined(log)));
}

void ExpectSaved(const ConfigSaveResult& save) {
    Check(save.status == ConfigSaveStatus::Saved && save.reason.empty(),
          std::string("Saved, got ") + ConfigSaveStatusName(save.status) + " (" + save.reason + ")");
}

void ExpectNotSaved(const ConfigSaveResult& save, const std::string& why) {
    Check(save.status == ConfigSaveStatus::NotSaved && Contains(save.reason, "Settings not saved: " + why),
          std::string("NotSaved with \"") + why + "\", got " + ConfigSaveStatusName(save.status) + " (" + save.reason + ")");
}

void ExpectSunkOnce(const Rig& rig, const std::string& message) {
    Check(rig.sink.size() == 1 && rig.sink[0] == message, "the sink got the message once: " + message);
}

// A deferred conversion leaves the legacy file as it was and writes no copy.
void ExpectUntouched(const Rig& rig) {
    Check(HoldsBytes(rig.path, kLegacyText), "the legacy file is unchanged");
    Check(ListingIs(rig.path.parent_path(), {kFileName}), "no copy is written");
}

template <class Exception, class F>
std::string Thrown(F&& f) {
    try {
        f();
    } catch (const Exception& e) {
        return e.what();
    }
    return "(nothing thrown)";
}

void SetReadOnly(const fs::path& path, bool read_only) {
    SetFileAttributesW(path.c_str(), read_only ? FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_NORMAL);
}

std::wstring WithSuffix(const fs::path& path, const wchar_t* suffix) { return path.wstring() + suffix; }

void AnAbsentFileIsCreated(const fs::path& dir) {
    Rig rig(dir);
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Created);
    Check(HoldsBytes(rig.path, Render(Defaults())), "the file holds the rendered defaults");
    Check(Same(load.config, Defaults()), "the session runs on the defaults");
    Check(rig.legacy->Runs() == 0, "no import runs for an absent file");
    Check(rig.sink.empty(), "nothing is reported");
    Check(ListingIs(dir, {kFileName}), "nothing else is written");
}

void AFileAppearingDuringCreationDefers(const fs::path& dir) {
    Rig rig(dir);
    rig.hook = [&](const std::string& step, const std::wstring&) -> std::uint32_t {
        if (step == "Create.RecheckTarget") WriteBytes(rig.path, "theirs");
        return 0;
    };
    auto owner = rig.Make();
    const Load load = owner->Load();
    ExpectStatus(load, ConfigLoadStatus::Deferred);
    Check(Contains(load.reason, "another program created the file at the same time"), "the reason says why");
    Check(Same(load.config, Defaults()), "the session runs on the defaults");
    Check(HoldsBytes(rig.path, "theirs"), "the other program's file is kept");
    ExpectSunkOnce(rig, load.reason);
    ExpectNotSaved(owner->Save([](HeadTrackingConfig& c) { c.world_space_yaw = false; }),
                   "the settings file could not be used this session");
    Check(HoldsBytes(rig.path, "theirs"), "the save wrote nothing");
    Check(ListingIs(dir, {kFileName}), "nothing else is written");
}

void AStampedFileIsCanonical(const fs::path& dir) {
    Rig rig(dir);
    HeadTrackingConfig chosen = Defaults();
    chosen.world_space_yaw = false;
    chosen.udp_port = 5000;
    const std::string canonical = Render(chosen);
    WriteBytes(rig.path, canonical);
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Canonical);
    Check(Same(load.config, chosen), "the file's values are read");
    Check(load.diagnostics.empty() && load.log.empty(), "a clean file draws nothing");
    Check(rig.legacy->Runs() == 0, "the import never runs on a stamped file");
    Check(HoldsBytes(rig.path, canonical), "the file is not written");
    Check(ListingIs(dir, {kFileName}), "nothing else is written");
}

void AStampedUtf16FileIsUnreadableAndNeverMigrated(const fs::path& dir) {
    Rig rig(dir);
    const std::string utf16 = Utf16(Render(Defaults()));
    WriteBytes(rig.path, utf16);
    auto owner = rig.Make();
    const Load load = owner->Load();
    ExpectStatus(load, ConfigLoadStatus::Unreadable);
    Check(Contains(load.reason, "it is saved as UTF-16; save it as ANSI or UTF-8"), "the reason says what to do");
    Check(Same(load.config, Defaults()), "the session runs on the defaults");
    Check(rig.legacy->Runs() == 0, "the import never runs on a stamped file");
    ExpectSunkOnce(rig, load.reason);
    ExpectNotSaved(owner->Save([](HeadTrackingConfig& c) { c.world_space_yaw = false; }),
                   "the settings file could not be used this session");
    Check(HoldsBytes(rig.path, utf16), "the file is not written");
    Check(ListingIs(dir, {kFileName}), "nothing else is written");
}

void AStampedFileHoldingANulIsUnreadable(const fs::path& dir) {
    Rig rig(dir);
    const std::string bytes = Render(Defaults()) + "\0\r\n"s;
    WriteBytes(rig.path, bytes);
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Unreadable);
    const auto line = std::count(bytes.begin(), bytes.end(), '\n');
    Check(Contains(load.reason, "line " + std::to_string(line) + " holds a NUL byte"), "the reason names the line");
    Check(rig.legacy->Runs() == 0, "the import never runs on a stamped file");
    Check(HoldsBytes(rig.path, bytes), "the file is not written");
    Check(ListingIs(dir, {kFileName}), "nothing else is written");
}

void AnUnstampedFileWithAnImportIsMigrated(const fs::path& dir) {
    Rig rig(dir);
    WriteBytes(rig.path, kLegacyText);
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Migrated);
    Check(Same(load.config, MigratedConfig()), "the session runs on the imported values");
    Check(HoldsBytes(rig.path, MigratedBytes()), "the file holds the rendered values");
    Check(HoldsBytes(WithSuffix(rig.path, L".pre-canonical"), kLegacyText), "the original is kept");
    Check(load.diagnostics.empty(), "the new file reads back clean");
    ExpectLogLine(load.log, rig.Text() + ": converted to the canonical format. The original is kept in " + rig.Text() +
                                ".pre-canonical.");
    ExpectLogLine(load.log, rig.Text() + ": not carried: [General] Smoothng=0.3 on line 5, this build does not read it");
    Check(CountContaining(load.log, "not carried") == 1, "only the unread key is listed");
    Check(rig.legacy->Runs() == 1 && rig.legacy->inputs[0].path == rig.path.wstring() &&
              detail::OwnerAnsiForLog(rig.legacy->inputs[0].ansi_path) == rig.Text() && !rig.legacy->inputs[0].ansi_lossy,
          "the import runs once, on the file itself, given its wide and ANSI paths");
    Check(rig.sink.empty(), "nothing is reported");
    Check(ListingIs(dir, {kFileName, L"HeadTracking.ini.pre-canonical"}), "only the copy is added");
}

void AnUnstampedUtf16FileWithAnImportIsMigrated(const fs::path& dir) {
    Rig rig(dir);
    const std::string utf16 = Utf16(kLegacyText);
    WriteBytes(rig.path, utf16);
    rig.legacy->result = [](HeadTrackingConfig& config) {
        config = MigratedConfig();
        return ImportResult::Imported({});
    };
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Migrated);
    Check(Same(load.config, MigratedConfig()), "the session runs on the imported values");
    Check(HoldsBytes(rig.path, MigratedBytes()), "the file holds the rendered values");
    Check(HoldsBytes(WithSuffix(rig.path, L".pre-canonical"), utf16), "the original is kept");
    ExpectLogLine(load.log, rig.Text() +
                                ": is saved as UTF-16, so its lines this build does not read are not listed; the original "
                                "keeps them.");
    Check(CountContaining(load.log, "not carried") == 0, "no line of a UTF-16 file is listed");
    Check(rig.legacy->Runs() == 1 && rig.sink.empty(), "one import and nothing reported");
    Check(ListingIs(dir, {kFileName, L"HeadTracking.ini.pre-canonical"}), "only the copy is added");
}

void AnUnstampedFileHoldingANulWithAnImportIsMigrated(const fs::path& dir) {
    Rig rig(dir);
    const std::string nul = kLegacyText + "Extra=1\0\r\n"s;
    WriteBytes(rig.path, nul);
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Migrated);
    Check(Same(load.config, MigratedConfig()), "the session runs on the imported values");
    Check(HoldsBytes(rig.path, MigratedBytes()), "the file holds the rendered values");
    Check(HoldsBytes(WithSuffix(rig.path, L".pre-canonical"), nul), "the original is kept");
    ExpectLogLine(load.log, rig.Text() + ": not carried: [General] Smoothng=0.3 on line 5, this build does not read it");
    ExpectLogLine(load.log, rig.Text() + ": not carried: [Position] Extra=1\0 on line 8, this build does not read it"s);
    Check(CountContaining(load.log, "not carried") == 2, "only the unread keys are listed");
    Check(rig.legacy->Runs() == 1 && rig.sink.empty(), "one import and nothing reported");
    Check(ListingIs(dir, {kFileName, L"HeadTracking.ini.pre-canonical"}), "only the copy is added");
}

void ASecondLoadRewritesNothing(const fs::path& dir) {
    Rig rig(dir);
    WriteBytes(rig.path, kLegacyText);
    ExpectStatus(rig.Make()->Load(), ConfigLoadStatus::Migrated);
    const auto written = fs::last_write_time(rig.path);
    const auto copied = fs::last_write_time(WithSuffix(rig.path, L".pre-canonical"));

    std::vector<std::string> steps;
    rig.hook = [&](const std::string& step, const std::wstring&) -> std::uint32_t {
        steps.push_back(step);
        return 0;
    };
    const Load again = rig.Make()->Load();
    ExpectStatus(again, ConfigLoadStatus::Canonical);
    Check(Same(again.config, MigratedConfig()), "the migrated values are read");
    Check(steps == std::vector<std::string>{"Open"}, "the second launch only opens the file");
    Check(rig.legacy->Runs() == 1, "the import does not run again");
    Check(HoldsBytes(rig.path, MigratedBytes()), "the file is unchanged");
    Check(fs::last_write_time(rig.path) == written &&
              fs::last_write_time(WithSuffix(rig.path, L".pre-canonical")) == copied,
          "neither file is rewritten");
    Check(ListingIs(dir, {kFileName, L"HeadTracking.ini.pre-canonical"}), "nothing else is written");
}

void AnUnstampedFileWithoutAnImportIsCanonicalAndStampedByASave(const fs::path& dir) {
    Rig rig(dir);
    rig.with_import = false;
    const std::string text = "; mine\r\n[General]\r\nWorldSpaceYaw=false\r\n";
    WriteBytes(rig.path, text);
    auto owner = rig.Make();
    const Load load = owner->Load();
    ExpectStatus(load, ConfigLoadStatus::Canonical);
    Check(!load.config.world_space_yaw, "the file's value is read");
    ExpectLogLine(load.log, rig.Text() +
                                ": has no [CameraUnlock] section. It is read as the canonical format, and the next save "
                                "adds the section.");
    Check(HoldsBytes(rig.path, text), "the load writes nothing");

    ExpectSaved(owner->Save([](HeadTrackingConfig& c) { c.world_space_yaw = true; }));
    Check(HoldsBytes(rig.path, "; mine\r\n[General]\r\nWorldSpaceYaw=true\r\n\r\n[CameraUnlock]\r\nConfigFormat=1\r\n"),
          "the save stamps the file in the same edit");
    Check(ListingIs(dir, {kFileName}), "nothing else is written");
}

void AnUnstampedUnreadableFileWithoutAnImportIsUnreadable(const fs::path& dir) {
    Rig rig(dir);
    rig.with_import = false;
    const std::string utf16 = Utf16("[General]\r\nWorldSpaceYaw=false\r\n");
    WriteBytes(rig.path, utf16);
    ExpectStatus(rig.Make()->Load(), ConfigLoadStatus::Unreadable);
    Check(HoldsBytes(rig.path, utf16), "the UTF-16 file is not written");

    const std::string nul = "[General]\r\nWorldSpaceYaw=false\0\r\n"s;
    WriteBytes(rig.path, nul);
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Unreadable);
    Check(Contains(load.reason, "line 2 holds a NUL byte"), "the reason names the line");
    Check(HoldsBytes(rig.path, nul), "the file is not written");
    Check(ListingIs(dir, {kFileName}), "nothing else is written");
}

void ADroppedValueIsLogged(const fs::path& dir) {
    Rig rig(dir);
    WriteBytes(rig.path, kLegacyText + "Light = NaN\r\n");
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Migrated);
    Check(load.config.light.multiplier == cameraunlock::effects::kDefaultLightMultiplier, "N2 gives the default");
    ExpectLogLine(load.log, rig.Text() +
                                ": not carried: [Light] LightMultiplier=nan, it is not a finite number, so the default is used");
    Check(CountContaining(load.log, "not carried") == 2, "the dropped value and the unread key");
}

void ADeletedStampMigratesAgainIntoPreCanonicalLast(const fs::path& dir) {
    Rig rig(dir);
    const std::wstring first = WithSuffix(rig.path, L".pre-canonical");
    const std::wstring last = WithSuffix(rig.path, L".pre-canonical.last");
    WriteBytes(rig.path, kLegacyText);
    ExpectStatus(rig.Make()->Load(), ConfigLoadStatus::Migrated);

    const std::string stampless = WithoutStamp(ReadBytes(rig.path));
    WriteBytes(rig.path, stampless);
    const Load again = rig.Make()->Load();
    ExpectStatus(again, ConfigLoadStatus::Migrated);
    ExpectLogLine(again.log, rig.Text() + ": converted to the canonical format. The original is kept in " + rig.Text() +
                                 ".pre-canonical.last.");
    Check(HoldsBytes(first, kLegacyText), ".pre-canonical keeps the first input");
    Check(HoldsBytes(last, stampless), ".pre-canonical.last holds the second");
    Check(HasCanonicalStamp(ReadBytes(rig.path)), "stamped again");

    const std::string later = WithoutStamp(ReadBytes(rig.path)) + "[Extra]\r\nNote=1\r\n";
    WriteBytes(rig.path, later);
    ExpectStatus(rig.Make()->Load(), ConfigLoadStatus::Migrated);
    Check(HoldsBytes(first, kLegacyText), ".pre-canonical is never replaced");
    Check(HoldsBytes(last, later), ".pre-canonical.last is replaced by a later input");

    WriteBytes(rig.path, kLegacyText);
    ExpectStatus(rig.Make()->Load(), ConfigLoadStatus::Migrated);
    Check(HoldsBytes(last, later), "an input equal to .pre-canonical leaves .pre-canonical.last");
    Check(ListingIs(dir, {kFileName, L"HeadTracking.ini.pre-canonical", L"HeadTracking.ini.pre-canonical.last"}),
          "nothing else is written");
}

void ARefusedImportIsLegacyRefused(const fs::path& dir) {
    Rig rig(dir);
    rig.legacy->result = [](HeadTrackingConfig&) { return ImportResult::Refused("Port=99999 is outside 1 to 65535"); };
    WriteBytes(rig.path, kLegacyText);
    auto owner = rig.Make();
    const Load load = owner->Load();
    ExpectStatus(load, ConfigLoadStatus::LegacyRefused);
    Check(Contains(load.reason, "Port=99999 is outside 1 to 65535"), "the reason is the import's");
    ExpectSunkOnce(rig, load.reason);
    ExpectNotSaved(owner->Save([](HeadTrackingConfig& c) { c.world_space_yaw = true; }),
                   "the settings file could not be used this session");
    ExpectUntouched(rig);
}

void AnUndecodableImportDefers(const fs::path& dir) {
    Rig rig(dir);
    rig.legacy->result = [](HeadTrackingConfig&) { return ImportResult::Undecodable("the file is not UTF-8"); };
    WriteBytes(rig.path, kLegacyText);
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Deferred);
    Check(Contains(load.reason, "the file is not UTF-8"), "the reason is the import's");
    ExpectSunkOnce(rig, load.reason);
    ExpectUntouched(rig);
}

void AnAbsentImportDefers(const fs::path& dir) {
    Rig rig(dir);
    rig.legacy->result = [](HeadTrackingConfig&) { return ImportResult::Absent({}); };
    WriteBytes(rig.path, kLegacyText);
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Deferred);
    Check(Contains(load.reason, "the old settings reader could not find the file"), "the reason says why");
    ExpectLogLine(load.log, rig.Text() + ": the old settings reader found no file, while the owner holds it open (" +
                                std::to_string(kLegacyText.size()) + " bytes); the ANSI path it was given is " +
                                rig.Text());
    ExpectSunkOnce(rig, load.reason);
    ExpectUntouched(rig);
}

void AReadOnlyFileDefers(const fs::path& dir) {
    Rig rig(dir);
    WriteBytes(rig.path, kLegacyText);
    SetReadOnly(rig.path, true);
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Deferred);
    Check(Contains(load.reason, "the file is read-only"), "the reason says why");
    Check(Same(load.config, MigratedConfig()), "the session runs on what the import gave");
    ExpectSunkOnce(rig, load.reason);
    Check(HoldsBytes(rig.path, kLegacyText), "the legacy file is unchanged");
    Check(HoldsBytes(WithSuffix(rig.path, L".pre-canonical"), kLegacyText), "the copy was written before the commit");
    Check(ListingIs(dir, {kFileName, L"HeadTracking.ini.pre-canonical"}), "nothing else is left");

    SetReadOnly(rig.path, false);
    ExpectStatus(rig.Make()->Load(), ConfigLoadStatus::Migrated);
    Check(HoldsBytes(rig.path, MigratedBytes()), "the next launch migrates");
    Check(ListingIs(dir, {kFileName, L"HeadTracking.ini.pre-canonical"}), "the copy is reused");
}

// Denies the current user FILE_ADD_FILE on the folder until destroyed.
class DenyCreateFiles {
public:
    explicit DenyCreateFiles(const fs::path& dir) : dir_(dir.wstring()) {
        PACL old = nullptr;
        if (GetNamedSecurityInfoW(dir_.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &old,
                                  nullptr, &descriptor_) != ERROR_SUCCESS) {
            throw std::runtime_error("GetNamedSecurityInfoW failed");
        }
        old_ = old;
        HANDLE token = nullptr;
        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        std::vector<BYTE> user(size);
        if (!GetTokenInformation(token, TokenUser, user.data(), size, &size)) throw std::runtime_error("GetTokenInformation");
        CloseHandle(token);
        EXPLICIT_ACCESS_W deny{};
        deny.grfAccessPermissions = FILE_ADD_FILE;
        deny.grfAccessMode = DENY_ACCESS;
        deny.grfInheritance = NO_INHERITANCE;
        deny.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        deny.Trustee.TrusteeType = TRUSTEE_IS_USER;
        deny.Trustee.ptstrName = static_cast<LPWSTR>(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid);
        PACL denied = nullptr;
        if (SetEntriesInAclW(1, &deny, old_, &denied) != ERROR_SUCCESS) throw std::runtime_error("SetEntriesInAclW");
        const DWORD set = SetNamedSecurityInfoW(&dir_[0], SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                                denied, nullptr);
        LocalFree(denied);
        if (set != ERROR_SUCCESS) throw std::runtime_error("SetNamedSecurityInfoW");
    }
    DenyCreateFiles(const DenyCreateFiles&) = delete;
    DenyCreateFiles& operator=(const DenyCreateFiles&) = delete;
    ~DenyCreateFiles() {
        SetNamedSecurityInfoW(&dir_[0], SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, old_, nullptr);
        LocalFree(descriptor_);
    }

private:
    std::wstring dir_;
    PSECURITY_DESCRIPTOR descriptor_ = nullptr;
    PACL old_ = nullptr;
};

void AFolderThatCannotBeWrittenDefers(const fs::path& dir) {
    Rig rig(dir);
    WriteBytes(rig.path, kLegacyText);
    std::optional<Load> load;
    {
        DenyCreateFiles deny(dir);
        load = rig.Make()->Load();
    }
    ExpectStatus(*load, ConfigLoadStatus::Deferred);
    Check(Contains(load->reason, "the folder cannot be written"), "the reason says why");
    ExpectSunkOnce(rig, load->reason);
    ExpectUntouched(rig);
}

void AFileHeldDenyingReadSharingDefers(const fs::path& dir) {
    Rig rig(dir);
    WriteBytes(rig.path, kLegacyText);
    const HANDLE exclusive = CreateFileW(rig.path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                         FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(exclusive != INVALID_HANDLE_VALUE, "another program holds the file with no sharing");
    const Load load = rig.Make()->Load();
    CloseHandle(exclusive);
    ExpectStatus(load, ConfigLoadStatus::Deferred);
    Check(Contains(load.reason, "the file is in use by another program"), "the reason says why");
    Check(Same(load.config, Defaults()), "a file that cannot be opened cannot be classified, so the defaults");
    Check(rig.legacy->Runs() == 0, "the import does not run");
    ExpectSunkOnce(rig, load.reason);
    ExpectUntouched(rig);
    ExpectStatus(rig.Make()->Load(), ConfigLoadStatus::Migrated);
}

void AnImportThatWritesTheFileDefers(const fs::path& dir) {
    Rig rig(dir);
    WriteBytes(rig.path, kLegacyText);
    rig.legacy->during = [&](const LegacyInput& input) { WriteBytes(input.path, kLegacyText + "Light = 2.0\r\n"); };
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Deferred);
    Check(Contains(load.reason, "the file was changed by another program while it was read"), "the reason says why");
    ExpectSunkOnce(rig, load.reason);
    Check(HoldsBytes(rig.path, kLegacyText + "Light = 2.0\r\n"), "the other program's write is kept");
    Check(ListingIs(dir, {kFileName}), "no copy is written");
}

void AFailedCopyDefers(const fs::path& dir) {
    Rig rig(dir);
    WriteBytes(rig.path, kLegacyText);
    rig.hook = [](const std::string& step, const std::wstring&) -> std::uint32_t {
        return step == "Copy.WriteTemporary" ? ERROR_GEN_FAILURE : 0;
    };
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Deferred);
    Check(Contains(load.reason, "it could not be written (Windows error 31"), "the reason carries the error");
    Check(CountContaining(load.log, "Writing " + rig.Text() + ".pre-canonical failed at WriteTemporary") == 1,
          "the log names the copy and the step");
    ExpectSunkOnce(rig, load.reason);
    ExpectUntouched(rig);
}

void ACopyThatDoesNotReadBackDefers(const fs::path& dir) {
    Rig rig(dir);
    WriteBytes(rig.path, kLegacyText);
    rig.hook = [](const std::string& step, const std::wstring& path) -> std::uint32_t {
        if (step == "ReadBack") WriteBytes(path, "damaged");
        return 0;
    };
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Deferred);
    Check(Contains(load.reason, "the copy of the original file could not be written"), "the reason says why");
    ExpectLogLine(load.log, rig.Text() + ".pre-canonical: does not hold the bytes just written to it");
    ExpectSunkOnce(rig, load.reason);
    Check(HoldsBytes(rig.path, kLegacyText), "the legacy file is unchanged");
}

void AVerifyMismatchDefers(const fs::path& dir) {
    Rig rig(dir);
    rig.legacy->result = [](HeadTrackingConfig& config) {
        config.rotation_enabled = false;
        config.position_enabled = false;
        return ImportResult::Imported({});
    };
    WriteBytes(rig.path, kLegacyText);
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Deferred);
    Check(Contains(load.reason, "[General] RotationEnabled=false cannot be converted"), "the reason names the row");
    ExpectLogLine(load.log, rig.Text() + ": [General] RotationEnabled reads back from the new format as true, not false");
    Check(!load.config.rotation_enabled && !load.config.position_enabled, "the session runs on what the import gave");
    ExpectUntouched(rig);
}

void AValueNoCodecWritesDefers(const fs::path& dir) {
    Rig rig(dir);
    WriteBytes(rig.path, kLegacyText + "Light = 7.5\r\n");
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Deferred);
    Check(Contains(load.reason, "[Light] LightMultiplier=7.5 cannot be converted"), "the reason names the row and value");
    Check(load.config.light.multiplier == 7.5f, "the session runs on what the import gave");
    Check(HoldsBytes(rig.path, kLegacyText + "Light = 7.5\r\n"), "the legacy file is unchanged");
    Check(ListingIs(dir, {kFileName}), "no copy is written");
}

void AFileChangedBeforeTheCommitDefers(const fs::path& dir) {
    Rig rig(dir);
    WriteBytes(rig.path, kLegacyText);
    rig.hook = [&](const std::string& step, const std::wstring&) -> std::uint32_t {
        if (step == "Commit.RecheckTarget") WriteBytes(rig.path, "edited");
        return 0;
    };
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Deferred);
    Check(Contains(load.reason, "the file was changed by another program at the same time"), "the reason says why");
    Check(HoldsBytes(rig.path, "edited"), "the other program's write is kept");
    Check(HoldsBytes(WithSuffix(rig.path, L".pre-canonical"), kLegacyText), "the copy holds the input");
    Check(ListingIs(dir, {kFileName, L"HeadTracking.ini.pre-canonical"}), "nothing else is left");
}

// Design 4.5 step 1 (R3-2): while the owner holds the legacy file, the readers imports use read
// it, and nothing can newly open it denying read sharing, rename it or delete it.
void TheHeldFileReadsAndRefusesExclusiveOpens(const fs::path& dir) {
    Rig rig(dir);
    WriteBytes(rig.path, kLegacyText);
    std::string profile;
    std::string crt;
    std::string wide_crt;
    std::string stream;
    DWORD exclusive_error = 0;
    DWORD delete_error = 0;
    DWORD move_error = 0;
    rig.legacy->during = [&](const LegacyInput& input) {
        char value[64] = {};
        GetPrivateProfileStringA("General", "Port", "", value, sizeof(value), input.ansi_path.c_str());
        profile = value;
        if (FILE* file = std::fopen(input.ansi_path.c_str(), "rb")) {
            char buffer[512] = {};
            crt.assign(buffer, std::fread(buffer, 1, sizeof(buffer), file));
            std::fclose(file);
        }
        if (FILE* file = _wfopen(input.path.c_str(), L"rb")) {
            char buffer[512] = {};
            wide_crt.assign(buffer, std::fread(buffer, 1, sizeof(buffer), file));
            std::fclose(file);
        }
        {
            std::ifstream in(fs::path(input.path), std::ios::binary);
            stream.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }
        const HANDLE denying = CreateFileW(input.path.c_str(), GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (denying == INVALID_HANDLE_VALUE) {
            exclusive_error = GetLastError();
        } else {
            CloseHandle(denying);
        }
        if (!DeleteFileW(input.path.c_str())) delete_error = GetLastError();
        if (!MoveFileW(input.path.c_str(), (input.path + L".moved").c_str())) move_error = GetLastError();
    };
    ExpectStatus(rig.Make()->Load(), ConfigLoadStatus::Migrated);
    Check(profile == "5555", "GetPrivateProfileStringA reads the held file");
    Check(crt == kLegacyText, "fopen reads the held file");
    Check(wide_crt == kLegacyText, "_wfopen reads the held file");
    Check(stream == kLegacyText, "std::ifstream reads the held file");
    Check(exclusive_error == ERROR_SHARING_VIOLATION, "an open denying read sharing fails with a sharing violation");
    Check(delete_error == ERROR_SHARING_VIOLATION && move_error == ERROR_SHARING_VIOLATION,
          "a delete and a rename fail with a sharing violation");
    Check(ListingIs(dir, {kFileName, L"HeadTracking.ini.pre-canonical"}), "the file is still there, converted");
}

// Design 4.7, last case: a folder the ANSI code page cannot name. The published build was handed
// the ANSI path, found nothing and ran on its defaults, so those are what the conversion writes.
void APathOutsideTheAnsiCodePage(const fs::path& dir) {
    const wchar_t omega = 0x03A9;
    const fs::path folder = dir / std::wstring(1, omega);
    fs::create_directories(folder);
    Rig rig(folder);
    WriteBytes(rig.path, kLegacyText);
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Migrated);
    Check(rig.legacy->Runs() == 1 && rig.legacy->inputs[0].ansi_lossy, "the import is told the ANSI path is lossy");
    const std::string ansi = detail::OwnerAnsiForLog(rig.legacy->inputs[0].ansi_path);
    Check(Same(load.config, Defaults()), "the session runs on the defaults the published build ran on");
    Check(HoldsBytes(rig.path, Render(Defaults())), "the defaults are written");
    Check(HoldsBytes(WithSuffix(rig.path, L".pre-canonical"), kLegacyText), "the content is kept in the copy");
    ExpectLogLine(load.log, rig.Text() + ": has a character the ANSI code page cannot hold, so a reader given its ANSI path, " +
                                ansi + ", finds no file there.");
    ExpectLogLine(load.log, rig.Text() +
                                ": the old settings reader found no file, as the old build found none there and ran on its "
                                "defaults. Those defaults are written, and the file's content is kept in the copy.");
    Check(rig.sink.empty(), "nothing is reported");

    const Load next = rig.Make()->Load();
    ExpectStatus(next, ConfigLoadStatus::Canonical);
    Check(rig.legacy->Runs() == 1, "the next launch reads the file by its wide path, with no import");

    // A per-key import has no Absent to report: it reads nothing and gives its defaults.
    WriteBytes(rig.path, kLegacyText);
    fs::remove(WithSuffix(rig.path, L".pre-canonical"));
    Rig per_key(folder);
    per_key.legacy->result = [](HeadTrackingConfig& config) {
        config.udp_port = 4242;
        return ImportResult::Imported({});
    };
    per_key.legacy->during = [&](const LegacyInput& input) {
        char value[64] = {};
        GetPrivateProfileStringA("General", "Port", "none", value, sizeof(value), input.ansi_path.c_str());
        Check(std::string(value) == "none", "GetPrivateProfileStringA finds nothing at the ANSI path");
    };
    const Load per_key_load = per_key.Make()->Load();
    ExpectStatus(per_key_load, ConfigLoadStatus::Migrated);
    Check(HoldsBytes(per_key.path, Render(Defaults())), "a per-key import's defaults are written");
    Check(HoldsBytes(WithSuffix(per_key.path, L".pre-canonical"), kLegacyText), "the content is kept in the copy");
    Check(CountContaining(per_key_load.log, "has a character the ANSI code page cannot hold") == 1, "the log names the case");
}

void ANewerConfigFormatRefusesSaves(const fs::path& dir) {
    Rig rig(dir);
    const std::string newer = Replace(Render(Defaults()), "ConfigFormat=1", "ConfigFormat=2");
    WriteBytes(rig.path, newer);
    auto owner = rig.Make();
    const Load load = owner->Load();
    ExpectStatus(load, ConfigLoadStatus::Canonical);
    Check(std::any_of(load.diagnostics.begin(), load.diagnostics.end(),
                      [](const CanonicalDiagnostic& d) { return d.kind == CanonicalDiagnosticKind::ConfigFormatNewer; }),
          "the reader warns");
    Check(rig.legacy->Runs() == 0, "a stamped file is never imported");
    ExpectNotSaved(owner->Save([](HeadTrackingConfig& c) { c.world_space_yaw = false; }),
                   std::string(kFileNameText) + " was written by a newer version of the mod");
    ExpectSunkOnce(rig, "Settings not saved: " + std::string(kFileNameText) + " was written by a newer version of the mod.");
    Check(HoldsBytes(rig.path, newer), "the file is not written");
}

void ASaveWritesAMissingOrUnreadableConfigFormat(const fs::path& dir) {
    const std::string canonical = Render(Defaults());
    const std::string missing = Replace(canonical, "ConfigFormat=1\r\n", "");
    const std::string yaw_off = Replace(canonical, "WorldSpaceYaw=true", "WorldSpaceYaw=false");
    struct Case {
        std::string file;
        CanonicalDiagnosticKind kind;
        std::string saved;
    };
    const Case cases[] = {
        {missing, CanonicalDiagnosticKind::ConfigFormatMissing,
         Replace(Replace(yaw_off, "ConfigFormat=1\r\n", ""), "[CameraUnlock]\r\n", "[CameraUnlock]\r\nConfigFormat=1\r\n")},
        {Replace(canonical, "ConfigFormat=1", "ConfigFormat=abc"), CanonicalDiagnosticKind::ConfigFormatInvalid, yaw_off},
    };
    for (const Case& test : cases) {
        Rig rig(dir);
        WriteBytes(rig.path, test.file);
        auto owner = rig.Make();
        const Load load = owner->Load();
        ExpectStatus(load, ConfigLoadStatus::Canonical);
        Check(std::any_of(load.diagnostics.begin(), load.diagnostics.end(),
                          [&](const CanonicalDiagnostic& d) { return d.kind == test.kind; }),
              std::string("the reader warns ") + CanonicalDiagnosticKindName(test.kind));
        ExpectSaved(owner->Save([](HeadTrackingConfig& c) { c.world_space_yaw = true; }));
        Check(HoldsBytes(rig.path, test.file), "a save that changes nothing writes nothing");
        ExpectSaved(owner->Save([](HeadTrackingConfig& c) { c.world_space_yaw = false; }));
        Check(HoldsBytes(rig.path, test.saved), "the save writes ConfigFormat=1 with the change");
        Check(rig.Make()->Load().diagnostics.empty(), "the saved file reads clean");
        Check(rig.legacy->Runs() == 0 && rig.sink.empty(), "no import and nothing reported");
        Check(ListingIs(dir, {kFileName}), "nothing else is written");
    }
}

void ASaveWritesOnlyTheChangedRow(const fs::path& dir) {
    Rig rig(dir);
    const std::string mine = Replace(Render(Defaults()), "[General]\r\n", "[General]\r\n; my note\r\nMyOwnKey=1\r\n");
    WriteBytes(rig.path, mine);
    auto owner = rig.Make();
    const Load load = owner->Load();
    ExpectStatus(load, ConfigLoadStatus::Canonical);
    Check(std::any_of(load.diagnostics.begin(), load.diagnostics.end(),
                      [](const CanonicalDiagnostic& d) { return d.kind == CanonicalDiagnosticKind::UnknownKey; }),
          "the unknown key is reported");
    ExpectSaved(owner->Save([](HeadTrackingConfig& c) { c.world_space_yaw = false; }));
    Check(HoldsBytes(rig.path, Replace(mine, "WorldSpaceYaw=true", "WorldSpaceYaw=false")),
          "only the changed row's value is replaced; the hand comment and key stay");
    Check(ListingIs(dir, {kFileName}), "nothing else is written");
}

void AModeChangeWritesBothRows(const fs::path& dir) {
    Rig rig(dir);
    const std::string without = Replace(Render(Defaults()), "RotationEnabled=true\r\n", "");
    WriteBytes(rig.path, without);
    auto owner = rig.Make();
    ExpectStatus(owner->Load(), ConfigLoadStatus::Canonical);
    ExpectSaved(owner->Save([](HeadTrackingConfig& c) { c.position_enabled = false; }));
    const std::string saved = ReadBytes(rig.path);
    Check(Contains(saved, "PositionEnabled=false\r\n") && Contains(saved, "RotationEnabled=true\r\n"),
          "both rows of the mode are written");
}

void ATableMarkingOneModeRowWritableIsRefused(const fs::path& dir) {
    const fs::path path = dir / kFileName;
    const auto options = [&](Concept writable) {
        ConfigOwnerOptions<HeadTrackingConfig> made;
        made.path = path.wstring();
        made.table = HeadTrackingConfigTable<HeadTrackingConfig>(
            {Concept::UdpPort, Concept::RotationEnabled, Concept::PositionEnabled});
        made.table.Select(writable).Writable();
        made.header = RenderHeader{kDisplay};
        return made;
    };
    Check(Contains(Thrown<std::invalid_argument>([&] { Owner owner(options(Concept::PositionEnabled)); }),
                   "the table marks [Position] PositionEnabled Writable but not [General] RotationEnabled"),
          "PositionEnabled Writable alone is refused");
    Check(Contains(Thrown<std::invalid_argument>([&] { Owner owner(options(Concept::RotationEnabled)); }),
                   "the table marks [General] RotationEnabled Writable but not [Position] PositionEnabled"),
          "RotationEnabled Writable alone is refused");
    Check(ListingIs(dir, {}), "nothing is written");

    ConfigOwnerOptions<HeadTrackingConfig> two_state;
    two_state.path = path.wstring();
    two_state.table = HeadTrackingConfigTable<HeadTrackingConfig>({Concept::UdpPort, Concept::PositionEnabled});
    two_state.table.Select(Concept::PositionEnabled).Writable();
    two_state.header = RenderHeader{kDisplay};
    Owner owner(two_state);
    Check(owner.Load().status == ConfigLoadStatus::Created, "a table with one mode row is built and loads");
    const std::string created = ReadBytes(path);
    Check(Contains(created, "PositionEnabled=true\r\n") && !Contains(created, "RotationEnabled="),
          "the file holds PositionEnabled and no RotationEnabled");
    ExpectSaved(owner.Save([](HeadTrackingConfig& c) { c.position_enabled = false; }));
    Check(HoldsBytes(path, Replace(created, "PositionEnabled=true", "PositionEnabled=false")), "the one row is saved");
}

void ASaveWithNothingChangedWritesNothing(const fs::path& dir) {
    Rig rig(dir);
    auto owner = rig.Make();
    ExpectStatus(owner->Load(), ConfigLoadStatus::Created);
    const auto written = fs::last_write_time(rig.path);
    std::vector<std::string> steps;
    rig.hook = [&](const std::string& step, const std::wstring&) -> std::uint32_t {
        steps.push_back(step);
        return 0;
    };
    ExpectSaved(owner->Save([](HeadTrackingConfig& c) { c.world_space_yaw = true; }));
    Check(steps.empty(), "the writer never ran");
    Check(HoldsBytes(rig.path, Render(Defaults())) && fs::last_write_time(rig.path) == written, "the file is not rewritten");
}

void ASaveConflictIsNotSaved(const fs::path& dir) {
    Rig rig(dir);
    auto owner = rig.Make();
    ExpectStatus(owner->Load(), ConfigLoadStatus::Created);
    const std::string theirs = Render(Defaults()) + "[Extra]\r\nNote=1\r\n";
    rig.hook = [&](const std::string& step, const std::wstring&) -> std::uint32_t {
        if (step == "Save.RecheckTarget") WriteBytes(rig.path, theirs);
        return 0;
    };
    const ConfigSaveResult save = owner->Save([](HeadTrackingConfig& c) { c.world_space_yaw = false; });
    ExpectNotSaved(save, "the file was changed by another program at the same time");
    Check(save.error == 0, "a conflict carries no error");
    ExpectSunkOnce(rig, save.reason);
    Check(HoldsBytes(rig.path, theirs), "the other program's write is kept");
    Check(ListingIs(dir, {kFileName}), "nothing else is left");
}

void AChangeToARowThatIsNotWritableThrows(const fs::path& dir) {
    Rig rig(dir);
    auto owner = rig.Make();
    ExpectStatus(owner->Load(), ConfigLoadStatus::Created);
    Check(Contains(Thrown<std::logic_error>([&] {
                       owner->Save([](HeadTrackingConfig& c) { c.enable_on_startup = !c.enable_on_startup; });
                   }),
                   "[General] EnableOnStartup changed, but the table does not mark it Writable"),
          "a change to a row that is not Writable throws, naming it");
    Check(HoldsBytes(rig.path, Render(Defaults())), "nothing is written");
    Check(rig.sink.empty(), "a programming error is thrown, not reported");
}

void ASaveOfAMissingFileCreatesNothing(const fs::path& dir) {
    Rig rig(dir);
    auto owner = rig.Make();
    ExpectStatus(owner->Load(), ConfigLoadStatus::Created);
    fs::remove(rig.path);
    ExpectNotSaved(owner->Save([](HeadTrackingConfig& c) { c.world_space_yaw = false; }),
                   "the settings file is missing; it is created again at the next launch");
    Check(ListingIs(dir, {}), "nothing is created");
}

void ASaveToALegacyFileIsRefused(const fs::path& dir) {
    Rig rig(dir);
    WriteBytes(rig.path, kLegacyText);
    auto owner = rig.Make();
    ExpectStatus(owner->Load(), ConfigLoadStatus::Migrated);
    WriteBytes(rig.path, kLegacyText);
    ExpectNotSaved(owner->Save([](HeadTrackingConfig& c) { c.world_space_yaw = true; }),
                   std::string(kFileNameText) + " is in the old settings format; it is converted at the next launch");
    Check(HoldsBytes(rig.path, kLegacyText), "the old file is not edited");
}

void ASaveToAReadOnlyFileIsNotSaved(const fs::path& dir) {
    Rig rig(dir);
    auto owner = rig.Make();
    ExpectStatus(owner->Load(), ConfigLoadStatus::Created);
    SetReadOnly(rig.path, true);
    const ConfigSaveResult save = owner->Save([](HeadTrackingConfig& c) { c.world_space_yaw = false; });
    ExpectNotSaved(save, "the file is read-only");
    Check(save.error == ERROR_ACCESS_DENIED, "the writer's error is carried");
    Check(CountContaining(save.log, "Writing " + rig.Text() + " failed at Commit") == 1, "the log names the file and the step");
    Check(HoldsBytes(rig.path, Render(Defaults())), "the file is unchanged");
    Check(ListingIs(dir, {kFileName}), "nothing else is left");
}

void AnUnfinishedSaveIsUncertain(const fs::path& dir) {
    Rig rig(dir);
    auto owner = rig.Make();
    ExpectStatus(owner->Load(), ConfigLoadStatus::Created);
    rig.hook = [](const std::string& step, const std::wstring&) -> std::uint32_t {
        return step == "Save.Commit" ? ERROR_UNABLE_TO_MOVE_REPLACEMENT : 0;
    };
    const ConfigSaveResult save = owner->Save([](HeadTrackingConfig& c) { c.world_space_yaw = false; });
    Check(save.status == ConfigSaveStatus::Uncertain, "an unfinished replacement is Uncertain");
    Check(!save.temporary_path.empty() && fs::exists(save.temporary_path), "the temporary is kept and named");
    Check(Contains(save.reason, rig.Text()) && Contains(save.reason, Utf8(save.temporary_path)),
          "the reason names the file and the temporary");
    ExpectSunkOnce(rig, save.reason);
    Check(HoldsBytes(save.temporary_path, Replace(Render(Defaults()), "WorldSpaceYaw=true", "WorldSpaceYaw=false")),
          "the temporary holds the new contents");
    fs::remove(save.temporary_path);
    Check(ListingIs(dir, {kFileName}), "nothing else is left");
}

void ReloadIgnoresTheOwnersOwnWrites(const fs::path& dir) {
    Rig rig(dir);
    WriteBytes(rig.path, kLegacyText);
    auto owner = rig.Make();
    ExpectStatus(owner->Load(), ConfigLoadStatus::Migrated);
    Check(!owner->FileChanged(), "the conversion's write is recorded");
    Check(owner->Reload().status == ConfigReloadStatus::Unchanged, "the conversion's bytes reload as Unchanged");

    ExpectSaved(owner->Save([](HeadTrackingConfig& c) { c.world_space_yaw = true; }));
    Check(!owner->FileChanged(), "the save's write is recorded");
    Check(owner->Reload().status == ConfigReloadStatus::Unchanged, "the save's bytes reload as Unchanged");

    WriteBytes(rig.path, Replace(ReadBytes(rig.path), "UdpPort=5555", "UdpPort=6000"));
    fs::last_write_time(rig.path, fs::last_write_time(rig.path) + std::chrono::seconds(5));
    Check(owner->FileChanged(), "an outside edit is seen");
    const Reload reload = owner->Reload();
    Check(reload.status == ConfigReloadStatus::Applied && reload.config && reload.config->udp_port == 6000 &&
              reload.config->world_space_yaw,
          "an outside edit is applied");
    Check(!owner->FileChanged(), "the reload records the write time");
    Check(rig.legacy->Runs() == 1 && rig.sink.empty(), "no import and nothing reported");
}

void ReloadOfAnOldFileIsReadOnly(const fs::path& dir) {
    Rig rig(dir);
    WriteBytes(rig.path, kLegacyText);
    auto owner = rig.Make();
    ExpectStatus(owner->Load(), ConfigLoadStatus::Migrated);
    const std::string old = Replace(kLegacyText, "5555", "7000");
    WriteBytes(rig.path, old);
    const Reload reload = owner->Reload();
    Check(reload.status == ConfigReloadStatus::LegacyReadOnly && reload.config && reload.config->udp_port == 7000,
          "an old file put back is read through the import");
    Check(Contains(reload.reason, "converted at the next launch"), "the reason says when it is converted");
    Check(rig.legacy->Runs() == 2, "the import ran again");
    Check(HoldsBytes(rig.path, old), "the old file is not written");
    Check(ListingIs(dir, {kFileName, L"HeadTracking.ini.pre-canonical"}), "nothing else is written");
    Check(rig.sink.empty(), "a read-only reload is not a failure");
}

void ReloadOfAnUnreadableFileKeepsTheSettings(const fs::path& dir) {
    Rig rig(dir);
    auto owner = rig.Make();
    ExpectStatus(owner->Load(), ConfigLoadStatus::Created);
    const std::string utf16 = Utf16(Render(Defaults()));
    WriteBytes(rig.path, utf16);
    const Reload reload = owner->Reload();
    Check(reload.status == ConfigReloadStatus::Unreadable && !reload.config, "Unreadable, with no settings");
    Check(Contains(reload.reason, "it is saved as UTF-16"), "the reason says why");
    ExpectSunkOnce(rig, reload.reason);
    Check(rig.legacy->Runs() == 0, "a stamped file is never imported");
    Check(HoldsBytes(rig.path, utf16), "the file is not written");
}

void ReloadOfAnOldFileTheImportCannotFindKeepsTheSettings(const fs::path& dir) {
    Rig rig(dir);
    WriteBytes(rig.path, kLegacyText);
    auto owner = rig.Make();
    ExpectStatus(owner->Load(), ConfigLoadStatus::Migrated);
    WriteBytes(rig.path, kLegacyText);
    rig.legacy->result = [](HeadTrackingConfig&) { return ImportResult::Absent({}); };
    const Reload reload = owner->Reload();
    Check(reload.status == ConfigReloadStatus::Unreadable && !reload.config, "an import that finds no file leaves the settings");
    Check(Contains(reload.reason, "the old settings reader could not find the file"), "the reason says why");
    ExpectLogLine(reload.log, rig.Text() + ": not reloaded: the old settings reader found no file, while the owner holds it "
                                           "open (" +
                                  std::to_string(kLegacyText.size()) + " bytes)");
    ExpectSunkOnce(rig, reload.reason);
    Check(rig.legacy->Runs() == 2, "the import ran on the reload");
    Check(HoldsBytes(rig.path, kLegacyText), "the file is not written");
}

void ReloadOfAnOldFileChangedDuringTheImportKeepsTheSettings(const fs::path& dir) {
    Rig rig(dir);
    WriteBytes(rig.path, kLegacyText);
    auto owner = rig.Make();
    ExpectStatus(owner->Load(), ConfigLoadStatus::Migrated);
    WriteBytes(rig.path, kLegacyText);
    const std::string changed = Replace(kLegacyText, "5555", "7000");
    rig.legacy->during = [&](const LegacyInput& input) { WriteBytes(input.path, changed); };
    Reload reload = owner->Reload();
    Check(reload.status == ConfigReloadStatus::Unreadable && !reload.config,
          "a file changed while the import read it leaves the settings");
    Check(Contains(reload.reason, "the file was changed by another program while it was read"), "the reason says why");
    ExpectSunkOnce(rig, reload.reason);
    Check(HoldsBytes(rig.path, changed), "the other program's write is kept");

    rig.legacy->during = nullptr;
    rig.sink.clear();
    reload = owner->Reload();
    Check(reload.status == ConfigReloadStatus::LegacyReadOnly && reload.config && reload.config->udp_port == 7000,
          "the next reload reads the settled file");
    Check(rig.sink.empty(), "nothing more is reported");
}

void OptionsAndCallOrderAreChecked(const fs::path& dir) {
    const fs::path path = dir / kFileName;
    const auto options = [&]() {
        ConfigOwnerOptions<HeadTrackingConfig> made;
        made.path = path.wstring();
        made.table = Table();
        made.header = RenderHeader{kDisplay};
        return made;
    };
    ConfigOwnerOptions<HeadTrackingConfig> no_path = options();
    no_path.path.clear();
    Check(Contains(Thrown<std::invalid_argument>([&] { Owner owner(no_path); }), "the options name no path"),
          "no path is refused");
    ConfigOwnerOptions<HeadTrackingConfig> relative = options();
    relative.path = kFileName;
    Check(Contains(Thrown<std::invalid_argument>([&] { Owner owner(relative); }), "is not a fully qualified path"),
          "a relative path is refused");
    relative.path = L"C:HeadTracking.ini";
    Check(Contains(Thrown<std::invalid_argument>([&] { Owner owner(relative); }), "is not a fully qualified path"),
          "a drive-relative path is refused");
    ConfigOwnerOptions<HeadTrackingConfig> no_table = options();
    no_table.table = ConfigTable<HeadTrackingConfig>();
    Check(Contains(Thrown<std::invalid_argument>([&] { Owner owner(no_table); }), "the options' table has no rows"),
          "an empty table is refused");
    ConfigOwnerOptions<HeadTrackingConfig> no_header = options();
    no_header.header = RenderHeader{};
    Check(Thrown<std::invalid_argument>([&] { Owner owner(no_header); }) != "(nothing thrown)", "no header is refused");
    ConfigOwnerOptions<HeadTrackingConfig> bad_header = options();
    bad_header.header = RenderHeader{"ABZ\xC3\x9B"};
    Check(Thrown<std::invalid_argument>([&] { Owner owner(bad_header); }) != "(nothing thrown)",
          "a header the renderer refuses is refused");
    ConfigOwnerOptions<HeadTrackingConfig> keys_only = options();
    keys_only.import.keys = {{"General", "Port"}};
    Check(Contains(Thrown<std::invalid_argument>([&] { Owner owner(keys_only); }),
                   "the options' import names keys but has no run"),
          "an import with keys and no run is refused");

    Owner owner(options());
    Check(Contains(Thrown<std::logic_error>([&] { owner.Save([](HeadTrackingConfig& c) { c.world_space_yaw = false; }); }),
                   "Save needs Load to have run first"),
          "Save before Load throws");
    Check(Contains(Thrown<std::logic_error>([&] { owner.Reload(); }), "Reload needs Load to have run first"),
          "Reload before Load throws");
    Check(Contains(Thrown<std::logic_error>([&] { owner.FileChanged(); }), "FileChanged needs Load to have run first"),
          "FileChanged before Load throws");
    Check(Contains(Thrown<std::invalid_argument>([&] { owner.Save(nullptr); }), "Save needs a change"),
          "an empty change throws");
    Check(ListingIs(dir, {}), "nothing is written");

    Check(std::string(ConfigLoadStatusName(ConfigLoadStatus::LegacyRefused)) == "LegacyRefused" &&
              std::string(ConfigSaveStatusName(ConfigSaveStatus::Uncertain)) == "Uncertain" &&
              std::string(ConfigReloadStatusName(ConfigReloadStatus::LegacyReadOnly)) == "LegacyReadOnly",
          "status names are the C# spellings");
}

// The steps of an in-place conversion, as the owner's hook names them.
std::vector<std::string> InterruptionLabels() {
    const CheckedWriteStep writer[] = {
        CheckedWriteStep::ReadTarget,     CheckedWriteStep::CreateTemporary, CheckedWriteStep::WriteTemporary,
        CheckedWriteStep::FlushTemporary, CheckedWriteStep::CloseTemporary,  CheckedWriteStep::RecheckTarget,
        CheckedWriteStep::Commit,
    };
    std::vector<std::string> labels = {"Open", "Import", "Recheck"};
    for (CheckedWriteStep step : writer) labels.push_back(std::string("Copy.") + cameraunlock::CheckedWriteStepName(step));
    labels.push_back("ReadBack");
    for (CheckedWriteStep step : writer) labels.push_back(std::string("Commit.") + cameraunlock::CheckedWriteStepName(step));
    labels.push_back("Remember");
    return labels;
}

// The child is killed at the start of the labelled step. After it, the legacy file is whole or
// the new file is, beside it at most the copy and the writer's temporaries, and the next launch
// ends where an uninterrupted one does.
void KilledDuring(const std::string& label, const fs::path& dir) {
    const fs::path target = dir / kFileName;
    const std::wstring copy = WithSuffix(target, L".pre-canonical");
    WriteBytes(target, kLegacyText);

    std::vector<wchar_t> exe(32768);
    const DWORD length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    Check(length > 0 && length < exe.size(), "found this executable");
    std::wstring command = L"\"" + std::wstring(exe.data(), length) + L"\" --config-owner-interrupt " +
                           std::wstring(label.begin(), label.end());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const bool started = CreateProcessW(exe.data(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(),
                                        &startup, &process) != FALSE;
    Check(started, "started the child");
    if (!started) return;
    const bool exited = WaitForSingleObject(process.hProcess, 60000) == WAIT_OBJECT_0;
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    Check(exited && code == kKilledExitCode, "the child was killed partway through");

    const bool committed = label == "Remember";
    Check(HoldsBytes(target, committed ? MigratedBytes() : kLegacyText),
          committed ? "the new file is whole" : "the legacy file is whole");
    const std::wstring temporary_prefix = std::wstring(kFileName) + L".";
    bool strays_are_temporaries = true;
    for (const auto& entry : fs::directory_iterator(dir)) {
        const std::wstring name = entry.path().filename().wstring();
        if (entry.path() == target) continue;
        if (entry.path().wstring() == copy) {
            Check(HoldsBytes(copy, kLegacyText), "a copy left behind is whole");
            continue;
        }
        strays_are_temporaries = strays_are_temporaries && name.compare(0, temporary_prefix.size(), temporary_prefix) == 0 &&
                                 name.size() > 4 && name.compare(name.size() - 4, 4, L".tmp") == 0;
    }
    Check(strays_are_temporaries, "nothing but the copy and the writer's temporaries is beside it");

    Rig rig(dir);
    const Load next = rig.Make()->Load();
    ExpectStatus(next, committed ? ConfigLoadStatus::Canonical : ConfigLoadStatus::Migrated);
    Check(HoldsBytes(target, MigratedBytes()), "the next launch ends with the new file");
    Check(HoldsBytes(copy, kLegacyText), "and the copy of the original");
    Check(!fs::exists(WithSuffix(target, L".pre-canonical.last")), "and no .pre-canonical.last");
}

void RunScenario(const std::string& name, const std::function<void(const fs::path&)>& body) {
    g_scenario = name;
    std::random_device random;
    const fs::path dir =
        fs::temp_directory_path() / (L"cu-config-owner-" + std::to_wstring(random()) + std::to_wstring(random()));
    fs::create_directories(dir);
    try {
        body(dir);
    } catch (const std::exception& e) {
        Check(false, std::string("threw ") + e.what());
    }
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        SetFileAttributesW(entry.path().c_str(), FILE_ATTRIBUTE_NORMAL);
    }
    fs::remove_all(dir);
}

}  // namespace

int RunConfigOwnerInterruptChild(const char* label) {
    Rig rig(fs::current_path());
    const std::string kill_at = label;
    rig.hook = [kill_at](const std::string& step, const std::wstring&) -> std::uint32_t {
        if (step == kill_at) TerminateProcess(GetCurrentProcess(), kKilledExitCode);
        return 0;
    };
    rig.Make()->Load();
    return 0;
}

int RunConfigOwnerTests() {
    std::cout << "\nConfigOwner:\n";
    g_failures = 0;
    RunScenario("an-absent-file-is-created", AnAbsentFileIsCreated);
    RunScenario("a-file-appearing-during-creation-defers", AFileAppearingDuringCreationDefers);
    RunScenario("a-stamped-file-is-canonical", AStampedFileIsCanonical);
    RunScenario("a-stamped-utf16-file-is-unreadable-and-never-migrated", AStampedUtf16FileIsUnreadableAndNeverMigrated);
    RunScenario("a-stamped-file-holding-a-nul-is-unreadable", AStampedFileHoldingANulIsUnreadable);
    RunScenario("an-unstamped-file-with-an-import-is-migrated", AnUnstampedFileWithAnImportIsMigrated);
    RunScenario("an-unstamped-utf16-file-with-an-import-is-migrated", AnUnstampedUtf16FileWithAnImportIsMigrated);
    RunScenario("an-unstamped-file-holding-a-nul-with-an-import-is-migrated",
                AnUnstampedFileHoldingANulWithAnImportIsMigrated);
    RunScenario("a-second-load-rewrites-nothing", ASecondLoadRewritesNothing);
    RunScenario("an-unstamped-file-without-an-import-is-canonical-and-stamped-by-a-save",
                AnUnstampedFileWithoutAnImportIsCanonicalAndStampedByASave);
    RunScenario("an-unstamped-unreadable-file-without-an-import-is-unreadable",
                AnUnstampedUnreadableFileWithoutAnImportIsUnreadable);
    RunScenario("a-dropped-value-is-logged", ADroppedValueIsLogged);
    RunScenario("a-deleted-stamp-migrates-again-into-pre-canonical-last", ADeletedStampMigratesAgainIntoPreCanonicalLast);
    RunScenario("a-refused-import-is-legacy-refused", ARefusedImportIsLegacyRefused);
    RunScenario("an-undecodable-import-defers", AnUndecodableImportDefers);
    RunScenario("an-absent-import-defers", AnAbsentImportDefers);
    RunScenario("a-read-only-file-defers", AReadOnlyFileDefers);
    RunScenario("a-folder-that-cannot-be-written-defers", AFolderThatCannotBeWrittenDefers);
    RunScenario("a-file-held-denying-read-sharing-defers", AFileHeldDenyingReadSharingDefers);
    RunScenario("an-import-that-writes-the-file-defers", AnImportThatWritesTheFileDefers);
    RunScenario("a-failed-copy-defers", AFailedCopyDefers);
    RunScenario("a-copy-that-does-not-read-back-defers", ACopyThatDoesNotReadBackDefers);
    RunScenario("a-verify-mismatch-defers", AVerifyMismatchDefers);
    RunScenario("a-value-no-codec-writes-defers", AValueNoCodecWritesDefers);
    RunScenario("a-file-changed-before-the-commit-defers", AFileChangedBeforeTheCommitDefers);
    RunScenario("the-held-file-reads-and-refuses-exclusive-opens", TheHeldFileReadsAndRefusesExclusiveOpens);
    RunScenario("a-path-outside-the-ansi-code-page", APathOutsideTheAnsiCodePage);
    RunScenario("a-newer-config-format-refuses-saves", ANewerConfigFormatRefusesSaves);
    RunScenario("a-save-writes-a-missing-or-unreadable-config-format", ASaveWritesAMissingOrUnreadableConfigFormat);
    RunScenario("a-save-writes-only-the-changed-row", ASaveWritesOnlyTheChangedRow);
    RunScenario("a-mode-change-writes-both-rows", AModeChangeWritesBothRows);
    RunScenario("a-table-marking-one-mode-row-writable-is-refused", ATableMarkingOneModeRowWritableIsRefused);
    RunScenario("a-save-with-nothing-changed-writes-nothing", ASaveWithNothingChangedWritesNothing);
    RunScenario("a-save-conflict-is-not-saved", ASaveConflictIsNotSaved);
    RunScenario("a-change-to-a-row-that-is-not-writable-throws", AChangeToARowThatIsNotWritableThrows);
    RunScenario("a-save-of-a-missing-file-creates-nothing", ASaveOfAMissingFileCreatesNothing);
    RunScenario("a-save-to-a-legacy-file-is-refused", ASaveToALegacyFileIsRefused);
    RunScenario("a-save-to-a-read-only-file-is-not-saved", ASaveToAReadOnlyFileIsNotSaved);
    RunScenario("an-unfinished-save-is-uncertain", AnUnfinishedSaveIsUncertain);
    RunScenario("reload-ignores-the-owners-own-writes", ReloadIgnoresTheOwnersOwnWrites);
    RunScenario("reload-of-an-old-file-is-read-only", ReloadOfAnOldFileIsReadOnly);
    RunScenario("reload-of-an-unreadable-file-keeps-the-settings", ReloadOfAnUnreadableFileKeepsTheSettings);
    RunScenario("reload-of-an-old-file-the-import-cannot-find-keeps-the-settings",
                ReloadOfAnOldFileTheImportCannotFindKeepsTheSettings);
    RunScenario("reload-of-an-old-file-changed-during-the-import-keeps-the-settings",
                ReloadOfAnOldFileChangedDuringTheImportKeepsTheSettings);
    RunScenario("options-and-call-order-are-checked", OptionsAndCallOrderAreChecked);
    for (const std::string& label : InterruptionLabels()) {
        RunScenario("killed-during-" + label, [label](const fs::path& dir) { KilledDuring(label, dir); });
    }
    return g_failures;
}

#else

int RunConfigOwnerInterruptChild(const char*) { return 1; }
int RunConfigOwnerTests() { return 0; }

#endif  // _WIN32
