param([string]$BodiesRoot = $PSScriptRoot)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Join-Path (Split-Path $PSScriptRoot) ('.lab/shim-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null

function Assert-File([string]$Path, [string]$Expected) {
    if ([IO.File]::ReadAllText($Path) -ne $Expected) { throw "Unexpected contents: $Path" }
}

function Invoke-Installer([string]$Action, [int]$Expected, [string]$Flags = '/y') {
    $result = Join-Path $caseRoot ([guid]::NewGuid().ToString('N') + '.exit')
    $driver = $result + '.cmd'
    $lines = @('@echo off', ('call "{0}\{1}.cmd" "{2}" {3}' -f $caseRoot, $Action, $game, $Flags), ('> "{0}" echo %errorlevel%' -f $result))
    [IO.File]::WriteAllText($driver, ($lines -join "`r`n") + "`r`n")
    # A console is required to exercise cmd's actual pause and error paths.
    $process = Start-Process $env:ComSpec -ArgumentList @('/d', '/c', ('""{0}""' -f $driver)) -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(30000)) {
        Stop-Process -Id $process.Id -Force
        throw "Timed out: $Action $Flags in $caseRoot"
    }
    $actual = [int][IO.File]::ReadAllText($result).Trim()
    if ($actual -ne $Expected) { throw "$Action returned $actual, expected $Expected in $caseRoot" }
}

foreach ($kind in @('shim', 'shim-forwarder')) {
    $caseRoot = Join-Path $root $kind
    $game = Join-Path $caseRoot 'Game ! Folder'
    $shared = Join-Path $caseRoot 'shared'
    $plugins = Join-Path $caseRoot 'plugins'
    New-Item -ItemType Directory -Path $game, $shared, $plugins | Out-Null
    foreach ($name in @("install-body-$kind.cmd", 'uninstall-body.cmd', 'cecil-marker-check.ps1')) {
        Copy-Item -LiteralPath (Join-Path $BodiesRoot $name) -Destination $shared
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'find-game.ps1') -Destination $shared
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../powershell/GamePathDetection.psm1') -Destination $shared
    [IO.File]::WriteAllText((Join-Path $shared 'games.json'), '{"schema_version":1,"games":{"fixture":{"display_name":"Fixture","env_var":"CUL_FIXTURE_PATH","executable_relpath":"fixture.exe"}}}')
    [IO.File]::WriteAllText((Join-Path $game 'fixture.exe'), '')
    foreach ($action in @('install', 'uninstall')) {
        $body = if ($action -eq 'install') { "install-body-$kind.cmd" } else { 'uninstall-body.cmd' }
        $dlls = if ($action -eq 'uninstall' -and $kind -eq 'shim-forwarder') { 'shim.dll real.dll' } else { 'shim.dll' }
        $wrapper = @"
@echo off
setlocal disabledelayedexpansion
set "WRAPPER_DIR=%~dp0"
set "GAME_ID=fixture"
set "MOD_DISPLAY_NAME=Fixture"
set "MOD_INTERNAL_NAME=Fixture"
set "MOD_VERSION=1.0.0"
set "STATE_FILE=.fixture-state.json"
set "FRAMEWORK_TYPE=None"
set "SHIM_MARKER=FixtureMarker"
set "SHIM_MARKER_ALT=FixtureLauncherMarker"
set "MOD_DLLS=$dlls"
set "MOD_SEED_FILES=fixture.ini"
set "SYSTEM_DLL=version.dll"
set "SYSTEM_DLL_COPY=real.dll"
set "SYSTEM_DLL_ARCH=x64"
call "%~dp0shared\$body" %*
exit /b %errorlevel%
"@
        [IO.File]::WriteAllText((Join-Path $caseRoot "$action.cmd"), $wrapper.Replace("`r`n", "`n").Replace("`n", "`r`n"))
    }
    $payload = Join-Path $plugins 'shim.dll'
    $live = Join-Path $game 'shim.dll'
    $backup = $live + '.backup'
    $state = Join-Path $game '.fixture-state.json'
    $seed = Join-Path $game 'fixture.ini'
    [IO.File]::WriteAllText((Join-Path $plugins 'fixture.ini'), 'default')
    [IO.File]::WriteAllText($payload, 'wrong marker')
    Invoke-Installer install 1
    if (@(Get-ChildItem -LiteralPath $game).Count -ne 1) { throw 'Invalid payload mutated the game' }
    [IO.File]::WriteAllText($payload, 'FixtureMarker v1')
    Invoke-Installer install 0
    [IO.File]::WriteAllText($seed, 'custom')
    [IO.File]::WriteAllText($payload, 'FixtureMarker v2')
    Invoke-Installer install 0
    Assert-File $live 'FixtureMarker v2'
    Assert-File $seed 'custom'
    if (Test-Path -LiteralPath $backup) { throw 'Upgrade captured the mod as an original' }
    Invoke-Installer uninstall 0
    if (Test-Path -LiteralPath $live) { throw 'Uninstall left the shim' }
    [IO.File]::WriteAllText($live, 'foreign original')
    Invoke-Installer install 0
    Assert-File $backup 'foreign original'
    Invoke-Installer uninstall 0
    Assert-File $live 'foreign original'
    Invoke-Installer install 0
    [IO.File]::WriteAllText($backup, 'FixtureMarker old poisoned backup')
    Invoke-Installer uninstall 0
    if ((Test-Path -LiteralPath $live) -or (Test-Path -LiteralPath $backup)) { throw 'Poisoned backup survived' }
    $widePayload = [byte[]](@(255) + [Text.Encoding]::Unicode.GetBytes('FixtureLauncherMarker old launcher'))
    [IO.File]::WriteAllBytes($payload, $widePayload)
    Invoke-Installer install 0
    [IO.File]::WriteAllBytes($backup, $widePayload)
    Invoke-Installer uninstall 0
    if ((Test-Path -LiteralPath $live) -or (Test-Path -LiteralPath $backup)) { throw 'Wide companion backup survived' }
    [IO.File]::WriteAllText($payload, 'FixtureMarker v2')
    Invoke-Installer install 0
    [IO.File]::WriteAllText($backup, 'FixtureMarker old backup')
    $beforeState = [IO.File]::ReadAllText($state)
    foreach ($lock in @(@{ Path = $backup; Share = 'None' }, @{ Path = $backup; Share = 'Read' }, @{ Path = $live; Share = 'None' })) {
        $handle = [IO.File]::Open($lock.Path, 'Open', 'Read', $lock.Share)
        try { Invoke-Installer uninstall 1 } finally { $handle.Dispose() }
        Assert-File $live 'FixtureMarker v2'
        Assert-File $state $beforeState
        Assert-File $seed 'default'
        if ($kind -eq 'shim-forwarder' -and -not (Test-Path -LiteralPath (Join-Path $game 'real.dll'))) {
            throw 'Failed uninstall removed the forward target'
        }
    }
    Invoke-Installer uninstall 0
    Invoke-Installer install 2 '--unknown /y'
    Write-Host "PASS ${kind}: payload validation, upgrade, seeds, originals, poisoned backups, locked files, exit codes"
}
Write-Host "Fixtures retained at $root"
