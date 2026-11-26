// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
// ConfigWatcher against real files and real change notifications, written the way the launcher
// writes a game's file: a temporary beside it, renamed over it. Ported from Headstart's live edit
// test (native/common/tests/common_tests.cpp TestLiveEdits).

#include <iostream>
#include <string>

#ifdef _WIN32

#include <cameraunlock/config/config_watcher.h>
#include <cameraunlock/config/head_tracking_config_table.h>

#include <windows.h>

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <random>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
using cameraunlock::HeadTrackingConfig;
using namespace cameraunlock::config;
using schema::Concept;
using Watcher = ConfigWatcher<HeadTrackingConfig>;

int g_failures = 0;

void Check(bool cond, const std::string& name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

template <class E>
std::string Thrown(const std::function<void()>& body) {
    try {
        body();
    } catch (const E& e) {
        return e.what();
    }
    return "";
}

bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
}

// Renames `tmp` over `path` as the launcher's write_checked does: a rename refused because the
// file is held open for a moment (a scanner looking at the file just renamed into place) is tried
// again for up to a second.
void RenameLikeLauncher(const fs::path& tmp, const fs::path& path) {
    for (int waited = 0;; waited += 10) {
        if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return;
        const DWORD error = GetLastError();
        if ((error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION) || waited >= 1000) {
            Check(false, "MoveFileExW failed with Windows error " + std::to_string(error));
            return;
        }
        Sleep(10);
    }
}

void WriteLikeLauncher(const fs::path& path, const std::string& text) {
    const fs::path tmp = path.wstring() + L".tmp-test";
    WriteBytes(tmp, text);
    RenameLikeLauncher(tmp, path);
}

FILETIME LastWriteTime(const fs::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    Check(GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) != 0, "the file's write time reads");
    return data.ftLastWriteTime;
}

