@echo off
:: ============================================
:: CameraUnlock Uninstall Template
:: ============================================
:: Copy to <mod>/scripts/uninstall.cmd, edit CONFIG BLOCK, leave the
:: rest alone. Detection is delegated to shared/find-game.ps1 which
:: reads cameraunlock-core/data/games.json by game id.
:: ============================================

:: --- CONFIG BLOCK (edit these for your mod) ---
set "GAME_ID=my-game-id"
set "MOD_DISPLAY_NAME=My Mod Name"
set "MOD_DLLS=MyMod.dll CameraUnlock.Core.dll CameraUnlock.Core.Unity.dll"
set "MOD_INTERNAL_NAME=MyMod"
set "STATE_FILE=.headtracking-state.json"
set "LEGACY_DLLS="
:: --- END CONFIG BLOCK ---

call :main %*
set "_EC=%errorlevel%"
echo.
pause
exit /b %_EC%

:main
setlocal enabledelayedexpansion

echo.
echo === %MOD_DISPLAY_NAME% - Uninstall ===
echo.

set "SCRIPT_DIR=%~dp0"
set "FORCE=0"

:: Parse args for --force / /force flags. The first path-looking arg
:: (if any) is passed to the shim as -GivenPath and wins over detection.
set "_GIVEN_PATH="
:parse_args
if "%~1"=="" goto :args_done
if /i "%~1"=="/force" ( set "FORCE=1" & shift & goto :parse_args )
if /i "%~1"=="--force" ( set "FORCE=1" & shift & goto :parse_args )
if not defined _GIVEN_PATH (
    if exist "%~1\" (
        set "_GIVEN_PATH=%~1"
        shift
        goto :parse_args
    )
)
shift
goto :parse_args

:args_done

:: --- Resolve game path via shared shim ---
set "_SHIM=%SCRIPT_DIR%shared\find-game.ps1"
if not exist "%_SHIM%" (
    echo ERROR: shared\find-game.ps1 missing from installer ZIP.
    echo This release is corrupt - re-download it from GitHub.
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
    echo Pass a path explicitly: uninstall.cmd "C:\path\to\game"
    echo.
    del "!_SHIM_OUT!" 2>nul
    exit /b 1
)
call "!_SHIM_OUT!"
del "!_SHIM_OUT!" 2>nul

echo Game found: %GAME_PATH%
echo.

:: --- Check if game is running ---
tasklist /fi "imagename eq %GAME_EXE%" 2>nul | findstr /i "%GAME_EXE%" >nul 2>&1
if not errorlevel 1 (
    echo ERROR: %GAME_DISPLAY_NAME% is currently running.
    echo Please close the game before uninstalling.
    echo.
    exit /b 1
)

:: --- Remove mod files ---
echo Removing mod files...

set "PLUGINS_PATH=%GAME_PATH%\BepInEx\plugins"
set "REMOVED=0"

for %%f in (%MOD_DLLS%) do (
    if exist "%PLUGINS_PATH%\%%f" (
        del "%PLUGINS_PATH%\%%f"
        echo   Removed: %%f
        set /a REMOVED+=1
    )
)

if defined LEGACY_DLLS (
    for %%f in (%LEGACY_DLLS%) do (
        if exist "%PLUGINS_PATH%\%%f" (
            del "%PLUGINS_PATH%\%%f"
            echo   Removed: %%f ^(legacy^)
            set /a REMOVED+=1
        )
    )
)

if "!REMOVED!"=="0" echo   No mod files found

:: --- Decide whether to remove BepInEx ---
if "!FORCE!"=="1" (
    echo.
    echo Removing BepInEx ^(--force^)...
    goto :remove_bepinex
)

set "WE_INSTALLED=0"
if exist "%GAME_PATH%\%STATE_FILE%" (
    findstr /c:"installed_by_us" "%GAME_PATH%\%STATE_FILE%" 2>nul | findstr /c:"true" >nul 2>&1
    if not errorlevel 1 set "WE_INSTALLED=1"
)

if "!WE_INSTALLED!"=="0" (
    echo.
    echo BepInEx was not installed by this mod - leaving intact. Use --force to remove anyway.
    goto :cleanup_state
)

echo.
echo Removing BepInEx ^(it was installed by this mod^)...

:remove_bepinex

if exist "%GAME_PATH%\BepInEx" (
    rmdir /s /q "%GAME_PATH%\BepInEx"
    echo   Removed: BepInEx folder
)

for %%f in (winhttp.dll doorstop_config.ini .doorstop_version) do (
    if exist "%GAME_PATH%\%%f" (
        del "%GAME_PATH%\%%f"
        echo   Removed: %%f
    )
)

:cleanup_state
if exist "%GAME_PATH%\%STATE_FILE%" (
    del "%GAME_PATH%\%STATE_FILE%"
    echo   Removed: state file
)

echo.
echo === Uninstall Complete ===
echo.
exit /b 0
