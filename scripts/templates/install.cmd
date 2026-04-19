@echo off
:: ============================================
:: CameraUnlock Install Template
:: ============================================
:: Copy this file and fill in the config block below for your mod.
:: Everything below the config block is shared logic — do not edit.
:: ============================================

:: --- CONFIG BLOCK (edit these for your mod) ---
set "MOD_DISPLAY_NAME=My Mod Name"
set "GAME_EXE=Game.exe"
set "GAME_DISPLAY_NAME=Full Game Name"
set "STEAM_FOLDER_NAME=GameFolder"
set "ENV_VAR_NAME=GAME_PATH"
set "MOD_DLLS=MyMod.dll CameraUnlock.Core.dll CameraUnlock.Core.Unity.dll"
set "MOD_INTERNAL_NAME=MyMod"
set "MOD_VERSION=1.0.0"
set "STATE_FILE=.mod-state.json"
set "BEPINEX_ARCH=x86"
:: BEPINEX_ARCH picks the vendored fallback zip name (BepInEx_win_<arch>.zip).
:: Upstream version is resolved by vendor/bepinex/fetch-latest.ps1 at install time
:: and the packager refreshes the vendored copy in vendor/bepinex/ at release time.
set "MOD_CONTROLS="
set "GOG_IDS="
set "SEARCH_DIRS="
:: --- END CONFIG BLOCK ---

call :main %*
set "_EC=%errorlevel%"
echo.
pause
exit /b %_EC%

:main
setlocal enabledelayedexpansion

echo.
echo === %MOD_DISPLAY_NAME% - Install ===
echo.

set "SCRIPT_DIR=%~dp0"
set "GAME_PATH="

:: --- Find game path ---

:: Check command line argument
if not "%~1"=="" (
    if exist "%~1\%GAME_EXE%" (
        set "GAME_PATH=%~1"
        goto :found_game
    )
    echo ERROR: %GAME_EXE% not found at: "%~1"
    echo.
    exit /b 1
)

:: Check environment variable
if defined %ENV_VAR_NAME% (
    call set "_ENV_PATH=%%%ENV_VAR_NAME%%%"
    if exist "!_ENV_PATH!\%GAME_EXE%" (
        set "GAME_PATH=!_ENV_PATH!"
        goto :found_game
    )
)

:: Check Steam
call :find_steam_game
if defined GAME_PATH goto :found_game

:: Check GOG
call :find_gog_game
if defined GAME_PATH goto :found_game

:: Check Epic
call :find_epic_game
if defined GAME_PATH goto :found_game

:: Check common directories
call :find_game_in_dirs
if defined GAME_PATH goto :found_game

echo ERROR: Could not find %GAME_DISPLAY_NAME% installation.
echo.
echo Please either:
echo   1. Set %ENV_VAR_NAME% environment variable to your game folder
echo   2. Run: install.cmd "C:\path\to\game"
echo.
exit /b 1

:found_game
echo Game found: %GAME_PATH%
echo.

:: --- Check if game is running ---
tasklist /fi "imagename eq %GAME_EXE%" 2>nul | findstr /i "%GAME_EXE%" >nul 2>&1
if not errorlevel 1 (
    echo ERROR: %GAME_DISPLAY_NAME% is currently running.
    echo Please close the game before installing.
    echo.
    exit /b 1
)

:: --- Check BepInEx ---
if not exist "%GAME_PATH%\BepInEx\core\BepInEx.dll" (
    echo BepInEx not found. Installing...
    echo.
    call :install_bepinex
    if errorlevel 1 exit /b 1
    echo.
    color 0E
    echo ========================================
    echo   BepInEx installed - action required
    echo ========================================
    echo.
    echo BepInEx was just installed but needs to initialize first.
    echo.
    echo   1. Start %GAME_DISPLAY_NAME%
    echo   2. Wait until you reach the main menu
    echo   3. Close the game
    echo   4. Come back here and type "install" to continue
    echo.
    :bepinex_gate
    set "_CONFIRM="
    set /p "_CONFIRM=Type install to continue: "
    if /i not "!_CONFIRM!"=="install" goto :bepinex_gate
    echo.
    color
) else (
    echo Existing BepInEx detected, skipping loader install, deploying plugin only.
)
echo.

