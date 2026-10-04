#include <cameraunlock/reframework/camera_pipeline.h>

#include <cameraunlock/ads/ads_fade.h>
#include <cameraunlock/ads/aim_mode.h>
#include <cameraunlock/math/smoothing_utils.h>
#include <cameraunlock/camera/zoom_compensation.h>
#include <cameraunlock/reframework/rig_lean.h>
#include <cameraunlock/memory/safe_memory.h>
#include <cameraunlock/reframework/camera_controller_hook.h>
#include <cameraunlock/reframework/log_callback.h>
#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/reframework/plugin_mod.h>
#include <cameraunlock/rendering/gui_marker_compensation.h>
#include <cameraunlock/time/qpc_clock.h>

#include <reframework/API.hpp>

#include <cmath>
#include <stdexcept>

namespace cameraunlock::reframework {

// Minimum gap between repeats of the camera-controller-not-found warning.
// Wall-clock, not frame-count: a frame-gated warning writes hundreds of lines
// an hour on a high-refresh display and buries the startup sequence.
constexpr uint64_t kHookWarnIntervalUs = 30ull * 1000000ull;

static CameraPipelineDescriptor g_descriptor;

static FrameProjection g_projection;
static uint64_t g_renderFrame = 0;

static CameraTransformResolver g_cameraResolver;

// via.Camera.get_ProjectionMatrix - not part of the standard chain, resolved
// separately for exact focal-length reads.
static ::reframework::API::Method* g_getProjectionMatrix = nullptr;

// The game's clean matrix, captured after the controller updated it, replayed
// at the next controller update so the game never accumulates our rotation.
static struct {
    Matrix4x4f gameMatrix;
    bool hasGameMatrix = false;
} g_saved;

// The clean matrix of the frame currently being rendered.
static struct {
    Matrix4x4f matrix;
    bool valid = false;
} g_cleanCameraMatrix;

static bool g_trackingAppliedThisFrame = false;

// The lean: clamped against the world, and handed over to the rig while the
// sights are up where the mod has one (reframework/rig_lean.h).
static RigLean g_rigLean;
static bool g_loggedAiming = false;

// The share of yaw, pitch and the lean that reaches the view in stock sights:
// 1 at the hip, 0 with the sights up.
static cameraunlock::ads::AdsFade g_stockSightsFade;

// The head rotation the camera got this frame, in degrees: after the stock
// sights share and the zoom factor.
static struct {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    bool valid = false;
} g_appliedRotation;

// The rig's offset, world space. `request` is decided at BeginRendering and
// written at the next LateUpdateBehavior; `applied` is what that write put on
// the rig, which the game's eye carries until the next write.
static struct {
    cameraunlock::math::Vec3 request;
    cameraunlock::math::Vec3 applied;
    bool written = false;
} g_rig;

static struct {
    bool contact = false;
    bool failed = false;
    uint64_t lastSampleUs = 0;
    cameraunlock::math::Vec3 lastCleanEye;
    bool hasLastCleanEye = false;
} g_clampLog;

// A clean eye that moves further than this in one frame is a camera cut
// (teleport, load, cinematic hand-back), not walking.
constexpr float kCameraCutMetres = 1.0f;
constexpr uint64_t kClampSampleIntervalUs = 10ull * 1000000ull;

static struct {
    bool loggedFirst = false;
    bool loggedZoomed = false;
} g_zoomLog;

// Per-frame transform + camera cache. Both are invalidated together at the
// camera-controller update pre-hook and at the end of every post-render
// callback, so within one render frame they hold the live primary camera and its
// transform without re-walking the SceneManager chain. The post-render clear has
// to run on frames that applied nothing too: the controller's post-hook fills the
// cache on a menu or loading frame, and with the controller then idle through a
// scene load the next gameplay frame would write through the old camera's
// transform, which the load may have freed.
static void* g_cachedTransform = nullptr;
static void* g_cachedCamera = nullptr;

static void* GetCameraTransformCached() {
    if (g_cachedTransform) return g_cachedTransform;
    g_cachedTransform = g_cameraResolver.ResolveTransform(&g_cachedCamera);
    return g_cachedTransform;
}

// Resolve the camera transform's world matrix, reusing the per-frame cache. The
// resolver's chain walk is SEH-guarded, so a camera torn down during a scene
// transition yields nullptr instead of crashing.
static Matrix4x4f* GetCameraWorldMatrix() {
    void* transform = GetCameraTransformCached();
    if (!transform) return nullptr;
    return reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + kTransformWorldMatrixOffset);
}

