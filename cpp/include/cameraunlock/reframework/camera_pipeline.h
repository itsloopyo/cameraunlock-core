#pragma once

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
    // value. What that leaves uncorrected fades with distance, and markers are
    // mostly distant.
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
};

// Install the descriptor. Call once, from plugin initialization.
void InitCameraPipeline(const CameraPipelineDescriptor& descriptor);

// REFramework BeginRendering callbacks. Pre applies head tracking to the
// primary camera transform; post hands the game back exactly the camera it
// computed, position row included, so aim, raycasts and physics never see
// head-tracked state.
void CameraPipelinePreRender();
void CameraPipelinePostRender();

const FrameProjection& GetFrameProjection();

// Bumped once per processed render frame. GUI draw callbacks fire during that
// same frame, so this is the key per-frame memos invalidate on.
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
