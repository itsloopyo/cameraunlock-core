@echo off
:: ============================================
:: CameraUnlock BeamNG user-mods install body (shared)
:: ============================================
:: Source of truth: cameraunlock-core/scripts/install-body-beamng.cmd.
:: Per-mod install.cmd wrappers set the CONFIG BLOCK + WRAPPER_DIR and
:: `call` here.
::
:: BeamNG loads mods from the per-user folder, not from the game install
:: directory. The payload is a game-ready mod archive that goes into
:: <user folder>\mods\ and the game reads it from there. Nothing is written
:: into the game folder and there is no loader to install, which is why
:: FRAMEWORK_TYPE is BeamNGUserMods and installed_by_us is always false.
::
:: [GAME_PATH] is still the first positional argument and the game is still
:: resolved through find-game.ps1: the launcher passes a path, the
:: running-game check needs the executable name, and the state file goes to
:: %GAME_PATH%\%STATE_FILE% where the launcher looks for it. The DEPLOY
:: directory is resolved separately, from BEAMNG_USER_FOLDER or from the
:: folder BeamNG creates by default, because no path under the game install
:: reaches it.
::
:: The archives named in MOD_DLLS sit next to install.cmd, at the root of the
:: release ZIP: a BeamNG mod ships as one archive, so there is no plugins\
:: subfolder to hold it.
::
:: Required env from the wrapper:
::   WRAPPER_DIR              wrapper's %~dp0
::   GAME_ID, MOD_DISPLAY_NAME, MOD_DLLS, MOD_INTERNAL_NAME, MOD_VERSION
::   STATE_FILE, FRAMEWORK_TYPE (always "BeamNGUserMods")
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

:: Where the payload archives are: next to install.cmd, at the release ZIP
:: root. Derived here, with delayed expansion still off, because a substring
:: is substituted before the `!` scan - below the `setlocal` it would drop a
:: `!` out of a user profile path that contains one.
set "SRC_DIR=%SCRIPT_DIR%"
if "%SRC_DIR:~-1%"=="\" set "SRC_DIR=%SRC_DIR:~0,-1%"

:: -------- Resolve the BeamNG user folder --------
:: Precedence, not a fallback chain: BEAMNG_USER_FOLDER wins outright, and a
:: value that is set and wrong fails below rather than quietly deploying into
:: the default folder, where the player would never look for it. `current` is
:: the folder BeamNG keeps the running version's user data in.
if defined BEAMNG_USER_FOLDER (
    set "USER_FOLDER=%BEAMNG_USER_FOLDER%"
) else (
    set "USER_FOLDER=%LOCALAPPDATA%\BeamNG\BeamNG.drive\current"
)
if "%USER_FOLDER:~-1%"=="\" set "USER_FOLDER=%USER_FOLDER:~0,-1%"

:: Delayed expansion is enabled HERE and not one line earlier. Everything the
:: shim resolved is already in the environment, and `!VAR!` hands the value back
:: byte-for-byte: it is substituted after cmd.exe has finished looking for `!`,
:: `&`, `^` and `)`, so a path like C:\Games\Oh! My Game survives. `%VAR%` is
:: substituted before that scan and would lose the `!`. Every path below is
:: therefore `!`-expanded; the arg parser, the shim call and the two
:: derivations above all run with expansion off for the same reason.
setlocal enabledelayedexpansion

echo Game found : !GAME_PATH!
echo User folder: !USER_FOLDER!
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

:: -------- Resolve the deploy directory --------
:: The user folder is the game's to create, so its absence is an answer rather
:: than something to paper over: a folder made here would be missing everything
:: else BeamNG puts in one, and the mod would sit in a profile the player never
:: loads. mods\ inside it is ours to create - a player who has never installed a
:: mod does not have one yet.
if not exist "!USER_FOLDER!\" (
    echo ERROR: BeamNG user folder not found:
    echo   !USER_FOLDER!
    echo BeamNG creates it the first time it runs. Start the game once, quit, and
    echo run this installer again. If your user folder is elsewhere, set
    echo BEAMNG_USER_FOLDER to it first.
    echo.
    exit /b 1
)
set "DEPLOY_DIR=!USER_FOLDER!\mods"
if not exist "!DEPLOY_DIR!\" mkdir "!DEPLOY_DIR!" >nul 2>&1
if not exist "!DEPLOY_DIR!\" (
    echo ERROR: could not create !DEPLOY_DIR!.
    echo.
    exit /b 1
)

:: -------- Deploy the mod archive(s) --------
echo Deploying mod files...

set "DEPLOY_FAILED=0"

:: Seeded before the archives, and copied only when absent: an upgrade has to
:: keep whatever the user tuned.
if defined MOD_SEED_FILES (
    for %%f in (%MOD_SEED_FILES%) do (
        if exist "!SRC_DIR!\%%f" (
            if exist "!DEPLOY_DIR!\%%f" (
                echo   Kept your existing %%f
            ) else (
                copy /y "!SRC_DIR!\%%f" "!DEPLOY_DIR!\" >nul
                if errorlevel 1 (
                    echo   ERROR: Failed to copy %%f - is the user folder writable?
                    set "DEPLOY_FAILED=1"
                ) else (
                    echo   Deployed default %%f
                )
            )
        ) else (
            echo   ERROR: %%f not found next to install.cmd
            set "DEPLOY_FAILED=1"
        )
    )
)

for %%f in (%MOD_DLLS%) do (
    if not exist "!SRC_DIR!\%%f" (
        echo   ERROR: %%f not found next to install.cmd
        set "DEPLOY_FAILED=1"
    ) else (
        copy /y "!SRC_DIR!\%%f" "!DEPLOY_DIR!\" >nul
        if errorlevel 1 (
            echo   ERROR: Failed to copy %%f - is the user folder writable?
            set "DEPLOY_FAILED=1"
        ) else (
            echo   Deployed %%f
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
:: There is no loader to install - BeamNG reads its own mods folder - so
:: nothing was put on disk on a framework's behalf and installed_by_us is
:: never true.
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
:: It goes to the game install root even though the payload did not, because
:: that is the one path the launcher can compute for itself.
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
