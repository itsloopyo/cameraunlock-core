#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# Tests for conformance.ps1's ci-minutes check
# ============================================================================
# Run: pixi run test-ci-minutes
#
# Builds throwaway mod repos, each a copy of a conformant one with one thing
# broken, and runs the check over all of them in one call. A repo has to draw
# exactly the findings listed for it and nothing else.
#
# The check reads each caller's pinned release-mod.yml out of this core
# checkout, so it needs core's history: HEAD is a commit whose release-mod.yml
# runs the config differential, and $OldPin one whose release-mod.yml does not.
# It also needs pixi on PATH, which reads every pixi.toml here.
# ============================================================================

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$CoreRoot = Split-Path -Parent $PSScriptRoot
$conformance = Join-Path $PSScriptRoot 'conformance.ps1'
$OldPin = '435915a8a3d8e1b799cc169b74f1fcf72b9534e7'
$UnknownPin = 'deadbeefdeadbeefdeadbeefdeadbeefdeadbeef'

$shallow = (& git -C $CoreRoot rev-parse --is-shallow-repository).Trim()
if ($shallow -ne 'false') { throw "$CoreRoot is a shallow clone. This test reads release-mod.yml at $OldPin; fetch the full history (git fetch --unshallow) and re-run." }
$HeadPin = (& git -C $CoreRoot rev-parse HEAD).Trim()
if (((& git -C $CoreRoot show "$($HeadPin):.github/workflows/release-mod.yml") -join "`n") -notmatch 'pixi run test-differential') {
    throw "HEAD's release-mod.yml has no config differential step; commit it before running this test."
}

$sandbox = Join-Path ([System.IO.Path]::GetTempPath()) "cuc-ci-minutes-$([guid]::NewGuid().ToString('N'))"
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

$PixiHead = @'
[workspace]
name = "mod"
channels = ["conda-forge"]
platforms = ["win-64"]

[tasks]
build = "echo build"
build-release = "echo build-release"

'@

$SplitTasks = @'
test-unit = { cmd = "echo unit", depends-on = ["build"] }
test-differential = { cmd = "echo differential", depends-on = ["build"] }
test = { depends-on = ["test-unit", "test-differential"] }
package = { cmd = "echo package", depends-on = ["build-release", "test-unit"] }
'@

$PlainTasks = @'
test = { cmd = "echo test", depends-on = ["build"] }
package = { cmd = "echo package", depends-on = ["build-release", "test"] }
'@

$BuildYml = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'templates/build-workflow.yml') -Raw

function Get-ReleaseYml {
    param([string]$Pin, [string]$With = '')
    return @"
name: Release

on:
  push:
    tags:
      - 'v*.*.*'

jobs:
  release:
    uses: itsloopyo/cameraunlock-core/.github/workflows/release-mod.yml@$Pin
    with:
      project-name: 'Mod'
      version-source: pixi
      version-path: 'pixi.toml'
      artifact-paths: 'src/'
$With    secrets: inherit
"@
}

$ReleasePs1 = @'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
Write-Host "Running the full test suite..."
Push-Location $ProjectRoot
try {
    pixi run test
    if ($LASTEXITCODE -ne 0) { exit 1 }
} finally {
    Pop-Location
}
'@

function New-ModRepo {
    param(
        [string]$Name,
        [switch]$Differential,
        [string]$Tasks,
        [string]$Build = $BuildYml,
        [string]$Release,
        [string]$ReleaseScript = $ReleasePs1,
        [hashtable]$ExtraWorkflows = @{}
    )
    $root = Join-Path $sandbox $Name
    New-Item -ItemType Directory -Path (Join-Path $root '.github/workflows') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $root 'scripts') -Force | Out-Null
    if ($Differential) { New-Item -ItemType Directory -Path (Join-Path $root 'tests/config_differential') -Force | Out-Null }
    if (-not $Tasks) { $Tasks = if ($Differential) { $SplitTasks } else { $PlainTasks } }
    if (-not $Release) { $Release = Get-ReleaseYml $HeadPin }
    $utf8 = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText((Join-Path $root 'pixi.toml'), $PixiHead + $Tasks, $utf8)
    if ($Build) { [System.IO.File]::WriteAllText((Join-Path $root '.github/workflows/build.yml'), $Build, $utf8) }
    [System.IO.File]::WriteAllText((Join-Path $root '.github/workflows/release.yml'), $Release, $utf8)
    if ($ReleaseScript) { [System.IO.File]::WriteAllText((Join-Path $root 'scripts/release.ps1'), $ReleaseScript, $utf8) }
    foreach ($file in $ExtraWorkflows.Keys) {
        [System.IO.File]::WriteAllText((Join-Path $root ".github/workflows/$file"), $ExtraWorkflows[$file], $utf8)
    }
    return $root
}

