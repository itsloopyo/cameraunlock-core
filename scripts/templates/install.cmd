@echo off
:: ============================================
:: CameraUnlock BepInEx Install Template
:: ============================================
:: Source of truth: cameraunlock-core/scripts/templates/install.cmd.
:: Copy this to <mod>/scripts/install.cmd, edit the CONFIG BLOCK below,
:: leave everything after it alone. Contract documented in the global
:: ~/.claude/CLAUDE.md under "install.cmd / uninstall.cmd - Unified
:: Launcher Contract".
::
:: Covers two BepInEx variants, dispatched by BEPINEX_SUBFOLDER:
::   * Regular BepInEx: leave BEPINEX_SUBFOLDER empty. Vendor zip is
::     BepInEx_win_<arch>.zip, extracted directly to the game root.
::   * Thunderstore-wrapped (BepInExPack_<Game>): set BEPINEX_SUBFOLDER
::     to the wrapper dir name. Override BEPINEX_VENDOR_ZIP_NAME to the
::     actual zip filename. We extract to a temp dir and flatten the
::     wrapper subfolder's contents into the game root.
::
:: Launcher CLI (always): install.cmd [GAME_PATH] [/y]
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=my-game-id"
set "MOD_DISPLAY_NAME=My Mod Name"
set "MOD_DLLS=MyMod.dll CameraUnlock.Core.dll CameraUnlock.Core.Unity.dll"
set "MOD_INTERNAL_NAME=MyMod"
set "MOD_VERSION=1.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=BepInEx"
set "BEPINEX_ARCH=x64"
set "BEPINEX_VENDOR_ZIP_NAME="
set "BEPINEX_SUBFOLDER="
set "MOD_CONTROLS="
:: --- END CONFIG BLOCK ---

call :main %*
set "_EC=%errorlevel%"
if not defined YES_FLAG ( echo. & pause )
exit /b %_EC%

:main
setlocal enabledelayedexpansion

:: -------- Arg parser (canonical, do not modify) --------
set "YES_FLAG="
set "_GIVEN_PATH="
:parse_args
if "%~1"=="" goto :args_done
set "_ARG=%~1"
if /i "!_ARG!"=="/y"    ( set "YES_FLAG=1" & shift & goto :parse_args )
if /i "!_ARG!"=="-y"    ( set "YES_FLAG=1" & shift & goto :parse_args )
if /i "!_ARG!"=="--yes" ( set "YES_FLAG=1" & shift & goto :parse_args )
if "!_ARG:~0,2!"=="--" ( echo ERROR: unknown flag "!_ARG!" & exit /b 2 )
if "!_ARG:~0,1!"=="/"  ( echo ERROR: unknown flag "!_ARG!" & exit /b 2 )
if "!_ARG:~0,1!"=="-"  ( echo ERROR: unknown flag "!_ARG!" & exit /b 2 )
if not defined _GIVEN_PATH (
    if exist "!_ARG!\" ( set "_GIVEN_PATH=!_ARG!" & shift & goto :parse_args )
)
echo ERROR: unrecognised argument "!_ARG!"
exit /b 2
:args_done

echo.
echo === %MOD_DISPLAY_NAME% - Install ===
echo.

set "SCRIPT_DIR=%~dp0"

:: -------- Resolve game path via shared shim --------
:: Release ZIP layout: scripts\ is the ZIP root, shim is at shared\find-game.ps1.
:: Dev tree layout: scripts\ is <repo>\scripts\, shim is at ..\cameraunlock-core\scripts\find-game.ps1.
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
if defined _GIVEN_PATH set "_GIVEN_ARG=-GivenPath "!_GIVEN_PATH!""
powershell -NoProfile -ExecutionPolicy Bypass -File "%_SHIM%" -GameId %GAME_ID% -OutFile "!_SHIM_OUT!" !_GIVEN_ARG!
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

:: -------- Game-running check --------
tasklist /fi "imagename eq %GAME_EXE%" 2>nul | findstr /i "%GAME_EXE%" >nul 2>&1
if not errorlevel 1 (
    echo ERROR: %GAME_DISPLAY_NAME% is currently running.
    echo Please close the game before installing.
    echo.
    exit /b 1
)

:: -------- Prior state: preserve installed_by_us=true across re-installs --------
set "WE_INSTALLED=false"
if exist "%GAME_PATH%\%STATE_FILE%" (
    findstr /c:"installed_by_us" "%GAME_PATH%\%STATE_FILE%" 2>nul | findstr /c:"true" >nul 2>&1
    if not errorlevel 1 set "WE_INSTALLED=true"
)

