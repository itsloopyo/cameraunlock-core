#pragma once

// The config type of the C++ example docs/canonical-config.md shows. Its table renders
// data/fixtures/canonical-ini/example/CameraUnlock.ini byte for byte.

#include <cameraunlock/config/config_table.h>
#include <cameraunlock/config/head_tracking_config.h>

namespace canonical_config_example {

struct ModConfig : cameraunlock::HeadTrackingConfig {
    bool write_log = false;
};

cameraunlock::config::ConfigTable<ModConfig> ModConfigTable();

}  // namespace canonical_config_example
