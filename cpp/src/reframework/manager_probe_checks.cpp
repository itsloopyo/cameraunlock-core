#include <cameraunlock/reframework/manager_probe_checks.h>

#include <cameraunlock/reframework/game_state_probing.h>
#include <cameraunlock/reframework/log_callback.h>
#include <cameraunlock/reframework/managed_utils.h>

#include <reframework/API.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstring>

namespace cameraunlock::reframework {

static struct {
    ::reframework::API::Method* getGlobalSpeed = nullptr;

    // Discovered checks - each is a method whose value indicates non-gameplay.
    MethodCheck isPaused;
    MethodCheck isPlayingEvent;
    MethodCheck isOpen;
    MethodCheck isSystemFlow;
    MethodCheck isEventFlow;
    MethodCheck isPlaying;
    MethodCheck isTransition;
    MethodCheck isEventPlaying;
    MethodCheck isMoviePlaying;
    MethodCheck isInputBlocked;
    MethodCheck isCutscene;
    MethodCheck situationType;

    // GuiManager boolean fields to probe during diagnostics
    struct GuiFieldProbe {
        ::reframework::API::Method* method = nullptr;
        const char* name = nullptr;
    };
    static constexpr int MAX_GUI_PROBES = 16;
    GuiFieldProbe guiProbes[MAX_GUI_PROBES];
    int guiProbeCount = 0;
    const char* guiManagerSingleton = nullptr;

    // Int checks
    MethodCheck pauseBits;
    MethodCheck flowStatus;

    // Pointer checks
    MethodCheck playerContext;

    // Input level: GuiOpenCloseData chain
    MethodCheck guiOpenClose;
    MethodCheck inputLevel;
    const char* guiSingletonName = nullptr;

    // Camera identity tracking for cinematic detection
    ::reframework::API::Method* getGameObject = nullptr;
    void* gameplayCameraGO = nullptr;
    bool gameplayCameraLocked = false;

