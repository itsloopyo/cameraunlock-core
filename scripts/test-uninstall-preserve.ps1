param(
    [string]$BodiesRoot = $PSScriptRoot,
    # An uninstall-body.cmd from before PRESERVE_FILES existed. Given, every case
    # without the list also runs against it and must leave the same tree, exit
    # code and console output.
    [string]$ReferenceBody = '',
    # An install-body-reframework.cmd from before MOD_SEED_FILES existed. Given,
    # the REFramework install without the list also runs against it and must
    # leave the same tree and print the same output.
    [string]$ReferenceInstallBody = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Join-Path (Split-Path $PSScriptRoot) ('.lab/uninstall-preserve-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
$sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$holding = 'CameraUnlock-kept-configs'

Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
# Injected from a child process: attaching to the uninstall's console means
# detaching from our own first, and a harness run from a terminal would lose
# its output for the rest of the run.
$enterKey = @'
Add-Type @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class ConsoleEnter {
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool AttachConsole(uint pid);
    [DllImport("kernel32.dll")] static extern bool FreeConsole();
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr CreateFile(string name, uint access, uint share, IntPtr security, uint mode, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool WriteConsoleInputW(IntPtr handle, byte[] records, uint count, out uint written);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    public static void Enter(uint pid) {
        FreeConsole();
        if (!AttachConsole(pid)) throw new Win32Exception();
        IntPtr input = CreateFile("CONIN$", 0x40000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
        if (input == new IntPtr(-1)) throw new Win32Exception();
        try {
            byte[] key = new byte[20];
            key[0] = 1; key[4] = 1; key[8] = 1; key[10] = 13; key[14] = 13;
            uint written;
            if (!WriteConsoleInputW(input, key, 1, out written) || written != 1) throw new Win32Exception();
        } finally { CloseHandle(input); }
    }
}
"@
[ConsoleEnter]::Enter(PID)
'@

function Send-Enter([int]$ProcessId) {
    $script = $enterKey.Replace('PID', [string]$ProcessId)
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($script))
    $child = Start-Process powershell.exe -ArgumentList @('-NoProfile', '-NonInteractive', '-EncodedCommand', $encoded) -WindowStyle Hidden -Wait -PassThru
    if ($child.ExitCode -ne 0) { throw "could not send Enter to process $ProcessId" }
}

function Copy-Map([Collections.IDictionary]$Map, [Collections.IDictionary]$Extra = @{}) {
    $copy = [ordered]@{}
    foreach ($key in $Map.Keys) { $copy[$key] = $Map[$key] }
    foreach ($key in $Extra.Keys) { $copy[$key] = $Extra[$key] }
    $copy
}

$ue4ssZipEntries = @('dwmapi.dll', 'ue4ss/', 'ue4ss/UE4SS.dll', 'Mods/', 'Mods/BPModLoaderMod/', 'Mods/BPModLoaderMod/enabled.txt')

function New-Case {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][Collections.IDictionary]$Config,
        [Parameter(Mandatory)][Collections.IDictionary]$Files,
        [Parameter(Mandatory)][string]$ExeRelPath,
        [string]$InstalledByUs = 'true',
        [string]$Body = (Join-Path $BodiesRoot 'uninstall-body.cmd'),
        [string]$Under = $root
    )
    $caseRoot = Join-Path $Under $Name
    $game = Join-Path $caseRoot 'Game ! Folder'
    $shared = Join-Path $caseRoot 'shared'
    New-Item -ItemType Directory -Path $game, $shared | Out-Null
    Copy-Item -LiteralPath $Body -Destination (Join-Path $shared 'uninstall-body.cmd')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'cecil-marker-check.ps1') -Destination $shared
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'find-game.ps1') -Destination $shared
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../powershell/GamePathDetection.psm1') -Destination $shared
    $gamesJson = @{ schema_version = 1; games = @{ fixture = @{ display_name = 'Fixture'; env_var = 'CUL_FIXTURE_PATH'; executable_relpath = $ExeRelPath } } }
    [IO.File]::WriteAllText((Join-Path $shared 'games.json'), ($gamesJson | ConvertTo-Json -Depth 5))
    if ($Config['FRAMEWORK_TYPE'] -eq 'UE4SS') {
        $vendor = Join-Path $caseRoot 'vendor\ue4ss'
        New-Item -ItemType Directory -Path $vendor | Out-Null
        $zip = [IO.Compression.ZipFile]::Open((Join-Path $vendor 'UE4SS.zip'), 'Create')
        try {
            foreach ($entryName in $ue4ssZipEntries) {
                $entry = $zip.CreateEntry($entryName)
                if (-not $entryName.EndsWith('/')) {
                    $writer = New-Object IO.StreamWriter($entry.Open())
                    try { $writer.Write($entryName) } finally { $writer.Dispose() }
                }
            }
        } finally { $zip.Dispose() }
    }
    $allFiles = [ordered]@{ $ExeRelPath = $ExeRelPath }
    foreach ($key in $Files.Keys) { $allFiles[$key] = $Files[$key] }
    foreach ($rel in $allFiles.Keys) {
        $path = Join-Path $game $rel
        New-Item -ItemType Directory -Force -Path (Split-Path $path) | Out-Null
        [IO.File]::WriteAllText($path, $allFiles[$rel])
    }
    if ($InstalledByUs) {
        [IO.File]::WriteAllText((Join-Path $game '.fixture-state.json'), "{`r`n  ""mod"": ""Fixture"",`r`n  ""installed_by_us"": $InstalledByUs`r`n}`r`n")
    }
    $lines = @('@echo off', 'setlocal disabledelayedexpansion', 'set "WRAPPER_DIR=%~dp0"',
        'set "GAME_ID=fixture"', 'set "MOD_DISPLAY_NAME=Fixture"', 'set "MOD_INTERNAL_NAME=Fixture"',
        'set "STATE_FILE=.fixture-state.json"')
    foreach ($key in $Config.Keys) { $lines += ('set "{0}={1}"' -f $key, $Config[$key]) }
    $lines += @('call "%~dp0shared\uninstall-body.cmd" %*', 'exit /b %errorlevel%')
    [IO.File]::WriteAllText((Join-Path $caseRoot 'uninstall.cmd'), ($lines -join "`r`n") + "`r`n")
    [pscustomobject]@{ Name = $Name; Root = $caseRoot; Game = $game; Runs = 0 }
}

# Each run gets its own cmd.exe and console, hidden, as the launcher's own
# child would; the pause-on-failure path only exists with one attached.
function Start-Run($Case, [string]$Flags, [string]$Script = 'uninstall') {
    $Case.Runs++
    $stem = Join-Path $Case.Root ('run{0}' -f $Case.Runs)
    $driver = "$stem.cmd"
    $lines = @('@echo off',
        ('call "{0}\{1}.cmd" "{2}" {3} > "{4}.out" 2>&1' -f $Case.Root, $Script, $Case.Game, $Flags, $stem),
        ('> "{0}.exit" echo %errorlevel%' -f $stem))
    [IO.File]::WriteAllText($driver, ($lines -join "`r`n") + "`r`n")
    $process = Start-Process $env:ComSpec -ArgumentList @('/d', '/c', ('""{0}""' -f $driver)) -WindowStyle Hidden -PassThru
    [pscustomobject]@{ Process = $process; Stem = $stem }
}

