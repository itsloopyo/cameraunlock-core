#pragma once

#include <reframework/API.hpp>

namespace cameraunlock::reframework {

// True when an RE Engine component type name looks like a render/post-process
// effect controller (DOF, fog, bloom, tone-map, ...). These share the
// "Camera*Controller" naming shape with gameplay camera controllers, but
// hooking one places a pre/post save-restore sandwich at the wrong point in
// the frame (e.g. app.CameraDOFController updates long before the gameplay
// camera writes its transform).
bool IsEffectControllerName(const char* typeName);

// Discovers and hooks the game's player-camera-controller update method for
// the pre/post save-restore sandwich (aim decoupling).
//
// Discovery order per TryHook() call:
//   1. Game-specific fully-qualified candidate type names (fast path).
//   2. Namespace-agnostic TDB scan for types short-named
//      "PlayerCameraController" (titles rename the namespace across
//      releases, not the type).
//   3. Parent-chain walk from the primary camera transform: accept the first
//      component whose type looks like a camera controller and is not an
//      effect controller, logging every component seen so an unrecognized
//      game's real controller type can be promoted to the fast path later.
//
// At the main menu the primary camera GameObject typically carries only
// render/effect controllers; the real player camera controller component
// exists once gameplay starts. Call TryHook() each gameplay frame until it
// returns true rather than latching discovery at plugin init.
class CameraControllerHooker {
public:
    // candidateTypes: game-specific fully-qualified type names tried first
    // (e.g. "app.ropeway.camera.PlayerCameraController"). The array must
    // outlive the hooker. preHook/postHook are installed on the discovered
    // update method via REFramework's add_hook.
    CameraControllerHooker(const char* const* candidateTypes, int candidateTypeCount,
                           REFPreHookFn preHook, REFPostHookFn postHook)
        : m_candidateTypes(candidateTypes),
          m_candidateTypeCount(candidateTypeCount),
          m_preHook(preHook),
          m_postHook(postHook) {}

    // Removes the managed hook if one is installed. Without this a REFramework
    // plugin reload leaves the game calling preHook at an address in freed
    // memory.
    ~CameraControllerHooker() { Unhook(); }

    CameraControllerHooker(const CameraControllerHooker&) = delete;
    CameraControllerHooker& operator=(const CameraControllerHooker&) = delete;

    // Attempt discovery and hooking. cameraTransform is the primary camera's
    // via.Transform managed object (for the parent-chain walk); pass nullptr
    // to limit discovery to the candidate-type and TDB short-name fast paths.
    // Returns true once hooked; further calls after success are no-ops
    // returning true.
    bool TryHook(void* cameraTransform);

    // Remove the installed hook. Safe to call when nothing is hooked; after it
    // returns, TryHook() will search again.
    void Unhook();

    bool IsHooked() const { return m_hooked; }
    int AttemptCount() const { return m_attempts; }

private:
    bool TryHookTypeDef(::reframework::API::TypeDefinition* type, const char* fullTypeName);
    bool TryHookType(const char* fullTypeName);
    bool WalkParentChain(void* cameraTransform);

    const char* const* m_candidateTypes;
    int m_candidateTypeCount;
    REFPreHookFn m_preHook;
    REFPostHookFn m_postHook;
    ::reframework::API::Method* m_hookedMethod = nullptr;
    unsigned int m_hookId = 0;
    bool m_hooked = false;
    int m_attempts = 0;
};

} // namespace cameraunlock::reframework