# Each case: the repo, and the FAIL/WARN findings it must draw, as regexes.
$cases = [ordered]@{}

$cases['good-differential'] = @{ Root = (New-ModRepo 'good-differential' -Differential); Expect = @() }
$cases['good-plain'] = @{ Root = (New-ModRepo 'good-plain' -Release (Get-ReleaseYml $OldPin)); Expect = @() }

$twice = $BuildYml -replace '(?m)^      - name: Build, test and package', @'
      - name: Run unit tests
        shell: pwsh
        run: |
          pixi run test
          if ($LASTEXITCODE -ne 0) {
            Write-Host "::error::pixi run test failed"
            exit 1
          }

      - name: Build, test and package
'@
$cases['tests-twice'] = @{ Root = (New-ModRepo 'tests-twice' -Build $twice); Expect = @('FAIL build\.yml runs `pixi run test` and `pixi run package`.*twice') }

$cases['unit-step-twice'] = @{
    Root = (New-ModRepo 'unit-step-twice' -Differential -Build ($twice -replace 'pixi run test\r?\n', "pixi run test-unit`n"))
    Expect = @('FAIL build\.yml runs `pixi run test-unit` and `pixi run package`.*twice')
}

$noConcurrency = $BuildYml -replace '(?ms)^concurrency:.*?(?=^jobs:)', ''
$cases['no-concurrency'] = @{ Root = (New-ModRepo 'no-concurrency' -Build $noConcurrency); Expect = @('FAIL build\.yml has no top-level concurrency') }

$commentUnderOn = $noConcurrency -replace '(?m)^on:\r?\n', "on:`n# a comment at column 0 inside the block`n"
$cases['no-concurrency-comment-under-on'] = @{ Root = (New-ModRepo 'no-concurrency-comment-under-on' -Build $commentUnderOn); Expect = @('FAIL build\.yml has no top-level concurrency') }

$cases['concurrency-not-cancelling'] = @{
    Root = (New-ModRepo 'concurrency-not-cancelling' -Build ($BuildYml -replace 'cancel-in-progress: true', 'cancel-in-progress: false'))
    Expect = @('FAIL build\.yml has no top-level concurrency')
}

$cases['concurrency-not-per-ref'] = @{
    Root = (New-ModRepo 'concurrency-not-per-ref' -Build ($BuildYml -replace 'group: \$\{\{ github\.workflow \}\}-\$\{\{ github\.ref \}\}', 'group: build'))
    Expect = @('FAIL build\.yml has no top-level concurrency')
}

$noDocsSkip = $BuildYml -replace '(?ms)^  pull_request:.*?(?=^permissions:)', "`n" -replace '(?ms)^    paths-ignore:.*?(?=^\S|^  \S)', ''
$cases['docs-build'] = @{ Root = (New-ModRepo 'docs-build' -Build $noDocsSkip); Expect = @("FAIL build\.yml's push trigger has no paths-ignore") }

$cases['docs-only-folder-ignored'] = @{
    Root = (New-ModRepo 'docs-only-folder-ignored' -Build ($BuildYml -replace "(?m)^      - '\*\*\.md'\r?\n", ''))
    Expect = @()
}

$cases['one-line-trigger'] = @{
    Root = (New-ModRepo 'one-line-trigger' -Build ($BuildYml -replace '(?ms)^on:.*?(?=^permissions:)', "on: [push, pull_request]`n`n"))
    Expect = @("FAIL build\.yml's push trigger has no paths-ignore", "FAIL build\.yml's pull_request trigger has no paths-ignore")
}

