@echo off
:: ============================================
:: <Game Name> Head Tracking - Install
:: ============================================
:: Thin wrapper - install body lives in cameraunlock-core/scripts/install-body-cecil.cmd,
:: staged into the release ZIP's shared/ by Copy-SharedBundle. To change
:: install behaviour edit the body, not this wrapper.
::
:: Source of truth for everything below the CONFIG BLOCK:
:: cameraunlock-core/scripts/templates/install-wrapper-cecil.cmd. Copy this
:: file to <mod>/scripts/install.cmd, fill in the CONFIG BLOCK, change nothing
:: else. scripts/conformance.ps1 checks that nothing else changed.
::
:: Mono.Cecil patcher: no external loader. The install body compiles
:: PATCHER_FILE and rewrites ASSEMBLY_DLL in place, keeping a pristine
:: .original beside it. PATCH_MARKER is what stops a second install from
:: capturing an already-patched assembly as that backup.
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=<games.json id>"
set "MOD_DISPLAY_NAME=<Game Name> Head Tracking"
set "MOD_DLLS=<Mod>HeadTracking.dll"
set "MOD_INTERNAL_NAME=<Mod>HeadTracking"
set "MOD_VERSION=0.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=MonoCecil"
:: Path under the game root holding the assembly to patch.
set "MANAGED_SUBFOLDER=<Game>_Data\Managed"
set "ASSEMBLY_DLL=Assembly-CSharp.dll"
:: C# patcher source in mod/, compiled at install time.
set "PATCHER_FILE=BootstrapPatcher.cs"
:: Type or field name the patcher injects, used to recognise a patched
:: assembly. Bump the suffix whenever the patch shape changes.
set "PATCH_MARKER=HeadTracking_Patched_<Game>_v1"
:: Post-install help text. `&echo ` starts each further line.
set "MOD_CONTROLS=Controls:&echo   End      - Toggle head tracking on/off&echo   Page Up  - Toggle position tracking on/off&echo   Page Down - Toggle yaw mode (world-locked / camera-local)"
:: --- END CONFIG BLOCK ---

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\install-body-cecil.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\install-body-cecil.cmd"
if not exist "%_BODY%" (
    echo ERROR: install-body-cecil.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%
