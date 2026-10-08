#include "cameraunlock/c/cameraunlock.h"
#ifdef CAMERAUNLOCK_C_TESTING
#include "cameraunlock/c/testing/cameraunlock_testing.h"
#endif

#include "cameraunlock/ads/ads_fade.h"
#include "cameraunlock/ads/aim_mode.h"
#include "cameraunlock/ads/lean_handover.h"
#include "cameraunlock/camera/lean_clamp.h"
#include "cameraunlock/camera/zoom_compensation.h"
#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/head_tracking_config_table.h"
#include "cameraunlock/config/hotkey_codec.h"
#include "cameraunlock/effects/head_follow_light.h"
#include "cameraunlock/input/hotkey_poller.h"
#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/logging/file_log.h"
#include "cameraunlock/os/game_window.h"
#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/tracking/head_tracking_session.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace cameraunlock;
namespace cfg = cameraunlock::config;

static_assert(sizeof(CameraUnlockSettings) == 60, "docs/c-interface.md states this layout");
static_assert(sizeof(CameraUnlockFrameInput) == 80 && offsetof(CameraUnlockFrameInput, now_ms) == 8 &&
                  offsetof(CameraUnlockFrameInput, aim_forward) == 32,
              "docs/c-interface.md states this layout");
static_assert(sizeof(CameraUnlockFrame) == 92, "docs/c-interface.md states this layout");
static_assert(sizeof(CameraUnlockObstruction) == 16, "docs/c-interface.md states this layout");
static_assert(sizeof(CameraUnlockLean) == 40, "docs/c-interface.md states this layout");
static_assert(sizeof(CameraUnlockConfig) == 76, "docs/c-interface.md states this layout");

// Everything below lives in objects that are never destroyed. A host's process can end with the
// receiver's and the poller's threads running, and a destructor that joined them would run
// while the C runtime is being torn down.

// ---- Errors and the log -----------------------------------------------------------------------

thread_local std::string t_last_error;

struct LogState {
    std::mutex mutex;
    std::string pending;
    bool file = false;
};

LogState& TheLog() {
    static LogState* state = new LogState();
    return *state;
}

void LogLine(const std::string& line) {
    LogState& log = TheLog();
    const std::lock_guard<std::mutex> lock(log.mutex);
    if (log.file) {
        logging::Text(line);
    } else {
        log.pending.append(line).push_back('\n');
    }
}

void LogLines(const std::vector<std::string>& lines) {
    for (const std::string& line : lines) LogLine(line);
}

template <class Body>
std::int32_t Guarded(const char* what, Body&& body) noexcept {
    std::string reason;
    try {
        return body();
    } catch (const std::exception& failure) {
        reason = failure.what();
    } catch (...) {
        reason = "something that is not a std::exception was thrown";
    }
    try {
        // A call made every frame that fails every frame would write a line a frame. The reason
        // is always there to be asked for, and the log has it once a second.
        thread_local std::chrono::steady_clock::time_point logged_at;
        const std::string failure = std::string(what) + ": " + reason;
        const auto now = std::chrono::steady_clock::now();
        if (failure != t_last_error || now - logged_at >= std::chrono::seconds(1)) {
            logged_at = now;
            LogLine(failure);
        }
        t_last_error = failure;
    } catch (...) {
        // Out of memory while recording the reason. Nothing may leave this function but the status.
    }
    return CAMERAUNLOCK_ERROR;
}

void Require(bool held, const char* otherwise) {
    if (!held) throw std::invalid_argument(otherwise);
}

template <class Struct>
void RequireStruct(const Struct* given, const char* name) {
    if (given == nullptr) throw std::invalid_argument(std::string(name) + " is NULL");
    if (given->struct_size != sizeof(Struct)) {
        throw std::invalid_argument(std::string(name) + " is " + std::to_string(sizeof(Struct)) +
                                    " bytes in this library and its struct_size says " +
                                    std::to_string(given->struct_size) +
                                    ": the host and the library are from different builds");
    }
}

std::wstring Wide(const char* utf8, const char* name) {
    if (utf8 == nullptr) throw std::invalid_argument(std::string(name) + " is NULL");
    const int length = static_cast<int>(std::strlen(utf8));
    if (length == 0) return std::wstring();
    const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, length, nullptr, 0);
    if (needed <= 0) throw std::invalid_argument(std::string(name) + " is not UTF-8");
    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, length, wide.data(), needed);
    return wide;
}

std::int32_t CopyOut(const std::string& text, char* buffer, std::int32_t capacity, bool with_nul) {
    const std::int32_t length = static_cast<std::int32_t>(text.size());
    if (buffer != nullptr && capacity >= length + (with_nul ? 1 : 0)) {
        std::memcpy(buffer, text.data(), text.size());
        if (with_nul) buffer[length] = '\0';
    }
    return length;
}

bool Finite(const float* values, int count) {
    for (int i = 0; i < count; ++i) {
        if (!std::isfinite(values[i])) return false;
    }
    return true;
}

// ---- The session ------------------------------------------------------------------------------

CameraUnlockSettings SettingsOf(const HeadTrackingConfig& config) {
    const std::optional<TrackingMode> mode = DecodeTrackingMode(config.rotation_enabled, config.position_enabled);
    if (!mode) throw std::invalid_argument("RotationEnabled and PositionEnabled are both false, which is no tracking mode");

    CameraUnlockSettings settings = {};
    settings.struct_size = sizeof(settings);
    settings.tracking_mode = static_cast<std::int32_t>(*mode);
    settings.aim_mode = static_cast<std::int32_t>(
        ads::DecodeAimMode(config.true_free_look, config.free_look_marker, config.stock_sights));
    settings.data_freshness_ms = config.data_freshness_ms;
    settings.collision_enabled = config.collision_enabled ? 1 : 0;
    settings.local_smoothing = config.local_smoothing;
    settings.remote_smoothing = config.remote_smoothing;
    settings.limit_x = config.position.limit_x;
    settings.limit_y = config.position.limit_y;
    settings.limit_y_down = config.position.limit_y_down;
    settings.limit_z = config.position.limit_z;
    settings.limit_z_back = config.position.limit_z_back;
    settings.collision_margin = config.lean_clamp.skin;
    settings.collision_release_smoothing = config.lean_clamp.release_smoothing;
    settings.light_multiplier = config.light.multiplier;
    return settings;
}

struct PendingLean {
    math::Vec3 lean;
    math::Vec3 aim;
    bool query = false;
    bool aiming = false;
    bool rig_available = false;
    bool free_look = false;
    float delta_seconds = 0.0f;
    float forward_stop = 0.0f;
    std::uint64_t now_ms = 0;
};

// How a line names a view. View 0 is the session and goes unnamed, so a host with one view reads
// the lines it always has.
std::string Named(std::int32_t view) {
    return view == 0 ? std::string() : " (view " + std::to_string(view) + ")";
}