    // Cursor visibility debounce (diagnostic only - not suppressing)
    int cursorVisibleCount = 0;
    int cursorHiddenCount = 0;
    bool cursorSuppressing = false;
    static constexpr int CURSOR_SUPPRESS_THRESHOLD = 3;
    static constexpr int CURSOR_RESUME_THRESHOLD = 15;
} g_state;

void DiscoverManagerProbes() {
    const auto api = ::reframework::API::get().get();
    auto tdb = api->tdb();

    LogInfo("=== Begin type/method discovery ===");

    auto appType = tdb->find_type("via.Application");
    if (appType) g_state.getGlobalSpeed = appType->find_method("get_GlobalSpeed");
    LogInfo("GlobalSpeed: %s", g_state.getGlobalSpeed ? "found" : "NOT found");

    { const char* methods[] = {"get_isPaused", "get_IsPaused", "get_Paused"};
      ProbeManager(tdb, api, "PauseManager", methods, 3, g_state.isPaused, "PauseManager.isPaused"); }
    { const char* methods[] = {"get_lastPauseBits", "get_PauseBits", "get_currentPauseFlag", "get_PauseFlag"};
      ProbeManager(tdb, api, "PauseManager", methods, 4, g_state.pauseBits, "PauseManager.pauseBits"); }
    { const char* methods[] = {"get_IsPlayingEvent", "get_isPlayingEvent"};
      if (!ProbeManager(tdb, api, "GuiManager", methods, 2, g_state.isPlayingEvent, "GuiManager.IsPlayingEvent"))
          ProbeManager(tdb, api, "GUIManager", methods, 2, g_state.isPlayingEvent, "GUIManager.IsPlayingEvent"); }
    { const char* methods[] = {"get_CurrentSituationType", "get_SituationType"};
      if (!ProbeManager(tdb, api, "GuiManager", methods, 2, g_state.situationType, "GuiManager.SituationType"))
          ProbeManager(tdb, api, "GUIManager", methods, 2, g_state.situationType, "GUIManager.SituationType"); }
    { const char* methods[] = {"get_isOpen", "get_IsOpen"};
      if (!ProbeManager(tdb, api, "GuiManager", methods, 2, g_state.isOpen, "GuiManager.isOpen"))
          ProbeManager(tdb, api, "GUIManager", methods, 2, g_state.isOpen, "GUIManager.isOpen"); }
    { const char* methods[] = {"get_isEnableSystemFlow", "get_IsEnableSystemFlow"};
      if (!ProbeManager(tdb, api, "GuiManager", methods, 2, g_state.isSystemFlow, "GuiManager.isEnableSystemFlow"))
          ProbeManager(tdb, api, "GUIManager", methods, 2, g_state.isSystemFlow, "GUIManager.isEnableSystemFlow"); }
    { const char* methods[] = {"get_isEnableEventFlow", "get_IsEnableEventFlow"};
      if (!ProbeManager(tdb, api, "GuiManager", methods, 2, g_state.isEventFlow, "GuiManager.isEnableEventFlow"))
          ProbeManager(tdb, api, "GUIManager", methods, 2, g_state.isEventFlow, "GUIManager.isEnableEventFlow"); }
    { const char* methods[] = {"get_isPlaying", "get_IsPlaying"};
      if (!ProbeManager(tdb, api, "SequenceManager", methods, 2, g_state.isPlaying, "SequenceManager.isPlaying"))
          ProbeManager(tdb, api, "CutsceneManager", methods, 2, g_state.isPlaying, "CutsceneManager.isPlaying"); }
    { const char* methods[] = {"get_isRunningTransition", "get_IsRunningTransition"};
      ProbeManager(tdb, api, "SceneTransitionManager", methods, 2, g_state.isTransition, "SceneTransitionManager.isRunningTransition"); }
    { const char* methods[] = {"get_Status", "get_CurrentStatus", "get_status"};
      ProbeManager(tdb, api, "GameFlowManager", methods, 3, g_state.flowStatus, "GameFlowManager.Status"); }
    { const char* methods[] = {"getPlayerContextRef", "get_PlayerContextRef", "get_playerContextRef"};
      ProbeManager(tdb, api, "CharacterManager", methods, 3, g_state.playerContext, "CharacterManager.playerCtx"); }
    if (!g_state.playerContext.method) {
        const char* methods[] = {"getCurrentSurvivor", "get_CurrentSurvivor"};
        ProbeManager(tdb, api, "survivor.SurvivorManager", methods, 2, g_state.playerContext, "SurvivorManager.currentSurvivor");
    }

    // GuiOpenCloseData chain
    {
        const char* guiNames[] = {"GuiManager", "GUIManager"};
        for (auto gn : guiNames) {
            auto guiType = FindType(tdb, gn);
            if (!guiType) continue;
            auto openCloseMethod = guiType->find_method("get_GuiOpenCloseData");
            if (!openCloseMethod) openCloseMethod = guiType->find_method("get_guiOpenCloseData");
            if (!openCloseMethod) continue;

            auto sn = FindSingleton(api, gn);
            if (!sn) continue;

            const char* ocdNames[] = { "gui.GuiOpenCloseData", "GuiOpenCloseData" };
            for (auto ocdn : ocdNames) {
                auto ocdType = FindType(tdb, ocdn);
                if (!ocdType) continue;
                const char* ilMethods[] = {"get_CurrActiveInputevel", "get_CurrActiveInputLevel"};
                auto ilMethod = FindMethod(ocdType, ilMethods, 2);
                if (!ilMethod) continue;

                static char guiSnBuf[256];
                strncpy(guiSnBuf, sn, 255);
                guiSnBuf[255] = '\0';
                g_state.guiSingletonName = guiSnBuf;
                g_state.guiOpenClose.method = openCloseMethod;
                g_state.inputLevel.method = ilMethod;
                LogInfo("Probe OK: GuiOpenCloseData -> CurrActiveInputLevel (singleton: %s)", guiSnBuf);
                goto doneOpenClose;
            }
        }
        doneOpenClose:;
    }

    // EventManager / MovieManager
    { const char* methods[] = {"get_isPlaying", "get_IsPlaying", "get_isEventPlaying", "get_IsEventPlaying"};
      if (!ProbeManager(tdb, api, "EventManager", methods, 4, g_state.isEventPlaying, "EventManager.isPlaying"))
          ProbeManager(tdb, api, "event.EventManager", methods, 4, g_state.isEventPlaying, "event.EventManager.isPlaying"); }
    { const char* methods[] = {"get_isPlaying", "get_IsPlaying", "get_isActive", "get_IsActive"};
      if (!ProbeManager(tdb, api, "MovieManager", methods, 4, g_state.isMoviePlaying, "MovieManager.isPlaying"))
          ProbeManager(tdb, api, "CinematicManager", methods, 4, g_state.isMoviePlaying, "CinematicManager.isPlaying"); }

    // InputManager / PlayerManager
    { const char* types[] = {"InputManager", "InputSystem", "PlayerInputManager", "PlayerManager"};
      const char* methods[] = {
          "get_isInputBlocked", "get_IsInputBlocked", "get_isBlocked",
          "get_isLocked", "get_IsLocked", "get_isPlayerControllable",
          "get_IsPlayerControllable", "get_isEnableInput", "get_IsEnableInput",
          "get_isDisableInput", "get_IsDisableInput",
      };
      for (auto tn : types) {
          if (g_state.isInputBlocked.method) break;
          ProbeManager(tdb, api, tn, methods, 11, g_state.isInputBlocked, "InputBlock");
      }
    }

    // Broader cutscene/event probes
    { const char* types[] = {
          "CutSceneManager", "CutsceneController", "EventSceneManager",
          "DemoManager", "StoryManager", "ScenarioManager", "PlayEventManager",
      };
      const char* methods[] = {
          "get_isPlaying", "get_IsPlaying", "get_isActive", "get_IsActive",
          "get_isRunning", "get_IsRunning",
      };
      for (auto tn : types) {
          if (g_state.isCutscene.method) break;
          ProbeManager(tdb, api, tn, methods, 6, g_state.isCutscene, tn);
      }
    }

    // GuiManager boolean probes for diagnostics
    {
        auto guiType = FindType(tdb, "GuiManager");
        if (guiType) {
            auto sn = FindSingleton(api, "GuiManager");
            if (sn) {
                static char guiSnBuf2[256];
                strncpy(guiSnBuf2, sn, 255);
                guiSnBuf2[255] = '\0';
                g_state.guiManagerSingleton = guiSnBuf2;

                const char* boolGetters[] = {
                    "get_Initialized", "get_IsSystemReady",
                    "get_IsFirstTimeItemGetRunning",
                    "get_canPauseInDemo", "get_canDemoSkip",
                    "get_IsQuickSaveExists", "get_IsOpenWorldMap",
                    "get_IsPause", "get_IsPaused", "get_isPause", "get_isPaused",
                    "get_IsMenu", "get_isMenu", "get_IsMenuOpen", "get_isMenuOpen",
                    "get_IsInventory", "get_isInventory", "get_IsInventoryOpen",
                    "get_IsMapOpen", "get_isMapOpen",
                    "get_IsEventSkip", "get_IsSubtitle",
                    "get_IsLoading", "get_isLoading",
                    "get_IsGameOver", "get_isGameOver",
                    "get_IsOption", "get_isOption",
                };
                for (auto getter : boolGetters) {
                    if (g_state.guiProbeCount >= g_state.MAX_GUI_PROBES) break;
                    auto m = guiType->find_method(getter);
                    if (m) {
                        g_state.guiProbes[g_state.guiProbeCount].method = m;
                        g_state.guiProbes[g_state.guiProbeCount].name = getter;
                        g_state.guiProbeCount++;
                        LogInfo("GuiManager probe found: %s", getter);
                    }
                }
                LogInfo("GuiManager: %d boolean probes found", g_state.guiProbeCount);
            }
        }
    }

    auto camType = tdb->find_type("via.Camera");
    if (camType) g_state.getGameObject = camType->find_method("get_GameObject");

    LogInfo("=== End type/method discovery ===");
}

// The cursor debounce is diagnostic only. It used to suppress tracking, and the
// suppression fired on the cursor flicker every RE Engine title emits during
// normal play, so tracking dropped out mid-fight. The counters stay because the
// diagnostic line is what identified that; nothing reads cursorSuppressing.
static void RefreshCursorDiagnostic(bool diag) {
    CURSORINFO ci = {};
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci)) return;

    bool cursorVisible = (ci.flags & CURSOR_SHOWING) != 0;
    if (cursorVisible) {
        g_state.cursorVisibleCount++;
        g_state.cursorHiddenCount = 0;
    } else {
        g_state.cursorHiddenCount++;
        if (g_state.cursorHiddenCount >= g_state.CURSOR_RESUME_THRESHOLD) {
            g_state.cursorVisibleCount = 0;
            g_state.cursorSuppressing = false;
        }
    }
    if (g_state.cursorVisibleCount >= g_state.CURSOR_SUPPRESS_THRESHOLD) {
        g_state.cursorSuppressing = true;
    }
    if (diag) LogInfo("Diag: cursor=%d vis=%d hid=%d suppress=%d (NOT SUPPRESSING)",
        cursorVisible ? 1 : 0, g_state.cursorVisibleCount,
        g_state.cursorHiddenCount, g_state.cursorSuppressing ? 1 : 0);
}

