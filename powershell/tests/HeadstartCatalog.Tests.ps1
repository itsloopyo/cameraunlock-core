#!/usr/bin/env pwsh
#Requires -Version 5.1
# Tests for HeadstartCatalog.psm1's New-HeadstartDevManifest, the one part of the module that
# reaches neither GitHub nor the catalog.
# Run: pixi run test-powershell-headstart-catalog
#
# No Pester dependency, matching ReleaseWorkflow.Tests.ps1.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Join-Path (Split-Path -Parent $PSScriptRoot) 'HeadstartCatalog.psm1') -Force

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

function Get-Thrown {
    param([scriptblock]$Action)
    try { & $Action | Out-Null; return '' } catch { return $_.Exception.Message }
}

$sandbox = Join-Path ([System.IO.Path]::GetTempPath()) "cuc-headstartcatalog-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $sandbox -Force | Out-Null

$download = 'https://github.com/itsloopyo/some-mod/releases/download'
$stable = @(
    'schema = 1'
    'id = "some-mod"'
    'version = "1.2.0"'
    'kind = "java-agent"'
    'interface = 1'
    ''
    '[[file]]'
    'path = "Mod.jar"'
    "url = `"$download/v1.2.0/Mod-aaaaaaaaaaaa.jar`""
    'sha256 = "00"'
    'size = 4'
    ''
    '[[file]]'
    'path = "Art.jar"'
    "url = `"$download/v1.1.0/Art-bbbbbbbbbbbb.jar`""
    'sha256 = "11"'
    'size = 5'
    ''
    '[java_agent]'
    'jar = "Mod.jar"'
    ''
    '[java_agent.properties]'
    '"mod.version" = "1.2.0"'
) -join "`n"
$manifest = Join-Path $sandbox 'cameraunlock-package.toml'
[System.IO.File]::WriteAllText($manifest, $stable + "`n", (New-Object System.Text.UTF8Encoding $false))
$out = Join-Path $sandbox 'dev\cameraunlock-package.toml'

function New-Dev {
    param([string[]]$AssetNames = @('Mod-aaaaaaaaaaaa.jar'), [string]$Version = '1.2.0', [string]$From = $manifest)
    New-HeadstartDevManifest -ManifestPath $From -Repo 'itsloopyo/some-mod' -Version $Version `
        -DevVersion '1.2.0-nightly.20261122.abc1234' -DevTag dev -AssetNames $AssetNames -OutPath $out
}

New-Dev
$dev = [System.IO.File]::ReadAllText($out)
$expected = ($stable + "`n").Replace('version = "1.2.0"', 'version = "1.2.0-nightly.20261122.abc1234"').Replace("$download/v1.2.0/", "$download/dev/")
Check 'the dev manifest differs only in its version and in where this build''s files are' ($dev -ceq $expected) "got:`n$dev"
Check 'a property that happens to hold the version is left as it is' ($dev.Contains('"mod.version" = "1.2.0"')) "got:`n$dev"
Check 'a file kept on an earlier release stays there' ($dev.Contains("$download/v1.1.0/Art-bbbbbbbbbbbb.jar")) "got:`n$dev"
$bytes = [System.IO.File]::ReadAllBytes($out)
Check 'it is written without a byte order mark' ($bytes[0] -eq [byte][char]'s') "first byte $($bytes[0])"

$why = Get-Thrown { New-Dev -AssetNames @('Other.jar') }
Check 'a file the dev release would not have is refused' ($why -match 'Mod-aaaaaaaaaaaa\.jar' -and $why -match 'would not have it') "got '$why'"

$why = Get-Thrown { New-Dev -Version '1.3.0' }
Check 'a manifest of another version is refused' ($why -match '0 lines reading version = "1\.3\.0"') "got '$why'"

$spelt = Join-Path $sandbox 'spelt.toml'
[System.IO.File]::WriteAllText($spelt, ($stable + "`n").Replace('url = "', 'url="'), (New-Object System.Text.UTF8Encoding $false))
$why = Get-Thrown { New-Dev -From $spelt }
Check 'a manifest whose urls are spelt another way is refused, not left naming the tagged release' ($why -match 'names 0 file\(s\)') "got '$why'"

$cased = Join-Path $sandbox 'cased.toml'
[System.IO.File]::WriteAllText($cased, ($stable + "`n").Replace('itsloopyo/some-mod/releases/download/v1.2.0', 'itsloopyo/Some-Mod/releases/download/v1.2.0'), (New-Object System.Text.UTF8Encoding $false))
$why = Get-Thrown { New-Dev -From $cased }
Check 'a file named under another spelling of the repo is refused' ($why -match 'names 0 file\(s\)') "got '$why'"

$crlf = Join-Path $sandbox 'crlf.toml'
[System.IO.File]::WriteAllText($crlf, ($stable + "`n").Replace("`n", "`r`n"), (New-Object System.Text.UTF8Encoding $false))
New-Dev -From $crlf
Check 'a manifest with CRLF line ends is rewritten the same way' ([System.IO.File]::ReadAllText($out) -ceq $expected.Replace("`n", "`r`n")) "got:`n$([System.IO.File]::ReadAllText($out))"

Remove-Item $sandbox -Recurse -Force

Write-Host ''
if ($script:Failures -gt 0) {
    Write-Host "$($script:Failures) check(s) failed" -ForegroundColor Red
    exit 1
}
Write-Host 'all checks passed' -ForegroundColor Green
