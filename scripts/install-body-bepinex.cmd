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
:: Launcher CLI (passed through %*): [GAME_PATH] [/y]
:: ============================================

call :detect_yes_flag %*
call :main %*
set "_EC=%errorlevel%"
if not defined YES_FLAG ( echo. & pause )
exit /b %_EC%

:: ============================================
:: Pre-scan args at outer scope so YES_FLAG propagates to the post-:main
:: pause check. Quoted-string form is required - bracket form
:: `if [%~1]==[/y]` does NOT quote, so a path arg containing whitespace
:: ("C:\...\Gone Home") splits across the brackets and crashes cmd with
:: "[Home]==[/y] was unexpected at this time".
:: ============================================
:detect_yes_flag
if "%~1"=="" exit /b 0
if /i "%~1"=="/y"    set "YES_FLAG=1"
if /i "%~1"=="-y"    set "YES_FLAG=1"
if /i "%~1"=="--yes" set "YES_FLAG=1"
shift
goto :detect_yes_flag

:main
setlocal enabledelayedexpansion

:: WRAPPER_DIR is the wrapper's %~dp0 (release-zip root or <mod>/scripts/).
:: Resolved here as SCRIPT_DIR so the rest of the body reads naturally.
if defined WRAPPER_DIR ( set "SCRIPT_DIR=%WRAPPER_DIR%" ) else ( set "SCRIPT_DIR=%~dp0" )

:: -------- Arg parser (canonical, do not modify) --------
set "YES_FLAG="
set "_GIVEN_PATH="
:parse_args
if "%~1"=="" goto :args_done
set "_ARG=%~1"
if /i "!_ARG!"=="/y"    ( set "YES_FLAG=1" & shift & goto :parse_args )
if /i "!_ARG!"=="-y"    ( set "YES_FLAG=1" & shift & goto :parse_args )
if /i "!_ARG!"=="--yes" ( set "YES_FLAG=1" & shift & goto :parse_args )
if "!_ARG:~0,2!"=="--" ( echo ERROR: unknown flag "!_ARG!" & exit /b 2 )
if "!_ARG:~0,1!"=="/"  ( echo ERROR: unknown flag "!_ARG!" & exit /b 2 )
if "!_ARG:~0,1!"=="-"  ( echo ERROR: unknown flag "!_ARG!" & exit /b 2 )
if not defined _GIVEN_PATH (
    if exist "!_ARG!\" ( set "_GIVEN_PATH=!_ARG!" & shift & goto :parse_args )
)
echo ERROR: unrecognised argument "!_ARG!"
exit /b 2
:args_done

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

echo Game found: %GAME_PATH%
echo.

:: -------- Game-running check --------
tasklist /fi "imagename eq %GAME_EXE%" 2>nul | findstr /i "%GAME_EXE%" >nul 2>&1
if not errorlevel 1 (
    echo ERROR: %GAME_DISPLAY_NAME% is currently running.
    echo Please close the game before installing.
    echo.
    exit /b 1
)

