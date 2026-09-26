#pragma once

#include <cameraunlock/config/config_table.h>
#include <cameraunlock/config/head_tracking_config.h>

#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace cameraunlock::config {

namespace detail {

inline const char* HeadTrackingConceptName(schema::Concept id) {
    const auto index = static_cast<std::size_t>(id);
    if (index >= schema::kConceptCount) {
        throw std::invalid_argument("concept " + std::to_string(static_cast<int>(id)) +
                                    " is not a canonical concept");
    }
    return schema::kConcepts[index].name;
}

template <schema::Concept Id, class Config, class Field>
void BindHeadTrackingMember(ConfigTable<Config>& table, Field HeadTrackingConfig::*field) {
    table.template Concept<Id>([field](const Config& c) { return c.*field; },
                               [field](Config& c, Field value) { c.*field = std::move(value); });
}

template <class Config>
void BindHeadTrackingConcept(ConfigTable<Config>& table, schema::Concept id) {
    using C = schema::Concept;
    using H = HeadTrackingConfig;
    switch (id) {
        case C::UdpPort:
            BindHeadTrackingMember<C::UdpPort>(table, &H::udp_port);
            return;
        case C::EnableOnStartup:
            BindHeadTrackingMember<C::EnableOnStartup>(table, &H::enable_on_startup);
            return;
        case C::LocalSmoothing:
            table.template Concept<C::LocalSmoothing>([](const Config& c) { return c.local_smoothing; },
                                                      [](Config& c, float v) {
                                                          c.local_smoothing = v;
                                                          c.position.local_smoothing = v;
                                                      });
            return;
        case C::RemoteSmoothing:
            table.template Concept<C::RemoteSmoothing>([](const Config& c) { return c.remote_smoothing; },
                                                       [](Config& c, float v) {
                                                           c.remote_smoothing = v;
                                                           c.position.remote_smoothing = v;
                                                       });
            return;
        case C::WorldSpaceYaw:
            BindHeadTrackingMember<C::WorldSpaceYaw>(table, &H::world_space_yaw);
            return;
        case C::AimDecoupling:
            BindHeadTrackingMember<C::AimDecoupling>(table, &H::aim_decoupling_enabled);
            return;
        case C::RotationEnabled:
            BindHeadTrackingMember<C::RotationEnabled>(table, &H::rotation_enabled);
            return;
        case C::DataFreshnessMs:
            BindHeadTrackingMember<C::DataFreshnessMs>(table, &H::data_freshness_ms);
            return;
        case C::PositionEnabled:
            BindHeadTrackingMember<C::PositionEnabled>(table, &H::position_enabled);
            return;
        case C::PositionAllowed:
            BindHeadTrackingMember<C::PositionAllowed>(table, &H::position_allowed);
            return;
        case C::TrueFreeLook:
            BindHeadTrackingMember<C::TrueFreeLook>(table, &H::true_free_look);
            return;
        case C::PositionLimitX:
            table.template Concept<C::PositionLimitX>([](const Config& c) { return c.position.limit_x; },
                                                      [](Config& c, float v) { c.position.limit_x = v; });
            return;
        case C::PositionLimitY:
            table.template Concept<C::PositionLimitY>([](const Config& c) { return c.position.limit_y; },
                                                      [](Config& c, float v) { c.position.limit_y = v; });
            return;
        case C::PositionLimitYDown:
            table.template Concept<C::PositionLimitYDown>([](const Config& c) { return c.position.limit_y_down; },
                                                          [](Config& c, float v) { c.position.limit_y_down = v; });
            return;
        case C::PositionLimitZ:
            table.template Concept<C::PositionLimitZ>([](const Config& c) { return c.position.limit_z; },
                                                      [](Config& c, float v) { c.position.limit_z = v; });
            return;
        case C::PositionLimitZBack:
            table.template Concept<C::PositionLimitZBack>([](const Config& c) { return c.position.limit_z_back; },
                                                          [](Config& c, float v) { c.position.limit_z_back = v; });
            return;
        case C::CollisionEnabled:
            BindHeadTrackingMember<C::CollisionEnabled>(table, &H::collision_enabled);
            return;
        case C::CollisionMargin:
            table.template Concept<C::CollisionMargin>([](const Config& c) { return c.lean_clamp.skin; },
                                                       [](Config& c, float v) { c.lean_clamp.skin = v; });
            return;
        case C::CollisionChannel:
            BindHeadTrackingMember<C::CollisionChannel>(table, &H::collision_channel);
            table.Engine();
            return;
        case C::CollisionReleaseSmoothing:
            table.template Concept<C::CollisionReleaseSmoothing>(
                [](const Config& c) { return c.lean_clamp.release_smoothing; },
                [](Config& c, float v) { c.lean_clamp.release_smoothing = v; });
            return;
        case C::TrackerPivotForward:
            BindHeadTrackingMember<C::TrackerPivotForward>(table, &H::tracker_pivot_forward);
            return;
        case C::TrackerPivotUp:
            BindHeadTrackingMember<C::TrackerPivotUp>(table, &H::tracker_pivot_up);
            return;
        case C::ToggleKey:
            BindHeadTrackingMember<C::ToggleKey>(table, &H::toggle_key_name);
            return;
        case C::CycleTrackingModeKey:
            BindHeadTrackingMember<C::CycleTrackingModeKey>(table, &H::cycle_tracking_mode_key_name);
            return;
        case C::YawModeKey:
            BindHeadTrackingMember<C::YawModeKey>(table, &H::yaw_mode_key_name);
            return;
        case C::TrueFreeLookKey:
            BindHeadTrackingMember<C::TrueFreeLookKey>(table, &H::true_free_look_key_name);
            return;
        case C::LightFollowsHead:
            table.template Concept<C::LightFollowsHead>([](const Config& c) { return c.light.follows_head; },
                                                        [](Config& c, bool v) { c.light.follows_head = v; });
            return;
        case C::LightMultiplier:
            table.template Concept<C::LightMultiplier>([](const Config& c) { return c.light.multiplier; },
                                                       [](Config& c, float v) { c.light.multiplier = v; });
            return;
    }
    // Reached by a concept the schema gained after this switch was written.
    throw std::logic_error(std::string("HeadTrackingConfigTable has no binding for ") + HeadTrackingConceptName(id));
}

}  // namespace detail

