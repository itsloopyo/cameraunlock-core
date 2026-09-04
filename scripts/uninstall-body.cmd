@echo off
:: ============================================
:: CameraUnlock unified uninstall body (shared)
:: ============================================
:: Source of truth: cameraunlock-core/scripts/uninstall-body.cmd.
:: One body, all loader variants. Per-mod uninstall.cmd wrappers set the
:: CONFIG BLOCK + WRAPPER_DIR and `call` here. Dispatch is by FRAMEWORK_TYPE
:: which MUST match what install wrote to the state file. Supported values:
::
::   BepInEx      - removes <game>/BepInEx/, winhttp.dll, doorstop files
::   MelonLoader  - removes <game>/MelonLoader/, version.dll, dobby.dll
::   MonoCecil    - restores Assembly-CSharp.dll from .original backup
::   ASILoader    - removes <exe-dir>/winmm.dll (or dinput8.dll)
::   REFramework  - removes <game>/dinput8.dll and <game>/reframework/
::   UE4SS        - removes <win64>/Mods/<ModName>/ and its mods.txt entry;
::                  every file the vendored UE4SS.zip lays down, Mods\ tree
::                  included, only if we installed the loader
::   xNVSE        - removes the plugin from <game>/Data/NVSE/Plugins; the
::                  loader itself is shared with every other New Vegas script
::                  mod and is never removed, /force included
::   None         - shim-only; restores shim DLLs from .backup if present
::   BeamNGUserMods - removes the mod archive from the BeamNG user folder's
::                  mods\, and USER_FOLDER_EXTRAS from the user folder
::                  itself; nothing was written to the game folder and there
::                  is no loader, so there is nothing else to take away
::
:: Required env from the wrapper:
::   WRAPPER_DIR        - wrapper's %~dp0 (release-zip root or <mod>/scripts/)
::   GAME_ID            - games.json id (find-game lookup)
::   MOD_DISPLAY_NAME   - banner / status text
::   MOD_DLLS           - space-separated DLL filenames to remove
::   MOD_INTERNAL_NAME  - state-file `mod.name` (informational)
::   STATE_FILE         - state file basename
::   FRAMEWORK_TYPE     - dispatch key (see list above)
::   LEGACY_DLLS        - optional extra DLLs from older versions to clean up
::   MOD_SEED_FILES     - optional config files install.cmd seeded write-if-
::                        absent; must match install.cmd's list
::   MANAGED_SUBFOLDER  - MonoCecil only: path under GAME_PATH containing
::                        Assembly-CSharp.dll
::   ASSEMBLY_DLL       - MonoCecil only: assembly to restore from .original
::   MANAGED_EXTRAS     - MonoCecil only: extra files (configs, logs) to wipe
::   ASI_LOADER_NAME    - ASILoader only: DLL filename (default winmm.dll)
::   ASI_SUBDIR         - ASILoader only: subdirectory below the exe directory
::                        the payload was deployed into; must match
::                        install.cmd's value
::   MOD_LEFTOVERS      - optional extra files to remove from DEPLOY_DIR (logs
::                        and configs the mod itself writes at runtime)
::   ROOT_EXTRAS        - optional extra files to remove from GAME_PATH, for
::                        mods that deploy below the game root but write their
::                        config and log at it
::   USER_FOLDER_EXTRAS - BeamNGUserMods only: files the mod writes at runtime
::                        into the per-user folder rather than into mods\.
::                        Entries may carry a relative subfolder
::   UE4_BINARIES_RELDIR - UE4SS only: path under GAME_PATH holding the
::                        shipping exe; must match install.cmd's value
::
:: Launcher CLI (passed through %*): [GAME_PATH] [/y] [/force]
::   /y      - non-interactive; skip every pause and prompt
::   /force  - remove loader even if state says installed_by_us=false
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
set "FORCE_FLAG="
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
if /i "%_ARG%"=="/y"      ( set "YES_FLAG=1"   & shift & goto :parse_args )
if /i "%_ARG%"=="-y"      ( set "YES_FLAG=1"   & shift & goto :parse_args )
if /i "%_ARG%"=="--yes"   ( set "YES_FLAG=1"   & shift & goto :parse_args )
if /i "%_ARG%"=="/force"  ( set "FORCE_FLAG=1" & shift & goto :parse_args )
if /i "%_ARG%"=="--force" ( set "FORCE_FLAG=1" & shift & goto :parse_args )
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
for %%v in (GAME_ID MOD_DISPLAY_NAME STATE_FILE FRAMEWORK_TYPE) do (
    if not defined %%v (
        echo ERROR: %%v is not set in this script's CONFIG BLOCK.
        exit /b 1
    )
)