// What is one tracker's and one camera's.
struct View {
    std::mutex mutex;
    UdpReceiver receiver;
    HeadTrackingSession<UdpReceiver> tracking{receiver};
    // Written under Session::starts and the view's own mutex, so either one reads them.
    bool started = false;
    std::int32_t port = 0;
    CameraUnlockSettings settings = SettingsOf(HeadTrackingConfig{});
    camera::LeanClamp clamp;
    ads::LeanHandover handover;
    ads::AdsFade stock_fade;
    time::FrameClock clock{std::numeric_limits<float>::max()};
    std::optional<PendingLean> pending;
};

struct Session {
    // Held through a start and a stop, taken before the view's own.
    std::mutex starts;
    // Held through a configure, a frame and a lean, taken before the view's own and never with
    // `starts`: a frame runs on the settings and the tracking mode of one configure, whole, in
    // every view.
    std::mutex settings;
    View views[CAMERAUNLOCK_VIEWS];
    std::atomic<std::int32_t> aim_mode{CAMERAUNLOCK_AIM_SIGHTS_LOCKED};

    Session() {
        views[0].receiver.SetLog(&LogLine);
        for (std::int32_t view = 1; view < CAMERAUNLOCK_VIEWS; ++view) {
            views[view].receiver.SetLog([view](const std::string& line) { LogLine(line + Named(view)); });
        }
    }

    // The tracking mode is every view's. View 0's pipeline keeps it, where core's cycle is, and a
    // frame of another view takes it from there.
    HeadTrackingSession<UdpReceiver>& mode() { return views[0].tracking; }
};

Session*& SessionSlot() {
    static Session* session = new Session();
    return session;
}

Session& TheSession() {
    return *SessionSlot();
}

View& ViewAt(Session& session, std::int32_t view) {
    if (view < 0 || view >= CAMERAUNLOCK_VIEWS) {
        throw std::invalid_argument("there is no view " + std::to_string(view) + ": the views are 0 to " +
                                    std::to_string(CAMERAUNLOCK_VIEWS - 1));
    }
    return session.views[view];
}

void RequireSettings(const CameraUnlockSettings& s) {
    Require(s.tracking_mode >= 0 && s.tracking_mode <= 2, "tracking_mode is not a CAMERAUNLOCK_TRACKING_*");
    Require(s.aim_mode >= 0 && s.aim_mode <= 3, "aim_mode is not a CAMERAUNLOCK_AIM_*");
    Require(s.data_freshness_ms >= 1, "data_freshness_ms is below 1");
    Require(s.local_smoothing >= 0.0f && s.local_smoothing <= 1.0f, "local_smoothing is outside 0 to 1");
    Require(s.remote_smoothing >= 0.0f && s.remote_smoothing <= 1.0f, "remote_smoothing is outside 0 to 1");
    // A negative limit turns the clamp's bounds round and pins the view at a fixed offset.
    const float limits[] = {s.limit_x, s.limit_y, s.limit_y_down, s.limit_z, s.limit_z_back, s.collision_margin};
    for (const float limit : limits) {
        Require(std::isfinite(limit) && limit >= 0.0f, "a position limit or the collision margin is negative or not a number");
    }
    Require(s.collision_release_smoothing >= 0.0f && s.collision_release_smoothing <= 1.0f,
            "collision_release_smoothing is outside 0 to 1");
    Require(s.light_multiplier >= 0.0f && s.light_multiplier <= effects::kMaxLightMultiplier,
            "light_multiplier is outside 0 to 5");
}

bool StopLean(View& s) {
    s.clamp.Reset();
    return s.handover.Stop();
}

void Rest(View& s, CameraUnlockFrame& out) {
    s.tracking.ResetTransientState();
    s.stock_fade.Reset();
    if (StopLean(s)) out.flags |= CAMERAUNLOCK_STATE_RELEASE_RIG;
}

std::int64_t SteadyMicros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

camera::LeanObstruction HandBack(void* context, const math::Vec3&, const math::Vec3&, float) {
    return *static_cast<const camera::LeanObstruction*>(context);
}

// ---- A view's start, stop, frame and lean -----------------------------------------------------

std::int32_t StartView(std::int32_t view, std::int32_t udp_port) {
    Session& session = TheSession();
    View& s = ViewAt(session, view);
    Require(udp_port >= 1 && udp_port <= 65535, "udp_port is outside 1 to 65535");
    const std::lock_guard<std::mutex> starts(session.starts);
    const std::lock_guard<std::mutex> lock(s.mutex);
    if (s.started) {
        throw std::logic_error(view == 0 ? "the session is already started: cameraunlock_session_stop first"
                                         : "view " + std::to_string(view) + " is already started: cameraunlock_view_stop first");
    }
    for (std::int32_t other = 0; other < CAMERAUNLOCK_VIEWS; ++other) {
        if (other != view && session.views[other].started && session.views[other].port == udp_port) {
            throw std::invalid_argument("view " + std::to_string(view) + " cannot listen on UDP port " +
                                        std::to_string(udp_port) + ", which view " + std::to_string(other) +
                                        " listens on: a port carries one tracker");
        }
    }
    bool bound = false;
    try {
        bound = s.receiver.Start(static_cast<std::uint16_t>(udp_port));
    } catch (...) {
        // A start that threw part way leaves the view not started and its port free to another.
        s.receiver.Stop();
        throw;
    }
    s.started = true;
    s.port = udp_port;
    // The receiver says why a bind failed and when a later one held. This is the line for
    // the bind that held at once, so a log always says which of the two happened.
    if (bound) LogLine("Listening for OpenTrack datagrams on UDP port " + std::to_string(udp_port) + Named(view));
    return CAMERAUNLOCK_OK;
}

std::int32_t StopView(std::int32_t view) {
    Session& session = TheSession();
    View& s = ViewAt(session, view);
    const std::lock_guard<std::mutex> starts(session.starts);
    const std::lock_guard<std::mutex> lock(s.mutex);
    s.receiver.Stop();
    s.started = false;
    s.pending.reset();
    s.tracking.ResetTransientState();
    s.stock_fade.Reset();
    StopLean(s);
    return CAMERAUNLOCK_OK;
}

