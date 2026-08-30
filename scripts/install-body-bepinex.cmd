@echo off
:: ============================================
:: CameraUnlock BepInEx install body (shared)
:: ============================================
:: Source of truth: cameraunlock-core/scripts/install-body-bepinex.cmd.
:: Per-mod install.cmd wrappers set the CONFIG BLOCK + WRAPPER_DIR and
:: `call` here. Resolved from <wrapper_dir>/shared/ in release zips, or
:: from <wrapper_dir>/../cameraunlock-core/scripts/ in the dev tree.
::
:: Covers two BepInEx variants, dispatched by BEPINEX_SUBFOLDER:
::   * Regular BepInEx: leave BEPINEX_SUBFOLDER empty. Vendor zip is
::     BepInEx_win_<arch>.zip, extracted directly to the game root.
::   * Thunderstore-wrapped (BepInExPack_<Game>): set BEPINEX_SUBFOLDER
::     to the wrapper dir name. Override BEPINEX_VENDOR_ZIP_NAME to the
::     actual zip filename. We extract to a temp dir and flatten the
::     wrapper subfolder's contents into the game root.
::
:: Required env from the wrapper:
::   WRAPPER_DIR              wrapper's %~dp0 (release zip root or <mod>/scripts/)
::   GAME_ID                  games.json id
::   MOD_DISPLAY_NAME         banner / status text
::   MOD_DLLS                 space-separated DLL filenames in plugins/
::   MOD_INTERNAL_NAME        state-file mod.name
::   MOD_VERSION              state-file mod.version
::   STATE_FILE               state file basename
::   FRAMEWORK_TYPE           always "BepInEx"
::   BEPINEX_ARCH             "x64" or "x86" (vendor zip selector)
::   BEPINEX_VENDOR_ZIP_NAME  optional override (Thunderstore packs)
::   BEPINEX_SUBFOLDER        optional Thunderstore wrapper dir
::   PLUGIN_SUBFOLDER         optional subfolder under BepInEx\plugins\
::                            to deploy DLLs into (e.g. Valheim).
::                            When set, also removes any flat-laid
::                            copies of MOD_DLLS in plugins\ to prevent
::                            duplicate-load conflicts.
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

:: WRAPPER_DIR is the wrapper's %~dp0 (release-zip root or <mod>/scripts/).
:: Resolved here as SCRIPT_DIR so the rest of the body reads naturally.
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

:: Delayed expansion is enabled HERE and not one line earlier. Everything the
:: shim resolved is already in the environment, and `!VAR!` hands the value back
:: byte-for-byte: it is substituted after cmd.exe has finished looking for `!`,
:: `&`, `^` and `)`, so a path like C:\Games\Oh! My Game survives. `%VAR%` is
:: substituted before that scan and would lose the `!`. Every path below is
:: therefore `!`-expanded; the arg parser, the shim call and the EXE_DIR
:: derivation above all run with expansion off for the same reason.
setlocal enabledelayedexpansion

echo Game found: !GAME_PATH!
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

:: -------- Prior state: preserve installed_by_us=true across re-installs --------
set "WE_INSTALLED=false"
if exist "!GAME_PATH!\%STATE_FILE%" (
    findstr /c:"installed_by_us" "!GAME_PATH!\%STATE_FILE%" 2>nul | findstr /c:"true" >nul 2>&1
    if not errorlevel 1 set "WE_INSTALLED=true"
)

