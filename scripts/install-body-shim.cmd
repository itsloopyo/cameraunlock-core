@echo off
:: ============================================
:: CameraUnlock Shim-Only install body (shared)
:: ============================================
:: Source of truth: cameraunlock-core/scripts/install-body-shim.cmd.
:: Per-mod install.cmd wrappers set the CONFIG BLOCK + WRAPPER_DIR and
:: `call` here.
::
:: Shim-only mod: the mod DLL itself is a system-DLL shim (xinput1_3.dll,
:: dxgi.dll, winmm.dll, etc.) loaded by the game directly - no external
:: framework. No loader fetch/extract; just copy the DLL to EXE_DIR,
:: preserving any pre-existing system DLL as <name>.backup so uninstall
:: can restore it.
::
:: FRAMEWORK_TYPE is "None" on the state file. /force on uninstall is a
:: no-op for framework removal (there's nothing beyond the shim DLL).
::
:: Required env from the wrapper:
::   WRAPPER_DIR              wrapper's %~dp0
::   GAME_ID, MOD_DISPLAY_NAME, MOD_DLLS, MOD_INTERNAL_NAME, MOD_VERSION
::   STATE_FILE, FRAMEWORK_TYPE (always "None")
::   MOD_SEED_FILES           optional config files written only when absent
::   MOD_CONTROLS             optional post-install help text
::
:: Launcher CLI (passed through %*): [GAME_PATH] [/y] [/force]
::   /force is accepted and ignored - it only means something to uninstall.
:: ============================================

:: :detect_yes_flag and the arg parser below both break if the wrapper left
:: delayed expansion on (see :parse_args), so pin it off for the whole body.
setlocal disabledelayedexpansion

call :detect_yes_flag %*
call :main %*
set "_EC=%errorlevel%"
if not defined _NO_PAUSE ( echo. & pause )
exit /b %_EC%

:: ============================================
:: Pre-scan args at outer scope and record the pause decision in _NO_PAUSE,
:: which :main never writes. :main's own parser re-derives YES_FLAG as it goes
:: and only reaches the /y token after the path, so a pause keyed off that
:: variable sat there forever whenever parsing failed on an earlier argument -
:: which is `install.cmd "<path>" /y`, lopari's exact call shape.
::
:: `if [%1]==[]` and not `if "%~1"==""`: %~1 strips the quotes off an empty
:: argument, which makes `install.cmd "" /y` indistinguishable from no
:: arguments at all and swallows the /y behind it. The bracket form keeps the
:: launcher's quotes, so a path with whitespace stays one token. The
:: comparisons below still use the quoted-string form - bracket form
:: `if [%~1]==[/y]` does NOT quote, so a path arg containing whitespace
:: ("C:\...\Gone Home") splits across the brackets and crashes cmd with
:: "[Home]==[/y] was unexpected at this time".
:: ============================================
:detect_yes_flag
if [%1]==[] exit /b 0
if /i "%~1"=="/y"    set "_NO_PAUSE=1"
if /i "%~1"=="-y"    set "_NO_PAUSE=1"
if /i "%~1"=="--yes" set "_NO_PAUSE=1"
shift
goto :detect_yes_flag

:main

if defined WRAPPER_DIR ( set "SCRIPT_DIR=%WRAPPER_DIR%" ) else ( set "SCRIPT_DIR=%~dp0" )

:: -------- Arg parser (canonical, do not modify) --------
:: Parsed with delayed expansion OFF; `setlocal enabledelayedexpansion` comes
:: much further down, after the game path has been resolved. With it on, cmd
:: strips `!` out of the expanded text of `set "_ARG=%~1"` - and out of `%~1`
:: itself - so a real game path like C:\Games\Oh! My Game silently loses the
:: `!`, `if exist` fails, and a valid directory is rejected as malformed.
set "YES_FLAG="
set "_GIVEN_PATH="
:parse_args
if [%1]==[] goto :args_done
set "_ARG=%~1"
:: An argument that is there but empty is not "no arguments": a launcher that
:: expanded a variable it never filled in reaches here, and treating it as the
:: end of the list both loses the flags behind it and silently falls back to
:: detection, installing into whichever copy of the game happens to be on the
:: machine rather than the one the caller named.
if not defined _ARG (
    echo ERROR: empty path argument.
    echo Pass the game folder, or pass no argument at all to let detection find it.
    exit /b 2
)
if /i "%_ARG%"=="/y"    ( set "YES_FLAG=1" & shift & goto :parse_args )
if /i "%_ARG%"=="-y"    ( set "YES_FLAG=1" & shift & goto :parse_args )
if /i "%_ARG%"=="--yes" ( set "YES_FLAG=1" & shift & goto :parse_args )
:: /force is an uninstall flag. Install accepts and ignores it so that a
:: launcher passing one flag set to both scripts does not get exit 2 here.
if /i "%_ARG%"=="/force"  ( shift & goto :parse_args )
if /i "%_ARG%"=="--force" ( shift & goto :parse_args )
if "%_ARG:~0,2%"=="--" ( echo ERROR: unknown flag "%_ARG%" & exit /b 2 )
if "%_ARG:~0,1%"=="/"  ( echo ERROR: unknown flag "%_ARG%" & exit /b 2 )
if "%_ARG:~0,1%"=="-"  ( echo ERROR: unknown flag "%_ARG%" & exit /b 2 )
if not defined _GIVEN_PATH (
    if exist "%_ARG%\" ( set "_GIVEN_PATH=%_ARG%" & shift & goto :parse_args )
)
:: Two characters never survive the trip into a batch file's arguments: every
:: `call` in the chain doubles a `^`, and the extra expansion round `call` runs
:: eats a lone `%`. Both are already gone by the time this parser compares the
:: string, so name them rather than leave the user guessing why a folder that
:: plainly exists came back "unrecognised". Detection reaches those folders
:: fine - it is only the argument that cannot carry them.
echo ERROR: unrecognised argument "%_ARG%"
echo A path argument cannot carry a ^^ or a %% - cmd.exe doubles the first and
echo drops the second. Run without a path so detection finds the game instead.
exit /b 2
:args_done
set "_ARG="

:: The path cannot keep a trailing backslash: `-GivenPath "%_GIVEN_PATH%"`
:: would hand CommandLineToArgvW a `\"`, which is an escaped quote, and
:: PowerShell receives the path with a `"` stuck on the end - Test-Path then
:: throws "Illegal characters in path" and the run dies on a stack trace.
:: `%~1` strips the launcher's own quotes, so `C:\Games\Foo\` passes the
:: `if exist` above and gets that far.
:strip_given_slash
if not defined _GIVEN_PATH goto :given_normalised
if not "%_GIVEN_PATH:~-1%"=="\" goto :given_normalised
if "%_GIVEN_PATH:~-2%"==":\" (
    rem A drive root has no backslash to spare, so end the value on a `.`
    rem instead: same directory, and it no longer escapes the closing quote.
    set "_GIVEN_PATH=%_GIVEN_PATH%."
    goto :given_normalised
)
set "_GIVEN_PATH=%_GIVEN_PATH:~0,-1%"
goto :strip_given_slash
:given_normalised

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
:: -GivenPath is spelled out in both branches rather than built into one
:: variable and expanded unquoted: the quotes are what keep a `&`, `^` or `)`
:: in the user's path from being parsed as syntax.
if defined _GIVEN_PATH (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%_SHIM%" -GameId %GAME_ID% -OutFile "%_SHIM_OUT%" -GivenPath "%_GIVEN_PATH%"
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%_SHIM%" -GameId %GAME_ID% -OutFile "%_SHIM_OUT%"
)
set "_PS_EC=%errorlevel%"
if not "%_PS_EC%"=="0" (
    echo.
    echo ERROR: Could not resolve game install path ^(shim exit code %_PS_EC%^).
    echo Pass a path explicitly: install.cmd "C:\path\to\game"
    echo.
    del "%_SHIM_OUT%" 2>nul
    exit /b 1
)
call "%_SHIM_OUT%"
del "%_SHIM_OUT%" 2>nul