echo.
echo === %MOD_DISPLAY_NAME% - Uninstall ===
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
    echo Pass a path explicitly: uninstall.cmd "C:\path\to\game"
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
:: Percent-expanded like the two lines above and for the same reason: delayed
:: expansion is still off here, so a `!` in the game path survives. Shifting
:: EXE_DIR once covers both DEPLOY_DIR and the loader proxy, which install.cmd
:: put in the same subdirectory.
if defined ASI_SUBDIR set "EXE_DIR=%EXE_DIR%\%ASI_SUBDIR%"

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
    echo Please close the game before uninstalling.
    echo.
    exit /b 1
)

:: -------- Validate the CONFIG BLOCK name lists --------
:: Checked as one string: the test is per character, so which of the six a bad
:: entry came from does not change the answer, and the message names all six.
set "_LIST=!MOD_DLLS! !LEGACY_DLLS! !MOD_SEED_FILES! !MOD_LEFTOVERS! !ROOT_EXTRAS! !MANAGED_EXTRAS!"
call :assert_safe_list
if errorlevel 1 exit /b 1

:: USER_FOLDER_EXTRAS is checked on its own because it is the one list whose
:: entries are allowed a relative subfolder - see :assert_safe_user_list.
set "_LIST=!USER_FOLDER_EXTRAS!"
call :assert_safe_user_list
if errorlevel 1 exit /b 1

:: -------- Compute DEPLOY_DIR per FRAMEWORK_TYPE --------
call :compute_deploy_dir
if errorlevel 1 exit /b 1

:: -------- Remove mod files (framework-aware) --------
if /i "%FRAMEWORK_TYPE%"=="None" (
    call :remove_shim_files
) else if /i "%FRAMEWORK_TYPE%"=="UE4SS" (
    rem The mod folder and its mods.txt line are ours whoever installed the
    rem loader, so they come off here, not in the installed_by_us-gated step.
    call :remove_ue4ss_mod
    if errorlevel 1 exit /b 1
) else if /i "%FRAMEWORK_TYPE%"=="MonoCecil" (
    rem Cecil: restore the pristine assembly FIRST. If it can't be restored
    rem (missing or corrupt backup over a still-patched assembly) abort WITHOUT
    rem stripping the mod DLLs - that would orphan a patched Assembly-CSharp.dll
    rem that can no longer load HeadTracking.dll and break the game. Keep the
    rem working install intact; the user repairs via Steam and re-runs.
    call :remove_MonoCecil
    if errorlevel 1 exit /b 1
    call :remove_mod_files_plain
    call :remove_managed_extras
) else (
    call :remove_mod_files_plain
)
call :remove_mod_seed_files
if errorlevel 1 exit /b 1
call :remove_mod_leftovers
call :remove_root_extras
call :remove_user_folder_extras
if errorlevel 1 exit /b 1

:: -------- Decide whether to remove loader --------
set "REMOVE_LOADER=0"
if "!FORCE_FLAG!"=="1" set "REMOVE_LOADER=1"
if "!REMOVE_LOADER!"=="0" (
    if exist "!GAME_PATH!\%STATE_FILE%" (
        findstr /c:"installed_by_us" "!GAME_PATH!\%STATE_FILE%" 2>nul | findstr /c:"true" >nul 2>&1
        if not errorlevel 1 set "REMOVE_LOADER=1"
    )
)

:: Shim-only is already handled in :remove_shim_files above (it restores the
:: .backup), and for Cecil the backup restore IS the loader removal.
:: Written as gotos rather than an if/else chain so `call :remove_<type>` sits
:: at the top level, where its exit code can be read - inside a parenthesised
:: block a failing loader removal ran on into "Uninstall Complete".
if /i "%FRAMEWORK_TYPE%"=="None"      goto :loader_done
if /i "%FRAMEWORK_TYPE%"=="MonoCecil" goto :loader_done
:: BeamNG reads its own mods folder; there was never a loader to install, so
:: there is none to remove and /force has nothing extra to reach.
if /i "%FRAMEWORK_TYPE%"=="BeamNGUserMods" goto :loader_done
if /i "%FRAMEWORK_TYPE%"=="xNVSE" (
    rem xNVSE is a shared modding framework: every other New Vegas script mod
    rem binds to the same loader, so it is not ours to take away. /force does
    rem not reach it either.
    echo.
    echo xNVSE was left intact - other mods may depend on it.
    goto :loader_done
)
if not "!REMOVE_LOADER!"=="1" (
    echo.
    echo %FRAMEWORK_TYPE% was not installed by this mod - leaving intact. Use /force to remove anyway.
    goto :loader_done
)
echo.
if "!FORCE_FLAG!"=="1" (
    echo Removing %FRAMEWORK_TYPE% ^(/force^)...
) else (
    echo Removing %FRAMEWORK_TYPE% ^(installed by this mod^)...
)
call :remove_%FRAMEWORK_TYPE%
if errorlevel 1 exit /b 1
:loader_done

