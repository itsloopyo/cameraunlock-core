@echo off
:: ============================================
:: CameraUnlock MelonLoader Install Template
:: ============================================
:: Source of truth: cameraunlock-core/scripts/templates/install-melonloader.cmd.
:: Copy to <mod>/scripts/install.cmd, edit CONFIG BLOCK, leave the rest alone.
:: Contract: see ~/.claude/CLAUDE.md "install.cmd / uninstall.cmd - Unified
:: Launcher Contract".
::
:: MelonLoader layout: mod DLL in <game>/Mods/, UserLibs in <game>/UserLibs/.
:: Loader files: version.dll, dobby.dll, MelonLoader/ at game root.
::
:: Launcher CLI: install.cmd [GAME_PATH] [/y]
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=my-game-id"
set "MOD_DISPLAY_NAME=My Mod Name"
set "MOD_DLLS=MyMod.dll CameraUnlock.Core.dll CameraUnlock.Core.Unity.dll"
set "MOD_INTERNAL_NAME=MyMod"
set "MOD_VERSION=1.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=MelonLoader"
set "MELONLOADER_MARKER=MelonLoader\net35\MelonLoader.dll"
set "MOD_CONTROLS="
:: MELONLOADER_MARKER tells us whether ML is already installed. Default is
:: net35 (Unity 2017-era games). For IL2CPP / modern Mono games override
:: to MelonLoader\net6\MelonLoader.dll. The bundled MelonLoader version is
:: pinned by vendor/melonloader/MelonLoader.x64.zip; bump via `pixi run update-deps`.
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

:: -------- Prior state --------
set "WE_INSTALLED=false"
if exist "%GAME_PATH%\%STATE_FILE%" (
    findstr /c:"installed_by_us" "%GAME_PATH%\%STATE_FILE%" 2>nul | findstr /c:"true" >nul 2>&1
    if not errorlevel 1 set "WE_INSTALLED=true"
)

:: -------- Ensure MelonLoader --------
if not exist "%GAME_PATH%\%MELONLOADER_MARKER%" (
    echo MelonLoader not found. Installing...
    echo.
    call :install_melonloader
    if errorlevel 1 exit /b 1
    set "WE_INSTALLED=true"
    echo.
    if defined YES_FLAG (
        echo MelonLoader installed. It will initialize on first game launch.
    ) else (
        call :prompt_melonloader_init
    )
) else (
    echo Existing MelonLoader detected, skipping loader install, deploying plugin only.
)
echo.

:: -------- Deploy mod files --------
echo Deploying mod files...

set "MODS_PATH=%GAME_PATH%\Mods"
set "DLL_DIR=%SCRIPT_DIR%plugins"

if not exist "%MODS_PATH%" mkdir "%MODS_PATH%"

set "DEPLOY_FAILED=0"
for %%f in (%MOD_DLLS%) do (
    if exist "%DLL_DIR%\%%f" (
        copy /y "%DLL_DIR%\%%f" "%MODS_PATH%\" >nul
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
echo   %MODS_PATH%
echo.
echo Start the game to use the mod!
if defined MOD_CONTROLS (
    echo.
    echo !MOD_CONTROLS!
)
echo.
exit /b 0

:: ============================================
:: Interactive MelonLoader init gate (manual-install flow only).
:: ============================================
:prompt_melonloader_init
color 0E
echo ========================================
echo   MelonLoader installed - action required
echo ========================================
echo.
echo MelonLoader was just installed but needs to initialize first.
echo.
echo   1. Start %GAME_DISPLAY_NAME%
echo   2. Wait until you reach the main menu
echo   3. Close the game
echo   4. Come back here and type "install" to continue
echo.
:melonloader_gate
set "_CONFIRM="
set /p "_CONFIRM=Type install to continue: "
if /i not "!_CONFIRM!"=="install" goto :melonloader_gate
echo.
color
exit /b 0

:: ============================================
:: Install MelonLoader from the bundled vendored copy.
:: Vendor tree is the single source of truth at install time. To bump the
:: bundled version, run `pixi run update-deps` in the mod repo and commit.
:: See ~/.claude/CLAUDE.md "Vendoring Third-Party Dependencies".
:: ============================================
:install_melonloader
set "VENDOR_DIR=%SCRIPT_DIR%vendor\melonloader"
set "VENDOR_ZIP=%VENDOR_DIR%\MelonLoader.x64.zip"

if not exist "%VENDOR_ZIP%" (
    echo   ERROR: Bundled MelonLoader not found at:
    echo     %VENDOR_ZIP%
    echo   The installer ZIP is corrupt. Re-download the release.
    exit /b 1
)

echo   Extracting bundled MelonLoader to game directory...
"%SystemRoot%\System32\tar.exe" -xf "%VENDOR_ZIP%" -C "%GAME_PATH%"
if errorlevel 1 (
    echo   ERROR: Extraction failed.
    exit /b 1
)

if not exist "%GAME_PATH%\Mods" mkdir "%GAME_PATH%\Mods"

echo   MelonLoader installed successfully!
exit /b 0

:: ============================================
:: Write the canonical state file.
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
