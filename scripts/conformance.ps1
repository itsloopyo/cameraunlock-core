#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# cameraunlock-core/scripts/conformance.ps1
# ============================================================================
# Lint a head-tracking mod repo, or the whole fleet, against the invariants
# core actually owns.
#
# The fleet audit that produced this script found one thing worth building on:
# what core PUSHES with a script stays identical everywhere, and what core
# PUBLISHES as a template drifts. The three workflow blocks sync-discord-
# announce.mjs writes are byte-identical in 49 of 49 repos; install.cmd matches
# its template in 3 of 48 and update-deps.ps1 in 0 of 44. Every check here is a
# thing that was measured wrong somewhere, not a thing that might go wrong.
#
#   pwsh scripts/conformance.ps1                  # the repo vendoring this core
#   pwsh scripts/conformance.ps1 -All             # every sibling mod repo
#   pwsh scripts/conformance.ps1 -Repo valheim subnautica
#   pwsh scripts/conformance.ps1 -All -Json       # findings as JSON on stdout
#   pwsh scripts/conformance.ps1 -All -Check install-wrapper,action-pins
#
# Exit 0 when nothing failed, 1 when anything did. Warnings never fail the run:
# a warning is something to decide about, a failure is something that is broken
# for a user today.
#
# Fixing is a separate job. This reports; scripts/sync-templates.ps1 pushes the
# template-shaped fixes back out.
# ============================================================================

[CmdletBinding()]
param(
    # Repo paths or bare tokens (valheim, valheim-headtracking). Defaults to
    # the repo that vendors this core checkout.
    [string[]]$Repo,
    # Every sibling head-tracking mod repo that vendors this core.
    [switch]$All,
    # Limit to these check ids. Run with no repos to list them.
    [string[]]$Check,
    # Emit findings as JSON instead of a report.
    [switch]$Json,
    # A core pin older than this is stale: the mod ships shared install bodies,
    # find-game.ps1 and games.json from whatever commit it pins.
    [int]$MaxCorePinAgeDays = 45
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$CoreRoot = Split-Path -Parent $PSScriptRoot
$ReposRoot = Split-Path -Parent $CoreRoot
Import-Module (Join-Path $CoreRoot 'powershell/ReleaseWorkflow.psm1') -Force

# The one SHA the fleet is meant to agree on per action. Bumping is a deliberate
# edit to that file; sync-templates.ps1 is what pushes it out.
$ACTION_PINS = (Get-Content -LiteralPath (Join-Path $CoreRoot 'scripts/templates/action-pins.json') -Raw | ConvertFrom-Json).pins

$CHECK_IDS = @(
    'install-wrapper', 'delayed-expansion', 'arg-parser', 'cmd-crlf', 'pixi-tasks',
    'action-pins', 'workflow-ref', 'workflow-build', 'core-pin', 'manifest',
    'stray-manifest', 'license', 'readme'
)

# Every task a mod's tooling, its docs or another mod's error message assumes
# exists. A documented no-op stub counts: `update-deps.ps1` tells the user to
# "Run 'pixi run sync'", so a repo without a `sync` task prints a recovery
# instruction that fails.
$CANONICAL_TASKS = @(
    'sync', 'setup', 'build', 'test', 'package', 'validate-manifest',
    'validate-notices', 'install', 'uninstall', 'update-deps', 'release',
    'release-nightly', 'clean'
)

$CANONICAL_README_HEADINGS = @(
    'Features', 'Requirements', 'Installation', 'Setting Up OpenTrack', 'Controls',
    'Configuration', 'Troubleshooting', 'Updating', 'Uninstalling',
    'Building from Source', 'Community & Support', 'License', 'Credits', 'Disclaimer'
)

# Claims about kit we do not own and have not tested. AGENTS.md forbids both
# shapes: an assertion about how someone's hardware behaves, and an "all X do Y"
# generalisation. Both read as authoritative, and both come back as bug reports
# from the user whose tracker does not do that.
$README_BANNED = @(
    @{ Pattern = 'any\s+OpenTrack[- ]compatible'; Why = 'claims every OpenTrack-compatible tracker works; we have tested some' }
    @{ Pattern = 'any\s+phone\s+tracker';         Why = 'claims every phone tracker works; phone trackers do not share one protocol' }
    @{ Pattern = 'all\s+\w+\s+(trackers|apps|headsets)\s+(speak|use|support|are|do|send)'; Why = 'an "all X do Y" generalisation about third-party kit' }
    @{ Pattern = 'every\s+(phone|tracker|headset|app)\s+(speaks|uses|supports|sends)';     Why = 'an "all X do Y" generalisation about third-party kit' }
    @{ Pattern = "(?i)it's not \w+,?\s+it's";     Why = 'the "not X, it''s Y" construction AGENTS.md bans' }
    @{ Pattern = [char]0x2014;                    Why = 'em-dash' }
)

# Build tools a workflow must not invoke directly. CI has to go through the same
# `pixi run` a developer runs, or the two builds are free to drift and the drift
# is only ever found by a release that fails.
$INLINE_BUILD_TOOLS = 'dotnet\s+(build|publish|pack)|msbuild|cmake|cargo\s+(build|rustc)|xmake|meson|ninja'

$findings = New-Object System.Collections.Generic.List[object]

function Add-Finding {
    param(
        [Parameter(Mandatory = $true)][string]$RepoName,
        [Parameter(Mandatory = $true)][string]$CheckId,
        [Parameter(Mandatory = $true)][ValidateSet('FAIL', 'WARN')][string]$Severity,
        [Parameter(Mandatory = $true)][string]$Message
    )
    $findings.Add([pscustomobject]@{
        repo     = $RepoName
        check    = $CheckId
        severity = $Severity
        message  = $Message
    })
}

function Read-TextFile {
    param([string]$Path)
    # -Raw keeps the file's own line endings, which several checks are about.
    return [System.IO.File]::ReadAllText($Path)
}

# Everything from the CONFIG BLOCK terminator to the end of file: the part a mod
# is not allowed to edit. Trailing whitespace and the final newline are
# normalised away because git, editors and Compress-Archive all touch them and
# none of it changes what cmd.exe does.
function Get-ScriptTail {
    param([string]$Text)
    $lines = ($Text -replace "`r`n", "`n") -split "`n"
    $start = -1
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match 'END CONFIG BLOCK') { $start = $i; break }
    }
    if ($start -lt 0) { return $null }
    $tail = $lines[$start..($lines.Count - 1)] | ForEach-Object { $_.TrimEnd() }
    return (($tail -join "`n").TrimEnd())
}

