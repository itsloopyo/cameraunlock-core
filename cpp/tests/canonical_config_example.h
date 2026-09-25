#pragma once

// The config type of the C++ example docs/canonical-config.md shows. canonical_config_example_tests.cpp
// runs the example, and the Defaults.ini probe (defaults_ini_probe.cpp) loads the same table, so the
// file each creates is data/fixtures/canonical-ini/example/CameraUnlock.ini.

#include <cameraunlock/config/config_table.h>
#include <cameraunlock/config/head_tracking_config.h>

namespace canonical_config_example {

struct ModConfig : cameraunlock::HeadTrackingConfig {
    bool write_log = false;
};

cameraunlock::config::ConfigTable<ModConfig> ModConfigTable();

}  // namespace canonical_config_example
