// ConfigOwner against real files and real Windows handles, scenario for scenario with the C#
// ConfigOwnerScenarios, plus what only the C++ owner does: the held-handle test with the Win32
// and CRT readers imports call, a path the ANSI code page cannot hold, a config file pending
// deletion, and killed-child runs through `--config-owner-interrupt <step>`. The config file is
// CameraUnlock.ini and the legacy file HeadTracking.ini beside it; every import scenario checks
// the legacy file's bytes and write time and the folder's listing.

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

namespace cameraunlock::config::detail {

struct ConfigOwnerTestAccess {
    template <class Config>
    static std::unique_ptr<ConfigOwner<Config>> Make(ConfigOwnerOptions<Config> options, ConfigOwnerHook hook) {
        return std::unique_ptr<ConfigOwner<Config>>(new ConfigOwner<Config>(std::move(options), std::move(hook)));
    }
};

}  // namespace cameraunlock::config::detail

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
static_assert(static_cast<int>(ConfigReloadStatus::Unreadable) == 3, "Unreadable");

using Owner = ConfigOwner<HeadTrackingConfig>;
using Load = ConfigLoadResult<HeadTrackingConfig>;
using Reload = ConfigReloadResult<HeadTrackingConfig>;

constexpr wchar_t kFileName[] = L"CameraUnlock.ini";
constexpr char kFileNameText[] = "CameraUnlock.ini";
constexpr wchar_t kLegacyName[] = L"HeadTracking.ini";
constexpr char kLegacyNameText[] = "HeadTracking.ini";
constexpr char kDisplay[] = "Test Game";
constexpr int kKilledExitCode = 3;

// Line 5 holds a key the legacy reader does not read.
const std::string kLegacyText =
    "; tuned by hand\r\n[General]\r\nPort = 5555\r\nYawWorld = false\r\nSmoothng = 0.3\r\n[Position]\r\nPosition = false\r\n";

// Set on every legacy file a scenario writes, so any write to it shows as a new time.
const fs::file_time_type kLegacyWriteTime =
    std::chrono::floor<std::chrono::seconds>(fs::file_time_type::clock::now() - std::chrono::hours(24 * 400));

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

bool StartsWith(const std::string& text, const std::string& start) { return text.compare(0, start.size(), start) == 0; }

bool EndsWith(const std::string& text, const std::string& end) {
    return text.size() >= end.size() && text.compare(text.size() - end.size(), end.size(), end) == 0;
}

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

std::vector<std::wstring> Listing(const fs::path& dir) {
    std::vector<std::wstring> actual;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()) actual.push_back(entry.path().filename().wstring());
    }
    std::sort(actual.begin(), actual.end());
    return actual;
}