:: -------- Remove state file --------
:: Only once the tree is actually clean. Deleting it after a failed removal
:: throws away installed_by_us, and the retry then reads the missing file as
:: "someone else installed the loader" and refuses to touch it without /force.
if defined _REMOVE_FAILED goto :uninstall_incomplete
set "_DEL_PATH=!GAME_PATH!\%STATE_FILE%"
set "_DEL_LABEL=state file"
call :del_one
if defined _REMOVE_FAILED goto :uninstall_incomplete

echo.
echo === Uninstall Complete ===
echo.
exit /b 0

:uninstall_incomplete
echo.
echo === Uninstall Incomplete ===
echo.
echo Some files are still in the game folder - see the errors above. Close the
echo game, and any launcher or overlay that may be holding its files open, then
echo run this uninstaller again.
echo The state file was left in place so the retry still knows what to remove.
echo.
exit /b 1

:: ============================================
:: Delete one file and report what actually happened. _DEL_PATH is the full
:: path, _DEL_LABEL what to print. They travel in variables rather than as
:: arguments because `%~1` is substituted before cmd.exe scans for `!`, so a
:: game folder with a `!` in it would arrive here already truncated.
::
:: Existence after the attempt is the test: `del` reports a failure on stderr
:: and leaves errorlevel alone, which is how "Removed: X" came to be printed
:: for a file that is still sitting there. Raises _REMOVE_FAILED instead of
:: exiting so one locked file does not hide the state of the rest of the tree.
:: ============================================
:del_one
if not exist "!_DEL_PATH!" exit /b 0
del /f /q "!_DEL_PATH!" >nul 2>&1
if exist "!_DEL_PATH!" (
    echo   ERROR: could not remove !_DEL_LABEL!
    set "_REMOVE_FAILED=1"
    exit /b 1
)
echo   Removed: !_DEL_LABEL!
exit /b 0

:: ============================================
:: Same contract as :del_one for a whole directory tree.
:: ============================================
:rmtree_one
if not exist "!_DEL_PATH!\" exit /b 0
rmdir /s /q "!_DEL_PATH!" >nul 2>&1
if exist "!_DEL_PATH!\" (
    echo   ERROR: could not remove !_DEL_LABEL!
    set "_REMOVE_FAILED=1"
    exit /b 1
)
echo   Removed: !_DEL_LABEL!
exit /b 0

:: ============================================
:: A CONFIG BLOCK name list is expanded by FOR, which globs its items against
:: the CURRENT directory rather than the game folder, and each item is then
:: pasted into a `del`. A wildcard, a `..`, a drive letter or a path separator
:: therefore reaches outside the folder this uninstall owns. _LIST = the value.
:: ============================================
:assert_safe_list
set "_BAD="
if not "!_LIST!"=="!_LIST:**=!" set "_BAD=a wildcard"
if not "!_LIST!"=="!_LIST:?=!"  set "_BAD=a wildcard"
if not "!_LIST!"=="!_LIST:..=!" set "_BAD=a .."
if not "!_LIST!"=="!_LIST::=!"  set "_BAD=a drive letter"
if not "!_LIST!"=="!_LIST:/=!"  set "_BAD=a path separator"
if not "!_LIST!"=="!_LIST:\=!"  set "_BAD=a path separator"
if not defined _BAD exit /b 0
echo ERROR: one of MOD_DLLS, LEGACY_DLLS, MOD_SEED_FILES, MOD_LEFTOVERS,
echo ROOT_EXTRAS or MANAGED_EXTRAS in the uninstall.cmd CONFIG BLOCK contains
echo !_BAD!:
echo   !_LIST!
echo Each entry is removed from the folder the mod was deployed into, so only
echo plain filenames belong in those lists.
exit /b 1

:: ============================================
:: USER_FOLDER_EXTRAS names files the mod writes at runtime into the per-user
:: folder, which is one level above the mods\ folder the payload went into and
:: has the game's own settings\ tree beside it - so a `\` is the only way to
:: name them and :assert_safe_list, which forbids one, cannot be reused. What
:: still has to hold is that no entry can leave the user folder. _LIST = the
:: value.
:: ============================================
:assert_safe_user_list
:: An UNSET _LIST is not the same as an empty one here: cmd expands
:: `!_LIST:x=y!` on an undefined variable to something that never compares
:: equal to `!_LIST!`, so every test below fires and a mod that leaves
:: USER_FOLDER_EXTRAS blank - every mod that is not BeamNG - is refused an
:: uninstall. :assert_safe_list above is immune because its _LIST always
:: holds the joining spaces.
if not defined _LIST exit /b 0
set "_BAD="
if not "!_LIST!"=="!_LIST:**=!" set "_BAD=a wildcard"
if not "!_LIST!"=="!_LIST:?=!"  set "_BAD=a wildcard"
if not "!_LIST!"=="!_LIST:..=!" set "_BAD=a .."
if not "!_LIST!"=="!_LIST::=!"  set "_BAD=a drive letter"
if not "!_LIST!"=="!_LIST:/=!"  set "_BAD=a forward slash"
if not defined _BAD exit /b 0
echo ERROR: USER_FOLDER_EXTRAS in the uninstall.cmd CONFIG BLOCK contains
echo !_BAD!:
echo   !_LIST!
echo Each entry is removed from the BeamNG user folder. A relative backslash
echo path is allowed; anything that could reach outside that folder is not.
exit /b 1

