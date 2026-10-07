#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# Tests for conformance.ps1's pipeline-port and changelog-unreleased checks
# ============================================================================
# Run: pixi run test-conformance-checks
#
# Builds throwaway git repos, each holding one shape the checks have to tell
# apart, and runs both checks over all of them in one call. A repo has to draw
# exactly the findings listed for it and nothing else.
#
# pipeline-port reads a repo's tracked files and its pixi task graph, so this
# needs git and pixi on PATH.
# ============================================================================

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$CoreRoot = Split-Path -Parent $PSScriptRoot
$conformance = Join-Path $PSScriptRoot 'conformance.ps1'

$sandbox = Join-Path ([System.IO.Path]::GetTempPath()) "cuc-conformance-checks-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $sandbox -Force | Out-Null
$script:Failures = 0

function Check {
    param([string]$Name, [bool]$Condition, [string]$Detail)
    if ($Condition) {
        Write-Host "PASS  $Name" -ForegroundColor Green
    } else {
        Write-Host "FAIL  $Name - $Detail" -ForegroundColor Red
        $script:Failures++
    }
}

function Invoke-Git {
    param([string]$Repo, [string[]]$GitArgs)
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $out = @(& git -C $Repo -c user.email=t@t -c user.name=t -c core.autocrlf=false @GitArgs 2>&1 | ForEach-Object { "$_" })
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
    if ($code -ne 0) { throw "git $($GitArgs -join ' ') failed in ${Repo}: $($out -join "`n")" }
}

# A git repo holding $Files (relative path -> text), all of them tracked.
function New-Repo {
    param([string]$Name, [hashtable]$Files, [string]$Tag)
    $root = Join-Path $sandbox $Name
    New-Item -ItemType Directory -Path $root -Force | Out-Null
    $utf8 = New-Object System.Text.UTF8Encoding $false
    foreach ($rel in $Files.Keys) {
        $path = Join-Path $root $rel
        New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
        [System.IO.File]::WriteAllText($path, $Files[$rel], $utf8)
    }
    Invoke-Git $root @('init', '--quiet')
    Invoke-Git $root @('add', '--all')
    Invoke-Git $root @('commit', '--quiet', '-m', 'init')
    if ($Tag) { Invoke-Git $root @('tag', $Tag) }
    return $root
}

function Get-Pixi {
    param([string]$Tasks)
    return @"
[workspace]
name = "mod"
channels = ["conda-forge"]
platforms = ["win-64"]

[tasks]
$Tasks
"@
}

$VectorsCmd = 'node cameraunlock-core/scripts/pipeline-vectors/run-vectors.mjs --harness \"lua tests/conformance_harness.lua\"'
$PixiNoVectors = Get-Pixi 'test = "echo test"'
$PixiVectors = Get-Pixi "test-vectors = `"$VectorsCmd`"`ntest = { depends-on = [`"test-vectors`"] }"
$PixiVectorsAside = Get-Pixi "test-vectors = `"$VectorsCmd`"`ntest = `"echo test`""

$JavaReceiver = @'
// Receives the OpenTrack datagram.
import java.net.DatagramPacket;
import java.net.DatagramSocket;
final class OpenTrackReceiver {
    void run() throws Exception {
        try (DatagramSocket socket = new DatagramSocket(4242)) {
            DatagramPacket packet = new DatagramPacket(new byte[64], 64);
            socket.receive(packet);
        }
    }
}
'@

$LuaInterpolator = @'
-- Sample rate to frame rate.
local M = { sampleInterval = 1 / 30, maxExtrapolationFraction = 0.5 }
function M.update(dt) return dt / M.sampleInterval end
return M
'@

$PythonSender = @'
# Sends OpenTrack test poses.
import socket, struct
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.sendto(struct.pack('<6d', 0, 0, 0, 10, 0, 0), ('127.0.0.1', 4242))
'@

$LuaTelemetry = @'
-- The game's own telemetry feed, nothing to do with head tracking.
local udp = socket.udp()
udp:setsockname('127.0.0.1', 4444)
local data = udp:receivefrom()
'@

$CMakeLinksCore = @'
add_subdirectory(../cameraunlock-core/cpp core)
add_library(Mod SHARED src/plugin.cpp)
target_link_libraries(Mod PRIVATE
    minhook
    cameraunlock
)
'@

$CMakeNoCore = @'
add_library(Mod SHARED src/plugin.cpp)
target_link_libraries(Mod PRIVATE minhook)
'@

$BuildRs = @'
fn main() {
    cc::Build::new().cpp(true).include("cameraunlock-core/cpp/include")
        .file("cameraunlock-core/cpp/src/config/config_owner.cpp").compile("core");
}
'@