function Complete-Run($Run, $Case, [int]$Expected) {
    if (-not $Run.Process.WaitForExit(60000)) {
        Stop-Process -Id $Run.Process.Id -Force
        throw "$($Case.Name): timed out"
    }
    $actual = [int][IO.File]::ReadAllText("$($Run.Stem).exit").Trim()
    $output = [IO.File]::ReadAllText("$($Run.Stem).out")
    if ($actual -ne $Expected) { throw "$($Case.Name): exit $actual, expected $Expected`n$output" }
    $output
}

function Invoke-Uninstall($Case, [int]$Expected, [string]$Flags = '/y') {
    Complete-Run (Start-Run $Case $Flags) $Case $Expected
}

function Invoke-Install($Case, [int]$Expected, [string]$Flags = '/y') {
    Complete-Run (Start-Run $Case $Flags 'install') $Case $Expected
}

function Get-Snapshot([string]$Dir) {
    $items = @(Get-ChildItem -LiteralPath $Dir -Recurse -Force)
    @($items | Sort-Object FullName | ForEach-Object {
        $rel = $_.FullName.Substring($Dir.Length + 1)
        if ($_.PSIsContainer) { "$rel\" } else { "$rel=" + [IO.File]::ReadAllText($_.FullName) }
    })
}

function Assert-Files($Case, [string[]]$Remaining, [Collections.IDictionary]$Contents = @{}) {
    $expected = @($Remaining | ForEach-Object {
        $content = if ($Contents.Contains($_)) { $Contents[$_] } else { $_ }
        "$_=$content"
    } | Sort-Object)
    $actual = @(Get-Snapshot $Case.Game | Where-Object { -not $_.EndsWith('\') } | Sort-Object)
    $diff = Compare-Object -CaseSensitive $expected $actual
    if ($diff) { throw "$($Case.Name): unexpected files`n$($diff | Out-String)" }
}

function Assert-Output($Case, [string]$Output, [string[]]$Lines) {
    foreach ($line in $Lines) {
        if (-not ($Output -split "`r?`n" | Where-Object { $_.Trim() -eq $line })) {
            throw "$($Case.Name): output lacks '$line'`n$Output"
        }
    }
}

function Assert-KeptLines($Case, [string]$Output, [string[]]$Expected) {
    $actual = @($Output -split "`r?`n" | ForEach-Object { $_.Trim() } | Where-Object { $_.StartsWith('Kept: ') } | Sort-Object)
    $diff = Compare-Object -CaseSensitive @($Expected | ForEach-Object { "Kept: $_" } | Sort-Object) $actual
    if ($diff) { throw "$($Case.Name): unexpected Kept lines`n$($diff | Out-String)`n$Output" }
}

function Assert-NoHolding($Case) {
    if (Test-Path -LiteralPath (Join-Path $Case.Game $holding)) { throw "$($Case.Name): holding folder left behind" }
}

# A case without PRESERVE_FILES, checked against its expected tree and, when a
# reference body is given, against what that body does with the same fixture.
function Invoke-Unlisted {
    param($Name, $Config, $Files, $ExeRelPath, $InstalledByUs, [string]$Flags, [string[]]$Remaining, [Collections.IDictionary]$Contents = @{})
    $case = New-Case -Name $Name -Config $Config -Files $Files -ExeRelPath $ExeRelPath -InstalledByUs $InstalledByUs
    $output = Invoke-Uninstall $case 0 $Flags
    Assert-Files $case $Remaining $Contents
    if ($ReferenceBody) {
        $reference = New-Case -Name $Name -Config $Config -Files $Files -ExeRelPath $ExeRelPath -InstalledByUs $InstalledByUs -Body $ReferenceBody -Under (Join-Path $root 'reference')
        $referenceOutput = Invoke-Uninstall $reference 0 $Flags
        $diff = Compare-Object -CaseSensitive (Get-Snapshot $reference.Game) (Get-Snapshot $case.Game)
        if ($diff) { throw "${Name}: tree differs from the reference body`n$($diff | Out-String)" }
        if ($output.Replace($case.Root, '<case>') -cne $referenceOutput.Replace($reference.Root, '<case>')) {
            throw "${Name}: output differs from the reference body`n--- this body`n$output`n--- reference`n$referenceOutput"
        }
    }
}

function Set-Deny([string]$Dir, [switch]$Remove, [string]$Right = 'AD') {
    $grant = if ($Remove) { @('/remove:d', "*$sid") } else { @('/deny', "*${sid}:($Right)") }
    & icacls.exe $Dir @grant | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "icacls failed on $Dir" }
}

# ---------------------------------------------------------------- ASI loader
$asiConfig = [ordered]@{
    FRAMEWORK_TYPE = 'ASILoader'; ASI_LOADER_NAME = 'winmm.dll'
    MOD_DLLS = 'Fixture.asi HeadTracking.ini'; MOD_SEED_FILES = 'Seed.ini'
    MOD_LEFTOVERS = 'Leftover.ini Fixture.log'
    ROOT_EXTRAS = 'Root.ini Root.log'
}
$asiFiles = [ordered]@{}
foreach ($rel in @('bin\winmm.dll', 'bin\Fixture.asi', 'bin\HeadTracking.ini', 'bin\Seed.ini', 'bin\Leftover.ini',
        'bin\Fixture.log', 'Root.ini', 'Root.log', 'bin\user.txt')) { $asiFiles[$rel] = $rel }
Invoke-Unlisted -Name 'asi-unlisted' -Config $asiConfig -Files $asiFiles -ExeRelPath 'bin\fixture.exe' -InstalledByUs 'true' -Flags '/y' `
    -Remaining @('bin\fixture.exe', 'bin\user.txt')

# BIN\headtracking.INI is spelled in another case than the file on disk, and
# bin\GONE.INI names a file that is not there.
$listed = Copy-Map $asiConfig @{ PRESERVE_FILES = 'BIN\headtracking.INI bin\Seed.ini "bin\Leftover.ini" Root.ini bin\GONE.INI' }
$case = New-Case -Name 'asi-listed' -Config $listed -Files $asiFiles -ExeRelPath 'bin\fixture.exe'
$output = Invoke-Uninstall $case 0
Assert-Files $case @('bin\fixture.exe', 'bin\user.txt', 'bin\HeadTracking.ini', 'bin\Seed.ini', 'bin\Leftover.ini', 'Root.ini')
Assert-KeptLines $case $output @('HeadTracking.ini', 'Seed.ini', 'Leftover.ini', 'Root.ini')
Assert-Output $case $output @('Removed: Fixture.log', 'Removed: winmm.dll', 'Removed: state file', '=== Uninstall Complete ===')
Assert-NoHolding $case
Write-Host 'PASS asi: unlisted removes as before; MOD_DLLS, MOD_SEED_FILES, MOD_LEFTOVERS and ROOT_EXTRAS entries kept, case-insensitively'

# ---------------------------------------------------------------- MonoCecil
$cecilConfig = [ordered]@{
    FRAMEWORK_TYPE = 'MonoCecil'; MANAGED_SUBFOLDER = 'Eternal Afternoon_Data\Managed'; ASSEMBLY_DLL = 'Assembly-CSharp.dll'
    PATCH_MARKER = 'FixturePatched'; MOD_DLLS = 'HeadTracking.dll'
    MANAGED_EXTRAS = 'HeadTracking.cfg HeadTracking.log'
}
$m = 'Eternal Afternoon_Data\Managed'
$cecilFiles = [ordered]@{
    "$m\Assembly-CSharp.dll" = 'FixturePatched build'; "$m\Assembly-CSharp.dll.original" = 'clean build'
    "$m\HeadTracking.dll" = "$m\HeadTracking.dll"; "$m\HeadTracking.cfg" = "$m\HeadTracking.cfg"
    "$m\HeadTracking.log" = "$m\HeadTracking.log"
}
$restored = @{ "$m\Assembly-CSharp.dll" = 'clean build' }
Invoke-Unlisted -Name 'cecil-unlisted' -Config $cecilConfig -Files $cecilFiles -ExeRelPath 'fixture.exe' -InstalledByUs 'true' -Flags '/y' `
    -Remaining @('fixture.exe', "$m\Assembly-CSharp.dll") -Contents $restored
$listed = Copy-Map $cecilConfig @{ PRESERVE_FILES = """$m\HeadTracking.cfg""" }
$case = New-Case -Name 'cecil-listed' -Config $listed -Files $cecilFiles -ExeRelPath 'fixture.exe'
$output = Invoke-Uninstall $case 0
Assert-Files $case @('fixture.exe', "$m\Assembly-CSharp.dll", "$m\HeadTracking.cfg") $restored
Assert-KeptLines $case $output @('HeadTracking.cfg')
Assert-Output $case $output @('Removed: HeadTracking.log', '=== Uninstall Complete ===')
Write-Host 'PASS cecil: MANAGED_EXTRAS entry quoted with a space kept'

# ---------------------------------------------------------------- loader trees
$bepConfig = 'BepInEx\config\CameraUnlock.ini'
$bepLegacy = 'BepInEx\config\com.cameraunlock.fixture.headtracking.cfg'
$trees = @(
    @{
        Kind = 'bepinex'; Exe = 'fixture.exe'
        Config = [ordered]@{ FRAMEWORK_TYPE = 'BepInEx'; MOD_DLLS = 'Fixture.dll' }
        Files = @('winhttp.dll', 'doorstop_config.ini', '.doorstop_version', 'BepInEx\core\BepInEx.dll', 'BepInEx\plugins\Fixture.dll',
            'BepInEx\plugins\Other.dll', 'BepInEx\config\BepInEx.cfg', 'BepInEx\config\other.cfg', $bepConfig, $bepLegacy, 'user.txt')
        Preserve = "$bepConfig $bepLegacy"
        Kept = @($bepConfig, $bepLegacy)
        ModFiles = @('BepInEx\plugins\Fixture.dll')
        LoaderKept = @('user.txt')
        DelOneKept = @()
        Contents = @{}
    },
    @{
        Kind = 'reframework'; Exe = 'fixture.exe'
        Config = [ordered]@{ FRAMEWORK_TYPE = 'REFramework'; MOD_DLLS = 'HeadTracking.dll HeadTracking.ini' }
        Files = @('dinput8.dll', 'reframework_revision.txt', 'reframework\plugins\HeadTracking.dll', 'reframework\plugins\HeadTracking.ini',
            'reframework\autorun\other.lua', 'user.txt')
        Preserve = 'reframework\plugins\HeadTracking.ini'
        Kept = @('reframework\plugins\HeadTracking.ini')
        ModFiles = @('reframework\plugins\HeadTracking.dll', 'reframework\plugins\HeadTracking.ini')
        LoaderKept = @('user.txt')
        DelOneKept = @('HeadTracking.ini')
        Contents = @{}
    },
    @{
        Kind = 'ue4ss'; Exe = 'Game\Binaries\Win64\fixture.exe'
        Config = [ordered]@{ FRAMEWORK_TYPE = 'UE4SS'; MOD_INTERNAL_NAME = 'HeadTracking'; UE4_BINARIES_RELDIR = 'Game\Binaries\Win64'; MOD_DLLS = '' }
        Files = @('Game\Binaries\Win64\dwmapi.dll', 'Game\Binaries\Win64\ue4ss\UE4SS.dll', 'Game\Binaries\Win64\Mods\BPModLoaderMod\enabled.txt',
            'Game\Binaries\Win64\Mods\mods.txt', 'Game\Binaries\Win64\Mods\HeadTracking\Scripts\main.lua',
            'Game\Binaries\Win64\Mods\HeadTracking\HeadTracking.ini', 'user.txt')
        Preserve = 'Game\Binaries\Win64\Mods\HeadTracking\HeadTracking.ini'
        Kept = @('Game\Binaries\Win64\Mods\HeadTracking\HeadTracking.ini')
        ModFiles = @('Game\Binaries\Win64\Mods\HeadTracking\Scripts\main.lua', 'Game\Binaries\Win64\Mods\HeadTracking\HeadTracking.ini')
        LoaderKept = @('user.txt', 'Game\Binaries\Win64\Mods\mods.txt')
        DelOneKept = @()
        Contents = @{ 'Game\Binaries\Win64\Mods\mods.txt' = "BPModLoaderMod : 1`r`n" }
        Initial = @{ 'Game\Binaries\Win64\Mods\mods.txt' = "BPModLoaderMod : 1`r`nHeadTracking : 1`r`n" }
    }
)

foreach ($tree in $trees) {
    $files = [ordered]@{}
    foreach ($rel in $tree.Files + 'Outside.ini') { $files[$rel] = $rel }
    if ($tree.Contains('Initial')) { foreach ($rel in $tree.Initial.Keys) { $files[$rel] = $tree.Initial[$rel] } }
    $all = @($tree.Exe, 'Outside.ini') + $tree.Files
    $loaderGone = @($tree.Exe, 'Outside.ini') + $tree.LoaderKept
    $loaderStays = @($all | Where-Object { $tree.ModFiles -notcontains $_ })
    foreach ($run in @(@{ By = 'true'; Flags = '/y' }, @{ By = 'false'; Flags = '/y' }, @{ By = 'false'; Flags = '/force /y' })) {
        $removesLoader = $run.By -eq 'true' -or $run.Flags -like '*/force*'
        $label = '{0}-{1}{2}' -f $tree.Kind, $run.By, $(if ($run.Flags -like '*/force*') { '-force' } else { '' })
        $remaining = if ($removesLoader) { $loaderGone } else { $loaderStays }
        Invoke-Unlisted -Name "$label-unlisted" -Config $tree.Config -Files $files -ExeRelPath $tree.Exe -InstalledByUs $run.By -Flags $run.Flags `
            -Remaining $remaining -Contents $tree.Contents

        # Outside.ini is listed but outside every tree removed, so no removal
        # may report it: it is not moved, and nothing names it for deletion.
        $listed = Copy-Map $tree.Config @{ PRESERVE_FILES = "$($tree.Preserve) Outside.ini" }
        $case = New-Case -Name "$label-listed" -Config $listed -Files $files -ExeRelPath $tree.Exe -InstalledByUs $run.By
        $output = Invoke-Uninstall $case 0 $run.Flags
        $remaining = if ($removesLoader) { $loaderGone + $tree.Kept } else { @($loaderStays + $tree.Kept | Select-Object -Unique) }
        Assert-Files $case $remaining $tree.Contents
        $treeKept = if ($removesLoader -or $tree.Kind -eq 'ue4ss') { $tree.Kept } else { @() }
        Assert-KeptLines $case $output (@($tree.DelOneKept) + $treeKept)
        Assert-Output $case $output @('=== Uninstall Complete ===')
        Assert-NoHolding $case
    }
    Write-Host "PASS $($tree.Kind): installed_by_us true, false and /force, with and without the list, listed files kept through the tree removal"
}

# ---------------------------------------------------------------- failures
$bepFiles = [ordered]@{}
foreach ($rel in $trees[0].Files) { $bepFiles[$rel] = $rel }
$bepListed = Copy-Map $trees[0].Config @{ PRESERVE_FILES = $trees[0].Preserve }

$case = New-Case -Name 'leftover-holding' -Config $bepListed -Files (Copy-Map $bepFiles @{ "$holding\$bepConfig" = 'set aside earlier' }) -ExeRelPath 'fixture.exe'
$before = Get-Snapshot $case.Game
$output = Invoke-Uninstall $case 1
if (Compare-Object -CaseSensitive $before (Get-Snapshot $case.Game)) { throw 'leftover-holding: the game folder changed' }
if ($output -notmatch [regex]::Escape("$holding is left over")) { throw "leftover-holding: output does not name the folder`n$output" }
Write-Host 'PASS leftover holding folder: refused with exit 1 before anything was touched'

$run = Start-Run $case ''
Start-Sleep -Seconds 3
if ($run.Process.HasExited) { throw 'pause: a failed uninstall without /y did not wait at the console' }
Send-Enter $run.Process.Id
$output = Complete-Run $run $case 1
if ($output -notmatch 'Press any key') { throw "pause: no pause prompt`n$output" }
if (Compare-Object -CaseSensitive $before (Get-Snapshot $case.Game)) { throw 'pause: the game folder changed' }
Write-Host 'PASS pause: a failed run without /y waits on the console, Enter releases it, exit code stays 1'

$case = New-Case -Name 'set-aside-fails' -Config $bepListed -Files $bepFiles -ExeRelPath 'fixture.exe'
$before = Get-Snapshot $case.Game
Set-Deny $case.Game
try { $output = Invoke-Uninstall $case 1 } finally { Set-Deny $case.Game -Remove }
Assert-Output $case $output @("ERROR: could not set $bepConfig aside, so the folder holding it is left in place.", '=== Uninstall Incomplete ===')
$after = @(Get-Snapshot $case.Game)
foreach ($entry in $before) {
    if ($entry -like 'BepInEx\*' -and $entry -notlike 'BepInEx\plugins\Fixture.dll*' -and $after -notcontains $entry) { throw "set-aside-fails: lost $entry" }
}
if (-not (Test-Path -LiteralPath (Join-Path $case.Game '.fixture-state.json'))) { throw 'set-aside-fails: state file gone' }
Assert-NoHolding $case
Invoke-Uninstall $case 0 | Out-Null
Assert-Files $case (@('fixture.exe', 'user.txt') + $trees[0].Kept)
Write-Host 'PASS set-aside failure: tree left whole, config in place, state kept, retry completes'

$ue = $trees[2]
$ueFiles = [ordered]@{}
foreach ($rel in $ue.Files) { $ueFiles[$rel] = $rel }
foreach ($rel in $ue.Initial.Keys) { $ueFiles[$rel] = $ue.Initial[$rel] }
$ueListed = Copy-Map $ue.Config @{ PRESERVE_FILES = $ue.Preserve }
$case = New-Case -Name 'move-back-fails' -Config $ueListed -Files $ueFiles -ExeRelPath $ue.Exe -InstalledByUs 'false'
$mods = Join-Path $case.Game 'Game\Binaries\Win64\Mods'
Set-Deny $mods
try {
    $output = Invoke-Uninstall $case 1
    Assert-Output $case $output @('Removed: Mods\HeadTracking\', "ERROR: could not move $($ue.Preserve) back into place.", '=== Uninstall Incomplete ===')
    if ($output -notmatch [regex]::Escape("Those files are still in $($case.Game)\$holding.")) { throw "move-back-fails: holding folder not named`n$output" }
    foreach ($rel in $ue.Kept) {
        if ([IO.File]::ReadAllText((Join-Path $case.Game "$holding\$rel")) -ne $rel) { throw "move-back-fails: $rel not in the holding folder" }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $case.Game '.fixture-state.json'))) { throw 'move-back-fails: state file gone' }
    $before = Get-Snapshot $case.Game
    $output = Invoke-Uninstall $case 1
    if (Compare-Object -CaseSensitive $before (Get-Snapshot $case.Game)) { throw 'move-back-fails: the rerun touched the game folder' }
} finally { Set-Deny $mods -Remove }
New-Item -ItemType Directory -Path (Join-Path $mods 'HeadTracking') | Out-Null
foreach ($rel in $ue.Kept) { Move-Item -LiteralPath (Join-Path $case.Game "$holding\$rel") -Destination (Join-Path $case.Game $rel) }
Remove-Item -LiteralPath (Join-Path $case.Game $holding) -Recurse
Invoke-Uninstall $case 0 | Out-Null
Assert-Files $case (@($ue.Exe) + @($ue.Files | Where-Object { $ue.ModFiles -notcontains $_ }) + $ue.Kept) $ue.Contents
Write-Host 'PASS move-back failure: files left in the named holding folder, state kept, rerun refused, retry after a manual restore completes'

# ---------------------------------------------------------------- arguments
foreach ($bad in @('*.ini', 'bin\?.ini', '..\up.ini', 'C:\abs.ini', 'D:rel.ini', '\lead.ini', 'bin\ok.ini \lead.ini', 'bin/fwd.ini',
        'bang!.ini', 'paren(1).ini', 'close).ini', '""', 'bin\ok.ini ""', 'bin\', 'bin\ok.ini "bin\"', 'a&b.ini', 'c^d.ini',
        'a<b.ini', 'a>b.ini', 'a|b.ini')) {
    $config = Copy-Map $asiConfig @{ PRESERVE_FILES = $bad }
    $case = New-Case -Name ('unsafe-' + [guid]::NewGuid().ToString('N').Substring(0, 8)) -Config $config -Files $asiFiles -ExeRelPath 'bin\fixture.exe'
    $before = Get-Snapshot $case.Game
    $output = Invoke-Uninstall $case 1
    if ($output -notmatch 'PRESERVE_FILES in the uninstall.cmd CONFIG BLOCK contains') { throw "unsafe '$bad': refusal does not name PRESERVE_FILES`n$output" }
    if (Compare-Object -CaseSensitive $before (Get-Snapshot $case.Game)) { throw "unsafe '$bad': the game folder changed" }
}
Write-Host 'PASS unsafe PRESERVE_FILES entries: exit 1, nothing touched'

# Only files are set aside, so a listed folder inside a loader folder would go
# with it.
$folderCases = @(
    @{ Name = 'folder-entry'; Config = Copy-Map $trees[0].Config @{ PRESERVE_FILES = 'BepInEx\config' }; Files = $bepFiles; Exe = 'fixture.exe'; Named = 'BepInEx\config' }
)
foreach ($folder in $folderCases) {
    $case = New-Case -Name $folder.Name -Config $folder.Config -Files $folder.Files -ExeRelPath $folder.Exe
    $before = Get-Snapshot $case.Game
    $output = Invoke-Uninstall $case 1
    Assert-Output $case $output @('ERROR: PRESERVE_FILES in the uninstall.cmd CONFIG BLOCK names a folder:', $folder.Named)
    if (Compare-Object -CaseSensitive $before (Get-Snapshot $case.Game)) { throw "$($folder.Name): the game folder changed" }
}
Write-Host 'PASS folder entries: a listed path that is a folder is refused with exit 1, nothing touched'

$case = New-Case -Name 'unknown-flag' -Config $bepListed -Files $bepFiles -ExeRelPath 'fixture.exe'
$before = Get-Snapshot $case.Game
Invoke-Uninstall $case 2 '--bogus /y' | Out-Null
if (Compare-Object -CaseSensitive $before (Get-Snapshot $case.Game)) { throw 'unknown-flag: the game folder changed' }
Invoke-Uninstall $case 0 '-y' | Out-Null
Assert-Files $case (@('fixture.exe', 'user.txt') + $trees[0].Kept)
Write-Host 'PASS arguments: unknown flag exit 2, -y completes'

# ---------------------------------------------------------------- wrapper templates
# A template filled in for the fixture, with the named CONFIG BLOCK lines left
# out to stand for a wrapper that never had them.
function Write-Template([string]$Template, [string[][]]$Pairs, [string]$Dest, [string[]]$Drop = @()) {
    $text = [IO.File]::ReadAllText((Join-Path $PSScriptRoot "templates\$Template"))
    foreach ($pair in $Pairs) {
        if (-not $text.Contains($pair[0])) { throw "${Template}: template no longer contains $($pair[0])" }
        $text = $text.Replace($pair[0], $pair[1])
    }
    foreach ($name in $Drop) {
        $line = "set `"$name=`"`r`n"
        if (-not $text.Contains($line)) { throw "${Template}: template no longer sets $name blank" }
        $text = $text.Replace($line, '')
    }
    [IO.File]::WriteAllText($Dest, $text)
}

# Every name the template's CONFIG BLOCK sets blank, each given the value another
# mod's wrapper could have left in the console. $Values overrides the default.
function Get-InheritedConsole([string]$Template, [Collections.IDictionary]$Values) {
    $console = [ordered]@{}
    $inBlock = $false
    foreach ($line in [IO.File]::ReadAllLines((Join-Path $PSScriptRoot "templates\$Template"))) {
        if ($line.Contains('END CONFIG BLOCK')) { break }
        if ($line.Contains('--- CONFIG BLOCK ---')) { $inBlock = $true; continue }
        if ($inBlock -and $line -match '^set "([A-Z0-9_]+)="$') {
            $console[$Matches[1]] = if ($Values.Contains($Matches[1])) { $Values[$Matches[1]] } else { 'OtherMod.ini' }
        }
    }
    if ($console.Count -eq 0) { throw "${Template}: no blank CONFIG BLOCK names found" }
    foreach ($name in $Values.Keys) { if (-not $console.Contains($name)) { throw "${Template}: no longer sets $name blank" } }
    $console
}

function Invoke-InConsole([Collections.IDictionary]$Console, [scriptblock]$Run) {
    foreach ($name in $Console.Keys) { Set-Item "Env:$name" $Console[$name] }
    try { & $Run } finally { foreach ($name in $Console.Keys) { Remove-Item "Env:$name" } }
}

# The uninstall wrapper template itself, from a folder holding '!', run from a
# console where another mod's wrappers already set every name its CONFIG BLOCK
# sets blank: ASI_SUBDIR to a folder of that mod's, PRESERVE_FILES to a file this
# mod installs, MOD_LEFTOVERS to the player's own file. The blank lines win, so
# the uninstall works in bin\ and removes exactly what it would from a clean
# console. The same wrapper without its ASI_SUBDIR line, which is how 91 fleet
# wrappers shipped, goes to the other mod's folder and leaves this mod installed.
$uninstallConsole = Get-InheritedConsole 'uninstall-wrapper.cmd' ([ordered]@{
        ASI_SUBDIR = 'OtherMod'; PRESERVE_FILES = 'bin\HeadTracking.ini'; MOD_LEFTOVERS = 'user.txt' })
$uninstallPairs = @(@('<games.json id>', 'fixture'), @('<Game Name> Head Tracking', 'Fixture'), @('<Mod>HeadTracking.dll', 'Fixture.asi HeadTracking.ini'),
    @('<Mod>HeadTracking', 'Fixture'), @('.headtracking-state.json', '.fixture-state.json'), @('set "FRAMEWORK_TYPE=None"', 'set "FRAMEWORK_TYPE=ASILoader"'))
$templateFiles = [ordered]@{}
foreach ($rel in @('bin\winmm.dll', 'bin\Fixture.asi', 'bin\HeadTracking.ini', 'bin\user.txt', 'bin\OtherMod\OtherMod.asi')) { $templateFiles[$rel] = $rel }

$case = New-Case -Name 'uninstall-template' -Config ([ordered]@{}) -Files $templateFiles -ExeRelPath 'bin\fixture.exe' -Under (Join-Path $root 'Wrapper ! Folder')
Write-Template 'uninstall-wrapper.cmd' $uninstallPairs (Join-Path $case.Root 'uninstall.cmd')
$output = Invoke-InConsole $uninstallConsole { Invoke-Uninstall $case 0 }
Assert-Files $case @('bin\fixture.exe', 'bin\user.txt', 'bin\OtherMod\OtherMod.asi')
if ($output -match '(?m)^\s*Kept: ') { throw "uninstall-template: the inherited PRESERVE_FILES reached the body`n$output" }
Assert-Output $case $output @('Removed: winmm.dll', 'Removed: Fixture.asi', 'Removed: HeadTracking.ini', '=== Uninstall Complete ===')

$case = New-Case -Name 'uninstall-template-no-asi-subdir' -Config ([ordered]@{}) -Files $templateFiles -ExeRelPath 'bin\fixture.exe' -Under (Join-Path $root 'Wrapper ! Folder')
Write-Template 'uninstall-wrapper.cmd' $uninstallPairs (Join-Path $case.Root 'uninstall.cmd') -Drop 'ASI_SUBDIR'
Invoke-InConsole $uninstallConsole { Invoke-Uninstall $case 0 } | Out-Null
foreach ($rel in @('bin\winmm.dll', 'bin\Fixture.asi')) {
    if (-not (Test-Path -LiteralPath (Join-Path $case.Game $rel))) { throw "uninstall-template-no-asi-subdir: $rel removed, so the harness no longer shows an inherited ASI_SUBDIR" }
}
Write-Host 'PASS uninstall wrapper template: every blank CONFIG BLOCK name another wrapper left in the console is cleared; without the ASI_SUBDIR line the uninstall misses bin\'

# ---------------------------------------------------------------- REFramework install
# The package folder holds a '!' as well as the game folder: install.cmd reads
# its payload from its own folder, so both paths go through the body.
$reZipEntries = @('dinput8.dll', 'reframework_revision.txt', 'reframework/', 'reframework/autorun/', 'reframework/autorun/loader.lua')
$reLoader = [ordered]@{}
foreach ($entryName in $reZipEntries) { if (-not $entryName.EndsWith('/')) { $reLoader[$entryName.Replace('/', '\')] = $entryName } }

function New-InstallCase {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][Collections.IDictionary]$Install,
        [Parameter(Mandatory)][Collections.IDictionary]$Uninstall,
        [Parameter(Mandatory)][Collections.IDictionary]$Package,
        [Collections.IDictionary]$GameFiles = @{},
        [string]$InstallBody = (Join-Path $BodiesRoot 'install-body-reframework.cmd'),
        [string]$Under = $root
    )
    $caseRoot = Join-Path $Under $Name
    $pkg = Join-Path $caseRoot 'Package ! Folder'
    $game = Join-Path $caseRoot 'Game ! Folder'
    $shared = Join-Path $pkg 'shared'
    $vendor = Join-Path $pkg 'vendor\reframework'
    New-Item -ItemType Directory -Path $game, $shared, $vendor | Out-Null
    Copy-Item -LiteralPath $InstallBody -Destination (Join-Path $shared 'install-body-reframework.cmd')
    Copy-Item -LiteralPath (Join-Path $BodiesRoot 'uninstall-body.cmd') -Destination $shared
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'cecil-marker-check.ps1') -Destination $shared
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'find-game.ps1') -Destination $shared
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../powershell/GamePathDetection.psm1') -Destination $shared
    $gamesJson = @{ schema_version = 1; games = @{ fixture = @{ display_name = 'Fixture'; env_var = 'CUL_FIXTURE_PATH'; executable_relpath = 'fixture.exe' } } }
    [IO.File]::WriteAllText((Join-Path $shared 'games.json'), ($gamesJson | ConvertTo-Json -Depth 5))
    $zip = [IO.Compression.ZipFile]::Open((Join-Path $vendor 'RE.zip'), 'Create')
    try {
        foreach ($entryName in $reZipEntries) {
            $entry = $zip.CreateEntry($entryName)
            if (-not $entryName.EndsWith('/')) {
                $writer = New-Object IO.StreamWriter($entry.Open())
                try { $writer.Write($entryName) } finally { $writer.Dispose() }
            }
        }
    } finally { $zip.Dispose() }
    foreach ($set in @(@{ Dir = $pkg; Files = $Package }, @{ Dir = $game; Files = (Copy-Map @{ 'fixture.exe' = 'fixture.exe' } $GameFiles) })) {
        foreach ($rel in $set.Files.Keys) {
            $path = Join-Path $set.Dir $rel
            New-Item -ItemType Directory -Force -Path (Split-Path $path) | Out-Null
            [IO.File]::WriteAllText($path, $set.Files[$rel])
        }
    }
    $head = @('@echo off', 'setlocal disabledelayedexpansion', 'set "WRAPPER_DIR=%~dp0"',
        'set "GAME_ID=fixture"', 'set "MOD_DISPLAY_NAME=Fixture"', 'set "MOD_INTERNAL_NAME=Fixture"',
        'set "STATE_FILE=.fixture-state.json"', 'set "FRAMEWORK_TYPE=REFramework"')
    foreach ($wrapper in @(@{ Name = 'install'; Config = $Install }, @{ Name = 'uninstall'; Config = $Uninstall })) {
        $lines = $head + @($wrapper.Config.Keys | ForEach-Object { 'set "{0}={1}"' -f $_, $wrapper.Config[$_] })
        $body = if ($wrapper.Name -eq 'install') { 'install-body-reframework.cmd' } else { 'uninstall-body.cmd' }
        $lines += @(('call "%~dp0shared\{0}" %*' -f $body), 'exit /b %errorlevel%')
        [IO.File]::WriteAllText((Join-Path $pkg "$($wrapper.Name).cmd"), ($lines -join "`r`n") + "`r`n")
    }
    [pscustomobject]@{ Name = $Name; Root = $pkg; Game = $game; Runs = 0 }
}

function Get-InstallSnapshot([string]$Dir) {
    @(Get-Snapshot $Dir | ForEach-Object { $_ -replace '"installed_at": "[^"]*"', '"installed_at": "<time>"' })
}

function Assert-Installed($Case, [Collections.IDictionary]$Expected, [string]$InstalledByUs) {
    $state = [IO.File]::ReadAllText((Join-Path $Case.Game '.fixture-state.json'))
    if ($state -notmatch """installed_by_us"": $InstalledByUs\r?\n") { throw "$($Case.Name): state file does not say installed_by_us $InstalledByUs`n$state" }
    $want = @($Expected.Keys | ForEach-Object { "$_=$($Expected[$_])" } | Sort-Object)
    $actual = @(Get-Snapshot $Case.Game | Where-Object { -not $_.EndsWith('\') -and -not $_.StartsWith('.fixture-state.json=') } | Sort-Object)
    $diff = Compare-Object -CaseSensitive $want $actual
    if ($diff) { throw "$($Case.Name): unexpected files`n$($diff | Out-String)" }
}

function Set-GameFile($Case, [string]$Rel, [string]$Content) { [IO.File]::WriteAllText((Join-Path $Case.Game $Rel), $Content) }
function Set-PackageFile($Case, [string]$Rel, [string]$Content) { [IO.File]::WriteAllText((Join-Path $Case.Root $Rel), $Content) }

$p = 'reframework\plugins'
$reInstall = [ordered]@{ MOD_DLLS = 'HeadTracking.dll'; MOD_VERSION = '1.0.0'; REFRAMEWORK_VENDOR_ZIP_NAME = 'RE.zip'; MOD_SEED_FILES = 'HeadTracking.ini Extra.ini' }
$reUninstall = [ordered]@{ MOD_DLLS = 'HeadTracking.dll'; MOD_SEED_FILES = 'HeadTracking.ini Extra.ini'; PRESERVE_FILES = "$p\HeadTracking.ini" }
# Extra.ini sits at the package root, the fallback MOD_DLLS already use.
$rePackage = [ordered]@{ 'plugins\HeadTracking.dll' = 'dll 1'; 'plugins\HeadTracking.ini' = 'default 1'; 'Extra.ini' = 'extra default' }
$preexisting = [ordered]@{ 'dinput8.dll' = 'their dinput8.dll'; 'reframework\autorun\theirs.lua' = 'theirs.lua' }

foreach ($variant in @(@{ By = 'true'; Loader = $reLoader; GameFiles = @{}; Flags = '/y' },
        @{ By = 'false'; Loader = $preexisting; GameFiles = $preexisting; Flags = '/force /y' })) {
    $case = New-InstallCase -Name "re-seed-$($variant.By)" -Install $reInstall -Uninstall $reUninstall -Package $rePackage -GameFiles $variant.GameFiles
    $output = Invoke-Install $case 0
    Assert-Output $case $output @('Deployed default HeadTracking.ini', 'Deployed default Extra.ini', 'Deployed: HeadTracking.dll')
    $expected = Copy-Map (Copy-Map @{ 'fixture.exe' = 'fixture.exe' } $variant.Loader) @{
        "$p\HeadTracking.dll" = 'dll 1'; "$p\HeadTracking.ini" = 'default 1'; "$p\Extra.ini" = 'extra default' }
    Assert-Installed $case $expected $variant.By

    Set-GameFile $case "$p\HeadTracking.ini" 'tuned by the player'
    Set-PackageFile $case 'plugins\HeadTracking.dll' 'dll 2'
    Set-PackageFile $case 'plugins\HeadTracking.ini' 'default 2'
    $output = Invoke-Install $case 0
    Assert-Output $case $output @('Existing REFramework detected, skipping loader install, deploying plugin only.',
        'Kept your existing HeadTracking.ini', 'Kept your existing Extra.ini', 'Deployed: HeadTracking.dll')
    Assert-Installed $case (Copy-Map $expected @{ "$p\HeadTracking.dll" = 'dll 2'; "$p\HeadTracking.ini" = 'tuned by the player' }) $variant.By

    $output = Invoke-Uninstall $case 0 $variant.Flags
    Assert-Files $case @('fixture.exe', "$p\HeadTracking.ini") @{ "$p\HeadTracking.ini" = 'tuned by the player' }
    Assert-KeptLines $case $output @('HeadTracking.ini', "$p\HeadTracking.ini")
    Assert-Output $case $output @('Removed: Extra.ini', '=== Uninstall Complete ===')
    Assert-NoHolding $case

    $output = Invoke-Install $case 0
    Assert-Output $case $output @('Kept your existing HeadTracking.ini', 'Deployed default Extra.ini')
    Assert-Installed $case (Copy-Map (Copy-Map @{ 'fixture.exe' = 'fixture.exe' } $reLoader) @{
        "$p\HeadTracking.dll" = 'dll 2'; "$p\HeadTracking.ini" = 'tuned by the player'; "$p\Extra.ini" = 'extra default' }) 'true'
    Write-Host "PASS reframework seed, installed_by_us $($variant.By) ($($variant.Flags)): fresh install seeds, reinstall keeps an edited seed and replaces the DLL, uninstall keeps the listed seed through the loader removal, the next install keeps it"
}

# Without the list the INI stays in MOD_DLLS and every install overwrites it.
$unlistedInstall = Copy-Map $reInstall @{ MOD_DLLS = 'HeadTracking.dll HeadTracking.ini'; MOD_SEED_FILES = '' }
$bodies = @(@{ Under = $root; Body = (Join-Path $BodiesRoot 'install-body-reframework.cmd') })
if ($ReferenceInstallBody) { $bodies += @{ Under = (Join-Path $root 'reference'); Body = $ReferenceInstallBody } }
$results = foreach ($b in $bodies) {
    $case = New-InstallCase -Name 're-unlisted' -Install $unlistedInstall -Uninstall $reUninstall -Package $rePackage -GameFiles $preexisting -InstallBody $b.Body -Under $b.Under
    $first = Invoke-Install $case 0
    Set-GameFile $case "$p\HeadTracking.ini" 'tuned by the player'
    Set-PackageFile $case 'plugins\HeadTracking.ini' 'default 2'
    $second = Invoke-Install $case 0
    Assert-Output $case $second @('Deployed: HeadTracking.ini')
    Assert-Installed $case (Copy-Map (Copy-Map @{ 'fixture.exe' = 'fixture.exe' } $preexisting) @{ "$p\HeadTracking.dll" = 'dll 1'; "$p\HeadTracking.ini" = 'default 2' }) 'false'
    [pscustomobject]@{ Case = $case; Output = ($first + $second).Replace($case.Game, '<game>').Replace($case.Root, '<pkg>'); Tree = Get-InstallSnapshot $case.Game }
}
if ($ReferenceInstallBody) {
    if (Compare-Object -CaseSensitive $results[0].Tree $results[1].Tree) { throw 're-unlisted: tree differs from the reference install body' }
    if ($results[0].Output -cne $results[1].Output) { throw "re-unlisted: output differs from the reference install body`n--- this body`n$($results[0].Output)`n--- reference`n$($results[1].Output)" }
}
Write-Host 'PASS reframework without MOD_SEED_FILES: the INI in MOD_DLLS is overwritten on every install, as before'

# The wrapper template itself, run from a console where another mod's wrapper
# already set MOD_SEED_FILES: its blank line has to win over the inherited list.
$case = New-InstallCase -Name 're-template' -Install $reInstall -Uninstall $reUninstall -Package $rePackage
$template = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'templates\install-wrapper-reframework.cmd'))
foreach ($pair in @(@('<games.json id>', 'fixture'), @('<Game Name> Head Tracking', 'Fixture'), @('<Mod>HeadTracking.dll', 'HeadTracking.dll'),
        @('<Mod>HeadTracking', 'Fixture'), @('.headtracking-state.json', '.fixture-state.json'), @('REFramework.zip', 'RE.zip'))) {
    if (-not $template.Contains($pair[0])) { throw "re-template: template no longer contains $($pair[0])" }
    $template = $template.Replace($pair[0], $pair[1])
}
[IO.File]::WriteAllText((Join-Path $case.Root 'install.cmd'), $template)
$env:MOD_SEED_FILES = 'OtherMod.ini'
try { $output = Invoke-Install $case 0 } finally { Remove-Item Env:MOD_SEED_FILES }
if ($output.Contains('OtherMod.ini')) { throw "re-template: the inherited MOD_SEED_FILES reached the body`n$output" }
Assert-Installed $case (Copy-Map (Copy-Map @{ 'fixture.exe' = 'fixture.exe' } $reLoader) @{ "$p\HeadTracking.dll" = 'dll 1' }) 'true'
Write-Host 'PASS reframework wrapper template: a MOD_SEED_FILES left in the console by another wrapper is cleared'

$case = New-InstallCase -Name 're-seed-missing' -Install (Copy-Map $reInstall @{ MOD_SEED_FILES = 'HeadTracking.ini Missing.ini' }) -Uninstall $reUninstall -Package $rePackage -GameFiles $preexisting
$output = Invoke-Install $case 1
Assert-Output $case $output @('ERROR: Missing.ini not found in installer package', 'Deployment Failed!')
if (Test-Path -LiteralPath (Join-Path $case.Game '.fixture-state.json')) { throw 're-seed-missing: state file written' }
Write-Host 'PASS reframework seed missing from the package: exit 1, no state file'

# The DLL is already there and overwriting it needs no right on the folder, so
# the seeds are the only copies that fail.
$case = New-InstallCase -Name 're-seed-unwritable' -Install $reInstall -Uninstall $reUninstall -Package $rePackage -GameFiles (Copy-Map $preexisting @{ "$p\HeadTracking.dll" = 'dll 0' })
$plugins = Join-Path $case.Game $p
Set-Deny $plugins -Right 'WD'
try { $output = Invoke-Install $case 1 } finally { Set-Deny $plugins -Remove }
Assert-Output $case $output @('ERROR: Failed to copy HeadTracking.ini - is the game folder writable?', 'ERROR: Failed to copy Extra.ini - is the game folder writable?',
    'Deployed: HeadTracking.dll', 'Deployment Failed!')
if (Test-Path -LiteralPath (Join-Path $case.Game '.fixture-state.json')) { throw 're-seed-unwritable: state file written' }
Write-Host 'PASS reframework seed copy failure: named, exit 1, no state file'

# The ASI install wrapper template, the same way: ASI_SUBDIR, ASI_LOADER_VERSION
# and MOD_SEED_FILES inherited from another mod's install.
$installConsole = Get-InheritedConsole 'install-wrapper-asi.cmd' ([ordered]@{ ASI_SUBDIR = 'OtherMod'; ASI_LOADER_VERSION = '9.9.9' })
function New-AsiInstallCase([string]$Name, [string[]]$Drop = @()) {
    $caseRoot = Join-Path (Join-Path $root 'Wrapper ! Folder') $Name
    $pkg = Join-Path $caseRoot 'Package ! Folder'
    $game = Join-Path $caseRoot 'Game ! Folder'
    $shared = Join-Path $pkg 'shared'
    New-Item -ItemType Directory -Path (Join-Path $game 'bin\OtherMod'), $shared, (Join-Path $pkg 'vendor\ultimate-asi-loader'), (Join-Path $pkg 'plugins') | Out-Null
    Copy-Item -LiteralPath (Join-Path $BodiesRoot 'install-body-asi.cmd') -Destination $shared
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'find-game.ps1') -Destination $shared
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../powershell/GamePathDetection.psm1') -Destination $shared
    $gamesJson = @{ schema_version = 1; games = @{ fixture = @{ display_name = 'Fixture'; env_var = 'CUL_FIXTURE_PATH'; executable_relpath = 'bin\fixture.exe' } } }
    [IO.File]::WriteAllText((Join-Path $shared 'games.json'), ($gamesJson | ConvertTo-Json -Depth 5))
    [IO.File]::WriteAllText((Join-Path $pkg 'vendor\ultimate-asi-loader\dinput8.dll'), 'asi loader')
    [IO.File]::WriteAllText((Join-Path $pkg 'plugins\Fixture.asi'), 'Fixture.asi')
    [IO.File]::WriteAllText((Join-Path $game 'bin\fixture.exe'), 'bin\fixture.exe')
    [IO.File]::WriteAllText((Join-Path $game 'bin\OtherMod\OtherMod.asi'), 'bin\OtherMod\OtherMod.asi')
    Write-Template 'install-wrapper-asi.cmd' @(@('<games.json id>', 'fixture'), @('<Game Name> Head Tracking', 'Fixture'), @('<Mod>HeadTracking.dll', 'Fixture.asi'),
        @('<Mod>HeadTracking', 'Fixture'), @('.headtracking-state.json', '.fixture-state.json')) (Join-Path $pkg 'install.cmd') -Drop $Drop
    [pscustomobject]@{ Name = $Name; Root = $pkg; Game = $game; Runs = 0 }
}

$case = New-AsiInstallCase 'asi-install-template'
$output = Invoke-InConsole $installConsole { Invoke-Install $case 0 }
if ($output.Contains('OtherMod') -or $output.Contains('9.9.9')) { throw "asi-install-template: an inherited value reached the body`n$output" }
if ([IO.File]::ReadAllText((Join-Path $case.Game '.fixture-state.json')).Contains('9.9.9')) { throw 'asi-install-template: the inherited ASI_LOADER_VERSION reached the state file' }
Assert-Installed $case ([ordered]@{ 'bin\fixture.exe' = 'bin\fixture.exe'; 'bin\winmm.dll' = 'asi loader'; 'bin\Fixture.asi' = 'Fixture.asi'
        'bin\OtherMod\OtherMod.asi' = 'bin\OtherMod\OtherMod.asi' }) 'true'

$case = New-AsiInstallCase 'asi-install-template-no-asi-subdir' 'ASI_SUBDIR'
Invoke-InConsole $installConsole { Invoke-Install $case 0 } | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $case.Game 'bin\OtherMod\Fixture.asi'))) { throw 'asi-install-template-no-asi-subdir: the harness no longer shows an inherited ASI_SUBDIR' }
Write-Host 'PASS asi install wrapper template: every blank CONFIG BLOCK name another wrapper left in the console is cleared; without the ASI_SUBDIR line the install lands in the other mod''s folder'

Write-Host "Fixtures retained at $root"
