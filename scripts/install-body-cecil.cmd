@echo off
:: ============================================
:: CameraUnlock Mono.Cecil install body (shared)
:: ============================================
:: Source of truth: cameraunlock-core/scripts/install-body-cecil.cmd.
:: This file is the install flow itself - per-mod install.cmd wrappers set
:: the CONFIG BLOCK + WRAPPER_DIR and then `call` into here. There is no
:: per-mod copy of this body; the wrapper resolves it from
:: <wrapper_dir>/shared/install-body-cecil.cmd in release zips, or from
:: <wrapper_dir>/../cameraunlock-core/scripts/install-body-cecil.cmd in
:: the dev tree.
::
:: Required env from the wrapper:
::   WRAPPER_DIR        - wrapper's %~dp0 (release-zip root in production,
::                        <mod>/scripts/ in dev). Used to resolve mod/ and
::                        shared/find-game.ps1 - both siblings of the
::                        wrapper, NOT siblings of this body file.
::   GAME_ID            - games.json id (find-game lookup)
::   MOD_DISPLAY_NAME   - banner / status text
::   MOD_DLLS           - space-separated DLL filenames to deploy
::   MOD_INTERNAL_NAME  - state-file `mod.name`
::   MOD_VERSION        - state-file `mod.version`
::   STATE_FILE         - state file basename (e.g. .headtracking-state.json)
::   FRAMEWORK_TYPE     - state-file `framework.type` (always "MonoCecil" here)
::   MANAGED_SUBFOLDER  - relative path under GAME_PATH containing Assembly-CSharp.dll
::   ASSEMBLY_DLL       - target assembly to patch (usually Assembly-CSharp.dll)
::   PATCHER_FILE       - C# patcher source filename in mod/
::   MOD_CONTROLS       - optional post-install help text (hotkeys etc.)
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
:: Fallback to the body's own %~dp0 only if the wrapper forgot to set it -
:: that path won't find mod/ but at least the find-game shim still resolves
:: via the dev fallback below.
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
for %%v in (GAME_ID MOD_DISPLAY_NAME MOD_INTERNAL_NAME STATE_FILE FRAMEWORK_TYPE MOD_DLLS MANAGED_SUBFOLDER ASSEMBLY_DLL PATCHER_FILE) do (
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

set "MANAGED_PATH=!GAME_PATH!\%MANAGED_SUBFOLDER%"
set "ASSEMBLY_PATH=!MANAGED_PATH!\%ASSEMBLY_DLL%"
set "BACKUP_PATH=!MANAGED_PATH!\%ASSEMBLY_DLL%.original"
set "MOD_DIR=!SCRIPT_DIR!mod"

if not exist "!MANAGED_PATH!" (
    echo ERROR: %MANAGED_SUBFOLDER% folder not found.
    echo   Expected at: !MANAGED_PATH!
    echo.
    exit /b 1
)

if not exist "!ASSEMBLY_PATH!" (
    echo ERROR: %ASSEMBLY_DLL% not found.
    echo   Expected at: !ASSEMBLY_PATH!
    echo.
    exit /b 1
)

for %%f in (%MOD_DLLS%) do (
    if not exist "!MOD_DIR!\%%f" (
        echo ERROR: %%f not found in mod folder.
        echo   Make sure all files from the release package are intact.
        echo.
        exit /b 1
    )
)

if not exist "!MOD_DIR!\%PATCHER_FILE%" (
    echo ERROR: %PATCHER_FILE% not found in mod folder.
    echo   Make sure all files from the release package are intact.
    echo.
    exit /b 1
)

:: -------- Prior state --------
set "WE_INSTALLED=false"
if exist "!GAME_PATH!\%STATE_FILE%" (
    findstr /c:"installed_by_us" "!GAME_PATH!\%STATE_FILE%" 2>nul | findstr /c:"true" >nul 2>&1
    if not errorlevel 1 set "WE_INSTALLED=true"
)

:: -------- Back up Assembly DLL (pristine-backup guard) --------
:: A Mono.Cecil patch is additive: the patched assembly carries PATCH_MARKER.
:: The .original backup MUST be a pristine (marker-free) assembly, else a
:: later uninstall restores a broken file. So we never copy ASSEMBLY ->
:: .original unless the source is proven clean, and never restore from a
:: marker-bearing .original. This makes the corrupt-backup state unreachable.
if not defined PATCH_MARKER (
    echo ERROR: PATCH_MARKER is not set in the install.cmd CONFIG BLOCK.
    echo This is required to protect the pristine %ASSEMBLY_DLL% backup.
    exit /b 1
)
set "_MARKER_CHECK=!SCRIPT_DIR!shared\cecil-marker-check.ps1"
if not exist "!_MARKER_CHECK!" set "_MARKER_CHECK=!SCRIPT_DIR!..\cameraunlock-core\scripts\cecil-marker-check.ps1"
if not exist "!_MARKER_CHECK!" (
    echo ERROR: cecil-marker-check.ps1 not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    exit /b 1
)

echo Backing up %ASSEMBLY_DLL%...
if not exist "!BACKUP_PATH!" (
    set "_PRISTINE_PATH=!ASSEMBLY_PATH!"
    call :assert_pristine "%ASSEMBLY_DLL% is already patched but no .original backup exists"
    if errorlevel 1 exit /b 1
    copy /y "!ASSEMBLY_PATH!" "!BACKUP_PATH!" >nul
    if errorlevel 1 (
        echo   ERROR: could not write the %ASSEMBLY_DLL%.original backup at:
        echo     !BACKUP_PATH!
        echo   This is the only copy of the pristine assembly, and the patch
        echo   below cannot be undone without it, so nothing has been changed.
        echo   Check the game folder is writable and re-run.
        exit /b 1
    )
    echo   Created: %ASSEMBLY_DLL%.original
    set "WE_INSTALLED=true"
) else (
    set "_PRISTINE_PATH=!BACKUP_PATH!"
    call :assert_pristine "%ASSEMBLY_DLL%.original is itself patched - corrupt backup"
    if errorlevel 1 exit /b 1
    echo   Backup verified clean, restoring before re-patch...
    copy /y "!BACKUP_PATH!" "!ASSEMBLY_PATH!" >nul
    if errorlevel 1 (
        echo   ERROR: could not restore %ASSEMBLY_DLL% from its verified backup.
        echo   Nothing has been patched. Close the game, check the folder is
        echo   writable, and re-run.
        exit /b 1
    )
    rem WE_INSTALLED stays whatever it was - we backed up on the first install,
    rem and that entitlement doesn't regress just because we're re-running.
)
echo.

:: -------- Copy mod files --------
echo Deploying mod files...

set "DEPLOY_FAILED=0"
for %%f in (%MOD_DLLS%) do (
    copy /y "!MOD_DIR!\%%f" "!MANAGED_PATH!\" >nul
    if errorlevel 1 (
        echo   ERROR: Failed to copy %%f
        set "DEPLOY_FAILED=1"
    ) else (
        echo   Deployed %%f
    )
)

if "!DEPLOY_FAILED!"=="1" (
    echo.
    echo ERROR: File deployment failed.
    echo.
    exit /b 1
)
echo.

:: Unblock DLLs (Windows SmartScreen MOTW).
:: Paths travel by environment variable, never interpolated into the PowerShell
:: source: a game folder with an apostrophe in it (C:\Games\Mike's Games\...)
:: closes a single-quoted literal early and the command dies on a parse error.
set "CUL_MANAGED_PATH=!MANAGED_PATH!"
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "Get-ChildItem -LiteralPath $env:CUL_MANAGED_PATH -Filter *.dll | Unblock-File"

:: -------- Patch Assembly DLL --------
echo Patching %ASSEMBLY_DLL%...

set "CECIL_PATH=!MANAGED_PATH!\Mono.Cecil.dll"
set "PATCHER_PATH=!MOD_DIR!\%PATCHER_FILE%"

set "CUL_CECIL_PATH=!CECIL_PATH!"
set "CUL_PATCHER_PATH=!PATCHER_PATH!"
set "CUL_ASSEMBLY_PATH=!ASSEMBLY_PATH!"
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "Add-Type -LiteralPath $env:CUL_CECIL_PATH; " ^
    "$code = Get-Content -LiteralPath $env:CUL_PATCHER_PATH -Raw; " ^
    "$cp = New-Object System.CodeDom.Compiler.CompilerParameters; " ^
    "$cp.ReferencedAssemblies.Add($env:CUL_CECIL_PATH); " ^
    "$cp.ReferencedAssemblies.Add('System.dll'); " ^
    "$cp.ReferencedAssemblies.Add('System.Core.dll'); " ^
    "$cp.CompilerOptions = '/nowarn:1668 /warn:0'; " ^
    "$cp.TreatWarningsAsErrors = $false; " ^
    "Add-Type -TypeDefinition $code -CompilerParameters $cp; " ^
    "if (-not [BootstrapPatcher]::PatchAssembly($env:CUL_ASSEMBLY_PATH)) { exit 1 }"

if errorlevel 1 (
    echo.
    echo ERROR: Patching failed.
    echo Try verifying game files through Steam and running the installer again.
    echo.
    exit /b 1
)

:: -------- Write state file --------
call :stamp_installed_at
if errorlevel 1 exit /b 1
call :write_state_file

echo.
echo ========================================
echo   Installation Complete^^!
echo ========================================
echo.
echo %MOD_DISPLAY_NAME% has been installed to:
echo   !MANAGED_PATH!
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
:: Write the canonical state file.
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

:: ============================================
:: Assert an assembly is pristine (no PATCH_MARKER) before we trust it as the
:: clean baseline. Returns errorlevel 0 if clean, 1 if patched or unreadable
:: (caller aborts). _PRISTINE_PATH = assembly to check, %~1 = human-readable
:: failure context. The path travels in a variable rather than as an argument
:: because `%~1` is substituted before cmd.exe scans for `!`, so a game folder
:: with a `!` in it would arrive here already truncated.
:: ============================================
:assert_pristine
powershell -NoProfile -ExecutionPolicy Bypass -File "!_MARKER_CHECK!" -AssemblyPath "!_PRISTINE_PATH!" -Marker "%PATCH_MARKER%"
set "_MK_EC=%errorlevel%"
if "%_MK_EC%"=="1" exit /b 0
if "%_MK_EC%"=="0" (
    echo   ERROR: %~1.
    echo   A clean state cannot be established from a modified file.
    echo   Verify the game files through Steam, which restores the original, then re-run.
    exit /b 1
)
echo   ERROR: could not read assembly to verify patch state ^(code %_MK_EC%^).
exit /b 1