// --- Core head tracking application ---

// Every path that applies no lean. Returns the offset that cancels a rig
// already written this frame, so the eye drops back at once rather than a frame
// later.
static cameraunlock::math::Vec3 StopLean() {
    g_rigLean.Stop();
    g_rig.request = cameraunlock::math::Vec3();
    g_clampLog.hasLastCleanEye = false;
    return g_rig.written ? -g_rig.applied : cameraunlock::math::Vec3();
}

// The factor a head movement is scaled by so it moves the picture as far as it
// would at the game's un-zoomed field of view. 1 without a baseline, or on a
// frame either field of view cannot be read.
static float ZoomFactor() {
    if (!g_descriptor.unzoomedFovDegrees) return 1.0f;
    const float live = g_cameraResolver.ResolveFovDegrees(g_cachedCamera);
    float base = 0.0f;
    const bool haveBase = g_descriptor.unzoomedFovDegrees(base);
    const bool usable = haveBase && live > 1.0f && live < 179.0f && base > 1.0f && base < 179.0f;
    const float factor = usable ? cameraunlock::camera::FovZoomFactor(std::tan(live * 0.5f * kDegToRad),
                                                                     std::tan(base * 0.5f * kDegToRad))
                                : 1.0f;
    // Every term, once on the first frame the camera updates and once the first
    // time the view is zoomed, so the units can be checked by eye: the factor
    // reads 1.0000 at the hip or the two numbers are not the same quantity.
    const bool zoomed = usable && (factor < 0.9999f || factor > 1.0001f);
    if (!g_zoomLog.loggedFirst || (zoomed && !g_zoomLog.loggedZoomed)) {
        g_zoomLog.loggedFirst = true;
        if (zoomed) g_zoomLog.loggedZoomed = true;
        LogInfo("Zoom compensation: fov=%.3f base=%.3f (degrees, via.Camera.get_FOV, same axis) factor=%.4f%s",
                live, base, factor, usable ? "" : " - unreadable, no compensation this frame");
    }
    return factor;
}

// What a lean asked for and what was applied, along the aim and across it, on a
// cadence while the head is off centre and at once when the sights change. A
// lean in that comes up short reads straight off this line: requested is the
// lean after the position limits and the zoom factor, applied is what the clamp,
// the forward stop and the hand-over left of it.
constexpr uint64_t kLeanSampleIntervalUs = 2ull * 1000000ull;

static void LogLean(const cameraunlock::math::Vec3& requested, const cameraunlock::math::Vec3& applied,
                    const cameraunlock::math::Vec3& rig, const cameraunlock::math::Vec3& aimForward, bool aiming,
                    float zoom) {
    static uint64_t s_lastUs = 0;
    static bool s_aiming = false;
    const uint64_t now = cameraunlock::time::QpcNowMicros();
    if (aiming == s_aiming && now - s_lastUs < kLeanSampleIntervalUs) return;
    if (requested.SqrMagnitude() < 1e-6f && aiming == s_aiming) return;
    s_lastUs = now;
    s_aiming = aiming;
    const float requestedAlong = cameraunlock::math::Vec3::Dot(requested, aimForward);
    const float appliedAlong = cameraunlock::math::Vec3::Dot(applied, aimForward);
    LogInfo("Lean: requested %.3f m along the aim, %.3f m across; applied %.3f m along, %.3f m across "
            "(camera %.3f m, rig %.3f m); sights %s, zoom %.4f",
            requestedAlong, (requested - aimForward * requestedAlong).Magnitude(), appliedAlong,
            (applied - aimForward * appliedAlong).Magnitude(), (applied - rig).Magnitude(), rig.Magnitude(),
            aiming ? "up" : "down", zoom);
}