bool ListingIs(const fs::path& dir, std::vector<std::wstring> names) {
    std::sort(names.begin(), names.end());
    return Listing(dir) == names;
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
    fs::path dir;
    fs::path path;
    fs::path legacy_path;
    std::string legacy_bytes;
    std::vector<std::string> sink;
    std::shared_ptr<Legacy> legacy = std::make_shared<Legacy>();
    std::function<std::uint32_t(const std::string&, const std::wstring&)> hook;
    bool with_import = true;

    explicit Rig(const fs::path& folder) : dir(folder), path(folder / kFileName), legacy_path(folder / kLegacyName) {}

    std::string Text() const { return Utf8(path.wstring()); }
    std::string LegacyText() const { return Utf8(legacy_path.wstring()); }

    void PutLegacy(const std::string& bytes) {
        WriteBytes(legacy_path, bytes);
        fs::last_write_time(legacy_path, kLegacyWriteTime);
        legacy_bytes = bytes;
    }

    ConfigOwnerOptions<HeadTrackingConfig> Options() {
        ConfigOwnerOptions<HeadTrackingConfig> options;
        options.path = path.wstring();
        options.table = Table();
        options.header = RenderHeader{kDisplay};
        if (with_import) {
            options.import = legacy->Import();
            options.legacy_path = legacy_path.wstring();
        }
        options.status_sink = [this](const std::string& message) { sink.push_back(message); };
        return options;
    }

    std::unique_ptr<Owner> Make() {
        return detail::ConfigOwnerTestAccess::Make(
            Options(), [this](const std::string& label, const std::wstring& at) -> std::uint32_t {
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

void ExpectReason(const std::string& reason, const std::string& expected) {
    Check(reason == expected, "the player is told \"" + expected + "\"" + (reason == expected ? "" : ", got \"" + reason + "\""));
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

void ExpectLegacyKept(const Rig& rig, const std::string& when = "") {
    const std::string prefix = when.empty() ? "" : when + ": ";
    Check(HoldsBytes(rig.legacy_path, rig.legacy_bytes), prefix + "the legacy file holds its bytes");
    Check(fs::exists(rig.legacy_path) && fs::last_write_time(rig.legacy_path) == kLegacyWriteTime,
          prefix + "the legacy file keeps its write time");
}

// An import that did not complete leaves the legacy file as it was and creates nothing.
void ExpectNotImported(const Rig& rig) {
    ExpectLegacyKept(rig);
    Check(ListingIs(rig.dir, {kLegacyName}), "nothing is created beside the legacy file");
}

void ExpectImported(const Rig& rig) {
    Check(HoldsBytes(rig.path, MigratedBytes()), "the config file holds the rendered imported values");
    ExpectLegacyKept(rig);
    Check(ListingIs(rig.dir, {kFileName, kLegacyName}), "the folder holds the config and the legacy file only");
}

std::string SettingsReadLine(const Rig& rig) {
    return rig.Text() + ": settings are read from this file. " + rig.LegacyText() + " is left as it was and is not read.";
}

std::string RetriedReason(const std::string& why) {
    return std::string(kLegacyNameText) + " was not imported into " + kFileNameText + ": " + why +
           ". The mod tries again at the next launch and saves nothing this session.";
}

std::string AppearedTail() {
    return std::string(". The mod saves nothing this session and reads ") + kFileNameText + ", not " + kLegacyNameText +
           ", at the next launch.";
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

void AnAbsentFileIsCreated(const fs::path& dir) {
    Rig rig(dir);
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Created);
    Check(HoldsBytes(rig.path, Render(Defaults())), "the file holds the rendered defaults");
    Check(Same(load.config, Defaults()), "the session runs on the defaults");
    Check(rig.legacy->Runs() == 0, "no import runs when there is no legacy file");
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
    Check(load.diagnostics.empty() && load.log.empty(), "a clean file with no legacy file beside it draws nothing");
    Check(rig.legacy->Runs() == 0, "the import never runs while the config exists");
    Check(HoldsBytes(rig.path, canonical), "the file is not written");
    Check(ListingIs(dir, {kFileName}), "nothing else is written");
}

void AStampedUtf16FileIsUnreadable(const fs::path& dir) {
    Rig rig(dir);
    const std::string utf16 = Utf16(Render(Defaults()));
    WriteBytes(rig.path, utf16);
    auto owner = rig.Make();
    const Load load = owner->Load();
    ExpectStatus(load, ConfigLoadStatus::Unreadable);
    Check(Contains(load.reason, "it is saved as UTF-16; save it as ANSI or UTF-8"), "the reason says what to do");
    Check(Same(load.config, Defaults()), "the session runs on the defaults");
    Check(rig.legacy->Runs() == 0, "the import never runs while the config exists");
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
    Check(rig.legacy->Runs() == 0, "the import never runs while the config exists");
    Check(HoldsBytes(rig.path, bytes), "the file is not written");
    Check(ListingIs(dir, {kFileName}), "nothing else is written");
}

void ALegacyFileIsImportedAndLeftAsItWas(const fs::path& dir) {
    Rig rig(dir);
    rig.PutLegacy(kLegacyText);
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Migrated);
    Check(Same(load.config, MigratedConfig()), "the session runs on the imported values");
    Check(load.diagnostics.empty(), "the new file reads back clean");
    ExpectLogLine(load.log, rig.Text() + ": created from " + rig.LegacyText() + ", which is left as it was.");
    ExpectLogLine(load.log,
                  rig.LegacyText() + ": not carried: [General] Smoothng=0.3 on line 5, this build does not read it");
    Check(CountContaining(load.log, "not carried") == 1, "only the unread key is listed");
    Check(rig.legacy->Runs() == 1 && rig.legacy->inputs[0].path == rig.legacy_path.wstring() &&
              detail::OwnerAnsiForLog(rig.legacy->inputs[0].ansi_path) == rig.LegacyText() &&
              !rig.legacy->inputs[0].ansi_lossy,
          "the import runs once, on the legacy file, given its wide and ANSI paths");
    Check(rig.sink.empty(), "nothing is reported");
    ExpectImported(rig);
}

void AUtf16LegacyFileIsImported(const fs::path& dir) {
    Rig rig(dir);
    rig.PutLegacy(Utf16(kLegacyText));
    rig.legacy->result = [](HeadTrackingConfig& config) {
        config = MigratedConfig();
        return ImportResult::Imported({});
    };
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Migrated);
    Check(Same(load.config, MigratedConfig()), "the session runs on the imported values");
    ExpectLogLine(load.log, rig.LegacyText() +
                                ": is saved as UTF-16, so its lines this build does not read are not listed; the original "
                                "keeps them.");
    Check(CountContaining(load.log, "not carried") == 0, "no line of a UTF-16 file is listed");
    Check(rig.legacy->Runs() == 1 && rig.sink.empty(), "one import and nothing reported");
    ExpectImported(rig);
}

void ALegacyFileHoldingANulIsImported(const fs::path& dir) {
    Rig rig(dir);
    rig.PutLegacy(kLegacyText + "Extra=1\0\r\n"s);
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Migrated);
    Check(Same(load.config, MigratedConfig()), "the session runs on the imported values");
    ExpectLogLine(load.log,
                  rig.LegacyText() + ": not carried: [General] Smoothng=0.3 on line 5, this build does not read it");
    ExpectLogLine(load.log,
                  rig.LegacyText() + ": not carried: [Position] Extra=1\0 on line 8, this build does not read it"s);
    Check(CountContaining(load.log, "not carried") == 2, "only the unread keys are listed");
    Check(rig.legacy->Runs() == 1 && rig.sink.empty(), "one import and nothing reported");
    ExpectImported(rig);
}

void ASecondLoadRewritesNothing(const fs::path& dir) {
    Rig rig(dir);
    rig.PutLegacy(kLegacyText);
    ExpectStatus(rig.Make()->Load(), ConfigLoadStatus::Migrated);
    const auto written = fs::last_write_time(rig.path);

    std::vector<std::string> steps;
    rig.hook = [&](const std::string& step, const std::wstring&) -> std::uint32_t {
        steps.push_back(step);
        return 0;
    };
    const Load again = rig.Make()->Load();
    ExpectStatus(again, ConfigLoadStatus::Canonical);
    Check(Same(again.config, MigratedConfig()), "the imported values are read");
    Check(steps == std::vector<std::string>{"Open"}, "the second launch only opens the config");
    Check(rig.legacy->Runs() == 1, "the import does not run again");
    ExpectLogLine(again.log, SettingsReadLine(rig));
    Check(again.log.size() == 1, "that is the only line" + Joined(again.log));
    Check(fs::last_write_time(rig.path) == written, "the config is not rewritten");
    ExpectImported(rig);
}

void AConfigBesideALegacyFileIsReadAndTheImportNeverRuns(const fs::path& dir) {
    Rig rig(dir);
    HeadTrackingConfig chosen = Defaults();
    chosen.udp_port = 6000;
    const std::string canonical = Render(chosen);
    WriteBytes(rig.path, canonical);
    rig.PutLegacy(kLegacyText);
    std::vector<std::pair<std::string, std::wstring>> opened;
    rig.hook = [&](const std::string& step, const std::wstring& at) -> std::uint32_t {
        opened.emplace_back(step, at);
        return 0;
    };
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Canonical);
    Check(Same(load.config, chosen), "the config's values, not the legacy file's");
    Check(rig.legacy->Runs() == 0, "the import never runs while the config exists");
    Check(opened.size() == 1 && opened[0].first == "Open" && opened[0].second == rig.path.wstring(),
          "only the config is opened");
    ExpectLogLine(load.log, SettingsReadLine(rig));
    Check(load.log.size() == 1, "that is the only line" + Joined(load.log));
    Check(rig.sink.empty(), "nothing is reported");
    Check(HoldsBytes(rig.path, canonical), "the config is not written");
    ExpectLegacyKept(rig);
    Check(ListingIs(dir, {kFileName, kLegacyName}), "nothing else is written");
}

void AnUnstampedConfigBesideALegacyFileIsCanonicalAndStampedByASave(const fs::path& dir) {
    Rig rig(dir);
    rig.PutLegacy(kLegacyText);
    WriteBytes(rig.path, "; mine\r\n[General]\r\nWorldSpaceYaw=false\r\n");
    auto owner = rig.Make();
    const Load load = owner->Load();
    ExpectStatus(load, ConfigLoadStatus::Canonical);
    Check(!load.config.world_space_yaw && load.config.udp_port != 5555, "the config's values, not the legacy file's");
    Check(rig.legacy->Runs() == 0, "an unstamped config is never imported");
    ExpectLogLine(load.log, SettingsReadLine(rig));
    ExpectLogLine(load.log, rig.Text() +
                                ": has no [CameraUnlock] section. It is read as the canonical format, and the next save "
                                "adds the section.");

    ExpectSaved(owner->Save([](HeadTrackingConfig& c) { c.world_space_yaw = true; }));
    Check(HoldsBytes(rig.path, "; mine\r\n[General]\r\nWorldSpaceYaw=true\r\n\r\n[CameraUnlock]\r\nConfigFormat=1\r\n"),
          "the save stamps the file in the same edit");
    Check(rig.legacy->Runs() == 0 && rig.sink.empty(), "no import and nothing reported");
    ExpectLegacyKept(rig);
    Check(ListingIs(dir, {kFileName, kLegacyName}), "nothing else is written");
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

void AnUnreadableConfigBesideALegacyFileIsUnreadableAndNeverImported(const fs::path& dir) {
    Rig rig(dir);
    rig.PutLegacy(kLegacyText);
    const std::string utf16 = Utf16("[General]\r\nWorldSpaceYaw=false\r\n");
    WriteBytes(rig.path, utf16);
    ExpectStatus(rig.Make()->Load(), ConfigLoadStatus::Unreadable);
    Check(HoldsBytes(rig.path, utf16), "the UTF-16 file is not written");

    const std::string nul = "[General]\r\nWorldSpaceYaw=false\0\r\n"s;
    WriteBytes(rig.path, nul);
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Unreadable);
    Check(Contains(load.reason, "line 2 holds a NUL byte"), "the reason names the line");
    Check(Same(load.config, Defaults()), "the session runs on the defaults, not the legacy file's values");
    Check(rig.legacy->Runs() == 0, "the legacy file is not imported while the config exists");
    Check(HoldsBytes(rig.path, nul), "the file is not written");
    ExpectLegacyKept(rig);
    Check(ListingIs(dir, {kFileName, kLegacyName}), "nothing else is written");
}

void ADroppedValueIsLogged(const fs::path& dir) {
    Rig rig(dir);
    rig.PutLegacy(kLegacyText + "Light = NaN\r\n");
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Migrated);
    Check(load.config.light.multiplier == cameraunlock::effects::kDefaultLightMultiplier, "N2 gives the default");
    ExpectLogLine(load.log, rig.LegacyText() +
                                ": not carried: [Light] LightMultiplier=nan, it is not a finite number, so the default is used");
    Check(CountContaining(load.log, "not carried") == 2, "the dropped value and the unread key");
    ExpectLegacyKept(rig);
    Check(ListingIs(dir, {kFileName, kLegacyName}), "nothing else is written");
}

