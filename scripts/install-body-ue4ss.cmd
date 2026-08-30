@echo off
:: ============================================
:: CameraUnlock UE4SS install body (shared)
:: ============================================
:: Source of truth: cameraunlock-core/scripts/install-body-ue4ss.cmd.
:: Per-mod install.cmd wrappers set the CONFIG BLOCK + WRAPPER_DIR and
:: `call` here. Resolved from <wrapper_dir>/shared/ in release zips, or
:: from <wrapper_dir>/../cameraunlock-core/scripts/ in the dev tree.
::
:: UE4SS (Unreal Engine 4/5 Scripting System) is a single-DLL injector that
:: lives next to the game's shipping exe in <Game>\<Project>\Binaries\Win64.
:: The mod deploys as a Lua mod folder into Win64\Mods\<MOD_INTERNAL_NAME>\
:: and is registered by a line in Win64\Mods\mods.txt.
::
:: Required env from the wrapper:
::   WRAPPER_DIR              wrapper's %~dp0 (release zip root or <mod>/scripts/)
::   GAME_ID                  games.json id
::   MOD_DISPLAY_NAME         banner / status text
::   MOD_INTERNAL_NAME        Mods\ folder name, mods.txt entry, state-file mod.name
::   MOD_VERSION              state-file mod.version
::   STATE_FILE               state file basename
::   FRAMEWORK_TYPE           always "UE4SS"
::   UE4_BINARIES_RELDIR      path under GAME_PATH holding the shipping exe,
::                            e.g. "Pathless\Binaries\Win64". uninstall.cmd's
::                            CONFIG BLOCK must carry the same value.
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
for %%v in (GAME_ID MOD_DISPLAY_NAME MOD_INTERNAL_NAME MOD_VERSION STATE_FILE FRAMEWORK_TYPE UE4_BINARIES_RELDIR) do (
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

:: Built with expansion still off so a `!` in the game path survives; the
:: shim's GAME_PATH is used verbatim rather than through a FOR variable.
set "WIN64_DIR=%GAME_PATH%\%UE4_BINARIES_RELDIR%"

:: Delayed expansion is enabled HERE and not one line earlier. Everything the
:: shim resolved is already in the environment, and `!VAR!` hands the value back
:: byte-for-byte: it is substituted after cmd.exe has finished looking for `!`,
:: `&`, `^` and `)`, so a path like C:\Games\Oh! My Game survives. `%VAR%` is
:: substituted before that scan and would lose the `!`. Every path below is
:: therefore `!`-expanded; the arg parser, the shim call and the WIN64_DIR
:: derivation above all run with expansion off for the same reason.
setlocal enabledelayedexpansion

echo Game found: !GAME_PATH!

if not exist "!WIN64_DIR!" (
    echo ERROR: UE4 Win64 directory not found:
    echo   !WIN64_DIR!
    echo Check the UE4_BINARIES_RELDIR value in this script's CONFIG BLOCK.
    exit /b 1
)
echo Win64 dir: !WIN64_DIR!
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

:: -------- Ensure UE4SS --------
if not exist "!WIN64_DIR!\UE4SS.dll" (
    echo UE4SS not found. Installing...
    echo.
    call :install_ue4ss
    if errorlevel 1 exit /b 1
    set "WE_INSTALLED=true"
) else (
    echo Existing UE4SS detected, skipping loader install, deploying mod only.
)
echo.

:: -------- Deploy mod files --------
echo Deploying mod files to Mods\%MOD_INTERNAL_NAME%\ ...

set "MOD_SRC=!SCRIPT_DIR!mod"
set "MOD_DST=!WIN64_DIR!\Mods\%MOD_INTERNAL_NAME%"

if not exist "!MOD_SRC!" (
    echo   ERROR: mod source folder not found: !MOD_SRC!
    echo   The installer ZIP is corrupt. Re-download the release.
    exit /b 1
)

:: Replaced wholesale rather than copied over: a Lua mod that dropped a script
:: between releases would otherwise keep loading the stale one.
if exist "!MOD_DST!" rmdir /s /q "!MOD_DST!"
mkdir "!MOD_DST!"
if errorlevel 1 (
    echo   ERROR: could not create !MOD_DST! - is the game folder writable?
    exit /b 1
)
xcopy /e /i /y /q "!MOD_SRC!\*" "!MOD_DST!\" >nul
if errorlevel 1 (
    echo   ERROR: failed to copy mod files to !MOD_DST!.
    exit /b 1
)
echo   Deployed mod folder: !MOD_DST!

:: -------- Register in mods.txt --------
call :register_in_mods_txt
if errorlevel 1 exit /b 1

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
echo   !MOD_DST!
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
:: Install UE4SS from the bundled vendored zip.
:: Vendor tree is the single source of truth at install time. To bump the
:: bundled version, run `pixi run update-deps` in the mod repo and commit.
:: ============================================
:install_ue4ss
set "VENDOR_DIR=!SCRIPT_DIR!vendor\ue4ss"
:: Release ZIP has vendor/ next to install.cmd; the dev tree has it at
:: <repo>/vendor/ with install.cmd one level down in scripts/.
if not exist "!VENDOR_DIR!" set "VENDOR_DIR=!SCRIPT_DIR!..\vendor\ue4ss"
set "VENDOR_ZIP=!VENDOR_DIR!\UE4SS.zip"

if not exist "!VENDOR_ZIP!" (
    echo   ERROR: Bundled UE4SS not found at:
    echo     !VENDOR_ZIP!
    echo   The installer ZIP is corrupt. Re-download the release.
    exit /b 1
)

echo   Extracting bundled UE4SS to Win64 directory...
"%SystemRoot%\System32\tar.exe" -xf "!VENDOR_ZIP!" -C "!WIN64_DIR!"
if errorlevel 1 (
    echo   ERROR: Extraction failed.
    exit /b 1
)
echo   UE4SS installed successfully^^!
exit /b 0

:: ============================================
:: Ensure the "<MOD_INTERNAL_NAME> : 1" line is present in Win64\Mods\mods.txt.
:: UE4SS writes mods.txt itself on first launch, so on a fresh loader install
:: there is nothing there yet and we create it.
::
:: Matched as "NAME :" rather than "NAME ". A bare trailing space also matches a
:: LONGER name that merely starts the same way, and uninstall-body.cmd
:: deregisters on the same pattern - the two have to agree or an upgrade
:: appends a second entry.
:: ============================================
:register_in_mods_txt
set "MODS_TXT=!WIN64_DIR!\Mods\mods.txt"
if not exist "!WIN64_DIR!\Mods" (
    mkdir "!WIN64_DIR!\Mods"
    if errorlevel 1 (
        echo   ERROR: could not create !WIN64_DIR!\Mods - is the game folder writable?
        exit /b 1
    )
)
if not exist "!MODS_TXT!" (
    > "!MODS_TXT!" echo %MOD_INTERNAL_NAME% : 1
    echo   Created mods.txt with the %MOD_INTERNAL_NAME% entry
    exit /b 0
)
findstr /b /c:"%MOD_INTERNAL_NAME% :" "!MODS_TXT!" >nul
:: 1 = not listed yet. 2+ = the file could not be READ, which is not the same
:: thing and must not be reported as an already-registered mod.
if errorlevel 2 (
    echo   ERROR: could not read !MODS_TXT! to register %MOD_INTERNAL_NAME%.
    exit /b 1
)
if not errorlevel 1 (
    echo   mods.txt already lists %MOD_INTERNAL_NAME%
    exit /b 0
)
>> "!MODS_TXT!" echo %MOD_INTERNAL_NAME% : 1
echo   Appended %MOD_INTERNAL_NAME% to mods.txt
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
    echo     "type": "!FRAMEWORK_TYPE!",
    echo     "installed_by_us": !WE_INSTALLED!
    echo   },
    echo   "mod": {
    echo     "id": "!GAME_ID!",
    echo     "name": "!MOD_INTERNAL_NAME!",
    echo     "version": "!MOD_VERSION!",
    echo     "installed_at": "!INSTALLED_AT!"
    echo   }
    echo }
)
exit /b 0