function Get-LauncherManifest {
    param([string]$RepoRoot)
    $path = Join-Path $RepoRoot 'launcher-manifest.json'
    if (-not (Test-Path $path)) { return $null }
    try { return (Read-TextFile $path).TrimStart([char]0xFEFF) | ConvertFrom-Json } catch { return $null }
}

function Get-WrapperBodyName {
    param([string]$Text)
    if ($Text -match '((?:un)?install-body[a-z-]*\.cmd)') { return $Matches[1] }
    return $null
}

function Get-PixiTaskNames {
    param([string]$Path)
    $names = New-Object System.Collections.Generic.HashSet[string]
    $inTasks = $false
    foreach ($line in [System.IO.File]::ReadAllLines($Path)) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^\[([^\]]+)\]') {
            $section = $Matches[1]
            # [tasks], [feature.x.tasks], [target.win-64.tasks] all declare tasks;
            # [tasks.name] declares exactly one.
            if ($section -match '(^|\.)tasks\.(.+)$') {
                $inTasks = $false
                [void]$names.Add($Matches[2].Trim('"').Trim("'"))
            } else {
                $inTasks = $section -match '(^|\.)tasks$'
            }
            continue
        }
        if (-not $inTasks) { continue }
        if ($trimmed -match '^["'']?([A-Za-z0-9_.-]+)["'']?\s*=') { [void]$names.Add($Matches[1]) }
    }
    return $names
}