/// Core's config table over HeadTrackingConfig, or over a Config derived from it that adds the
/// game's own fields for its local rows. The caller names the concepts its game implements, core
/// binds each, and the game appends its local rows and modifiers. Naming them keeps a file to what
/// the game binds (a game with no carried light has no [Light]), and a concept core adds later
/// reaches a game's file only once the game names it.
///
/// LocalSmoothing and RemoteSmoothing write the top-level field and its copy in `position`.
/// PositionLimitY never sets PositionLimitYDown: each key is read on its own. CollisionMargin and
/// CollisionReleaseSmoothing live in `lean_clamp`, LightFollowsHead and LightMultiplier in
/// `light`. CollisionMargin and CollisionChannel are not global in the schema, so their rows hold
/// the game's own default and never follow Defaults.ini. CollisionChannel is also an Engine row: the
/// channel is data about the game, so at its default it is written as a comment.
/// The sensitivity and inversion fields, and those in
/// `position`, have no row: the canonical format carries no pose shaping, so they keep the
/// defaults instance's values.
///
/// The defaults instance is Config{} with the four hotkey lists and CollisionEnabled at the schema's
/// canonical_default (`End, Ctrl+Shift+Y`, `PageUp, Ctrl+Shift+G`, `PageDown, Ctrl+Shift+H`,
/// `Insert, Ctrl+Shift+U`, true); HeadTrackingConfig's own field initialisers, which the flat reader
/// uses, stay single keys and false.
///
/// Throws std::invalid_argument for an empty list, a concept named twice, or a value that is not a
/// canonical concept.
template <class Config = HeadTrackingConfig>
ConfigTable<Config> HeadTrackingConfigTable(std::initializer_list<schema::Concept> implemented) {
    static_assert(std::is_base_of_v<HeadTrackingConfig, Config>,
                  "HeadTrackingConfigTable binds HeadTrackingConfig fields, so Config must derive from it");
    if (implemented.size() == 0) {
        throw std::invalid_argument("HeadTrackingConfigTable needs the concepts the game implements");
    }
    for (auto it = implemented.begin(); it != implemented.end(); ++it) {
        const char* name = detail::HeadTrackingConceptName(*it);
        for (auto earlier = implemented.begin(); earlier != it; ++earlier) {
            if (*earlier == *it) {
                throw std::invalid_argument(std::string("HeadTrackingConfigTable names ") + name + " twice");
            }
        }
    }

    Config defaults{};
    defaults.toggle_key_name = schema::ConceptTraits<schema::Concept::ToggleKey>::kCanonicalDefault;
    defaults.cycle_tracking_mode_key_name =
        schema::ConceptTraits<schema::Concept::CycleTrackingModeKey>::kCanonicalDefault;
    defaults.yaw_mode_key_name = schema::ConceptTraits<schema::Concept::YawModeKey>::kCanonicalDefault;
    defaults.true_free_look_key_name = schema::ConceptTraits<schema::Concept::TrueFreeLookKey>::kCanonicalDefault;
    defaults.collision_enabled =
        std::string_view(schema::ConceptTraits<schema::Concept::CollisionEnabled>::kCanonicalDefault) == "true";

    ConfigTable<Config> table(std::move(defaults));
    for (schema::Concept id : implemented) detail::BindHeadTrackingConcept(table, id);
    return table;
}

}  // namespace cameraunlock::config