// Every change of state, and a sample on a cadence while the head is off centre,
// since transitions alone cannot tell a clear room from a query that never runs.
static void LogLeanClampState(bool leaning) {
    const cameraunlock::camera::LeanClamp& clamp = g_rigLean.Clamp();
    const bool contact = clamp.InContact();
    const bool failed = clamp.LastQueryFailed();
    const uint64_t now = cameraunlock::time::QpcNowMicros();
    const bool transition = contact != g_clampLog.contact || failed != g_clampLog.failed;
    if (!transition && (!leaning || now - g_clampLog.lastSampleUs < kClampSampleIntervalUs)) return;
    g_clampLog.contact = contact;
    g_clampLog.failed = failed;
    g_clampLog.lastSampleUs = now;
    LogInfo("Lean clamp: %s%s",
            failed ? "query failed, lean passed through unclamped" : contact ? "held off a surface" : "clear",
            transition ? "" : " (sample)");
}

static void ApplyHeadTracking(Matrix4x4f* worldMat) {
    float yaw, pitch, roll;
    // Zero rotation builds an exact-identity matrix (bit-exact: sin(0)=0,
    // cos(0)=1 give the identity quaternion, which maps to the exact identity
    // 3x3, and pre-multiplying by identity returns the input unchanged).
    // Skipping the rotation block in that case is byte-identical and avoids
    // the per-frame trig/quaternion work in position-only mode and whenever
    // the view is perfectly centered.
    bool hasRotation = PluginMod::Instance().GetProcessedRotation(yaw, pitch, roll)
                       && (yaw != 0.0f || pitch != 0.0f || roll != 0.0f);

    float px, py, pz;
    bool hasPosition = PluginMod::Instance().GetPositionOffset(px, py, pz);

    const bool aiming = g_descriptor.isAiming && g_descriptor.isAiming();
    const unsigned long long nowMs = cameraunlock::time::QpcNowMicros() / 1000ull;

    // Stock sights: yaw, pitch and the whole lean ease out with the sights up and
    // roll stays. Before the zoom factor, the clamp, the rig and the reticle read
    // the pose, so all of them work from the pose that is applied.
    const float poseShare = g_stockSightsFade.Update(
        cameraunlock::ads::StockSightsEngaged(PluginMod::Instance().GetAimMode(), aiming), nowMs);
    if (poseShare != 1.0f) {
        yaw *= poseShare;
        pitch *= poseShare;
        px *= poseShare;
        py *= poseShare;
        pz *= poseShare;
    }

    const float zoom = ZoomFactor();
    if (zoom != 1.0f) {
        yaw = cameraunlock::camera::ScaleAngleForZoom(yaw, zoom);
        pitch = cameraunlock::camera::ScaleAngleForZoom(pitch, zoom);
        // px, py and pz are in the camera's own axes, where z is the view axis
        // (ViewSpaceOffsetToWorld), so the lean in keeps its full travel.
        const cameraunlock::math::Vec3 scaled = cameraunlock::camera::ScaleLeanForZoom(
            cameraunlock::math::Vec3(px, py, pz), cameraunlock::math::Vec3(0.0f, 0.0f, 1.0f), zoom);
        px = scaled.x;
        py = scaled.y;
        pz = scaled.z;
    }

    g_appliedRotation.yaw = hasRotation ? yaw : 0.0f;
    g_appliedRotation.pitch = hasRotation ? pitch : 0.0f;
    g_appliedRotation.roll = hasRotation ? roll : 0.0f;
    g_appliedRotation.valid = true;

    // Through the clean camera's own axes, before the head rotation below.
    float lean[3] = {0.0f, 0.0f, 0.0f};
    if (hasPosition) ViewSpaceOffsetToWorld(*worldMat, px, py, pz, lean);
    cameraunlock::math::Vec3 cameraOffset(lean[0], lean[1], lean[2]);

    const bool shapedLean = g_descriptor.isAiming || g_descriptor.leanQuery;
    if (shapedLean && hasPosition) {
        if (aiming != g_loggedAiming) {
            g_loggedAiming = aiming;
            LogInfo("Aim state: %s", aiming ? "sights up" : "sights down");
        }
        const cameraunlock::camera::LeanQueryFn query =
            PluginMod::Instance().GetConfig().collisionEnabled ? g_descriptor.leanQuery : nullptr;
        const bool rigAvailable = g_descriptor.writeRig && g_descriptor.rigAvailable();

        const cameraunlock::math::Vec3 gameEye(worldMat->m[3][0], worldMat->m[3][1], worldMat->m[3][2]);
        const cameraunlock::math::Vec3 applied = g_rig.written ? g_rig.applied : cameraunlock::math::Vec3();
        const cameraunlock::math::Vec3 cleanEye = gameEye - applied;
        if (g_clampLog.hasLastCleanEye && (cleanEye - g_clampLog.lastCleanEye).Magnitude() > kCameraCutMetres) {
            g_rigLean.Clamp().Reset();
        }
        g_clampLog.lastCleanEye = cleanEye;
        g_clampLog.hasLastCleanEye = true;

        // The way the clean camera looks. The sign matters: the forward stop
        // holds a lean in, not a lean back.
        float forward[3];
        CameraForward(*worldMat, forward);
        const cameraunlock::math::Vec3 aimForward =
            cameraunlock::math::Vec3(forward[0], forward[1], forward[2]).Normalized();
        const cameraunlock::math::Vec3 requested = cameraOffset;
        const RigLeanFrame frame = g_rigLean.Update(
            gameEye, cameraOffset, aimForward, applied, aiming, PluginMod::Instance().IsTrueFreeLook(), rigAvailable,
            PluginMod::Instance().GetLastDeltaTime(), nowMs, query, nullptr);
        cameraOffset = frame.camera;
        LogLean(requested, frame.camera + applied, frame.rigRequest, aimForward, aiming, zoom);
        g_rig.request = frame.rigRequest;
        if (query) LogLeanClampState(cameraOffset.SqrMagnitude() > 1e-8f || applied.SqrMagnitude() > 1e-8f);
    } else if (shapedLean) {
        cameraOffset = StopLean();
    }

    const bool hasOffset = cameraOffset.x != 0.0f || cameraOffset.y != 0.0f || cameraOffset.z != 0.0f;
    if (!hasRotation && !hasOffset) return;

    if (hasRotation) {
        float yr = -yaw * kDegToRad;
        float pr = pitch * kDegToRad;
        float rr = roll * kDegToRad;

        if (PluginMod::Instance().IsWorldSpaceYaw()) {
            ApplyWorldSpaceHeadRotation(*worldMat, yr, pr, rr);
        } else {
            ApplyCameraLocalHeadRotation(*worldMat, yr, pr, rr);
        }
    }

    worldMat->m[3][0] += cameraOffset.x;
    worldMat->m[3][1] += cameraOffset.y;
    worldMat->m[3][2] += cameraOffset.z;
}

