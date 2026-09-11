param([string]$Node = 'node')

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
$root = Join-Path ([IO.Path]::GetTempPath()) ('manifest-variants-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
$validator = Join-Path $PSScriptRoot 'validate-manifest.mjs'
$template = @'
{
  "schema_version": 2,
  "mod_info": { "name": "Fixture", "version": "0.0.0", "game_id": "fixture" },
  "delivery_mode": "manifest_variants",
  "variants": [
    {
      "id": "il2cpp", "when": { "file_exists": { "path": "GameAssembly.dll" } },
      "loader": { "archives": [{ "source": "vendor/il2cpp.zip" }] },
      "files": [{ "source": "plugins-il2cpp/mod.dll", "target": "plugins/mod.dll" }]
    },
    {
      "id": "mono",
      "loader": { "archives": [{ "source": "vendor/mono.zip" }] },
      "files": [{ "source": "plugins/mod.dll", "target": "plugins/mod.dll" }]
    }
  ]
}
'@
$cases = @(
    @{ Name = 'valid'; Change = {}; Error = '' },
    @{ Name = 'duplicate-path'; Change = {}; Error = 'duplicate paths' },
    @{ Name = 'missing-helper'; Change = {}; Error = 'install-all-bepinex.ps1' },
    @{ Name = 'duplicate-id'; Change = { $args[0].variants[1].id = 'il2cpp' }; Error = 'share the id' },
    @{ Name = 'empty-id'; Change = { $args[0].variants[0].id = ' ' }; Error = 'nonempty string' },
    @{ Name = 'numeric-id'; Change = { $args[0].variants[0].id = 1 }; Error = 'nonempty string' },
    @{ Name = 'null-variant'; Change = { $args[0].variants[0] = $null }; Error = 'nonempty string' },
    @{ Name = 'object-variants'; Change = { $args[0].variants = @{} }; Error = 'must be an array' },
    @{ Name = 'early-catchall'; Change = { $args[0].variants[0].when = $null }; Error = 'matches every install' },
    @{ Name = 'mixed-payload'; Change = { $args[0] | Add-Member files @(@{ source = 'plugins/mod.dll' }) }; Error = 'top-level' },
    @{ Name = 'unknown-probe'; Change = { $args[0].variants[0].when = @{ typo = @{ path = 'marker' } } }; Error = 'file_exists probe' },
    @{ Name = 'traversal'; Change = { $args[0].variants[0].when.file_exists.path = '../marker' }; Error = 'relative to its anchor' },
    @{ Name = 'absolute'; Change = { $args[0].variants[0].when.file_exists.path = 'C:\marker' }; Error = 'relative to its anchor' },
    @{ Name = 'empty-probe'; Change = { $args[0].variants[0].when.file_exists.path = '' }; Error = 'nonempty path' },
    @{ Name = 'unknown-anchor'; Change = { $args[0].variants[0].when.file_exists | Add-Member anchor 'typo' }; Error = 'unknown probe anchor' },
    @{ Name = 'old-mode'; Change = { $args[0].delivery_mode = 'manifest' }; Error = 'older launchers' },
    @{ Name = 'missing-plugin'; Change = { $args[0].variants[0].files[0].source = 'plugins-il2cpp/missing.dll' }; Error = 'missing.dll' },
    @{ Name = 'missing-loader'; Change = { $args[0].variants[1].loader.archives[0].source = 'vendor/missing.zip' }; Error = 'missing.zip' }
)
foreach ($case in $cases) {
    $manifest = $template | ConvertFrom-Json
    & $case.Change $manifest
    $zipPath = Join-Path $root ($case.Name + '-dev-installer.zip')
    $zip = [IO.Compression.ZipFile]::Open($zipPath, [IO.Compression.ZipArchiveMode]::Create)
    try {
        $entries = @{
            'launcher-manifest.json' = ($manifest | ConvertTo-Json -Depth 20)
            'install.cmd' = '@echo off'
            'uninstall.cmd' = '@echo off'
            'plugins/mod.dll' = 'mono'
            'plugins-il2cpp/mod.dll' = 'il2cpp'
            'vendor/mono.zip' = 'mono loader'
            'vendor/il2cpp.zip' = 'il2cpp loader'
        }
        if ($case.Name -eq 'missing-helper') { $entries['install.cmd'] = 'call "%~dp0shared\install-all-bepinex.ps1"' }
        if ($case.Name -eq 'duplicate-path') { $entries['plugins\mod.dll'] = 'duplicate' }
        foreach ($name in $entries.Keys) {
            $writer = [IO.StreamWriter]::new($zip.CreateEntry($name).Open())
            try { $writer.Write($entries[$name]) } finally { $writer.Dispose() }
        }
    } finally { $zip.Dispose() }
    $stdout = Join-Path $root ($case.Name + '.out')
    $stderr = Join-Path $root ($case.Name + '.err')
    $process = Start-Process -FilePath $Node -ArgumentList @(('"' + $validator + '"'), ('"' + $zipPath + '"')) -NoNewWindow -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $output = (Get-Content $stdout -Raw) + (Get-Content $stderr -Raw)
    if ($case.Error) {
        if ($process.ExitCode -eq 0 -or $output -notlike ('*' + $case.Error + '*')) {
            throw "$($case.Name) did not fail as expected: $output"
        }
    } elseif ($process.ExitCode -ne 0) { throw "$($case.Name) failed: $output" }
    Write-Host "PASS $($case.Name)"
}
Write-Host "$($cases.Count) manifest variant checks passed. Fixtures: $root"

Import-Module (Join-Path $PSScriptRoot '../powershell/ReleaseWorkflow.psm1') -Force
$seedRoot = Join-Path $root 'seeds'
New-Item -ItemType Directory -Path $seedRoot | Out-Null
[IO.File]::WriteAllText((Join-Path $seedRoot 'settings.cfg'), 'current')
foreach ($index in 0, 1) {
    $manifest = $template | ConvertFrom-Json
    $seed = @{ target = 'config/settings.cfg'; content_b64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes('current')) }
    $manifest.variants[$index].loader | Add-Member seed @($seed)
    $manifestPath = Join-Path $seedRoot 'launcher-manifest.json'
    [IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 20))
    Assert-ManifestSeedsMatchShipped -ManifestPath $manifestPath -ProjectRoot $seedRoot
    $seed.content_b64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes('stale'))
    [IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 20))
    $rejected = $false
    try { Assert-ManifestSeedsMatchShipped -ManifestPath $manifestPath -ProjectRoot $seedRoot }
    catch {
        if ($_.Exception.Message -notlike '*no longer matches*') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw "Variant $index seed mismatch was accepted." }
    Write-Host "PASS variant $index seed validation"
}
