#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# Tests for DifferentialGate.psm1
# ============================================================================
# Run: pixi run test-powershell-differential-gate
#
# The gate may only skip a differential whose inputs are the ones that passed,
# so each check is either "this change must rerun it" or "this change must not".
# A throwaway mod repo with core as a submodule stands in for a real one. It has
# no build tree, so it exercises the `full` runner (every tracked file); the
# MSBuild and dotnet runners are exercised against real mod repos.
#
# Needs git and node on PATH. No Pester dependency, matching the other tests.
# ============================================================================

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$modulePath = Join-Path (Split-Path -Parent $PSScriptRoot) 'DifferentialGate.psm1'
Import-Module $modulePath -Force

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

function Invoke-TestGit {
    param([string]$Dir)
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $out = & git -C $Dir @args 2>&1 } finally { $ErrorActionPreference = $prev }
    if ($LASTEXITCODE -ne 0) { throw "git -C $Dir $($args -join ' ') failed: $($out -join ' ')" }
    return $out
}

function Write-Text {
    param([string]$Path, [string]$Text)
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    [System.IO.File]::WriteAllText($Path, $Text, [System.Text.UTF8Encoding]::new($false))
}

# --- masking ----------------------------------------------------------------

$cmake = "cmake_minimum_required(VERSION 3.20)`nproject(Mod VERSION 0.2.1 LANGUAGES CXX)`nset(X 1)`n"
Check 'a version bump in CMakeLists.txt is masked' ((Get-MaskedText 'CMakeLists.txt' $cmake) -eq (Get-MaskedText 'CMakeLists.txt' ($cmake -replace '0\.2\.1', '0.3.0'))) ''
Check 'cmake_minimum_required keeps its version' ((Get-MaskedText 'CMakeLists.txt' $cmake) -ne (Get-MaskedText 'CMakeLists.txt' ($cmake -replace '3\.20', '3.28'))) ''
Check 'any other CMakeLists.txt edit counts' ((Get-MaskedText 'CMakeLists.txt' $cmake) -ne (Get-MaskedText 'CMakeLists.txt' ($cmake -replace 'X 1', 'X 2'))) ''
Check 'CRLF and LF hash alike' ((Get-MaskedText 'CMakeLists.txt' $cmake) -eq (Get-MaskedText 'CMakeLists.txt' ($cmake -replace "`n", "`r`n"))) ''
$pixi = "[workspace]`nname = `"mod`"`nversion = `"0.2.1`"`n[tasks]`ntest-differential = `"ctest`"`n"
Check 'a version bump in pixi.toml is masked' ((Get-MaskedText 'pixi.toml' $pixi) -eq (Get-MaskedText 'pixi.toml' ($pixi -replace '0\.2\.1', '0.3.0'))) ''
Check 'a task edit in pixi.toml counts' ((Get-MaskedText 'pixi.toml' $pixi) -ne (Get-MaskedText 'pixi.toml' ($pixi -replace '"ctest"', '"ctest -L x"'))) ''
$csproj = '<Project><PropertyGroup><Version>1.2.3</Version></PropertyGroup><ItemGroup><PackageReference Include="xunit" Version="2.9.3" /></ItemGroup></Project>'
Check 'a <Version> bump is masked' ((Get-MaskedText 'A.csproj' $csproj) -eq (Get-MaskedText 'A.csproj' ($csproj -replace '1\.2\.3', '1.3.0'))) ''
Check 'a package version is not masked' ((Get-MaskedText 'A.csproj' $csproj) -ne (Get-MaskedText 'A.csproj' ($csproj -replace '2\.9\.3', '2.9.4'))) ''

# --- a repo --------------------------------------------------------------------

