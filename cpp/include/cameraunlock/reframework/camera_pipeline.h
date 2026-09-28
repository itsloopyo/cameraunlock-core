#pragma once

#include <cameraunlock/camera/lean_clamp.h>
#include <cameraunlock/reframework/camera_chain.h>
#include <cameraunlock/reframework/gameplay_gate.h>
#include <cameraunlock/reframework/re_math.h>

#include <cstdint>

namespace cameraunlock::reframework {

// Smoothing applied to the derived screen-space projection values. Deliberately
// independent of the user's tracking smoothing: it exists to take perspective-
// division and per-frame FOV noise out of the reticle and marker offsets, not
// to shape the head pose.
inline constexpr float kProjectionSmoothing = 0.15f;

// Half of the 1920x1080 reference canvas the GUI compensation projects against.
// Multiplying NDC focal factors by these yields pixel focal lengths.
inline constexpr float kHalfReferenceCanvasWidth = 960.f;
inline constexpr float kHalfReferenceCanvasHeight = 540.f;

// Screen-space state derived once per render frame from the clean and
// head-tracked camera matrices, read by every GUI consumer in that frame.
struct FrameProjection {
    // Rotation-only tangents of the clean view forward under head rotation.
    // Carry no lean term: parallax is lean/depth, a marker sits at its own
    // depth, and one write to a marker's container cannot express a per-depth
    // value. A consumer holding a single marker whose depth it can name adds
    // the lean itself from cleanLocalPositionDelta below; one shifting several
    // markers at once has no right value and correctly leaves it out.
    float markerTanRight = 0.f;
    float markerTanUp = 0.f;
    bool markerValid = false;

    // Tangents of the clean aim point at the descriptor's aim distance,
    // projected into the head-tracked view. Only computed when the descriptor
    // sets a non-zero aim distance.
    float aimTanRight = 0.f;
    float aimTanUp = 0.f;
    bool aimValid = false;

    float fovDegrees = 75.f;
    float rollDegrees = 0.f;

    // C = R_head * R_clean^T, mapping directions from clean camera space into
    // head camera space.
    float cleanToHead[3][3] = {};
    bool cleanToHeadValid = false;

    // The head's translation away from the clean camera, in metres, in the clean
    // camera's own axes. Subtract it from a depth-scaled anchor ray before
    // projecting through cleanToHead to pin a world anchor under a lean. Valid
    // whenever cleanToHeadValid; zero when position tracking is off.
    float cleanLocalPositionDelta[3] = {};
};

struct CameraPipelineDescriptor {
    // Fully-qualified player-camera-controller type names tried first. The
    // array must outlive the process.
    const char* const* controllerCandidateTypes = nullptr;
    int controllerCandidateCount = 0;

    // Try the candidate / TDB short-name fast paths at plugin init. Games whose
    // controller types exist in the TDB before gameplay starts hook here; the
    // rest wait for a gameplay frame, because at the main menu the primary
    // camera GameObject carries only render/effect controllers.
    bool hookControllerAtInit = false;

    // Frames to wait between discovery retries once gameplay has started. 0
    // retries every frame. A cooldown bounds the per-attempt component logging
    // the hooker's parent-chain walk produces on a game where nothing matches.
    int hookRetryCooldownFrames = 0;

    // Range to the aimed-at point, in metres, for the reticle projection. 0
    // leaves FrameProjection::aimValid false and skips the work.
    float aimDistanceMeters = 0.f;

    // Gameplay gate consulted before any camera write. Required.
    GameplayGate* gate = nullptr;

    // Resolve game-specific methods, once, after the camera chain resolves.
    void (*onInit)() = nullptr;

    // Run at the top of the render callback, before every gate. Games with
    // their own deferred hotkey actions drain them here.
    void (*onFrameStart)() = nullptr;

    // Run after head tracking has been written and FrameProjection updated.
    void (*onFrameApplied)(const Matrix4x4f& clean, const Matrix4x4f& head) = nullptr;

