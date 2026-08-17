@echo off
:: ============================================
:: CameraUnlock UE4SS Install Template
:: ============================================
:: Source of truth: cameraunlock-core/scripts/templates/install-ue4ss.cmd.
:: Copy to <mod>/scripts/install.cmd, edit CONFIG BLOCK, leave the rest
:: alone. Contract: see ~/.claude/CLAUDE.md "install.cmd / uninstall.cmd".
::
:: UE4SS (Unreal Engine 4/5 Scripting System): a single-DLL injector that
:: lives next to the game's shipping exe in <Game>\<Project>\Binaries\Win64.
:: Mods deploy as Lua mods into Win64\Mods\<ModName>\ and get registered
:: in Win64\Mods\mods.txt.
::
:: Launcher CLI: install.cmd [GAME_PATH] [/y]
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=my-game-id"
set "MOD_DISPLAY_NAME=My Mod Name"
set "MOD_INTERNAL_NAME=MyMod"
set "MOD_VERSION=0.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=UE4SS"
set "MOD_CONTROLS="
:: UE4_BINARIES_RELDIR is the path under GAME_PATH where the shipping exe
:: lives (e.g. "Pathless\Binaries\Win64"). UE4SS extracts here and mods
:: deploy to its Mods\ subfolder.
set "UE4_BINARIES_RELDIR=Project\Binaries\Win64"
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

:detect_yes_flag
if "%~1"=="" exit /b 0
if /i "%~1"=="/y"    set "YES_FLAG=1"
if /i "%~1"=="-y"    set "YES_FLAG=1"
if /i "%~1"=="--yes" set "YES_FLAG=1"
shift
goto :detect_yes_flag

:main

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
for %%v in (GAME_ID MOD_DISPLAY_NAME MOD_INTERNAL_NAME STATE_FILE FRAMEWORK_TYPE UE4_BINARIES_RELDIR) do (
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

set "WIN64_DIR=%GAME_PATH%\%UE4_BINARIES_RELDIR%"
if not exist "%WIN64_DIR%" (
    echo ERROR: UE4 Win64 directory not found:
    echo   !WIN64_DIR!
    echo Check the UE4_BINARIES_RELDIR value in this script's CONFIG BLOCK.
    exit /b 1
)
echo Win64 dir: !WIN64_DIR!
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

:: -------- Ensure UE4SS --------
if not exist "%WIN64_DIR%\UE4SS.dll" (
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

set "MOD_SRC=%SCRIPT_DIR%mod"
set "MOD_DST=%WIN64_DIR%\Mods\%MOD_INTERNAL_NAME%"

if not exist "%MOD_SRC%" (
    echo   ERROR: mod source folder not found: !MOD_SRC!
    echo   The installer ZIP is corrupt. Re-download the release.
    exit /b 1
)

if exist "%MOD_DST%" rmdir /s /q "%MOD_DST%"
mkdir "%MOD_DST%" >nul 2>&1
xcopy /e /i /y /q "%MOD_SRC%\*" "%MOD_DST%\" >nul
if errorlevel 1 (
    echo   ERROR: failed to copy mod files to !MOD_DST!.
    exit /b 1
)
echo   Deployed mod folder: !MOD_DST!

:: -------- Register in mods.txt --------
call :register_in_mods_txt
if errorlevel 1 exit /b 1

:: -------- Write state file --------
call :write_state_file

echo.
echo ========================================
echo   Deployment Complete!
echo ========================================
echo.
echo %MOD_DISPLAY_NAME% has been deployed to:
echo   !MOD_DST!
echo.
echo Start the game to use the mod!
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
:: See ~/.claude/CLAUDE.md "Vendoring Third-Party Dependencies".
:: ============================================
:install_ue4ss
set "VENDOR_DIR=%SCRIPT_DIR%vendor\ue4ss"
set "VENDOR_ZIP=%VENDOR_DIR%\UE4SS.zip"

if not exist "%VENDOR_ZIP%" (
    echo   ERROR: Bundled UE4SS not found at:
    echo     !VENDOR_ZIP!
    echo   The installer ZIP is corrupt. Re-download the release.
    exit /b 1
)

echo   Extracting bundled UE4SS to Win64 directory...
"%SystemRoot%\System32\tar.exe" -xf "%VENDOR_ZIP%" -C "%WIN64_DIR%"
if errorlevel 1 (
    echo   ERROR: Extraction failed.
    exit /b 1
)
echo   UE4SS installed successfully!
exit /b 0

:: ============================================
:: Ensure "<MOD_INTERNAL_NAME> : 1" line is present in Win64\Mods\mods.txt.
:: UE4SS auto-creates mods.txt on first launch; we may need to write it
:: ourselves if the user is installing into a fresh UE4SS layout.
:: ============================================
:register_in_mods_txt
set "MODS_TXT=%WIN64_DIR%\Mods\mods.txt"
if not exist "%MODS_TXT%" (
    > "%MODS_TXT%" echo %MOD_INTERNAL_NAME% : 1
    echo   Created mods.txt with %MOD_INTERNAL_NAME% entry
    exit /b 0
)
findstr /b /c:"%MOD_INTERNAL_NAME% " "%MODS_TXT%" >nul 2>&1
if not errorlevel 1 (
    echo   mods.txt already lists %MOD_INTERNAL_NAME%
    exit /b 0
)
>> "%MODS_TXT%" echo %MOD_INTERNAL_NAME% : 1
echo   Appended %MOD_INTERNAL_NAME% to mods.txt
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
