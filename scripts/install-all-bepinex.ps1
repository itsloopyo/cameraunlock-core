param(
    [Parameter(Mandatory)][string]$GameId,
    [Parameter(Mandatory)][string]$InstallerPath,
    [switch]$Yes
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$modulePath = Join-Path $PSScriptRoot 'GamePathDetection.psm1'
if (-not (Test-Path -LiteralPath $modulePath)) {
    $modulePath = Join-Path $PSScriptRoot '../powershell/GamePathDetection.psm1'
}
Import-Module $modulePath -Force
$targets = @(Find-AllGamePaths -GameId $GameId)
if ($targets.Count -eq 0) { throw "No installations found for $GameId. Pass a game folder to install.cmd." }

Write-Host "Found $($targets.Count) installation(s):"
foreach ($target in $targets) { Write-Host "  $target" }
$previousTarget = $env:_CUL_INSTALL_GAME_PATH
$previousPause = $env:_NO_PAUSE
$failed = 0
try {
    $env:_NO_PAUSE = '1'
    foreach ($target in $targets) {
        # Passing the detected path through the environment avoids CALL expanding
        # percent signs or carets in folder names a second time.
        $env:_CUL_INSTALL_GAME_PATH = $target
        $arguments = @()
        if ($Yes) { $arguments += '/y' }
        & $InstallerPath @arguments
        if ($LASTEXITCODE -ne 0) {
            $failed++
            Write-Host "FAILED: $target (exit $LASTEXITCODE)"
        } else {
            Write-Host "INSTALLED: $target"
        }
    }
} finally {
    $env:_CUL_INSTALL_GAME_PATH = $previousTarget
    $env:_NO_PAUSE = $previousPause
}
Write-Host "Installed $($targets.Count - $failed) of $($targets.Count) copies; $failed failed."
if ($failed -gt 0) { exit 1 }
