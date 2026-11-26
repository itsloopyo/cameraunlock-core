#pragma once

namespace cameraunlock::reframework {

// Gameplay check body for titles whose managers have to be found by probing
// rather than named outright.
//
// RE7, Village and Requiem all reached the same place: their manager types and
// method names differ from release to release and from the RE2/RE3 shapes, so
// each resolves a dozen candidate patterns (PauseManager, GuiManager,
// SequenceManager, EventManager, MovieManager, ...) through the namespace-
// agnostic probes in game_state_probing.h and suppresses on whichever ones
// resolved. Nothing here is game-specific; the per-game part is the namespace
// candidate list (SetNamespaceCandidates) and, for menus that render over a
// live 3D backdrop, GameplayGate::NotifyMenuDrawn.
//
// A title whose managers ARE known by their fully-qualified names should not
// use this: naming them binds the type that was verified, where probing binds
// whichever candidate happens to resolve first.
//
// Wire both into a GameplayGate:
//   GameplayGate g{&DiscoverManagerProbes, &ManagerProbeGameplayCheck};
void DiscoverManagerProbes();
bool ManagerProbeGameplayCheck(void* primaryCamera, bool diag, const char** reason);

} // namespace cameraunlock::reframework