:: -------- Prior state: preserve installed_by_us=true across re-installs --------
set "WE_INSTALLED=false"
if exist "%GAME_PATH%\%STATE_FILE%" (
    findstr /c:"installed_by_us" "%GAME_PATH%\%STATE_FILE%" 2>nul | findstr /c:"true" >nul 2>&1
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
if exist "%GAME_PATH%\BepInEx\core\BepInEx.dll"      set "_LOADER_PRESENT=1"
if exist "%GAME_PATH%\BepInEx\core\BepInEx.Core.dll" set "_LOADER_PRESENT=1"

set "_LOADER_BAD="
if defined _LOADER_PRESENT (
    call :verify_loader_arch
    if errorlevel 1 set "_LOADER_BAD=1"
)

if not defined _LOADER_PRESENT goto :install_loader
if defined _LOADER_BAD goto :replace_loader
echo Existing BepInEx detected, skipping loader install, deploying plugin only.
goto :after_loader

:replace_loader
echo Existing BepInEx is the wrong architecture for this game ^(expected %BEPINEX_ARCH%^).
echo This usually means another mod manager ^(Thunderstore Mod Manager,
echo r2modman, ...^) installed the wrong BepInExPack first.
echo Replacing it with the matching %BEPINEX_ARCH% loader...
call :wipe_existing_bepinex
goto :do_install_loader

:install_loader
echo BepInEx not found. Installing...

:do_install_loader
echo.
call :install_bepinex
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

set "PLUGINS_PATH=%GAME_PATH%\BepInEx\plugins"
set "DLL_DIR=%SCRIPT_DIR%plugins"
if defined PLUGIN_SUBFOLDER (
    set "DEPLOY_PATH=%PLUGINS_PATH%\%PLUGIN_SUBFOLDER%"
) else (
    set "DEPLOY_PATH=%PLUGINS_PATH%"
)

if not exist "%PLUGINS_PATH%" mkdir "%PLUGINS_PATH%"
if not exist "!DEPLOY_PATH!" mkdir "!DEPLOY_PATH!"

if defined PLUGIN_SUBFOLDER (
    for %%f in (%MOD_DLLS%) do (
        if exist "%PLUGINS_PATH%\%%f" (
            del /q "%PLUGINS_PATH%\%%f" >nul 2>&1
            echo   Removed flat-laid %%f from plugins\ ^(superseded by %PLUGIN_SUBFOLDER%\^)
        )
    )
)

set "DEPLOY_FAILED=0"
for %%f in (%MOD_DLLS%) do (
    if exist "%DLL_DIR%\%%f" (
        copy /y "%DLL_DIR%\%%f" "!DEPLOY_PATH!\" >nul
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

:: -------- Write state file --------
call :write_state_file

echo.
echo ========================================
echo   Deployment Complete!
echo ========================================
echo.
echo %MOD_DISPLAY_NAME% has been deployed to:
echo   %PLUGINS_PATH%
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
:: Verify the existing loader proxy (winhttp.dll) matches BEPINEX_ARCH.
:: Returns:
::   0 - arch matches, existing loader is trustworthy
::   1 - winhttp.dll missing OR wrong arch OR not a valid PE - caller
::       should wipe + reinstall
:: ============================================
:verify_loader_arch
:: BepInEx.dll without winhttp.dll = broken loader stack regardless of arch.
if not exist "%GAME_PATH%\winhttp.dll" exit /b 1
set "_ARCH_SHIM=%SCRIPT_DIR%shared\check-loader-arch.ps1"
if not exist "%_ARCH_SHIM%" set "_ARCH_SHIM=%SCRIPT_DIR%..\cameraunlock-core\scripts\check-loader-arch.ps1"
if not exist "%_ARCH_SHIM%" (
    echo   ERROR: check-loader-arch.ps1 not found in shared\ or ..\cameraunlock-core\scripts\.
    echo   If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo   If this is the dev tree, make sure the cameraunlock-core submodule is checked out.
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%_ARCH_SHIM%" -Path "%GAME_PATH%\winhttp.dll" -ExpectedArch %BEPINEX_ARCH%
exit /b %errorlevel%

:: ============================================
:: Wipe an existing wrong-arch BepInEx loader so :install_bepinex can
:: lay down our matching one. Removes the same set of files uninstall-
:: body.cmd would for a BepInEx framework type.
:: ============================================
:wipe_existing_bepinex
echo   Removing wrong-arch BepInEx files...
if exist "%GAME_PATH%\BepInEx" rmdir /s /q "%GAME_PATH%\BepInEx"
if exist "%GAME_PATH%\winhttp.dll" del /f /q "%GAME_PATH%\winhttp.dll"
if exist "%GAME_PATH%\doorstop_config.ini" del /f /q "%GAME_PATH%\doorstop_config.ini"
if exist "%GAME_PATH%\.doorstop_version" del /f /q "%GAME_PATH%\.doorstop_version"
if exist "%GAME_PATH%\changelog.txt" del /f /q "%GAME_PATH%\changelog.txt"
exit /b 0

:: ============================================
:: Install BepInEx from the bundled vendored copy.
:: Vendor tree is the single source of truth at install time. To bump the
:: bundled version, run `pixi run update-deps` in the mod repo and commit.
:: ============================================
:install_bepinex
:: Release ZIP layout has vendor/ flat next to install.cmd; the dev tree
:: has install.cmd in <repo>/scripts/ and vendor/ at <repo>/vendor/. Try
:: the release-zip layout first, then fall back to the dev-tree parent.
set "VENDOR_DIR=%SCRIPT_DIR%vendor\bepinex"
if not exist "%VENDOR_DIR%" set "VENDOR_DIR=%SCRIPT_DIR%..\vendor\bepinex"
if defined BEPINEX_VENDOR_ZIP_NAME (
    set "VENDOR_ZIP=%VENDOR_DIR%\%BEPINEX_VENDOR_ZIP_NAME%"
) else (
    set "VENDOR_ZIP=%VENDOR_DIR%\BepInEx_win_%BEPINEX_ARCH%.zip"
)

if not exist "!VENDOR_ZIP!" (
    echo   ERROR: Bundled BepInEx not found at:
    echo     !VENDOR_ZIP!
    echo   The installer ZIP is corrupt. Re-download the release.
    exit /b 1
)

echo   Extracting bundled BepInEx to game directory...
if defined BEPINEX_SUBFOLDER (
    rem Thunderstore BepInExPack: extract to temp, flatten wrapper into GAME_PATH.
    set "BEP_TEMP=%TEMP%\BepInEx_extract"
    if exist "!BEP_TEMP!" rmdir /s /q "!BEP_TEMP!"
    mkdir "!BEP_TEMP!"
    "%SystemRoot%\System32\tar.exe" -xf "!VENDOR_ZIP!" -C "!BEP_TEMP!"
    if errorlevel 1 (
        echo   ERROR: Extraction failed.
        rmdir /s /q "!BEP_TEMP!" 2>nul
        exit /b 1
    )
    xcopy /s /e /y /q "!BEP_TEMP!\%BEPINEX_SUBFOLDER%\*" "%GAME_PATH%\" >nul
    rmdir /s /q "!BEP_TEMP!"
) else (
    "%SystemRoot%\System32\tar.exe" -xf "!VENDOR_ZIP!" -C "%GAME_PATH%"
    if errorlevel 1 (
        echo   ERROR: Extraction failed.
        exit /b 1
    )
)

if not exist "%GAME_PATH%\BepInEx\plugins" mkdir "%GAME_PATH%\BepInEx\plugins"

:: Enable console + disk logging. Skip if BepInEx.cfg already exists
:: (Thunderstore packs ship preconfigured; don't clobber).
if not exist "%GAME_PATH%\BepInEx\config\BepInEx.cfg" (
    if not exist "%GAME_PATH%\BepInEx\config" mkdir "%GAME_PATH%\BepInEx\config"
    > "%GAME_PATH%\BepInEx\config\BepInEx.cfg" (
        echo [Logging.Console]
        echo Enabled = true
        echo.
        echo [Logging.Disk]
        echo Enabled = true
    )
)

echo   BepInEx installed successfully!
exit /b 0

:: ============================================
:: Write the canonical state file.
:: Schema version 1. Preserves WE_INSTALLED which may have been
:: already-true from a prior install.
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