:: --- Deploy mod files ---
echo Deploying mod files...

set "PLUGINS_PATH=%GAME_PATH%\BepInEx\plugins"
set "DLL_DIR=%SCRIPT_DIR%plugins"

if not exist "%PLUGINS_PATH%" mkdir "%PLUGINS_PATH%"

set "DEPLOY_FAILED=0"
for %%f in (%MOD_DLLS%) do (
    if exist "%DLL_DIR%\%%f" (
        copy /y "%DLL_DIR%\%%f" "%PLUGINS_PATH%\" >nul
        echo   Deployed %%f
    ) else (
        echo   ERROR: %%f not found in plugins folder
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

:: --- Update state file ---
:: Preserve installed_by_us flag from previous state
set "WE_INSTALLED=false"
if exist "%GAME_PATH%\%STATE_FILE%" (
    findstr /c:"installed_by_us" "%GAME_PATH%\%STATE_FILE%" 2>nul | findstr /c:"true" >nul 2>&1
    if not errorlevel 1 set "WE_INSTALLED=true"
)

> "%GAME_PATH%\%STATE_FILE%" (
    echo {
    echo   "framework": {
    echo     "type": "BepInEx",
    echo     "installed_by_us": !WE_INSTALLED!
    echo   },
    echo   "mod": {
    echo     "name": "%MOD_INTERNAL_NAME%",
    echo     "version": "%MOD_VERSION%"
    echo   }
    echo }
)

echo.
echo ========================================
echo   Deployment Complete!
echo ========================================
echo.
echo %MOD_DISPLAY_NAME% has been deployed to:
echo   %PLUGINS_PATH%
echo.
echo Start the game to use the mod!
if defined MOD_CONTROLS (
    echo.
    echo !MOD_CONTROLS!
)
echo.
exit /b 0

:: ============================================
:: Find game in Steam libraries
:: ============================================
:find_steam_game
set "STEAM_PATH="

:: Get Steam install path from registry (64-bit)
for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\WOW6432Node\Valve\Steam" /v InstallPath 2^>nul') do set "STEAM_PATH=%%b"

:: Try 32-bit registry
if not defined STEAM_PATH (
    for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\Valve\Steam" /v InstallPath 2^>nul') do set "STEAM_PATH=%%b"
)

:: Check default Steam library
if defined STEAM_PATH (
    if exist "%STEAM_PATH%\steamapps\common\%STEAM_FOLDER_NAME%\%GAME_EXE%" (
        set "GAME_PATH=%STEAM_PATH%\steamapps\common\%STEAM_FOLDER_NAME%"
        exit /b 0
    )
)

:: Parse libraryfolders.vdf for additional Steam library paths
if defined STEAM_PATH (
    set "VDF_FILE=%STEAM_PATH%\steamapps\libraryfolders.vdf"
    if exist "!VDF_FILE!" (
        for /f "tokens=1,2 delims=	 " %%a in ('findstr /c:"\"path\"" "!VDF_FILE!" 2^>nul') do (
            set "_LIB_PATH=%%~b"
            set "_LIB_PATH=!_LIB_PATH:\\=\!"
            if exist "!_LIB_PATH!\steamapps\common\%STEAM_FOLDER_NAME%\%GAME_EXE%" (
                set "GAME_PATH=!_LIB_PATH!\steamapps\common\%STEAM_FOLDER_NAME%"
                exit /b 0
            )
        )
    )
)

exit /b 1

:: ============================================
:: Find game in GOG registry
:: ============================================
:find_gog_game
if not defined GOG_IDS exit /b 1
for %%g in (%GOG_IDS%) do (
    for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\WOW6432Node\GOG.com\Games\%%g" /v path 2^>nul') do (
        if exist "%%b\%GAME_EXE%" ( set "GAME_PATH=%%b" & exit /b 0 )
    )
    for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\GOG.com\Games\%%g" /v path 2^>nul') do (
        if exist "%%b\%GAME_EXE%" ( set "GAME_PATH=%%b" & exit /b 0 )
    )
)
exit /b 1

