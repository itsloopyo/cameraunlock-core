<#
.SYNOPSIS
    CameraUnlock uninstall.ps1 template.

.DESCRIPTION
    Source of truth: cameraunlock-core/scripts/templates/uninstall.ps1.
    Copy to <mod>/scripts/uninstall.ps1, edit the CONFIG BLOCK, leave the
    rest alone. uninstall.cmd already resolves GAME_PATH via the shared
    find-game.ps1 shim and forwards it as -GamePath, so this script does
    NOT need to import GamePathDetection.psm1 in the normal flow. The
    import is attempted only as a convenience for direct dev runs
    (`./uninstall.ps1` from a checkout with the submodule present) - if
    the module isn't on disk we just require -GamePath.

    This template handles the simple "remove a mod folder + plugin DLL"
    shape that fits most CET / RED4ext / standalone mods. Mods that need
    multi-install enumeration (Steam + Xbox + given path) or .backup
    restoration should NOT use this template - write a bespoke
    uninstall.ps1 for those cases.

.PARAMETER GamePath
    Game install root. Required when the cameraunlock-core submodule
    isn't on disk (release ZIPs, Lopari profiles); optional in a dev
    checkout where auto-detect can run.

.PARAMETER KeepConfig
    Backs up <mod folder>/config.json (or equivalent) to %TEMP% before
    removing the mod folder.

.PARAMETER Force
    Accepted for compatibility with the CameraUnlock uninstall contract.
    Whether it does anything is per-mod - the contract uses it to
    escalate loader removal in BepInEx/MelonLoader/etc. mods. Pure
    CET/RED4ext mods treat it as a no-op since their frameworks are
    user-managed.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string]$GamePath,

    [Parameter(Mandatory = $false)]
    [switch]$KeepConfig,

    [Parameter(Mandatory = $false)]
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# --- CONFIG BLOCK ---------------------------------------------------------
# Game id matches the slug in cameraunlock-core/data/games.json. Used only
# for the (optional) auto-detect fallback below.
$GameId          = 'my-game-id'
$ModDisplayName  = 'MyMod'

# Relative path from $GamePath to the game executable - used to validate a
# user-supplied -GamePath.
$GameExeRelpath  = 'bin\x64\Game.exe'

# Paths to remove (relative to $GamePath). The mod folder is removed
# recursively; the plugin file is removed if present. Add or remove
# entries as needed - all are optional, missing paths log "not present".
$ModFolderRelpath  = 'bin\x64\plugins\cyber_engine_tweaks\mods\MyMod'
$PluginFileRelpath = 'red4ext\plugins\MyMod.dll'

# Filename inside the mod folder to back up when -KeepConfig is set.
$ConfigFilename    = 'config.json'

# Backup destination for -KeepConfig.
$ConfigBackupName  = "$ModDisplayName-config-backup.json"
# --- END CONFIG BLOCK -----------------------------------------------------

function Write-Info    { param([string]$Message) Write-Host "[INFO] $Message" -ForegroundColor Cyan }
function Write-Success { param([string]$Message) Write-Host "[SUCCESS] $Message" -ForegroundColor Green }
function Write-Fail    { param([string]$Message) Write-Host "[ERROR] $Message" -ForegroundColor Red }

# Optional auto-detect: only loads if the cameraunlock-core submodule is on
# disk. uninstall.cmd has already resolved -GamePath via the shared shim in
# the normal flow, so the absence of this module is not an error - it just
# means -GamePath becomes mandatory.
$projectRootForUninstall = Split-Path -Parent $PSScriptRoot
$gamePathDetectionModule = Join-Path $projectRootForUninstall 'cameraunlock-core/powershell/GamePathDetection.psm1'
$haveGamePathDetection = $false
if (Test-Path -LiteralPath $gamePathDetectionModule) {
    Import-Module $gamePathDetectionModule -Force
    $haveGamePathDetection = $true
}