std::int32_t FrameOf(std::int32_t view, const CameraUnlockFrameInput* input, CameraUnlockFrame* out) {
    Session& session = TheSession();
    View& s = ViewAt(session, view);
    RequireStruct(input, "CameraUnlockFrameInput");
    RequireStruct(out, "CameraUnlockFrame");
    const std::lock_guard<std::mutex> settings(session.settings);
    const std::lock_guard<std::mutex> lock(s.mutex);

    *out = {};
    out->struct_size = sizeof(*out);
    out->pose_share = 1.0f;
    out->zoom_factor = 1.0f;

    if (s.pending) {
        s.pending.reset();
        StopLean(s);
        throw std::logic_error(
            view == 0 ? "the frame before this one asked for cameraunlock_session_lean, which was not called"
                      : "the frame of view " + std::to_string(view) +
                            " before this one asked for cameraunlock_view_lean, which was not called");
    }

    float delta = input->delta_seconds;
    Require(std::isfinite(delta) && delta >= 0.0f, "delta_seconds is negative or not a number");
    std::uint64_t now_ms = input->now_ms;
    if ((input->flags & CAMERAUNLOCK_FRAME_CLOCK) != 0) {
        const float measured = s.clock.Tick();
        delta = measured < delta ? measured : delta;
        now_ms = static_cast<std::uint64_t>(SteadyMicros() / 1000);
    }
    out->delta_seconds = delta;

    const TrackingMode mode = session.mode().GetMode();
    // Not on view 0, which keeps the mode: a cycle on another thread between the two would be undone.
    if (view != 0) s.tracking.SetMode(mode);
    const ads::AimMode aim_mode = static_cast<ads::AimMode>(session.aim_mode.load());
    out->tracking_mode = static_cast<std::int32_t>(mode);
    out->aim_mode = static_cast<std::int32_t>(aim_mode);

    const std::int64_t received = s.receiver.GetLastReceiveTimestamp();
    if (s.receiver.IsRunning()) out->flags |= CAMERAUNLOCK_STATE_LISTENING;
    if (s.receiver.IsRemoteConnection()) out->flags |= CAMERAUNLOCK_STATE_REMOTE;
    if (received != 0 && (SteadyMicros() - received) / 1000 < s.settings.data_freshness_ms) {
        out->flags |= CAMERAUNLOCK_STATE_FRESH;
    }
    {
        LogState& log = TheLog();
        const std::lock_guard<std::mutex> log_lock(log.mutex);
        if (!log.pending.empty()) out->flags |= CAMERAUNLOCK_STATE_LOG;
    }

    if ((input->flags & CAMERAUNLOCK_FRAME_ACTIVE) == 0) {
        Rest(s, *out);
        return CAMERAUNLOCK_OK;
    }
    Require(std::isfinite(input->tan_half_fov) && input->tan_half_fov > 0.0f &&
                std::isfinite(input->tan_half_fov_base) && input->tan_half_fov_base > 0.0f,
            "tan_half_fov and tan_half_fov_base are not both positive numbers");

    if (!s.tracking.Update(delta)) {
        Rest(s, *out);
        return CAMERAUNLOCK_OK;
    }
    out->flags |= CAMERAUNLOCK_STATE_POSE;

    const bool aiming = (input->flags & CAMERAUNLOCK_FRAME_AIMING) != 0;
    const float share = s.stock_fade.Update(ads::StockSightsEngaged(aim_mode, aiming), now_ms);
    const float zoom = camera::FovZoomFactor(input->tan_half_fov, input->tan_half_fov_base);
    out->pose_share = share;
    out->zoom_factor = zoom;

    float yaw, pitch, roll;
    if (mode != TrackingMode::PositionOnly && s.tracking.GetRotation(yaw, pitch, roll)) {
        out->flags |= CAMERAUNLOCK_STATE_ROTATION;
        out->head_yaw = yaw;
        out->head_pitch = pitch;
        out->head_roll = roll;
        out->yaw = camera::ScaleAngleForZoom(yaw * share, zoom);
        out->pitch = camera::ScaleAngleForZoom(pitch * share, zoom);
        out->roll = roll;
        const effects::HeadEuler light =
            effects::ScaleHeadEuler({out->yaw, out->pitch, out->roll}, s.settings.light_multiplier);
        out->light_yaw = light.yaw;
        out->light_pitch = light.pitch;
        out->light_roll = light.roll;
    }

    float x, y, z;
    const bool has_position = s.tracking.GetPositionOffset(x, y, z);
    if (has_position) {
        out->head_x = x;
        out->head_y = y;
        out->head_z = z;
    }
    if (!has_position || (input->flags & CAMERAUNLOCK_FRAME_LEAN) == 0) {
        if (StopLean(s)) out->flags |= CAMERAUNLOCK_STATE_RELEASE_RIG;
        return CAMERAUNLOCK_OK;
    }

    const float* m = input->tracker_to_world;
    const math::Vec3 aim(input->aim_forward[0], input->aim_forward[1], input->aim_forward[2]);
    Require(Finite(m, 9) && Finite(input->aim_forward, 3), "tracker_to_world or aim_forward holds something that is not a number");
    Require(std::fabs(aim.Magnitude() - 1.0f) < 1e-3f, "aim_forward is not unit length");
    Require(!std::isnan(input->forward_stop), "forward_stop is not a number");

    const math::Vec3 head(x * share, y * share, z * share);
    const math::Vec3 lean = camera::ScaleLeanForZoom(
        math::Vec3(m[0] * head.x + m[1] * head.y + m[2] * head.z, m[3] * head.x + m[4] * head.y + m[5] * head.z,
                   m[6] * head.x + m[7] * head.y + m[8] * head.z),
        aim, zoom);
    const float desired = lean.Magnitude();

    PendingLean pending;
    pending.lean = lean;
    pending.aim = aim;
    pending.query = s.settings.collision_enabled != 0 && desired > camera::LeanClamp::kMinimumLean;
    pending.aiming = aiming;
    pending.rig_available = (input->flags & CAMERAUNLOCK_FRAME_RIG_AVAILABLE) != 0;
    pending.free_look = ads::IsFreeLook(aim_mode);
    pending.delta_seconds = delta;
    pending.forward_stop = input->forward_stop;
    pending.now_ms = now_ms;
    if (pending.query) {
        out->flags |= CAMERAUNLOCK_STATE_LEAN_QUERY;
        out->query_direction[0] = lean.x / desired;
        out->query_direction[1] = lean.y / desired;
        out->query_direction[2] = lean.z / desired;
        out->query_reach = desired + s.clamp.Settings().skin;
    }
    s.pending = pending;
    out->flags |= CAMERAUNLOCK_STATE_LEAN;
    return CAMERAUNLOCK_OK;
}

std::int32_t LeanOf(std::int32_t view, const CameraUnlockObstruction* obstruction, CameraUnlockLean* out) {
    Session& session = TheSession();
    View& s = ViewAt(session, view);
    RequireStruct(out, "CameraUnlockLean");
    const std::lock_guard<std::mutex> settings(session.settings);
    const std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.pending) {
        throw std::logic_error("no lean is waiting" + (view == 0 ? std::string() : " for view " + std::to_string(view)) +
                               ": it follows a frame whose flags carry CAMERAUNLOCK_STATE_LEAN, once");
    }
    const PendingLean pending = *s.pending;
    s.pending.reset();

    math::Vec3 lean = pending.lean;
    if (s.settings.collision_enabled == 0) {
        s.clamp.Reset();
    } else {
        camera::LeanObstruction hit;
        if (pending.query) {
            RequireStruct(obstruction, "CameraUnlockObstruction");
            hit.queried = obstruction->queried != 0;
            hit.blocked = obstruction->blocked != 0;
            hit.distance = obstruction->distance;
            Require(!hit.queried || !hit.blocked || (std::isfinite(hit.distance) && hit.distance >= 0.0f),
                    "the obstruction's distance is negative or not a number");
        }
        lean = s.clamp.Apply(math::Vec3(), pending.lean, pending.delta_seconds, &HandBack, &hit);
    }

    s.handover.SetForwardStop(pending.forward_stop);
    const ads::LeanShares shares = s.handover.Update(lean, pending.aim, pending.aiming, pending.free_look,
                                                     pending.rig_available, pending.now_ms);

    *out = {};
    out->struct_size = sizeof(*out);
    if (s.clamp.InContact()) out->flags |= CAMERAUNLOCK_LEAN_CONTACT;
    if (s.clamp.LastQueryFailed()) out->flags |= CAMERAUNLOCK_LEAN_QUERY_FAILED;
    out->camera[0] = shares.camera.x;
    out->camera[1] = shares.camera.y;
    out->camera[2] = shares.camera.z;
    out->rig[0] = shares.rig.x;
    out->rig[1] = shares.rig.y;
    out->rig[2] = shares.rig.z;
    out->asked = pending.lean.Magnitude();
    out->given = lean.Magnitude();
    return CAMERAUNLOCK_OK;
}

