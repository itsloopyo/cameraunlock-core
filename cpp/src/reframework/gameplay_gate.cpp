#include <cameraunlock/reframework/gameplay_gate.h>

#include <cameraunlock/reframework/log_callback.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace cameraunlock::reframework {

// Refresh interval. The camera hook asks every render frame; the managed
// invokes behind a check body cost far more than the answer changes.
constexpr unsigned long long kCheckIntervalMs = 100;

// How long a menu draw keeps suppressing after the last one, so a single
// missed frame does not flicker tracking back on.
constexpr unsigned long long kMenuHoldMs = 300;

// Refreshes after a transition during which check bodies dump probe values.
constexpr int kDiagBurstRefreshes = 5;

void PrimaryCameraProbe::Resolve(::reframework::API::TDB* tdb) {
    auto smType = tdb->find_type("via.SceneManager");
    if (smType) getMainView = smType->find_method("get_MainView");
    auto svType = tdb->find_type("via.SceneView");
    if (svType) getPrimaryCamera = svType->find_method("get_PrimaryCamera");
}

void* PrimaryCameraProbe::ResolvePrimaryCamera(const ::reframework::API* api) const {
    if (!getMainView || !getPrimaryCamera) return nullptr;
    auto sceneManager = api->get_native_singleton("via.SceneManager");
    if (!sceneManager) return nullptr;
    auto vmCtx = api->get_vm_context();
    auto mainView = getMainView->call<void*>(vmCtx, sceneManager);
    if (!mainView) return nullptr;
    return getPrimaryCamera->call<void*>(vmCtx, mainView);
}

void GameplayGate::NotifyMenuDrawn() {
    m_lastMenuDrawMs = GetTickCount64();
}

void GameplayGate::Refresh() {
    unsigned long long now = GetTickCount64();
    if (now - m_lastCheckMs < kCheckIntervalMs) return;
    m_lastCheckMs = now;

    const auto api = ::reframework::API::get().get();
    if (!api) {
        m_inGameplay = false;
        return;
    }

    if (!m_discovered) {
        m_discovered = true;
        m_probe.Resolve(api->tdb());
        if (m_discover) m_discover();
    }

    bool diag = m_diagBurstRemaining > 0;
    if (diag) m_diagBurstRemaining--;

    const char* reason = nullptr;
    bool newState = false;

    if (void* camera = m_probe.ResolvePrimaryCamera(api)) {
        newState = m_check ? m_check(camera, diag, &reason) : true;
    } else {
        reason = "no camera";
    }

    // A menu draw overrides a positive check: these titles render their title,
    // pause and loading screens over a live 3D backdrop that satisfies every
    // manager probe, so the draw is the only signal that separates them.
    if (newState && m_lastMenuDrawMs != 0 && (now - m_lastMenuDrawMs) < kMenuHoldMs) {
        newState = false;
        reason = "main menu";
        if (diag) LogInfo("Diag: suppressed by main-menu signal");
    }

    m_inGameplay = newState;

    if (m_inGameplay && !m_wasInGameplay) {
        m_diagBurstRemaining = kDiagBurstRefreshes;
        LogInfo("Game state: entered gameplay");
    } else if (!m_inGameplay && m_wasInGameplay) {
        m_diagBurstRemaining = kDiagBurstRefreshes;
        LogInfo("Game state: left gameplay (%s)", reason ? reason : "unknown");
    }
    m_wasInGameplay = m_inGameplay;
}

bool GameplayGate::IsInGameplay() {
    Refresh();
    return m_inGameplay;
}

} // namespace cameraunlock::reframework