function Find-GameInstallation {
    param([string]$CustomPath)

    if ($CustomPath) {
        $exePath = Join-Path $CustomPath $GameExeRelpath
        if ((Test-Path $CustomPath) -and (Test-Path $exePath)) { return $CustomPath }
        Write-Fail "Provided -GamePath does not contain $ModDisplayName's expected exe at $GameExeRelpath : $CustomPath"
        exit 1
    }

    if (-not $haveGamePathDetection) {
        Write-Fail "Pass -GamePath explicitly: this build doesn't include the auto-detection module."
        exit 1
    }

    $found = Find-GamePath -GameId $GameId
    if ($found) { return $found }
    return $null
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Yellow
Write-Host "  $ModDisplayName Uninstall Script" -ForegroundColor Yellow
Write-Host "========================================" -ForegroundColor Yellow
Write-Host ""

$gameDir = Find-GameInstallation -CustomPath $GamePath
if (-not $gameDir) {
    Write-Fail "$ModDisplayName game installation not found!"
    Write-Host "Specify a path: .\uninstall.ps1 -GamePath ""C:\path\to\game""" -ForegroundColor Yellow
    exit 1
}
Write-Info "Found $ModDisplayName at: $gameDir"

# The CONFIG BLOCK paths are author-edited and documented as optional, but
# `Join-Path $gameDir ''` returns "$gameDir\" and Test-Path on that is TRUE.
# A blank $ModFolderRelpath therefore aimed the Remove-Item -Recurse -Force
# below at the entire game install and reported it as a successful uninstall.
# Blank means "not configured"; anything that resolves to or above $gameDir is
# a config error, not something to delete.
function Resolve-TargetUnderGame {
    param([string]$Relpath, [string]$Label)

    if ([string]::IsNullOrWhiteSpace($Relpath)) {
        Write-Info "$Label is not configured - skipping."
        return $null
    }

    # Resolved through the PROVIDER, not [System.IO.Path]::GetFullPath. GetFullPath
    # resolves a relative path against [Environment]::CurrentDirectory, which does NOT
    # track Set-Location - so `cd D:\Games\MyGame; .\uninstall.ps1 -GamePath .` had
    # Find-GameInstallation validate D:\Games\MyGame (Test-Path uses the provider) while
    # this resolved "." to the shell's start directory. Root and target then resolved
    # consistently WRONG, so the containment check below passed and the
    # Remove-Item -Recurse -Force was aimed at a tree that was never the game.
    $resolver   = $ExecutionContext.SessionState.Path
    $rootFull   = $resolver.GetUnresolvedProviderPathFromPSPath($gameDir).TrimEnd('\')
    $targetFull = $resolver.GetUnresolvedProviderPathFromPSPath((Join-Path $gameDir $Relpath)).TrimEnd('\')

    if (-not $targetFull.StartsWith($rootFull + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        Write-Fail "$Label resolves outside the game folder and will not be removed: $targetFull"
        exit 1
    }
    return $targetFull
}

$modDir  = Resolve-TargetUnderGame -Relpath $ModFolderRelpath  -Label 'Mod folder'
$dllPath = Resolve-TargetUnderGame -Relpath $PluginFileRelpath -Label 'Plugin file'

$removedSomething = $false

if ($modDir -and (Test-Path -LiteralPath $modDir)) {
    if ($KeepConfig) {
        $cfgSrc = Join-Path $modDir $ConfigFilename
        if (Test-Path -LiteralPath $cfgSrc) {
            $backup = Join-Path $env:TEMP $ConfigBackupName
            Copy-Item -LiteralPath $cfgSrc -Destination $backup -Force
            Write-Info "Backed up $ConfigFilename to: $backup"
        }
    }
    Remove-Item -LiteralPath $modDir -Recurse -Force
    Write-Info "Removed mod folder: $modDir"
    $removedSomething = $true
} elseif ($modDir) {
    Write-Info "Mod folder not present (already removed?)"
}

if ($dllPath -and (Test-Path -LiteralPath $dllPath)) {
    Remove-Item -LiteralPath $dllPath -Force
    Write-Info "Removed plugin: $dllPath"
    $removedSomething = $true
} elseif ($dllPath) {
    Write-Info "Plugin not present (already removed or never installed)"
}

Write-Host ""
if ($removedSomething) {
    Write-Success "$ModDisplayName uninstalled successfully."
} else {
    Write-Info "Nothing to uninstall - mod was not installed."
}
exit 0