// ---- The config -------------------------------------------------------------------------------

enum class HostEnum : std::int32_t {};
using LocalValue = std::variant<bool, std::int32_t, float, HostEnum, std::string>;

struct HostConfig : HeadTrackingConfig {
    std::vector<LocalValue> locals;
};

enum class LocalKind { Bool, Int, Float, Enum, Hotkey };

struct LocalRow {
    LocalKind kind;
    std::string section;
    std::string key;
    std::string comment;
    std::uint32_t flags = 0;
    LocalValue start;
    std::int32_t int_min = 0;
    std::int32_t int_max = 0;
    float float_min = 0.0f;
    float float_max = 0.0f;
    std::vector<std::string> tokens;
    std::int32_t hotkey_bit = 0;
    // What a key of the row answers once held, or 0 for a row whose keys answer as they go down.
    std::int32_t held_bit = 0;
};

struct ConceptRow {
    cfg::schema::Concept id;
    std::uint32_t flags = 0;
    std::optional<std::string> comment;
    std::optional<std::string> start;
};

struct ConfigState {
    std::mutex mutex;
    std::optional<std::string> display_name;
    std::vector<ConceptRow> concepts;
    std::vector<LocalRow> locals;
    std::unique_ptr<cfg::ConfigOwner<HostConfig>> owner;
    HostConfig loaded;
    // Written under the mutex, and read without it by cameraunlock_hotkeys_drop.
    std::atomic<input::HotkeyPoller*> poller{nullptr};
    std::atomic<std::int32_t> pressed{0};
};

ConfigState& TheConfig() {
    static ConfigState* state = new ConfigState();
    return *state;
}

void RequireDescribing(const ConfigState& c) {
    if (!c.display_name) throw std::logic_error("cameraunlock_config_describe has not run");
    if (c.owner) throw std::logic_error("the file is loaded, and its rows are settled");
}

void RequireLoaded(const ConfigState& c) {
    if (!c.owner) throw std::logic_error("cameraunlock_config_load has not run");
}

template <class Value>
auto Getter(std::size_t row) {
    return [row](const HostConfig& c) { return std::get<Value>(c.locals[row]); };
}

template <class Value>
auto Setter(std::size_t row) {
    return [row](HostConfig& c, Value value) { c.locals[row] = std::move(value); };
}

cfg::ConfigTable<HostConfig> BuildTable(const ConfigState& c) {
    if (!c.display_name) throw std::logic_error("cameraunlock_config_describe has not run");

    HostConfig defaults;
    for (const LocalRow& row : c.locals) defaults.locals.push_back(row.start);
    std::vector<cfg::schema::Concept> ids;
    for (const ConceptRow& row : c.concepts) ids.push_back(row.id);
    cfg::ConfigTable<HostConfig> table = cfg::HeadTrackingConfigTableFrom<HostConfig>(ids, std::move(defaults));

    for (const ConceptRow& row : c.concepts) {
        table.Select(row.id);
        if (row.comment) table.Comment(row.comment->c_str());
        if ((row.flags & CAMERAUNLOCK_ROW_PER_GAME) != 0) {
            if (row.start) {
                table.PerGame(*row.start);
            } else {
                table.PerGame();
            }
        } else if (row.start) {
            table.Default(*row.start);
        }
        if ((row.flags & CAMERAUNLOCK_ROW_WRITABLE) != 0) table.Writable();
    }

    for (std::size_t i = 0; i < c.locals.size(); ++i) {
        const LocalRow& row = c.locals[i];
        const char* section = row.section.c_str();
        const char* key = row.key.c_str();
        const char* comment = row.comment.c_str();
        switch (row.kind) {
            case LocalKind::Bool:
                table.Local(section, key, Getter<bool>(i), Setter<bool>(i), cfg::BoolCodec(), comment);
                break;
            case LocalKind::Int:
                table.Local(section, key, Getter<std::int32_t>(i), Setter<std::int32_t>(i),
                            cfg::IntCodec<std::int32_t>(row.int_min, row.int_max), comment);
                break;
            case LocalKind::Float:
                table.Local(section, key, Getter<float>(i), Setter<float>(i),
                            cfg::FloatCodec(row.float_min, row.float_max), comment);
                break;
            case LocalKind::Enum: {
                std::vector<cfg::EnumToken<HostEnum>> tokens;
                for (std::size_t t = 0; t < row.tokens.size(); ++t) {
                    tokens.push_back({row.tokens[t], static_cast<HostEnum>(t)});
                }
                table.Local(section, key, Getter<HostEnum>(i), Setter<HostEnum>(i),
                            cfg::EnumCodec<HostEnum>(std::move(tokens)), comment);
                break;
            }
            case LocalKind::Hotkey:
                table.Local(section, key, Getter<std::string>(i), Setter<std::string>(i), cfg::HotkeyCodec(), comment);
                break;
        }
        if ((row.flags & CAMERAUNLOCK_ROW_WRITABLE) != 0) table.Writable();
    }
    return table;
}

// Under the config's mutex.
std::int32_t AddLocal(ConfigState& c, const char* section, const char* key, const char* comment, std::uint32_t flags,
                      LocalRow row) {
    RequireDescribing(c);
    Require(section != nullptr && key != nullptr && comment != nullptr, "a local row needs a section, a key and a comment");
    Require((flags & ~(CAMERAUNLOCK_ROW_WRITABLE | CAMERAUNLOCK_ROW_LIVE)) == 0,
            "a local row takes CAMERAUNLOCK_ROW_WRITABLE and CAMERAUNLOCK_ROW_LIVE only");
    row.section = section;
    row.key = key;
    row.comment = comment;
    row.flags = flags;
    c.locals.push_back(std::move(row));
    try {
        BuildTable(c);
    } catch (...) {
        c.locals.pop_back();
        throw;
    }
    return static_cast<std::int32_t>(c.locals.size() - 1);
}

const LocalRow& LocalAt(const ConfigState& c, std::int32_t row) {
    if (row < 0 || static_cast<std::size_t>(row) >= c.locals.size()) {
        throw std::invalid_argument("there is no local row " + std::to_string(row));
    }
    return c.locals[static_cast<std::size_t>(row)];
}

std::int32_t Save(ConfigState& c, const std::function<void(HostConfig&)>& change) {
    RequireLoaded(c);
    const cfg::ConfigSaveResult saved = c.owner->Save(change);
    LogLines(saved.log);
    if (saved.status != cfg::ConfigSaveStatus::Saved) LogLine(saved.reason);
    return static_cast<std::int32_t>(saved.status);
}