void DeletingTheConfigImportsTheLegacyFileAgain(const fs::path& dir) {
    Rig rig(dir);
    rig.PutLegacy(kLegacyText);
    auto owner = rig.Make();
    ExpectStatus(owner->Load(), ConfigLoadStatus::Migrated);
    ExpectSaved(owner->Save([](HeadTrackingConfig& c) { c.world_space_yaw = true; }));

    fs::remove(rig.path);
    const Load again = rig.Make()->Load();
    ExpectStatus(again, ConfigLoadStatus::Migrated);
    Check(rig.legacy->Runs() == 2, "the next load imports the legacy file again");
    Check(Same(again.config, MigratedConfig()), "the legacy file's values, without the deleted save");
    ExpectImported(rig);
}

void ARefusedImportIsLegacyRefused(const fs::path& dir) {
    Rig rig(dir);
    rig.legacy->result = [](HeadTrackingConfig&) { return ImportResult::Refused("Port=99999 is outside 1 to 65535"); };
    rig.PutLegacy(kLegacyText);
    auto owner = rig.Make();
    const Load load = owner->Load();
    ExpectStatus(load, ConfigLoadStatus::LegacyRefused);
    ExpectReason(load.reason, RetriedReason("Port=99999 is outside 1 to 65535"));
    ExpectSunkOnce(rig, load.reason);
    ExpectNotSaved(owner->Save([](HeadTrackingConfig& c) { c.world_space_yaw = true; }),
                   "the settings file could not be used this session");
    ExpectNotImported(rig);
}

