#include <cameraunlock/reframework/camera_pipeline.h>

#include <cameraunlock/math/smoothing_utils.h>
#include <cameraunlock/memory/safe_memory.h>
#include <cameraunlock/reframework/camera_controller_hook.h>
#include <cameraunlock/reframework/log_callback.h>
#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/reframework/plugin_mod.h>
#include <cameraunlock/rendering/gui_marker_compensation.h>
#include <cameraunlock/time/qpc_clock.h>

#include <reframework/API.hpp>

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

// Per-frame transform + camera cache. Both are invalidated together at the
// camera-controller update pre-hook and at the end of the post-render callback,
// so within one render frame they hold the live primary camera and its
// transform without re-walking the SceneManager chain.
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

    if (!hasRotation && !hasPosition) return;

    // The pre-rotation axes are only read by the position offset below, so
    // capture them only when that branch will run.
    Matrix4x4f preRotationAxes;
    if (hasPosition) preRotationAxes = *worldMat;

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

    if (hasPosition) {
        ApplyViewSpacePositionOffset(*worldMat, preRotationAxes, px, py, pz);
    }
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
    g_descriptor = descriptor;
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

const FrameProjection& GetFrameProjection() { return g_projection; }
uint64_t GetRenderFrame() { return g_renderFrame; }
const Matrix4x4f& GetCleanCameraMatrix() { return g_cleanCameraMatrix.matrix; }
bool IsCleanCameraMatrixValid() { return g_cleanCameraMatrix.valid; }
CameraTransformResolver& GetCameraResolver() { return g_cameraResolver; }
void* GetCachedCamera() { return g_cachedCamera; }

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
        // Nothing was projected this frame, so nothing derived from a projection
        // is usable. Leaving these true hands a GUI consumer in a menu the last
        // gameplay frame's offsets.
        g_projection.markerValid = false;
        g_projection.aimValid = false;
        g_projection.cleanToHeadValid = false;
        return;
    }
    EnsureCameraControllerHooked();

    // Advance interpolation + smoothing once per render frame. Every
    // downstream consumer (ApplyHeadTracking, the projection below, GUI
    // compensation) reads cached values, so the rendered camera and the
    // smoother see the same wall-clock dt.
    PluginMod::Instance().TickFrame();

    Matrix4x4f* worldMat = GetCameraWorldMatrix();
    if (!worldMat) return;

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

    if (!g_trackingAppliedThisFrame) return;
    g_trackingAppliedThisFrame = false;

    if (!g_cleanCameraMatrix.valid) return;

    // The pre-render callback populated the per-frame transform cache this
    // frame (g_trackingAppliedThisFrame is only set after that succeeded), so
    // reuse it rather than re-walking the SceneManager chain.
    Matrix4x4f* worldMat = GetCameraWorldMatrix();
    if (!worldMat) return;

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
    cameraunlock::memory::SafeWrite(reinterpret_cast<std::uintptr_t>(worldMat),
                                    g_cleanCameraMatrix.matrix);

    g_cachedTransform = nullptr;
    g_cachedCamera = nullptr;
}

} // namespace cameraunlock::reframework