// Under the config's mutex. `held_bit` is 0 for a row of cameraunlock_config_local_hotkey.
std::int32_t AddHotkeyRow(ConfigState& c, const char* key, const char* comment, std::uint32_t flags,
                          const char* default_keys, std::int32_t hotkey_bit, std::int32_t held_bit) {
    for (const LocalRow& other : c.locals) {
        if (other.hotkey_bit == hotkey_bit || other.held_bit == hotkey_bit) {
            throw std::invalid_argument("hotkey_bit is another row's");
        }
        if (held_bit != 0 && (other.hotkey_bit == held_bit || other.held_bit == held_bit)) {
            throw std::invalid_argument("held_bit is another row's");
        }
    }
    const cfg::CodecParseResult<std::string> parsed = cfg::HotkeyCodec().Parse(default_keys);
    if (!parsed.ok()) throw std::invalid_argument(std::string(default_keys) + ": " + parsed.error);
    LocalRow row{LocalKind::Hotkey};
    row.start = parsed.value;
    row.hotkey_bit = hotkey_bit;
    row.held_bit = held_bit;
    return AddLocal(c, "Hotkeys", key, comment, flags, std::move(row));
}

// `held_bit` is 0 for a list whose keys answer `bit` as they go down.
void Register(ConfigState& c, const std::string& row, const std::string& keys, std::int32_t bit,
              std::int32_t held_bit = 0) {
    const input::KeyBindingsParseResult parsed = input::ParseKeyBindings(keys);
    if (!parsed.ok()) throw std::runtime_error(row + "=" + keys + ": " + parsed.error);
    std::atomic<std::int32_t>* pressed = &c.pressed;
    input::HotkeyPoller& poller = *c.poller.load();
    if (held_bit == 0) {
        input::RegisterKeyBindings(poller, parsed.bindings, [pressed, bit] { pressed->fetch_or(bit); });
    } else {
        input::RegisterHoldKeyBindings(
            poller, parsed.bindings, CAMERAUNLOCK_HOLD_MS, [pressed, bit] { pressed->fetch_or(bit); },
            [pressed, held_bit] { pressed->fetch_or(held_bit); });
    }
}

}  // namespace

// ---- Exports ----------------------------------------------------------------------------------

std::int32_t cameraunlock_abi(void) {
    return CAMERAUNLOCK_ABI;
}

std::int32_t cameraunlock_struct_size(std::int32_t which) {
    return Guarded("cameraunlock_struct_size", [&] {
        constexpr std::size_t sizes[] = {sizeof(CameraUnlockSettings), sizeof(CameraUnlockFrameInput),
                                         sizeof(CameraUnlockFrame),    sizeof(CameraUnlockObstruction),
                                         sizeof(CameraUnlockLean),     sizeof(CameraUnlockConfig)};
        Require(which >= 0 && which < static_cast<std::int32_t>(std::size(sizes)), "which is not a CAMERAUNLOCK_STRUCT_*");
        return static_cast<std::int32_t>(sizes[which]);
    });
}

std::int32_t cameraunlock_last_error(char* buffer, std::int32_t capacity) {
    return CopyOut(t_last_error, buffer, capacity, true);
}

std::int32_t cameraunlock_log_open(const char* path) {
    return Guarded("cameraunlock_log_open", [&] {
        const std::wstring file = Wide(path, "path");
        LogState& log = TheLog();
        const std::lock_guard<std::mutex> lock(log.mutex);
        if (log.file) throw std::logic_error("the log is already open");
        logging::Open(file);
        if (!logging::IsOpen()) {
            throw std::runtime_error(std::string(path) + " could not be created or written to");
        }
        log.file = true;
        std::size_t start = 0;
        while (start < log.pending.size()) {
            const std::size_t end = log.pending.find('\n', start);
            logging::Text(log.pending.substr(start, end - start));
            start = end + 1;
        }
        log.pending.clear();
        return CAMERAUNLOCK_OK;
    });
}

std::int32_t cameraunlock_log_write(const char* line) {
    return Guarded("cameraunlock_log_write", [&] {
        Require(line != nullptr, "line is NULL");
        LogState& log = TheLog();
        const std::lock_guard<std::mutex> lock(log.mutex);
        if (!log.file) throw std::logic_error("cameraunlock_log_open has not run");
        logging::Text(line);
        return CAMERAUNLOCK_OK;
    });
}

std::int32_t cameraunlock_log_take(char* buffer, std::int32_t capacity) {
    LogState& log = TheLog();
    const std::lock_guard<std::mutex> lock(log.mutex);
    const std::int32_t length = CopyOut(log.pending, buffer, capacity, false);
    if (buffer != nullptr && capacity >= length) log.pending.clear();
    return length;
}

std::int32_t cameraunlock_settings_defaults(CameraUnlockSettings* out) {
    return Guarded("cameraunlock_settings_defaults", [&] {
        RequireStruct(out, "CameraUnlockSettings");
        *out = SettingsOf(HeadTrackingConfig{});
        return CAMERAUNLOCK_OK;
    });
}

std::int32_t cameraunlock_session_configure(const CameraUnlockSettings* settings) {
    return Guarded("cameraunlock_session_configure", [&] {
        RequireStruct(settings, "CameraUnlockSettings");
        RequireSettings(*settings);
        PositionSettings position;
        position.limit_x = settings->limit_x;
        position.limit_y = settings->limit_y;
        position.limit_y_down = settings->limit_y_down;
        position.limit_z = settings->limit_z;
        position.limit_z_back = settings->limit_z_back;
        camera::LeanClampSettings clamp;
        clamp.skin = settings->collision_margin;
        clamp.release_smoothing = settings->collision_release_smoothing;
        Session& session = TheSession();
        const std::lock_guard<std::mutex> configuring(session.settings);
        for (View& s : session.views) {
            const std::lock_guard<std::mutex> lock(s.mutex);
            s.settings = *settings;
            s.tracking.SetLocalSmoothing(settings->local_smoothing);
            s.tracking.SetRemoteSmoothing(settings->remote_smoothing);
            s.tracking.SetPositionSettings(position);
            s.clamp.SetSettings(clamp);
        }
        session.mode().SetMode(static_cast<TrackingMode>(settings->tracking_mode));
        session.aim_mode.store(settings->aim_mode);
        return CAMERAUNLOCK_OK;
    });
}

std::int32_t cameraunlock_session_start(std::int32_t udp_port) {
    return Guarded("cameraunlock_session_start", [&] { return StartView(0, udp_port); });
}

std::int32_t cameraunlock_session_stop(void) {
    return Guarded("cameraunlock_session_stop", [&] { return StopView(0); });
}

std::int32_t cameraunlock_view_start(std::int32_t view, std::int32_t udp_port) {
    return Guarded("cameraunlock_view_start", [&] { return StartView(view, udp_port); });
}

std::int32_t cameraunlock_view_stop(std::int32_t view) {
    return Guarded("cameraunlock_view_stop", [&] { return StopView(view); });
}

std::int32_t cameraunlock_session_cycle_tracking_mode(void) {
    return Guarded("cameraunlock_session_cycle_tracking_mode",
                   [&] { return static_cast<std::int32_t>(TheSession().mode().CycleMode()); });
}

