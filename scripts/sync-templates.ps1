#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# cameraunlock-core/scripts/sync-templates.ps1
# ============================================================================
# Push core's templates back out across the fleet, and report where a repo has
# drifted off them.
#
# This is the other half of scripts/conformance.ps1. The audit behind both
# found one thing worth generalising: what core PUSHES with a script stays
# identical everywhere - the three Discord workflow blocks sync-discord-
# announce.mjs writes are byte-identical in every repo - while what core
# PUBLISHES as a template drifts, because publishing puts the work of staying
# current on 50 repos instead of one. So anything that can be pushed is pushed
# here rather than documented as a convention.
#
#   pwsh scripts/sync-templates.ps1 -All                 # report drift
#   pwsh scripts/sync-templates.ps1 -All -Apply          # write it
#   pwsh scripts/sync-templates.ps1 -Repo valheim -Only wrappers
#
# Report is the default. -Apply writes files and nothing else: it never stages,
# commits or pushes, because a template change needs a human reading the diff
# in each repo before it becomes that repo's history.
#
# What is pushed, and what is only reported:
#
#   wrappers      Pushed. Everything below `--- END CONFIG BLOCK ---` in a thin
#                 install.cmd / uninstall.cmd is core's, so it is replaced from
#                 scripts/templates/. A repo still carrying a legacy in-tree
#                 body is reported: converting it means writing a CONFIG BLOCK
#                 out of a 300-line script, which is a per-repo judgement.
#
#   update-deps   Pushed, narrowly. The header of each update-deps.ps1 carries
#                 real per-repo reasoning (firewatch pins MelonLoader to v0.5.x
#                 because v0.6 crashes on Unity 2017 Mono) and the CALL BLOCK is
#                 the mod's own vendoring. Only the mechanical middle - strict
#                 mode, the module resolution, the import - comes from the
#                 template, so only that is replaced.
#
#   action-pins   Pushed. Every `uses:` is rewritten to the SHA and version
#                 comment in scripts/templates/action-pins.json. An action with
#                 no entry there is reported, never pinned to a SHA nobody
#                 verified.
#
#   pixi-tasks    `sync` is pushed; the rest is reported with the exact line to
#                 add. validate-manifest and validate-notices need `nodejs` in
#                 the repo's environment, which no mod repo has, so writing the
#                 task alone would add one that cannot run. The other gaps
#                 (test, package, release-nightly) are repo-specific commands
#                 and there is nothing correct to write.
# ============================================================================

[CmdletBinding()]
param(
    [string[]]$Repo,
    [switch]$All,
    [ValidateSet('wrappers', 'update-deps', 'action-pins', 'pixi-tasks')]
    [string[]]$Only,
    [switch]$Apply
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$CoreRoot = Split-Path -Parent $PSScriptRoot
$ReposRoot = Split-Path -Parent $CoreRoot
$TemplateDir = Join-Path $CoreRoot 'scripts/templates'

$UNITS = @('wrappers', 'update-deps', 'action-pins', 'pixi-tasks')
$selected = @($Only | Where-Object { $_ })
if ($selected.Count -eq 0) { $selected = $UNITS }

$CANONICAL_SYNC_TASK = 'sync = "git submodule update --remote cameraunlock-core"'
$REPORT_ONLY_TASKS = [ordered]@{
    'validate-manifest' = 'validate-manifest = "node cameraunlock-core/scripts/validate-manifest.mjs"   (also needs nodejs in [dependencies])'
    'validate-notices'  = 'validate-notices = "node cameraunlock-core/scripts/validate-notices.mjs"     (also needs nodejs in [dependencies])'
    'test'              = 'test = ...            repo-specific; a documented no-op stub beats absence'
    'package'           = 'package = ...         repo-specific'
    'release-nightly'   = 'release-nightly = ... repo-specific'
    'setup'             = 'setup = ...           repo-specific'
    'update-deps'       = 'update-deps = "powershell -ExecutionPolicy Bypass -File scripts/update-deps.ps1"'
    'build'             = 'build = ...           repo-specific'
    'install'           = 'install = ...         repo-specific'
    'uninstall'         = 'uninstall = "scripts\\uninstall.cmd"'
    'release'           = 'release = "powershell -ExecutionPolicy Bypass -File scripts/release.ps1"'
    'clean'             = 'clean = ...           repo-specific'
}

$actionPins = (Get-Content -LiteralPath (Join-Path $TemplateDir 'action-pins.json') -Raw | ConvertFrom-Json).pins

$results = New-Object System.Collections.Generic.List[object]

function Add-Result {
    param(
        [string]$RepoName,
        [string]$Unit,
        [ValidateSet('drift', 'written', 'report', 'skip')][string]$State,
        [string]$Detail
    )
    $results.Add([pscustomobject]@{ repo = $RepoName; unit = $Unit; state = $State; detail = $Detail })
}

function Read-TextFile { param([string]$Path) return [System.IO.File]::ReadAllText($Path) }

# .cmd must be CRLF or cmd.exe fails on it without saying so; everything else
# in these repos is LF. Write each file the way its own kind is declared in
# .gitattributes rather than inheriting whatever the host newline is.
function Write-TextFile {
    param([string]$Path, [string]$Text, [switch]$Crlf)
    $normalised = $Text -replace "`r`n", "`n"
    if ($Crlf) { $normalised = $normalised -replace "`n", "`r`n" }
    [System.IO.File]::WriteAllText($Path, $normalised, (New-Object System.Text.UTF8Encoding($false)))
}

function Split-AtMarker {
    param([string]$Text, [string]$Marker)
    $lines = ($Text -replace "`r`n", "`n") -split "`n"
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match $Marker) {
            return @{
                Head = if ($i -eq 0) { @() } else { $lines[0..($i - 1)] }
                Tail = $lines[$i..($lines.Count - 1)]
            }
        }
    }
    return $null
}