$sandbox = Join-Path ([System.IO.Path]::GetTempPath()) "cuc-differential-gate-$([guid]::NewGuid().ToString('N'))"
try {
    $core = Join-Path $sandbox 'core'
    Write-Text (Join-Path $core 'data/config-format.json') '{"schema_version":1,"normalisations":{"N1":"x"},"approved_changes":{},"configs":{"mod":[{"committed":"config/Mod.ini"}],"other":[{"committed":"a.ini"}]},"legacy":{},"exempt":{},"conversion_notes":{},"allow_legacy_symbols":{},"per_game":{}}'
    Write-Text (Join-Path $core 'scripts/lint.mjs') "import fs from 'node:fs';`nexport const schema = 'config-format.json';`n"
    Write-Text (Join-Path $core 'README.md') "core`n"
    Invoke-TestGit $core init -q -b main | Out-Null
    Invoke-TestGit $core add -A | Out-Null
    Invoke-TestGit $core -c user.email=t@t -c user.name=t commit -q -m core | Out-Null

    $repo = Join-Path $sandbox 'mod'
    New-Item -ItemType Directory -Path $repo -Force | Out-Null
    Invoke-TestGit $repo init -q -b main | Out-Null
    Invoke-TestGit $repo -c protocol.file.allow=always submodule add -q $core core | Out-Null
    Write-Text (Join-Path $repo 'pixi.toml') $pixi
    Write-Text (Join-Path $repo 'CMakeLists.txt') $cmake
    Write-Text (Join-Path $repo 'src/config.cpp') "int config = 1;`n"
    Write-Text (Join-Path $repo 'config/Mod.ini') "[General]`n"
    Write-Text (Join-Path $repo 'tests/config_differential/lint-migrated.mjs') "import { schema } from '../../core/scripts/lint.mjs';`n"
    Invoke-TestGit $repo add -A | Out-Null
    Invoke-TestGit $repo -c user.email=t@t -c user.name=t commit -q -m mod | Out-Null

    $inputs = Get-DifferentialInputs -Root $repo
    Check 'a repo with no build tree uses the full runner' ($inputs.Runner -eq 'full') "runner $($inputs.Runner)"
    Check 'the full runner lists the submodule commit' ($inputs.Entries -contains 'submodule:core') ($inputs.Entries -join ', ')
    Check 'the lint import is followed into the submodule' ($inputs.Entries -contains 'core/scripts/lint.mjs') ($inputs.Entries -join ', ')
    Check 'config-format.json is hashed as this repo''s slice' ($inputs.Entries -contains 'slice:core/data/config-format.json#mod') ($inputs.Entries -join ', ')
    Check 'the record is not its own input' ($inputs.Entries -notcontains 'tests/config_differential/passed.json') ''

    Write-DifferentialStamp -Root $repo -Inputs $inputs | Out-Null
    $stamp = Read-DifferentialStamp -Root $repo
    Check 'a fresh record holds' (@(Get-DifferentialChanges -Root $repo -Stamp $stamp).Count -eq 0) ''

    $clean = [System.IO.File]::ReadAllText((Join-Path $repo 'src/config.cpp'))
    [System.IO.File]::WriteAllText((Join-Path $repo 'src/config.cpp'), ($clean -replace "`n", "`r`n"))
    Check 'CRLF in the working tree still matches the committed file' (@(Get-DifferentialChanges -Root $repo -Stamp $stamp).Count -eq 0) ''
    Write-Text (Join-Path $repo 'src/config.cpp') "int config = 2;`n"
    $changed = @(Get-DifferentialChanges -Root $repo -Stamp $stamp)
    Check 'an edited source breaks the record and is named' ($changed -contains 'src/config.cpp') ($changed -join ', ')
    Write-Text (Join-Path $repo 'src/config.cpp') $clean

    Write-Text (Join-Path $repo 'pixi.toml') ($pixi -replace '0\.2\.1', '0.9.0')
    Write-Text (Join-Path $repo 'CMakeLists.txt') ($cmake -replace '0\.2\.1', '0.9.0')
    Check 'a release version bump keeps the record' (@(Get-DifferentialChanges -Root $repo -Stamp $stamp).Count -eq 0) ''

    $format = Join-Path $core 'data/config-format.json'
    $sub = Join-Path $repo 'core/data/config-format.json'
    $text = [System.IO.File]::ReadAllText($sub)
    Write-Text $sub ($text -replace '"a.ini"', '"b.ini"')
    Check 'another repo''s registration keeps the record' (@(Get-DifferentialChanges -Root $repo -Stamp $stamp).Count -eq 0) ''
    Write-Text $sub ($text -replace 'config/Mod.ini', 'config/Other.ini')
    $changed = @(Get-DifferentialChanges -Root $repo -Stamp $stamp)
    Check 'this repo''s registration breaks it' ($changed -contains 'slice:core/data/config-format.json#mod') ($changed -join ', ')
    Write-Text $sub ($text -replace '"x"', '"y"')
    $changed = @(Get-DifferentialChanges -Root $repo -Stamp $stamp)
    Check 'a shared section breaks it' ($changed -contains 'slice:core/data/config-format.json#mod') ($changed -join ', ')
    Write-Text $sub $text

    Write-Text (Join-Path $repo 'tests/config_differential/data/new.ini') "[x]`n"
    Invoke-TestGit $repo add -A | Out-Null
    $changed = @(Get-DifferentialChanges -Root $repo -Stamp $stamp)
    Check 'the full runner reruns on a new tracked file' ($changed.Count -gt 0) ''
} finally {
    Remove-Item -LiteralPath $sandbox -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:Failures -gt 0) {
    Write-Host "$script:Failures check(s) failed" -ForegroundColor Red
    exit 1
}
Write-Host 'All DifferentialGate checks passed' -ForegroundColor Green