:: -------- Ensure BepInEx --------
:: Loader-presence check: BepInEx 5 ships BepInEx/core/BepInEx.dll;
:: BepInEx 6 (IL2CPP) renamed the core assembly to BepInEx.Core.dll, so
:: check both. If a loader is there, also arch-check the loader proxy
:: (winhttp.dll) against BEPINEX_ARCH before trusting it - other mod
:: managers (TMM/r2modman) drop the generic x64 BepInExPack into x86
:: Unity 2017 games, which silently prevents the loader from injecting
:: (32-bit process can't load 64-bit DLL). Without this gate we deploy
:: plugins onto a dead loader and the game launches vanilla.
set "_LOADER_PRESENT="
if exist "!GAME_PATH!\BepInEx\core\BepInEx.dll"      set "_LOADER_PRESENT=1"
if exist "!GAME_PATH!\BepInEx\core\BepInEx.Core.dll" set "_LOADER_PRESENT=1"

set "_LOADER_BAD="
if defined _LOADER_PRESENT (
    call :verify_loader_arch
    if errorlevel 1 set "_LOADER_BAD=1"
)

if not defined _LOADER_PRESENT goto :install_loader
if defined _LOADER_BAD goto :replace_loader

:: Right arch, right marker file - but a marker check alone still calls a
:: half-deleted BepInEx "installed". core\BepInEx.dll survives while
:: doorstop_config.ini and the Cecil/MonoMod assemblies go missing, Doorstop
:: loads with no target assembly, the chainloader never runs, and the broken
:: install survives a reinstall as "the mod just stopped working".
call :check_bepinex_health
if errorlevel 1 exit /b 1
if defined _LOADER_FOREIGN (
    echo Existing BepInEx is a different edition to the bundled one, skipping loader install, deploying plugin only.
    goto :after_loader
)
if "!_LOADER_MISSING!"=="0" (
    echo Existing BepInEx detected, skipping loader install, deploying plugin only.
    goto :after_loader
)

:repair_loader
echo Existing BepInEx is incomplete - !_LOADER_MISSING! of its files are missing. Repairing from the bundled copy.
echo.
call :install_bepinex
if errorlevel 1 exit /b 1
call :check_bepinex_health
if errorlevel 1 exit /b 1
if not "!_LOADER_MISSING!"=="0" (
    echo   ERROR: BepInEx is still incomplete after extraction.
    echo   The installer ZIP is corrupt. Re-download the release.
    exit /b 1
)
:: A repair does not transfer ownership: WE_INSTALLED keeps whatever the prior
:: state file said, so uninstall still leaves a loader the user installed
:: themselves intact.
goto :after_loader

:replace_loader
echo Existing BepInEx is the wrong architecture for this game ^(expected %BEPINEX_ARCH%^).
echo This usually means another mod manager ^(Thunderstore Mod Manager,
echo r2modman, ...^) installed the wrong BepInExPack first.
echo The loader core will be replaced with the matching %BEPINEX_ARCH% build.
echo Your BepInEx\plugins\ and BepInEx\config\ are set aside and put back.
if not defined YES_FLAG (
    echo.
    set /p "_REPLY=Replace the wrong-architecture BepInEx? [y/N] "
    if /i not "!_REPLY!"=="y" (
        echo Aborted - nothing was changed.
        exit /b 1
    )
)
call :wipe_existing_bepinex
if errorlevel 1 exit /b 1
goto :do_install_loader

:install_loader
echo BepInEx not found. Installing...

:do_install_loader
echo.
call :install_bepinex
set "_BEP_INSTALL_EC=!errorlevel!"
if not "!_BEP_INSTALL_EC!"=="0" (
    echo   Loader install failed - putting your BepInEx plugins/config back.
    call :restore_bepinex_stash
    exit /b 1
)
call :restore_bepinex_stash
if errorlevel 1 exit /b 1
set "WE_INSTALLED=true"
echo.
:: Single-pass install: BepInEx bootstraps on first game launch and loads
:: any plugins it finds in BepInEx\plugins\ at that point. The previous
:: two-phase prompt was defensive against edge cases the /y path already
:: trusted away.
echo BepInEx installed. It will initialize on first game launch.

:after_loader
echo.

:: -------- Deploy mod files --------
echo Deploying mod files...

set "PLUGINS_PATH=!GAME_PATH!\BepInEx\plugins"
set "DLL_DIR=!SCRIPT_DIR!plugins"
if defined PLUGIN_SUBFOLDER (
    set "DEPLOY_PATH=!PLUGINS_PATH!\%PLUGIN_SUBFOLDER%"
) else (
    set "DEPLOY_PATH=!PLUGINS_PATH!"
)

if not exist "!PLUGINS_PATH!" mkdir "!PLUGINS_PATH!"
if not exist "!DEPLOY_PATH!" mkdir "!DEPLOY_PATH!"

if defined PLUGIN_SUBFOLDER (
    for %%f in (%MOD_DLLS%) do (
        if exist "!PLUGINS_PATH!\%%f" (
            del /q "!PLUGINS_PATH!\%%f" >nul 2>&1
            echo   Removed flat-laid %%f from plugins\ ^(superseded by %PLUGIN_SUBFOLDER%\^)
        )
    )
)

set "DEPLOY_FAILED=0"
for %%f in (%MOD_DLLS%) do (
    if exist "!DLL_DIR!\%%f" (
        copy /y "!DLL_DIR!\%%f" "!DEPLOY_PATH!\" >nul
        if errorlevel 1 (
            echo   ERROR: Failed to copy %%f - is the game folder writable?
            set "DEPLOY_FAILED=1"
        ) else (
            echo   Deployed %%f
        )
    ) else (
        echo   ERROR: %%f not found in plugins folder
        set "DEPLOY_FAILED=1"
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
call :stamp_installed_at
if errorlevel 1 exit /b 1
call :write_state_file

echo.
echo ========================================
echo   Deployment Complete^^!
echo ========================================
echo.
echo %MOD_DISPLAY_NAME% has been deployed to:
echo   !PLUGINS_PATH!
echo.
echo Start the game to use the mod^^!
:: Percent-expansion splits MOD_CONTROLS on its embedded &echo separators;
:: delayed expansion prints them literally. Kept outside a ( ) block so a
:: literal ) in the controls text cannot close the block.
if not defined MOD_CONTROLS goto :controls_done
echo.
echo %MOD_CONTROLS%
:controls_done
echo.
exit /b 0

:: ============================================
:: Verify the existing loader proxy (winhttp.dll) matches BEPINEX_ARCH.
:: Returns:
::   0 - arch matches, existing loader is trustworthy
::   1 - winhttp.dll missing OR wrong arch OR not a valid PE - caller
::       should wipe + reinstall
:: ============================================
:verify_loader_arch
:: BepInEx.dll without winhttp.dll = broken loader stack regardless of arch.
if not exist "!GAME_PATH!\winhttp.dll" exit /b 1
set "_ARCH_SHIM=!SCRIPT_DIR!shared\check-loader-arch.ps1"
if not exist "!_ARCH_SHIM!" set "_ARCH_SHIM=!SCRIPT_DIR!..\cameraunlock-core\scripts\check-loader-arch.ps1"
if not exist "!_ARCH_SHIM!" (
    echo   ERROR: check-loader-arch.ps1 not found in shared\ or ..\cameraunlock-core\scripts\.
    echo   If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo   If this is the dev tree, make sure the cameraunlock-core submodule is checked out.
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "!_ARCH_SHIM!" -Path "!GAME_PATH!\winhttp.dll" -ExpectedArch %BEPINEX_ARCH%
exit /b %errorlevel%

:: ============================================
:: Compare the installed loader against the bundled zip's own file list, and
:: report through _LOADER_MISSING (a count) and _LOADER_FOREIGN.
::
:: Only .dll and .ini entries count. The XML doc files and changelog.txt in the
:: zip are not load-bearing, and a user who cleaned them out does not need a
:: repair. _LOADER_FOREIGN guards the other direction: an install that does not
:: carry the same core assembly the bundled zip does is a different BepInEx
:: edition, belonging to someone else's setup, and extracting our core over it
:: would break theirs.
:: ============================================
:check_bepinex_health
set "_LOADER_MISSING=0"
set "_LOADER_FOREIGN="
call :resolve_vendor_zip
if not exist "!VENDOR_ZIP!" (
    echo   ERROR: Bundled BepInEx not found at:
    echo     !VENDOR_ZIP!
    echo   The installer ZIP is corrupt. Re-download the release.
    exit /b 1
)

set "_ZIP_LIST=!TEMP!\cul-bepinex-list-%RANDOM%-%RANDOM%.txt"
"%SystemRoot%\System32\tar.exe" -tf "!VENDOR_ZIP!" > "!_ZIP_LIST!"
if errorlevel 1 (
    del "!_ZIP_LIST!" 2>nul
    echo   ERROR: could not list the bundled BepInEx zip:
    echo     !VENDOR_ZIP!
    echo   The installer ZIP is corrupt. Re-download the release.
    exit /b 1
)

:: BepInEx 5 ships core\BepInEx.dll; BepInEx 6 (IL2CPP) renamed it to
:: BepInEx.Core.dll. Whichever the bundled zip carries is the one to compare
:: against, so read it off the listing rather than assuming an edition.
set "_VENDOR_MARKER="
for /f "usebackq delims=" %%e in ("!_ZIP_LIST!") do (
    call :normalize_zip_entry "%%e"
    if /i "!_ENTRY!"=="BepInEx\core\BepInEx.dll"      set "_VENDOR_MARKER=!_ENTRY!"
    if /i "!_ENTRY!"=="BepInEx\core\BepInEx.Core.dll" set "_VENDOR_MARKER=!_ENTRY!"
)
if not defined _VENDOR_MARKER (
    del "!_ZIP_LIST!" 2>nul
    echo   ERROR: the bundled zip carries no BepInEx\core\BepInEx.dll, so it is
    echo   not a BepInEx package: !VENDOR_ZIP!
    echo   The installer ZIP is corrupt. Re-download the release.
    exit /b 1
)

if not exist "!GAME_PATH!\!_VENDOR_MARKER!" (
    set "_LOADER_FOREIGN=1"
    del "!_ZIP_LIST!" 2>nul
    exit /b 0
)

for /f "usebackq delims=" %%e in ("!_ZIP_LIST!") do (
    call :normalize_zip_entry "%%e"
    set "_WANT="
    if /i "!_ENTRY:~-4!"==".dll" set "_WANT=1"
    if /i "!_ENTRY:~-4!"==".ini" set "_WANT=1"
    if defined _WANT if not exist "!GAME_PATH!\!_ENTRY!" set /a _LOADER_MISSING+=1
)

del "!_ZIP_LIST!" 2>nul
exit /b 0

:: Turn one zip entry into a game-relative path: drop the Thunderstore wrapper
:: directory :install_bepinex flattens away, and swap the separators.
:normalize_zip_entry
set "_ENTRY=%~1"
if defined BEPINEX_SUBFOLDER set "_ENTRY=!_ENTRY:*%BEPINEX_SUBFOLDER%/=!"
set "_ENTRY=!_ENTRY:/=\!"
exit /b 0

:: ============================================
:: Resolve the bundled loader zip into VENDOR_ZIP. Release ZIP layout has
:: vendor/ flat next to install.cmd; the dev tree has install.cmd in
:: <repo>/scripts/ and vendor/ at <repo>/vendor/.
:: ============================================
:resolve_vendor_zip
set "VENDOR_DIR=!SCRIPT_DIR!vendor\bepinex"
if not exist "!VENDOR_DIR!" set "VENDOR_DIR=!SCRIPT_DIR!..\vendor\bepinex"
if defined BEPINEX_VENDOR_ZIP_NAME (
    set "VENDOR_ZIP=!VENDOR_DIR!\%BEPINEX_VENDOR_ZIP_NAME%"
) else (
    set "VENDOR_ZIP=!VENDOR_DIR!\BepInEx_win_%BEPINEX_ARCH%.zip"
)
exit /b 0

:: ============================================
:: Wipe an existing wrong-arch BepInEx loader so :install_bepinex can lay
:: down our matching one. The loader core is ours to replace, but
:: BepInEx\plugins\ and BepInEx\config\ are NOT - a Thunderstore user can
:: easily have five other plugins and their configs under there, and
:: rmdir /s on BepInEx\ took the lot. Move both aside first;
:: :restore_bepinex_stash puts them back once the new core is extracted.
:: ============================================
:wipe_existing_bepinex
echo   Replacing the wrong-architecture BepInEx core...
:: Staged INSIDE the game folder under a fixed name, not %!TEMP!% under a
:: random one. The old form was unrecoverable: :restore_bepinex_stash only
:: ran on the success path, so any loader-install failure (missing vendored
:: zip, extraction error, xcopy error) - or a Ctrl+C - deleted BepInEx\ from
:: the game and left the user's other mods' plugins and every plugin config
:: in a %!TEMP!% folder whose randomised name was never printed. Same volume
:: also makes the move atomic rather than a copy.
set "_BEP_STASH=!GAME_PATH!\BepInEx-cul-stash"
:: A stash already here means a previous run died between the move and the
:: restore. Put it back before doing anything else, or this run overwrites
:: the only copy of the user's data.
if exist "!_BEP_STASH!" (
    echo   Found plugins/config staged by an earlier interrupted run - restoring first.
    call :restore_bepinex_stash
    if errorlevel 1 exit /b 1
)
set "_BEP_STASH="
if exist "!GAME_PATH!\BepInEx\plugins" set "_BEP_STASH=!GAME_PATH!\BepInEx-cul-stash"
if exist "!GAME_PATH!\BepInEx\config" if not defined _BEP_STASH set "_BEP_STASH=!GAME_PATH!\BepInEx-cul-stash"
if defined _BEP_STASH (
    mkdir "!_BEP_STASH!"
    if errorlevel 1 (
        echo   ERROR: could not create the staging folder next to BepInEx\.
        echo   Is the game folder writable?
        exit /b 1
    )
)
if exist "!GAME_PATH!\BepInEx\plugins" (
    move "!GAME_PATH!\BepInEx\plugins" "!_BEP_STASH!\plugins" >nul
    if errorlevel 1 (
        echo   ERROR: could not set aside BepInEx\plugins\ - refusing to delete it.
        exit /b 1
    )
    echo   Set aside your BepInEx\plugins\
)
if exist "!GAME_PATH!\BepInEx\config" (
    move "!GAME_PATH!\BepInEx\config" "!_BEP_STASH!\config" >nul
    if errorlevel 1 (
        echo   ERROR: could not set aside BepInEx\config\ - refusing to delete it.
        exit /b 1
    )
    echo   Set aside your BepInEx\config\
)
if exist "!GAME_PATH!\BepInEx" rmdir /s /q "!GAME_PATH!\BepInEx"
if exist "!GAME_PATH!\winhttp.dll" del /f /q "!GAME_PATH!\winhttp.dll"
if exist "!GAME_PATH!\doorstop_config.ini" del /f /q "!GAME_PATH!\doorstop_config.ini"
if exist "!GAME_PATH!\.doorstop_version" del /f /q "!GAME_PATH!\.doorstop_version"
if exist "!GAME_PATH!\changelog.txt" del /f /q "!GAME_PATH!\changelog.txt"
exit /b 0

:: ============================================
:: Put the plugins/ and config/ trees :wipe_existing_bepinex set aside back
:: over the freshly extracted loader. No-op on a first install, where nothing
:: was stashed. robocopy is used rather than xcopy because it reports success
:: for an empty source tree; its exit codes below 8 are all success.
:: ============================================
:restore_bepinex_stash
:: Resolve from the fixed path rather than the variable, so this still works
:: on a later run whose _BEP_STASH was never set.
set "_BEP_STASH=!GAME_PATH!\BepInEx-cul-stash"
if not exist "!_BEP_STASH!" exit /b 0
robocopy "!_BEP_STASH!" "!GAME_PATH!\BepInEx" /e /njh /njs /ndl /nfl /nc /ns >nul
if errorlevel 8 (
    echo   ERROR: failed to restore your BepInEx plugins/config from:
    echo     !_BEP_STASH!
    echo   They are still in that folder - move them back by hand.
    exit /b 1
)
rmdir /s /q "!_BEP_STASH!"
echo   Restored your BepInEx\plugins\ and BepInEx\config\
exit /b 0

:: ============================================
:: Install BepInEx from the bundled vendored copy.
:: Vendor tree is the single source of truth at install time. To bump the
:: bundled version, run `pixi run update-deps` in the mod repo and commit.
:: ============================================
:install_bepinex
call :resolve_vendor_zip

if not exist "!VENDOR_ZIP!" (
    echo   ERROR: Bundled BepInEx not found at:
    echo     !VENDOR_ZIP!
    echo   The installer ZIP is corrupt. Re-download the release.
    exit /b 1
)

echo   Extracting bundled BepInEx to game directory...
if defined BEPINEX_SUBFOLDER (
    rem Thunderstore BepInExPack: extract to temp, flatten wrapper into GAME_PATH.
    set "BEP_TEMP=!TEMP!\cul-bepinex-extract-%RANDOM%-%RANDOM%"
    mkdir "!BEP_TEMP!"
    "%SystemRoot%\System32\tar.exe" -xf "!VENDOR_ZIP!" -C "!BEP_TEMP!"
    if errorlevel 1 (
        echo   ERROR: Extraction failed.
        rmdir /s /q "!BEP_TEMP!" 2>nul
        exit /b 1
    )
    xcopy /s /e /y /q "!BEP_TEMP!\%BEPINEX_SUBFOLDER%\*" "!GAME_PATH!\" >nul
    set "_XCOPY_EC=!errorlevel!"
    rmdir /s /q "!BEP_TEMP!"
    if not "!_XCOPY_EC!"=="0" (
        echo   ERROR: Failed to copy BepInEx into the game folder ^(xcopy exit code !_XCOPY_EC!^).
        echo   Check the game directory is writable.
        exit /b 1
    )
) else (
    "%SystemRoot%\System32\tar.exe" -xf "!VENDOR_ZIP!" -C "!GAME_PATH!"
    if errorlevel 1 (
        echo   ERROR: Extraction failed.
        exit /b 1
    )
)

if not exist "!GAME_PATH!\BepInEx\plugins" mkdir "!GAME_PATH!\BepInEx\plugins"

:: Enable console + disk logging. Skip if BepInEx.cfg already exists
:: (Thunderstore packs ship preconfigured; don't clobber).
if not exist "!GAME_PATH!\BepInEx\config\BepInEx.cfg" (
    if not exist "!GAME_PATH!\BepInEx\config" mkdir "!GAME_PATH!\BepInEx\config"
    > "!GAME_PATH!\BepInEx\config\BepInEx.cfg" (
        echo [Logging.Console]
        echo Enabled = true
        echo.
        echo [Logging.Disk]
        echo Enabled = true
    )
)

echo   BepInEx installed successfully^^!
exit /b 0

:: ============================================
:: Write the canonical state file.
:: Schema version 1. Preserves WE_INSTALLED which may have been
:: already-true from a prior install.
:: ============================================
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