# ---------------------------------------------------------------------------

function Sync-Wrappers {
    param([string]$Name, [string]$Root)

    foreach ($script in @('install.cmd', 'uninstall.cmd')) {
        $path = Join-Path $Root "scripts/$script"
        if (-not (Test-Path $path)) {
            Add-Result $Name 'wrappers' 'skip' "no scripts/$script"
            continue
        }
        $text = Read-TextFile $path
        # Anchored to the `set "_BODY=..."` dispatch line, not to any mention of
        # a body filename. Everything below END CONFIG BLOCK is replaced on the
        # strength of this match, so a bespoke installer that merely names a body
        # in a comment would have its whole body deleted.
        if ($text -notmatch '(?m)^\s*set "_BODY=[^"]*?((?:un)?install-body[a-z0-9-]*\.cmd)"') {
            Add-Result $Name 'wrappers' 'report' "scripts/$script is a legacy in-tree body; converting it to a wrapper needs a CONFIG BLOCK written by hand, then scripts/templates/install-wrapper-*.cmd for the rest"
            continue
        }
        $body = $Matches[1]
        $templateName = if ($script -eq 'uninstall.cmd') {
            'uninstall-wrapper.cmd'
        } else {
            "install-wrapper-$($body -replace '^install-body-|\.cmd$', '').cmd"
        }
        $templatePath = Join-Path $TemplateDir $templateName
        if (-not (Test-Path $templatePath)) {
            Add-Result $Name 'wrappers' 'report' "scripts/$script dispatches to $body, for which core publishes no $templateName"
            continue
        }

        $mine = Split-AtMarker $text 'END CONFIG BLOCK'
        $theirs = Split-AtMarker (Read-TextFile $templatePath) 'END CONFIG BLOCK'
        if (-not $mine) {
            Add-Result $Name 'wrappers' 'report' "scripts/$script has no END CONFIG BLOCK marker, so nothing separates its config from the shared tail"
            continue
        }
        $currentTail = (($mine.Tail | ForEach-Object { $_.TrimEnd() }) -join "`n").TrimEnd()
        $wantedTail = (($theirs.Tail | ForEach-Object { $_.TrimEnd() }) -join "`n").TrimEnd()
        if ($currentTail -eq $wantedTail) { continue }

        if (-not $Apply) {
            Add-Result $Name 'wrappers' 'drift' "scripts/$script tail differs from templates/$templateName"
            continue
        }
        # Trailing newline: the tails are compared TrimEnd'd, because git and
        # editors touch that byte and cmd.exe does not care about it. Writing
        # the trimmed form back is what put 64 of 85 fleet install.cmd files one
        # byte short of their template, so every later diff of them carried a
        # `\ No newline at end of file` nobody meant.
        Write-TextFile -Path $path -Crlf -Text (((@($mine.Head) + @($wantedTail)) -join "`n") + "`n")
        Add-Result $Name 'wrappers' 'written' "scripts/$script tail restored from templates/$templateName"
    }
}

