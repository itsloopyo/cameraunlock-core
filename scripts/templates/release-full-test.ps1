# The full-suite gate of a mod's scripts/release.ps1. Paste it after the clean
# tree check and the version resolution, before the first line that edits a file
# (the changelog, the version bump), so a failure leaves nothing to undo.
# $ProjectRoot is the repo root; use the script's own name for it.
#
# A push build runs only the fast tests (package depends on test-unit where the
# repo has a config differential test). This is where the author finds out that
# the whole suite passes before a tag is pushed, rather than from the tag build.
# conformance.ps1's ci-minutes check fails a release.ps1 with no live
# `pixi run test`.

Write-Host "Running the full test suite..." -ForegroundColor Cyan
Push-Location $ProjectRoot
try {
    pixi run test
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error: pixi run test failed. Nothing was changed." -ForegroundColor Red
        exit 1
    }
} finally {
    Pop-Location
}