$uploads = $BuildYml + @'


      - name: Upload installer
        uses: actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02 # v4.6.2
        with:
          name: installer
          path: release/*-installer.zip
'@
$cases['uploads-artifact'] = @{ Root = (New-ModRepo 'uploads-artifact' -Build $uploads); Expect = @('FAIL build\.yml uploads an artifact') }

$cases['differential-not-split'] = @{
    Root = (New-ModRepo 'differential-not-split' -Differential -Tasks $PlainTasks)
    Expect = @('FAIL the repo has tests/config_differential and pixi\.toml has no test-differential task')
}

$cases['package-runs-differential'] = @{
    Root = (New-ModRepo 'package-runs-differential' -Differential -Tasks ($SplitTasks -replace '"build-release", "test-unit"', '"build-release", "test"'))
    Expect = @('FAIL package runs test-differential', 'FAIL build\.yml runs test-differential on every push')
}

$cases['test-skips-differential'] = @{
    Root = (New-ModRepo 'test-skips-differential' -Differential -Tasks ($SplitTasks -replace 'test = \{ depends-on = \["test-unit", "test-differential"\] \}', 'test = { depends-on = ["test-unit"] }'))
    Expect = @('FAIL `pixi run test` does not run test-differential')
}

$cases['test-runs-more'] = @{
    Root = (New-ModRepo 'test-runs-more' -Differential -Tasks ($SplitTasks + "`ntest-vectors = `"echo vectors`"`n" -replace '\["test-unit", "test-differential"\]', '["test-unit", "test-differential", "test-vectors"]'))
    Expect = @('FAIL `pixi run test` runs test-vectors, which neither package nor test-differential runs')
}

$cases['push-untested'] = @{
    Root = (New-ModRepo 'push-untested' -Tasks ($PlainTasks -replace '"build-release", "test"', '"build-release"'))
    Expect = @('FAIL `pixi run test` runs test''s own command, build, which neither package', 'FAIL build\.yml runs no test task')
}

$cases['old-release-pin'] = @{
    Root = (New-ModRepo 'old-release-pin' -Differential -Release (Get-ReleaseYml $OldPin))
    Expect = @('FAIL release\.yml pins release-mod\.yml at 435915a8, which has no config differential step')
}

$cases['unknown-release-pin'] = @{
    Root = (New-ModRepo 'unknown-release-pin' -Differential -Release (Get-ReleaseYml $UnknownPin))
    Expect = @('WARN release\.yml pins release-mod\.yml at deadbeef, which this core checkout does not have')
}

$cases['release-through-build'] = @{
    Root = (New-ModRepo 'release-through-build' -Release (Get-ReleaseYml $HeadPin "      package-task: build-release`n"))
    Expect = @('FAIL release\.yml releases through build-release, which does not run test''s own command')
}

$inlineRelease = @'
name: Release

on:
  push:
    tags:
      - 'v*.*.*'

concurrency:
  group: ${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true

jobs:
  release:
    runs-on: windows-latest
    steps:
      - run: pixi run build-release
'@
$cases['inline-release'] = @{
    Root = (New-ModRepo 'inline-release' -Release $inlineRelease)
    Expect = @('FAIL release\.yml sets cancel-in-progress: true', 'FAIL release\.yml is a release workflow that does not run test''s own command, build from `pixi run test`')
}

$cases['inline-release-full'] = @{
    Root = (New-ModRepo 'inline-release-full' -Differential -Release ($inlineRelease -replace '(?ms)^concurrency:.*?(?=^jobs:)', '' -replace 'pixi run build-release', "pixi run package`n      - run: pixi run test-differential"))
    Expect = @()
}

$cases['release-script-untested'] = @{
    Root = (New-ModRepo 'release-script-untested' -ReleaseScript @'
# pixi run test
Write-Host "Run pixi run test first"
pixi run test-unit
pixi run build-release
'@)
    Expect = @('FAIL scripts/release\.ps1 never runs `pixi run test`')
}

$cases['scheduled-workflow-ignored'] = @{
    Root = (New-ModRepo 'scheduled-workflow-ignored' -ExtraWorkflows @{ 'watch.yml' = "name: Watch`n`non:`n  schedule:`n    - cron: '0 0 * * *'`n`njobs:`n  watch:`n    runs-on: ubuntu-latest`n    steps:`n      - run: pixi run test`n" })
    Expect = @()
}

try {
    $roots = @($cases.Values | ForEach-Object { $_.Root })
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $json = & powershell -NoProfile -ExecutionPolicy Bypass -Command "& '$conformance' -Repo $(($roots | ForEach-Object { "'$_'" }) -join ',') -Check ci-minutes -Json; exit `$LASTEXITCODE" 2>&1
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
} finally {
    Remove-Item -LiteralPath $sandbox -Recurse -Force
}

if ($script:Failures -gt 0) {
    Write-Host "$($script:Failures) failed" -ForegroundColor Red
    exit 1
}
Write-Host 'All ci-minutes cases passed' -ForegroundColor Green