    // Run at the top of the post-render callback, before the clean restore, so
    // a game that moved something else (a light, a weapon) can put it back.
    void (*onPostRestore)() = nullptr;

    // The game's own aim state, polled once per gameplay frame, never latched.
    // Set, the lean eases out while this is true and PluginMod::IsTrueFreeLook
    // is false (sights locked), on ads/ads_fade.h's timing, and stays in full in
    // true free look. Rotation is never touched. A frame it cannot read reports
    // false, so the lean returns. Null leaves the lean alone.
    bool (*isAiming)() = nullptr;

    // The rig: the transform the camera, the arms, the held weapon and the
    // round's start point all hang off (the shooter-ads-handling skill, "Carry
    // the lean on the rig"). With isAiming set, sights locked hands the lean
    // over to it as the sights come up. writeRig is called from the
    // LateUpdateBehavior pre-callback with the world-space offset to add, so the
    // game places the camera, the weapon and the next shot from the moved rig,
    // and returns false when it wrote nothing. restoreRig takes that exact
    // offset back off, and is called from the EndRendering post-callback, or
    // before the next write if that callback never came. rigAvailable is polled
    // at BeginRendering: false wherever the rig must stay where the game put it,
    // and the lean then eases out on the camera instead. All three or none.
    bool (*rigAvailable)() = nullptr;
    bool (*writeRig)(const float worldOffset[3]) = nullptr;
    void (*restoreRig)() = nullptr;

    // The engine's lean collision query (camera/lean_clamp.h), with metres and
    // world space throughout. Set, and with [Position] CollisionEnabled true, the
    // whole lean is clamped against the world from the un-leaned eye before it
    // is split between the camera and the rig.
    cameraunlock::camera::LeanQueryFn leanQuery = nullptr;

    // The game's un-zoomed field of view, in the units via.Camera.get_FOV reports.
    // Set, yaw, pitch and the lean are scaled so a head movement moves the picture
    // as far as it would at that field of view (camera/zoom_compensation.h). Roll
    // is not scaled. False on a frame it cannot answer, which applies no
    // compensation that frame.
    bool (*unzoomedFovDegrees)(float& out) = nullptr;
};

// Install the descriptor. Call once, from plugin initialization.
void InitCameraPipeline(const CameraPipelineDescriptor& descriptor);

// REFramework BeginRendering callbacks. Pre applies head tracking to the
// primary camera transform; post hands the game back exactly the camera it
// computed, position row included, so aim, raycasts and physics never see
// head-tracked state.
void CameraPipelinePreRender();
void CameraPipelinePostRender();

// The rig callbacks, registered on LateUpdateBehavior (pre) and EndRendering
// (post) only when the descriptor sets writeRig.
void CameraPipelinePreLateUpdate();
void CameraPipelinePostEndRendering();

const FrameProjection& GetFrameProjection();

// Bumped once per render callback, before the enable and gameplay gates. GUI
// draw callbacks fire during that same frame - including in a menu, where no
// tracking is applied - so this is the key per-frame memos invalidate on.
uint64_t GetRenderFrame();

// The clean camera matrix saved before head tracking was applied this frame.
const Matrix4x4f& GetCleanCameraMatrix();
bool IsCleanCameraMatrixValid();

// Shared resolver for the primary camera chain (transform, camera, live FOV).
CameraTransformResolver& GetCameraResolver();

// The primary via.Camera resolved alongside the transform this frame, or
// nullptr before the first resolve.
void* GetCachedCamera();

// Pixel focal lengths for GUI compensation on the reference canvas, memoized
// per render frame. Prefers the camera's projection matrix (exact per-axis
// scale, no FOV-convention guess) over a get_FOV derivation.
bool GetMarkerFocalLengths(float& fx, float& fy);

} // namespace cameraunlock::reframework
