#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
    Check that every member reference a built mod emits resolves against the
    assemblies it will actually bind to at runtime.

.DESCRIPTION
    Unity mods compile against reference stubs so the build needs no game
    install. That works only while the stub declares each member the same WAY
    the shipped assembly does, because the compiler bakes the access form into
    the IL:

        stub says field,    engine says property -> ldfld  -> MissingFieldException
        stub says property, engine says field    -> call get_X -> MissingMethodException

    Neither shows up at build time. Both throw at runtime, and if the throw is
    inside a render or update callback the game degrades in ways that look
    nothing like a mod bug (Eternal Afternoon: Rect.width declared as a field
    threw every frame inside OnBeginCameraRendering and rendering stalled).

    This resolves each emitted reference against the real assemblies, walking
    the base-type chain the way the runtime does, and reports the mismatches.

    Run it on a machine with the game installed. Without one it skips and
    succeeds, so it is safe to leave in a task chain that also runs on CI or on
    a contributor's machine that does not own the game. Pass -RequireGame to
    turn a missing game into a failure instead.

.PARAMETER GameId
    Game slug from cameraunlock-core/data/games.json. The Managed directory is
    resolved from it. Ignored when -RealAsmDir is given.

.PARAMETER RealAsmDir
    Directory of the real assemblies (a Unity game's *_Data\Managed). Overrides
    -GameId.

.PARAMETER Assembly
    One or more built DLLs to check. Defaults to the mod and CameraUnlock
    assemblies found under -BuildDir.

.PARAMETER BuildDir
    Directory to scan for built assemblies when -Assembly is not given.

.PARAMETER NamespaceFilter
    Only check references whose declaring type starts with this. Empty (the
    default) checks every reference, including the game's own types.

.PARAMETER CecilDll
    Path to Mono.Cecil.dll. Auto-discovered from the repo when omitted.

.PARAMETER RequireGame
    Fail instead of skipping when the game cannot be located.

.EXAMPLE
    ./verify-assembly-refs.ps1 -GameId valheim -BuildDir src/ValheimHeadTracking/bin/Release
#>
[CmdletBinding()]
param(
    [string]$GameId,
    [string]$RealAsmDir,
    [string[]]$Assembly,
    [string]$BuildDir,
    [string]$NamespaceFilter = '',
    [string]$CecilDll,
    [switch]$RequireGame
)

$ErrorActionPreference = 'Stop'
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$coreRoot  = Split-Path -Parent $scriptDir

# --- locate the real assemblies -------------------------------------------
if (-not $RealAsmDir -and $GameId) {
    Import-Module (Join-Path $coreRoot 'powershell\GamePathDetection.psm1') -Force
    $gamePath = $null
    try { $gamePath = Find-GamePath -GameId $GameId } catch { }
    if ($gamePath) {
        $dataDir = Get-ChildItem -Path $gamePath -Filter '*_Data' -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($dataDir) {
            $candidate = Join-Path $dataDir.FullName 'Managed'
            if (Test-Path $candidate) { $RealAsmDir = $candidate }
        }
    }
}

if (-not $RealAsmDir -or -not (Test-Path $RealAsmDir)) {
    $msg = "Game assemblies not found$(if ($GameId) { " for '$GameId'" }). Reference check skipped."
    if ($RequireGame) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }
    Write-Host $msg -ForegroundColor Yellow
    Write-Host "Install the game, or pass -RealAsmDir, to run this check." -ForegroundColor Yellow
    exit 0
}

