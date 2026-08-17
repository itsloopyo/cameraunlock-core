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

call :detect_yes_flag %*
:: :detect_yes_flag and the arg parser both break if the shell left delayed
:: expansion on - cmd /V:ON, or DelayedExpansion=1 under
:: HKCU\Software\Microsoft\Command Processor. Under either, a "!" in the game
:: path is eaten out of the expanded line before the parser ever compares it, and
:: a real directory is rejected as a malformed argument. Moving the enable to
:: after :args_done is not enough on its own; the default has to be pinned OFF.
setlocal disabledelayedexpansion

call :main %*
set "_EC=%errorlevel%"
if not defined YES_FLAG ( echo. & pause )
exit /b %_EC%

:: ============================================
:: Pre-scan args at outer scope so YES_FLAG propagates to the post-:main
:: pause check. :main's arg parser sets its own (local) YES_FLAG too, but
:: cmd.exe discards local vars when setlocal pops on `exit /b`, so without
:: this pre-scan the post-:main `if not defined YES_FLAG` always pauses
:: and /y can't make the script headless. Quoted-string form is required
:: here - bracket form `if [%~1]==[/y]` does NOT quote, so a path arg
:: containing whitespace ("C:\...\Gone Home") splits across the brackets
:: and crashes cmd with "[Home]==[/y] was unexpected at this time". The
:: trailing-backslash hazard the bracket form was working around is moot
:: with `%~1`: it strips the launcher's surrounding quotes before the
:: comparison, so a value like `C:\foo\` can't escape the closing `"`.
:: ============================================
:detect_yes_flag
if "%~1"=="" exit /b 0
if /i "%~1"=="/y"    set "YES_FLAG=1"
if /i "%~1"=="-y"    set "YES_FLAG=1"
if /i "%~1"=="--yes" set "YES_FLAG=1"
shift
goto :detect_yes_flag

:main

:: Capture script dir BEFORE the arg parser runs. Inside `call :main`,
:: `shift` rotates %0 too, so %~dp0 read after shifts resolves to the
:: dirname of the first arg (e.g. C:\ for /y) instead of the script.
set "SCRIPT_DIR=%~dp0"

:: -------- Arg parser (canonical, do not modify) --------
:: Parsed with delayed expansion OFF; `setlocal enabledelayedexpansion`
:: deliberately comes after :args_done. With it on, cmd strips `!` out of the
:: expanded text of `set "_ARG=%~1"` - and out of `%~1` itself - so a real
:: game path like C:\Games\Oh! My Game silently loses the `!`, `if exist`
:: fails, and a valid directory is rejected as a malformed argument.
set "YES_FLAG="
set "_GIVEN_PATH="
:parse_args
if "%~1"=="" goto :args_done
set "_ARG=%~1"
if /i "%_ARG%"=="/y"    ( set "YES_FLAG=1" & shift & goto :parse_args )
if /i "%_ARG%"=="-y"    ( set "YES_FLAG=1" & shift & goto :parse_args )
if /i "%_ARG%"=="--yes" ( set "YES_FLAG=1" & shift & goto :parse_args )
if "%_ARG:~0,2%"=="--" ( echo ERROR: unknown flag "%_ARG%" & exit /b 2 )
if "%_ARG:~0,1%"=="/"  ( echo ERROR: unknown flag "%_ARG%" & exit /b 2 )
if "%_ARG:~0,1%"=="-"  ( echo ERROR: unknown flag "%_ARG%" & exit /b 2 )
if not defined _GIVEN_PATH (
    if exist "%_ARG%\" ( set "_GIVEN_PATH=%_ARG%" & shift & goto :parse_args )
)
echo ERROR: unrecognised argument "%_ARG%"
exit /b 2
:args_done
set "_ARG="

setlocal enabledelayedexpansion

:: -------- Validate CONFIG BLOCK --------
:: Every name below is interpolated straight into a path that gets written,
:: deleted or recursively removed. A blank one does not fail - it silently
:: retargets the operation at the parent directory, which is the game folder.
for %%v in (GAME_ID MOD_DISPLAY_NAME MOD_INTERNAL_NAME STATE_FILE FRAMEWORK_TYPE MOD_DLLS) do (
    if not defined %%v (
        echo ERROR: %%v is not set in this script's CONFIG BLOCK.
        exit /b 1
    )
)

echo.
echo === %MOD_DISPLAY_NAME% - Install ===
echo.

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

echo Game found: !GAME_PATH!

:: Derive EXE_DIR (where shim DLLs land) from GAME_PATH + GAME_EXE_RELPATH.
for %%i in ("%GAME_PATH%\%GAME_EXE_RELPATH%") do set "EXE_DIR=%%~dpi"
if "!EXE_DIR:~-1!"=="\" set "EXE_DIR=!EXE_DIR:~0,-1!"
echo Exe dir : !EXE_DIR!
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
:: in EXE_DIR, back it up to <name>.backup so uninstall can put the user's
:: pre-mod state back.
::
:: Only a FIRST install may capture that backup. When the game ships no such
:: DLL, run 1 has nothing to back up and installs ours; on run 2 the file at
:: the target is our own shim, and copying it to .backup would record the mod
:: as "the original" - uninstall would then restore the mod over itself and
:: report a clean removal. The state file is what tells the two apart.
echo Deploying shim files...

set "SRC_DIR=%SCRIPT_DIR%plugins"
set "DEPLOY_FAILED=0"

set "FIRST_INSTALL=1"
if exist "%GAME_PATH%\%STATE_FILE%" set "FIRST_INSTALL="

for %%f in (%MOD_DLLS%) do (
    if not exist "%SRC_DIR%\%%f" (
        echo   ERROR: %%f not found in plugins folder
        set "DEPLOY_FAILED=1"
    ) else (
        set "_BACKUP_OK=1"
        rem Decided PER FILE by CONTENT, not by whether this is the first install.
        rem Two failure modes have to be avoided at once. Backing up unconditionally
        rem enshrines OUR shim as "the original" on the second install of a game that
        rem ships no such DLL, so uninstall reinstalls the mod. Gating the whole backup
        rem on first-install instead means a DLL newly ADDED to MOD_DLLS in a later mod
        rem version overwrites the game's real file with no backup at all. Comparing the
        rem bytes answers the actual question: is the file already there ours?
        if exist "%EXE_DIR%\%%f" if not exist "%EXE_DIR%\%%f.backup" (
            fc /b "%EXE_DIR%\%%f" "%SRC_DIR%\%%f" >nul 2>&1
            if errorlevel 1 (
                copy /y "%EXE_DIR%\%%f" "%EXE_DIR%\%%f.backup" >nul
                if errorlevel 1 (
                    set "_BACKUP_OK="
                ) else (
                    echo   Backed up original %%f to %%f.backup
                )
            )
        )
        if defined _BACKUP_OK (
            copy /y "%SRC_DIR%\%%f" "%EXE_DIR%\%%f" >nul
            if errorlevel 1 (
                echo   ERROR: Failed to copy %%f - is the game folder writable?
                set "DEPLOY_FAILED=1"
            ) else (
                echo   Deployed %%f
            )
        ) else (
            echo   ERROR: Failed to back up the existing %%f - not overwriting it.
            set "DEPLOY_FAILED=1"
        )
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
:: Percent-expansion splits MOD_CONTROLS on its embedded &echo separators;
:: delayed expansion prints them literally. Kept outside a ( ) block so a
:: literal ) in the controls text cannot close the block.
if not defined MOD_CONTROLS goto :controls_done
echo %MOD_CONTROLS%
echo.
:controls_done
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