function Get-WorkflowFiles {
    param([string]$RepoRoot)
    $dir = Join-Path $RepoRoot '.github/workflows'
    if (-not (Test-Path $dir)) { return @() }
    return @(Get-ChildItem -Path $dir -File | Where-Object { $_.Extension -in '.yml', '.yaml' })
}

# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------

function Test-InstallWrapper {
    param([string]$Name, [string]$Root)

    $manifest = Get-LauncherManifest $Root
    $deliveryMode = if ($manifest -and $manifest.PSObject.Properties.Name -contains 'delivery_mode') { $manifest.delivery_mode } else { $null }

    foreach ($pair in @(
            @{ Script = 'install.cmd';   Template = { param($body) "install-wrapper-$($body -replace '^install-body-|\.cmd$', '').cmd" } },
            @{ Script = 'uninstall.cmd'; Template = { param($body) 'uninstall-wrapper.cmd' } })) {

        $path = Join-Path $Root "scripts/$($pair.Script)"
        if (-not (Test-Path $path)) {
            # Not every mod delivers through a batch script: a Fabric or
            # manifest-mode mod is deployed by the loader or by lopari, and
            # inventing an install.cmd for it would ship a path nothing runs.
            $severity = if ($deliveryMode -eq 'install_cmd') { 'FAIL' } else { 'WARN' }
            Add-Finding $Name 'install-wrapper' $severity "scripts/$($pair.Script) is missing (delivery_mode $(if ($deliveryMode) { $deliveryMode } else { 'undeclared' }))"
            continue
        }

        $text = Read-TextFile $path
        $body = Get-WrapperBodyName $text
        if (-not $body) {
            Add-Finding $Name 'install-wrapper' 'WARN' "scripts/$($pair.Script) is a legacy in-tree body, not a wrapper - a fix to the shared body never reaches it"
            continue
        }

        $templateName = & $pair.Template $body
        $templatePath = Join-Path $CoreRoot "scripts/templates/$templateName"
        if (-not (Test-Path $templatePath)) {
            Add-Finding $Name 'install-wrapper' 'FAIL' "scripts/$($pair.Script) dispatches to $body, for which core publishes no $templateName"
            continue
        }

        $mine = Get-ScriptTail $text
        $theirs = Get-ScriptTail (Read-TextFile $templatePath)
        if ($null -eq $mine) {
            Add-Finding $Name 'install-wrapper' 'FAIL' "scripts/$($pair.Script) has no END CONFIG BLOCK marker, so nothing separates per-repo config from the shared tail"
        } elseif ($mine -ne $theirs) {
            Add-Finding $Name 'install-wrapper' 'FAIL' "scripts/$($pair.Script) has edits below the CONFIG BLOCK; it no longer matches scripts/templates/$templateName"
        }
    }
}

function Test-DelayedExpansion {
    param([string]$Name, [string]$Root)

    foreach ($script in @('install.cmd', 'uninstall.cmd')) {
        $path = Join-Path $Root "scripts/$script"
        if (-not (Test-Path $path)) { continue }
        $text = Read-TextFile $path
        # A wrapper parses nothing; the shared body it calls pins expansion off
        # at its own outer scope, and that body is core's.
        if (Get-WrapperBodyName $text) { continue }

        $lines = ($text -replace "`r`n", "`n") -split "`n"
        $off = -1; $on = -1; $argsDone = -1
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($off -lt 0 -and $lines[$i] -match '^\s*setlocal\s+disabledelayedexpansion') { $off = $i }
            if ($on -lt 0 -and $lines[$i] -match '^\s*setlocal\s+.*enabledelayedexpansion') { $on = $i }
            if ($argsDone -lt 0 -and $lines[$i] -match '^\s*:args_done\b') { $argsDone = $i }
        }

        if ($off -lt 0) {
            Add-Finding $Name 'delayed-expansion' 'FAIL' "scripts/$script never pins ``setlocal disabledelayedexpansion`` at outer scope, so a game path containing ! is silently mangled and rejected with exit 2"
        }
        if ($on -ge 0 -and $argsDone -ge 0 -and $on -lt $argsDone) {
            Add-Finding $Name 'delayed-expansion' 'FAIL' "scripts/$script enables delayed expansion at line $($on + 1), before :args_done at line $($argsDone + 1); the arg parser then eats ! out of the game path"
        }
        if ($on -ge 0 -and $off -ge 0 -and $on -lt $off) {
            Add-Finding $Name 'delayed-expansion' 'FAIL' "scripts/$script enables delayed expansion at line $($on + 1) before disabling it at line $($off + 1)"
        }
    }
}