# --- locate Mono.Cecil -----------------------------------------------------
if (-not $CecilDll) {
    $searchRoots = @($PWD.Path, $coreRoot) | Select-Object -Unique
    foreach ($r in $searchRoots) {
        $hit = Get-ChildItem -Path $r -Recurse -Filter 'Mono.Cecil.dll' -ErrorAction SilentlyContinue |
               Where-Object { $_.FullName -notmatch '\\obj\\' } | Select-Object -First 1
        if ($hit) { $CecilDll = $hit.FullName; break }
    }
}
# BepInEx ships Mono.Cecil, so an installed game usually has a copy even when
# the repo does not.
if (-not $CecilDll) {
    $gameRoot = Split-Path -Parent (Split-Path -Parent $RealAsmDir)
    foreach ($p in @((Join-Path $gameRoot 'BepInEx\core'), $RealAsmDir, $gameRoot)) {
        if (-not (Test-Path $p)) { continue }
        $hit = Get-ChildItem -Path $p -Recurse -Filter 'Mono.Cecil.dll' -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($hit) { $CecilDll = $hit.FullName; break }
    }
}
if (-not $CecilDll) {
    $nupkg = Get-ChildItem -Path $PWD.Path -Recurse -Filter 'Mono.Cecil.*.nupkg' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($nupkg) {
        $tmp = Join-Path $env:TEMP ("cecil-" + [Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $tmp -Force | Out-Null
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [System.IO.Compression.ZipFile]::ExtractToDirectory($nupkg.FullName, $tmp)
        $CecilDll = (Get-ChildItem -Path $tmp -Recurse -Filter 'Mono.Cecil.dll' |
                     Where-Object { $_.FullName -match 'net4' } | Select-Object -First 1).FullName
    }
}
if (-not $CecilDll -or -not (Test-Path $CecilDll)) {
    Write-Host "ERROR: Mono.Cecil.dll not found. Pass -CecilDll." -ForegroundColor Red
    exit 1
}
Add-Type -Path $CecilDll

# --- pick the assemblies to check -----------------------------------------
if (-not $Assembly) {
    if (-not $BuildDir -or -not (Test-Path $BuildDir)) {
        Write-Host "ERROR: pass -Assembly or a -BuildDir that exists." -ForegroundColor Red
        exit 1
    }
    $Assembly = Get-ChildItem -Path $BuildDir -Recurse -Filter *.dll -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -notmatch '^(0Harmony|BepInEx|Mono\.Cecil|MonoMod|UnityEngine|Assembly-CSharp|System|Newtonsoft)' } |
        Select-Object -ExpandProperty FullName -Unique
}
if (-not $Assembly) { Write-Host "No assemblies to check." -ForegroundColor Yellow; exit 0 }

# --- index the real assemblies --------------------------------------------
# A deployed mod puts our own DLLs in the same folder, and that copy is whatever
# was installed last. Indexing it would compare this build against a stale one
# and report every newly added member as missing, so skip anything we are about
# to check.
$ours = @{}
foreach ($a in $Assembly) { $ours[(Split-Path -Leaf $a)] = $true }

$real = @{}
foreach ($f in Get-ChildItem -Path $RealAsmDir -Filter *.dll) {
    if ($ours.ContainsKey($f.Name)) { continue }
    try {
        $m = [Mono.Cecil.ModuleDefinition]::ReadModule($f.FullName)
        foreach ($t in $m.GetTypes()) { if (-not $real.ContainsKey($t.FullName)) { $real[$t.FullName] = $t } }
    } catch { }
}
Write-Host ("Indexed {0} types from {1}" -f $real.Count, $RealAsmDir)

$problems = @()
foreach ($path in $Assembly) {
    if (-not (Test-Path $path)) { continue }
    $mod = [Mono.Cecil.ModuleDefinition]::ReadModule($path)
    $name = Split-Path -Leaf $path

    foreach ($r in $mod.GetMemberReferences()) {
        $declaring = $r.DeclaringType.FullName
        if ($NamespaceFilter -and $declaring -notlike "$NamespaceFilter*") { continue }
        $key = ($declaring -replace '<.*$', '') -replace '\[\]$', ''
        if (-not $real.ContainsKey($key)) { continue }   # type lives elsewhere; not ours to judge

        # The runtime resolves a reference by walking the base-type chain, so a
        # reference emitted against a derived type still binds to a member the
        # base declares. Walk it here too, or inherited members read as broken.
        $chain = @(); $cur = $real[$key]
        while ($cur) {
            $chain += $cur
            if (-not $cur.BaseType) { break }
            $bn = $cur.BaseType.FullName
            if (-not $real.ContainsKey($bn)) { break }
            $cur = $real[$bn]
        }

        if ($r -is [Mono.Cecil.FieldReference]) {
            if (-not ($chain.Fields | Where-Object { $_.Name -eq $r.Name })) {
                $isProp = [bool]($chain.Properties | Where-Object { $_.Name -eq $r.Name })
                $problems += [pscustomobject]@{
                    Assembly = $name; Kind = 'FIELD'; Member = "$key::$($r.Name)"
                    Detail = $(if ($isProp) { 'is a PROPERTY in the real assembly - declare it as a property in the stub' }
                               else { 'does not exist in the real assembly' })
                }
            }
        } elseif ($r -is [Mono.Cecil.MethodReference]) {
            $n = $r.Parameters.Count
            if (-not ($chain.Methods | Where-Object { $_.Name -eq $r.Name -and $_.Parameters.Count -eq $n })) {
                $asField = [bool]($chain.Fields | Where-Object { "get_$($_.Name)" -eq $r.Name -or "set_$($_.Name)" -eq $r.Name })
                $problems += [pscustomobject]@{
                    Assembly = $name; Kind = 'METHOD'; Member = "$key::$($r.Name)($n)"
                    Detail = $(if ($asField) { 'is a FIELD in the real assembly - declare it as a field in the stub' }
                               else { 'no method of that name and arity in the real assembly' })
                }
            }
        }
    }
}

if ($problems.Count -eq 0) {
    Write-Host ("OK: {0} assembl{1} resolve against the real assemblies." -f $Assembly.Count, $(if ($Assembly.Count -eq 1) { 'y' } else { 'ies' })) -ForegroundColor Green
    exit 0
}

Write-Host "`nMISMATCHES - these compile but throw at runtime:" -ForegroundColor Red
$problems | Sort-Object Assembly, Member -Unique | ForEach-Object {
    Write-Host ("  [{0}] {1}`n         in {2}: {3}" -f $_.Kind, $_.Member, $_.Assembly, $_.Detail)
}
Write-Host "`nFix the stub declaration to match, then rebuild." -ForegroundColor Red
exit 1
