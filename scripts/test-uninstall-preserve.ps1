param(
    [string]$BodiesRoot = $PSScriptRoot,
    # An uninstall-body.cmd from before PRESERVE_FILES existed. Given, every case
    # without the list also runs against it and must leave the same tree, exit
    # code and console output.
    [string]$ReferenceBody = ''
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
function Start-Uninstall($Case, [string]$Flags) {
    $Case.Runs++
    $stem = Join-Path $Case.Root ('run{0}' -f $Case.Runs)
    $driver = "$stem.cmd"
    $lines = @('@echo off',
        ('call "{0}\uninstall.cmd" "{1}" {2} > "{3}.out" 2>&1' -f $Case.Root, $Case.Game, $Flags, $stem),
        ('> "{0}.exit" echo %errorlevel%' -f $stem))
    [IO.File]::WriteAllText($driver, ($lines -join "`r`n") + "`r`n")
    $process = Start-Process $env:ComSpec -ArgumentList @('/d', '/c', ('""{0}""' -f $driver)) -WindowStyle Hidden -PassThru
    [pscustomobject]@{ Process = $process; Stem = $stem }
}

function Complete-Uninstall($Run, $Case, [int]$Expected) {
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
    Complete-Uninstall (Start-Uninstall $Case $Flags) $Case $Expected
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

function Set-DenyAddSubdirectory([string]$Dir, [switch]$Remove) {
    $grant = if ($Remove) { @('/remove:d', "*$sid") } else { @('/deny', "*${sid}:(AD)") }
    & icacls.exe $Dir @grant | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "icacls failed on $Dir" }
}

# ---------------------------------------------------------------- ASI loader
$asiConfig = [ordered]@{
    FRAMEWORK_TYPE = 'ASILoader'; ASI_LOADER_NAME = 'winmm.dll'
    MOD_DLLS = 'Fixture.asi HeadTracking.ini'; MOD_SEED_FILES = 'Seed.ini'
    MOD_LEFTOVERS = 'Leftover.ini Fixture.log HeadTracking.ini.pre-canonical Fixture.log.pre-canonical Gone.ini.pre-canonical'
    ROOT_EXTRAS = 'Root.ini Root.log'
}
$asiFiles = [ordered]@{}
foreach ($rel in @('bin\winmm.dll', 'bin\Fixture.asi', 'bin\HeadTracking.ini', 'bin\HeadTracking.ini.pre-canonical',
        'bin\HeadTracking.ini.pre-canonical.last', 'bin\Seed.ini', 'bin\Leftover.ini', 'bin\Fixture.log',
        'bin\Fixture.log.pre-canonical', 'bin\Gone.ini.pre-canonical', 'Root.ini', 'Root.log', 'bin\user.txt')) { $asiFiles[$rel] = $rel }
Invoke-Unlisted -Name 'asi-unlisted' -Config $asiConfig -Files $asiFiles -ExeRelPath 'bin\fixture.exe' -InstalledByUs 'true' -Flags '/y' `
    -Remaining @('bin\fixture.exe', 'bin\user.txt', 'bin\HeadTracking.ini.pre-canonical.last')

# %~f gives an existing file the case it has on disk and a missing one the case
# it was written in, so bin\GONE.INI, gone while its copy is still there, is
# the entry that needs the comparison to ignore case.
$listed = Copy-Map $asiConfig @{ PRESERVE_FILES = 'BIN\headtracking.INI bin\Seed.ini "bin\Leftover.ini" Root.ini bin\GONE.INI' }
$case = New-Case -Name 'asi-listed' -Config $listed -Files $asiFiles -ExeRelPath 'bin\fixture.exe'
$output = Invoke-Uninstall $case 0
Assert-Files $case @('bin\fixture.exe', 'bin\user.txt', 'bin\HeadTracking.ini', 'bin\HeadTracking.ini.pre-canonical',
    'bin\HeadTracking.ini.pre-canonical.last', 'bin\Seed.ini', 'bin\Leftover.ini', 'Root.ini', 'bin\Gone.ini.pre-canonical')
Assert-KeptLines $case $output @('HeadTracking.ini', 'Seed.ini', 'Leftover.ini', 'Root.ini', 'HeadTracking.ini.pre-canonical', 'Gone.ini.pre-canonical')
Assert-Output $case $output @('Removed: Fixture.log.pre-canonical', 'Removed: winmm.dll', 'Removed: state file', '=== Uninstall Complete ===')
Assert-NoHolding $case
Write-Host 'PASS asi: unlisted removes as before; MOD_DLLS, MOD_SEED_FILES, MOD_LEFTOVERS and ROOT_EXTRAS entries kept, with a copy, case-insensitively'

# ---------------------------------------------------------------- MonoCecil
$cecilConfig = [ordered]@{
    FRAMEWORK_TYPE = 'MonoCecil'; MANAGED_SUBFOLDER = 'Eternal Afternoon_Data\Managed'; ASSEMBLY_DLL = 'Assembly-CSharp.dll'
    PATCH_MARKER = 'FixturePatched'; MOD_DLLS = 'HeadTracking.dll'
    MANAGED_EXTRAS = 'HeadTracking.cfg HeadTracking.log HeadTracking.cfg.pre-canonical'
}
$m = 'Eternal Afternoon_Data\Managed'
$cecilFiles = [ordered]@{
    "$m\Assembly-CSharp.dll" = 'FixturePatched build'; "$m\Assembly-CSharp.dll.original" = 'clean build'
    "$m\HeadTracking.dll" = "$m\HeadTracking.dll"; "$m\HeadTracking.cfg" = "$m\HeadTracking.cfg"
    "$m\HeadTracking.cfg.pre-canonical" = "$m\HeadTracking.cfg.pre-canonical"; "$m\HeadTracking.log" = "$m\HeadTracking.log"
}
$restored = @{ "$m\Assembly-CSharp.dll" = 'clean build' }
Invoke-Unlisted -Name 'cecil-unlisted' -Config $cecilConfig -Files $cecilFiles -ExeRelPath 'fixture.exe' -InstalledByUs 'true' -Flags '/y' `
    -Remaining @('fixture.exe', "$m\Assembly-CSharp.dll") -Contents $restored
$listed = Copy-Map $cecilConfig @{ PRESERVE_FILES = """$m\HeadTracking.cfg""" }
$case = New-Case -Name 'cecil-listed' -Config $listed -Files $cecilFiles -ExeRelPath 'fixture.exe'
$output = Invoke-Uninstall $case 0
Assert-Files $case @('fixture.exe', "$m\Assembly-CSharp.dll", "$m\HeadTracking.cfg", "$m\HeadTracking.cfg.pre-canonical") $restored
Assert-KeptLines $case $output @('HeadTracking.cfg', 'HeadTracking.cfg.pre-canonical')
Assert-Output $case $output @('Removed: HeadTracking.log', '=== Uninstall Complete ===')
Write-Host 'PASS cecil: MANAGED_EXTRAS entry quoted with a space kept, with its copy'

# ---------------------------------------------------------------- loader trees
$guid = 'BepInEx\config\com.cameraunlock.fixture.headtracking'
$trees = @(
    @{
        Kind = 'bepinex'; Exe = 'fixture.exe'
        Config = [ordered]@{ FRAMEWORK_TYPE = 'BepInEx'; MOD_DLLS = 'Fixture.dll' }
        Files = @('winhttp.dll', 'doorstop_config.ini', '.doorstop_version', 'BepInEx\core\BepInEx.dll', 'BepInEx\plugins\Fixture.dll',
            'BepInEx\plugins\Other.dll', 'BepInEx\config\BepInEx.cfg', 'BepInEx\config\other.cfg', "$guid.ini",
            "$guid.ini.pre-canonical", "$guid.ini.pre-canonical.last", "$guid.cfg", 'user.txt')
        Preserve = "$guid.ini $guid.cfg"
        Kept = @("$guid.ini", "$guid.ini.pre-canonical", "$guid.ini.pre-canonical.last", "$guid.cfg")
        ModFiles = @('BepInEx\plugins\Fixture.dll')
        LoaderKept = @('user.txt')
        DelOneKept = @()
        Contents = @{}
    },
    @{
        Kind = 'reframework'; Exe = 'fixture.exe'
        Config = [ordered]@{ FRAMEWORK_TYPE = 'REFramework'; MOD_DLLS = 'HeadTracking.dll HeadTracking.ini' }
        Files = @('dinput8.dll', 'reframework_revision.txt', 'reframework\plugins\HeadTracking.dll', 'reframework\plugins\HeadTracking.ini',
            'reframework\plugins\HeadTracking.ini.pre-canonical', 'reframework\plugins\HeadTracking.ini.pre-canonical.last',
            'reframework\autorun\other.lua', 'user.txt')
        Preserve = 'reframework\plugins\HeadTracking.ini'
        Kept = @('reframework\plugins\HeadTracking.ini', 'reframework\plugins\HeadTracking.ini.pre-canonical',
            'reframework\plugins\HeadTracking.ini.pre-canonical.last')
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
            'Game\Binaries\Win64\Mods\HeadTracking\HeadTracking.ini', 'Game\Binaries\Win64\Mods\HeadTracking\HeadTracking.ini.pre-canonical',
            'Game\Binaries\Win64\Mods\HeadTracking\HeadTracking.ini.pre-canonical.last', 'user.txt')
        Preserve = 'Game\Binaries\Win64\Mods\HeadTracking\HeadTracking.ini'
        Kept = @('Game\Binaries\Win64\Mods\HeadTracking\HeadTracking.ini', 'Game\Binaries\Win64\Mods\HeadTracking\HeadTracking.ini.pre-canonical',
            'Game\Binaries\Win64\Mods\HeadTracking\HeadTracking.ini.pre-canonical.last')
        ModFiles = @('Game\Binaries\Win64\Mods\HeadTracking\Scripts\main.lua', 'Game\Binaries\Win64\Mods\HeadTracking\HeadTracking.ini',
            'Game\Binaries\Win64\Mods\HeadTracking\HeadTracking.ini.pre-canonical', 'Game\Binaries\Win64\Mods\HeadTracking\HeadTracking.ini.pre-canonical.last')
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
    Write-Host "PASS $($tree.Kind): installed_by_us true, false and /force, with and without the list, copies kept through the tree removal"
}

# ---------------------------------------------------------------- failures
$bepFiles = [ordered]@{}
foreach ($rel in $trees[0].Files) { $bepFiles[$rel] = $rel }
$bepListed = Copy-Map $trees[0].Config @{ PRESERVE_FILES = $trees[0].Preserve }

$case = New-Case -Name 'leftover-holding' -Config $bepListed -Files (Copy-Map $bepFiles @{ "$holding\$guid.ini" = 'set aside earlier' }) -ExeRelPath 'fixture.exe'
$before = Get-Snapshot $case.Game
$output = Invoke-Uninstall $case 1
if (Compare-Object -CaseSensitive $before (Get-Snapshot $case.Game)) { throw 'leftover-holding: the game folder changed' }
if ($output -notmatch [regex]::Escape("$holding is left over")) { throw "leftover-holding: output does not name the folder`n$output" }
Write-Host 'PASS leftover holding folder: refused with exit 1 before anything was touched'

$run = Start-Uninstall $case ''
Start-Sleep -Seconds 3
if ($run.Process.HasExited) { throw 'pause: a failed uninstall without /y did not wait at the console' }
Send-Enter $run.Process.Id
$output = Complete-Uninstall $run $case 1
if ($output -notmatch 'Press any key') { throw "pause: no pause prompt`n$output" }
if (Compare-Object -CaseSensitive $before (Get-Snapshot $case.Game)) { throw 'pause: the game folder changed' }
Write-Host 'PASS pause: a failed run without /y waits on the console, Enter releases it, exit code stays 1'

$case = New-Case -Name 'set-aside-fails' -Config $bepListed -Files $bepFiles -ExeRelPath 'fixture.exe'
$before = Get-Snapshot $case.Game
Set-DenyAddSubdirectory $case.Game
try { $output = Invoke-Uninstall $case 1 } finally { Set-DenyAddSubdirectory $case.Game -Remove }
Assert-Output $case $output @("ERROR: could not set $guid.ini aside, so the folder holding it is left in place.", '=== Uninstall Incomplete ===')
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
Set-DenyAddSubdirectory $mods
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
} finally { Set-DenyAddSubdirectory $mods -Remove }
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
# with it. The copies are checked too.
$folderCases = @(
    @{ Name = 'folder-entry'; Config = Copy-Map $trees[0].Config @{ PRESERVE_FILES = 'BepInEx\config' }; Files = $bepFiles; Exe = 'fixture.exe'; Named = 'BepInEx\config' },
    @{ Name = 'folder-copy'; Config = Copy-Map $asiConfig @{ PRESERVE_FILES = 'bin\Other.ini' }
        Files = Copy-Map $asiFiles @{ 'bin\Other.ini' = 'bin\Other.ini'; 'bin\Other.ini.pre-canonical.last\x.txt' = 'x' }
        Exe = 'bin\fixture.exe'; Named = 'bin\Other.ini.pre-canonical.last' }
)
foreach ($folder in $folderCases) {
    $case = New-Case -Name $folder.Name -Config $folder.Config -Files $folder.Files -ExeRelPath $folder.Exe
    $before = Get-Snapshot $case.Game
    $output = Invoke-Uninstall $case 1
    Assert-Output $case $output @('ERROR: PRESERVE_FILES in the uninstall.cmd CONFIG BLOCK names a folder:', $folder.Named)
    if (Compare-Object -CaseSensitive $before (Get-Snapshot $case.Game)) { throw "$($folder.Name): the game folder changed" }
}
Write-Host 'PASS folder entries: a listed path or copy that is a folder is refused with exit 1, nothing touched'

$case = New-Case -Name 'unknown-flag' -Config $bepListed -Files $bepFiles -ExeRelPath 'fixture.exe'
$before = Get-Snapshot $case.Game
Invoke-Uninstall $case 2 '--bogus /y' | Out-Null
if (Compare-Object -CaseSensitive $before (Get-Snapshot $case.Game)) { throw 'unknown-flag: the game folder changed' }
Invoke-Uninstall $case 0 '-y' | Out-Null
Assert-Files $case (@('fixture.exe', 'user.txt') + $trees[0].Kept)
Write-Host 'PASS arguments: unknown flag exit 2, -y completes'

Write-Host "Fixtures retained at $root"