std::int32_t cameraunlock_session_cycle_aim_mode(void) {
    return Guarded("cameraunlock_session_cycle_aim_mode", [&] {
        std::atomic<std::int32_t>& mode = TheSession().aim_mode;
        std::int32_t now = mode.load();
        std::int32_t next;
        do {
            next = static_cast<std::int32_t>(ads::NextAimMode(static_cast<ads::AimMode>(now)));
        } while (!mode.compare_exchange_weak(now, next));
        return next;
    });
}

std::int32_t cameraunlock_session_set_aim_mode(std::int32_t aim_mode) {
    return Guarded("cameraunlock_session_set_aim_mode", [&] {
        Require(aim_mode >= 0 && aim_mode <= 3, "aim_mode is not a CAMERAUNLOCK_AIM_*");
        TheSession().aim_mode.store(aim_mode);
        return CAMERAUNLOCK_OK;
    });
}

const char* cameraunlock_aim_mode_label(std::int32_t aim_mode) {
    const char* label = nullptr;
    Guarded("cameraunlock_aim_mode_label", [&] {
        Require(aim_mode >= 0 && aim_mode <= 3, "aim_mode is not a CAMERAUNLOCK_AIM_*");
        label = ads::AimModeLabel(static_cast<ads::AimMode>(aim_mode));
        return CAMERAUNLOCK_OK;
    });
    return label;
}

std::int32_t cameraunlock_session_frame(const CameraUnlockFrameInput* input, CameraUnlockFrame* out) {
    return Guarded("cameraunlock_session_frame", [&] { return FrameOf(0, input, out); });
}

std::int32_t cameraunlock_session_lean(const CameraUnlockObstruction* obstruction, CameraUnlockLean* out) {
    return Guarded("cameraunlock_session_lean", [&] { return LeanOf(0, obstruction, out); });
}

std::int32_t cameraunlock_view_frame(std::int32_t view, const CameraUnlockFrameInput* input, CameraUnlockFrame* out) {
    return Guarded("cameraunlock_view_frame", [&] { return FrameOf(view, input, out); });
}

std::int32_t cameraunlock_view_lean(std::int32_t view, const CameraUnlockObstruction* obstruction, CameraUnlockLean* out) {
    return Guarded("cameraunlock_view_lean", [&] { return LeanOf(view, obstruction, out); });
}

std::int32_t cameraunlock_config_describe(const char* display_name) {
    return Guarded("cameraunlock_config_describe", [&] {
        Require(display_name != nullptr, "display_name is NULL");
        ConfigState& c = TheConfig();
        const std::lock_guard<std::mutex> lock(c.mutex);
        if (c.owner) throw std::logic_error("the file is loaded, and its rows are settled");
        c.display_name = display_name;
        c.concepts.clear();
        c.locals.clear();
        return CAMERAUNLOCK_OK;
    });
}

std::int32_t cameraunlock_config_concept(const char* name, std::uint32_t flags, const char* comment,
                                         const char* default_text) {
    return Guarded("cameraunlock_config_concept", [&] {
        ConfigState& c = TheConfig();
        const std::lock_guard<std::mutex> lock(c.mutex);
        RequireDescribing(c);
        Require(name != nullptr, "name is NULL");
        Require((flags & ~(CAMERAUNLOCK_ROW_WRITABLE | CAMERAUNLOCK_ROW_PER_GAME)) == 0,
                "a concept row takes CAMERAUNLOCK_ROW_WRITABLE and CAMERAUNLOCK_ROW_PER_GAME only");
        ConceptRow row;
        bool found = false;
        for (const cfg::schema::ConceptInfo& info : cfg::schema::kConcepts) {
            if (std::strcmp(info.name, name) == 0) {
                row.id = info.id;
                found = true;
            }
        }
        if (!found) throw std::invalid_argument(std::string(name) + " is not a canonical concept of data/config-schema.json");
        row.flags = flags;
        if (comment != nullptr) row.comment = comment;
        if (default_text != nullptr) row.start = default_text;
        c.concepts.push_back(std::move(row));
        try {
            BuildTable(c);
        } catch (...) {
            c.concepts.pop_back();
            throw;
        }
        return CAMERAUNLOCK_OK;
    });
}

std::int32_t cameraunlock_config_local_bool(const char* section, const char* key, const char* comment,
                                            std::uint32_t flags, std::int32_t default_value) {
    return Guarded("cameraunlock_config_local_bool", [&] {
        LocalRow row{LocalKind::Bool};
        row.start = default_value != 0;
        ConfigState& c = TheConfig();
        const std::lock_guard<std::mutex> lock(c.mutex);
        return AddLocal(c, section, key, comment, flags, std::move(row));
    });
}

std::int32_t cameraunlock_config_local_int(const char* section, const char* key, const char* comment,
                                           std::uint32_t flags, std::int32_t default_value, std::int32_t min,
                                           std::int32_t max) {
    return Guarded("cameraunlock_config_local_int", [&] {
        LocalRow row{LocalKind::Int};
        row.start = default_value;
        row.int_min = min;
        row.int_max = max;
        ConfigState& c = TheConfig();
        const std::lock_guard<std::mutex> lock(c.mutex);
        return AddLocal(c, section, key, comment, flags, std::move(row));
    });
}

std::int32_t cameraunlock_config_local_float(const char* section, const char* key, const char* comment,
                                             std::uint32_t flags, float default_value, float min, float max) {
    return Guarded("cameraunlock_config_local_float", [&] {
        LocalRow row{LocalKind::Float};
        row.start = default_value;
        row.float_min = min;
        row.float_max = max;
        ConfigState& c = TheConfig();
        const std::lock_guard<std::mutex> lock(c.mutex);
        return AddLocal(c, section, key, comment, flags, std::move(row));
    });
}

std::int32_t cameraunlock_config_local_enum(const char* section, const char* key, const char* comment,
                                            std::uint32_t flags, const char* tokens, std::int32_t default_index) {
    return Guarded("cameraunlock_config_local_enum", [&] {
        Require(tokens != nullptr, "tokens is NULL");
        LocalRow row{LocalKind::Enum};
        const std::string list = tokens;
        std::size_t start = 0;
        while (start <= list.size()) {
            std::size_t end = list.find(',', start);
            if (end == std::string::npos) end = list.size();
            row.tokens.push_back(list.substr(start, end - start));
            start = end + 1;
        }
        Require(default_index >= 0 && static_cast<std::size_t>(default_index) < row.tokens.size(),
                "default_index names no token");
        row.start = static_cast<HostEnum>(default_index);
        ConfigState& c = TheConfig();
        const std::lock_guard<std::mutex> lock(c.mutex);
        return AddLocal(c, section, key, comment, flags, std::move(row));
    });
}