:: ============================================
:: compute_deploy_dir: set DEPLOY_DIR based on FRAMEWORK_TYPE.
:: For ASILoader and None, DEPLOY_DIR is EXE_DIR, derived above from the shim's
:: GAME_EXE_RELPATH (so nested-exe games like DL2 work).
:: ============================================
:compute_deploy_dir
if /i "%FRAMEWORK_TYPE%"=="BepInEx" (
    if defined PLUGIN_SUBFOLDER (
        set "DEPLOY_DIR=!GAME_PATH!\BepInEx\plugins\%PLUGIN_SUBFOLDER%"
    ) else (
        set "DEPLOY_DIR=!GAME_PATH!\BepInEx\plugins"
    )
    exit /b 0
)
if /i "%FRAMEWORK_TYPE%"=="MelonLoader" (
    set "DEPLOY_DIR=!GAME_PATH!\Mods"
    exit /b 0
)
if /i "%FRAMEWORK_TYPE%"=="REFramework" (
    set "DEPLOY_DIR=!GAME_PATH!\reframework\plugins"
    exit /b 0
)
if /i "%FRAMEWORK_TYPE%"=="UE4SS" (
    if not defined UE4_BINARIES_RELDIR (
        echo ERROR: UE4_BINARIES_RELDIR is not set in the uninstall CONFIG BLOCK.
        echo It must match the value install.cmd used.
        exit /b 1
    )
    if not defined MOD_INTERNAL_NAME (
        echo ERROR: MOD_INTERNAL_NAME is not set in the uninstall CONFIG BLOCK.
        echo Without it the mod folder path resolves to the whole Mods\ tree.
        exit /b 1
    )
    set "UE4_BINARIES_DIR=!GAME_PATH!\%UE4_BINARIES_RELDIR%"
    set "DEPLOY_DIR=!GAME_PATH!\%UE4_BINARIES_RELDIR%\Mods\%MOD_INTERNAL_NAME%"
    exit /b 0
)
if /i "%FRAMEWORK_TYPE%"=="MonoCecil" (
    set "DEPLOY_DIR=!GAME_PATH!\%MANAGED_SUBFOLDER%"
    exit /b 0
)
if /i "%FRAMEWORK_TYPE%"=="ASILoader" (
    set "DEPLOY_DIR=!EXE_DIR!"
    exit /b 0
)
if /i "%FRAMEWORK_TYPE%"=="xNVSE" (
    set "DEPLOY_DIR=!GAME_PATH!\Data\NVSE\Plugins"
    exit /b 0
)
if /i "%FRAMEWORK_TYPE%"=="None" (
    set "DEPLOY_DIR=!EXE_DIR!"
    exit /b 0
)
if /i "%FRAMEWORK_TYPE%"=="BeamNGUserMods" (
    rem Not derivable from GAME_PATH: BeamNG reads mods from the per-user
    rem folder, which is outside the game install entirely.
    call :resolve_beamng_user_folder
    set "DEPLOY_DIR=!USER_FOLDER!\mods"
    exit /b 0
)
echo ERROR: Unknown FRAMEWORK_TYPE "%FRAMEWORK_TYPE%" in uninstall CONFIG BLOCK.
exit /b 1

:: ============================================
:: Precedence, not a fallback chain, and the same order install.cmd used:
:: BEAMNG_USER_FOLDER wins outright, otherwise the folder BeamNG creates by
:: default. `current` is where it keeps the running version's user data.
:: ============================================
:resolve_beamng_user_folder
if defined BEAMNG_USER_FOLDER (
    set "USER_FOLDER=!BEAMNG_USER_FOLDER!"
) else (
    set "USER_FOLDER=!LOCALAPPDATA!\BeamNG\BeamNG.drive\current"
)
if "!USER_FOLDER:~-1!"=="\" set "USER_FOLDER=!USER_FOLDER:~0,-1!"
exit /b 0