void AnUndecodableImportDefers(const fs::path& dir) {
    Rig rig(dir);
    rig.legacy->result = [](HeadTrackingConfig&) { return ImportResult::Undecodable("the file is not UTF-8"); };
    rig.PutLegacy(kLegacyText);
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Deferred);
    ExpectReason(load.reason, RetriedReason("the file is not UTF-8"));
    ExpectSunkOnce(rig, load.reason);
    ExpectNotImported(rig);
}

void AnAbsentImportDefers(const fs::path& dir) {
    Rig rig(dir);
    rig.legacy->result = [](HeadTrackingConfig&) { return ImportResult::Absent({}); };
    rig.PutLegacy(kLegacyText);
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Deferred);
    ExpectReason(load.reason, RetriedReason("the old settings reader could not find the file"));
    ExpectLogLine(load.log, rig.LegacyText() + ": the old settings reader found no file, while the owner holds it open (" +
                                std::to_string(kLegacyText.size()) + " bytes); the ANSI path it was given is " +
                                rig.LegacyText());
    ExpectSunkOnce(rig, load.reason);
    ExpectNotImported(rig);
}

void AReadOnlyLegacyFileIsImportedAndLeftAsItWas(const fs::path& dir) {
    Rig rig(dir);
    rig.PutLegacy(kLegacyText);
    SetReadOnly(rig.legacy_path, true);
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Migrated);
    Check(Same(load.config, MigratedConfig()), "the session runs on the imported values");
    Check(rig.sink.empty(), "nothing is reported");
    Check((GetFileAttributesW(rig.legacy_path.c_str()) & FILE_ATTRIBUTE_READONLY) != 0, "the legacy file is still read-only");
    ExpectImported(rig);
}