:: ============================================
:: Find game in Epic Games manifests
:: ============================================
:find_epic_game
set "_EPIC_MANIFESTS=%ProgramData%\Epic\EpicGamesLauncher\Data\Manifests"
if not exist "%_EPIC_MANIFESTS%" exit /b 1
for %%m in ("%_EPIC_MANIFESTS%\*.item") do (
    for /f "usebackq delims=" %%l in ("%%m") do (
        set "_EL=%%l"
        if not "!_EL:InstallLocation=!"=="!_EL!" (
            set "_EL=!_EL:*InstallLocation=!"
            set "_EL=!_EL:~4!"
            set "_EL=!_EL:~0,-2!"
            set "_EL=!_EL:\\=\!"
            if exist "!_EL!\%GAME_EXE%" ( set "GAME_PATH=!_EL!" & exit /b 0 )
        )
    )
)
exit /b 1

:: ============================================
:: Find game by scanning common directories
:: ============================================
:find_game_in_dirs
if not defined SEARCH_DIRS exit /b 1
for %%d in (%SEARCH_DIRS%) do (
    if exist "%%~d\%GAME_EXE%" ( set "GAME_PATH=%%~d" & exit /b 0 )
    for /f "delims=" %%p in ('dir /b /ad "%%~d" 2^>nul') do (
        if exist "%%~d\%%p\%GAME_EXE%" ( set "GAME_PATH=%%~d\%%p" & exit /b 0 )
        for /f "delims=" %%s in ('dir /b /ad "%%~d\%%p" 2^>nul') do (
            if exist "%%~d\%%p\%%s\%GAME_EXE%" ( set "GAME_PATH=%%~d\%%p\%%s" & exit /b 0 )
        )
    )
)
exit /b 1

:: ============================================
:: Install BepInEx (upstream-first, fall back to vendored copy)
:: See vendoring pattern docs in ~/.claude/CLAUDE.md "Vendoring Third-Party Dependencies"
:: ============================================
:install_bepinex
set "VENDOR_DIR=%SCRIPT_DIR%vendor\bepinex"
set "VENDOR_ZIP=%VENDOR_DIR%\BepInEx_win_%BEPINEX_ARCH%.zip"
set "FETCH_SCRIPT=%VENDOR_DIR%\fetch-latest.ps1"
set "BEP_ZIP=%TEMP%\BepInEx_install.zip"
set "LOADER_SOURCE="

if exist "%FETCH_SCRIPT%" (
    echo   Trying upstream BepInEx %BEPINEX_ARCH% (latest within range)...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%FETCH_SCRIPT%" -OutputPath "%BEP_ZIP%" >nul 2>&1
    if not errorlevel 1 (
        set "LOADER_SOURCE=%BEP_ZIP%"
        set "USED_UPSTREAM=1"
        echo   Using upstream BepInEx.
    )
)

if not defined LOADER_SOURCE (
    if not exist "%VENDOR_ZIP%" (
        echo   ERROR: Upstream unreachable AND bundled fallback missing at:
        echo     %VENDOR_ZIP%
        echo   The installer ZIP is corrupt. Re-download the release.
        exit /b 1
    )
    set "LOADER_SOURCE=%VENDOR_ZIP%"
    echo   Upstream unreachable, using bundled fallback copy.
)

echo   Extracting BepInEx to game directory...
tar -xf "%LOADER_SOURCE%" -C "%GAME_PATH%"
if errorlevel 1 (
    echo   ERROR: Extraction failed.
    if defined USED_UPSTREAM del "%BEP_ZIP%" 2>nul
    exit /b 1
)
if defined USED_UPSTREAM del "%BEP_ZIP%" 2>nul

:: Create plugins directory
if not exist "%GAME_PATH%\BepInEx\plugins" mkdir "%GAME_PATH%\BepInEx\plugins"

:: Enable console logging
if not exist "%GAME_PATH%\BepInEx\config" mkdir "%GAME_PATH%\BepInEx\config"
> "%GAME_PATH%\BepInEx\config\BepInEx.cfg" (
    echo [Logging.Console]
    echo Enabled = true
    echo.
    echo [Logging.Disk]
    echo Enabled = true
)

:: Write state file marking that we installed BepInEx
> "%GAME_PATH%\%STATE_FILE%" (
    echo {
    echo   "framework": {
    echo     "type": "BepInEx",
    echo     "installed_by_us": true
    echo   }
    echo }
)

echo   BepInEx installed successfully!
exit /b 0
