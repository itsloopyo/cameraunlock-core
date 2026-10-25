# The mod repos the fleet scripts (conformance.ps1, sync-templates.ps1,
# sync-core-notices.ps1) work on. Dot-source it.

# Every git checkout beside core that either vendors this core or is named like
# a head-tracking mod. Neither signal alone is enough: vendoring alone missed
# homeworld-remastered-collection and kingdom-come-deliverance-2, which pin core
# without the `-headtracking` suffix, and the name alone misses a mod repo with
# no core checkout yet (baldurs-gate-3-headtracking and others). lopari,
# headcam and quickfeed have neither, and none of the fleet's rules applies to
# them. A check or sync unit that needs something such a repo lacks skips or
# warns, which is what makes it safe to widen the selection instead of the
# checks.
function Get-FleetModRepos {
    param(
        [Parameter(Mandatory)][string]$ReposRoot,
        [Parameter(Mandatory)][string]$CoreRoot
    )
    @(Get-ChildItem -Path $ReposRoot -Directory |
        Where-Object {
            $_.FullName -ne $CoreRoot -and
            (Test-Path (Join-Path $_.FullName '.git')) -and
            ((Test-Path (Join-Path $_.FullName 'cameraunlock-core')) -or ($_.Name -match '-head-?tracking$'))
        } |
        Select-Object -ExpandProperty FullName)
}

# A repo named on the command line: a path, a folder under ReposRoot, or that
# folder without its -headtracking or -head-tracking suffix.
function Resolve-RepoPath {
    param(
        [Parameter(Mandatory)][string]$Token,
        [Parameter(Mandatory)][string]$ReposRoot
    )
    foreach ($candidate in @($Token, (Join-Path $ReposRoot $Token), (Join-Path $ReposRoot "$Token-headtracking"), (Join-Path $ReposRoot "$Token-head-tracking"))) {
        if (Test-Path -LiteralPath $candidate -PathType Container) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    throw "No repo found for '$Token' - tried it as a path and under $ReposRoot."
}