:: ============================================
:: Remove mod DLLs + legacy DLLs from DEPLOY_DIR (framework-generic).
:: ============================================
:remove_mod_files_plain
echo Removing mod files...
set "REMOVED=0"
for %%f in (%MOD_DLLS%) do (
    if exist "!DEPLOY_DIR!\%%f" (
        set "_DEL_PATH=!DEPLOY_DIR!\%%f"
        set "_DEL_LABEL=%%f"
        call :del_one
        set /a REMOVED+=1
    )
)
if defined LEGACY_DLLS (
    for %%f in (%LEGACY_DLLS%) do (
        if exist "!DEPLOY_DIR!\%%f" (
            set "_DEL_PATH=!DEPLOY_DIR!\%%f"
            set "_DEL_LABEL=%%f (legacy)"
            call :del_one
            set /a REMOVED+=1
        )
    )
)
if /i "%FRAMEWORK_TYPE%"=="BepInEx" if defined PLUGIN_SUBFOLDER (
    for %%f in (%MOD_DLLS%) do (
        if exist "!GAME_PATH!\BepInEx\plugins\%%f" (
            set "_DEL_PATH=!GAME_PATH!\BepInEx\plugins\%%f"
            set "_DEL_LABEL=%%f (flat-laid duplicate)"
            call :del_one
            set /a REMOVED+=1
        )
    )
    if defined LEGACY_DLLS (
        for %%f in (%LEGACY_DLLS%) do (
            if exist "!GAME_PATH!\BepInEx\plugins\%%f" (
                set "_DEL_PATH=!GAME_PATH!\BepInEx\plugins\%%f"
                set "_DEL_LABEL=%%f (legacy, flat-laid)"
                call :del_one
                set /a REMOVED+=1
            )
        )
    )
    rem Only if the mod's own subfolder came out empty; a plugin someone else
    rem put there keeps it, and rmdir without /s refuses to take it.
    rmdir "!DEPLOY_DIR!" >nul 2>&1
)
if "!REMOVED!"=="0" echo   No mod files found
exit /b 0

:: ============================================
:: Remove the config and log files the mod writes at runtime, wherever it was
:: deployed. Its own routine rather than a tail on :remove_mod_files_plain so a
:: shim-only mod, which takes :remove_shim_files instead, gets them too.
:: ============================================
:remove_mod_leftovers
if not defined MOD_LEFTOVERS exit /b 0
for %%f in (%MOD_LEFTOVERS%) do (
    set "_DEL_PATH=!DEPLOY_DIR!\%%f"
    set "_DEL_LABEL=%%f"
    call :del_one
)
exit /b 0

:: ============================================
:: Remove the files install.cmd seeded write-if-absent. They are the mod's own
:: config, deployed into the same folder as its DLLs, so they come off with it.
:: ============================================
:remove_mod_seed_files
if not defined MOD_SEED_FILES exit /b 0
for %%f in (%MOD_SEED_FILES%) do (
    set "_DEL_PATH=!DEPLOY_DIR!\%%f"
    set "_DEL_LABEL=%%f"
    call :del_one
)
exit /b 0