std::int32_t cameraunlock_config_local_hotkey(const char* key, const char* comment, std::uint32_t flags,
                                              const char* default_keys, std::int32_t hotkey_bit) {
    return Guarded("cameraunlock_config_local_hotkey", [&] {
        Require(default_keys != nullptr, "default_keys is NULL");
        Require(hotkey_bit >= CAMERAUNLOCK_HOTKEY_LOCAL && (hotkey_bit & (hotkey_bit - 1)) == 0,
                "hotkey_bit is not one bit at CAMERAUNLOCK_HOTKEY_LOCAL or above");
        ConfigState& c = TheConfig();
        const std::lock_guard<std::mutex> lock(c.mutex);
        return AddHotkeyRow(c, key, comment, flags, default_keys, hotkey_bit, 0);
    });
}

std::int32_t cameraunlock_config_local_hotkey_held(const char* key, const char* comment, std::uint32_t flags,
                                                   const char* default_keys, std::int32_t hotkey_bit,
                                                   std::int32_t held_bit) {
    return Guarded("cameraunlock_config_local_hotkey_held", [&] {
        Require(default_keys != nullptr, "default_keys is NULL");
        Require(hotkey_bit >= CAMERAUNLOCK_HOTKEY_LOCAL && (hotkey_bit & (hotkey_bit - 1)) == 0,
                "hotkey_bit is not one bit at CAMERAUNLOCK_HOTKEY_LOCAL or above");
        Require(held_bit >= CAMERAUNLOCK_HOTKEY_LOCAL && (held_bit & (held_bit - 1)) == 0,
                "held_bit is not one bit at CAMERAUNLOCK_HOTKEY_LOCAL or above");
        Require(held_bit != hotkey_bit, "hotkey_bit and held_bit are one bit, and a tap has to be told from a hold");
        ConfigState& c = TheConfig();
        const std::lock_guard<std::mutex> lock(c.mutex);
        return AddHotkeyRow(c, key, comment, flags, default_keys, hotkey_bit, held_bit);
    });
}

std::int32_t cameraunlock_config_render(const char* path) {
    return Guarded("cameraunlock_config_render", [&] {
        const std::filesystem::path file(Wide(path, "path"));
        ConfigState& c = TheConfig();
        const std::lock_guard<std::mutex> lock(c.mutex);
        cfg::RenderHeader header;
        const cfg::ConfigTable<HostConfig> table = BuildTable(c);
        header.display_name = *c.display_name;
        const std::string bytes = cfg::RenderCanonicalFresh(table, header);
        std::ofstream stream(file, std::ios::binary | std::ios::trunc);
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        stream.close();
        if (!stream) throw std::runtime_error("the rendered file could not be written to " + std::string(path));
        return CAMERAUNLOCK_OK;
    });
}

std::int32_t cameraunlock_config_load(const char* path, const char* defaults_path, CameraUnlockConfig* out) {
    return Guarded("cameraunlock_config_load", [&] {
        RequireStruct(out, "CameraUnlockConfig");
        ConfigState& c = TheConfig();
        const std::lock_guard<std::mutex> lock(c.mutex);
        RequireDescribing(c);

        cfg::ConfigOwnerOptions<HostConfig> options;
        options.path = Wide(path, "path");
        options.table = BuildTable(c);
        options.header.display_name = *c.display_name;
        options.defaults = defaults_path == nullptr ? cfg::DefaultsFile::PerUser()
                                                    : cfg::DefaultsFile::At(Wide(defaults_path, "defaults_path"));
        auto owner = std::make_unique<cfg::ConfigOwner<HostConfig>>(std::move(options));
        cfg::ConfigLoadResult<HostConfig> loaded = owner->Load();
        LogLines(loaded.log);
        if (!loaded.reason.empty()) LogLine(loaded.reason);

        CameraUnlockConfig filled = {};
        filled.struct_size = sizeof(filled);
        filled.udp_port = loaded.config.udp_port;
        filled.enable_on_startup = loaded.config.enable_on_startup ? 1 : 0;
        filled.world_space_yaw = loaded.config.world_space_yaw ? 1 : 0;
        filled.settings = SettingsOf(loaded.config);

        c.loaded = std::move(loaded.config);
        c.owner = std::move(owner);
        *out = filled;
        return static_cast<std::int32_t>(loaded.status);
    });
}

std::int32_t cameraunlock_config_reload(void) {
    return Guarded("cameraunlock_config_reload", [&] {
        ConfigState& c = TheConfig();
        const std::lock_guard<std::mutex> lock(c.mutex);
        RequireLoaded(c);
        if (!c.owner->FileChanged()) return static_cast<std::int32_t>(cfg::ConfigReloadStatus::Unchanged);
        const cfg::ConfigReloadResult<HostConfig> reloaded = c.owner->Reload();
        LogLines(reloaded.log);
        if (reloaded.config) {
            for (std::size_t i = 0; i < c.locals.size(); ++i) {
                if ((c.locals[i].flags & CAMERAUNLOCK_ROW_LIVE) != 0) c.loaded.locals[i] = reloaded.config->locals[i];
            }
        }
        return static_cast<std::int32_t>(reloaded.status);
    });
}

std::int32_t cameraunlock_config_get_int(std::int32_t row, std::int32_t* out) {
    return Guarded("cameraunlock_config_get_int", [&] {
        Require(out != nullptr, "out is NULL");
        ConfigState& c = TheConfig();
        const std::lock_guard<std::mutex> lock(c.mutex);
        RequireLoaded(c);
        const LocalRow& described = LocalAt(c, row);
        const LocalValue& value = c.loaded.locals[static_cast<std::size_t>(row)];
        switch (described.kind) {
            case LocalKind::Bool:
                *out = std::get<bool>(value) ? 1 : 0;
                break;
            case LocalKind::Int:
                *out = std::get<std::int32_t>(value);
                break;
            case LocalKind::Enum:
                *out = static_cast<std::int32_t>(std::get<HostEnum>(value));
                break;
            default:
                throw std::invalid_argument(described.key + " is not a bool, an int or an enum row");
        }
        return CAMERAUNLOCK_OK;
    });
}

std::int32_t cameraunlock_config_get_float(std::int32_t row, float* out) {
    return Guarded("cameraunlock_config_get_float", [&] {
        Require(out != nullptr, "out is NULL");
        ConfigState& c = TheConfig();
        const std::lock_guard<std::mutex> lock(c.mutex);
        RequireLoaded(c);
        const LocalRow& described = LocalAt(c, row);
        if (described.kind != LocalKind::Float) throw std::invalid_argument(described.key + " is not a float row");
        *out = std::get<float>(c.loaded.locals[static_cast<std::size_t>(row)]);
        return CAMERAUNLOCK_OK;
    });
}

std::int32_t cameraunlock_config_save_int(std::int32_t row, std::int32_t value) {
    return Guarded("cameraunlock_config_save_int", [&] {
        ConfigState& c = TheConfig();
        const std::lock_guard<std::mutex> lock(c.mutex);
        RequireLoaded(c);
        const LocalRow& described = LocalAt(c, row);
        LocalValue next;
        switch (described.kind) {
            case LocalKind::Bool:
                next = value != 0;
                break;
            case LocalKind::Int:
                next = value;
                break;
            case LocalKind::Enum:
                Require(value >= 0 && static_cast<std::size_t>(value) < described.tokens.size(), "value names no token");
                next = static_cast<HostEnum>(value);
                break;
            default:
                throw std::invalid_argument(described.key + " is not a bool, an int or an enum row");
        }
        const std::size_t index = static_cast<std::size_t>(row);
        const std::int32_t status = Save(c, [&](HostConfig& config) { config.locals[index] = next; });
        c.loaded.locals[index] = next;
        return status;
    });
}

