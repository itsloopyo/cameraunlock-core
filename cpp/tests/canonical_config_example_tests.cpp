// The C++ example docs/canonical-config.md shows, run for real. scripts/check-doc-examples.mjs
// holds the document's C++ blocks to this file and canonical_config_example.h line for line, so an
// example that stops compiling or behaving as the document says fails here. The table renders
// data/fixtures/canonical-ini/example/CameraUnlock.ini, as the C# CanonicalConfigExample's does.

#include "canonical_config_example.h"

#include <cameraunlock/config/config_owner.h>
#include <cameraunlock/config/head_tracking_config_table.h>
#include <cameraunlock/input/key_bindings.h>

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <random>
#include <utility>
#endif

namespace canonical_config_example {

using namespace cameraunlock::config;
using schema::Concept;

ConfigTable<ModConfig> ModConfigTable() {
    ConfigTable<ModConfig> table = HeadTrackingConfigTable<ModConfig>(
        {Concept::UdpPort, Concept::EnableOnStartup, Concept::WorldSpaceYaw, Concept::RotationEnabled,
         Concept::PositionEnabled, Concept::ToggleKey, Concept::CycleTrackingModeKey, Concept::YawModeKey});
    table.Select(Concept::WorldSpaceYaw).Writable()
        .Select(Concept::RotationEnabled).Writable()
        .Select(Concept::PositionEnabled).Writable();
    table.Local("Logging", "WriteLog", &ModConfig::write_log, BoolCodec(),
                "true: write HeadTracking.log beside the game's executable.");
    return table;
}

}  // namespace canonical_config_example

namespace {

namespace fs = std::filesystem;
using namespace cameraunlock::config;
using canonical_config_example::ModConfig;
using canonical_config_example::ModConfigTable;

int g_failures = 0;

void Check(bool cond, const std::string& name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) throw std::runtime_error("cannot open " + path.string());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string Expected() {
    return ReadBytes(fs::path(CAMERAUNLOCK_CANONICAL_INI_FIXTURES) / "example" / "CameraUnlock.ini");
}

void TheTableRendersTheExampleFile() {
    Check(RenderCanonicalFresh(ModConfigTable(), RenderHeader{"Example Game"}) == Expected(),
          "the fresh render is example/CameraUnlock.ini");
}

#ifdef _WIN32

void TheOwnerCreatesSavesAndReadsTheFile(const fs::path& dir) {
    ConfigOwnerOptions<ModConfig> options;
    options.path = (dir / L"CameraUnlock.ini").wstring();
    options.table = ModConfigTable();
    options.header.display_name = "Example Game";
    options.defaults = DefaultsFile::At((dir / L"global" / L"Defaults.ini").wstring());
    ConfigOwner<ModConfig> owner(std::move(options));

    ConfigLoadResult<ModConfig> loaded = owner.Load();
    const ModConfig& config = loaded.config;
    cameraunlock::input::KeyBindingsParseResult toggle = cameraunlock::input::ParseKeyBindings(config.toggle_key_name);

    ConfigSaveResult saved = owner.Save([](ModConfig& c) { c.world_space_yaw = false; });

    Check(loaded.status == ConfigLoadStatus::Created, "the first Load creates the file");
    Check(toggle.ok() && toggle.bindings.size() == 2, "ToggleKey reads as two bindings");
    Check(saved.status == ConfigSaveStatus::Saved, "the yaw toggle saves");

    std::string after = Expected();
    after.replace(after.find("WorldSpaceYaw=default"), 21, "WorldSpaceYaw=false");
    Check(ReadBytes(dir / "CameraUnlock.ini") == after, "the save changed the WorldSpaceYaw line and nothing else");

    ConfigOwnerOptions<ModConfig> again;
    again.path = (dir / L"CameraUnlock.ini").wstring();
    again.table = ModConfigTable();
    again.header.display_name = "Example Game";
    again.defaults = DefaultsFile::At((dir / L"global" / L"Defaults.ini").wstring());
    ConfigOwner<ModConfig> next(std::move(again));
    ConfigLoadResult<ModConfig> reread = next.Load();
    Check(reread.status == ConfigLoadStatus::Canonical && !reread.config.world_space_yaw && !reread.config.write_log,
          "the next launch reads the saved value");
}

void RunInScratch() {
    std::random_device random;
    const fs::path dir =
        fs::temp_directory_path() / (L"cu-config-example-" + std::to_wstring(random()) + std::to_wstring(random()));
    fs::create_directories(dir);
    try {
        TheOwnerCreatesSavesAndReadsTheFile(dir);
    } catch (const std::exception& e) {
        Check(false, std::string("the owner example threw ") + e.what());
    }
    fs::remove_all(dir);
}

#endif  // _WIN32

}  // namespace

int RunCanonicalConfigExampleTests() {
    std::cout << "\nCanonical config example (docs/canonical-config.md)\n";
    try {
        TheTableRendersTheExampleFile();
    } catch (const std::exception& e) {
        Check(false, std::string("the table example threw ") + e.what());
    }
#ifdef _WIN32
    RunInScratch();
#endif
    return g_failures;
}