bool ManagerProbeGameplayCheck(void* primaryCamera, bool diag, const char** reason) {
    const auto api = ::reframework::API::get().get();
    auto vmCtx = api->get_vm_context();

    if (diag && g_state.getGameObject) {
        __try {
            auto camGO = g_state.getGameObject->call<void*>(vmCtx, primaryCamera);
            LogInfo("Diag: cameraGO=%p (gameplay=%p, locked=%d)",
                camGO, g_state.gameplayCameraGO, g_state.gameplayCameraLocked ? 1 : 0);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    if (g_state.getGlobalSpeed) {
        __try {
            auto app = api->get_native_singleton("via.Application");
            if (app) {
                float speed = g_state.getGlobalSpeed->call<float>(vmCtx, app);
                if (diag) LogInfo("Diag: GlobalSpeed=%.3f", speed);
                if (speed <= 0.001f) { *reason = "time stopped"; return false; }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            g_state.getGlobalSpeed = nullptr;
        }
    }

    // SituationType: -1 = Normal, 0 and above = a cut scene or menu situation.
    if (g_state.situationType.method && !g_state.situationType.failed) {
        void* guiMgr = api->get_managed_singleton(g_state.situationType.singletonName);
        if (guiMgr) {
            __try {
                auto ret = g_state.situationType.method->invoke(
                    reinterpret_cast<::reframework::API::ManagedObject*>(guiMgr), EmptyArgs());
                int32_t sitType = static_cast<int32_t>(ret.dword);
                if (diag) LogInfo("Diag: situationType=%d", sitType);
                if (sitType >= 0) { *reason = "situation type"; return false; }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                g_state.situationType.failed = true;
            }
        }
    }

    if (InvokeBool(api, vmCtx, g_state.isPaused, diag, "isPaused")) { *reason = "paused"; return false; }
    if (InvokeBool(api, vmCtx, g_state.isPlayingEvent, diag, "isPlayingEvent")) { *reason = "event playing"; return false; }
    if (InvokeBool(api, vmCtx, g_state.isOpen, diag, "isOpen")) { *reason = "gui open"; return false; }
    if (InvokeBool(api, vmCtx, g_state.isSystemFlow, diag, "isSystemFlow")) { *reason = "system flow"; return false; }
    if (InvokeBool(api, vmCtx, g_state.isEventFlow, diag, "isEventFlow")) { *reason = "event flow"; return false; }
    if (InvokeBool(api, vmCtx, g_state.isPlaying, diag, "isPlaying")) { *reason = "sequence playing"; return false; }
    if (InvokeBool(api, vmCtx, g_state.isTransition, diag, "isTransition")) { *reason = "scene transition"; return false; }
    if (InvokeBool(api, vmCtx, g_state.isEventPlaying, diag, "isEventPlaying")) { *reason = "event playing"; return false; }
    if (InvokeBool(api, vmCtx, g_state.isMoviePlaying, diag, "isMoviePlaying")) { *reason = "movie playing"; return false; }
    if (InvokeBool(api, vmCtx, g_state.isInputBlocked, diag, "isInputBlocked")) { *reason = "input blocked"; return false; }
    if (InvokeBool(api, vmCtx, g_state.isCutscene, diag, "isCutscene")) { *reason = "cutscene"; return false; }

    if (InvokeInt(api, vmCtx, g_state.pauseBits, diag, "pauseBits") != 0) { *reason = "paused"; return false; }

    if (g_state.flowStatus.method && !g_state.flowStatus.failed) {
        uint32_t status = InvokeInt(api, vmCtx, g_state.flowStatus, diag, "flowStatus");
        if (g_state.flowStatus.method && status < 2) { *reason = "game flow"; return false; }
    }

    if (!InvokePointer(api, vmCtx, g_state.playerContext, diag, "playerCtx")) {
        *reason = "no player (menu/loading)";
        return false;
    }

    if (g_state.guiOpenClose.method && g_state.inputLevel.method &&
        !g_state.guiOpenClose.failed && !g_state.inputLevel.failed && g_state.guiSingletonName) {
        bool blocked = false;
        __try {
            void* guiMgr = api->get_managed_singleton(g_state.guiSingletonName);
            if (guiMgr) {
                auto ocd = g_state.guiOpenClose.method->invoke(
                    reinterpret_cast<::reframework::API::ManagedObject*>(guiMgr), EmptyArgs());
                if (ocd.ptr) {
                    auto lvl = g_state.inputLevel.method->invoke(
                        reinterpret_cast<::reframework::API::ManagedObject*>(ocd.ptr), EmptyArgs());
                    if (diag) LogInfo("Diag: inputLevel=%u", lvl.dword);
                    blocked = lvl.dword > 0;
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            g_state.guiOpenClose.failed = true;
            LogWarning("GuiOpenCloseData chain crashed, disabling");
        }
        if (blocked) { *reason = "menu input level"; return false; }
    }

    RefreshCursorDiagnostic(diag);

    if (diag && g_state.guiManagerSingleton && g_state.guiProbeCount > 0) {
        void* guiMgr = api->get_managed_singleton(g_state.guiManagerSingleton);
        if (guiMgr) {
            for (int i = 0; i < g_state.guiProbeCount; i++) {
                __try {
                    auto ret = g_state.guiProbes[i].method->invoke(
                        reinterpret_cast<::reframework::API::ManagedObject*>(guiMgr), EmptyArgs());
                    LogInfo("Diag: %s = %u", g_state.guiProbes[i].name, ret.dword);
                } __except(EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
    }

    // Latch the GameObject the gameplay camera hangs off, once. Diagnostic: a
    // cinematic that swaps the primary camera shows up as a different pointer
    // in the per-transition diag burst above.
    if (g_state.getGameObject && !g_state.gameplayCameraLocked) {
        __try {
            auto go = g_state.getGameObject->call<void*>(vmCtx, primaryCamera);
            if (go) {
                g_state.gameplayCameraGO = go;
                g_state.gameplayCameraLocked = true;
                LogInfo("Gameplay camera locked: GO=%p", go);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    return true;
}

} // namespace cameraunlock::reframework