std::int32_t cameraunlock_config_save_float(std::int32_t row, float value) {
    return Guarded("cameraunlock_config_save_float", [&] {
        ConfigState& c = TheConfig();
        const std::lock_guard<std::mutex> lock(c.mutex);
        RequireLoaded(c);
        const LocalRow& described = LocalAt(c, row);
        if (described.kind != LocalKind::Float) throw std::invalid_argument(described.key + " is not a float row");
        const std::size_t index = static_cast<std::size_t>(row);
        const std::int32_t status = Save(c, [&](HostConfig& config) { config.locals[index] = value; });
        c.loaded.locals[index] = value;
        return status;
    });
}

std::int32_t cameraunlock_config_save_tracking_mode(void) {
    return Guarded("cameraunlock_config_save_tracking_mode", [&] {
        const TrackingModeChannels channels = EncodeTrackingMode(TheSession().mode().GetMode());
        ConfigState& c = TheConfig();
        const std::lock_guard<std::mutex> lock(c.mutex);
        return Save(c, [&](HostConfig& config) {
            config.rotation_enabled = channels.rotation_enabled;
            config.position_enabled = channels.position_enabled;
        });
    });
}

std::int32_t cameraunlock_config_save_aim_mode(void) {
    return Guarded("cameraunlock_config_save_aim_mode", [&] {
        const ads::AimModeSettings mode = ads::EncodeAimMode(static_cast<ads::AimMode>(TheSession().aim_mode.load()));
        ConfigState& c = TheConfig();
        const std::lock_guard<std::mutex> lock(c.mutex);
        return Save(c, [&](HostConfig& config) {
            config.true_free_look = mode.trueFreeLook;
            config.free_look_marker = mode.freeLookMarker;
            config.stock_sights = mode.stockSights;
        });
    });
}

std::int32_t cameraunlock_config_save_world_space_yaw(std::int32_t world_space_yaw) {
    return Guarded("cameraunlock_config_save_world_space_yaw", [&] {
        ConfigState& c = TheConfig();
        const std::lock_guard<std::mutex> lock(c.mutex);
        return Save(c, [&](HostConfig& config) { config.world_space_yaw = world_space_yaw != 0; });
    });
}

std::int32_t cameraunlock_hotkeys_start(void) {
    return Guarded("cameraunlock_hotkeys_start", [&] {
        ConfigState& c = TheConfig();
        const std::lock_guard<std::mutex> lock(c.mutex);
        RequireLoaded(c);
        if (c.poller.load() != nullptr) throw std::logic_error("the hotkeys are already started");
        c.poller.store(new input::HotkeyPoller());
        using Concept = cfg::schema::Concept;
        for (const ConceptRow& row : c.concepts) {
            if (row.id == Concept::ToggleKey) {
                Register(c, "ToggleKey", c.loaded.toggle_key_name, CAMERAUNLOCK_HOTKEY_TOGGLE);
            } else if (row.id == Concept::CycleTrackingModeKey) {
                Register(c, "CycleTrackingModeKey", c.loaded.cycle_tracking_mode_key_name,
                         CAMERAUNLOCK_HOTKEY_CYCLE_TRACKING_MODE);
            } else if (row.id == Concept::YawModeKey) {
                Register(c, "YawModeKey", c.loaded.yaw_mode_key_name, CAMERAUNLOCK_HOTKEY_YAW_MODE);
            } else if (row.id == Concept::TrueFreeLookKey) {
                Register(c, "TrueFreeLookKey", c.loaded.true_free_look_key_name, CAMERAUNLOCK_HOTKEY_AIM_MODE);
            }
        }
        for (std::size_t i = 0; i < c.locals.size(); ++i) {
            if (c.locals[i].kind == LocalKind::Hotkey) {
                Register(c, c.locals[i].key, std::get<std::string>(c.loaded.locals[i]), c.locals[i].hotkey_bit,
                         c.locals[i].held_bit);
            }
        }
        if (!c.poller.load()->Start()) throw std::runtime_error("the hotkey poller did not start");
        return CAMERAUNLOCK_OK;
    });
}

std::int32_t cameraunlock_window_center(std::uint64_t window) {
    return Guarded("cameraunlock_window_center", [&] {
        const bool centred = os::CenterWindowInWorkArea(
            reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window)),
            [](os::WindowLogLevel, const char* message) { LogLine(message); });
        return centred ? 1 : 0;
    });
}

#ifdef CAMERAUNLOCK_C_TESTING

std::int32_t cameraunlock_testing_reset(void) {
    return Guarded("cameraunlock_testing_reset", [&] {
        delete SessionSlot();
        SessionSlot() = new Session();
        return CAMERAUNLOCK_OK;
    });
}

namespace {

std::int32_t DeliverTo(std::int32_t view, const void* datagram, std::int32_t length, std::int32_t remote) {
    View& s = ViewAt(TheSession(), view);
    Require(datagram != nullptr && length >= 0, "datagram is NULL or its length negative");
    const std::lock_guard<std::mutex> lock(s.mutex);
    if (s.started) {
        throw std::logic_error((view == 0 ? std::string("the session") : "view " + std::to_string(view)) +
                               " is listening: a datagram is delivered to one that is not");
    }
    sockaddr_in sender = {};
    sender.sin_family = AF_INET;
    sender.sin_port = htons(4242);
    inet_pton(AF_INET, remote != 0 ? "192.0.2.1" : "127.0.0.1", &sender.sin_addr);
    // Each delivery is a later arrival than the last, as the session tells packets apart by it.
    static std::int64_t last_arrival = 0;
    const std::int64_t now = SteadyMicros();
    last_arrival = now > last_arrival ? now : last_arrival + 1;
    detail::UdpReceiverTestAccess::Deliver(s.receiver, datagram, length, sender, last_arrival);
    return CAMERAUNLOCK_OK;
}

}  // namespace

std::int32_t cameraunlock_testing_deliver(const void* datagram, std::int32_t length, std::int32_t remote) {
    return Guarded("cameraunlock_testing_deliver", [&] { return DeliverTo(0, datagram, length, remote); });
}

std::int32_t cameraunlock_testing_deliver_view(std::int32_t view, const void* datagram, std::int32_t length,
                                               std::int32_t remote) {
    return Guarded("cameraunlock_testing_deliver_view", [&] { return DeliverTo(view, datagram, length, remote); });
}

#endif

std::int32_t cameraunlock_hotkeys_take(void) {
    return TheConfig().pressed.exchange(0);
}

void cameraunlock_hotkeys_drop(void) {
    ConfigState& c = TheConfig();
    // First, so a key down now ends as nothing: its tap or its hold would otherwise be answered
    // after this, when the host is acting on keys again.
    if (input::HotkeyPoller* poller = c.poller.load()) poller->DisarmHoldPresses();
    c.pressed.store(0);
}