// WriteLikeLauncher with the write landing in the same clock tick as the one before it: Windows
// stamps both with one time, so only the file ID, change time and size say the file changed.
void WriteLikeLauncherSameTick(const fs::path& path, const std::string& text) {
    const FILETIME before = LastWriteTime(path);
    const fs::path tmp = path.wstring() + L".tmp-test";
    WriteBytes(tmp, text);
    const HANDLE h = CreateFileW(tmp.c_str(), FILE_WRITE_ATTRIBUTES, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(h != INVALID_HANDLE_VALUE, "the temporary opens to set its write time");
    Check(SetFileTime(h, nullptr, nullptr, &before) != 0, "the temporary takes the file's write time");
    CloseHandle(h);
    RenameLikeLauncher(tmp, path);
    const FILETIME after = LastWriteTime(path);
    Check(CompareFileTime(&before, &after) == 0, "the same-tick write leaves the write time as it was");
}

const std::string kHead = "[CameraUnlock]\r\nConfigFormat=1\r\n[Network]\r\nUdpPort=6100\r\n";

std::string GameFile(const char* light) { return kHead + "[Light]\r\nLightMultiplier=" + light + "\r\n"; }

struct Rig {
    fs::path games;
    fs::path fleet;
    fs::path game;
    fs::path defaults;
    std::mutex lock;
    std::vector<std::string> lines;

    explicit Rig(const fs::path& dir)
        : games(dir / L"games"), fleet(dir / L"CameraUnlock"), game(games / L"Game.ini"), defaults(fleet / L"Defaults.ini") {
        fs::create_directories(games);
        fs::create_directories(fleet);
    }

    ConfigOwnerOptions<HeadTrackingConfig> Options() const {
        ConfigOwnerOptions<HeadTrackingConfig> options;
        options.path = game.wstring();
        options.table = HeadTrackingConfigTable({Concept::UdpPort, Concept::CollisionEnabled, Concept::LightMultiplier});
        options.header = RenderHeader{"Test Game"};
        options.defaults = DefaultsFile::At(defaults.wstring());
        return options;
    }

    Watcher::LogFn Log() {
        return [this](const std::string& line) {
            std::lock_guard<std::mutex> held(lock);
            lines.push_back(line);
        };
    }

    bool Logged(const std::string& part) {
        std::lock_guard<std::mutex> held(lock);
        for (const std::string& line : lines) {
            if (Contains(line, part)) return true;
        }
        return false;
    }
};

void Stamps(const fs::path& dir) {
    std::cout << "ReadConfigFileStamp:\n";
    const ConfigFileStamp nofolder = ReadConfigFileStamp((dir / L"absent" / L"Game.ini").wstring());
    Check(!nofolder.present && nofolder.error == 0, "a file in a missing folder is not present, and no error");
    const fs::path file = dir / L"Game.ini";
    const ConfigFileStamp nofile = ReadConfigFileStamp(file.wstring());
    Check(!nofile.present && nofile.error == 0, "a missing file is not present, and no error");
    WriteBytes(file, "abc");
    const ConfigFileStamp first = ReadConfigFileStamp(file.wstring());
    Check(first.present && first.size == 3 && first.id != 0 && first.write_time != 0, "a file is present with its size, ID and time");
    Check(ReadConfigFileStamp(file.wstring()) == first, "an unchanged file reads the same stamp");
    // Matched without case, as Windows names files.
    Check(ReadConfigFileStamp((dir / L"GAME.INI").wstring()) == first, "the name is matched without case");
    WriteLikeLauncherSameTick(file, "abcd");
    const ConfigFileStamp same_tick = ReadConfigFileStamp(file.wstring());
    Check(same_tick.write_time == first.write_time && same_tick != first,
          "a same-tick rename keeps the write time and still changes the stamp");
    Check(Contains(Thrown<std::invalid_argument>([] { ReadConfigFileStamp(L"Game.ini"); }), "has no folder"),
          "a path with no folder throws std::invalid_argument");
    Check(ConfigFolderOf(L"C:\\a\\b.ini") == L"C:\\a" && ConfigFolderOf(L"C:/a/b.ini") == L"C:/a",
          "ConfigFolderOf cuts at the last slash of either kind");
}

void Polled(const fs::path& dir) {
    std::cout << "ConfigWatcher, polled:\n";
    Rig rig(dir);
    Check(Contains(Thrown<std::invalid_argument>([&] { Watcher(rig.Options(), nullptr); }), "needs a log"),
          "a watcher with no log throws std::invalid_argument");
    WriteBytes(rig.defaults, "[Light]\r\nLightMultiplier=1.5\r\n");
    WriteBytes(rig.game, kHead);
    Watcher watcher(rig.Options(), rig.Log());
    Check(!Thrown<std::logic_error>([&] { watcher.Changed(); }).empty(), "Changed before Load throws std::logic_error");
    Check(!Thrown<std::logic_error>([&] { watcher.Start([](ConfigReloadResult<HeadTrackingConfig>) {}); }).empty(),
          "Start before Load throws std::logic_error");
    const ConfigLoadResult<HeadTrackingConfig> loaded = watcher.Load();
    Check(loaded.config.udp_port == 6100 && Near(loaded.config.light.multiplier, 1.5f), "Load reads the file and Defaults.ini");
    Check(!watcher.Changed(), "nothing changed after Load");
    const std::vector<std::wstring> folders = watcher.Folders();
    Check(folders.size() == 2 && folders[0] == rig.games.wstring() && folders[1] == rig.fleet.wstring(),
          "Folders names the file's folder, then Defaults.ini's");

    WriteLikeLauncher(rig.game, GameFile("2.0"));
    Check(watcher.Changed(), "an edit to the file is a change");
    ConfigReloadResult<HeadTrackingConfig> read = watcher.ReloadSettled();
    Check(read.status == ConfigReloadStatus::Applied && read.config && Near(read.config->light.multiplier, 2.0f),
          "ReloadSettled applies the edit");
    Check(!watcher.Changed(), "after the read nothing has changed");

    WriteLikeLauncherSameTick(rig.game, GameFile("2.7"));
    Check(!watcher.Owner().FileChanged(), "the owner's write time alone misses a same-tick write");
    Check(watcher.Changed(), "the watcher's stamps catch it");
    read = watcher.ReloadSettled();
    Check(read.status == ConfigReloadStatus::Applied && read.config && Near(read.config->light.multiplier, 2.7f),
          "and the same-tick value is read");

    WriteLikeLauncherSameTick(rig.defaults, "[Light]\r\nLightMultiplier=0\r\n[Position]\r\nCollisionEnabled=false\r\n");
    Check(watcher.Changed(), "a same-tick write to Defaults.ini is a change");
    read = watcher.ReloadSettled();
    Check(read.status == ConfigReloadStatus::Applied && read.config && !read.config->collision_enabled &&
              Near(read.config->light.multiplier, 2.7f),
          "a row the file leaves alone follows Defaults.ini, and the file's own row stays");
    Check(!watcher.Changed(), "settled again");
}

void OneFolder(const fs::path& dir) {
    std::cout << "ConfigWatcher, Defaults.ini beside the file:\n";
    Rig rig(dir);
    ConfigOwnerOptions<HeadTrackingConfig> options = rig.Options();
    options.defaults = DefaultsFile::At((rig.games / L"Defaults.ini").wstring());
    WriteBytes(rig.games / L"Defaults.ini", "[Light]\r\nLightMultiplier=1.5\r\n");
    WriteBytes(rig.game, kHead);
    Watcher watcher(std::move(options), rig.Log());
    watcher.Load();
    Check(watcher.Folders() == std::vector<std::wstring>{rig.games.wstring()}, "one folder holding both is watched once");
}

void Threaded(const fs::path& dir) {
    std::cout << "ConfigWatcher, on its thread:\n";
    Rig rig(dir);
    WriteBytes(rig.defaults, "[Light]\r\nLightMultiplier=1.5\r\n");
    WriteBytes(rig.game, kHead);
    // Before the watcher, so its thread never outlives what it writes to.
    std::mutex lock;
    std::condition_variable woke;
    int reads = 0;
    std::optional<HeadTrackingConfig> last;
    Watcher watcher(rig.Options(), rig.Log());
    watcher.Load();

    watcher.Start([&](ConfigReloadResult<HeadTrackingConfig> read) {
        if (read.status != ConfigReloadStatus::Applied) return;
        std::lock_guard<std::mutex> held(lock);
        last = *read.config;
        ++reads;
        woke.notify_all();
    });
    Check(watcher.Running(), "Running once started");
    Check(!Thrown<std::logic_error>([&] { watcher.Start([](ConfigReloadResult<HeadTrackingConfig>) {}); }).empty(),
          "a second Start throws std::logic_error");
    Check(rig.Logged("watching for edits, applied as they land"), "the start is logged");
    // The bound only keeps a lost edit from hanging the run, so it is far past any machine load.
    const auto waitFor = [&](int count) {
        std::unique_lock<std::mutex> held(lock);
        return woke.wait_for(held, std::chrono::seconds(20), [&] { return reads > count; });
    };
    const auto count = [&] {
        std::lock_guard<std::mutex> held(lock);
        return reads;
    };
    const auto light = [&] {
        std::lock_guard<std::mutex> held(lock);
        return last ? last->light.multiplier : -1.0f;
    };

    WriteLikeLauncher(rig.games / L"Other.ini", kHead);
    Sleep(200);
    Check(count() == 0, "another file in the folder applies nothing");

    for (int step = 0; step < 3; ++step) {
        const char* values[] = {"1.6", "2.0", "2.4"};
        const int before = count();
        WriteLikeLauncher(rig.game, GameFile(values[step]));
        Check(waitFor(before) && Near(light(), std::stof(values[step])), std::string("an edit to ") + values[step] + " is read");
    }
    {
        const int before = count();
        WriteLikeLauncherSameTick(rig.game, GameFile("2.7"));
        Check(waitFor(before) && Near(light(), 2.7f), "a same-tick edit to the file is read");
    }
    {
        const int before = count();
        WriteLikeLauncherSameTick(rig.defaults, "[Light]\r\nLightMultiplier=0\r\n[Position]\r\nCollisionEnabled=false\r\n");
        Check(waitFor(before), "a same-tick edit to Defaults.ini, in its own folder, is read");
        std::lock_guard<std::mutex> held(lock);
        Check(last && !last->collision_enabled, "and applied");
    }
    watcher.Stop();
    Check(!watcher.Running(), "not Running once stopped");
    Check(!Thrown<std::logic_error>([&] { watcher.Stop(); }).empty(), "a second Stop throws std::logic_error");
    const int stopped = count();
    WriteLikeLauncher(rig.game, GameFile("3.0"));
    Sleep(200);
    Check(count() == stopped, "nothing is read once stopped");
}

void ReadThrows(const fs::path& dir) {
    std::cout << "ConfigWatcher, a read that throws:\n";
    Rig rig(dir);
    WriteBytes(rig.defaults, "[Light]\r\nLightMultiplier=1.5\r\n");
    WriteBytes(rig.game, kHead);
    Watcher watcher(rig.Options(), rig.Log());
    watcher.Load();
    watcher.Start([](ConfigReloadResult<HeadTrackingConfig>) { throw std::runtime_error("boom"); });
    WriteLikeLauncher(rig.game, GameFile("2.0"));
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!rig.Logged("watching for edits stopped: applying an edit threw: boom") && std::chrono::steady_clock::now() < until) {
        Sleep(10);
    }
    Check(rig.Logged("watching for edits stopped: applying an edit threw: boom"), "the thread stops, logged with the error");
    watcher.Stop();
}

void Scenario(const std::function<void(const fs::path&)>& body) {
    std::random_device random;
    const fs::path dir =
        fs::temp_directory_path() / (L"cu-config-watcher-" + std::to_wstring(random()) + std::to_wstring(random()));
    fs::create_directories(dir);
    try {
        body(dir);
    } catch (const std::exception& e) {
        Check(false, std::string("threw ") + e.what());
    }
    std::error_code error;
    fs::remove_all(dir, error);
    if (error) std::cout << "  (the scratch folder stays: " << error.message() << ")\n";
}

}  // namespace

int RunConfigWatcherTests() {
    std::cout << "\nConfigWatcher:\n";
    g_failures = 0;
    Scenario(Stamps);
    Scenario(Polled);
    Scenario(OneFolder);
    Scenario(Threaded);
    Scenario(ReadThrows);
    return g_failures;
}

#else

int RunConfigWatcherTests() { return 0; }

#endif  // _WIN32
