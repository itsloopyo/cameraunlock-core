@echo off
:: ============================================
:: CameraUnlock REFramework Install Template
:: ============================================
:: Source of truth: cameraunlock-core/scripts/templates/install-reframework.cmd.
:: Copy to <mod>/scripts/install.cmd, edit CONFIG BLOCK, leave the rest
:: alone. Contract: see ~/.claude/CLAUDE.md "install.cmd / uninstall.cmd".
::
:: REFramework layout:
::   Loader:     <game>/dinput8.dll
::   Runtime:    <game>/reframework/
::   Plugins:    <game>/reframework/plugins/   (mod DLLs + INI go here)
::
:: Upstream is prerelease-tracked (nightly zips), so fetch-latest.ps1
:: uses `-IncludePrerelease`.
::
:: Launcher CLI: install.cmd [GAME_PATH] [/y]
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=my-game-id"
set "MOD_DISPLAY_NAME=My Mod Name"
set "MOD_DLLS=MyMod.dll HeadTracking.ini"
set "MOD_INTERNAL_NAME=MyMod"
set "MOD_VERSION=1.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=REFramework"
set "REFRAMEWORK_VENDOR_ZIP_NAME=RE9.zip"
set "MOD_CONTROLS="
:: REFRAMEWORK_VENDOR_ZIP_NAME is per-game (RE:Requiem is RE9.zip, RE2 is
:: RE2.zip, etc). The upstream release has one zip per supported game.
:: fetch-latest.ps1 filters on the game-specific asset pattern.
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

:: -------- Ensure REFramework --------
if not exist "%GAME_PATH%\dinput8.dll" (
    echo REFramework not found. Installing...
    echo.
    call :install_reframework
    if errorlevel 1 exit /b 1
    set "WE_INSTALLED=true"
    echo REFramework installed successfully.
    echo.
) else (
    echo Existing REFramework detected, skipping loader install, deploying plugin only.
)

:: -------- Deploy mod files --------
set "PLUGINS_DIR=%GAME_PATH%\reframework\plugins"
if not exist "%PLUGINS_DIR%" mkdir "%PLUGINS_DIR%"

echo.
echo Deploying mod files...

set "DEPLOY_FAILED=0"
for %%f in (%MOD_DLLS%) do (
    if exist "%SCRIPT_DIR%plugins\%%f" (
        copy /y "%SCRIPT_DIR%plugins\%%f" "%PLUGINS_DIR%\%%f" >nul
        echo   Deployed: %%f
    ) else if exist "%SCRIPT_DIR%%%f" (
        copy /y "%SCRIPT_DIR%%%f" "%PLUGINS_DIR%\%%f" >nul
        echo   Deployed: %%f
    ) else (
        echo   ERROR: %%f not found in installer package
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
echo ============================================
echo  %MOD_DISPLAY_NAME% v%MOD_VERSION% installed!
echo ============================================
echo.
if defined MOD_CONTROLS (
    echo !MOD_CONTROLS!
    echo.
)
echo Make sure OpenTrack is running and sending
echo data to UDP port 4242.
echo.
exit /b 0

:: ============================================
:: Install REFramework (upstream-first nightly, fall back to vendored copy).
:: See ~/.claude/CLAUDE.md "Vendoring Third-Party Dependencies".
:: ============================================
:install_reframework
set "VENDOR_DIR=%SCRIPT_DIR%vendor\reframework"
set "VENDOR_ZIP=%VENDOR_DIR%\%REFRAMEWORK_VENDOR_ZIP_NAME%"
set "FETCH_SCRIPT=%VENDOR_DIR%\fetch-latest.ps1"
set "TEMP_ZIP=%TEMP%\reframework_install.zip"
set "LOADER_SOURCE="
set "USED_UPSTREAM="

if exist "%FETCH_SCRIPT%" (
    echo   Trying upstream REFramework nightly...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%FETCH_SCRIPT%" -OutputPath "%TEMP_ZIP%" >nul 2>&1
    if not errorlevel 1 (
        set "LOADER_SOURCE=%TEMP_ZIP%"
        set "USED_UPSTREAM=1"
        echo   Using upstream REFramework.
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

echo   Extracting REFramework...
powershell -NoProfile -Command "Expand-Archive -Path '!LOADER_SOURCE!' -DestinationPath '%GAME_PATH%' -Force"
if defined USED_UPSTREAM del "!TEMP_ZIP!" 2>nul

if not exist "%GAME_PATH%\dinput8.dll" (
    echo   ERROR: REFramework installation failed.
    exit /b 1
)

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
