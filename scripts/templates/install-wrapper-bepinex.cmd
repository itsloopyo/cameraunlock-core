@echo off
:: ============================================
:: <Game Name> Head Tracking - Install
:: ============================================
:: Thin wrapper - install body lives in cameraunlock-core/scripts/install-body-bepinex.cmd,
:: staged into the release ZIP's shared/ by Copy-SharedBundle. To change
:: install behaviour edit the body, not this wrapper.
::
:: Source of truth for everything below the CONFIG BLOCK:
:: cameraunlock-core/scripts/templates/install-wrapper-bepinex.cmd. Copy this
:: file to <mod>/scripts/install.cmd, fill in the CONFIG BLOCK, change nothing
:: else. scripts/conformance.ps1 checks that nothing else changed.
::
:: Two BepInEx variants, dispatched by BEPINEX_SUBFOLDER: leave it empty for
:: a regular BepInEx_win_<arch>.zip extracted to the game root, or set it to
:: the wrapper directory name for a Thunderstore BepInExPack_<Game>, whose
:: contents are flattened out of that directory into the game root.
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=<games.json id>"
set "MOD_DISPLAY_NAME=<Game Name> Head Tracking"
set "MOD_DLLS=<Mod>HeadTracking.dll"
set "MOD_INTERNAL_NAME=<Mod>HeadTracking"
set "MOD_VERSION=0.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=BepInEx"
:: x64 or x86 - selects the vendored BepInEx zip.
set "BEPINEX_ARCH=x64"
:: Override the vendor zip filename (Thunderstore packs ship their own name).
set "BEPINEX_VENDOR_ZIP_NAME="
:: Thunderstore wrapper directory to flatten into the game root. Empty for
:: regular BepInEx.
set "BEPINEX_SUBFOLDER="
:: Subfolder under BepInEx\plugins\ to deploy into. Empty lays the DLLs flat.
set "PLUGIN_SUBFOLDER="
:: Post-install help text. `&echo ` starts each further line.
set "MOD_CONTROLS=Controls:&echo   End      - Toggle head tracking on/off&echo   Page Up  - Toggle position tracking on/off&echo   Page Down - Toggle yaw mode (world-locked / camera-local)"
:: --- END CONFIG BLOCK ---

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\install-body-bepinex.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\install-body-bepinex.cmd"
if not exist "%_BODY%" (
    echo ERROR: install-body-bepinex.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%