:: Derive EXE_DIR from GAME_PATH + GAME_EXE_RELPATH, still with expansion off:
:: a FOR variable is substituted before the `!` scan, so `set "EXE_DIR=%%~dpi"`
:: would drop a `!` from the path if this ran below.
for %%i in ("%GAME_PATH%\%GAME_EXE_RELPATH%") do set "EXE_DIR=%%~dpi"
if "%EXE_DIR:~-1%"=="\" set "EXE_DIR=%EXE_DIR:~0,-1%"

:: Delayed expansion is enabled HERE and not one line earlier. Everything the
:: shim resolved is already in the environment, and `!VAR!` hands the value back
:: byte-for-byte: it is substituted after cmd.exe has finished looking for `!`,
:: `&`, `^` and `)`, so a path like C:\Games\Oh! My Game survives. `%VAR%` is
:: substituted before that scan and would lose the `!`. Every path below is
:: therefore `!`-expanded; the arg parser, the shim call and the EXE_DIR
:: derivation above all run with expansion off for the same reason.
setlocal enabledelayedexpansion

echo Game found: !GAME_PATH!

echo Exe dir : !EXE_DIR!
echo.

:: -------- Game-running check --------
:: /c: or findstr reads the exe name as a space-separated list of terms and
:: matches on ANY of them. With nothing running tasklist prints "INFO: No tasks
:: are running which match the specified criteria.", so an exe whose name
:: contains one of those words - "South Park - The Stick of Truth.exe" - matched
:: that line, and the install refused to run on every machine, forever.
tasklist /fi "imagename eq !GAME_EXE!" 2>nul | findstr /i /c:"!GAME_EXE!" >nul 2>&1
if not errorlevel 1 (
    echo ERROR: !GAME_DISPLAY_NAME! is currently running.
    echo Please close the game before installing.
    echo.
    exit /b 1
)