// --- The rig (LateUpdateBehavior and EndRendering) ---

void CameraPipelinePreLateUpdate() {
    // Unset when InitCameraPipeline refused the descriptor.
    if (!g_descriptor.writeRig) return;
    // A restore that never came (a frame with no EndRendering) is taken off here,
    // so an offset is never written on top of one still in place.
    if (g_rig.written) {
        g_descriptor.restoreRig();
        g_rig.written = false;
    }
    g_rig.applied = cameraunlock::math::Vec3();
    const cameraunlock::math::Vec3& r = g_rig.request;
    const bool wanted = r.x != 0.0f || r.y != 0.0f || r.z != 0.0f;
    if (wanted) {
        const float offset[3] = {r.x, r.y, r.z};
        if (g_descriptor.writeRig(offset)) {
            g_rig.applied = r;
            g_rig.written = true;
        }
    }
    static bool s_carrying = false;
    if (g_rig.written != s_carrying) {
        s_carrying = g_rig.written;
        LogInfo("Rig: %s", s_carrying ? "carrying the lean" : "released");
    }
    if (wanted && !g_rig.written) {
        static uint64_t s_lastFailUs = 0;
        const uint64_t now = cameraunlock::time::QpcNowMicros();
        if (now - s_lastFailUs > kClampSampleIntervalUs) {
            s_lastFailUs = now;
            LogWarning("Rig: the mod could not write it, the lean stays on the camera");
        }
    }
}

