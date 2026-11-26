#Requires -Version 5.1
# Runs jvm-site-config.ps1 against data/fixtures/jvm-site-config/vectors.json, the cases the
# launcher's jvm_site_config deploy step runs too. Under Windows PowerShell 5.1, as
# install-body-javaagent.cmd runs it.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$coreRoot = Split-Path $PSScriptRoot
$helper = Join-Path $PSScriptRoot 'jvm-site-config.ps1'
$vectors = [IO.File]::ReadAllText((Join-Path $coreRoot 'data/fixtures/jvm-site-config/vectors.json')) | ConvertFrom-Json
$root = Join-Path $coreRoot ('.lab/jvm-site-config-tests-' + [guid]::NewGuid().ToString('N'))
$utf8 = New-Object System.Text.UTF8Encoding($false)
$exitFor = @{ stock = 1; foreign = 3 }

# PowerShell keeps a parsed object's key order, so two parses compared through one compact
# serialisation differ wherever the order or a value does.
function ConvertTo-Canonical([string]$Text) {
    ConvertTo-Json -InputObject ($Text | ConvertFrom-Json) -Depth 64 -Compress
}

$checks = 0
$failures = New-Object System.Collections.Generic.List[string]
foreach ($case in $vectors.cases) {
    $checks++
    $dir = Join-Path $root $case.name
    New-Item -ItemType Directory -Path $dir | Out-Null
    $stock = Join-Path $dir 'Game64.json'
    $site = Join-Path $dir 'Game64.site.json'
    $stockEncoding = if ($case.PSObject.Properties.Name -contains 'stock_encoding') { New-Object System.Text.UnicodeEncoding($false, $true) } else { $utf8 }
    if ($null -ne $case.stock) { [IO.File]::WriteAllText($stock, $case.stock, $stockEncoding) }
    if ($null -ne $case.site) { [IO.File]::WriteAllText($site, $case.site, $utf8) }

    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $helper,
        '-ConfigPath', $stock, '-SitePath', $site, '-AgentJars', ($case.agent_jars -join ' '))
    if ($case.PSObject.Properties.Name -contains 'main_class') { $argList += @('-MainClass', $case.main_class) }
    # Under Stop, Windows PowerShell turns a child's stderr line into a terminating error.
    $ErrorActionPreference = 'Continue'
    & powershell @argList 2> (Join-Path $dir 'stderr.txt')
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'

    if ($case.expect.PSObject.Properties.Name -contains 'refused') {
        $want = $exitFor[$case.expect.refused]
        if ($code -ne $want) { $failures.Add("$($case.name): exit $code, expected $want ($($case.expect.refused))"); continue }
        $siteNow = if (Test-Path -LiteralPath $site) { [IO.File]::ReadAllText($site) } else { $null }
        if ($siteNow -ne $case.site) { $failures.Add("$($case.name): a refused run changed the site config") }
        continue
    }
    if ($code -ne 0) { $failures.Add("$($case.name): exit $code, expected 0: $([IO.File]::ReadAllText((Join-Path $dir 'stderr.txt')).Trim())"); continue }
    $bytes = [IO.File]::ReadAllBytes($site)
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        $failures.Add("$($case.name): the site config starts with a BOM")
    }
    $written = [IO.File]::ReadAllText($site)
    if (-not $written.EndsWith("}`r`n")) { $failures.Add("$($case.name): the site config does not end in a CRLF line end") }
    $actual = ConvertTo-Canonical $written
    $expected = ConvertTo-Canonical $case.expect.site
    if ($actual -ne $expected) { $failures.Add("$($case.name): wrote $actual, expected $expected") }
    if ($null -ne $case.stock -and [IO.File]::ReadAllText($stock) -ne $case.stock) { $failures.Add("$($case.name): the stock config was changed") }
}

foreach ($f in $failures) { Write-Host "FAIL $f" }
Write-Host "jvm-site-config vectors: $checks cases, $($failures.Count) failed (fixtures at $root)"
if ($failures.Count -gt 0) { exit 1 }
Remove-Item -LiteralPath $root -Recurse -Force