function Sync-UpdateDeps {
    param([string]$Name, [string]$Root)

    $path = Join-Path $Root 'scripts/update-deps.ps1'
    if (-not (Test-Path $path)) {
        Add-Result $Name 'update-deps' 'report' 'no scripts/update-deps.ps1, so vendored loaders can only be bumped by hand'
        return
    }

    $template = Read-TextFile (Join-Path $TemplateDir 'update-deps.ps1')
    $wanted = Get-MechanicalPrologue $template
    $mine = Read-TextFile $path
    $current = Get-MechanicalPrologue $mine
    if (-not $current) {
        Add-Result $Name 'update-deps' 'report' 'scripts/update-deps.ps1 has no Set-StrictMode..Import-Module prologue to replace; its shape is its own'
        return
    }
    if ($current.Text -eq $wanted.Text) { return }

    if (-not $Apply) {
        Add-Result $Name 'update-deps' 'drift' 'scripts/update-deps.ps1 resolves ModLoaderSetup.psm1 its own way; the template tries both submodule layouts'
        return
    }
    $rebuilt = $mine.Substring(0, $current.Start) + $wanted.Text + $mine.Substring($current.Start + $current.Length)
    Write-TextFile -Path $path -Text $rebuilt
    Add-Result $Name 'update-deps' 'written' 'scripts/update-deps.ps1 prologue restored from templates/update-deps.ps1'
}

# The part of update-deps.ps1 that is core's: strict mode through the module
# import. The header above it carries per-repo reasoning and the CALL BLOCK
# below it is the mod's own vendoring, so neither is touched.
function Get-MechanicalPrologue {
    param([string]$Text)
    $m = [regex]::Match($Text, '(?s)Set-StrictMode -Version Latest.*?Import-Module \$module(?:Path)? -Force')
    if (-not $m.Success) { return $null }
    return @{ Text = $m.Value; Start = $m.Index; Length = $m.Length }
}

function Sync-ActionPins {
    param([string]$Name, [string]$Root)

    $dir = Join-Path $Root '.github/workflows'
    if (-not (Test-Path $dir)) { return }

    foreach ($wf in Get-ChildItem -Path $dir -File | Where-Object { $_.Extension -in '.yml', '.yaml' }) {
        $lines = [System.IO.File]::ReadAllLines($wf.FullName)
        $changed = $false
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($lines[$i] -notmatch '^(\s*(?:-\s+)?uses:\s*)(\S+?)@(\S+)(.*)$') { continue }
            $prefix = $Matches[1]
            $action = $Matches[2]
            $ref = $Matches[3]
            if ($action.StartsWith('./') -or $action.StartsWith('docker://')) { continue }
            # A reusable workflow reference names a path inside the repo; its ref
            # is a release decision, not an action pin.
            if ($action -match '/\.github/workflows/') { continue }

            $pin = $actionPins.PSObject.Properties | Where-Object { $_.Name -eq $action } | Select-Object -First 1
            if (-not $pin) {
                if ($ref -notmatch '^[0-9a-f]{40}$') {
                    Add-Result $Name 'action-pins' 'report' "$($wf.Name):$($i + 1) pins $action to the mutable tag $ref and action-pins.json records no canonical SHA for it"
                }
                continue
            }
            $want = "$prefix$action@$($pin.Value.sha) # $($pin.Value.version)"
            if ($lines[$i] -eq $want) { continue }

            if (-not $Apply) {
                $how = if ($ref -match '^[0-9a-f]{40}$') { "a different SHA ($($ref.Substring(0, 8)))" } else { "the mutable tag $ref" }
                Add-Result $Name 'action-pins' 'drift' "$($wf.Name):$($i + 1) pins $action to $how; canonical is $($pin.Value.sha.Substring(0, 8)) # $($pin.Value.version)"
                continue
            }
            $lines[$i] = $want
            $changed = $true
        }
        if ($changed) {
            Write-TextFile -Path $wf.FullName -Text (($lines -join "`n") + "`n")
            Add-Result $Name 'action-pins' 'written' "$($wf.Name) pins normalised to action-pins.json"
        }
    }
}

function Sync-PixiTasks {
    param([string]$Name, [string]$Root)

    $path = Join-Path $Root 'pixi.toml'
    if (-not (Test-Path $path)) {
        Add-Result $Name 'pixi-tasks' 'report' 'no pixi.toml'
        return
    }
    $text = Read-TextFile $path
    $lines = ($text -replace "`r`n", "`n") -split "`n"

    $declared = New-Object System.Collections.Generic.HashSet[string]
    $tasksAt = -1
    $inTasks = $false
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $trimmed = $lines[$i].Trim()
        if ($trimmed -match '^\[([^\]]+)\]') {
            $section = $Matches[1]
            if ($section -match '(^|\.)tasks\.(.+)$') {
                $inTasks = $false
                [void]$declared.Add($Matches[2].Trim('"').Trim("'"))
            } else {
                $inTasks = $section -match '(^|\.)tasks$'
                if ($inTasks -and $tasksAt -lt 0) { $tasksAt = $i }
            }
            continue
        }
        if ($inTasks -and $trimmed -match '^["'']?([A-Za-z0-9_.-]+)["'']?\s*=') { [void]$declared.Add($Matches[1]) }
    }

    foreach ($task in $REPORT_ONLY_TASKS.Keys) {
        if ($declared.Contains($task)) { continue }
        Add-Result $Name 'pixi-tasks' 'report' "no '$task' task; add: $($REPORT_ONLY_TASKS[$task])"
    }

    if ($declared.Contains('sync')) { return }
    if ($tasksAt -lt 0) {
        Add-Result $Name 'pixi-tasks' 'report' "no [tasks] table to add 'sync' to"
        return
    }
    if (-not $Apply) {
        Add-Result $Name 'pixi-tasks' 'drift' "no 'sync' task, and every update-deps.ps1 tells the user to run one on failure"
        return
    }
    $rebuilt = @($lines[0..$tasksAt]) + @($CANONICAL_SYNC_TASK) + @($lines[($tasksAt + 1)..($lines.Count - 1)])
    Write-TextFile -Path $path -Text ($rebuilt -join "`n")
    Add-Result $Name 'pixi-tasks' 'written' "added: $CANONICAL_SYNC_TASK"
}

