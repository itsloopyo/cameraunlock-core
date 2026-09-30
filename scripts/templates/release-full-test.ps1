# The full-suite gate of a mod's scripts/release.ps1. Paste it after the clean
# tree check and the version resolution, before the first line that edits a file
# (the changelog, the version bump), so a failure leaves nothing to undo.
# $ProjectRoot is the repo root; use the script's own name for it. The script
# must already import ReleaseWorkflow.psm1.
#
# A push build runs only the fast tests (package depends on test-unit where the
# repo has a config differential test). This is where the author finds out that
# the whole suite passes before a tag is pushed, rather than from the tag build.
#
# Invoke-ReleaseTestSuite runs `pixi run test` in a repo without
# tests/config_differential. In one with it, it runs test-unit and then the
# differential, unless the pass recorded in tests/config_differential/passed.json
# still holds for every file the differential depends on (DifferentialGate.psm1).
# A pass it records is committed by Invoke-VersionCommit, and the tag build reads
# the same record. conformance.ps1's ci-minutes check fails a release.ps1 that
# runs neither this nor `pixi run test`.

Write-Host "Running the full test suite..." -ForegroundColor Cyan
try {
    Invoke-ReleaseTestSuite -ProjectRoot $ProjectRoot
} catch {
    Write-Host "Error: $($_.Exception.Message). Nothing was changed." -ForegroundColor Red
    exit 1
}