// Denies the current user the given rights on a file or folder until destroyed.
class DenyAccess {
public:
    DenyAccess(const fs::path& path, DWORD rights) : path_(path.wstring()) {
        PACL old = nullptr;
        if (GetNamedSecurityInfoW(path_.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &old,
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
        deny.grfAccessPermissions = rights;
        deny.grfAccessMode = DENY_ACCESS;
        deny.grfInheritance = NO_INHERITANCE;
        deny.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        deny.Trustee.TrusteeType = TRUSTEE_IS_USER;
        deny.Trustee.ptstrName = static_cast<LPWSTR>(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid);
        PACL denied = nullptr;
        if (SetEntriesInAclW(1, &deny, old_, &denied) != ERROR_SUCCESS) throw std::runtime_error("SetEntriesInAclW");
        const DWORD set = SetNamedSecurityInfoW(&path_[0], SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                                denied, nullptr);
        LocalFree(denied);
        if (set != ERROR_SUCCESS) throw std::runtime_error("SetNamedSecurityInfoW");
    }
    DenyAccess(const DenyAccess&) = delete;
    DenyAccess& operator=(const DenyAccess&) = delete;
    ~DenyAccess() {
        SetNamedSecurityInfoW(&path_[0], SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, old_, nullptr);
        LocalFree(descriptor_);
    }

private:
    std::wstring path_;
    PSECURITY_DESCRIPTOR descriptor_ = nullptr;
    PACL old_ = nullptr;
};

void AFolderThatCannotBeWrittenDefers(const fs::path& dir) {
    Rig rig(dir);
    rig.PutLegacy(kLegacyText);
    std::optional<Load> load;
    {
        DenyAccess deny(dir, FILE_ADD_FILE);
        load = rig.Make()->Load();
    }
    ExpectStatus(*load, ConfigLoadStatus::Deferred);
    ExpectReason(load->reason, RetriedReason("the folder cannot be written"));
    Check(Same(load->config, MigratedConfig()), "the session runs on what the import gave");
    ExpectSunkOnce(rig, load->reason);
    ExpectNotImported(rig);

    ExpectStatus(rig.Make()->Load(), ConfigLoadStatus::Migrated);
    ExpectImported(rig);
}

// The read-only attribute never refuses a read, so a denied read is not blamed on it.
void AReadOnlyLegacyFileThatCannotBeReadCouldNotBeRead(const fs::path& dir) {
    Rig rig(dir);
    rig.PutLegacy(kLegacyText);
    SetReadOnly(rig.legacy_path, true);
    std::optional<Load> load;
    {
        DenyAccess deny(rig.legacy_path, FILE_READ_DATA);
        load = rig.Make()->Load();
    }
    ExpectStatus(*load, ConfigLoadStatus::Deferred);
    Check(Contains(load->reason, std::string(kLegacyNameText) + " was not imported into " + kFileNameText +
                                     ": it could not be read (Windows error 5"),
          "the reason says why: " + load->reason);
    Check(Same(load->config, Defaults()), "a legacy file that cannot be read cannot be imported, so the defaults");
    ExpectSunkOnce(rig, load->reason);
    ExpectNotImported(rig);

    ExpectStatus(rig.Make()->Load(), ConfigLoadStatus::Migrated);
    ExpectImported(rig);
}

void ALegacyFileHeldDenyingReadSharingDefers(const fs::path& dir) {
    Rig rig(dir);
    rig.PutLegacy(kLegacyText);
    const HANDLE exclusive = CreateFileW(rig.legacy_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                         FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(exclusive != INVALID_HANDLE_VALUE, "another program holds the legacy file with no sharing");
    const Load load = rig.Make()->Load();
    CloseHandle(exclusive);
    ExpectStatus(load, ConfigLoadStatus::Deferred);
    ExpectReason(load.reason, RetriedReason("the file is in use by another program"));
    Check(std::any_of(load.log.begin(), load.log.end(),
                      [&](const std::string& line) { return StartsWith(line, rig.LegacyText() + ": could not be opened: "); }),
          "the log names the legacy file that could not be opened" + Joined(load.log));
    Check(Same(load.config, Defaults()), "a legacy file that cannot be opened cannot be imported, so the defaults");
    Check(rig.legacy->Runs() == 0, "the import does not run");
    ExpectSunkOnce(rig, load.reason);
    ExpectNotImported(rig);

    ExpectStatus(rig.Make()->Load(), ConfigLoadStatus::Migrated);
    ExpectImported(rig);
}

void AConfigHeldDenyingReadSharingDefersAndNothingIsImported(const fs::path& dir) {
    Rig rig(dir);
    const std::string canonical = Render(Defaults());
    WriteBytes(rig.path, canonical);
    rig.PutLegacy(kLegacyText);
    const HANDLE exclusive = CreateFileW(rig.path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                         FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(exclusive != INVALID_HANDLE_VALUE, "another program holds the config with no sharing");
    const Load load = rig.Make()->Load();
    CloseHandle(exclusive);
    ExpectStatus(load, ConfigLoadStatus::Deferred);
    Check(Contains(load.reason, kFileNameText + " cannot be read: the file is in use by another program"s),
          "the reason says why: " + load.reason);
    Check(rig.legacy->Runs() == 0, "the legacy file is not imported in place of a config that cannot be read");
    ExpectSunkOnce(rig, load.reason);
    Check(HoldsBytes(rig.path, canonical), "the config is not written");
    ExpectLegacyKept(rig);
    Check(ListingIs(dir, {kFileName, kLegacyName}), "nothing else is written");
}

void AFilePendingDeletionDefers(const fs::path& dir) {
    Rig rig(dir);
    rig.PutLegacy(kLegacyText);
    WriteBytes(rig.path, Render(Defaults()));
    HANDLE doomed = CreateFileW(rig.path.c_str(), DELETE | GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    struct Release {
        HANDLE& handle;
        ~Release() {
            if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        }
    } release{doomed};
    FILE_DISPOSITION_INFO disposition{TRUE};
    Check(doomed != INVALID_HANDLE_VALUE &&
              SetFileInformationByHandle(doomed, FileDispositionInfo, &disposition, sizeof disposition),
          "another program deletes the config while it holds it open");
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    const bool refused = !GetFileAttributesExW(rig.path.c_str(), GetFileExInfoStandard, &attributes);
    Check(refused && GetLastError() == ERROR_ACCESS_DENIED, "GetFileAttributesExW refuses the file with access denied");

    const std::unique_ptr<Owner> owner = rig.Make();
    const Load load = owner->Load();
    ExpectStatus(load, ConfigLoadStatus::Deferred);
    Check(Contains(load.reason, kFileNameText + " cannot be read: it could not be read (Windows error 5"s),
          "the reason says why: " + load.reason);
    Check(Same(load.config, Defaults()), "the session runs on the defaults");
    Check(rig.legacy->Runs() == 0, "the legacy file is not imported in place of a config that cannot be read");
    ExpectSunkOnce(rig, load.reason);
    Check(!owner->FileChanged(), "FileChanged reads the write time the folder still lists");
    const Reload reload = owner->Reload();
    Check(reload.status == ConfigReloadStatus::Unreadable && !reload.config, "Reload is Unreadable: " + reload.reason);

    CloseHandle(doomed);
    doomed = INVALID_HANDLE_VALUE;
    Check(!fs::exists(rig.path), "the config is gone once the other program closes it");
    Check(owner->FileChanged(), "the file going away is a change");
    ExpectNotImported(rig);
}

void AnImportThatWritesTheFileDefers(const fs::path& dir) {
    Rig rig(dir);
    rig.PutLegacy(kLegacyText);
    rig.legacy->during = [&](const LegacyInput& input) { WriteBytes(input.path, kLegacyText + "Light = 2.0\r\n"); };
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Deferred);
    ExpectReason(load.reason, RetriedReason("the file was changed by another program while it was read"));
    ExpectSunkOnce(rig, load.reason);
    Check(HoldsBytes(rig.legacy_path, kLegacyText + "Light = 2.0\r\n"), "the other program's write is kept");
    Check(ListingIs(dir, {kLegacyName}), "nothing is created");
}

void AVerifyMismatchDefers(const fs::path& dir) {
    Rig rig(dir);
    rig.legacy->result = [](HeadTrackingConfig& config) {
        config.rotation_enabled = false;
        config.position_enabled = false;
        return ImportResult::Imported({});
    };
    rig.PutLegacy(kLegacyText);
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Deferred);
    ExpectReason(load.reason, RetriedReason("[General] RotationEnabled=false cannot be converted"));
    ExpectLogLine(load.log,
                  rig.LegacyText() + ": [General] RotationEnabled reads back from the new format as true, not false");
    Check(!load.config.rotation_enabled && !load.config.position_enabled, "the session runs on what the import gave");
    ExpectNotImported(rig);
}

void AValueNoCodecWritesDefers(const fs::path& dir) {
    Rig rig(dir);
    rig.PutLegacy(kLegacyText + "Light = 7.5\r\n");
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Deferred);
    ExpectReason(load.reason, RetriedReason("[Light] LightMultiplier=7.5 cannot be converted"));
    Check(load.config.light.multiplier == 7.5f, "the session runs on what the import gave");
    ExpectNotImported(rig);
}

// Another program creating the config file between the import and the commit, at each point of
// the create-if-absent write: before its first read, before its final check, and in the gap
// between the check and the rename. The next launch reads that file and never imports.
void AConfigAppearingBeforeTheCommitDefers(const fs::path& dir) {
    for (const std::string label : {"Commit.ReadTarget", "Commit.RecheckTarget", "Commit.Commit"}) {
        Rig rig(dir);
        rig.PutLegacy(kLegacyText);
        rig.hook = [&](const std::string& step, const std::wstring&) -> std::uint32_t {
            if (step == label) WriteBytes(rig.path, "theirs");
            return 0;
        };
        auto owner = rig.Make();
        const Load load = owner->Load();
        ExpectStatus(load, ConfigLoadStatus::Deferred);
        ExpectReason(load.reason, std::string(kLegacyNameText) + " was not imported into " + kFileNameText +
                                      ": another program created the file at the same time" + AppearedTail());
        ExpectLogLine(load.log, rig.Text() + ": not created: TargetAppeared");
        Check(Same(load.config, MigratedConfig()), label + ": the session runs on what the import gave");
        ExpectSunkOnce(rig, load.reason);
        ExpectNotSaved(owner->Save([](HeadTrackingConfig& c) { c.world_space_yaw = true; }),
                       "the settings file could not be used this session");
        Check(HoldsBytes(rig.path, "theirs"), label + ": the other program's file is kept");
        ExpectLegacyKept(rig, label);
        Check(ListingIs(dir, {kFileName, kLegacyName}), label + ": nothing else is left");

        rig.hook = nullptr;
        ExpectStatus(rig.Make()->Load(), ConfigLoadStatus::Canonical);
        Check(rig.legacy->Runs() == 1, label + ": the next launch reads the file that appeared and does not import");
        Check(HoldsBytes(rig.path, "theirs"), label + ": and writes nothing");
        ExpectLegacyKept(rig, label);
        fs::remove(rig.path);
    }
}

// A temporary that cannot be removed after a config appeared at the commit: the player is told
// the next launch reads that config, not that it tries the import again.
void AConfigAppearingWithATemporaryLeftBehindDefers(const fs::path& dir) {
    Rig rig(dir);
    rig.PutLegacy(kLegacyText);
    rig.hook = [&](const std::string& step, const std::wstring&) -> std::uint32_t {
        if (step == "Commit.RecheckTarget") WriteBytes(rig.path, "theirs");
        return step == "Commit.RemoveTemporary" ? ERROR_GEN_FAILURE : 0;
    };
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Deferred);
    Check(StartsWith(load.reason, std::string(kLegacyNameText) + " was not imported into " + kFileNameText +
                                      ": it could not be written (Windows error 31") &&
              EndsWith(load.reason, AppearedTail()),
          "the player is told the next launch reads the config: " + load.reason);
    ExpectSunkOnce(rig, load.reason);
    Check(HoldsBytes(rig.path, "theirs"), "the other program's file is kept");
    ExpectLegacyKept(rig);
    std::vector<fs::path> temporaries;
    for (const auto& entry : fs::directory_iterator(dir)) {
        const std::string name = Utf8(entry.path().filename().wstring());
        if (StartsWith(name, kFileNameText + "."s) && EndsWith(name, ".tmp")) temporaries.push_back(entry.path());
    }
    Check(temporaries.size() == 1, "one temporary is left, found " + std::to_string(temporaries.size()));
    for (const fs::path& temporary : temporaries) fs::remove(temporary);
    Check(ListingIs(dir, {kFileName, kLegacyName}), "nothing else is left");
}

// Design 4.5 step 1 (R3-2): while the owner holds the legacy file, the readers imports use read
// it, and nothing can newly open it denying read sharing, rename it or delete it.
void TheHeldFileReadsAndRefusesExclusiveOpens(const fs::path& dir) {
    Rig rig(dir);
    rig.PutLegacy(kLegacyText);
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
    ExpectImported(rig);
}

// Design 4.7, last case: a folder the ANSI code page cannot name. The published build was handed
// the ANSI path, found nothing and ran on its defaults, so those are what the import writes to
// CameraUnlock.ini. The legacy file is left as it was.
void APathOutsideTheAnsiCodePage(const fs::path& dir) {
    const wchar_t omega = 0x03A9;
    const fs::path folder = dir / std::wstring(1, omega);
    fs::create_directories(folder);
    Rig rig(folder);
    rig.PutLegacy(kLegacyText);
    const Load load = rig.Make()->Load();
    ExpectStatus(load, ConfigLoadStatus::Migrated);
    Check(rig.legacy->Runs() == 1 && rig.legacy->inputs[0].ansi_lossy, "the import is told the ANSI path is lossy");
    const std::string ansi = detail::OwnerAnsiForLog(rig.legacy->inputs[0].ansi_path);
    Check(Same(load.config, Defaults()), "the session runs on the defaults the published build ran on");
    Check(HoldsBytes(rig.path, Render(Defaults())), "the defaults are written to the config file");
    ExpectLegacyKept(rig);
    Check(ListingIs(folder, {kFileName, kLegacyName}), "nothing else is written");
    ExpectLogLine(load.log, rig.LegacyText() +
                                ": has a character the ANSI code page cannot hold, so a reader given its ANSI path, " +
                                ansi + ", finds no file there.");
    ExpectLogLine(load.log, rig.LegacyText() +
                                ": the old settings reader found no file, as the old build found none there and ran on its "
                                "defaults. Those defaults are written to " +
                                rig.Text() + ", and this file is left as it was.");
    Check(rig.sink.empty(), "nothing is reported");

    const Load next = rig.Make()->Load();
    ExpectStatus(next, ConfigLoadStatus::Canonical);
    Check(rig.legacy->Runs() == 1, "the next launch reads the config by its wide path, with no import");
    ExpectLogLine(next.log, SettingsReadLine(rig));

    // A per-key import has no Absent to report: it reads nothing and gives its defaults.
    fs::remove(rig.path);
    Rig per_key(folder);
    per_key.PutLegacy(kLegacyText);
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
    ExpectLegacyKept(per_key);
    Check(ListingIs(folder, {kFileName, kLegacyName}), "nothing else is written by a per-key import");
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
    rig.PutLegacy(kLegacyText);
    auto owner = rig.Make();
    ExpectStatus(owner->Load(), ConfigLoadStatus::Migrated);
    Check(!owner->FileChanged(), "the import's write is recorded");
    Check(owner->Reload().status == ConfigReloadStatus::Unchanged, "the import's bytes reload as Unchanged");

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
    ExpectLegacyKept(rig);
    Check(ListingIs(dir, {kFileName, kLegacyName}), "nothing else is written");
}

void ReloadReadsAnUnstampedConfigAndNeverImports(const fs::path& dir) {
    Rig rig(dir);
    rig.PutLegacy(kLegacyText);
    auto owner = rig.Make();
    ExpectStatus(owner->Load(), ConfigLoadStatus::Migrated);

    WriteBytes(rig.path, Replace(WithoutStamp(ReadBytes(rig.path)), "UdpPort=5555", "UdpPort=7000"));
    Reload reload = owner->Reload();
    Check(reload.status == ConfigReloadStatus::Applied && reload.config && reload.config->udp_port == 7000,
          std::string("an unstamped config is read as canonical, got ") + ConfigReloadStatusName(reload.status));
    Check(rig.legacy->Runs() == 1, "the reload does not import");

    fs::remove(rig.path);
    reload = owner->Reload();
    Check(reload.status == ConfigReloadStatus::Unreadable && !reload.config, "a deleted config keeps the settings");
    Check(Contains(reload.reason, kFileNameText + " is missing, so the current settings stay"s), "the reason says why");
    Check(rig.legacy->Runs() == 1, "the reload does not import the legacy file in place of a deleted config");
    ExpectSunkOnce(rig, reload.reason);
    ExpectNotImported(rig);
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
    Check(rig.legacy->Runs() == 0, "the config is never imported");
    Check(HoldsBytes(rig.path, utf16), "the file is not written");
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
    relative.path = L"C:CameraUnlock.ini";
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

    Legacy legacy;
    ConfigOwnerOptions<HeadTrackingConfig> import_without_source = options();
    import_without_source.import = legacy.Import();
    Check(Thrown<std::invalid_argument>([&] { Owner owner(import_without_source); }) ==
              "import is set, but no legacy_path names the file it reads",
          "an import with no legacy file is refused");
    ConfigOwnerOptions<HeadTrackingConfig> source_without_import = options();
    source_without_import.legacy_path = (dir / kLegacyName).wstring();
    Check(Thrown<std::invalid_argument>([&] { Owner owner(source_without_import); }) ==
              "legacy_path is set, but no import reads it",
          "a legacy file with no import is refused");
    ConfigOwnerOptions<HeadTrackingConfig> relative_source = options();
    relative_source.import = legacy.Import();
    relative_source.legacy_path = kLegacyName;
    Check(Thrown<std::invalid_argument>([&] { Owner owner(relative_source); }) ==
              "legacy_path 'HeadTracking.ini' is not a fully qualified path",
          "a relative legacy file is refused");
    ConfigOwnerOptions<HeadTrackingConfig> source_is_path = options();
    source_is_path.import = legacy.Import();
    source_is_path.legacy_path = (dir / L"CAMERAUNLOCK.INI").wstring();
    Check(Thrown<std::invalid_argument>([&] { Owner owner(source_is_path); }) == "legacy_path names the config file itself",
          "a legacy file that is the config, in another case, is refused");
    Check(legacy.Runs() == 0, "no refused owner imports");

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
              std::string(ConfigReloadStatusName(ConfigReloadStatus::Unreadable)) == "Unreadable",
          "status names are the C# spellings");
    Check(Thrown<std::invalid_argument>([] { ConfigReloadStatusName(static_cast<ConfigReloadStatus>(2)); }) ==
              "ConfigReloadStatus 2 has no name",
          "reload status 2 has no name");
}

