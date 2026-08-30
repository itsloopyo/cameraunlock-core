@echo off
:: ============================================
:: <Game Name> Head Tracking - Uninstall
:: ============================================
:: Thin wrapper - uninstall body lives in cameraunlock-core/scripts/uninstall-body.cmd,
:: staged into the release ZIP's shared/ by Copy-SharedBundle. One body covers
:: every loader; FRAMEWORK_TYPE dispatches, and it MUST match what install
:: wrote to the state file.
::
:: Source of truth for everything below the CONFIG BLOCK:
:: cameraunlock-core/scripts/templates/uninstall-wrapper.cmd. Copy this file to
:: <mod>/scripts/uninstall.cmd, fill in the CONFIG BLOCK, change nothing else.
:: scripts/conformance.ps1 checks that nothing else changed.
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=<games.json id>"
set "MOD_DISPLAY_NAME=<Game Name> Head Tracking"
set "MOD_DLLS=<Mod>HeadTracking.dll"
set "MOD_INTERNAL_NAME=<Mod>HeadTracking"
set "STATE_FILE=.headtracking-state.json"
:: BepInEx | MelonLoader | MonoCecil | ASILoader | REFramework | UE4SS | None
set "FRAMEWORK_TYPE=None"
:: DLL names shipped by older versions of this mod, removed too so an upgrade
:: does not leave a second copy for the loader to bind.
set "LEGACY_DLLS="
:: BepInEx: subfolder under BepInEx\plugins\ the DLLs were deployed into.
set "PLUGIN_SUBFOLDER="

:: --- Loader-specific config (leave the ones that don't apply blank) ---
:: MonoCecil: used to find + restore the original Assembly-CSharp.dll.
set "MANAGED_SUBFOLDER="
set "ASSEMBLY_DLL="
:: MonoCecil: marker the patcher injects; guards against capturing/restoring a
:: patched Assembly-CSharp.dll as the pristine .original backup.
set "PATCH_MARKER="
:: MonoCecil: extra files to also remove from MANAGED_SUBFOLDER (config/log
:: files left behind by the mod itself).
set "MANAGED_EXTRAS="
:: ASILoader: filename the ASI DLL was renamed to. Defaults to winmm.dll.
set "ASI_LOADER_NAME=winmm.dll"
:: --- END CONFIG BLOCK ---

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\uninstall-body.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\uninstall-body.cmd"
if not exist "%_BODY%" (
    echo ERROR: uninstall-body.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%