function Test-ArgParser {
    param([string]$Name, [string]$Root)

    foreach ($script in @('install.cmd', 'uninstall.cmd')) {
        $path = Join-Path $Root "scripts/$script"
        if (-not (Test-Path $path)) { continue }
        $text = Read-TextFile $path
        # A wrapper forwards %* verbatim; the parser under test is the body's.
        if (Get-WrapperBodyName $text) { continue }

        $required = @('/y', '-y', '--yes')
        if ($script -eq 'uninstall.cmd') { $required += '/force' }
        foreach ($flag in $required) {
            if ($text -notmatch [regex]::Escape("`"$flag`"")) {
                Add-Finding $Name 'arg-parser' 'FAIL' "scripts/$script does not accept $flag; lopari drives these installs programmatically"
            }
        }
        if ($text -notmatch 'exit /b 2') {
            Add-Finding $Name 'arg-parser' 'FAIL' "scripts/$script never exits 2, so an unknown argument is indistinguishable from a user-fixable failure"
        }
    }
}

function Test-CmdCrlf {
    param([string]$Name, [string]$Root)

    $attributes = Join-Path $Root '.gitattributes'
    if (-not (Test-Path $attributes)) {
        Add-Finding $Name 'cmd-crlf' 'FAIL' 'no .gitattributes, so *.cmd line endings depend on whoever cloned it'
    } elseif ((Read-TextFile $attributes) -notmatch '\*\.cmd\s+text\s+eol=crlf') {
        Add-Finding $Name 'cmd-crlf' 'FAIL' '.gitattributes does not pin `*.cmd text eol=crlf`'
    }

    $eol = & git -C $Root ls-files --eol -- '*.cmd' 2>$null
    if ($LASTEXITCODE -ne 0) { return }
    foreach ($line in @($eol)) {
        if ($line -match '^\S*\s+w/lf\s+.*?\t(.+)$') {
            Add-Finding $Name 'cmd-crlf' 'FAIL' "$($Matches[1]) is LF in the working tree; cmd.exe fails on it silently"
        }
    }
}

# Does anything in the repo instruct a user to run this task? Only scripts/ and
# the README, so a task named in passing inside the vendored core does not count.
function Test-TaskIsReferenced {
    param([string]$Root, [string]$Task)
    $needle = "pixi run $Task"
    foreach ($file in @(Get-ChildItem -Path (Join-Path $Root 'scripts') -File -ErrorAction SilentlyContinue) + @(Get-Item -Path (Join-Path $Root 'README.md') -ErrorAction SilentlyContinue)) {
        if ($file.Extension -notin '.ps1', '.cmd', '.md', '.mjs', '.js', '.py') { continue }
        if ((Read-TextFile $file.FullName) -like "*$needle*") { return $true }
    }
    return $false
}

function Test-PixiTasks {
    param([string]$Name, [string]$Root)

    $pixi = Join-Path $Root 'pixi.toml'
    if (-not (Test-Path $pixi)) {
        Add-Finding $Name 'pixi-tasks' 'FAIL' 'no pixi.toml'
        return
    }
    $tasks = Get-PixiTaskNames $pixi
    $absent = @($CANONICAL_TASKS | Where-Object { -not $tasks.Contains($_) })
    if ($absent.Count -eq 0) { return }

    # A declared task counts however it is written, including a documented no-op:
    # rv-there-yet's update-deps explains that a shim-only mod vendors no loader,
    # so there is nothing to fetch, and that is conformant rather than a gap.
    #
    # Absence only breaks something today when the repo's own scripts tell a user
    # to run the task that is not there. Every update-deps.ps1 throws with "Run
    # 'pixi run sync'", so a repo with no sync task prints a recovery instruction
    # that fails. The rest is a gap to fill, not a defect to fix.
    $breaks = @($absent | Where-Object { Test-TaskIsReferenced -Root $Root -Task $_ })
    $rest = @($absent | Where-Object { $_ -notin $breaks })
    if ($breaks.Count -gt 0) {
        Add-Finding $Name 'pixi-tasks' 'FAIL' "pixi.toml has no $($breaks -join ', ') task$(if ($breaks.Count -gt 1) { 's' }), and this repo's own scripts tell the user to run $(if ($breaks.Count -gt 1) { 'them' } else { 'it' })"
    }
    if ($rest.Count -gt 0) {
        Add-Finding $Name 'pixi-tasks' 'WARN' "pixi.toml declares no $($rest -join ', ') task$(if ($rest.Count -gt 1) { 's' }); a documented no-op stub beats absence"
    }
}

function Test-ActionPins {
    param([string]$Name, [string]$Root)

    foreach ($wf in Get-WorkflowFiles $Root) {
        $n = 0
        foreach ($line in [System.IO.File]::ReadAllLines($wf.FullName)) {
            $n++
            if ($line -notmatch '^\s*(-\s+)?uses:\s*(\S+)') { continue }
            $ref = $Matches[2].Trim('"').Trim("'")
            # A path-local composite action carries no version to pin.
            if ($ref.StartsWith('./') -or $ref.StartsWith('docker://')) { continue }
            if ($ref -notmatch '@(.+)$') {
                Add-Finding $Name 'action-pins' 'FAIL' "$($wf.Name):$n uses $ref with no ref at all"
                continue
            }
            $at = $Matches[1]
            if ($at -notmatch '^[0-9a-f]{40}$') {
                Add-Finding $Name 'action-pins' 'FAIL' "$($wf.Name):$n pins $ref to a mutable tag; pin the commit SHA with a trailing # vX.Y.Z"
                continue
            }
            if ($line -notmatch '#\s*v?\d') {
                Add-Finding $Name 'action-pins' 'WARN' "$($wf.Name):$n pins a SHA with no trailing # vX.Y.Z comment, so nobody can tell what version it is"
            }
            # Pinned, but to a different commit from the rest of the fleet. Not
            # broken - a repo can be deliberately ahead - but unmanaged: three
            # SHAs were in use for actions/checkout v6 when this was written, so
            # a security bump reaches whichever third someone remembers.
            $action = $ref -replace '@.*$', ''
            $canonical = $ACTION_PINS.PSObject.Properties | Where-Object { $_.Name -eq $action } | Select-Object -First 1
            if ($canonical -and $at -ne $canonical.Value.sha) {
                Add-Finding $Name 'action-pins' 'WARN' "$($wf.Name):$n pins $action to $($at.Substring(0, 8)); scripts/templates/action-pins.json records $($canonical.Value.sha.Substring(0, 8)) # $($canonical.Value.version)"
            }
        }
    }
}

function Test-WorkflowRef {
    param([string]$Name, [string]$Root)

    foreach ($wf in Get-WorkflowFiles $Root) {
        $n = 0
        foreach ($line in [System.IO.File]::ReadAllLines($wf.FullName)) {
            $n++
            if ($line -notmatch '^\s*(-\s+)?uses:\s*(\S+/\.github/workflows/\S+)') { continue }
            $ref = $Matches[2].Trim('"').Trim("'")
            if ($ref -notmatch '@(.+)$') { continue }
            $at = $Matches[1]
            if ($at -match '^[0-9a-f]{40}$') { continue }
            if ($at -match '^v\d') {
                Add-Finding $Name 'workflow-ref' 'WARN' "$($wf.Name):$n calls $ref at tag $at; a tag can be moved, pin the SHA"
                continue
            }
            Add-Finding $Name 'workflow-ref' 'FAIL' "$($wf.Name):$n calls a cross-repo workflow at branch '$at'. That workflow holds contents:write, the Discord webhook and the Lopari PAT, and whatever lands on that branch runs with them"
        }
    }
}

function Test-WorkflowBuild {
    param([string]$Name, [string]$Root)

    foreach ($wf in Get-WorkflowFiles $Root) {
        $n = 0
        foreach ($line in [System.IO.File]::ReadAllLines($wf.FullName)) {
            $n++
            # Only the command itself, so `pixi run build` and a step named
            # "Build with cmake" are not mistaken for an inline build.
            if ($line -notmatch '^\s*(-\s+)?(run:\s*)?\|?\s*([a-z][^#]*)$') { continue }
            $cmd = $Matches[3].Trim()
            if ($cmd -match '^(pixi|npm|pnpm|yarn)\s') { continue }
            if ($cmd -match "^($INLINE_BUILD_TOOLS)\b") {
                Add-Finding $Name 'workflow-build' 'FAIL' "$($wf.Name):$n builds inline (``$($cmd.Substring(0, [Math]::Min(60, $cmd.Length)))``); CI must go through the same ``pixi run`` a developer runs or the two builds drift"
            }
        }
    }
}

function Test-CorePin {
    param([string]$Name, [string]$Root)

    $pin = Get-PinnedCoreCommit -RepoRoot $Root
    if (-not $pin) { return }

    $notices = Join-Path $Root 'THIRD-PARTY-NOTICES.md'
    if (-not (Test-Path $notices)) {
        Add-Finding $Name 'core-pin' 'FAIL' 'no THIRD-PARTY-NOTICES.md, but cameraunlock-core is compiled into the shipped DLLs'
    } else {
        $state = Sync-CoreCommitInNotices -RepoRoot $Root -ReadOnly
        if ($state.Recorded -eq 0) {
            Add-Finding $Name 'core-pin' 'FAIL' "THIRD-PARTY-NOTICES.md names no cameraunlock-core commit, so the attribution the user receives points at nothing (pin is $($pin.Substring(0, 8)))"
        } elseif ($state.Stale.Count -gt 0) {
            Add-Finding $Name 'core-pin' 'FAIL' "THIRD-PARTY-NOTICES.md records $($state.Stale -join ', '), the submodule pins $($pin.Substring(0, 8))"
        }
    }

    $when = & git -C $CoreRoot show -s --format=%cI $pin 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $when) {
        Add-Finding $Name 'core-pin' 'WARN' "the pinned core commit $($pin.Substring(0, 8)) is not in this core checkout, so its age cannot be read"
        return
    }
    $age = [int]((Get-Date) - [datetime]::Parse($when.Trim())).TotalDays
    if ($age -gt $MaxCorePinAgeDays) {
        Add-Finding $Name 'core-pin' 'WARN' "pins cameraunlock-core $($pin.Substring(0, 8)), $age days old; the release ships that commit's install bodies, find-game.ps1 and games.json"
    }
}

function Test-Manifest {
    param([string]$Name, [string]$Root)

    $path = Join-Path $Root 'launcher-manifest.json'
    if (-not (Test-Path $path)) {
        # A manifest has to name real shipped paths, so authoring one for a repo
        # lopari does not list is a guess that fails on a user's machine rather
        # than at build time. Most of the fleet is pre-release.
        Add-Finding $Name 'manifest' 'WARN' 'no launcher-manifest.json; correct while lopari does not list this mod, wrong the moment it does'
        return
    }
    try {
        $man = (Read-TextFile $path).TrimStart([char]0xFEFF) | ConvertFrom-Json
    } catch {
        Add-Finding $Name 'manifest' 'FAIL' "launcher-manifest.json is not valid JSON: $($_.Exception.Message)"
        return
    }
    $mode = if ($man.PSObject.Properties.Name -contains 'delivery_mode') { $man.delivery_mode } else { $null }
    if ($mode -notin @('manifest', 'install_cmd', 'external')) {
        Add-Finding $Name 'manifest' 'FAIL' "delivery_mode is '$mode'; the deploy engine knows manifest, install_cmd and external"
    }
    $schema = if ($man.PSObject.Properties.Name -contains 'schema_version') { $man.schema_version } else { $null }
    if ($schema -ne 2) {
        Add-Finding $Name 'manifest' 'FAIL' "schema_version is '$schema', the fleet is on 2"
    }
}

# Only mod.json. A root manifest.json is NOT a dead file and is deliberately not
# flagged: OWML and Thunderstore each read one, and in dying-light-2 and
# skyrim-special-edition it is the canonical version source - release.ps1 writes
# it, package-release.ps1 and validate-release.ps1 read it, and release.yml
# validates the pushed tag against it. Deleting those breaks the release.
function Test-StrayManifest {
    param([string]$Name, [string]$Root)

    if (Test-Path (Join-Path $Root 'mod.json')) {
        Add-Finding $Name 'stray-manifest' 'FAIL' 'mod.json is the dead parallel manifest format; nothing in lopari has ever read it, and audit-loaders.py classes a repo carrying it as LEGACY'
    }
}

function Test-License {
    param([string]$Name, [string]$Root)

    $path = Join-Path $Root 'LICENSE'
    if (-not (Test-Path $path)) {
        Add-Finding $Name 'license' 'FAIL' 'no LICENSE'
        return
    }
    $mine = (Read-TextFile $path) -replace "`r`n", "`n"
    $theirs = (Read-TextFile (Join-Path $CoreRoot 'LICENSE')) -replace "`r`n", "`n"
    if ($mine.TrimEnd() -eq $theirs.TrimEnd()) { return }

    $holder = if ($mine -match '(?m)^Copyright \(c\) (.+)$') { $Matches[1] } else { '(no copyright line)' }
    $want = if ($theirs -match '(?m)^Copyright \(c\) (.+)$') { $Matches[1] } else { '' }
    # The permission text and disclaimer are what MIT actually requires to
    # travel; the copyright line is whose name goes on it, and a repo naming a
    # different holder is an editorial inconsistency, not an altered licence.
    # Text APPENDED after the MIT body is also fine and several repos have it -
    # a scope note saying the licence does not cover the game footage in their
    # README clip or the game's trademarks. Only a body that is not reproduced
    # intact fails.
    $body = { param($t) ($t -replace '(?m)^Copyright \(c\).+$', '').Trim() }
    if (-not (& $body $mine).StartsWith((& $body $theirs))) {
        Add-Finding $Name 'license' 'FAIL' "LICENSE does not reproduce core's MIT text intact; it says '$holder', core says '$want'"
        return
    }
    Add-Finding $Name 'license' 'WARN' "LICENSE names '$holder', core names '$want'; the MIT body is identical"
}

function Test-Readme {
    param([string]$Name, [string]$Root)

    $path = Join-Path $Root 'README.md'
    if (-not (Test-Path $path)) {
        Add-Finding $Name 'readme' 'FAIL' 'no README.md'
        return
    }
    $text = Read-TextFile $path
    $headings = @([regex]::Matches($text, '(?m)^##\s+(.+?)\s*$') | ForEach-Object { $_.Groups[1].Value })
    $absent = @($CANONICAL_README_HEADINGS | Where-Object { $_ -notin $headings })
    if ($absent.Count -gt 0) {
        Add-Finding $Name 'readme' 'WARN' "no '$($absent -join "', '")' section"
    }

    $lines = ($text -replace "`r`n", "`n") -split "`n"
    foreach ($rule in $README_BANNED) {
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($lines[$i] -notmatch $rule.Pattern) { continue }
            Add-Finding $Name 'readme' 'FAIL' "README.md:$($i + 1) - $($rule.Why): $($lines[$i].Trim())"
        }
    }
}

$CHECK_TABLE = [ordered]@{
    'install-wrapper'   = ${function:Test-InstallWrapper}
    'delayed-expansion' = ${function:Test-DelayedExpansion}
    'arg-parser'        = ${function:Test-ArgParser}
    'cmd-crlf'          = ${function:Test-CmdCrlf}
    'pixi-tasks'        = ${function:Test-PixiTasks}
    'action-pins'       = ${function:Test-ActionPins}
    'workflow-ref'      = ${function:Test-WorkflowRef}
    'workflow-build'    = ${function:Test-WorkflowBuild}
    'core-pin'          = ${function:Test-CorePin}
    'manifest'          = ${function:Test-Manifest}
    'stray-manifest'    = ${function:Test-StrayManifest}
    'license'           = ${function:Test-License}
    'readme'            = ${function:Test-Readme}
}

# ---------------------------------------------------------------------------
# Repo selection
# ---------------------------------------------------------------------------

function Resolve-RepoPath {
    param([string]$Token)
    foreach ($candidate in @($Token, (Join-Path $ReposRoot $Token), (Join-Path $ReposRoot "$Token-headtracking"), (Join-Path $ReposRoot "$Token-head-tracking"))) {
        if (Test-Path -LiteralPath $candidate -PathType Container) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    throw "No repo found for '$Token' - tried it as a path and under $ReposRoot."
}

$selected = @($Check | Where-Object { $_ })
if ($selected.Count -eq 0) { $selected = $CHECK_IDS }
foreach ($id in $selected) {
    if ($id -notin $CHECK_IDS) { throw "Unknown check '$id'. Known: $($CHECK_IDS -join ', ')" }
}

if ($All) {
    if ($Repo) { throw '-All and -Repo are mutually exclusive.' }
    # A mod repo, not every sibling checkout: named for the fleet convention and
    # actually vendoring this core. lopari, headcam and quickfeed all have
    # scripts/ and a pixi.toml and none of these invariants apply to them.
    $roots = @(Get-ChildItem -Path $ReposRoot -Directory |
        Where-Object {
            $_.FullName -ne $CoreRoot -and
            $_.Name -match '-(headtracking|head-tracking)$' -and
            (Test-Path (Join-Path $_.FullName '.git')) -and
            (Test-Path (Join-Path $_.FullName 'cameraunlock-core'))
        } |
        Select-Object -ExpandProperty FullName)
} elseif ($Repo) {
    $roots = @($Repo | ForEach-Object { Resolve-RepoPath $_ })
} else {
    $roots = @([System.IO.Path]::GetFullPath((Join-Path $CoreRoot '..')))
}

if ($roots.Count -eq 0) { throw "No repos to check under $ReposRoot." }

foreach ($root in $roots) {
    $name = Split-Path -Leaf $root
    foreach ($id in $selected) {
        & $CHECK_TABLE[$id] $name $root
    }
}

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------

if ($Json) {
    $findings | ConvertTo-Json -Depth 4
} else {
    $byRepo = $findings | Group-Object repo
    foreach ($root in $roots) {
        $name = Split-Path -Leaf $root
        $group = $byRepo | Where-Object { $_.Name -eq $name }
        if (-not $group) {
            Write-Host "ok    $name" -ForegroundColor DarkGray
            continue
        }
        $fails = @($group.Group | Where-Object { $_.severity -eq 'FAIL' }).Count
        $warns = @($group.Group | Where-Object { $_.severity -eq 'WARN' }).Count
        Write-Host ''
        Write-Host "$name  ($fails fail, $warns warn)" -ForegroundColor Cyan
        foreach ($f in ($group.Group | Sort-Object severity, check)) {
            $colour = if ($f.severity -eq 'FAIL') { 'Red' } else { 'Yellow' }
            Write-Host ("  {0,-4} {1,-18} {2}" -f $f.severity, $f.check, $f.message) -ForegroundColor $colour
        }
    }

    $totalFail = @($findings | Where-Object { $_.severity -eq 'FAIL' }).Count
    $totalWarn = @($findings | Where-Object { $_.severity -eq 'WARN' }).Count
    $clean = @($roots | Where-Object {
        $n = Split-Path -Leaf $_
        -not ($findings | Where-Object { $_.repo -eq $n -and $_.severity -eq 'FAIL' })
    }).Count
    Write-Host ''
    Write-Host "$($roots.Count) repos, $clean clean, $totalFail failures, $totalWarn warnings."
    if ($totalFail -gt 0) {
        Write-Host 'Template-shaped failures are fixable fleet-wide with scripts/sync-templates.ps1.' -ForegroundColor Cyan
    }
}

if (@($findings | Where-Object { $_.severity -eq 'FAIL' }).Count -gt 0) { exit 1 }
exit 0
