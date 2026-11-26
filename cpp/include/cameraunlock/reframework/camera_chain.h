#pragma once

#include <reframework/API.hpp>
#include <cstddef>

#include "re_math.h"

namespace cameraunlock::reframework {

// World-matrix byte offset within via.Transform. Identical across every RE
// Engine title shipped so far (RE2/RE3/RE4/RE7/Village/Requiem).
inline constexpr ptrdiff_t kTransformWorldMatrixOffset = 0x80;

// Cached resolver for the standard RE Engine primary-camera transform chain:
//   via.SceneManager -> get_MainView -> get_PrimaryCamera
//                    -> get_GameObject -> get_Transform
// The chain is identical across RE Engine titles. Method handles are resolved
// once from the TDB; each per-call walk is SEH-guarded so camera objects torn
// down during scene transitions yield nullptr instead of crashing the game.
class CameraTransformResolver {
public:
    // Resolve the chain's TDB method handles. Latches failure if any type or
    // method is missing. Safe to call repeatedly; resolution happens once.
    bool Initialize();
    bool HasFailed() const { return m_failed; }

    // Walk the full chain. Returns nullptr if uninitialized or any link fails.
    // When outCamera is non-null it also receives the intermediate primary
    // via.Camera from the same walk, so callers that need both (per-frame
    // transform writes plus FOV reads) avoid re-walking the first two links.
    void* ResolveTransform(void** outCamera = nullptr);

    // Walk the chain only as far as the primary via.Camera object.
    void* ResolveCamera();

    // ResolveTransform() reinterpreted as the world matrix at the given byte
    // offset within the via.Transform.
    Matrix4x4f* ResolveWorldMatrix(ptrdiff_t worldMatrixOffset = kTransformWorldMatrixOffset);

    // Read via.Camera.get_FOV off the primary camera. RE Engine declares the
    // return as Single in the TDB but the native ABI hands back a double, so
    // both representations are tried against the plausible [10, 170] degree
    // range. Returns 0 when the chain, the method, or the range check fails.
    // Pass a camera previously obtained from ResolveTransform/ResolveCamera to
    // skip the chain walk; with nullptr the chain is walked fresh.
    float ResolveFovDegrees(void* camera = nullptr);

private:
    ::reframework::API::Method* m_getMainView = nullptr;
    ::reframework::API::Method* m_getPrimaryCamera = nullptr;
    ::reframework::API::Method* m_getGameObject = nullptr;
    ::reframework::API::Method* m_getTransform = nullptr;
    ::reframework::API::Method* m_getFov = nullptr;
    bool m_initialized = false;
    bool m_failed = false;
};

} // namespace cameraunlock::reframework
