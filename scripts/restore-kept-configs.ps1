# Finishes an uninstall that stopped while it had config files set aside.
#
# uninstall-body.cmd moves each PRESERVE_FILES entry inside a loader folder to
# <game>\CameraUnlock-kept-configs\<its path relative to the game folder>,
# removes the loader folder, and moves the entry back. A run that dies between
# the two moves leaves the player's config in that folder, where a fresh
# install would create a new CameraUnlock.ini and skip the legacy import. Every
# install and uninstall body runs this before it changes anything, so each file
# goes back to its own path first.
#
# The game folder comes from GAME_PATH, which find-game.ps1 set, rather than an
# argument: a quoted argument ending in a backslash, as a drive root does, would
# reach this script with a quote stuck on the end.
#
# Exit 0: no folder, or every file went back and the folder is gone.
# Exit 1: a file is already at the path a kept one belongs at. Nothing is moved
# or deleted, and both paths are named.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not $env:GAME_PATH) { throw 'GAME_PATH is not set.' }
$game = [IO.Path]::GetFullPath($env:GAME_PATH).TrimEnd('\')
$kept = Join-Path $game 'CameraUnlock-kept-configs'
if (-not (Test-Path -LiteralPath $kept -PathType Container)) { exit 0 }

$files = @(Get-ChildItem -LiteralPath $kept -Recurse -Force -File | ForEach-Object {
    $rel = $_.FullName.Substring($kept.Length + 1)
    [pscustomobject]@{ Rel = $rel; Held = $_.FullName; Live = Join-Path $game $rel }
})

$conflicts = @($files | Where-Object { Test-Path -LiteralPath $_.Live })
if ($conflicts.Count -gt 0) {
    Write-Host "ERROR: $kept holds config files an earlier uninstall set aside"
    Write-Host 'and did not put back, and a file is already in place for:'
    foreach ($c in $conflicts) {
        Write-Host "  set aside: $($c.Held)"
        Write-Host "  in place:  $($c.Live)"
    }
    Write-Host 'Nothing was changed. Keep the copy you want at the in-place path, delete'
    Write-Host 'the other one, and run the installer or uninstaller again: it puts back'
    Write-Host 'whatever is left first.'
    exit 1
}

if ($files.Count -gt 0) {
    Write-Host "Putting back config files an earlier uninstall left in ${kept}:"
}
foreach ($f in $files) {
    [IO.Directory]::CreateDirectory((Split-Path $f.Live)) | Out-Null
    [IO.File]::Move($f.Held, $f.Live)
    Write-Host "  Restored: $($f.Rel)"
}

# Deepest first, and never recursive: the folder can only be emptied of the
# subfolders the moves left behind, so a file that appeared since the scan
# stops the removal instead of being deleted with it.
$dirs = @(Get-ChildItem -LiteralPath $kept -Recurse -Force -Directory | Sort-Object { $_.FullName.Length } -Descending)
foreach ($d in $dirs) { [IO.Directory]::Delete($d.FullName) }
[IO.Directory]::Delete($kept)
if ($files.Count -gt 0) { Write-Host '' }
exit 0
