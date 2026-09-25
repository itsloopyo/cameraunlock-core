@echo off
:: ============================================
:: <Game Name> Head Tracking - Install
:: ============================================
:: Thin wrapper - install body lives in cameraunlock-core/scripts/install-body-shim-forwarder.cmd,
:: staged into the release ZIP's shared/ by Copy-SharedBundle. To change
:: install behaviour edit the body, not this wrapper.
::
:: Source of truth for everything below the CONFIG BLOCK:
:: cameraunlock-core/scripts/templates/install-wrapper-shim-forwarder.cmd. Copy
:: this file to <mod>/scripts/install.cmd, fill in the CONFIG BLOCK, change
:: nothing else. scripts/conformance.ps1 checks that nothing else changed.
:: Keep every CONFIG BLOCK line, blank where it does not apply. A name the
:: block leaves out is not unset: it keeps whatever another mod's wrapper set
:: in the same console, and the body acts on that value.
::
:: Forwarding shim: the mod DLL is a system-DLL proxy whose exports forward to
:: a renamed copy of the real system DLL, which cannot ship in the ZIP. The
:: body copies it from the user's own system directory next to the shim. Any
:: pre-existing DLL at the shim's name is preserved as <name>.backup for
:: uninstall to restore, and there is no framework, so FRAMEWORK_TYPE is None.
:: The matching uninstall.cmd lists SYSTEM_DLL_COPY in its MOD_DLLS.
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=<games.json id>"
set "MOD_DISPLAY_NAME=<Game Name> Head Tracking"
set "MOD_DLLS=<system dll name>"
set "MOD_INTERNAL_NAME=<Mod>HeadTracking"
set "MOD_VERSION=0.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=None"
:: A byte sequence every build of this mod's shim carries - the mod's own name
:: in a string literal is the usual choice. It answers "is the DLL already
:: sitting at that name ours?", which is what decides whether that file is the
:: user's original and has to be kept as <name>.backup. Comparing bytes against
:: the build being installed cannot answer it: on an upgrade the installed shim
:: is the previous version, so the bytes differ and the mod's own DLL gets
:: recorded as the user's original.
set "SHIM_MARKER=<string present in every build of the shim>"
:: Optional second identity for a companion launcher with different strings.
set "SHIM_MARKER_ALT="
:: The system DLL the shim replaces, the name its forwards point at, and the
:: game executable's architecture (x64 or x86), which picks the system
:: directory the copy is taken from.
set "SYSTEM_DLL=<system dll name>"
set "SYSTEM_DLL_COPY=<system dll name>_real.dll"
set "SYSTEM_DLL_ARCH=x64"
:: Files copied only when they are not already there, so an upgrade keeps
:: whatever the user tuned. Listing an .ini in MOD_DLLS instead puts it through
:: the unconditional copy and the SHIM_MARKER check, which resets every key on
:: every update and then records the tuned file as the game original.
set "MOD_SEED_FILES="
:: Post-install help text. `&echo ` starts each further line.
set "MOD_CONTROLS=Controls:&echo   End      - Toggle head tracking on/off&echo   Page Up  - Toggle position tracking on/off&echo   Page Down - Toggle yaw mode (world-locked / camera-local)"
:: --- END CONFIG BLOCK ---

:: Pin delayed expansion off before `%*` is expanded on the `call` below.
:: Under `cmd /V:ON`, or with DelayedExpansion=1 in
:: HKCU\Software\Microsoft\Command Processor, cmd.exe eats a `!` out of the
:: expanded line, and a real game path like C:\Games\Oh! My Game reaches the
:: body already mangled. The body pins expansion off at its own outer scope
:: too, but that is one `call` too late to save the argument it was handed.
setlocal disabledelayedexpansion

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\install-body-shim-forwarder.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\install-body-shim-forwarder.cmd"
if not exist "%_BODY%" (
    echo ERROR: install-body-shim-forwarder.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%
