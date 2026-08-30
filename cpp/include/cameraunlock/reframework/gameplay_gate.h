#pragma once

#include <reframework/API.hpp>

namespace cameraunlock::reframework {

// Per-game "is this active gameplay" body, run only after the gate has already
// confirmed a primary camera exists; `primaryCamera` is that camera, so a body
// that inspects it does not re-walk the SceneManager chain.
//
// Return false to suppress tracking. `reason` may be pointed at a static string
// naming the suppression, which the gate prints when it logs the transition out
// of gameplay. `diag` is true for a short burst of refreshes after each
// transition, for bodies that dump probe values.
using GameplayCheckFn = bool (*)(void* primaryCamera, bool diag, const char** reason);

// The gameplay gate every RE Engine head-tracking plugin runs in front of its
// camera hook: a 100 ms refresh throttle, the shared primary-camera existence
// check, one per-game check body, and the entered/left transition logging.
//
// Discovery is deferred to the first refresh rather than run at plugin init:
// the managers a check body resolves are only present once the game's TDB is
// warm.
// Resolve via.SceneManager.get_MainView and via.SceneView.get_PrimaryCamera.
// Shared by the gate's tier-1 check and by check bodies that need the live
// primary camera themselves.
struct PrimaryCameraProbe {
    ::reframework::API::Method* getMainView = nullptr;
    ::reframework::API::Method* getPrimaryCamera = nullptr;

    void Resolve(::reframework::API::TDB* tdb);

    // Walk SceneManager -> MainView -> PrimaryCamera. Returns nullptr when any
    // link is missing, which is what a menu or a loading screen looks like.
    void* ResolvePrimaryCamera(const ::reframework::API* api) const;
};

class GameplayGate {
public:
    // `discover` resolves the check body's types and methods, and runs once.
    // Either function may be null.
    GameplayGate(void (*discover)(), GameplayCheckFn check)
        : m_discover(discover), m_check(check) {}

    GameplayGate(const GameplayGate&) = delete;
    GameplayGate& operator=(const GameplayGate&) = delete;

    // Refresh if the throttle has elapsed, then report the cached state.
    bool IsInGameplay();

    // Refresh the cached state if the throttle has elapsed.
    void Refresh();

    // A title / main-menu / loading GUI element drew this frame. Menus on these
    // titles render over a live 3D backdrop that passes every other tier, so a
    // recent menu draw is the one unambiguous "not gameplay" signal. Games that
    // never call this are unaffected: the gate suppresses nothing until the
    // first notification arrives.
    //
    // Called from the GUI draw hook on the render thread; read on the same
    // thread by Refresh().
    void NotifyMenuDrawn();

private:
    void (*m_discover)();
    GameplayCheckFn m_check;

    PrimaryCameraProbe m_probe;
    bool m_inGameplay = false;
    bool m_wasInGameplay = false;
    bool m_discovered = false;
    unsigned long long m_lastCheckMs = 0;
    unsigned long long m_lastMenuDrawMs = 0;
    int m_diagBurstRemaining = 0;
};

} // namespace cameraunlock::reframework
