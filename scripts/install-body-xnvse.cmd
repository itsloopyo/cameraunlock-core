@echo off
:: ============================================
:: CameraUnlock xNVSE install body (shared)
:: ============================================
:: Source of truth: cameraunlock-core/scripts/install-body-xnvse.cmd.
:: Per-mod install.cmd wrappers set the CONFIG BLOCK + WRAPPER_DIR and
:: `call` here. Resolved from <wrapper_dir>/shared/ in release zips, or
:: from <wrapper_dir>/../cameraunlock-core/scripts/ in the dev tree.
::
:: xNVSE (New Vegas Script Extender, community fork) is a loader exe that sits
:: at the game root beside FalloutNV.exe; plugins are DLLs under
:: Data\NVSE\Plugins. The player launches nvse_loader.exe rather than the game
:: exe, and the loader starts the game with the extender injected.
::
:: THE ONE BODY THAT REACHES THE NETWORK, and only because it has to. Every
:: other loader is vendored into the release ZIP and installed from there;
:: xNVSE ships with no upstream license, so redistributing its binary is not
:: something we have permission to do. Instead the mod pins an exact version,
:: download URL and SHA-256 in its CONFIG BLOCK, and this body fetches that
:: exact artifact and refuses it unless the hash matches. The pin is content-
:: addressed and committed, so what a user installs is what the dev reviewed.
:: `pixi run update-deps` in the mod repo rewrites the three pin lines.
:: Do not copy this pattern into a body whose loader can be vendored.
::
:: Required env from the wrapper:
::   WRAPPER_DIR              wrapper's %~dp0 (release zip root or <mod>/scripts/)
::   GAME_ID                  games.json id
::   MOD_DISPLAY_NAME         banner / status text
::   MOD_DLLS                 space-separated DLL filenames in plugins/
::   MOD_INTERNAL_NAME        state-file mod.name
::   MOD_VERSION              state-file mod.version
::   STATE_FILE               state file basename
::   FRAMEWORK_TYPE           always "xNVSE"
::   XNVSE_VERSION            pinned upstream release tag (status text)
::   XNVSE_URL                pinned release asset URL, a .zip
::   XNVSE_SHA256             lowercase hex SHA-256 the download must match
::   MOD_SEED_FILES           optional files copied only when not already
::                            present, so an upgrade keeps whatever the user
::                            tuned in an .ini the mod ships a default for.
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
for %%v in (GAME_ID MOD_DISPLAY_NAME MOD_INTERNAL_NAME MOD_VERSION STATE_FILE FRAMEWORK_TYPE MOD_DLLS XNVSE_VERSION XNVSE_URL XNVSE_SHA256) do (
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
:: therefore `!`-expanded; the arg parser and the shim call above run with
:: expansion off for the same reason.
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

:: -------- Ensure xNVSE --------
if not exist "!GAME_PATH!\nvse_loader.exe" (
    echo xNVSE not found. Installing...
    echo.
    call :install_xnvse
    if errorlevel 1 exit /b 1
    set "WE_INSTALLED=true"
) else (
    echo Existing xNVSE detected, skipping loader install, deploying plugin only.
)
echo.

:: -------- Deploy mod files --------
echo Deploying mod files...

set "PLUGINS_PATH=!GAME_PATH!\Data\NVSE\Plugins"
set "DLL_DIR=!SCRIPT_DIR!plugins"
:: Release ZIP layout has plugins/ next to install.cmd; the dev tree has
:: install.cmd in <repo>/scripts/ and the freshly built DLL in
:: <repo>/build/Release.
if not exist "!DLL_DIR!" set "DLL_DIR=!SCRIPT_DIR!..\build\Release"

if not exist "!PLUGINS_PATH!" mkdir "!PLUGINS_PATH!"
if not exist "!PLUGINS_PATH!" (
    echo   ERROR: could not create !PLUGINS_PATH! - is the game folder writable?
    exit /b 1
)

set "DEPLOY_FAILED=0"
:: Seeded before the mod DLLs, and copied only when absent: an upgrade has to
:: keep the values the user tuned.
if defined MOD_SEED_FILES (
    for %%f in (%MOD_SEED_FILES%) do (
        if exist "!DLL_DIR!\%%f" (
            if exist "!PLUGINS_PATH!\%%f" (
                echo   Kept your existing %%f
            ) else (
                copy /y "!DLL_DIR!\%%f" "!PLUGINS_PATH!\" >nul
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
    if exist "!DLL_DIR!\%%f" (
        copy /y "!DLL_DIR!\%%f" "!PLUGINS_PATH!\" >nul
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
echo Launch the game via nvse_loader.exe to use the mod^^!
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
:: Install xNVSE from the pinned upstream release. See the header for why this
:: one loader is downloaded rather than vendored. The hash check is the whole
:: point: a download that does not match the committed SHA-256 is discarded
:: and the install fails, rather than being installed and hoped about.
:: ============================================
:install_xnvse
set "XNVSE_DL=!TEMP!\cul-xnvse-%RANDOM%-%RANDOM%.zip"

echo   Downloading xNVSE %XNVSE_VERSION%...
powershell -NoProfile -ExecutionPolicy Bypass -Command "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%XNVSE_URL%' -OutFile '!XNVSE_DL!' -UseBasicParsing -TimeoutSec 120 } catch { Write-Host $_.Exception.Message; exit 1 }"
if errorlevel 1 (
    echo   ERROR: Failed to download xNVSE from:
    echo     %XNVSE_URL%
    echo   Check your internet connection, or install xNVSE manually from
    echo   https://github.com/xNVSE/NVSE/releases and re-run this installer.
    del "!XNVSE_DL!" 2>nul
    exit /b 1
)

echo   Extracting xNVSE...
set "XNVSE_EXTRACT=!TEMP!\cul-xnvse-extract-%RANDOM%-%RANDOM%"
mkdir "!XNVSE_EXTRACT!"
if errorlevel 1 (
    echo   ERROR: could not create a temporary extraction folder.
    del "!XNVSE_DL!" 2>nul
    exit /b 1
)

:: Hashed and unpacked through ONE handle, opened FileShare.Read so nothing
:: else can write to or delete the file while it is held. Hashing the download
:: and then handing the path to tar.exe re-opens it, and the archive that gets
:: unpacked is then not provably the one that was checked - the download lands
:: in TEMP, which every process running as this user can write.
:: Paths travel by environment variable: a TEMP path with an apostrophe in it
:: would close a single-quoted literal early and fail to parse.
set "CUL_XNVSE_ZIP=!XNVSE_DL!"
set "CUL_XNVSE_SHA=%XNVSE_SHA256%"
set "CUL_XNVSE_DIR=!XNVSE_EXTRACT!"
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ErrorActionPreference = 'Stop'; try { Add-Type -AssemblyName System.IO.Compression; Add-Type -AssemblyName System.IO.Compression.FileSystem; $fs = [IO.File]::Open($env:CUL_XNVSE_ZIP, 'Open', 'Read', 'Read'); try { $sha = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($fs)).Replace('-', '').ToLower(); if ($sha -ne $env:CUL_XNVSE_SHA.ToLower()) { Write-Host ('    expected ' + $env:CUL_XNVSE_SHA); Write-Host ('    got      ' + $sha); exit 2 }; $fs.Position = 0; $zip = New-Object System.IO.Compression.ZipArchive($fs); [System.IO.Compression.ZipFileExtensions]::ExtractToDirectory($zip, $env:CUL_XNVSE_DIR) } finally { $fs.Dispose() } } catch { Write-Host $_.Exception.Message; exit 1 }"
set "_XNVSE_EC=!errorlevel!"
del "!XNVSE_DL!" 2>nul
if "!_XNVSE_EC!"=="2" (
    echo   ERROR: the xNVSE download failed its integrity check.
    rmdir /s /q "!XNVSE_EXTRACT!" 2>nul
    exit /b 1
)
if not "!_XNVSE_EC!"=="0" (
    echo   ERROR: Extraction failed.
    rmdir /s /q "!XNVSE_EXTRACT!" 2>nul
    exit /b 1
)

:: Both loops below walk the extraction folder from `.` rather than from
:: "!XNVSE_EXTRACT!". FOR /R resolves its root at parse time, before delayed
:: expansion has run, so a `!VAR!` root is taken literally and every path it
:: yields comes back relative to the current directory instead.
pushd "!XNVSE_EXTRACT!"

:: The legacy-version asset wraps the binaries in an inner .zip. Unpack any
:: nested archive in place and delete it so only the binaries remain to copy.
:: -C takes `%%~dpz.` and not `%%~dpz`: the latter ends in a backslash, and
:: tar.exe parses its command line the modern way, where a backslash before the
:: closing quote escapes it and the path arrives with a stray quote on the end.
for /r . %%z in (*.zip) do (
    "%SystemRoot%\System32\tar.exe" -xf "%%z" -C "%%~dpz."
    del /q "%%z"
)

:: Upstream has moved the binaries between the archive root and a versioned
:: subfolder across releases, so locate them by the loader exe rather than by
:: a path the next release can invalidate. FOR /R offers a candidate for every
:: directory it walks whether the file is there or not, hence the `if exist`.
set "XNVSE_SRC="
for /r . %%f in (nvse_loader.exe) do (
    if not defined XNVSE_SRC if exist "%%f" set "XNVSE_SRC=%%~dpf"
)
popd

if not defined XNVSE_SRC (
    echo   ERROR: nvse_loader.exe is not in the downloaded archive.
    rmdir /s /q "!XNVSE_EXTRACT!" 2>nul
    exit /b 1
)

xcopy "!XNVSE_SRC!*" "!GAME_PATH!\" /s /y /q >nul
set "_XCOPY_EC=!errorlevel!"
rmdir /s /q "!XNVSE_EXTRACT!" 2>nul
if not "!_XCOPY_EC!"=="0" (
    echo   ERROR: failed to copy xNVSE into the game folder ^(xcopy exit code !_XCOPY_EC!^).
    echo   Check the game directory is writable.
    exit /b 1
)

if not exist "!GAME_PATH!\nvse_loader.exe" (
    echo   ERROR: xNVSE installation failed - nvse_loader.exe is not present after the copy.
    exit /b 1
)

echo   xNVSE installed successfully^^!
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
