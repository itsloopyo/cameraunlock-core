@echo off
:: ============================================
:: CameraUnlock REFramework Install Template
:: ============================================
:: Source of truth: cameraunlock-core/scripts/templates/install-reframework.cmd.
:: Copy to <mod>/scripts/install.cmd, edit CONFIG BLOCK, leave the rest
:: alone. Contract: see ~/.claude/CLAUDE.md "install.cmd / uninstall.cmd".
::
:: REFramework layout:
::   Loader:     <game>/dinput8.dll
::   Runtime:    <game>/reframework/
::   Plugins:    <game>/reframework/plugins/   (mod DLLs + INI go here)
::
:: REFramework upstream ships per-game nightly zips (RE9.zip, RE2.zip, ...).
:: We vendor a known-good copy of the per-game zip into vendor/reframework/.
:: To bump the bundled version, run `pixi run update-deps` and commit.
::
:: Launcher CLI: install.cmd [GAME_PATH] [/y]
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=my-game-id"
set "MOD_DISPLAY_NAME=My Mod Name"
set "MOD_DLLS=MyMod.dll HeadTracking.ini"
set "MOD_INTERNAL_NAME=MyMod"
set "MOD_VERSION=1.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=REFramework"
set "REFRAMEWORK_VENDOR_ZIP_NAME=RE9.zip"
set "MOD_CONTROLS="
:: REFRAMEWORK_VENDOR_ZIP_NAME is per-game (RE:Requiem is RE9.zip, RE2 is
:: RE2.zip, etc). update-deps.ps1 must be configured with the same name so
:: the refresh writes to the right vendor file.
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
for %%v in (GAME_ID MOD_DISPLAY_NAME MOD_INTERNAL_NAME STATE_FILE FRAMEWORK_TYPE MOD_DLLS REFRAMEWORK_VENDOR_ZIP_NAME) do (
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

:: -------- Ensure REFramework --------
if not exist "%GAME_PATH%\dinput8.dll" (
    echo REFramework not found. Installing...
    echo.
    call :install_reframework
    if errorlevel 1 exit /b 1
    set "WE_INSTALLED=true"
    echo REFramework installed successfully.
    echo.
) else (
    echo Existing REFramework detected, skipping loader install, deploying plugin only.
)

:: -------- Deploy mod files --------
set "PLUGINS_DIR=%GAME_PATH%\reframework\plugins"
if not exist "%PLUGINS_DIR%" mkdir "%PLUGINS_DIR%"

echo.
echo Deploying mod files...

set "DEPLOY_FAILED=0"
for %%f in (%MOD_DLLS%) do (
    if exist "%SCRIPT_DIR%plugins\%%f" (
        copy /y "%SCRIPT_DIR%plugins\%%f" "%PLUGINS_DIR%\%%f" >nul
        if errorlevel 1 (
            echo   ERROR: Failed to copy %%f - is the game folder writable?
            set "DEPLOY_FAILED=1"
        ) else (
            echo   Deployed: %%f
        )
    ) else if exist "%SCRIPT_DIR%%%f" (
        copy /y "%SCRIPT_DIR%%%f" "%PLUGINS_DIR%\%%f" >nul
        if errorlevel 1 (
            echo   ERROR: Failed to copy %%f - is the game folder writable?
            set "DEPLOY_FAILED=1"
        ) else (
            echo   Deployed: %%f
        )
    ) else (
        echo   ERROR: %%f not found in installer package
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
echo ============================================
echo  %MOD_DISPLAY_NAME% v%MOD_VERSION% installed!
echo ============================================
echo.
:: Percent-expansion splits MOD_CONTROLS on its embedded &echo separators;
:: delayed expansion prints them literally. Kept outside a ( ) block so a
:: literal ) in the controls text cannot close the block.
if not defined MOD_CONTROLS goto :controls_done
echo %MOD_CONTROLS%
echo.
:controls_done
echo Make sure OpenTrack is running and sending
echo data to UDP port 4242.
echo.
exit /b 0

:: ============================================
:: Install REFramework from the bundled vendored copy.
:: Vendor tree is the single source of truth at install time. To bump the
:: bundled nightly, run `pixi run update-deps` in the mod repo and commit.
:: See ~/.claude/CLAUDE.md "Vendoring Third-Party Dependencies".
:: ============================================
:install_reframework
set "VENDOR_DIR=%SCRIPT_DIR%vendor\reframework"
set "VENDOR_ZIP=%VENDOR_DIR%\%REFRAMEWORK_VENDOR_ZIP_NAME%"

if not exist "%VENDOR_ZIP%" (
    echo   ERROR: Bundled REFramework not found at:
    echo     !VENDOR_ZIP!
    echo   The installer ZIP is corrupt. Re-download the release.
    exit /b 1
)

echo   Extracting bundled REFramework...
:: Paths travel by environment variable - a game folder with an apostrophe in
:: it would close the single-quoted literal early and fail to parse.
set "CUL_VENDOR_ZIP=%VENDOR_ZIP%"
set "CUL_GAME_PATH=%GAME_PATH%"
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "Expand-Archive -LiteralPath $env:CUL_VENDOR_ZIP -DestinationPath $env:CUL_GAME_PATH -Force"

if not exist "%GAME_PATH%\dinput8.dll" (
    echo   ERROR: REFramework installation failed.
    exit /b 1
)

:: Flatscreen-only. The per-game nightly zip bundles VR runtime DLLs; if
:: present, REFramework auto-loads its VR mod when a runtime is available
:: (e.g. SteamVR installed) and takes over the camera with per-eye stereo
:: rendering, which fights our flat head-tracking. Strip them so REFramework
:: stays flatscreen. See https://cursey.github.io/reframework-book/ VR notes.
for %%f in (openvr_api.dll openxr_loader.dll DELETE_OPENVR_API_DLL_IF_YOU_WANT_TO_USE_OPENXR) do (
    if exist "%GAME_PATH%\%%f" (
        del /q "%GAME_PATH%\%%f" >nul 2>&1
        echo   Removed VR runtime file: %%f ^(flatscreen install^)
    )
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
