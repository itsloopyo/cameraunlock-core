@echo off
:: ============================================
:: CameraUnlock Mono.Cecil Patcher Install Template
:: ============================================
:: Source of truth: cameraunlock-core/scripts/templates/install-cecil.cmd.
:: Copy to <mod>/scripts/install.cmd, edit CONFIG BLOCK, leave the rest
:: alone. Contract: see ~/.claude/CLAUDE.md "install.cmd / uninstall.cmd".
::
:: Standalone Mono.Cecil patcher: no loader download, no external framework.
:: Vendoring: Mono.Cecil.dll is bundled in plugins/ (it's just a copied DLL;
:: no vendor/ tree needed because there's nothing to fetch from upstream).
:: Patches <Managed>/Assembly-CSharp.dll in place and backs up the original
:: to Assembly-CSharp.dll.original. Uninstall restores the backup.
::
:: Launcher CLI: install.cmd [GAME_PATH] [/y]
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=my-game-id"
set "MOD_DISPLAY_NAME=My Mod Name"
set "MOD_DLLS=MyMod.dll CameraUnlock.Core.dll CameraUnlock.Core.Unity.dll Mono.Cecil.dll"
set "MOD_INTERNAL_NAME=MyMod"
set "MOD_VERSION=1.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=MonoCecil"
set "MANAGED_SUBFOLDER=MyGame_Data\Managed"
set "ASSEMBLY_DLL=Assembly-CSharp.dll"
set "PATCHER_FILE=BootstrapPatcher.cs"
set "MOD_CONTROLS="
:: --- END CONFIG BLOCK ---

call :main %*
set "_EC=%errorlevel%"
if not defined YES_FLAG ( echo. & pause )
exit /b %_EC%

:main
setlocal enabledelayedexpansion

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

set "SCRIPT_DIR=%~dp0"

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

set "MANAGED_PATH=%GAME_PATH%\%MANAGED_SUBFOLDER%"
set "ASSEMBLY_PATH=%MANAGED_PATH%\%ASSEMBLY_DLL%"
set "BACKUP_PATH=%MANAGED_PATH%\%ASSEMBLY_DLL%.original"
set "MOD_DIR=%SCRIPT_DIR%mod"

if not exist "%MANAGED_PATH%" (
    echo ERROR: %MANAGED_SUBFOLDER% folder not found.
    echo   Expected at: !MANAGED_PATH!
    echo.
    exit /b 1
)

if not exist "%ASSEMBLY_PATH%" (
    echo ERROR: %ASSEMBLY_DLL% not found.
    echo   Expected at: !ASSEMBLY_PATH!
    echo.
    exit /b 1
)

for %%f in (%MOD_DLLS%) do (
    if not exist "%MOD_DIR%\%%f" (
        echo ERROR: %%f not found in mod folder.
        echo   Make sure all files from the release package are intact.
        echo.
        exit /b 1
    )
)

if not exist "%MOD_DIR%\%PATCHER_FILE%" (
    echo ERROR: %PATCHER_FILE% not found in mod folder.
    echo   Make sure all files from the release package are intact.
    echo.
    exit /b 1
)

:: -------- Prior state --------
set "WE_INSTALLED=false"
if exist "%GAME_PATH%\%STATE_FILE%" (
    findstr /c:"installed_by_us" "%GAME_PATH%\%STATE_FILE%" 2>nul | findstr /c:"true" >nul 2>&1
    if not errorlevel 1 set "WE_INSTALLED=true"
)

:: -------- Back up Assembly DLL (or restore clean state if we're re-patching) --------
echo Backing up %ASSEMBLY_DLL%...
if not exist "%BACKUP_PATH%" (
    copy /y "%ASSEMBLY_PATH%" "%BACKUP_PATH%" >nul
    echo   Created: %ASSEMBLY_DLL%.original
    set "WE_INSTALLED=true"
) else (
    echo   Backup already exists, restoring clean state before re-patch...
    copy /y "%BACKUP_PATH%" "%ASSEMBLY_PATH%" >nul
    :: WE_INSTALLED stays whatever it was - we backed up on the first install,
    :: and that entitlement doesn't regress just because we're re-running.
)
echo.

:: -------- Copy mod files --------
echo Deploying mod files...

set "DEPLOY_FAILED=0"
for %%f in (%MOD_DLLS%) do (
    copy /y "%MOD_DIR%\%%f" "%MANAGED_PATH%\" >nul
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

:: Unblock DLLs (Windows SmartScreen MOTW)
powershell -ExecutionPolicy Bypass -Command ^
    "Get-ChildItem '%MANAGED_PATH%\*.dll' | Unblock-File -ErrorAction SilentlyContinue"

:: -------- Patch Assembly DLL --------
echo Patching %ASSEMBLY_DLL%...

set "CECIL_PATH=%MANAGED_PATH%\Mono.Cecil.dll"
set "PATCHER_PATH=%MOD_DIR%\%PATCHER_FILE%"

powershell -ExecutionPolicy Bypass -Command ^
    "Add-Type -Path '%CECIL_PATH%'; " ^
    "$code = Get-Content '%PATCHER_PATH%' -Raw; " ^
    "$cp = New-Object System.CodeDom.Compiler.CompilerParameters; " ^
    "$cp.ReferencedAssemblies.Add('%CECIL_PATH%'); " ^
    "$cp.ReferencedAssemblies.Add('System.dll'); " ^
    "$cp.ReferencedAssemblies.Add('System.Core.dll'); " ^
    "$cp.CompilerOptions = '/nowarn:1668 /warn:0'; " ^
    "$cp.TreatWarningsAsErrors = $false; " ^
    "Add-Type -TypeDefinition $code -CompilerParameters $cp; " ^
    "if (-not [BootstrapPatcher]::PatchAssembly('%ASSEMBLY_PATH%')) { exit 1 }"

if errorlevel 1 (
    echo.
    echo ERROR: Patching failed.
    echo Try verifying game files through Steam and running the installer again.
    echo.
    exit /b 1
)

:: -------- Write state file --------
call :write_state_file

echo.
echo ========================================
echo   Installation Complete!
echo ========================================
echo.
echo %MOD_DISPLAY_NAME% has been installed to:
echo   %MANAGED_PATH%
echo.
echo Start the game to use the mod!
if defined MOD_CONTROLS (
    echo.
    echo !MOD_CONTROLS!
)
echo.
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
