// Compile-only header-hygiene check: every self-contained public header in one TU,
// at the strictest warning level the fleet's own mods use, so a problem that only
// appears when two headers MEET is caught here.
//
// It exists because quat4.h declared function-local kDegToRad / kRadToDeg that
// shadowed angle_utils.h's namespace-scope constants of the same name. Neither header
// is wrong on its own; together they are MSVC C4459, and the mods that compile with
// /W4 /WX turned that into error C2220 and a dead build. Nothing in this repo included
// both headers in one TU, so nothing here saw it - it surfaced only after the fleet had
// already pulled the change.
//
// Adding a public header means adding it here. Excluded, and why:
//   cameraunlock/reframework/camera_chain.h        external dep: reframework/API.hpp
//   cameraunlock/reframework/camera_controller_hook.h external dep: reframework/API.hpp
//   cameraunlock/reframework/camera_pipeline.h     external dep: via camera_chain.h
//   cameraunlock/reframework/game_state_probing.h  external dep: reframework/API.hpp
//   cameraunlock/reframework/gameplay_gate.h       external dep: reframework/API.hpp
//   cameraunlock/reframework/gui_elements.h        external dep: reframework/API.hpp
//   cameraunlock/reframework/managed_utils.h       external dep: reframework/API.hpp
//   cameraunlock/reframework/plugin_bootstrap.h    external dep: reframework/API.hpp
//   cameraunlock/reframework/tdb_inspector.h       external dep: reframework/API.hpp
//   cameraunlock/rendering/aim_marker.h            overlay: covered by cameraunlock_overlay_compile
//   cameraunlock/rendering/aim_marker_dx11.h       overlay: covered by cameraunlock_overlay_compile
//   cameraunlock/rendering/aim_marker_dx12.h       overlay: covered by cameraunlock_overlay_compile
//   cameraunlock/rendering/dx11_overlay.h          overlay: covered by cameraunlock_overlay_compile
//   cameraunlock/rendering/dx12_overlay.h          overlay: covered by cameraunlock_overlay_compile
//   cameraunlock/rendering/dx9_overlay.h           overlay: covered by cameraunlock_overlay_compile
//
// This target's include path is the public include/ directory alone, so the
// REFramework plugin SDK is not reachable from it. That is what "external dep"
// above means, and it is why a reframework/ header that reaches API.hpp - at
// any depth - is excluded while the three that do not are compiled below.

#ifdef _WIN32
#include <windows.h>
#endif

#include "cameraunlock/ads/ads_blend.h"
#include "cameraunlock/ads/ads_fade.h"
#include "cameraunlock/ads/ads_mode.h"
#include "cameraunlock/ads/entry_pose.h"
#include "cameraunlock/camera/lean_clamp.h"
#include "cameraunlock/config/config_key_schema.g.h"
#include "cameraunlock/config/head_tracking_config.h"
#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/config/value_guards.h"
#include "cameraunlock/data/position_data.h"
#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/data/tracking_pose.h"
#include "cameraunlock/diagnostics/crash_handler.h"
#include "cameraunlock/discovery/camera_discovery.h"
#include "cameraunlock/discovery/float_classifier.h"
#include "cameraunlock/discovery/probe_selection.h"
#include "cameraunlock/effects/head_follow_light.h"
#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/input/chord_hotkeys.h"
#include "cameraunlock/input/deferred_actions.h"
#include "cameraunlock/input/hotkey_poller.h"
#include "cameraunlock/logging/file_log.h"
#include "cameraunlock/math/angle_utils.h"
#include "cameraunlock/math/deadzone_utils.h"
#include "cameraunlock/math/finite_utils.h"
#include "cameraunlock/math/quat4.h"
#include "cameraunlock/math/rotation_utils.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/math/vec3.h"
#include "cameraunlock/memory/pattern_scanner.h"
#include "cameraunlock/memory/pe_fingerprint.h"
#include "cameraunlock/memory/rtti_vtable.h"
#ifdef _WIN32
// SEH-only, so it #errors rather than degrading on a non-Windows build.
#include "cameraunlock/memory/safe_memory.h"
#endif
#include "cameraunlock/os/game_window.h"
#include "cameraunlock/os/module_paths.h"
#include "cameraunlock/processing/center_offset_manager.h"
#include "cameraunlock/processing/pose_interpolator.h"
#include "cameraunlock/processing/position_interpolator.h"
#include "cameraunlock/processing/position_processor.h"
#include "cameraunlock/processing/tracking_processor.h"
#include "cameraunlock/protocol/opentrack_packet.h"
#include "cameraunlock/protocol/polling_udp_receiver.h"
#include "cameraunlock/protocol/port_utils.h"
#include "cameraunlock/protocol/socket_types.h"
#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/protocol/udp_socket.h"
#include "cameraunlock/reframework/game_window.h"
#include "cameraunlock/reframework/log_callback.h"
#include "cameraunlock/reframework/manager_probe_checks.h"
#include "cameraunlock/reframework/plugin_config.h"
#include "cameraunlock/reframework/plugin_mod.h"
#include "cameraunlock/reframework/re_math.h"
#include "cameraunlock/rendering/aim_ndc_projection.h"
#include "cameraunlock/rendering/aim_quat_projection.h"
#include "cameraunlock/rendering/crosshair_projection.h"
#include "cameraunlock/rendering/gui_marker_compensation.h"
#include "cameraunlock/rendering/overlay_draw_list.h"
#include "cameraunlock/rendering/world_reprojection.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/time/qpc_clock.h"
#include "cameraunlock/tracking/head_tracking_session.h"
#include "cameraunlock/unreal/ue_math.h"
#include "cameraunlock/unreal/ue_runtime.h"
