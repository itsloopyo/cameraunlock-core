@echo off
:: ============================================
:: CameraUnlock Install Template
:: ============================================
:: Copy this file to <mod>/scripts/install.cmd, edit the CONFIG BLOCK
:: below, and leave everything after it alone. Detection fields
:: (GAME_EXE, Steam folder, env var, etc.) come from the shared
:: find-game.ps1 shim bundled into the release ZIP under `shared/`,
:: which reads cameraunlock-core/data/games.json by game id. Adding
:: a new game = one entry in games.json, not per-mod CONFIG duplication.
:: ============================================

:: --- CONFIG BLOCK (edit these for your mod) ---
set "GAME_ID=my-game-id"
set "MOD_DISPLAY_NAME=My Mod Name"
set "MOD_DLLS=MyMod.dll CameraUnlock.Core.dll CameraUnlock.Core.Unity.dll"
set "MOD_INTERNAL_NAME=MyMod"
set "MOD_VERSION=1.0.0"
set "STATE_FILE=.headtracking-state.json"
set "BEPINEX_ARCH=x86"
:: BEPINEX_ARCH picks the vendored fallback zip name (BepInEx_win_<arch>.zip).
:: Upstream version is resolved by vendor/bepinex/fetch-latest.ps1 at install time.
set "MOD_CONTROLS="
:: --- END CONFIG BLOCK ---

call :main %*
set "_EC=%errorlevel%"
echo.
pause
exit /b %_EC%

:main
setlocal enabledelayedexpansion

echo.
echo === %MOD_DISPLAY_NAME% - Install ===
echo.

set "SCRIPT_DIR=%~dp0"