:: ============================================
:: Remove files the mod leaves at the game root. A mod deployed below the root
:: (Source engine's bin\, say) still resolves its config and log from the exe's
:: own directory, so those do not sit in DEPLOY_DIR.
:: ============================================
:remove_root_extras
if not defined ROOT_EXTRAS exit /b 0
for %%f in (%ROOT_EXTRAS%) do (
    set "_DEL_PATH=!GAME_PATH!\%%f"
    set "_DEL_LABEL=%%f"
    call :del_one
)
exit /b 0

:: ============================================
:: Remove what the mod writes at runtime into the per-user folder. BeamNG
:: hands a mod that folder as its write target, so its log lands beside
:: beamng.log and its config inside the game's own settings\ tree - neither is
:: in DEPLOY_DIR, and neither is under GAME_PATH.
:: ============================================
:remove_user_folder_extras
if not defined USER_FOLDER_EXTRAS exit /b 0
if not defined USER_FOLDER (
    echo ERROR: USER_FOLDER_EXTRAS is set but FRAMEWORK_TYPE is %FRAMEWORK_TYPE%,
    echo which has no per-user folder. Every entry would be deleted from the root
    echo of the current drive instead.
    exit /b 1
)
for %%f in (%USER_FOLDER_EXTRAS%) do (
    set "_DEL_PATH=!USER_FOLDER!\%%f"
    set "_DEL_LABEL=%%f"
    call :del_one
)
exit /b 0

:: ============================================
:: Remove extra files Cecil mods leave in Managed/ (configs, logs, etc.).
:: ============================================
:remove_managed_extras
if not defined MANAGED_EXTRAS exit /b 0
for %%f in (%MANAGED_EXTRAS%) do (
    set "_DEL_PATH=!DEPLOY_DIR!\%%f"
    set "_DEL_LABEL=%%f"
    call :del_one
)
exit /b 0

:: ============================================
:: Remove shim DLLs - restore <name>.backup if present so the user's
:: pre-mod state comes back. Also handles any LEGACY_DLLS list entries.
:: ============================================
:remove_shim_files
echo Removing shim files...
set "REMOVED=0"
for %%f in (%MOD_DLLS%) do (
    if exist "!DEPLOY_DIR!\%%f.backup" (
        set "_DEL_PATH=!DEPLOY_DIR!\%%f"
        set "_DEL_LABEL=%%f"
        call :del_one >nul
        if exist "!DEPLOY_DIR!\%%f" (
            echo   ERROR: could not remove %%f, so the original cannot be put back.
        ) else (
            move /y "!DEPLOY_DIR!\%%f.backup" "!DEPLOY_DIR!\%%f" >nul
            if errorlevel 1 (
                echo   ERROR: could not restore the original %%f from %%f.backup.
                set "_REMOVE_FAILED=1"
            ) else (
                echo   Restored original %%f from backup
            )
        )
        set /a REMOVED+=1
    ) else (
        if exist "!DEPLOY_DIR!\%%f" (
            set "_DEL_PATH=!DEPLOY_DIR!\%%f"
            set "_DEL_LABEL=%%f (no backup was present)"
            call :del_one
            set /a REMOVED+=1
        )
    )
)
if defined LEGACY_DLLS (
    for %%f in (%LEGACY_DLLS%) do (
        if exist "!DEPLOY_DIR!\%%f" (
            set "_DEL_PATH=!DEPLOY_DIR!\%%f"
            set "_DEL_LABEL=%%f (legacy)"
            call :del_one
            set /a REMOVED+=1
        )
    )
)
if "!REMOVED!"=="0" echo   No shim files found
exit /b 0

:: ============================================
:: Remove BepInEx (regular and BepInExPack both land in the same layout).
:: ============================================
:remove_BepInEx
set "_DEL_PATH=!GAME_PATH!\BepInEx"
set "_DEL_LABEL=BepInEx folder"
call :rmtree_one
for %%f in (winhttp.dll doorstop_config.ini .doorstop_version changelog.txt) do (
    set "_DEL_PATH=!GAME_PATH!\%%f"
    set "_DEL_LABEL=%%f"
    call :del_one
)
exit /b 0

:: ============================================
:: Remove MelonLoader. Only delete Mods/UserLibs/UserData if empty
:: (mod-file removal above may leave them clean; users with other
:: melon mods installed keep their data).
:: ============================================
:remove_MelonLoader
set "_DEL_PATH=!GAME_PATH!\MelonLoader"
set "_DEL_LABEL=MelonLoader folder"
call :rmtree_one
for %%f in (version.dll dobby.dll NOTICE.txt) do (
    set "_DEL_PATH=!GAME_PATH!\%%f"
    set "_DEL_LABEL=%%f"
    call :del_one
)
for %%d in (Mods UserLibs UserData) do (
    if exist "!GAME_PATH!\%%d" (
        dir /b /a "!GAME_PATH!\%%d" 2>nul | findstr /r /v "^$" >nul
        if errorlevel 1 (
            rmdir "!GAME_PATH!\%%d" 2>nul
            if not exist "!GAME_PATH!\%%d" echo   Removed: %%d\ ^(empty^)
        )
    )
)
exit /b 0

:: ============================================
:: Mono.Cecil: restore Assembly-CSharp.dll from the .original backup.
:: The mod DLLs in Managed/ are cleaned up separately by the plain loop.
:: ============================================
:remove_MonoCecil
set "MANAGED_PATH=!GAME_PATH!\%MANAGED_SUBFOLDER%"
set "ASSEMBLY_PATH=!MANAGED_PATH!\%ASSEMBLY_DLL%"
set "BACKUP_PATH=!ASSEMBLY_PATH!.original"
:: The .original must be pristine: never restore a patched backup over the
:: game assembly, and never strip the mod DLLs while leaving a patched
:: assembly that can no longer find them. PATCH_MARKER drives the check.
if not defined PATCH_MARKER (
    echo   ERROR: PATCH_MARKER is not set in the uninstall.cmd CONFIG BLOCK.
    echo   Cannot verify assembly patch state; aborting.
    exit /b 1
)
if not exist "!BACKUP_PATH!" (
    rem No backup. Safe only if the live assembly is already clean; otherwise
    rem removing the mod DLLs would orphan a patched assembly.
    set "_MARKER_PATH=!ASSEMBLY_PATH!"
    call :cecil_marker_state
    if errorlevel 2 ( echo   ERROR: could not verify %ASSEMBLY_DLL% patch state. & exit /b 1 )
    if errorlevel 1 ( echo   No backup, and %ASSEMBLY_DLL% is already clean - nothing to restore. & exit /b 0 )
    echo   ERROR: %ASSEMBLY_DLL% is patched but no .original backup exists.
    echo   Run Steam "Verify integrity of game files" to restore a clean assembly, then re-run uninstall.
    exit /b 1
)
set "_MARKER_PATH=!BACKUP_PATH!"
call :cecil_marker_state
if errorlevel 2 ( echo   ERROR: could not read %ASSEMBLY_DLL%.original to verify it is pristine. & exit /b 1 )
if errorlevel 1 goto :_cecil_restore
echo   ERROR: %ASSEMBLY_DLL%.original is patched - corrupt backup, not restoring.
echo   Delete it and run Steam "Verify integrity of game files" to restore a clean %ASSEMBLY_DLL%.
exit /b 1
:_cecil_restore
copy /y "!BACKUP_PATH!" "!ASSEMBLY_PATH!" >nul
if errorlevel 1 (
    echo   ERROR: could not restore %ASSEMBLY_DLL% from its .original backup.
    echo   The backup is untouched. Close the game, check the folder is
    echo   writable, and run this uninstaller again.
    exit /b 1
)
echo   Restored: %ASSEMBLY_DLL% from backup
set "_DEL_PATH=!BACKUP_PATH!"
set "_DEL_LABEL=%ASSEMBLY_DLL%.original"
call :del_one
exit /b 0

:: ============================================
:: Resolve the marker-check helper and report whether _MARKER_PATH is patched.
:: The path travels in a variable rather than as an argument because `%~1` is
:: substituted before cmd.exe scans for `!`, so a game folder with a `!` in it
:: would arrive here already truncated.
:: Returns errorlevel 0 = patched, 1 = pristine, 2 = error. Requires
:: PATCH_MARKER. Kept as its own routine so the errorlevel reads stay outside
:: parenthesised blocks where %errorlevel% would expand too early.
:: ============================================
:cecil_marker_state
set "_MARKER_CHECK=!SCRIPT_DIR!shared\cecil-marker-check.ps1"
if not exist "!_MARKER_CHECK!" set "_MARKER_CHECK=!SCRIPT_DIR!..\cameraunlock-core\scripts\cecil-marker-check.ps1"
if not exist "!_MARKER_CHECK!" exit /b 2
powershell -NoProfile -ExecutionPolicy Bypass -File "!_MARKER_CHECK!" -AssemblyPath "!_MARKER_PATH!" -Marker "%PATCH_MARKER%"
exit /b %errorlevel%

:: ============================================
:: Remove Ultimate ASI Loader from EXE_DIR.
:: ============================================
:remove_ASILoader
:: Only the proxy this package actually installed. Sweeping the other common
:: ASI names off the disk deletes OTHER software's loader: winmm.dll and
:: dinput8.dll are what ReShade and most other ASI mods proxy through, so
:: uninstalling this mod used to silently break them.
:: Guarded because an unset ASI_LOADER_NAME collapses the path to the exe
:: DIRECTORY, and deleting a bare directory path expands to every file in it
:: and prompts "Are you sure (Y/N)?" - which under /y has no answer and blocks
:: on stdin forever. The CONFIG BLOCK documents this name as optional with a
:: default, but the body never applied one.
if not defined ASI_LOADER_NAME (
    echo   ERROR: ASI_LOADER_NAME is not set in the uninstall.cmd CONFIG BLOCK.
    echo   Cannot tell which proxy DLL belongs to this mod; refusing to guess.
    exit /b 1
)
set "_DEL_PATH=!EXE_DIR!\%ASI_LOADER_NAME%"
set "_DEL_LABEL=%ASI_LOADER_NAME%"
call :del_one
exit /b 0

:: ============================================
:: Remove REFramework.
:: ============================================
:remove_REFramework
set "_DEL_PATH=!GAME_PATH!\dinput8.dll"
set "_DEL_LABEL=dinput8.dll"
call :del_one
set "_DEL_PATH=!GAME_PATH!\reframework"
set "_DEL_LABEL=reframework/"
call :rmtree_one
:: Loose files REFramework's zip drops at the game root. openvr_api.dll and
:: openxr_loader.dll are NOT in this list: install strips the copies the zip
:: laid down and puts back any the game or another mod already had, so one
:: sitting here at uninstall time belongs to someone else.
for %%f in (reframework_revision.txt DELETE_OPENVR_API_DLL_IF_YOU_WANT_TO_USE_OPENXR) do (
    set "_DEL_PATH=!GAME_PATH!\%%f"
    set "_DEL_LABEL=%%f"
    call :del_one
)
exit /b 0

:: ============================================
:: Remove the UE4SS Lua mod: its folder under Win64\Mods\ and its line in
:: Win64\Mods\mods.txt. Runs regardless of who installed the loader - only
:: the loader DLLs themselves are gated on installed_by_us.
:: ============================================
:remove_ue4ss_mod
echo Removing mod files...
if exist "!DEPLOY_DIR!\" (
    set "_DEL_PATH=!DEPLOY_DIR!"
    set "_DEL_LABEL=Mods\%MOD_INTERNAL_NAME%\"
    call :rmtree_one
) else (
    echo   No mod folder found
)
set "MODS_TXT=!UE4_BINARIES_DIR!\Mods\mods.txt"
if not exist "!MODS_TXT!" exit /b 0
:: Matched as "NAME :" rather than "NAME ". A bare trailing space also matches a
:: LONGER name that merely starts the same way, so uninstalling "HeadTracking"
:: would have deregistered "HeadTracking Extras" as well - and UE4SS mod names
:: come from folder names, which may contain spaces.
findstr /b /c:"%MOD_INTERNAL_NAME% :" "!MODS_TXT!" >nul
:: 1 = not present (nothing to do). 2+ = the file could not be READ, which is
:: not the same thing and must not be reported as a clean deregistration.
if errorlevel 2 (
    echo   ERROR: could not read !MODS_TXT! to deregister %MOD_INTERNAL_NAME%.
    exit /b 1
)
if errorlevel 1 exit /b 0
set "_MODS_TMP=!TEMP!\cul-modstxt-%RANDOM%-%RANDOM%.txt"
findstr /v /b /c:"%MOD_INTERNAL_NAME% :" "!MODS_TXT!" > "!_MODS_TMP!"
:: findstr /v returns 1 when it filtered every line out, which is a normal
:: result here (our entry was the only one). Only 2+ is a read failure.
if errorlevel 2 (
    del "!_MODS_TMP!" 2>nul
    echo   ERROR: could not read !MODS_TXT! to deregister %MOD_INTERNAL_NAME%.
    exit /b 1
)
move /y "!_MODS_TMP!" "!MODS_TXT!" >nul
if errorlevel 1 (
    del "!_MODS_TMP!" 2>nul
    echo   ERROR: could not rewrite !MODS_TXT!.
    exit /b 1
)
echo   Deregistered %MOD_INTERNAL_NAME% from mods.txt
exit /b 0