:: -------- Ensure BepInEx --------
if not exist "%GAME_PATH%\BepInEx\core\BepInEx.dll" (
    echo BepInEx not found. Installing...
    echo.
    call :install_bepinex
    if errorlevel 1 exit /b 1
    set "WE_INSTALLED=true"
    echo.
    if defined YES_FLAG (
        echo BepInEx installed. It will initialize on first game launch.
    ) else (
        call :prompt_bepinex_init
    )
) else (
    echo Existing BepInEx detected, skipping loader install, deploying plugin only.
)
echo.

:: -------- Deploy mod files --------
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

:: -------- Write state file --------
call :write_state_file

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
:: Skipped entirely when /y (launcher/automation) is set.
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
:: Install BepInEx from the bundled vendored copy.
:: Vendor tree is the single source of truth at install time. To bump the
:: bundled version, run `pixi run update-deps` in the mod repo and commit.
:: See ~/.claude/CLAUDE.md "Vendoring Third-Party Dependencies".
:: ============================================
:install_bepinex
set "VENDOR_DIR=%SCRIPT_DIR%vendor\bepinex"
if defined BEPINEX_VENDOR_ZIP_NAME (
    set "VENDOR_ZIP=%VENDOR_DIR%\%BEPINEX_VENDOR_ZIP_NAME%"
) else (
    set "VENDOR_ZIP=%VENDOR_DIR%\BepInEx_win_%BEPINEX_ARCH%.zip"
)

if not exist "!VENDOR_ZIP!" (
    echo   ERROR: Bundled BepInEx not found at:
    echo     !VENDOR_ZIP!
    echo   The installer ZIP is corrupt. Re-download the release.
    exit /b 1
)

echo   Extracting bundled BepInEx to game directory...
if defined BEPINEX_SUBFOLDER (
    :: Thunderstore BepInExPack: extract to temp, flatten wrapper into GAME_PATH.
    set "BEP_TEMP=%TEMP%\BepInEx_extract"
    if exist "!BEP_TEMP!" rmdir /s /q "!BEP_TEMP!"
    mkdir "!BEP_TEMP!"
    "%SystemRoot%\System32\tar.exe" -xf "!VENDOR_ZIP!" -C "!BEP_TEMP!"
    if errorlevel 1 (
        echo   ERROR: Extraction failed.
        rmdir /s /q "!BEP_TEMP!" 2>nul
        exit /b 1
    )
    xcopy /s /e /y /q "!BEP_TEMP!\%BEPINEX_SUBFOLDER%\*" "%GAME_PATH%\" >nul
    rmdir /s /q "!BEP_TEMP!"
) else (
    "%SystemRoot%\System32\tar.exe" -xf "!VENDOR_ZIP!" -C "%GAME_PATH%"
    if errorlevel 1 (
        echo   ERROR: Extraction failed.
        exit /b 1
    )
)

if not exist "%GAME_PATH%\BepInEx\plugins" mkdir "%GAME_PATH%\BepInEx\plugins"

:: Enable console + disk logging. Skip if BepInEx.cfg already exists
:: (Thunderstore packs ship preconfigured; don't clobber).
if not exist "%GAME_PATH%\BepInEx\config\BepInEx.cfg" (
    if not exist "%GAME_PATH%\BepInEx\config" mkdir "%GAME_PATH%\BepInEx\config"
    > "%GAME_PATH%\BepInEx\config\BepInEx.cfg" (
        echo [Logging.Console]
        echo Enabled = true
        echo.
        echo [Logging.Disk]
        echo Enabled = true
    )
)

echo   BepInEx installed successfully!
exit /b 0

:: ============================================
:: Write the canonical state file.
:: Schema version 1. Preserves WE_INSTALLED which may have been
:: already-true from a prior install.
:: ============================================
:write_state_file
> "%GAME_PATH%\%STATE_FILE%" (
    echo {
    echo   "schema_version": 1,
    echo   "framework": {
    echo     "type": "%FRAMEWORK_TYPE%",
    echo     "installed_by_us": !WE_INSTALLED!
    echo   },
    echo   "mod": {
    echo     "id": "%GAME_ID%",
    echo     "name": "%MOD_INTERNAL_NAME%",
    echo     "version": "%MOD_VERSION%"
    echo   }
    echo }
)
exit /b 0
