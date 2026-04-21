@echo off
:: ============================================
:: CameraUnlock Shim-Only Install Template
:: ============================================
:: Source of truth: cameraunlock-core/scripts/templates/install-shim.cmd.
:: Copy to <mod>/scripts/install.cmd, edit CONFIG BLOCK, leave the rest
:: alone. Contract: see ~/.claude/CLAUDE.md "install.cmd / uninstall.cmd".
::
:: Shim-only mod: the mod DLL itself is a system-DLL shim (xinput1_3.dll,
:: dxgi.dll, winmm.dll, etc.) loaded by the game directly - no external
:: framework. No loader fetch/extract; just copy the DLL to EXE_DIR,
:: preserving any pre-existing system DLL as <name>.backup on first
:: install so uninstall can restore it.
::
:: FRAMEWORK_TYPE is "None" on the state file. /force on uninstall is a
:: no-op for framework removal (there's nothing beyond the shim DLL).
::
:: Launcher CLI: install.cmd [GAME_PATH] [/y]
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=my-game-id"
set "MOD_DISPLAY_NAME=My Mod Name"
set "MOD_DLLS=xinput1_3.dll"
set "MOD_INTERNAL_NAME=MyMod"
set "MOD_VERSION=1.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=None"
set "MOD_CONTROLS="
:: MOD_DLLS is the shim DLL(s). Typical is one entry (xinput1_3.dll).
:: Can be multi-entry if the shim needs a sibling INI / config file.
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

:: Derive EXE_DIR (where shim DLLs land) from GAME_PATH + GAME_EXE_RELPATH.
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

:: -------- Deploy shim DLL(s) --------
:: For each entry in MOD_DLLS: if an existing file is present at that name
:: in EXE_DIR (could be a stock system DLL the game uses or a prior version
:: of our own shim), back it up to <name>.backup on the *first* install.
:: If a .backup is already present, leave it alone - we must keep the
:: user's pre-mod state intact across our re-installs.
echo Deploying shim files...

set "SRC_DIR=%SCRIPT_DIR%plugins"
set "DEPLOY_FAILED=0"

for %%f in (%MOD_DLLS%) do (
    if not exist "%SRC_DIR%\%%f" (
        echo   ERROR: %%f not found in plugins folder
        set "DEPLOY_FAILED=1"
    ) else (
        if exist "%EXE_DIR%\%%f" (
            if not exist "%EXE_DIR%\%%f.backup" (
                copy /y "%EXE_DIR%\%%f" "%EXE_DIR%\%%f.backup" >nul
                echo   Backed up original %%f to %%f.backup
            )
        )
        copy /y "%SRC_DIR%\%%f" "%EXE_DIR%\%%f" >nul
        echo   Deployed %%f
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
:: Shim-only mods never set installed_by_us=true; there's no separate loader.
set "WE_INSTALLED=false"
call :write_state_file

echo.
echo ========================================
echo   Installation Complete!
echo ========================================
echo.
echo Launch the game normally.
echo.
if defined MOD_CONTROLS (
    echo !MOD_CONTROLS!
    echo.
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
