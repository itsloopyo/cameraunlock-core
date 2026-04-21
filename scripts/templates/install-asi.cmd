@echo off
:: ============================================
:: CameraUnlock Ultimate ASI Loader Install Template
:: ============================================
:: Source of truth: cameraunlock-core/scripts/templates/install-asi.cmd.
:: Copy to <mod>/scripts/install.cmd, edit CONFIG BLOCK, leave the rest
:: alone. Contract: see ~/.claude/CLAUDE.md "install.cmd / uninstall.cmd".
::
:: Ultimate ASI Loader: a single-DLL loader renamed to winmm.dll (or
:: dinput8.dll, xinput1_3.dll, etc. depending on the game). Mod files
:: are .asi plugins dropped into the same directory as the game exe.
:: EXE_DIR is derived from GAME_PATH + GAME_EXE_RELPATH returned by the
:: shim, so games with ph/work/bin/x64-style nested exes work.
::
:: Launcher CLI: install.cmd [GAME_PATH] [/y]
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=my-game-id"
set "MOD_DISPLAY_NAME=My Mod Name"
set "MOD_DLLS=MyMod.asi HeadTracking.ini"
set "MOD_INTERNAL_NAME=MyMod"
set "MOD_VERSION=1.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=ASILoader"
set "ASI_LOADER_NAME=winmm.dll"
set "MOD_CONTROLS="
:: ASI_LOADER_NAME is the filename the ASI DLL is renamed to. DL2 and most
:: modern games use winmm.dll; older ones use dinput8.dll or xinput1_3.dll.
:: fetch-latest.ps1 always downloads dinput8.dll from upstream; we copy it
:: to ASI_LOADER_NAME in EXE_DIR.
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

:: Derive EXE_DIR (where .asi plugins land) from GAME_PATH + GAME_EXE_RELPATH.
for %%i in ("%GAME_PATH%\%GAME_EXE_RELPATH%") do set "EXE_DIR=%%~dpi"
if "!EXE_DIR:~-1!"=="\" set "EXE_DIR=!EXE_DIR:~0,-1!"
echo Exe dir : %EXE_DIR%
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

:: -------- Ensure ASI Loader --------
if not exist "%EXE_DIR%\%ASI_LOADER_NAME%" (
    echo ASI Loader not found. Installing...
    echo.
    call :install_asi_loader
    if errorlevel 1 exit /b 1
    set "WE_INSTALLED=true"
) else (
    echo Existing ASI Loader detected, skipping loader install, deploying plugin only.
)
echo.

:: -------- Deploy mod files --------
echo Deploying mod files...

set "FILES_DIR=%SCRIPT_DIR%plugins"

set "DEPLOY_FAILED=0"
for %%f in (%MOD_DLLS%) do (
    if exist "%FILES_DIR%\%%f" (
        copy /y "%FILES_DIR%\%%f" "%EXE_DIR%\" >nul
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
echo   %EXE_DIR%
echo.
echo Start the game to use the mod!
if defined MOD_CONTROLS (
    echo.
    echo !MOD_CONTROLS!
)
echo.
exit /b 0

:: ============================================
:: Install Ultimate ASI Loader (upstream-first, MIT-licensed).
:: See ~/.claude/CLAUDE.md "Vendoring Third-Party Dependencies".
:: ============================================
:install_asi_loader
set "VENDOR_DIR=%SCRIPT_DIR%vendor\ultimate-asi-loader"
set "VENDOR_DLL=%VENDOR_DIR%\dinput8.dll"
set "FETCH_SCRIPT=%VENDOR_DIR%\fetch-latest.ps1"
set "FETCHED_DLL=%TEMP%\asi-loader-dinput8.dll"
set "ASI_SRC="
set "USED_UPSTREAM="

if exist "%FETCH_SCRIPT%" (
    echo   Trying upstream Ultimate ASI Loader, latest within range...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%FETCH_SCRIPT%" -OutputPath "%FETCHED_DLL%" >nul 2>&1
    if not errorlevel 1 (
        set "ASI_SRC=%FETCHED_DLL%"
        set "USED_UPSTREAM=1"
        echo   Using upstream Ultimate ASI Loader.
    )
)

if not defined ASI_SRC (
    if not exist "%VENDOR_DLL%" (
        echo   ERROR: Upstream unreachable AND bundled fallback missing at:
        echo     %VENDOR_DLL%
        echo   The installer ZIP is corrupt. Re-download the release.
        exit /b 1
    )
    set "ASI_SRC=%VENDOR_DLL%"
    echo   Upstream unreachable, using bundled fallback copy.
)

copy /y "!ASI_SRC!" "%EXE_DIR%\%ASI_LOADER_NAME%" >nul
if errorlevel 1 (
    echo   ERROR: Failed to copy loader to %EXE_DIR%.
    echo   Check the game directory is writable.
    if defined USED_UPSTREAM del "%FETCHED_DLL%" 2>nul
    exit /b 1
)
if defined USED_UPSTREAM del "%FETCHED_DLL%" 2>nul

echo   Ultimate ASI Loader installed successfully!
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