// The steps of an import, as the owner's hook names them.
std::vector<std::string> InterruptionLabels() {
    const CheckedWriteStep writer[] = {
        CheckedWriteStep::ReadTarget,     CheckedWriteStep::CreateTemporary, CheckedWriteStep::WriteTemporary,
        CheckedWriteStep::FlushTemporary, CheckedWriteStep::CloseTemporary,  CheckedWriteStep::RecheckTarget,
        CheckedWriteStep::Commit,
    };
    std::vector<std::string> labels = {"Open", "Import", "Recheck"};
    for (CheckedWriteStep step : writer) labels.push_back(std::string("Commit.") + cameraunlock::CheckedWriteStepName(step));
    labels.push_back("Remember");
    return labels;
}

// The child is killed at the start of the labelled step. After it, the legacy file is whole and
// unwritten, the config file is absent before the commit and whole after it, beside them at most
// the writer's temporaries, and the next launch ends where an uninterrupted one does.
void KilledDuring(const std::string& label, const fs::path& dir) {
    Rig rig(dir);
    rig.PutLegacy(kLegacyText);

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

    ExpectLegacyKept(rig, "after the kill");
    const bool committed = label == "Remember";
    if (committed) {
        Check(HoldsBytes(rig.path, MigratedBytes()), "the config file is whole");
    } else {
        Check(!fs::exists(rig.path), "the config file is absent, since the child died before the commit");
    }
    const std::string temporary_prefix = kFileNameText + "."s;
    bool strays_are_temporaries = true;
    for (const std::wstring& wide : Listing(dir)) {
        if (wide == kFileName || wide == kLegacyName) continue;
        const std::string name = Utf8(wide);
        strays_are_temporaries = strays_are_temporaries && StartsWith(name, temporary_prefix) && EndsWith(name, ".tmp");
    }
    Check(strays_are_temporaries, "nothing but the writer's temporaries is beside them");

    const Load next = rig.Make()->Load();
    ExpectStatus(next, committed ? ConfigLoadStatus::Canonical : ConfigLoadStatus::Migrated);
    Check(HoldsBytes(rig.path, MigratedBytes()), "the next launch ends with the config file");
    ExpectLegacyKept(rig, "after the next launch");
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
    RunScenario("a-stamped-utf16-file-is-unreadable", AStampedUtf16FileIsUnreadable);
    RunScenario("a-stamped-file-holding-a-nul-is-unreadable", AStampedFileHoldingANulIsUnreadable);
    RunScenario("a-legacy-file-is-imported-and-left-as-it-was", ALegacyFileIsImportedAndLeftAsItWas);
    RunScenario("a-utf16-legacy-file-is-imported", AUtf16LegacyFileIsImported);
    RunScenario("a-legacy-file-holding-a-nul-is-imported", ALegacyFileHoldingANulIsImported);
    RunScenario("a-second-load-rewrites-nothing", ASecondLoadRewritesNothing);
    RunScenario("a-config-beside-a-legacy-file-is-read-and-the-import-never-runs",
                AConfigBesideALegacyFileIsReadAndTheImportNeverRuns);
    RunScenario("an-unstamped-config-beside-a-legacy-file-is-canonical-and-stamped-by-a-save",
                AnUnstampedConfigBesideALegacyFileIsCanonicalAndStampedByASave);
    RunScenario("an-unstamped-file-without-an-import-is-canonical-and-stamped-by-a-save",
                AnUnstampedFileWithoutAnImportIsCanonicalAndStampedByASave);
    RunScenario("an-unreadable-config-beside-a-legacy-file-is-unreadable-and-never-imported",
                AnUnreadableConfigBesideALegacyFileIsUnreadableAndNeverImported);
    RunScenario("a-dropped-value-is-logged", ADroppedValueIsLogged);
    RunScenario("deleting-the-config-imports-the-legacy-file-again", DeletingTheConfigImportsTheLegacyFileAgain);
    RunScenario("a-refused-import-is-legacy-refused", ARefusedImportIsLegacyRefused);
    RunScenario("an-undecodable-import-defers", AnUndecodableImportDefers);
    RunScenario("an-absent-import-defers", AnAbsentImportDefers);
    RunScenario("a-read-only-legacy-file-is-imported-and-left-as-it-was", AReadOnlyLegacyFileIsImportedAndLeftAsItWas);
    RunScenario("a-folder-that-cannot-be-written-defers", AFolderThatCannotBeWrittenDefers);
    RunScenario("a-read-only-legacy-file-that-cannot-be-read-could-not-be-read",
                AReadOnlyLegacyFileThatCannotBeReadCouldNotBeRead);
    RunScenario("a-legacy-file-held-denying-read-sharing-defers", ALegacyFileHeldDenyingReadSharingDefers);
    RunScenario("a-config-held-denying-read-sharing-defers-and-nothing-is-imported",
                AConfigHeldDenyingReadSharingDefersAndNothingIsImported);
    RunScenario("a-file-pending-deletion-defers", AFilePendingDeletionDefers);
    RunScenario("an-import-that-writes-the-file-defers", AnImportThatWritesTheFileDefers);
    RunScenario("a-verify-mismatch-defers", AVerifyMismatchDefers);
    RunScenario("a-value-no-codec-writes-defers", AValueNoCodecWritesDefers);
    RunScenario("a-config-appearing-before-the-commit-defers", AConfigAppearingBeforeTheCommitDefers);
    RunScenario("a-config-appearing-with-a-temporary-left-behind-defers", AConfigAppearingWithATemporaryLeftBehindDefers);
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
    RunScenario("a-save-to-a-read-only-file-is-not-saved", ASaveToAReadOnlyFileIsNotSaved);
    RunScenario("an-unfinished-save-is-uncertain", AnUnfinishedSaveIsUncertain);
    RunScenario("reload-ignores-the-owners-own-writes", ReloadIgnoresTheOwnersOwnWrites);
    RunScenario("reload-reads-an-unstamped-config-and-never-imports", ReloadReadsAnUnstampedConfigAndNeverImports);
    RunScenario("reload-of-an-unreadable-file-keeps-the-settings", ReloadOfAnUnreadableFileKeepsTheSettings);
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
