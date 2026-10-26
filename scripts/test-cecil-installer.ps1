param([string]$BodiesRoot = $PSScriptRoot)

# Runs install-body-cecil.cmd through a real cmd.exe against synthetic game trees under a
# path holding `!`. The patch tool is a stand-in that appends the marker to the assembly, and
# a framework assembly stands in for Mono.Cecil.dll, so what is under test is the body's
# backup handling: the .original it keeps, and what it puts back before it patches.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Join-Path (Split-Path $PSScriptRoot) ('.lab/cecil-install-tests-' + [guid]::NewGuid().ToString('N'))
$marker = 'HeadTracking_Patched_Fixture_v1'
$managed = 'Fixture_Data\Managed'
$failures = New-Object System.Collections.Generic.List[string]

function Check([string]$Case, [bool]$Ok, [string]$What) {
    if (-not $Ok) { $failures.Add("${Case}: $What") }
}

function New-Case([string]$Name, [hashtable]$Files) {
    $caseRoot = Join-Path $root $Name
    $game = Join-Path $caseRoot 'Game ! Folder'
    $shared = Join-Path $caseRoot 'shared'
    New-Item -ItemType Directory -Path (Join-Path $game $managed), $shared, (Join-Path $caseRoot 'mod'), (Join-Path $caseRoot 'tools') | Out-Null
    foreach ($f in 'install-body-cecil.cmd', 'restore-kept-configs.ps1') {
        Copy-Item -LiteralPath (Join-Path $BodiesRoot $f) -Destination $shared
    }
    foreach ($f in 'find-game.ps1', 'cecil-marker-check.ps1') {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $f) -Destination $shared
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../powershell/GamePathDetection.psm1') -Destination $shared
    [IO.File]::WriteAllText((Join-Path $shared 'games.json'), '{"schema_version":1,"games":{"fixture":{"display_name":"Fixture","env_var":"CUL_FIXTURE_PATH","executable_relpath":"Fixture.exe"}}}')

    [IO.File]::WriteAllText((Join-Path $caseRoot 'mod\Fixture.dll'), 'mod dll')
    Copy-Item -LiteralPath ([Reflection.Assembly]::Load('System.Xml.Linq, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089').Location) -Destination (Join-Path $caseRoot 'mod\Mono.Cecil.dll')
    $patcher = @"
public static class BootstrapPatcher {
    public static bool PatchAssembly(string path) {
        System.IO.File.AppendAllText(path, " $marker");
        return true;
    }
}
"@
    [IO.File]::WriteAllText((Join-Path $caseRoot 'tools\BootstrapPatcher.cs'), $patcher)

    [IO.File]::WriteAllText((Join-Path $game 'Fixture.exe'), 'exe')
    foreach ($rel in $Files.Keys) { [IO.File]::WriteAllText((Join-Path $game $rel), $Files[$rel]) }

    $wrapper = @('@echo off', 'set "GAME_ID=fixture"', 'set "MOD_DISPLAY_NAME=Fixture"',
        'set "MOD_DLLS=Fixture.dll Mono.Cecil.dll"', 'set "MOD_INTERNAL_NAME=Fixture"', 'set "MOD_VERSION=1.0.0"',
        'set "STATE_FILE=.fixture-state.json"', 'set "FRAMEWORK_TYPE=MonoCecil"',
        ('set "MANAGED_SUBFOLDER={0}"' -f $managed), 'set "ASSEMBLY_DLL=Assembly-CSharp.dll"',
        'set "PATCHER_FILE=BootstrapPatcher.cs"', ('set "PATCH_MARKER={0}"' -f $marker), 'set "MOD_CONTROLS=Controls:"',
        'setlocal disabledelayedexpansion', 'set "WRAPPER_DIR=%~dp0"',
        'call "%~dp0shared\install-body-cecil.cmd" %*', 'exit /b %errorlevel%')
    [IO.File]::WriteAllText((Join-Path $caseRoot 'install.cmd'), ($wrapper -join "`r`n") + "`r`n")
    [pscustomobject]@{ Name = $Name; Root = $caseRoot; Game = $game }
}

function Invoke-Install($Case) {
    $out = Join-Path $Case.Root 'install.out'
    $exitFile = Join-Path $Case.Root 'install.exit'
    $driver = Join-Path $Case.Root 'drive.cmd'
    $lines = @('@echo off',
        ('call "{0}\install.cmd" "{1}" /y < nul > "{2}" 2>&1' -f $Case.Root, $Case.Game, $out),
        ('> "{0}" echo %errorlevel%' -f $exitFile))
    [IO.File]::WriteAllText($driver, ($lines -join "`r`n") + "`r`n")
    $process = Start-Process $env:ComSpec -ArgumentList @('/d', '/c', ('""{0}""' -f $driver)) -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(120000)) {
        Stop-Process -Id $process.Id -Force
        throw "$($Case.Name): install timed out"
    }
    [pscustomobject]@{ Exit = [int][IO.File]::ReadAllText($exitFile).Trim(); Output = [IO.File]::ReadAllText($out) }
}

function Text($Case, [string]$Rel) { [IO.File]::ReadAllText((Join-Path $Case.Game $Rel)) }

$assembly = "$managed\Assembly-CSharp.dll"
$backup = "$assembly.original"

try {
    # A first install backs up the pristine assembly and patches it.
    $c = New-Case 'first' @{ $assembly = 'game build 1' }
    $r = Invoke-Install $c
    Check $c.Name ($r.Exit -eq 0) "install exited $($r.Exit): $($r.Output)"
    Check $c.Name ((Text $c $backup) -eq 'game build 1') 'the backup is not the pristine assembly'
    Check $c.Name ((Text $c $assembly) -eq "game build 1 $marker") 'the assembly was not patched'

    # A reinstall puts the backup back before it patches, so the patch is applied once.
    $c = New-Case 'reinstall' @{ $assembly = "game build 1 $marker"; $backup = 'game build 1' }
    $r = Invoke-Install $c
    Check $c.Name ($r.Exit -eq 0) "install exited $($r.Exit): $($r.Output)"
    Check $c.Name ((Text $c $backup) -eq 'game build 1') 'the backup changed'
    Check $c.Name ((Text $c $assembly) -eq "game build 1 $marker") 'the assembly was not patched from the backup alone'

    # A game update left an unpatched assembly beside an older backup: the update is kept,
    # becomes the backup, and is what gets patched.
    $c = New-Case 'game-updated' @{ $assembly = 'game build 2'; $backup = 'game build 1' }
    $r = Invoke-Install $c
    Check $c.Name ($r.Exit -eq 0) "install exited $($r.Exit): $($r.Output)"
    Check $c.Name ((Text $c $backup) -eq 'game build 2') 'the backup was not refreshed from the update'
    Check $c.Name ((Text $c $assembly) -eq "game build 2 $marker") 'the older backup was restored over the update'

    # A backup that is itself patched is refused, and nothing changes.
    $c = New-Case 'corrupt-backup' @{ $assembly = "game build 1 $marker"; $backup = "game build 1 $marker" }
    $r = Invoke-Install $c
    Check $c.Name ($r.Exit -eq 1) "install exited $($r.Exit): $($r.Output)"
    Check $c.Name ((Text $c $assembly) -eq "game build 1 $marker") 'the assembly changed on a refused install'
} finally {
    if ($failures.Count -eq 0) { Remove-Item -LiteralPath $root -Recurse -Force }
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Host "FAIL $_" -ForegroundColor Red }
    Write-Host "Case folders kept under $root"
    exit 1
}
Write-Host 'cecil install: 4 cases passed' -ForegroundColor Green