function Get-Changelog {
    param([int]$Bullets, [string]$Bullet = '-', [int]$Nested = 0, [int]$Released = 0)
    $lines = @('# Changelog', '', '## [Unreleased]', '', '### Added', '')
    for ($i = 1; $i -le $Bullets; $i++) { $lines += "$Bullet Added thing $i" }
    for ($i = 1; $i -le $Nested; $i++) { $lines += "  - detail $i" }
    $lines += @('', '## [1.0.0] - 2026-01-01', '', '### Added', '')
    for ($i = 1; $i -le $Released; $i++) { $lines += "- Released thing $i" }
    return ($lines -join "`n") + "`n"
}

$linkIt = 'FAIL .*{0} already builds core into this mod''s own native code\. docs/porting-the-pipeline\.md: "If the mod already loads a native DLL of its own, link `cameraunlock` and do not port'

# Each case: the repo, and the FAIL/WARN findings it must draw, as regexes.
$cases = [ordered]@{}

$cases['no-port'] = @{
    Root = (New-Repo 'no-port' @{ 'pixi.toml' = $PixiNoVectors; 'src/Plugin.cs' = 'class Plugin {}'; 'scripts/send-pose.py' = $PythonSender })
    Expect = @()
}
$cases['unrelated-udp'] = @{
    Root = (New-Repo 'unrelated-udp' @{ 'pixi.toml' = $PixiNoVectors; 'mod/telemetry.lua' = $LuaTelemetry })
    Expect = @()
}
$cases['packet-layer'] = @{
    Root = (New-Repo 'packet-layer' @{ 'pixi.toml' = $PixiNoVectors; 'src/main/java/OpenTrackReceiver.java' = $JavaReceiver })
    Expect = @('FAIL ports the packet layer \(src/main/java/OpenTrackReceiver\.java\) of core''s tracking pipeline.*no pixi task runs .*run-vectors\.mjs')
}
$cases['interpolator'] = @{
    Root = (New-Repo 'interpolator' @{ 'pixi.toml' = $PixiNoVectors; 'modules/poseinterpolator.lua' = $LuaInterpolator })
    Expect = @('FAIL ports the interpolator \(modules/poseinterpolator\.lua\) of core''s tracking pipeline.*no pixi task runs .*run-vectors\.mjs')
}
$cases['both-stages'] = @{
    Root = (New-Repo 'both-stages' @{ 'pixi.toml' = $PixiNoVectors; 'src/OpenTrackReceiver.java' = $JavaReceiver; 'mod/pipeline.lua' = $LuaInterpolator })
    Expect = @('FAIL ports the packet layer \(src/OpenTrackReceiver\.java\) and the interpolator \(mod/pipeline\.lua\) of core''s tracking pipeline')
}
$cases['port-runs-vectors'] = @{
    Root = (New-Repo 'port-runs-vectors' @{ 'pixi.toml' = $PixiVectors; 'modules/poseinterpolator.lua' = $LuaInterpolator })
    Expect = @()
}
$cases['vectors-outside-test'] = @{
    Root = (New-Repo 'vectors-outside-test' @{ 'pixi.toml' = $PixiVectorsAside; 'modules/poseinterpolator.lua' = $LuaInterpolator })
    Expect = @('FAIL ports the interpolator .*`pixi run test` does not run test-vectors')
}
$cases['port-no-pixi'] = @{
    Root = (New-Repo 'port-no-pixi' @{ 'bridge/OpenTrackReceiver.java' = $JavaReceiver })
    Expect = @('FAIL ports the packet layer .*the repo has no pixi\.toml')
}
# Linking core outranks running the vectors: the port has no reason to exist.
$cases['port-beside-linked-core'] = @{
    Root = (New-Repo 'port-beside-linked-core' @{ 'pixi.toml' = $PixiVectors; 'modules/poseinterpolator.lua' = $LuaInterpolator; 'native/CMakeLists.txt' = $CMakeLinksCore })
    Expect = @(($linkIt -f 'native/CMakeLists\.txt'))
}
$cases['port-beside-cargo-core'] = @{
    Root = (New-Repo 'port-beside-cargo-core' @{ 'pixi.toml' = $PixiVectors; 'src/smoothing.rs' = "// sample_interval and extrapolation`n"; 'build.rs' = $BuildRs })
    Expect = @(($linkIt -f 'build\.rs'))
}
$cases['port-beside-other-native'] = @{
    Root = (New-Repo 'port-beside-other-native' @{ 'pixi.toml' = $PixiVectors; 'modules/poseinterpolator.lua' = $LuaInterpolator; 'native/CMakeLists.txt' = $CMakeNoCore })
    Expect = @()
}
# The harness, the vendored core and a build tree are not the mod's port.
$cases['port-shaped-files-elsewhere'] = @{
    Root = (New-Repo 'port-shaped-files-elsewhere' @{
        'pixi.toml' = $PixiNoVectors
        'tests/conformance_harness.lua' = $LuaInterpolator
        'src/test/java/OpenTrackReceiver.java' = $JavaReceiver
        'cameraunlock-core/scripts/example.lua' = $LuaInterpolator
        'vendor/lib/OpenTrackReceiver.java' = $JavaReceiver
    })
    Expect = @()
}

