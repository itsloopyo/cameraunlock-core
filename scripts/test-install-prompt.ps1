param([string]$BodiesRoot = $PSScriptRoot)

# Covers the interactive game-folder prompt in find-game.ps1 and the wiring
# that reaches it from an install body: detection finds nothing, the caller did
# not pass /y, so the shim asks. Driven through a real cmd.exe with stdin on a
# file, because the prompt only exists on that path - everything the launcher
# does passes /y and must never reach it.
#
# The fixture game has no store keys in its games.json entry and its env var is
# left unset, which is exactly the shape of a game that arrived as a zip from
# itch.io: nothing on the machine to detect.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Join-Path (Split-Path $PSScriptRoot) ('.lab/prompt-tests-' + [guid]::NewGuid().ToString('N'))
$caseRoot = Join-Path $root 'asi'
$game = Join-Path $caseRoot 'Game ! Folder'
$decoy = Join-Path $caseRoot 'Not The Game'
$shared = Join-Path $caseRoot 'shared'
$plugins = Join-Path $caseRoot 'plugins'
$vendor = Join-Path $caseRoot 'vendor\ultimate-asi-loader'
New-Item -ItemType Directory -Path $game, $decoy, $shared, $plugins, $vendor | Out-Null

foreach ($name in @('install-body-asi.cmd', 'uninstall-body.cmd')) {
    Copy-Item -LiteralPath (Join-Path $BodiesRoot $name) -Destination $shared
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'find-game.ps1') -Destination $shared
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../powershell/GamePathDetection.psm1') -Destination $shared
[IO.File]::WriteAllText((Join-Path $shared 'games.json'), '{"schema_version":1,"games":{"fixture":{"display_name":"Fixture","env_var":"CUL_FIXTURE_PATH","executable_relpath":"fixture.exe"}}}')
[IO.File]::WriteAllText((Join-Path $game 'fixture.exe'), '')
[IO.File]::WriteAllText((Join-Path $plugins 'fixture.asi'), 'asi')
[IO.File]::WriteAllText((Join-Path $vendor 'dinput8.dll'), 'loader')

$wrapper = @"
@echo off
setlocal disabledelayedexpansion
set "WRAPPER_DIR=%~dp0"
set "GAME_ID=fixture"
set "MOD_DISPLAY_NAME=Fixture"
set "MOD_INTERNAL_NAME=Fixture"
set "MOD_VERSION=1.0.0"
set "STATE_FILE=.fixture-state.json"
set "FRAMEWORK_TYPE=ASILoader"
set "ASI_LOADER_NAME=winmm.dll"
set "ASI_SUBDIR="
set "MOD_SEED_FILES="
set "MOD_DLLS=fixture.asi"
call "%~dp0shared\install-body-asi.cmd" %*
"@
[IO.File]::WriteAllText((Join-Path $caseRoot 'install.cmd'), ($wrapper -replace "`r?`n", "`r`n"))

# CUL_FIXTURE_PATH is the fixture's only detection route, so nothing may leave
# it set - with it set the shim resolves the game and never prompts, and every
# case below would pass without exercising a line of the new code.
if ($env:CUL_FIXTURE_PATH) { throw 'CUL_FIXTURE_PATH is set; it would bypass the prompt under test' }

$caseIndex = 0
function Invoke-Install {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Typed,
        [Parameter(Mandatory = $true)][int]$Expected,
        [string]$Flags = '',
        [string]$WorkingDirectory = $PWD.ProviderPath)

    $script:caseIndex++
    $stem = Join-Path $caseRoot ('case{0:d2}' -f $script:caseIndex)
    $input = "$stem.in"
    $result = "$stem.exit"
    $driver = "$stem.cmd"
    $script:lastOutput = "$stem.out"

    # One trailing blank line beyond what the case types: the run ends on a
    # `pause` whenever /y is absent, and that read has to find something.
    [IO.File]::WriteAllText($input, (($Typed + @('')) -join "`r`n") + "`r`n")
    $lines = @(
        '@echo off',
        ('call "{0}\install.cmd" {1} < "{2}" > "{3}" 2>&1' -f $caseRoot, $Flags, $input, $script:lastOutput),
        ('> "{0}" echo %errorlevel%' -f $result))
    [IO.File]::WriteAllText($driver, ($lines -join "`r`n") + "`r`n")

    $process = Start-Process $env:ComSpec -ArgumentList @('/d', '/c', ('""{0}""' -f $driver)) -WorkingDirectory $WorkingDirectory -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(60000)) {
        Stop-Process -Id $process.Id -Force
        throw "Timed out with input [$($Typed -join '|')] flags [$Flags]"
    }
    $actual = [int][IO.File]::ReadAllText($result).Trim()
    if ($actual -ne $Expected) { throw "install returned $actual, expected $Expected with input [$($Typed -join '|')] flags [$Flags]" }
}

function Assert-OutputContains([string]$Expected) {
    $text = [IO.File]::ReadAllText($script:lastOutput)
    if (-not $text.Contains($Expected)) { throw "Expected the run to print '$Expected'. It printed:`n$text" }
}

function Reset-Game {
    foreach ($leaf in @('winmm.dll', 'fixture.asi', '.fixture-state.json')) {
        $path = Join-Path $game $leaf
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
}

function Assert-Deployed {
    foreach ($leaf in @('winmm.dll', 'fixture.asi', '.fixture-state.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $game $leaf))) { throw "Expected $leaf in the game folder" }
    }
}

function Assert-NotDeployed {
    foreach ($leaf in @('winmm.dll', 'fixture.asi', '.fixture-state.json')) {
        if (Test-Path -LiteralPath (Join-Path $game $leaf)) { throw "$leaf was deployed when the install should have stopped" }
    }
}

# A typed path installs, and does so into the folder that was typed.
Invoke-Install -Typed @($game) -Expected 0
Assert-Deployed
Reset-Game

# Quoted, as Explorer's "Copy as path" and a drag-and-drop both deliver it,
# with a trailing separator on top.
Invoke-Install -Typed @('"' + $game + '\"') -Expected 0
Assert-Deployed
Reset-Game

# A folder that is not the game is rejected and re-asked rather than accepted,
# so the mod cannot be deployed next to the wrong exe.
Invoke-Install -Typed @($decoy, $game) -Expected 0
Assert-Deployed
if (Test-Path -LiteralPath (Join-Path $decoy 'fixture.asi')) { throw 'Deployed into the rejected folder' }
Reset-Game

# A folder that does not exist at all, same treatment.
Invoke-Install -Typed @((Join-Path $caseRoot 'no such folder'), $game) -Expected 0
Assert-Deployed
Reset-Game

# A path relative to where the installer was run from, which is what a
# `cd` into the game's parent and a typed folder name produce.
Invoke-Install -Typed @('Game ! Folder') -Expected 0 -WorkingDirectory $caseRoot
Assert-Deployed
# Resolved to a full path before anything records it: the log line and the
# state file both outlive the working directory that gave the typed one
# meaning.
Assert-OutputContains "Game found: $game"
Reset-Game

# Blank cancels, and cancelling installs nothing.
Invoke-Install -Typed @() -Expected 1
Assert-NotDeployed

# /y is the launcher's call shape: no prompt, no stdin read, and it fails
# rather than hanging when detection comes up empty.
Invoke-Install -Typed @() -Expected 1 -Flags '/y'
Assert-NotDeployed

Write-Host 'PASS install prompt: typed path, quoted path, wrong folder, missing folder, relative path, cancel, /y non-interactive'
Write-Host "Fixtures retained at $root"
