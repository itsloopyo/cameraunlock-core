@echo off
:: ============================================
:: <Game Name> Head Tracking - Install
:: ============================================
:: Thin wrapper - install body lives in cameraunlock-core/scripts/install-body-asi.cmd,
:: staged into the release ZIP's shared/ by Copy-SharedBundle. To change
:: install behaviour edit the body, not this wrapper.
::
:: Source of truth for everything below the CONFIG BLOCK:
:: cameraunlock-core/scripts/templates/install-wrapper-asi.cmd. Copy this
:: file to <mod>/scripts/install.cmd, fill in the CONFIG BLOCK, change nothing
:: else. scripts/conformance.ps1 checks that nothing else changed.
::
:: Ultimate ASI Loader: one DLL renamed to the proxy the game already
:: imports, with the mod shipped as an .asi beside the game exe. Check the
:: exe's import table before choosing ASI_LOADER_NAME - a proxy the game does
:: not import is never loaded and the mod silently does nothing.
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=<games.json id>"
set "MOD_DISPLAY_NAME=<Game Name> Head Tracking"
set "MOD_DLLS=<Mod>HeadTracking.dll"
set "MOD_INTERNAL_NAME=<Mod>HeadTracking"
set "MOD_VERSION=0.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=ASILoader"
:: Filename the ASI loader DLL is renamed to: the import the game exe already
:: has. winmm.dll, dinput8.dll, dxgi.dll and xinput1_3.dll are the common ones.
set "ASI_LOADER_NAME=winmm.dll"
:: Post-install help text. `&echo ` starts each further line.
set "MOD_CONTROLS=Controls:&echo   End      - Toggle head tracking on/off&echo   Page Up  - Toggle position tracking on/off&echo   Page Down - Toggle yaw mode (world-locked / camera-local)"
:: --- END CONFIG BLOCK ---

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\install-body-asi.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\install-body-asi.cmd"
if not exist "%_BODY%" (
    echo ERROR: install-body-asi.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%