:: --- Resolve game path via shared shim ---
::
:: find-game.ps1 reads cameraunlock-core/data/games.json (bundled into
:: this ZIP's `shared/` folder), runs the shared detection pipeline
:: (Steam appmanifest > Steam folder > GOG > Epic > Xbox > env var),
:: and writes a short batch file we `call` to pick up GAME_PATH,
:: GAME_EXE, GAME_DISPLAY_NAME, GAME_EXE_RELPATH, and ENV_VAR_NAME.
:: Passing %~1 as `-GivenPath` lets the launcher (or a user invoking
:: manually with a path) short-circuit detection entirely.
:: Release ZIP layout: scripts\ is the ZIP root, shim is at shared\find-game.ps1.
:: Dev tree layout: scripts\ is <repo>\scripts\, shim is at ..\cameraunlock-core\scripts\find-game.ps1.
:: The shim itself handles both layouts for its sibling GamePathDetection.psm1.
set "_SHIM=%SCRIPT_DIR%shared\find-game.ps1"
if not exist "%_SHIM%" set "_SHIM=%SCRIPT_DIR%..\cameraunlock-core\scripts\find-game.ps1"
if not exist "%_SHIM%" (
    echo ERROR: find-game.ps1 not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, make sure the cameraunlock-core submodule is checked out.
    exit /b 1
)
set "_SHIM_OUT=%TEMP%\cul-find-%RANDOM%-%RANDOM%.cmd"
set "_GIVEN_ARG="
if not "%~1"=="" set "_GIVEN_ARG=-GivenPath "%~1""
powershell -NoProfile -ExecutionPolicy Bypass -File "%_SHIM%" -GameId %GAME_ID% -OutFile "%_SHIM_OUT%" %_GIVEN_ARG%
set "_PS_EC=!errorlevel!"
if not "!_PS_EC!"=="0" (
    echo.
    echo ERROR: Could not resolve game install path ^(shim exit code !_PS_EC!^).
    echo Pass a path explicitly: install.cmd "C:\path\to\game"
    echo.
    del "!_SHIM_OUT!" 2>nul
    exit /b 1
)
call "!_SHIM_OUT!"
del "!_SHIM_OUT!" 2>nul

echo Game found: %GAME_PATH%
echo.

:: --- Check if game is running ---
tasklist /fi "imagename eq %GAME_EXE%" 2>nul | findstr /i "%GAME_EXE%" >nul 2>&1
if not errorlevel 1 (
    echo ERROR: %GAME_DISPLAY_NAME% is currently running.
    echo Please close the game before installing.
    echo.
    exit /b 1
)

:: --- Check BepInEx ---
::
:: Second positional arg `UNATTENDED` means the launcher is invoking us -
:: skip the interactive "type install to continue" gate, which would
:: loop forever against a null stdin. BepInEx initialises on first game
:: launch whether or not plugins are sitting in plugins\ already.
set "UNATTENDED="
if /i "%~2"=="UNATTENDED" set "UNATTENDED=1"

if not exist "%GAME_PATH%\BepInEx\core\BepInEx.dll" (
    echo BepInEx not found. Installing...
    echo.
    call :install_bepinex
    if errorlevel 1 exit /b 1
    echo.
    if defined UNATTENDED (
        echo BepInEx installed. It will initialize on first game launch.
    ) else (
        call :prompt_bepinex_init
    )
) else (
    echo Existing BepInEx detected, skipping loader install, deploying plugin only.
)
echo.

:: --- Deploy mod files ---
echo Deploying mod files...

set "PLUGINS_PATH=%GAME_PATH%\BepInEx\plugins"
set "DLL_DIR=%SCRIPT_DIR%plugins"

if not exist "%PLUGINS_PATH%" mkdir "%PLUGINS_PATH%"

set "DEPLOY_FAILED=0"
for %%f in (%MOD_DLLS%) do (
    if exist "%DLL_DIR%\%%f" (
        copy /y "%DLL_DIR%\%%f" "%PLUGINS_PATH%\" >nul
        echo   Deployed %%f
    ) else (
        echo   ERROR: %%f not found in plugins folder
        set "DEPLOY_FAILED=1"
    )
)

if "!DEPLOY_FAILED!"=="1" (
    echo.
    echo ========================================
    echo   Deployment Failed!
    echo ========================================
    echo.
    exit /b 1
)

:: --- Update state file ---
:: Preserve installed_by_us flag from previous state
set "WE_INSTALLED=false"
if exist "%GAME_PATH%\%STATE_FILE%" (
    findstr /c:"installed_by_us" "%GAME_PATH%\%STATE_FILE%" 2>nul | findstr /c:"true" >nul 2>&1
    if not errorlevel 1 set "WE_INSTALLED=true"
)

> "%GAME_PATH%\%STATE_FILE%" (
    echo {
    echo   "framework": {
    echo     "type": "BepInEx",
    echo     "installed_by_us": !WE_INSTALLED!
    echo   },
    echo   "mod": {
    echo     "name": "%MOD_INTERNAL_NAME%",
    echo     "version": "%MOD_VERSION%"
    echo   }
    echo }
)

echo.
echo ========================================
echo   Deployment Complete!
echo ========================================
echo.
echo %MOD_DISPLAY_NAME% has been deployed to:
echo   %PLUGINS_PATH%
echo.
echo Start the game to use the mod!
if defined MOD_CONTROLS (
    echo.
    echo !MOD_CONTROLS!
)
echo.
exit /b 0

:: ============================================
:: Interactive BepInEx init gate (manual-install flow only).
:: Extracted to a subroutine so the label can live at the top level -
:: cmd labels inside parenthesized blocks interact badly with `goto`.
:: ============================================
:prompt_bepinex_init
color 0E
echo ========================================
echo   BepInEx installed - action required
echo ========================================
echo.
echo BepInEx was just installed but needs to initialize first.
echo.
echo   1. Start %GAME_DISPLAY_NAME%
echo   2. Wait until you reach the main menu
echo   3. Close the game
echo   4. Come back here and type "install" to continue
echo.
:bepinex_gate
set "_CONFIRM="
set /p "_CONFIRM=Type install to continue: "
if /i not "!_CONFIRM!"=="install" goto :bepinex_gate
echo.
color
exit /b 0

:: ============================================
:: Install BepInEx (upstream-first, fall back to vendored copy)
:: See vendoring pattern docs in ~/.claude/CLAUDE.md "Vendoring Third-Party Dependencies"
:: ============================================
:install_bepinex
set "VENDOR_DIR=%SCRIPT_DIR%vendor\bepinex"
set "VENDOR_ZIP=%VENDOR_DIR%\BepInEx_win_%BEPINEX_ARCH%.zip"
set "FETCH_SCRIPT=%VENDOR_DIR%\fetch-latest.ps1"
set "BEP_ZIP=%TEMP%\BepInEx_install.zip"
set "LOADER_SOURCE="

if exist "%FETCH_SCRIPT%" (
    echo   Trying upstream BepInEx %BEPINEX_ARCH% ^(latest within range^)...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%FETCH_SCRIPT%" -OutputPath "%BEP_ZIP%" >nul 2>&1
    if not errorlevel 1 (
        set "LOADER_SOURCE=%BEP_ZIP%"
        set "USED_UPSTREAM=1"
        echo   Using upstream BepInEx.
    )
)

if not defined LOADER_SOURCE (
    if not exist "%VENDOR_ZIP%" (
        echo   ERROR: Upstream unreachable AND bundled fallback missing at:
        echo     %VENDOR_ZIP%
        echo   The installer ZIP is corrupt. Re-download the release.
        exit /b 1
    )
    set "LOADER_SOURCE=%VENDOR_ZIP%"
    echo   Upstream unreachable, using bundled fallback copy.
)

echo   Extracting BepInEx to game directory...
"%SystemRoot%\System32\tar.exe" -xf "%LOADER_SOURCE%" -C "%GAME_PATH%"
if errorlevel 1 (
    echo   ERROR: Extraction failed.
    if defined USED_UPSTREAM del "%BEP_ZIP%" 2>nul
    exit /b 1
)
if defined USED_UPSTREAM del "%BEP_ZIP%" 2>nul

:: Create plugins directory
if not exist "%GAME_PATH%\BepInEx\plugins" mkdir "%GAME_PATH%\BepInEx\plugins"

:: Enable console logging
if not exist "%GAME_PATH%\BepInEx\config" mkdir "%GAME_PATH%\BepInEx\config"
> "%GAME_PATH%\BepInEx\config\BepInEx.cfg" (
    echo [Logging.Console]
    echo Enabled = true
    echo.
    echo [Logging.Disk]
    echo Enabled = true
)

:: Write state file marking that we installed BepInEx
> "%GAME_PATH%\%STATE_FILE%" (
    echo {
    echo   "framework": {
    echo     "type": "BepInEx",
    echo     "installed_by_us": true
    echo   }
    echo }
)

echo   BepInEx installed successfully!
exit /b 0