$cases['changelog-absent'] = @{ Root = (New-Repo 'changelog-absent' @{ 'README.md' = "# Mod`n" }); Expect = @() }
$cases['changelog-no-unreleased'] = @{
    Root = (New-Repo 'changelog-no-unreleased' @{ 'CHANGELOG.md' = "# Changelog`n`n## [1.0.0] - 2026-01-01`n`n" + ((1..80 | ForEach-Object { "- Thing $_" }) -join "`n") + "`n" })
    Expect = @()
}
$cases['changelog-at-limit'] = @{ Root = (New-Repo 'changelog-at-limit' @{ 'CHANGELOG.md' = (Get-Changelog 50 -Nested 40 -Released 80) }); Expect = @() }
$cases['changelog-long-unreleased'] = @{
    Root = (New-Repo 'changelog-long-unreleased' @{ 'CHANGELOG.md' = (Get-Changelog 51) })
    Expect = @('WARN CHANGELOG\.md''s \[Unreleased\] holds 51 bullets \(more than 50\).*The repo has no v\* tag.*rewrite it as the short list of what the first release does')
}
$cases['changelog-long-released'] = @{
    Root = (New-Repo 'changelog-long-released' @{ 'CHANGELOG.md' = (Get-Changelog 60 -Bullet '*') } -Tag 'v1.0.0')
    Expect = @('WARN CHANGELOG\.md''s \[Unreleased\] holds 60 bullets.*Or empty the section, and the release writes the entry from the feat:, fix: and perf: commit subjects')
}
# The nightly publisher's rolling tag is not a release.
$cases['changelog-long-dev-tag'] = @{
    Root = (New-Repo 'changelog-long-dev-tag' @{ 'CHANGELOG.md' = (Get-Changelog 51) } -Tag 'dev')
    Expect = @('WARN CHANGELOG\.md''s \[Unreleased\] holds 51 bullets.*The repo has no v\* tag')
}

try {
    $doc = ((Get-Content -LiteralPath (Join-Path $CoreRoot 'docs/porting-the-pipeline.md') -Raw) -replace '\*\*', '' -replace '\s+', ' ')
    $quoted = [regex]::Match((Get-Content -LiteralPath $conformance -Raw), "(?m)^\`$PORT_LINK_IT = '(.+)'\s*$").Groups[1].Value
    Check 'the quoted sentence is in docs/porting-the-pipeline.md' ($quoted -and $doc.Contains($quoted)) "conformance.ps1 quotes: $quoted"

    $roots = @($cases.Values | ForEach-Object { $_.Root })
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $json = & powershell -NoProfile -ExecutionPolicy Bypass -Command "& '$conformance' -Repo $(($roots | ForEach-Object { "'$_'" }) -join ',') -Check pipeline-port,changelog-unreleased -Json; exit `$LASTEXITCODE" 2>&1
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
    $text = (@($json | ForEach-Object { "$_" }) -join "`n").TrimStart([char]0xFEFF)
    Check 'conformance exits 1 when a repo fails' ($code -eq 1) "exit $code`n$text"
    # Windows PowerShell 5.1 hands a JSON array down the pipeline as one object.
    $findings = @(($text | ConvertFrom-Json) | ForEach-Object { $_ })

    foreach ($name in $cases.Keys) {
        $got = @($findings | Where-Object { $_.repo -eq $name } | ForEach-Object { "$($_.severity) $($_.message)" })
        $expect = @($cases[$name].Expect)
        $unmatched = @($expect | Where-Object { $pattern = $_; -not ($got | Where-Object { $_ -match $pattern }) })
        $extra = @($got | Where-Object { $finding = $_; -not ($expect | Where-Object { $finding -match $_ }) })
        Check $name ($unmatched.Count -eq 0 -and $extra.Count -eq 0 -and $got.Count -eq $expect.Count) "missing: $($unmatched -join ' | ') extra: $($extra -join ' | ') got: $($got -join ' | ')"
    }

    # A long [Unreleased] is something to decide about, not a broken repo.
    $warnOnly = & powershell -NoProfile -ExecutionPolicy Bypass -Command "& '$conformance' -Repo '$($cases['changelog-long-unreleased'].Root)' -Check changelog-unreleased -Json | Out-Null; exit `$LASTEXITCODE"
    Check 'a warning alone exits 0' ($LASTEXITCODE -eq 0) "exit $LASTEXITCODE"
} finally {
    Remove-Item -LiteralPath $sandbox -Recurse -Force
}

if ($script:Failures -gt 0) {
    Write-Host "$($script:Failures) failed" -ForegroundColor Red
    exit 1
}
Write-Host 'All pipeline-port and changelog-unreleased cases passed' -ForegroundColor Green