:: -------- Deploy shim DLL(s) --------
:: MOD_DLLS are system-DLL shims: the game loads them by the name Windows would
:: have loaded, so a file already sitting at that name is the game's own and is
:: copied to <name>.backup before ours goes over it. Uninstall restores it.
::
:: MOD_SEED_FILES are the mod's own config, and they go through the loop below
:: this one instead: written once, never overwritten, never backed up. Listing a
:: config in MOD_DLLS puts it through the unconditional copy and the byte
:: compare, and both readings of it are wrong - the copy resets every key the
:: user tuned, and the compare then records that tuned file as "the game's
:: original", so uninstall plants a mod file back in the game folder.
echo Deploying shim files...

set "SRC_DIR=!SCRIPT_DIR!plugins"
set "DEPLOY_FAILED=0"

:: Seeded before the shim DLLs, and copied only when absent: an upgrade has to
:: keep the values the user tuned.
if defined MOD_SEED_FILES (
    for %%f in (%MOD_SEED_FILES%) do (
        if exist "!SRC_DIR!\%%f" (
            if exist "!EXE_DIR!\%%f" (
                echo   Kept your existing %%f
            ) else (
                copy /y "!SRC_DIR!\%%f" "!EXE_DIR!\" >nul
                if errorlevel 1 (
                    echo   ERROR: Failed to copy %%f - is the game folder writable?
                    set "DEPLOY_FAILED=1"
                ) else (
                    echo   Deployed default %%f
                )
            )
        ) else (
            echo   ERROR: %%f not found in plugins folder
            set "DEPLOY_FAILED=1"
        )
    )
)

for %%f in (%MOD_DLLS%) do (
    if not exist "!SRC_DIR!\%%f" (
        echo   ERROR: %%f not found in plugins folder
        set "DEPLOY_FAILED=1"
    ) else (
        set "_BACKUP_OK=1"
        rem Decided PER FILE by CONTENT, not by whether this is the first install.
        rem Two failure modes have to be avoided at once. Backing up unconditionally
        rem enshrines OUR shim as "the original" on the second install of a game that
        rem ships no such DLL, so uninstall reinstalls the mod. Gating the whole backup
        rem on first-install instead means a DLL newly ADDED to MOD_DLLS in a later mod
        rem version overwrites the game's real file with no backup at all. Comparing
        rem the bytes answers the actual question for a BINARY we ship whole and never
        rem edit: is the file already there ours? It answers it wrongly for anything the
        rem user or the game writes to, which is why config belongs in MOD_SEED_FILES.
        if exist "!EXE_DIR!\%%f" if not exist "!EXE_DIR!\%%f.backup" (
            fc /b "!EXE_DIR!\%%f" "!SRC_DIR!\%%f" >nul 2>&1
            if errorlevel 1 (
                copy /y "!EXE_DIR!\%%f" "!EXE_DIR!\%%f.backup" >nul
                if errorlevel 1 (
                    set "_BACKUP_OK="
                ) else (
                    echo   Backed up original %%f to %%f.backup
                )
            )
        )
        if defined _BACKUP_OK (
            copy /y "!SRC_DIR!\%%f" "!EXE_DIR!\%%f" >nul
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
    echo   Deployment Failed^^!
    echo ========================================
    echo.
    exit /b 1
)

:: -------- Write state file --------
:: Shim-only mods never set installed_by_us=true; there's no separate loader.
set "WE_INSTALLED=false"
call :stamp_installed_at
if errorlevel 1 exit /b 1
call :write_state_file

echo.
echo ========================================
echo   Installation Complete^^!
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

:: UTC ISO-8601, read through PowerShell: %DATE% is whatever the user's regional
:: settings say and is not parseable, and WMIC is gone from current Windows 11.
:: PowerShell already resolved the game path above, so a failure here is a real
:: one and is reported rather than papered over with a placeholder date.
:stamp_installed_at
set "INSTALLED_AT="
for /f "usebackq delims=" %%I in (`powershell -NoProfile -Command "[DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')"`) do set "INSTALLED_AT=%%I"
if not defined INSTALLED_AT (
    echo ERROR: could not read the current UTC time from PowerShell.
    exit /b 1
)
exit /b 0

:: ============================================
:: Write the canonical state file. Schema version 1: schema_version,
:: framework.type, framework.installed_by_us, mod.id, mod.name, mod.version and
:: mod.installed_at are written by every body; framework.version is the single
:: optional field, emitted only where the CONFIG BLOCK names a loader version.
:: WE_INSTALLED may be already-true from a prior install and is preserved.
:: ============================================
:write_state_file
> "!GAME_PATH!\%STATE_FILE%" (
    echo {
    echo   "schema_version": 1,
    echo   "framework": {
    echo     "type": "%FRAMEWORK_TYPE%",
    echo     "installed_by_us": !WE_INSTALLED!
    echo   },
    echo   "mod": {
    echo     "id": "%GAME_ID%",
    echo     "name": "%MOD_INTERNAL_NAME%",
    echo     "version": "%MOD_VERSION%",
    echo     "installed_at": "!INSTALLED_AT!"
    echo   }
    echo }
)
exit /b 0