# ---------------------------------------------------------------------------

$UNIT_TABLE = [ordered]@{
    'wrappers'    = ${function:Sync-Wrappers}
    'update-deps' = ${function:Sync-UpdateDeps}
    'action-pins' = ${function:Sync-ActionPins}
    'pixi-tasks'  = ${function:Sync-PixiTasks}
}

function Resolve-RepoPath {
    param([string]$Token)
    foreach ($candidate in @($Token, (Join-Path $ReposRoot $Token), (Join-Path $ReposRoot "$Token-headtracking"), (Join-Path $ReposRoot "$Token-head-tracking"))) {
        if (Test-Path -LiteralPath $candidate -PathType Container) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    throw "No repo found for '$Token' - tried it as a path and under $ReposRoot."
}

if ($All) {
    if ($Repo) { throw '-All and -Repo are mutually exclusive.' }
    # Same selection as conformance.ps1 and sync-core-notices.ps1: a git
    # checkout that either vendors this core or is named like a head-tracking
    # mod. Vendoring alone missed homeworld-remastered-collection and
    # kingdom-come-deliverance-2, which do not carry the `-headtracking`
    # suffix; the name check alone misses a mod repo with no core checkout at
    # all (baldurs-gate-3-headtracking and eight more). Each sync unit already
    # reports/skips gracefully when the file it needs (scripts/*.cmd,
    # .github/workflows, pixi.toml) is not there, so widening this filter does
    # not need any unit to change.
    $roots = @(Get-ChildItem -Path $ReposRoot -Directory |
        Where-Object {
            $_.FullName -ne $CoreRoot -and
            (Test-Path (Join-Path $_.FullName '.git')) -and
            ((Test-Path (Join-Path $_.FullName 'cameraunlock-core')) -or ($_.Name -match '-head-?tracking$'))
        } |
        Select-Object -ExpandProperty FullName)
} elseif ($Repo) {
    $roots = @($Repo | ForEach-Object { Resolve-RepoPath $_ })
} else {
    $roots = @([System.IO.Path]::GetFullPath((Join-Path $CoreRoot '..')))
}
if ($roots.Count -eq 0) { throw "No repos to sync under $ReposRoot." }

foreach ($root in $roots) {
    $name = Split-Path -Leaf $root
    foreach ($unit in $selected) { & $UNIT_TABLE[$unit] $name $root }
}

$byRepo = $results | Group-Object repo
foreach ($root in $roots) {
    $name = Split-Path -Leaf $root
    $group = $byRepo | Where-Object { $_.Name -eq $name }
    if (-not $group) {
        Write-Host "in step  $name" -ForegroundColor DarkGray
        continue
    }
    Write-Host ''
    Write-Host $name -ForegroundColor Cyan
    foreach ($r in ($group.Group | Sort-Object unit, state)) {
        $colour = switch ($r.state) { 'written' { 'Green' } 'drift' { 'Yellow' } 'report' { 'Gray' } default { 'DarkGray' } }
        Write-Host ("  {0,-8} {1,-12} {2}" -f $r.state, $r.unit, $r.detail) -ForegroundColor $colour
    }
}

$drift = @($results | Where-Object { $_.state -eq 'drift' })
$written = @($results | Where-Object { $_.state -eq 'written' })
Write-Host ''
if ($Apply) {
    Write-Host "$($roots.Count) repos, $($written.Count) file(s) rewritten. Nothing was staged or committed - read each diff, then commit per repo."
} else {
    Write-Host "$($roots.Count) repos, $($drift.Count) fixable drift(s). Re-run with -Apply to write them."
}
exit 0