:: ============================================
:: Remove the UE4SS loader itself: every file the vendored zip lays down, read
:: off the zip's own listing. That listing is the record of what arrived with
:: the loader, so it also covers Mods\ - the ten built-in Lua mods, Mods\shared\
:: and mods.txt - which a hand-written list of top-level files left behind.
:: Anything under Mods\ that the listing does not name is the user's and stays,
:: and so does any directory it left something in.
::
:: Only reached when the state file says this mod installed the loader, or
:: under /force. Our own mod folder and its mods.txt line came off earlier in
:: :remove_ue4ss_mod, which runs whoever installed the loader.
:: ============================================
:remove_UE4SS
set "VENDOR_DIR=!SCRIPT_DIR!vendor\ue4ss"
if not exist "!VENDOR_DIR!" set "VENDOR_DIR=!SCRIPT_DIR!..\vendor\ue4ss"
set "VENDOR_ZIP=!VENDOR_DIR!\UE4SS.zip"
if not exist "!VENDOR_ZIP!" (
    echo   ERROR: the bundled UE4SS.zip is not in this package:
    echo     !VENDOR_ZIP!
    echo   Without it there is no record of which files arrived with the loader,
    echo   and guessing would take the user's own Lua mods with it.
    echo   Re-download the release ZIP and run this uninstaller from it.
    exit /b 1
)
set "_ZIP_LIST=!TEMP!\cul-ue4ss-list-%RANDOM%-%RANDOM%.txt"
"%SystemRoot%\System32\tar.exe" -tf "!VENDOR_ZIP!" > "!_ZIP_LIST!"
if errorlevel 1 (
    del "!_ZIP_LIST!" 2>nul
    echo   ERROR: could not list the bundled UE4SS zip:
    echo     !VENDOR_ZIP!
    echo   The installer ZIP is corrupt. Re-download the release.
    exit /b 1
)
for /f "usebackq delims=" %%e in ("!_ZIP_LIST!") do (
    set "_ENTRY=%%e"
    call :remove_ue4ss_entry
)
:: A directory only becomes empty once its children are gone, so the prune
:: repeats until a pass removes nothing.
:ue4ss_prune
set "_PRUNED="
for /f "usebackq delims=" %%e in ("!_ZIP_LIST!") do (
    set "_ENTRY=%%e"
    call :prune_ue4ss_dir
)
if defined _PRUNED goto :ue4ss_prune
del "!_ZIP_LIST!" 2>nul
exit /b 0

:remove_ue4ss_entry
if "!_ENTRY:~-1!"=="/" exit /b 0
set "_ENTRY=!_ENTRY:/=\!"
set "_DEL_PATH=!UE4_BINARIES_DIR!\!_ENTRY!"
set "_DEL_LABEL=!_ENTRY!"
call :del_one
exit /b 0

:prune_ue4ss_dir
if not "!_ENTRY:~-1!"=="/" exit /b 0
set "_ENTRY=!_ENTRY:~0,-1!"
set "_ENTRY=!_ENTRY:/=\!"
if not exist "!UE4_BINARIES_DIR!\!_ENTRY!\" exit /b 0
rmdir "!UE4_BINARIES_DIR!\!_ENTRY!" 2>nul
if exist "!UE4_BINARIES_DIR!\!_ENTRY!\" exit /b 0
set "_PRUNED=1"
echo   Removed: !_ENTRY!\
exit /b 0

:: ============================================
:: Shim-only: no framework to remove beyond the shim DLL (handled already).
:: ============================================
:remove_None
exit /b 0
