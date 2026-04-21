#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
    Bridge between install.cmd and the shared game-detection module.
.DESCRIPTION
    Called by each mod's install.cmd / uninstall.cmd. Looks up the
    game by id in cameraunlock-core/data/games.json, uses the shared
    detection pipeline (Steam appmanifest > Steam folder > GOG > Epic
    > Xbox > env var), and writes the resolved values as a batch
    script the caller can `call` to pick them up as `set` variables:

        GAME_PATH            - resolved install path (required)
        GAME_EXE             - leaf filename of the executable (for
                               the "game is running" tasklist check)
        GAME_EXE_RELPATH     - full relative path of the executable
                               (same as Executable in games.json)
        GAME_DISPLAY_NAME    - human-readable name (for messages)
        ENV_VAR_NAME         - env var that overrides detection

    If `-GivenPath` is supplied and points at an existing directory,
    it's used verbatim (trusting the caller - matches the step 1
    "trust %~1" semantics baked into the install.cmd templates).

.PARAMETER GameId
    Game id from games.json (hyphen-lowercase, e.g. "obra-dinn").

.PARAMETER OutFile
    Path to write the batch script to. install.cmd passes a %TEMP%-
    derived filename.

.PARAMETER GivenPath
    Optional caller-supplied path (install.cmd's %~1). If set and it
    is an existing directory, it is used verbatim instead of running
    detection.

.EXITCODE
    0 - game resolved, OutFile written
    1 - bad input (unknown game id, missing games.json, OutFile write failed)
    2 - game not installed (detection ran cleanly but nothing matched)
#>
param(
    [Parameter(Mandatory = $true)][string]$GameId,
    [Parameter(Mandatory = $true)][string]$OutFile,
    [string]$GivenPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# This script lives in two possible layouts:
#   1. Dev tree: cameraunlock-core/scripts/find-game.ps1 alongside a
#      sibling cameraunlock-core/powershell/GamePathDetection.psm1.
#   2. Release ZIP: <zip>/shared/find-game.ps1 alongside a sibling
#      <zip>/shared/GamePathDetection.psm1 (packaged by Copy-SharedBundle).
# Try layout 2 first since that's the hot path at user-install time.
$modulePath = Join-Path $PSScriptRoot 'GamePathDetection.psm1'
if (-not (Test-Path $modulePath)) {
    $modulePath = Join-Path $PSScriptRoot '..\powershell\GamePathDetection.psm1'
}
if (-not (Test-Path $modulePath)) {
    Write-Error "GamePathDetection.psm1 not found next to find-game.ps1 or in ../powershell/. Installer ZIP is corrupt or dev checkout is incomplete."
    exit 1
}
Import-Module $modulePath -Force

$cfg = Get-GameConfig -GameId $GameId
if (-not $cfg) {
    Write-Error "Unknown game id: $GameId. Add it to cameraunlock-core/data/games.json."
    exit 1
}

$exeRelPath = $cfg.Executable
$exeLeaf    = Split-Path $exeRelPath -Leaf
$displayName = if ($cfg.ContainsKey('DisplayName')) { $cfg.DisplayName } else { $GameId }
$envVarName  = if ($cfg.ContainsKey('EnvVar'))      { $cfg.EnvVar }      else { '' }

# Resolve the game path. Given path wins - the caller (launcher or
# knowledgeable user) has already decided where the game is.
$gamePath = $null
if ($GivenPath) {
    if (Test-Path -LiteralPath $GivenPath -PathType Container) {
        $gamePath = $GivenPath
    } else {
        Write-Error "Caller-supplied path does not exist or is not a directory: $GivenPath"
        exit 1
    }
} else {
    $gamePath = Find-GamePath -GameId $GameId
}

if (-not $gamePath) {
    $libraries = @(Find-SteamLibraries)
    $hint = if ($libraries.Count -gt 0) {
        "Searched Steam libraries: $($libraries -join '; ')"
    } else {
        "No Steam libraries detected on this machine."
    }
    Write-Error "Could not find $displayName installation. $hint"
    exit 2
}

# ASCII encoding for batch. Paths with extended chars (unusual on
# Windows game installs) would need UTF-8 but we write with ASCII
# to stay compatible with cmd.exe's legacy interpreter without BOM
# tricks. Real paths under Steam/GOG/Epic are ASCII in practice.
$lines = @(
    "set `"GAME_PATH=$gamePath`""
    "set `"GAME_EXE=$exeLeaf`""
    "set `"GAME_EXE_RELPATH=$exeRelPath`""
    "set `"GAME_DISPLAY_NAME=$displayName`""
    "set `"ENV_VAR_NAME=$envVarName`""
)
$lines -join "`r`n" | Out-File -Encoding ASCII -FilePath $OutFile -NoNewline
exit 0
