@echo off
:: ============================================
:: <Game Name> Head Tracking - Install
:: ============================================
:: Thin wrapper - install body lives in cameraunlock-core/scripts/install-body-beamng.cmd,
:: staged into the release ZIP's shared/ by Copy-SharedBundle. To change
:: install behaviour edit the body, not this wrapper.
::
:: Source of truth for everything below the CONFIG BLOCK:
:: cameraunlock-core/scripts/templates/install-wrapper-beamng.cmd. Copy this
:: file to <mod>/scripts/install.cmd, fill in the CONFIG BLOCK, change nothing
:: else. scripts/conformance.ps1 checks that nothing else changed.
::
:: BeamNG user mods: the payload is a game-ready mod archive that goes into
:: the per-user folder's mods\, which BeamNG reads on its own. Nothing lands in
:: the game install directory and there is no loader, which is why
:: FRAMEWORK_TYPE is BeamNGUserMods. The archive named in MOD_DLLS sits next to
:: this file at the release ZIP root, not in a plugins\ subfolder.
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=<games.json id>"
set "MOD_DISPLAY_NAME=<Game Name> Head Tracking"
:: The game-ready mod archive(s), copied into <user folder>\mods\ on every
:: install. BeamNG loads a mod from the archive as it ships, so this is the
:: whole payload.
set "MOD_DLLS=<Mod>HeadTracking.zip"
set "MOD_INTERNAL_NAME=<Mod>HeadTracking"
set "MOD_VERSION=0.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=BeamNGUserMods"
:: Files copied into <user folder>\mods\ only when they are not already there,
:: so an upgrade keeps whatever the user tuned. A mod whose config is written
:: by its own Lua on first run seeds nothing and leaves this blank.
set "MOD_SEED_FILES="
:: Post-install help text. `&echo ` starts each further line.
set "MOD_CONTROLS=Controls:&echo   Ctrl+Shift+Y - Toggle head tracking on/off&echo   Ctrl+Shift+G - Cycle mode: full 6DOF, rotation only, position only"
:: --- END CONFIG BLOCK ---

:: Pin delayed expansion off before `%*` is expanded on the `call` below.
:: Under `cmd /V:ON`, or with DelayedExpansion=1 in
:: HKCU\Software\Microsoft\Command Processor, cmd.exe eats a `!` out of the
:: expanded line, and a real game path like C:\Games\Oh! My Game reaches the
:: body already mangled. The body pins expansion off at its own outer scope
:: too, but that is one `call` too late to save the argument it was handed.
setlocal disabledelayedexpansion

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\install-body-beamng.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\install-body-beamng.cmd"
if not exist "%_BODY%" (
    echo ERROR: install-body-beamng.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%