void CameraPipelinePostEndRendering() {
    if (!g_descriptor.writeRig || !g_rig.written) return;
    g_descriptor.restoreRig();
    g_rig.written = false;
}

// --- Camera controller hooks (save/restore) ---

static int CameraUpdatePreHook(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys,
                               unsigned long long ret_addr) {
    g_cachedTransform = nullptr;
    g_cachedCamera = nullptr;

    if (!g_saved.hasGameMatrix || !PluginMod::Instance().IsEnabled()) {
        return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    }

    Matrix4x4f* worldMat = GetCameraWorldMatrix();
    if (!worldMat) return REFRAMEWORK_HOOK_CALL_ORIGINAL;

    // RE Engine transform pointers can go stale across scene transitions; guard
    // the raw write so a torn-down camera never crashes the game.
    cameraunlock::memory::SafeWrite(reinterpret_cast<std::uintptr_t>(worldMat),
                                    g_saved.gameMatrix);

    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

static void CameraUpdatePostHook(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty,
                                 unsigned long long ret_addr) {
    Matrix4x4f* worldMat = GetCameraWorldMatrix();
    if (!worldMat) return;

    if (!cameraunlock::memory::SafeRead(reinterpret_cast<std::uintptr_t>(worldMat),
                                        g_saved.gameMatrix)) {
        return;
    }
    g_saved.hasGameMatrix = true;

    static bool s_loggedOnce = false;
    if (!s_loggedOnce) {
        REQuat q = MatrixToQuat(g_saved.gameMatrix);
        LogInfo("Hook save/restore active: gameQ=%.3f %.3f %.3f %.3f", q.x, q.y, q.z, q.w);
        s_loggedOnce = true;
    }
}

static CameraControllerHooker* g_controllerHooker = nullptr;

// Retry discovery for the whole session rather than capping it: a cap turns a
// controller that appears late - a save loaded twenty minutes in, a rig rebuilt
// after a scene change - into a hook that can never install again.
static void EnsureCameraControllerHooked() {
    if (g_controllerHooker->IsHooked()) return;

    if (g_descriptor.hookRetryCooldownFrames > 0) {
        static int s_cooldown = 0;
        if (s_cooldown-- > 0) return;
        s_cooldown = g_descriptor.hookRetryCooldownFrames;
    }

    if (g_controllerHooker->TryHook(GetCameraTransformCached())) return;

    int attempts = g_controllerHooker->AttemptCount();
    uint64_t now = cameraunlock::time::QpcNowMicros();
    static uint64_t s_lastHookWarnUs = 0;
    if (attempts == 1 || (now - s_lastHookWarnUs) >= kHookWarnIntervalUs) {
        s_lastHookWarnUs = now;
        LogWarning("Camera controller hook not yet found (attempt %d) - head tracking "
                   "still active via the BeginRendering restore path", attempts);
    }
}

// --- Initialization ---

static bool InitCachedFunctions() {
    static bool s_attempted = false;
    if (s_attempted) return !g_cameraResolver.HasFailed();
    s_attempted = true;

    if (!g_cameraResolver.Initialize()) return false;

    g_getProjectionMatrix = FindMethodByParamCount("via.Camera", "get_ProjectionMatrix", 0);
    if (!g_getProjectionMatrix) {
        LogWarning("via.Camera.get_ProjectionMatrix not found - will fall back to get_FOV");
    }

    if (g_descriptor.hookControllerAtInit && !g_controllerHooker->TryHook(nullptr)) {
        LogWarning("Camera controller hook not installed at init - retrying during gameplay");
    }

    if (g_descriptor.onInit) g_descriptor.onInit();

    LogInfo("Methods cached");
    return true;
}

void InitCameraPipeline(const CameraPipelineDescriptor& descriptor) {
    if (!descriptor.gate) {
        LogError("CameraPipelineDescriptor::gate is null - the pipeline has no way to tell "
                 "gameplay from a menu and stays inert");
        return;
    }
    const int rigCallbacks = (descriptor.rigAvailable ? 1 : 0) + (descriptor.writeRig ? 1 : 0) +
                             (descriptor.restoreRig ? 1 : 0);
    if (rigCallbacks != 0 && rigCallbacks != 3) {
        throw std::invalid_argument(
            "CameraPipelineDescriptor: rigAvailable, writeRig and restoreRig are set together or not at all");
    }
    if (descriptor.writeRig && !descriptor.isAiming) {
        throw std::invalid_argument("CameraPipelineDescriptor: a rig needs isAiming, which decides when it carries the lean");
    }
    g_descriptor = descriptor;

    const PluginConfig& config = PluginMod::Instance().GetConfig();
    cameraunlock::camera::LeanClampSettings clamp;
    clamp.skin = config.collisionMargin;
    clamp.release_smoothing = config.collisionReleaseSmoothing;
    g_rigLean.Clamp().SetSettings(clamp);
    if (descriptor.forwardStopMetres > 0.f) {
        g_rigLean.SetForwardStop(descriptor.forwardStopMetres);
        LogInfo("Lean in: stops %.3f m forward of the eye while the sights are up", descriptor.forwardStopMetres);
    }
    if (descriptor.leanQuery) {
        LogInfo("Lean clamp: %s, margin %.3f m, release smoothing %.2f",
                config.collisionEnabled ? "on" : "off (CollisionEnabled=false)", clamp.skin, clamp.release_smoothing);
    }

    static CameraControllerHooker hooker{
        g_descriptor.controllerCandidateTypes,
        g_descriptor.controllerCandidateCount,
        CameraUpdatePreHook,
        CameraUpdatePostHook};
    g_controllerHooker = &hooker;
}

// --- Focal lengths ---

static bool ComputeMarkerFocalLengths(float& fx, float& fy) {
    void* cam = g_cachedCamera ? g_cachedCamera : g_cameraResolver.ResolveCamera();
    if (!cam) return false;

    if (g_getProjectionMatrix) {
        auto ret = g_getProjectionMatrix->invoke(
            reinterpret_cast<::reframework::API::ManagedObject*>(cam), EmptyArgs());
        if (!ret.exception_thrown) {
            // Matrix4x4 (64 bytes) returned by value in ret.bytes, row-major.
            auto* m = reinterpret_cast<const float*>(ret.bytes.data());
            if (cameraunlock::rendering::FocalLengthsFromProjection(
                    m[0], m[5], kHalfReferenceCanvasWidth, kHalfReferenceCanvasHeight, fx, fy)) {
                static bool s_logged = false;
                if (!s_logged) {
                    s_logged = true;
                    LogInfo("Projection matrix focal lengths: P00=%.4f P11=%.4f fx=%.1f fy=%.1f",
                            m[0], m[5], fx, fy);
                }
                // Square pixels: horizontal and vertical pixel focal lengths must
                // match. Most of these titles report them equal, but the RE3 build
                // proved this projection path can return P00 at half its true value
                // (fx ends up half of fy), which under-compensates yaw and drifts
                // the reticle/markers horizontally. fy (vertical) is the trusted
                // value; enforce fx = fy so a divergent matrix can never slip
                // through. This lives here, in the one shared computation, because
                // a per-call-site copy is exactly how Village lost it once.
                fx = fy;
                return true;
            }
        }
    }

    float fov = g_cameraResolver.ResolveFovDegrees(cam);
    return cameraunlock::rendering::FocalLengthsFromVerticalFov(
        fov, kHalfReferenceCanvasWidth, kHalfReferenceCanvasHeight, fx, fy);
}

bool GetMarkerFocalLengths(float& fx, float& fy) {
    static uint64_t s_frame = static_cast<uint64_t>(-1);
    static bool s_ok = false;
    static float s_fx = 0.f;
    static float s_fy = 0.f;

    if (s_frame != g_renderFrame) {
        s_frame = g_renderFrame;
        s_ok = ComputeMarkerFocalLengths(s_fx, s_fy);
    }
    if (!s_ok) return false;
    fx = s_fx;
    fy = s_fy;
    return true;
}

// --- Per-frame projection ---

static void UpdateFrameProjection(const Matrix4x4f& clean, const Matrix4x4f& head) {
    const float dt = PluginMod::Instance().GetLastDeltaTime();

    ComputeCleanToHeadRotation(clean, head, g_projection.cleanToHead);
    ComputeCleanLocalPositionDelta(clean, head, g_projection.cleanLocalPositionDelta);
    g_projection.cleanToHeadValid = true;

    float rawFov = g_cameraResolver.ResolveFovDegrees(g_cachedCamera);
    if (rawFov <= 10.f) rawFov = g_projection.fovDegrees;
    static cameraunlock::math::SmoothedFloat s_fov;
    g_projection.fovDegrees = s_fov.Update(rawFov, kProjectionSmoothing, dt);

    float yaw = 0.f, pitch = 0.f, roll = 0.f;
    PluginMod::Instance().GetProcessedRotation(yaw, pitch, roll);
    g_projection.rollDegrees = roll;

    float rawRight = 0.f, rawUp = 0.f;
    if (ProjectForwardToViewTangents(clean, head, rawRight, rawUp)) {
        static cameraunlock::math::SmoothedFloat s_markerRight;
        static cameraunlock::math::SmoothedFloat s_markerUp;
        g_projection.markerTanRight = s_markerRight.Update(rawRight, kProjectionSmoothing, dt);
        g_projection.markerTanUp = s_markerUp.Update(rawUp, kProjectionSmoothing, dt);
        g_projection.markerValid = true;
    } else {
        g_projection.markerValid = false;
    }

    if (g_descriptor.aimDistanceMeters <= 0.f) {
        g_projection.aimValid = false;
        return;
    }

    if (ProjectAimToViewTangents(clean, head, g_descriptor.aimDistanceMeters, rawRight, rawUp)) {
        static cameraunlock::math::SmoothedFloat s_aimRight;
        static cameraunlock::math::SmoothedFloat s_aimUp;
        g_projection.aimTanRight = s_aimRight.Update(rawRight, kProjectionSmoothing, dt);
        g_projection.aimTanUp = s_aimUp.Update(rawUp, kProjectionSmoothing, dt);
        g_projection.aimValid = g_projection.fovDegrees > 10.f;
    } else {
        g_projection.aimValid = false;
    }
}

// --- Public API ---

bool GetAppliedHeadRotation(float& yaw, float& pitch, float& roll) {
    if (!g_appliedRotation.valid) return false;
    yaw = g_appliedRotation.yaw;
    pitch = g_appliedRotation.pitch;
    roll = g_appliedRotation.roll;
    return true;
}

const FrameProjection& GetFrameProjection() { return g_projection; }
uint64_t GetRenderFrame() { return g_renderFrame; }
const Matrix4x4f& GetCleanCameraMatrix() { return g_cleanCameraMatrix.matrix; }
bool IsCleanCameraMatrixValid() { return g_cleanCameraMatrix.valid; }
CameraTransformResolver& GetCameraResolver() { return g_cameraResolver; }
void* GetCachedCamera() { return g_cachedCamera; }

// A frame that writes nothing to the camera. Nothing was projected, so nothing
// derived from a projection is usable: leaving the flags true hands a GUI
// consumer the last tracked frame's offsets over an untracked camera.
static void SkipFrame() {
    g_projection.markerValid = false;
    g_projection.aimValid = false;
    g_projection.cleanToHeadValid = false;
    StopLean();
    g_stockSightsFade.Reset();
    g_appliedRotation.valid = false;
}

void CameraPipelinePreRender() {
    // Before every gate below: the first-packet latch has to survive
    // AutoEnable=false, a menu, and a failed function cache, because those are
    // exactly the states a "no head tracking" report is trying to tell apart.
    PluginMod::Instance().LogFirstTrackerPose();

    // Drain hotkey requests on the render thread so the mode cycle never
    // mutates session state concurrently with the pipeline tick below.
    PluginMod::Instance().ProcessDeferredActions();
    if (g_descriptor.onFrameStart) g_descriptor.onFrameStart();

    // Null only when InitCameraPipeline refused the descriptor, or was never
    // called at all - and g_controllerHooker is unset in the same breath, so
    // this covers the InitCachedFunctions dereference below too.
    if (!g_descriptor.gate) return;

    // Counted here, ahead of the enable and gameplay gates, because it is what
    // the per-frame memos below key on and GUI draw callbacks keep firing in a
    // menu. Bumped only past the gates, the counter froze the moment the gate
    // closed and GetMarkerFocalLengths then served the last gameplay frame's
    // focal lengths for the rest of the session.
    ++g_renderFrame;

    if (!InitCachedFunctions()) return;
    if (!PluginMod::Instance().IsEnabled() || !g_descriptor.gate->IsInGameplay()) {
        SkipFrame();
        return;
    }
    EnsureCameraControllerHooked();

    // Advance interpolation + smoothing once per render frame. Every
    // downstream consumer (ApplyHeadTracking, the projection below, GUI
    // compensation) reads cached values, so the rendered camera and the
    // smoother see the same wall-clock dt.
    PluginMod::Instance().TickFrame();

    Matrix4x4f* worldMat = GetCameraWorldMatrix();
    if (!worldMat) {
        SkipFrame();
        return;
    }

    g_cleanCameraMatrix.matrix = *worldMat;
    g_cleanCameraMatrix.valid = true;

    ApplyHeadTracking(worldMat);
    g_trackingAppliedThisFrame = true;

    UpdateFrameProjection(g_cleanCameraMatrix.matrix, *worldMat);

    if (g_descriptor.onFrameApplied) {
        g_descriptor.onFrameApplied(g_cleanCameraMatrix.matrix, *worldMat);
    }
}

void CameraPipelinePostRender() {
    if (g_descriptor.onPostRestore) g_descriptor.onPostRestore();

    if (g_trackingAppliedThisFrame) {
        g_trackingAppliedThisFrame = false;

        // The pre-render callback populated the per-frame transform cache this
        // frame (g_trackingAppliedThisFrame is only set after that succeeded), so
        // this reuses it rather than re-walking the SceneManager chain.
        Matrix4x4f* worldMat = GetCameraWorldMatrix();

        // Restore the clean camera in full - POSITION as well as rotation.
        //
        // Keeping the head-tracked translation row left the game aiming off a
        // leaned eye: the shot converges on the leaned eye's axis while the round
        // leaves the un-leaned body, so reticle and impact agree at exactly one
        // range and splay apart either side of it, swapping sides as the player
        // walks through it. Head tracking must not move where bullets go.
        //
        // The lean renders on the same terms as the rotation does. Both are written
        // at the BeginRendering pre-callback and taken back at the post-callback,
        // into the same transform world matrix, and rotation is demonstrably what
        // the player sees - so whatever the renderer samples between the two hooks
        // carries the translation row as well. This has not been observed in game;
        // if the lean turns out not to render, the two hooks are the wrong pair for
        // position and nothing here can tell us that.
        if (worldMat) {
            cameraunlock::memory::SafeWrite(reinterpret_cast<std::uintptr_t>(worldMat),
                                            g_cleanCameraMatrix.matrix);
        }
    }

    g_cachedTransform = nullptr;
    g_cachedCamera = nullptr;
}

} // namespace cameraunlock::reframework
