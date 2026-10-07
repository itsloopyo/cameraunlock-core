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
    'install-wrapper', 'delayed-expansion', 'arg-parser', 'config-block', 'config-pairing',
    'shim-marker', 'cmd-crlf', 'pixi-tasks', 'action-pins', 'workflow-ref', 'workflow-build', 'core-pin',
    'manifest', 'manifest-seed', 'mod-version', 'stray-manifest', 'license', 'readme',
    'config-format', 'config-legacy-reader', 'config-preserve', 'config-descriptor', 'config-defaults',
    'release-canonical-since', 'ci-minutes', 'pipeline-port', 'changelog-unreleased'
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
#
# Two sentences are exempt, and only from the first of these, because AGENTS.md
# mandates both of them verbatim and the phrase is inside each: the opening
# sentence ("Every README opens the same way") and the Features bullet that
# names the tracker ("Works with any OpenTrack compatible tracker - free options
# available for PC, iOS and Android", which ships verbatim on the Nexus page and
# in the README alike). Without both carve-outs no README in the fleet can
# satisfy both documents at once - the bullet alone accounted for 118 of the 127
# repos. The rule still fires on the phrase anywhere else on the page, which is
# where it was earning its keep.
#
# Matched against the whole file rather than one line, and with \s+ for every
# space in it: repos hard-wrap those sentences at whichever word reaches the
# margin, so both the line anchoring and the literal spaces have to go, or the
# exemption only covers the repos that happened not to wrap. The bullet's `**`
# emphasis is optional for the same reason - 13 repos ship it unbolded.
$README_BANNED = @(
    @{ Pattern = '\bany\s+OpenTrack[- ]compatible'; Why = 'claims every OpenTrack-compatible tracker works; we have tested some'; Except = @(
        'An\s+unofficial\s+head\s+tracking\s+mod\s+for\s+[\s\S]{1,200}?driven\s+by\s+a\s+webcam,\s+phone,\s+or\s+any\s+OpenTrack\s+compatible\s+tracker,\s+with\s+no\s+VR\s+headset\s+required\.'
        '\**Works\s+with\s+any\s+OpenTrack\s+compatible\s+tracker\**\s+-\s+free\s+options\s+available\s+for\s+PC,\s+iOS\s+and\s+Android'
      ) }
    @{ Pattern = '\bany\s+phone\s+tracker';        Why = 'claims every phone tracker works; phone trackers do not share one protocol' }
    @{ Pattern = '\ball\s+\w+\s+(trackers|apps|headsets)\s+(speak|use|support|are|do|send)'; Why = 'an "all X do Y" generalisation about third-party kit' }
    @{ Pattern = '\bevery\s+(phone|tracker|headset|app)\s+(speaks|uses|supports|sends)';    Why = 'an "all X do Y" generalisation about third-party kit' }
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

# Anchored to the `set "_BODY=..."` dispatch line rather than to any mention of
# a body filename. sync-templates.ps1 decides from the same answer whether to
# replace a script's entire tail, so a comment that happens to name a body must
# not make a bespoke installer look like a wrapper - that reads as a one-line
# audit tweak and lands as a deleted installer.
function Get-WrapperBodyName {
    param([string]$Text)
    if ($Text -match '(?m)^\s*set "_BODY=[^"]*?((?:un)?install-body[a-z0-9-]*\.cmd)"') { return $Matches[1] }
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
            continue
        }
        if ($mine -ne $theirs) {
            Add-Finding $Name 'install-wrapper' 'FAIL' "scripts/$($pair.Script) has edits below the CONFIG BLOCK; it no longer matches scripts/templates/$templateName"
        }

        # A wrapper sets its CONFIG BLOCK before its setlocal, so a name it leaves
        # out keeps whatever another mod's wrapper set in the same console, and the
        # body acts on it: an inherited ASI_SUBDIR sends an uninstall to another
        # mod's folder. sync-templates.ps1 never writes a CONFIG BLOCK.
        $wrapperVars = Get-ConfigBlockVars $text
        $missing = @((Get-ConfigBlockVars (Read-TextFile $templatePath)).Keys | Where-Object { -not $wrapperVars.Contains($_) })
        if ($missing.Count -gt 0) {
            $lines = ($missing | ForEach-Object { "set `"$_=`"" }) -join ', '
            Add-Finding $Name 'install-wrapper' 'FAIL' "scripts/$($pair.Script)'s CONFIG BLOCK does not set $($missing -join ', '), so it runs with whatever another mod's wrapper left in the console; add $lines as scripts/templates/$templateName sets them"
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

# The shared bodies expand the CONFIG BLOCK with %VAR%, so whatever a mod puts
# there is handed back to cmd.exe's parser. `abzu-headtracking` separated its
# MOD_CONTROLS lines with " | " instead of "&echo "; the install ran to
# completion, then cmd tried to pipe the banner into a program called PageUp and
# the script returned 255, which lopari reads as a failed install.
#
# MOD_CONTROLS is printed outside any ( ) block, so a literal ) in it is safe and
# "&echo " is the intended separator. Everything else that reaches the parser is
# a live metacharacter.
$CONFIG_METACHARS = @{ '|' = 'a pipe'; '<' = 'a redirect'; '>' = 'a redirect'; '&' = 'a command separator' }

# Values that reach cmd.exe from inside a parenthesised block, where a `)` in
# the value closes the block early and the rest of it runs as commands. The
# state-file heredoc echoes the first four out of `> "..." ( ... )`; the rest
# are expanded inside `if defined ... ( ... )` / `if /i "%FRAMEWORK_TYPE%"==...`
# blocks in install-body-bepinex.cmd, install-body-cecil.cmd,
# install-body-ue4ss.cmd and uninstall-body.cmd, PRESERVE_FILES inside the
# `for %%k in (%PRESERVE_FILES%)` loops of uninstall-body.cmd.
$CONFIG_IN_BLOCK = @(
    'GAME_ID', 'MOD_INTERNAL_NAME', 'MOD_VERSION', 'FRAMEWORK_TYPE',
    'PLUGIN_SUBFOLDER', 'BEPINEX_SUBFOLDER', 'BEPINEX_VENDOR_ZIP_NAME',
    'MANAGED_SUBFOLDER', 'UE4_BINARIES_RELDIR', 'PRESERVE_FILES'
)

# The CONFIG BLOCK as name -> value. Both spellings are parsed: cmd.exe accepts
# `set "NAME=value"` and bare `set NAME=value`, and the bare form is exactly
# where an unescaped metacharacter is most likely to be written, so reading only
# the quoted form let the whole class of defect this check exists for through.
function Get-ConfigBlockVars {
    param([string]$Text)
    $vars = [ordered]@{}
    $inBlock = $false
    foreach ($line in (($Text -replace "`r`n", "`n") -split "`n")) {
        if ($line -match 'END CONFIG BLOCK') { break }
        if ($line -match '--- CONFIG BLOCK ---') { $inBlock = $true; continue }
        if (-not $inBlock) { continue }
        if ($line -match '^\s*set\s+"([A-Za-z_][A-Za-z0-9_]*)=(.*)"\s*$') {
            $vars[$Matches[1]] = $Matches[2]
        } elseif ($line -match '^\s*set\s+([A-Za-z_][A-Za-z0-9_]*)=(.*?)\s*$') {
            $vars[$Matches[1]] = $Matches[2]
        }
    }
    return $vars
}

function Test-ConfigBlock {
    param([string]$Name, [string]$Root)

    foreach ($script in @('install.cmd', 'uninstall.cmd')) {
        $path = Join-Path $Root "scripts/$script"
        if (-not (Test-Path $path)) { continue }
        $vars = Get-ConfigBlockVars (Read-TextFile $path)

        foreach ($var in $vars.Keys) {
            $value = $vars[$var]

            # ^X is escaped and prints literally; drop those before looking.
            $bare = $value -replace '\^.', ''
            # `&echo ` and `& echo ` both work as the separator, so both are the
            # intended spelling rather than an unescaped &.
            if ($var -eq 'MOD_CONTROLS') { $bare = $bare -replace '&\s*echo[ .]', '' }

            foreach ($char in $CONFIG_METACHARS.Keys) {
                if (-not $bare.Contains($char)) { continue }
                $fix = if ($var -eq 'MOD_CONTROLS' -and $char -eq '&') {
                    'use "&echo " to start each further line'
                } else {
                    "escape it as ^$char"
                }
                Add-Finding $Name 'config-block' 'FAIL' "scripts/$script sets $var with an unescaped $char ($($CONFIG_METACHARS[$char])); the shared body expands it with %$var% and cmd.exe runs it - $fix"
            }
            if ($var -in $CONFIG_IN_BLOCK -and $bare -match '[()]') {
                Add-Finding $Name 'config-block' 'FAIL' "scripts/$script sets $var with a parenthesis; the shared body expands it inside a ( ) block, which it closes early"
            }
        }
    }
}

# install.cmd and uninstall.cmd are two files a mod hand-fills, and the second
# has to undo what the first did. The proxy-DLL incident that motivated the
# wrapper conversion was ASI_LOADER_NAME disagreeing between them: install
# dropped the payload as one filename, uninstall deleted another, and the game
# kept loading the mod after the user removed it. Every name here is one the
# uninstall body uses to find what the install body wrote.
$CONFIG_PAIRED = @(
    'GAME_ID', 'MOD_DISPLAY_NAME', 'MOD_INTERNAL_NAME', 'STATE_FILE', 'FRAMEWORK_TYPE',
    'PLUGIN_SUBFOLDER', 'MANAGED_SUBFOLDER', 'ASSEMBLY_DLL',
    'ASI_LOADER_NAME', 'ASI_SUBDIR', 'UE4_BINARIES_RELDIR', 'SHIM_MARKER', 'SHIM_MARKER_ALT'
)

function Test-ConfigPairing {
    param([string]$Name, [string]$Root)

    $installPath = Join-Path $Root 'scripts/install.cmd'
    $uninstallPath = Join-Path $Root 'scripts/uninstall.cmd'
    if (-not (Test-Path $installPath) -or -not (Test-Path $uninstallPath)) { return }

    $installText = Read-TextFile $installPath
    $uninstallText = Read-TextFile $uninstallPath
    # A legacy in-tree body carries its own config in its own shape; there is no
    # CONFIG BLOCK contract to hold it to. Test-InstallWrapper already reports it.
    if (-not (Get-WrapperBodyName $installText) -or -not (Get-WrapperBodyName $uninstallText)) { return }

    $install = Get-ConfigBlockVars $installText
    $uninstall = Get-ConfigBlockVars $uninstallText

    foreach ($var in $CONFIG_PAIRED) {
        # Only names both files declare. A name absent from one is that loader's
        # config not applying there, which the bodies themselves check for.
        if (-not $install.Contains($var) -or -not $uninstall.Contains($var)) { continue }
        if ($install[$var] -eq $uninstall[$var]) { continue }
        Add-Finding $Name 'config-pairing' 'FAIL' "install.cmd sets $var to '$($install[$var])' and uninstall.cmd to '$($uninstall[$var])'; uninstall uses it to find what install wrote, so it looks in the wrong place and leaves the mod loaded"
    }

    # MOD_DLLS and MOD_SEED_FILES are not compared for equality: uninstall.cmd's
    # MOD_DLLS is deliberately a superset across the fleet, listing the logs and
    # config the mod writes at runtime as well as what was installed. What has to
    # hold is that nothing installed is left behind.
    $removed = @()
    foreach ($var in @('MOD_DLLS', 'LEGACY_DLLS', 'MOD_SEED_FILES', 'MOD_LEFTOVERS', 'ROOT_EXTRAS', 'MANAGED_EXTRAS')) {
        if ($uninstall.Contains($var)) { $removed += @($uninstall[$var] -split '\s+' | Where-Object { $_ }) }
    }
    foreach ($var in @('MOD_DLLS', 'MOD_SEED_FILES')) {
        if (-not $install.Contains($var)) { continue }
        foreach ($file in @($install[$var] -split '\s+' | Where-Object { $_ })) {
            if ($file -in $removed) { continue }
            Add-Finding $Name 'config-pairing' 'FAIL' "install.cmd's $var installs $file and uninstall.cmd removes nothing by that name; it is left in the game folder after an uninstall"
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

    # `2>$null` on its own is a trap under $ErrorActionPreference = 'Stop': in
    # Windows PowerShell 5.1 a native command's stderr becomes a
    # NativeCommandError, which terminates the whole run before the
    # $LASTEXITCODE guard below is ever read. A path that is not a git work
    # tree is an ordinary answer here, so drop to Continue for the call and
    # gate on the exit code, which is the real success signal for a native exe.
    $prevPref = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $eol = & git -C $Root ls-files --eol -- '*.cmd' 2>$null
    $ErrorActionPreference = $prevPref
    if ($LASTEXITCODE -ne 0) { return }
    foreach ($line in @($eol)) {
        # w/mixed as well as w/lf: a file whose endings were half-converted has
        # LF lines in it, and cmd.exe fails on those the same way it fails on a
        # wholly-LF file. `none` is an empty file and has nothing to get wrong.
        if ($line -match '^\S*\s+w/(lf|mixed)\s+.*?\t(.+)$') {
            $how = if ($Matches[1] -eq 'lf') { 'is LF' } else { 'has mixed line endings' }
            Add-Finding $Name 'cmd-crlf' 'FAIL' "$($Matches[2]) $how in the working tree; cmd.exe fails on it silently"
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
        $text = Read-TextFile $file.FullName
        # Strip line comments first: the task name inside a comment is not a caller.
        $live = ($text -split "`n" | ForEach-Object { $_ -replace '^\s*(#|::|//|REM\s).*$', '' }) -join "`n"
        # Anchored on a word boundary: a bare -like match reports `pixi run test`
        # as referenced by a script that only ever calls `pixi run test-udp`.
        if ($live -match ("(?m)" + [regex]::Escape($needle) + "(?![-\w])")) { return $true }
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
            # A reusable workflow names a path inside another repo; its ref is a
            # release decision, not an action pin, and Test-WorkflowRef below is
            # the check that owns it. Without this, every caller of core's
            # release-mod.yml also collects an action-pins WARN for having no
            # `# vX.Y.Z` comment - a version core does not publish for it.
            # sync-templates.ps1 has always skipped these for the same reason.
            if ($ref -match '/\.github/workflows/') { continue }
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
            # `(?![-\w])`, not `\b`: a word boundary sits between the `e` and the
            # `-` of a YAML key like `cmake-version:`, so `\b` failed the
            # workflow of anyone who pinned a cmake version in a `with:` block.
            if ($cmd -match "^($INLINE_BUILD_TOOLS)(?![-\w])") {
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

    # The release script is where a submodule bump gets its notices correction.
    # Without that call the FAIL above returns the moment anyone bumps the
    # pointer, and in CI it lands after the tag has already been pushed.
    # Read the script's presence from the PINNED commit, not the submodule
    # working tree: a repo whose bump is still uncommitted would otherwise be
    # told to call a script nobody who clones it has.
    $release = Join-Path $Root 'scripts\release.ps1'
    $pinnedSync = & git -C (Join-Path $Root 'cameraunlock-core') ls-tree --name-only $pin -- scripts/sync-core-notices.ps1
    if ($pinnedSync -and (Test-Path $release) -and ((Read-TextFile $release) -notmatch 'sync-core-notices')) {
        Add-Finding $Name 'core-pin' 'FAIL' 'scripts/release.ps1 never re-syncs THIRD-PARTY-NOTICES.md against the pinned cameraunlock-core commit, so the next submodule bump breaks the release'
    }

    # Same NativeCommandError trap as Test-CmdCrlf, and this one is on the
    # documented path: `show` fails exactly when the pinned commit is missing
    # from this checkout, which is the WARN below.
    $prevPref = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $when = & git -C $CoreRoot show -s --format=%cI $pin 2>$null
    $ErrorActionPreference = $prevPref
    if ($LASTEXITCODE -ne 0 -or -not $when) {
        Add-Finding $Name 'core-pin' 'WARN' "the pinned core commit $($pin.Substring(0, 8)) is not in this core checkout, so its age cannot be read"
        return
    }
    $age = [int]((Get-Date) - [datetime]::Parse($when.Trim())).TotalDays
    if ($age -gt $MaxCorePinAgeDays) {
        Add-Finding $Name 'core-pin' 'WARN' "pins cameraunlock-core $($pin.Substring(0, 8)), $age days old; the release ships that commit's install bodies, find-game.ps1 and games.json"
    }
}

# git writes to stderr on a non-repo, and under PS 5.1 with ErrorActionPreference
# Stop that becomes a terminating NativeCommandError. Same trap as Get-PinnedCoreCommit.
function Test-GitTracked {
    param([string]$Root, [string]$RelPath)
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & git -C $Root ls-files --error-unmatch -- $RelPath 2>$null | Out-Null
        return $LASTEXITCODE -eq 0
    } finally {
        $ErrorActionPreference = $prev
    }
}

function Test-Manifest {
    param([string]$Name, [string]$Root)

    $path = Join-Path $Root 'launcher-manifest.json'
    if ((Test-Path $path) -and -not (Test-GitTracked $Root 'launcher-manifest.json')) {
        Add-Finding $Name 'manifest' 'FAIL' 'launcher-manifest.json is on disk but untracked, so a clean clone does not have it; a packager that reads it fails in CI while passing locally'
        return
    }
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
    if ($mode -notin @('manifest', 'manifest_variants', 'install_cmd', 'external')) {
        Add-Finding $Name 'manifest' 'FAIL' "delivery_mode is '$mode'; expected manifest, manifest_variants, install_cmd or external"
    }
    $schema = if ($man.PSObject.Properties.Name -contains 'schema_version') { $man.schema_version } else { $null }
    if ($schema -ne 2) {
        Add-Finding $Name 'manifest' 'FAIL' "schema_version is '$schema', the fleet is on 2"
    }
}

# A manifest that seeds a config file carries it as a base64 blob, and nothing
# regenerates that blob when the shipped file changes. The drift is invisible
# until it reaches a user, where lopari writes the stale copy over the defaults
# the mod ships. resident-evil-requiem's blob was seeding position sensitivities
# of 2.0 against a mod that ships 1.0, no [Flashlight] section, and a
# ReticleToggleKey that mod has never had.
function Test-ManifestSeed {
    param([string]$Name, [string]$Root)

    $path = Join-Path $Root 'launcher-manifest.json'
    if (-not (Test-Path $path)) { return }
    try {
        $man = (Read-TextFile $path).TrimStart([char]0xFEFF) | ConvertFrom-Json
    } catch {
        # Test-Manifest already reports unparseable JSON.
        return
    }

    $seeds = New-Object System.Collections.Generic.List[object]
    if ($man.PSObject.Properties.Name -contains 'loader' -and $man.loader -and
        $man.loader.PSObject.Properties.Name -contains 'seed') {
        foreach ($s in @($man.loader.seed)) { if ($s) { $seeds.Add($s) } }
    }
    if ($man.PSObject.Properties.Name -contains 'seed') {
        foreach ($s in @($man.seed)) { if ($s) { $seeds.Add($s) } }
    }

    foreach ($seed in $seeds) {
        if ($seed.PSObject.Properties.Name -notcontains 'content_b64') { continue }
        if ($seed.PSObject.Properties.Name -notcontains 'target') { continue }
        $leaf = ($seed.target -split '[\\/]')[-1]

        # Only the mod's own files. A seed with no counterpart in the repo is the
        # loader's config rather than ours: BepInEx writes BepInEx.cfg, we do not
        # ship one, and there is nothing to compare it against.
        $shipped = @(Get-ChildItem -LiteralPath $Root -File -Filter $leaf -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -notmatch '(?i)\\(\.git|\.pixi|\.lab|\.vs|cameraunlock-core|vendor|node_modules|obj|bin|build|release|dist)\\' })
        if ($shipped.Count -eq 0) { continue }
        if ($shipped.Count -gt 1) {
            Add-Finding $Name 'manifest-seed' 'WARN' "seeds $($seed.target) and the repo holds $($shipped.Count) files named $leaf, so nothing says which one the blob is meant to match"
            continue
        }

        try {
            $decoded = [System.Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($seed.content_b64))
        } catch {
            Add-Finding $Name 'manifest-seed' 'FAIL' "the content_b64 for $($seed.target) is not valid base64: $($_.Exception.Message)"
            continue
        }

        $normalise = { param($t) (($t -replace "^$([char]0xFEFF)", '') -replace "`r`n", "`n").TrimEnd() }
        if ((& $normalise $decoded) -eq (& $normalise (Read-TextFile $shipped[0].FullName))) { continue }

        $rel = $shipped[0].FullName.Substring($Root.Length).TrimStart('\\') -replace '\\', '/'
        Add-Finding $Name 'manifest-seed' 'FAIL' "launcher-manifest.json seeds $($seed.target) from a base64 blob that no longer matches $rel; installing writes the stale copy over the defaults the mod ships"
    }
}

# release-mod.yml gates the pushed git tag against the version source and
# nothing else, so MOD_VERSION - which the install body writes into
# .headtracking-state.json as `mod.version`, the field lopari reads to decide
# whether an install is out of date - is free to say something else entirely.
# It did, in eight repos: a v1.4.0 Subnautica install was writing "1.0.0".
#
# The fleet's own fix is already in ~30 release.ps1 scripts, which rewrite
# `set "MOD_VERSION=..."` from the canonical version at release time. Stamping
# it at package time instead would leave the committed installer wrong while
# the ZIP was right - the same trade Assert-ManifestSeedsMatchShipped refuses,
# and worse here because `pixi run install` runs the committed file directly.
function Test-ModVersion {
    param([string]$Name, [string]$Root)

    $install = Join-Path $Root 'scripts/install.cmd'
    $workflow = Join-Path $Root '.github/workflows/release.yml'
    if (-not (Test-Path $install) -or -not (Test-Path $workflow)) { return }

    $vars = Get-ConfigBlockVars (Read-TextFile $install)
    if (-not $vars.Contains('MOD_VERSION')) { return }

    # The same four values release-mod.yml is called with. A mod that declares
    # no version-source does not run that workflow, so there is no authority to
    # compare against.
    $yaml = Read-TextFile $workflow
    $readInput = {
        param($key)
        if ($yaml -notmatch "(?m)^\s*$([regex]::Escape($key)):\s*(.+?)\s*$") { return $null }
        $v = $Matches[1]
        if ($v -match "^'(.*)'$" -or $v -match '^"(.*)"$') { return $Matches[1] }
        return $v
    }
    $source = & $readInput 'version-source'
    $path = & $readInput 'version-path'
    if (-not $source -or -not $path) {
        # A silent return is indistinguishable from a pass in the output, which is
        # how two repos sat unchecked while printing ok.
        Add-Finding $Name 'mod-version' 'WARN' 'scripts/install.cmd sets MOD_VERSION but release.yml declares no version-source, so nothing compares them and no tag check runs'
        return
    }

    $full = Join-Path $Root $path
    try {
        $key = & $readInput 'version-key'
        $pattern = & $readInput 'version-pattern'
        $fileVersion = Get-ProjectVersion -Source $source -Path $full `
            -Key $(if ($key) { $key } else { 'version' }) -Pattern $(if ($pattern) { $pattern } else { '' })
    } catch {
        Add-Finding $Name 'mod-version' 'FAIL' "release.yml declares version-source $source at $path, and reading it fails: $($_.Exception.Message). The tag check in release-mod.yml runs this same read, so no release can be cut"
        return
    }

    if ($fileVersion -eq $vars['MOD_VERSION']) { return }
    Add-Finding $Name 'mod-version' 'FAIL' "$path has $fileVersion but scripts/install.cmd sets MOD_VERSION=$($vars['MOD_VERSION']); the installer writes that into .headtracking-state.json as mod.version, which is what lopari reads to decide upgrades"
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
    if ($holder -eq $want) {
        # Identical holder and an intact body: the difference is the appended
        # scope note described above, which is deliberate. Nothing to report.
        return
    }
    Add-Finding $Name 'license' 'WARN' "LICENSE names '$holder', core names '$want'; the MIT body is identical"
}

# True when line $Index falls inside a stretch of the file that matches
# $Pattern. The banned-phrase rules are applied per line, but a sentence a repo
# has hard-wrapped spans several, so an exemption has to be measured against the
# joined text and then mapped back to the lines it covers.
function Test-ExemptLine {
    param(
        # No [Parameter(Mandatory)] on $Lines: a mandatory string[] rejects an
        # empty element, and a README is mostly blank lines.
        [string[]]$Lines,
        [Parameter(Mandatory = $true)][int]$Index,
        [Parameter(Mandatory = $true)][string]$Pattern
    )
    $text = $Lines -join "`n"
    $lineStart = 0
    for ($i = 0; $i -lt $Index; $i++) { $lineStart += $Lines[$i].Length + 1 }
    $lineEnd = $lineStart + $Lines[$Index].Length
    foreach ($m in [regex]::Matches($text, $Pattern)) {
        if ($m.Index -le $lineEnd -and ($m.Index + $m.Length) -ge $lineStart) { return $true }
    }
    return $false
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
            # Against the whole file, and only for the lines an exempt
            # sentence actually spans, so a wrapped opener or Features bullet
            # is exempt while the same phrase elsewhere on the page is not.
            if ($rule.ContainsKey('Except') -and (@($rule.Except | Where-Object { Test-ExemptLine $lines $i $_ }).Count -gt 0)) { continue }
            Add-Finding $Name 'readme' 'FAIL' "README.md:$($i + 1) - $($rule.Why): $($lines[$i].Trim())"
        }
    }

    $config = $ReadmeConfig[$Root]
    $fix = 'pixi run readme --write --sections config renders it, in the change that makes the mod read CameraUnlock.ini or after it, since the block describes that file'
    if ($null -ne $config.error) {
        Add-Finding $Name 'readme' 'FAIL' "the config block cannot be rendered: $($config.error)"
        return
    }
    switch ($config.sections.config) {
        'unchanged' { }
        'rewritten' { Add-Finding $Name 'readme' 'FAIL' "the config block differs from the one rendered from data/config-format.json and the committed config; $fix" }
        'inserted'  { Add-Finding $Name 'readme' 'FAIL' "converted to the canonical config format, and README.md has no config block; $fix" }
        'removed'   { Add-Finding $Name 'readme' 'FAIL' 'README.md has a config block, and the repo is not converted to the canonical config format, so there is nothing for it to describe' }
        default     { throw "scripts/generate-readme.mjs reported config '$($config.sections.config)' for $Root" }
    }
}

# SHIM_MARKER is how the shim bodies tell this mod's own DLL from whatever the
# user already had at that name, and there is no safe default for it: backing up
# unconditionally records our own shim as the user's original, and skipping the
# backup unconditionally loses a file that really was theirs. The bodies refuse
# to run without one, so a shim repo that ships no SHIM_MARKER has an
# install.cmd that stops at the CONFIG BLOCK check.
function Test-ShimMarker {
    param([string]$Name, [string]$Root)

    $installPath = Join-Path $Root 'scripts/install.cmd'
    if (-not (Test-Path $installPath)) { return }
    $body = Get-WrapperBodyName (Read-TextFile $installPath)
    if ($body -notin @('install-body-shim.cmd', 'install-body-shim-forwarder.cmd')) { return }

    foreach ($script in @('install.cmd', 'uninstall.cmd')) {
        $path = Join-Path $Root "scripts/$script"
        if (-not (Test-Path $path)) { continue }
        $vars = Get-ConfigBlockVars (Read-TextFile $path)
        if ($vars.Contains('SHIM_MARKER') -and $vars['SHIM_MARKER']) { continue }
        Add-Finding $Name 'shim-marker' 'FAIL' "scripts/$script dispatches to a shim body but sets no SHIM_MARKER; the body refuses to run without one, and a backup taken on upgrade would record this mod's own DLL as the user's original"
    }
}

# The canonical config format (design 6.3). scripts/check-canonical-config.mjs --json says
# where each repo stands in data/config-format.json and lints every committed config file that
# carries the [CameraUnlock] stamp; the checks below turn that into findings. A repo is
# converted when one of its committed files carries the stamp: nothing records it separately.
$CONFIG_CHECK_IDS = @('config-format', 'config-legacy-reader', 'config-preserve', 'config-defaults')
$CanonicalConfig = @{}
# scripts/generate-readme.mjs --json --sections config, per root: whether README.md's config
# block matches the one rendered from data/config-format.json and the committed config.
$ReadmeConfig = @{}

# The frozen legacy import lives here and nowhere else (design 4.2): src/legacy_config/ in a
# C++ repo, a Legacy/ folder in a C# one.
$LEGACY_FOLDER = '(^|/)(src/legacy_config|Legacy)/'
$LEGACY_SCAN_SKIP = '(^|/)(vendor|extern|third_party|cameraunlock-core|bin|obj|build|out|release|dist|target)/'
# The differential test (design 6.2) compiles the published build's reader as its oracle, so it
# names the banned symbols by design, and it is not the import.
$DIFFERENTIAL_TEST_FOLDER = '^tests/config_differential/'
$LEGACY_SCAN_SOURCE = '\.(c|cc|cpp|cxx|h|hh|hpp|hxx|inl|ipp|cs|rs)$'
$LEGACY_READER_SYMBOLS = '\b(GetPrivateProfile\w*|WritePrivateProfile\w*|IniReader|IniWriter|ParseIniConfig|ParseIniFile)\b'
# BepInEx's ConfigFile is reached through BaseUnityPlugin.Config or a parameter of any name, so
# a Bind call counts in any C# file that names BepInEx.
$BEPINEX_BIND = '\.\s*Bind\s*(<[^<>()]*>)?\s*\('

function Get-TrackedFiles {
    param([string]$Root)
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $files = & git -c core.quotepath=off -C $Root ls-files 2>&1
        if ($LASTEXITCODE -ne 0) { throw "git ls-files failed in ${Root}: $files" }
        # 2>&1 turns a stderr line into an ErrorRecord; on success only the paths are wanted.
        return $files | Where-Object { $_ -is [string] }
    } finally {
        $ErrorActionPreference = $prev
    }
}

function Get-LegacyFolderFiles {
    param([string]$Root)
    return Get-TrackedFiles $Root | Where-Object { $_ -cmatch $LEGACY_FOLDER -and $_ -notmatch $LEGACY_SCAN_SKIP -and $_ -notmatch $DIFFERENTIAL_TEST_FOLDER }
}

function Test-ConfigFormat {
    param([string]$Name, [string]$Root)

    $state = $CanonicalConfig[$Root]
    if ($state.listing -eq 'predecessor') { return }

    $legacyFiles = @(Get-LegacyFolderFiles $Root)
    if ($state.listing -ne 'legacy' -and $legacyFiles.Count -gt 0) {
        $folder = [regex]::Match($legacyFiles[0], "^(.*?$LEGACY_FOLDER)").Value
        Add-Finding $Name 'config-format' 'FAIL' "$folder is a legacy config import, and only a repo in data/config-format.json legacy, one that published a pre-canonical build, carries one"
    }
    if ($state.listing -eq 'exempt') { return }
    if ($state.listing -eq 'unlisted') {
        Add-Finding $Name 'config-format' 'FAIL' 'not in data/config-format.json; a repo outside legacy and exempt must be canonical, and configs records where its config lives'
        return
    }

    $unrecordedStamped = @($state.unrecorded_stamped)
    foreach ($rel in $unrecordedStamped) {
        Add-Finding $Name 'config-format' 'FAIL' "$rel carries the [CameraUnlock] stamp, and data/config-format.json configs records no committed file for it; a conversion records its committed path in core, and nothing is linted until it does"
    }

    foreach ($file in $state.files) {
        if ($file.state -eq 'unrecorded' -and $unrecordedStamped.Count -gt 0) { continue }
        if ($file.state -eq 'stamped') {
            foreach ($problem in $file.problems) { Add-Finding $Name 'config-format' 'FAIL' "$($file.committed): $problem" }
        } elseif ($file.state -eq 'missing') {
            Add-Finding $Name 'config-format' 'FAIL' "data/config-format.json records $($file.committed) as the committed config, and the repo has no such file"
        } elseif ($state.listing -ne 'legacy' -or $state.converted) {
            $why = if ($file.state -eq 'unrecorded') {
                $installed = @($file.installed)
                $of = if ($installed.Count -gt 0) { " of $($installed[0])" } else { '' }
                "data/config-format.json records no committed file for the config$of"
            } else {
                "$($file.committed) carries no [CameraUnlock] stamp"
            }
            $rule = if ($state.listing -eq 'legacy') {
                'one conversion switches every config file of a repo'
            } else {
                'a repo outside legacy and exempt commits its rendered canonical file'
            }
            Add-Finding $Name 'config-format' 'FAIL' "${why}; $rule"
        }
    }

    if ($state.listing -ne 'legacy') { return }
    if (-not $state.converted) {
        Add-Finding $Name 'config-format' 'WARN' 'not converted to the canonical config format yet'
        return
    }
    # An REFramework mod's legacy import is core's PluginConfigLegacyImport (design 2.8), so the
    # repo has no folder of its own to carry it.
    $installPath = Join-Path $Root 'scripts/install.cmd'
    $coreImport = (Test-Path $installPath) -and ((Get-WrapperBodyName (Read-TextFile $installPath)) -eq 'install-body-reframework.cmd')
    if ($legacyFiles.Count -eq 0 -and -not $coreImport) {
        Add-Finding $Name 'config-format' 'FAIL' 'converted, and has no src/legacy_config/ or Legacy/ folder; a repo that published a pre-canonical build keeps its frozen legacy import for the life of the repo'
    }
}

function Test-ConfigLegacyReader {
    param([string]$Name, [string]$Root)

    $state = $CanonicalConfig[$Root]
    if (-not $state.converted) { return }

    $uses = [ordered]@{}
    foreach ($rel in @(Get-TrackedFiles $Root)) {
        if ($rel -notmatch $LEGACY_SCAN_SOURCE -or $rel -match $LEGACY_SCAN_SKIP -or $rel -match $DIFFERENTIAL_TEST_FOLDER -or $rel -cmatch $LEGACY_FOLDER) { continue }
        $lines = [System.IO.File]::ReadAllLines((Join-Path $Root $rel))
        $isBepInEx = $rel.EndsWith('.cs') -and (($lines -join "`n") -match 'BepInEx')
        for ($i = 0; $i -lt $lines.Count; $i++) {
            $symbols = @([regex]::Matches($lines[$i], $LEGACY_READER_SYMBOLS) | ForEach-Object { $_.Groups[1].Value })
            if ($isBepInEx -and $lines[$i] -match $BEPINEX_BIND) { $symbols += 'ConfigFile.Bind' }
            foreach ($symbol in $symbols) {
                $allowed = @($state.allow_legacy_symbols | Where-Object { $_.symbol -eq $symbol -and $_.file -eq $rel })
                if ($allowed.Count -gt 0) { continue }
                $key = "$rel`n$symbol"
                if (-not $uses.Contains($key)) { $uses[$key] = New-Object System.Collections.Generic.List[int] }
                if (-not $uses[$key].Contains($i + 1)) { $uses[$key].Add($i + 1) }
            }
        }
    }
    foreach ($key in $uses.Keys) {
        $rel, $symbol = $key -split "`n"
        Add-Finding $Name 'config-legacy-reader' 'FAIL' "${rel}:$($uses[$key] -join ',') uses $symbol outside the legacy import; a converted repo reads its config with the canonical reader (a use that reads no config goes in data/config-format.json allow_legacy_symbols)"
    }
}

# The quoted-token split cmd.exe's `for %%k in (...)` gives a list: PRESERVE_FILES entries may
# hold spaces inside quotes.
function Get-CmdListItems {
    param([string]$Value)
    return [regex]::Matches($Value, '"([^"]*)"|(\S+)') | ForEach-Object {
        if ($_.Groups[1].Success) { $_.Groups[1].Value } else { $_.Groups[2].Value }
    }
}

function Test-ConfigPreserve {
    param([string]$Name, [string]$Root)

    $state = $CanonicalConfig[$Root]
    if (-not $state.converted) { return }
    $installed = @($state.files | ForEach-Object { @($_.installed) })
    # legacy_source is a bare name: the legacy file sits in the folder of each installed path.
    $legacyPaths = @($state.files | Where-Object { $_.legacy_source } | ForEach-Object {
        $legacyName = $_.legacy_source
        foreach ($at in @($_.installed)) {
            $cut = $at.LastIndexOf('\')
            if ($cut -lt 0) { $legacyName } else { $at.Substring(0, $cut + 1) + $legacyName }
        }
    })

    # A converted install ships no config: not CameraUnlock.ini, not the legacy file, and not the
    # committed file under any other name. MOD_DLLS copies over the player's file on every
    # install. MOD_SEED_FILES writes a file only where none is, and on an update from a legacy
    # build CameraUnlock.ini is absent, so a seeded one stops the mod importing the player's
    # legacy file; on a fresh install a seeded legacy file is imported as if an older build wrote
    # it. The uninstall wrapper's MOD_SEED_FILES mirrors install.cmd's and deletes what it lists.
    $configLeaves = @($installed | ForEach-Object { Split-Path -Leaf $_ })
    $legacyLeaves = @($state.files | Where-Object { $_.legacy_source } | ForEach-Object { $_.legacy_source })
    $committedLeaves = @(@($state.files | Where-Object { $_.committed } | ForEach-Object { $_.committed }) + @($state.unrecorded_stamped) | ForEach-Object { Split-Path -Leaf $_ })
    $listNeither = 'The mod creates CameraUnlock.ini at first launch; list neither file.'
    $installPath = Join-Path $Root 'scripts/install.cmd'
    if (Test-Path $installPath) {
        $vars = Get-ConfigBlockVars (Read-TextFile $installPath)
        foreach ($var in @('MOD_DLLS', 'MOD_SEED_FILES')) {
            if (-not $vars.Contains($var)) { continue }
            foreach ($item in @(Get-CmdListItems $vars[$var])) {
                $leaf = Split-Path -Leaf $item
                $seeded = $var -eq 'MOD_SEED_FILES'
                if ($leaf -in $configLeaves) {
                    $why = if ($seeded) { 'which an update from a legacy build writes before the mod starts, so the mod finds CameraUnlock.ini and never imports the player''s legacy file' } else { 'so every script install copies the default over the player''s settings' }
                } elseif ($leaf -in $legacyLeaves) {
                    $why = if ($seeded) { 'the legacy file, so a fresh install gets a legacy file no older build wrote and the mod imports it' } else { 'the legacy file, so every script install copies a shipped file over the one an older build reads after a rollback' }
                } elseif ($leaf -in $committedLeaves) {
                    $why = 'a copy of the committed config, which a converted release does not ship'
                } else {
                    continue
                }
                Add-Finding $Name 'config-preserve' 'FAIL' "install.cmd's $var lists $item, $why. $listNeither"
            }
        }
    }

    # Only the shared uninstall body reads MOD_SEED_FILES and PRESERVE_FILES. A repo whose
    # uninstall is its own script, or that has none because only the launcher installs it, keeps
    # its config there.
    $uninstallPath = Join-Path $Root 'scripts/uninstall.cmd'
    if (-not (Test-Path $uninstallPath)) { return }
    $uninstallText = Read-TextFile $uninstallPath
    if ((Get-WrapperBodyName $uninstallText) -ne 'uninstall-body.cmd') { return }
    $vars = Get-ConfigBlockVars $uninstallText
    if ($vars.Contains('MOD_SEED_FILES')) {
        foreach ($item in @(Get-CmdListItems $vars['MOD_SEED_FILES'])) {
            $leaf = Split-Path -Leaf $item
            if ($leaf -in $configLeaves) {
                $what = 'the player''s settings'
            } elseif ($leaf -in $legacyLeaves) {
                $what = 'the legacy file an older build reads after a rollback'
            } elseif ($leaf -in $committedLeaves) {
                $what = 'a file named like the committed config'
            } else {
                continue
            }
            Add-Finding $Name 'config-preserve' 'FAIL' "uninstall.cmd's MOD_SEED_FILES lists $item, $what, which the uninstall deletes as a file install.cmd seeded unless PRESERVE_FILES names it. $listNeither"
        }
    }
    $preserved = @(if ($vars.Contains('PRESERVE_FILES')) { Get-CmdListItems $vars['PRESERVE_FILES'] })
    foreach ($path in @($installed + $legacyPaths)) {
        if ($path -in $preserved) { continue }
        Add-Finding $Name 'config-preserve' 'FAIL' "uninstall.cmd's PRESERVE_FILES does not list $path, so an uninstall deletes the player's settings"
    }
}

# Where a converted repo's owners find Defaults.ini. A mod passes DefaultsFile.PerUser() and never
# a fixed path; a test passes DefaultsFile.At with a scratch path and never PerUser, and a test
# source that builds an owner or initialises PluginMod has to name At, since an options helper
# shared with the mod would otherwise hand a test the player's real file. The fleet's C++ mods
# declare a std::optional owner, often in a header, and emplace it in a source file, so the name of
# every optional owner in the repo is collected first and an emplace of one counts as building it.
# A test folder is a folder named test or tests in any case, or one whose name ends in Tests.
$DEFAULTS_SCAN_SOURCE = '\.(c|cc|cpp|cxx|h|hh|hpp|hxx|inl|ipp|cs)$'
$DEFAULTS_AT = '\bDefaultsFile\s*(\.|::)\s*At\b'
$DEFAULTS_PER_USER = '\bDefaultsFile\s*(\.|::)\s*PerUser\b'
$OWNER_BUILT_CPP = '\bnew\s+(\w+\s*::\s*)*ConfigOwner\s*<|\bmake_(unique|shared|optional)\s*<\s*(\w+\s*::\s*)*ConfigOwner\s*<|\bConfigOwner\s*<([^<>;]|<[^<>;]*>)*>\s*([A-Za-z_]\w*\s*)?[({=]'
$OWNER_OPTIONAL_CPP = '\boptional\s*<\s*(\w+\s*::\s*)*ConfigOwner\s*<([^<>;]|<[^<>;]*>)*>\s*>\s*[&*]?\s*(?<name>[A-Za-z_]\w*)'
$OWNER_BUILT_CS = '\bnew\s+(\w+\s*\.\s*)*ConfigOwner\s*<|\bConfigOwner\s*<[^;()]*>\s+[A-Za-z_]\w*\s*=\s*new\s*\('
$PLUGIN_MOD_INIT = '\bPluginMod\s*::\s*Instance\s*\(\s*\)\s*\.\s*Initialize\s*\(|\bInitializePlugin\s*\('
$PLUGIN_MOD_REF = '[&*]\s*(?<name>[A-Za-z_]\w*)\s*=\s*&?\s*(\w+\s*::\s*)*PluginMod\s*::\s*Instance\s*\(\s*\)(?!\s*(\.|->))'

function Test-IsTestSource {
    param([string]$Rel)
    foreach ($folder in @($Rel -split '/' | Select-Object -SkipLast 1)) {
        if ($folder -match '^tests?$' -or $folder -cmatch 'Tests$') { return $true }
    }
    return $false
}

function Test-ConfigDefaults {
    param([string]$Name, [string]$Root)

    $state = $CanonicalConfig[$Root]
    if (-not $state.converted) { return }

    $testRule = 'a test must never read or create the player''s real Defaults.ini, so it passes DefaultsFile.At with a scratch path'
    $sources = [ordered]@{}
    $optionalOwners = New-Object System.Collections.Generic.HashSet[string]
    foreach ($rel in @(Get-TrackedFiles $Root)) {
        if ($rel -notmatch $DEFAULTS_SCAN_SOURCE -or $rel -match $LEGACY_SCAN_SKIP) { continue }
        $sources[$rel] = [System.IO.File]::ReadAllLines((Join-Path $Root $rel))
        if ($rel.EndsWith('.cs')) { continue }
        foreach ($line in $sources[$rel]) {
            foreach ($m in [regex]::Matches($line, $OWNER_OPTIONAL_CPP)) { [void]$optionalOwners.Add($m.Groups['name'].Value) }
        }
    }
    $emplaced = if ($optionalOwners.Count -gt 0) {
        '\b(' + (@($optionalOwners | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')\s*(\.|->)\s*emplace\s*\('
    }
    foreach ($rel in $sources.Keys) {
        $lines = $sources[$rel]
        $isCs = $rel.EndsWith('.cs')
        $built = if ($isCs) { $OWNER_BUILT_CS } else { $OWNER_BUILT_CPP }
        $pluginRefs = @(if (-not $isCs) { foreach ($line in $lines) { foreach ($m in [regex]::Matches($line, $PLUGIN_MOD_REF)) { [regex]::Escape($m.Groups['name'].Value) } } })
        $pluginRefInit = if ($pluginRefs.Count -gt 0) { '\b(' + ($pluginRefs -join '|') + ')\s*(\.|->)\s*Initialize\s*\(' }
        $at = New-Object System.Collections.Generic.List[int]
        $perUser = New-Object System.Collections.Generic.List[int]
        $owners = New-Object System.Collections.Generic.List[int]
        $pluginMod = New-Object System.Collections.Generic.List[int]
        for ($i = 0; $i -lt $lines.Count; $i++) {
            $line = $lines[$i]
            if ($line -cmatch $DEFAULTS_AT) { $at.Add($i + 1) }
            if ($line -cmatch $DEFAULTS_PER_USER) { $perUser.Add($i + 1) }
            if ($line -cmatch $built -or
                (-not $isCs -and (($emplaced -and $line -cmatch $emplaced) -or ($line -cmatch $OWNER_OPTIONAL_CPP -and $line -cmatch '\bin_place\b')))) {
                $owners.Add($i + 1)
            }
            if ($line -cmatch $PLUGIN_MOD_INIT -or ($pluginRefInit -and $line -cmatch $pluginRefInit)) { $pluginMod.Add($i + 1) }
        }
        if (-not (Test-IsTestSource $rel)) {
            if ($at.Count -gt 0) {
                Add-Finding $Name 'config-defaults' 'FAIL' "${rel}:$($at -join ',') names DefaultsFile.At outside a test folder; a mod must never point at a fixed path, so it passes DefaultsFile.PerUser()"
            }
            continue
        }
        if ($perUser.Count -gt 0) {
            Add-Finding $Name 'config-defaults' 'FAIL' "${rel}:$($perUser -join ',') names DefaultsFile.PerUser in a test folder; $testRule"
        }
        if ($at.Count -gt 0) { continue }
        if ($owners.Count -gt 0) {
            Add-Finding $Name 'config-defaults' 'FAIL' "${rel}:$($owners -join ',') builds a ConfigOwner and never names DefaultsFile.At; $testRule"
        }
        if ($pluginMod.Count -gt 0) {
            Add-Finding $Name 'config-defaults' 'FAIL' "${rel}:$($pluginMod -join ',') initialises PluginMod and never names DefaultsFile.At; $testRule"
        }
    }
}

# The config descriptor, the launcher-manifest.json block a launcher reads to find a converted
# mod's config and the rows the game keeps for itself. scripts/check-config-descriptor.mjs --json fails
# a repo delivered by manifest whose one recorded config file is stamped and that has no block,
# holds the committed manifest to every rule in that script except the ones reading
# mod_info.version, which packaging stamps, and holds canonical_since above
# every v* tag whose committed config lacks the stamp, which needs a clone with its tags. It also
# fails a converted repo's manifest, block or not, that seeds or ships through files[] its config,
# its legacy file or a file named like its committed config.
$DescriptorState = @{}

function Test-ConfigDescriptor {
    param([string]$Name, [string]$Root)

    $report = $DescriptorState[$Root]
    foreach ($problem in @($report.problems)) {
        Add-Finding $Name 'config-descriptor' 'FAIL' $problem
    }
    if ($report.shallow) {
        Add-Finding $Name 'config-descriptor' 'WARN' 'a shallow clone has no tags, so canonical_since was not held to them'
    }
}

# A manifest whose config block carries canonical_since needs scripts/release.ps1 to refuse a
# version below it before it tags: Assert-ReleaseNotBelowCanonicalSince, or New-ReleaseTag, which
# runs it. Without either, the only stop is validate-manifest in the tag's CI build, after the
# tag is already pushed. Commented-out calls do not count.
function Test-ReleaseCanonicalSince {
    param([string]$Name, [string]$Root)

    $manifest = Get-LauncherManifest $Root
    if (-not $manifest) { return }
    $config = $manifest.PSObject.Properties['config']
    if (-not $config -or -not $config.Value -or -not $config.Value.PSObject.Properties['canonical_since']) { return }

    $release = Join-Path $Root 'scripts/release.ps1'
    if (-not (Test-Path -LiteralPath $release)) {
        Add-Finding $Name 'release-canonical-since' 'FAIL' 'launcher-manifest.json carries config.canonical_since and there is no scripts/release.ps1 to refuse a release below it before tagging'
        return
    }
    $code = (Read-TextFile $release) -replace '(?s)<#.*?#>', '' -replace '(?m)^\s*#.*$', ''
    if ($code -match '\b(Assert-ReleaseNotBelowCanonicalSince|New-ReleaseTag)\b') { return }
    Add-Finding $Name 'release-canonical-since' 'FAIL' "launcher-manifest.json carries config.canonical_since and scripts/release.ps1 calls neither Assert-ReleaseNotBelowCanonicalSince nor New-ReleaseTag, so a version below it is tagged and pushed before CI refuses it. Call Assert-ReleaseNotBelowCanonicalSince -RepoRoot `$projectDir -Version `$Version as soon as the version is resolved"
}

# ---------------------------------------------------------------------------
# ci-minutes: what a push build and a release run, held against the pixi task
# graph. The files a repo copies are scripts/templates/build-workflow.yml,
# pixi-test-tasks.toml and release-full-test.ps1.
# ---------------------------------------------------------------------------

# A native command whose stderr is an ordinary answer. Under Windows PowerShell
# 5.1 a native command's stderr line becomes a NativeCommandError record, which
# this script's $ErrorActionPreference = 'Stop' turns into a terminating error.
function Invoke-NativeQuiet {
    param([scriptblock]$Command)
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $all = @(& $Command 2>&1)
    } finally {
        $ErrorActionPreference = $saved
    }
    return [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Out      = @($all | Where-Object { $_ -isnot [System.Management.Automation.ErrorRecord] } | ForEach-Object { "$_" })
        Err      = @($all | Where-Object { $_ -is [System.Management.Automation.ErrorRecord] } | ForEach-Object { "$_" })
    }
}

# pixi's own reading of the manifest, so every spelling pixi accepts (inline
# tables, [tasks.name] tables, feature tasks, depends-on objects) reads the way
# pixi runs it. A task defined in several environments is merged into one.
function Get-PixiTaskGraph {
    param([string]$Root)
    $manifest = Join-Path $Root 'pixi.toml'
    $result = Invoke-NativeQuiet { pixi task list --json --manifest-path $manifest }
    if ($result.ExitCode -ne 0) { return [pscustomobject]@{ Error = "exit $($result.ExitCode): $(($result.Err -join ' ').Trim())"; Tasks = $null } }
    $graph = @{}
    foreach ($environment in @(($result.Out -join "`n") | ConvertFrom-Json)) {
        foreach ($feature in @($environment.features)) {
            foreach ($task in @($feature.tasks)) {
                if (-not $graph.ContainsKey($task.name)) {
                    $graph[$task.name] = [pscustomobject]@{ Cmd = $task.cmd; Deps = (New-Object System.Collections.Generic.HashSet[string]) }
                }
                foreach ($dep in @($task.depends_on)) {
                    if ($dep) { [void]$graph[$task.name].Deps.Add($dep.task_name) }
                }
            }
        }
    }
    return [pscustomobject]@{ Error = $null; Tasks = $graph }
}

# Every task that running $Tasks runs, the named ones included.
function Get-TaskReach {
    param([hashtable]$Graph, [string[]]$Tasks)
    $seen = New-Object System.Collections.Generic.HashSet[string]
    $stack = New-Object System.Collections.Generic.Stack[string]
    foreach ($t in $Tasks) { $stack.Push($t) }
    while ($stack.Count -gt 0) {
        $t = $stack.Pop()
        if (-not $seen.Add($t)) { continue }
        if ($Graph.ContainsKey($t)) {
            foreach ($d in $Graph[$t].Deps) { $stack.Push($d) }
        }
    }
    return ,$seen
}

# The tasks a workflow runs, in order. Only a `pixi run` that starts a command
# counts, so the task named in an error message such as
# "::error::pixi run test failed" is not taken for a step that runs it.
function Get-WorkflowPixiTasks {
    param([string[]]$Lines)
    $tasks = New-Object System.Collections.Generic.List[string]
    foreach ($line in $Lines) {
        if ($line -match '^\s*#') { continue }
        foreach ($m in [regex]::Matches($line, '(?:^\s*(?:-\s+)?(?:run:\s*)?|[;&|{]\s*)pixi\s+run\s+(?:(?:-e|--environment)\s+\S+\s+)?([A-Za-z0-9_.-]+)')) {
            $tasks.Add($m.Groups[1].Value)
        }
    }
    return ,$tasks
}

# The top-level block that starts with "${Key}:", its key line included.
function Get-TopLevelBlock {
    param([string[]]$Lines, [string]$Key)
    $block = New-Object System.Collections.Generic.List[string]
    $inside = $false
    foreach ($line in $Lines) {
        if ($line -match '^#') {
            if ($inside) { $block.Add($line) }
            continue
        }
        if ($line -match '^\S') {
            if ($inside) { break }
            if ($line -match ('^["'']?' + [regex]::Escape($Key) + '["'']?\s*:')) { $inside = $true }
        }
        if ($inside) { $block.Add($line) }
    }
    return ,$block
}

# The triggers under `on:`, each with the lines beneath it, from the block form
# or from the one-line `on: push` and `on: [push, pull_request]` forms.
function Get-WorkflowTriggers {
    param([string[]]$Lines)
    $triggers = [ordered]@{}
    $block = Get-TopLevelBlock $Lines 'on'
    if ($block.Count -eq 0) { return $triggers }
    if ($block[0] -match '^["'']?on["'']?\s*:\s*([^#\s].*)$') {
        foreach ($name in ($Matches[1] -replace '[\[\]\s]', '' -split ',')) {
            if ($name) { $triggers[$name] = @() }
        }
        return $triggers
    }
    $indent = -1
    $current = $null
    foreach ($line in ($block | Select-Object -Skip 1)) {
        if ($line -match '^\s*(#|$)') { continue }
        $lead = $line.Length - $line.TrimStart().Length
        if ($indent -lt 0) { $indent = $lead }
        if ($lead -eq $indent -and $line -match '^\s*([A-Za-z_]+)\s*:') {
            $current = $Matches[1]
            $triggers[$current] = @()
        } elseif ($current) {
            $triggers[$current] += $line
        }
    }
    return $triggers
}

# The release-mod.yml a caller pins, read out of this core checkout. $null when
# the commit is not here: a shallow clone, or a checkout older than the pin.
function Get-PinnedReleaseWorkflow {
    param([string]$Sha)
    $result = Invoke-NativeQuiet { git -C $CoreRoot show "$($Sha):.github/workflows/release-mod.yml" }
    if ($result.ExitCode -ne 0) { return $null }
    return ($result.Out -join "`n")
}

function Test-CiMinutes {
    param([string]$Name, [string]$Root)

    if (-not (Test-Path -LiteralPath (Join-Path $Root 'pixi.toml'))) { return }
    $read = Get-PixiTaskGraph $Root
    if ($read.Error) {
        Add-Finding $Name 'ci-minutes' 'FAIL' "pixi task list could not read pixi.toml ($($read.Error))"
        return
    }
    $graph = $read.Tasks
    $hasDifferential = Test-Path -LiteralPath (Join-Path $Root 'tests/config_differential') -PathType Container

    $split = $graph.ContainsKey('test-differential')
    if ($hasDifferential -and -not $split) {
        Add-Finding $Name 'ci-minutes' 'FAIL' 'the repo has tests/config_differential and pixi.toml has no test-differential task, so the release paths cannot run the differential on its own and a push build that tests runs it. Split test into test-unit and test-differential, and make package depend on test-unit (scripts/templates/pixi-test-tasks.toml)'
    }

    # A release runs its package task and then, where there is one, the differential.
    $releaseExtra = @(if ($hasDifferential) { 'test-differential' })

    # What `pixi run test` runs and running $Tasks does not.
    $skippedBy = {
        param([string[]]$Tasks)
        if (-not $graph.ContainsKey('test')) { return @() }
        $release = Get-TaskReach $graph $Tasks
        if ($release.Contains('test')) { return @() }
        $test = Get-TaskReach $graph @('test')
        $skipped = @($test | Where-Object { $_ -ne 'test' -and -not $release.Contains($_) } | Sort-Object)
        if ($graph['test'].Cmd) { $skipped = @("test's own command") + $skipped }
        return $skipped
    }

    if ($hasDifferential -and $split) {
        if ($graph.ContainsKey('package') -and (Get-TaskReach $graph @('package')).Contains('test-differential')) {
            Add-Finding $Name 'ci-minutes' 'FAIL' 'package runs test-differential, so every push build runs the slow differential. package depends on test-unit, and test on test-unit and test-differential'
        }
        if ($graph.ContainsKey('test') -and -not (Get-TaskReach $graph @('test')).Contains('test-differential')) {
            Add-Finding $Name 'ci-minutes' 'FAIL' '`pixi run test` does not run test-differential, so a developer''s full run skips the differential'
        }
    }
    if ($graph.ContainsKey('package') -and ($split -or -not $hasDifferential)) {
        $skipped = @(& $skippedBy (@('package') + $releaseExtra))
        if ($skipped.Count -gt 0) {
            Add-Finding $Name 'ci-minutes' 'FAIL' "``pixi run test`` runs $($skipped -join ', '), which neither package nor $(if ($hasDifferential) { 'test-differential' } else { 'anything package depends on' }) runs, so push builds and releases skip it"
        }
    }

    foreach ($wf in Get-WorkflowFiles $Root) {
        $lines = [System.IO.File]::ReadAllLines($wf.FullName)
        $text = $lines -join "`n"
        if ($text -match '(?m)^\s*workflow_call\s*:') { continue }
        $triggers = Get-WorkflowTriggers $lines
        $pushTags = $triggers.Contains('push') -and @(@($triggers['push']) -match '^\s*tags\s*:').Count -gt 0
        $callsRelease = $text -match '(?m)^\s*(-\s+)?uses:\s*\S+/\.github/workflows/release-(bepinex-)?mod\.yml@'
        $ran = Get-WorkflowPixiTasks $lines

        if ($callsRelease -or $pushTags) {
            if ($text -match '(?m)^\s*cancel-in-progress\s*:\s*true') {
                Add-Finding $Name 'ci-minutes' 'FAIL' "$($wf.Name) sets cancel-in-progress: true, so a second run for the same ref cancels a release part-way through publishing it"
            }
            if ($text -match '(?m)^\s*(?:-\s+)?uses:\s*\S+/\.github/workflows/release-mod\.yml@([0-9a-f]{40})') {
                $sha = $Matches[1]
                if ($hasDifferential) {
                    $pinned = Get-PinnedReleaseWorkflow $sha
                    if ($null -eq $pinned) {
                        Add-Finding $Name 'ci-minutes' 'WARN' "$($wf.Name) pins release-mod.yml at $($sha.Substring(0, 8)), which this core checkout does not have, so whether that release runs the config differential is unchecked. Fetch core and re-run"
                    } elseif ($pinned -notmatch 'pixi run test-differential|Invoke-ConfigDifferential') {
                        Add-Finding $Name 'ci-minutes' 'FAIL' "$($wf.Name) pins release-mod.yml at $($sha.Substring(0, 8)), which has no config differential step, so a release never runs the differential. Pin a core commit that has one"
                    }
                }
                $packageTask = if ($text -match '(?m)^\s*package-task\s*:\s*[''"]?([A-Za-z0-9_.-]+)') { $Matches[1] } else { 'package' }
                if ($packageTask -ne 'package') {
                    $skipped = @(& $skippedBy (@($packageTask) + $releaseExtra))
                    if ($skipped.Count -gt 0) {
                        Add-Finding $Name 'ci-minutes' 'FAIL' "$($wf.Name) releases through $packageTask, which does not run $($skipped -join ', ') from ``pixi run test``"
                    }
                }
            } elseif (-not $callsRelease) {
                $skipped = @(& $skippedBy @($ran))
                if ($skipped.Count -gt 0) {
                    Add-Finding $Name 'ci-minutes' 'FAIL' "$($wf.Name) is a release workflow that does not run $($skipped -join ', ') from ``pixi run test``, so a release can ship without the full suite"
                }
            }
            continue
        }

        if (-not ($triggers.Contains('push') -or $triggers.Contains('pull_request'))) { continue }
        if ($ran.Count -eq 0) { continue }

        $concurrency = Get-TopLevelBlock $lines 'concurrency'
        $group = @($concurrency | Where-Object { $_ -match '^\s*group\s*:' })
        $cancels = @($concurrency | Where-Object { $_ -match '^\s*cancel-in-progress\s*:\s*true\s*$' }).Count -gt 0
        if ($group.Count -eq 0 -or $group[0] -notmatch 'github\.ref' -or -not $cancels) {
            Add-Finding $Name 'ci-minutes' 'FAIL' "$($wf.Name) has no top-level concurrency block grouped on github.ref with cancel-in-progress: true, so a push that a newer one superseded keeps building"
        }

        if (@($lines | Where-Object { $_ -match '^\s*(?:-\s+)?uses:\s*[''"]?actions/upload-artifact@' }).Count -gt 0) {
            Add-Finding $Name 'ci-minutes' 'FAIL' "$($wf.Name) uploads an artifact. Players get installers from GitHub Releases, so a push build's artifact only costs Actions storage. Delete the upload-artifact step and any step that only stages files for it"
        }

        foreach ($trigger in @('push', 'pull_request')) {
            if (-not $triggers.Contains($trigger)) { continue }
            $body = @($triggers[$trigger])
            # A build that reads prose has to leave '**.md' out (the-witness runs
            # conformance, whose readme check reads README.md), so 'docs/**' counts too.
            $ignoresProse = @($body | Where-Object { $_ -match '^\s*paths-ignore\s*:' }).Count -gt 0 -and
                @($body | Where-Object { $_ -match '^\s*-\s*[''"]?(\*\*/\*\.md|\*\*\.md|\*\.md|docs/\*\*)[''"]?\s*$' }).Count -gt 0
            $allowList = @($body | Where-Object { $_ -match '^\s*paths\s*:' }).Count -gt 0
            if (-not ($ignoresProse -or $allowList)) {
                Add-Finding $Name 'ci-minutes' 'FAIL' "$($wf.Name)'s $trigger trigger has no paths-ignore for '**.md', 'docs/**' and 'LICENSE', so a docs-only push builds"
            }
        }

        $distinct = @($ran | Select-Object -Unique)
        foreach ($task in $distinct) {
            if ($task -notmatch '^test') { continue }
            foreach ($other in $distinct) {
                if ($other -ne $task -and (Get-TaskReach $graph @($other)).Contains($task)) {
                    Add-Finding $Name 'ci-minutes' 'FAIL' "$($wf.Name) runs ``pixi run $task`` and ``pixi run $other``, and $other runs $task again, so the suite runs twice. Drop the $task step"
                }
            }
        }

        $reach = Get-TaskReach $graph $distinct
        if ($hasDifferential -and $split -and $reach.Contains('test-differential')) {
            Add-Finding $Name 'ci-minutes' 'FAIL' "$($wf.Name) runs test-differential on every push. It belongs to the release paths only"
        }
        if ($graph.ContainsKey('test') -and @($reach | Where-Object { $_ -match '^test' }).Count -eq 0) {
            Add-Finding $Name 'ci-minutes' 'FAIL' "$($wf.Name) runs no test task, so a push is never tested. Make package depend on $(if ($hasDifferential) { 'test-unit' } else { 'test' })"
        }
    }

    $release = Join-Path $Root 'scripts/release.ps1'
    if (Test-Path -LiteralPath $release) {
        $code = (Read-TextFile $release) -replace '(?s)<#.*?#>', '' -replace '(?m)^\s*#.*$', ''
        if ($code -notmatch '(?m)(?:^|[;{&])\s*pixi\s+run\s+(?:(?:-e|--environment)\s+\S+\s+)?test(?![-\w])' -and
            $code -notmatch '(?m)(?:^|[;{&])\s*Invoke-ReleaseTestSuite(?![-\w])') {
            Add-Finding $Name 'ci-minutes' 'FAIL' 'scripts/release.ps1 never runs `pixi run test` or Invoke-ReleaseTestSuite, so a tag is pushed without the full suite having passed first. Paste scripts/templates/release-full-test.ps1 before its first file edit'
        }
    }
}

# ---------------------------------------------------------------------------
# pipeline-port: a hand-written port of core's tracking pipeline. A mod that can
# load native code links core and does not port it; a port is for a host that
# forbids native code, and it runs core's pipeline vectors in `pixi run test`
# (docs/porting-the-pipeline.md).
# ---------------------------------------------------------------------------

# The languages core does not ship that the fleet's mods are written in.
$PORT_SOURCE = '\.(java|kt|lua|py|rs)$'

# A port is found by what it does, in two stages the vectors cover. Neither is
# "opens a UDP socket": dying-light-2, subnautica and the-long-dark each track a
# Python script that sends test poses, and cyberpunk-2077's Lua opens no socket
# at all, since its native DLL receives, and still carries an interpolator.
#
# The packet layer: a file that names OpenTrack, opens a UDP socket and reads
# from it.
$PORT_OPENTRACK = '(?i)opentrack'
$PORT_UDP_OPEN = '\b(DatagramSocket|DatagramChannel|UdpSocket|SOCK_DGRAM)\b|\budp[46]?\s*\('
$PORT_UDP_READ = '\b(recv\w*|receive(from)?)\s*\('
# The interpolator: a file that estimates the tracker's sample interval and
# extrapolates past the newest sample. Every port of PoseInterpolator names both.
$PORT_SAMPLE_INTERVAL = '(?i)sample[_ ]?interval'
$PORT_EXTRAPOLATES = '(?i)extrapolat'

# The repo's own native build takes core: a CMake target that links
# `cameraunlock`, or a Cargo build script that compiles sources out of
# cameraunlock-core/cpp into the crate's DLL (bioshock-remastered's build.rs).
$PORT_CMAKE_LINKS_CORE = '(?s)\btarget_link_libraries\s*\([^)]*\bcameraunlock\w*'
$PORT_CARGO_BUILDS_CORE = 'cameraunlock-core/cpp/'

# The sentence the check quotes from docs/porting-the-pipeline.md, "Before
# porting: if you can link the core, link it". test-conformance-checks.ps1 holds
# the two to the same words.
$PORT_LINK_IT = 'If the mod already loads a native DLL of its own, link `cameraunlock` and do not port - not the packet layer, not the interpolators, not the processors.'

function Test-PipelinePort {
    param([string]$Name, [string]$Root)

    $tracked = @(Get-TrackedFiles $Root | Where-Object { $_ -notmatch $LEGACY_SCAN_SKIP })
    $packet = New-Object System.Collections.Generic.List[string]
    $interpolator = New-Object System.Collections.Generic.List[string]
    foreach ($rel in $tracked) {
        if ($rel -notmatch $PORT_SOURCE -or (Test-IsTestSource $rel)) { continue }
        $text = Read-TextFile (Join-Path $Root $rel)
        if ($text -match $PORT_OPENTRACK -and $text -cmatch $PORT_UDP_OPEN -and $text -cmatch $PORT_UDP_READ) { $packet.Add($rel) }
        if ($text -match $PORT_SAMPLE_INTERVAL -and $text -match $PORT_EXTRAPOLATES) { $interpolator.Add($rel) }
    }
    if ($packet.Count -eq 0 -and $interpolator.Count -eq 0) { return }

    $stages = @()
    if ($packet.Count -gt 0) { $stages += "the packet layer ($($packet -join ', '))" }
    if ($interpolator.Count -gt 0) { $stages += "the interpolator ($($interpolator -join ', '))" }
    $ported = "ports $($stages -join ' and ') of core's tracking pipeline to a language core does not ship"

    $links = @($tracked | Where-Object {
        $leaf = ($_ -split '/')[-1]
        ($leaf -eq 'CMakeLists.txt' -and (Read-TextFile (Join-Path $Root $_)) -match $PORT_CMAKE_LINKS_CORE) -or
        ($leaf -eq 'build.rs' -and (Read-TextFile (Join-Path $Root $_)).Contains($PORT_CARGO_BUILDS_CORE))
    })
    if ($links.Count -gt 0) {
        Add-Finding $Name 'pipeline-port' 'FAIL' "$ported, and $($links -join ', ') already builds core into this mod's own native code. docs/porting-the-pipeline.md: `"$PORT_LINK_IT`" Have the native code run that stage and hand the result up, and delete the port"
        return
    }

    $fix = 'A port is for a host that forbids native code, and it runs core''s pipeline vectors in its tests: write the harness (docs/porting-the-pipeline.md, Conformance vectors) and a test-vectors task that test depends on, as minecraft-java-edition-headtracking has'
    if (-not (Test-Path -LiteralPath (Join-Path $Root 'pixi.toml'))) {
        Add-Finding $Name 'pipeline-port' 'FAIL' "$ported, and the repo has no pixi.toml, so nothing runs cameraunlock-core/scripts/pipeline-vectors/run-vectors.mjs against it. $fix"
        return
    }
    $read = Get-PixiTaskGraph $Root
    if ($read.Error) {
        Add-Finding $Name 'pipeline-port' 'FAIL' "pixi task list could not read pixi.toml ($($read.Error))"
        return
    }
    $graph = $read.Tasks
    $vectorTasks = @($graph.Keys | Where-Object { "$($graph[$_].Cmd)" -match 'run-vectors\.mjs' } | Sort-Object)
    if ($vectorTasks.Count -eq 0) {
        Add-Finding $Name 'pipeline-port' 'FAIL' "$ported, and no pixi task runs cameraunlock-core/scripts/pipeline-vectors/run-vectors.mjs against it. $fix"
        return
    }
    $reach = Get-TaskReach $graph @('test')
    if (@($vectorTasks | Where-Object { $reach.Contains($_) }).Count -eq 0) {
        Add-Finding $Name 'pipeline-port' 'FAIL' "$ported, and ``pixi run test`` does not run $($vectorTasks -join ' or '), so the vectors run only when someone remembers them. Make test depend on $($vectorTasks[0])"
    }
}

# ---------------------------------------------------------------------------
# changelog-unreleased: a hand-kept [Unreleased] section long enough to be the
# history of the work. New-ChangelogFromCommits renames a non-empty [Unreleased]
# to the release's heading and keeps it as written, so every bullet in it is
# published as one version's entry.
# ---------------------------------------------------------------------------

# Measured across the fleet on 2026-10-07: 147 repos have a CHANGELOG.md, 35 of
# them with an empty or absent [Unreleased]. The other 112 hold 1 to 66
# top-level bullets, and then project-zomboid-headtracking holds 197. Five
# repos are above 50: 197, 66, 61, 60 and 51. A lower number warns on work in
# progress across the fleet, and the conversion to the canonical config alone
# adds about a dozen bullets (scripts/templates/canonical-config-changelog.md).
$MAX_UNRELEASED_BULLETS = 50

function Test-ChangelogUnreleased {
    param([string]$Name, [string]$Root)

    $path = Join-Path $Root 'CHANGELOG.md'
    if (-not (Test-Path -LiteralPath $path)) { return }
    # The same section and the same bullet New-ChangelogFromCommits reads, with `* ` counted too.
    $section = [regex]::Match((Read-TextFile $path), '(?ms)^## \[Unreleased\][^\r\n]*?\r?$(?<body>.*?)(?=^## |\z)')
    if (-not $section.Success) { return }
    $bullets = [regex]::Matches($section.Groups['body'].Value, '(?m)^[-*] ').Count
    if ($bullets -le $MAX_UNRELEASED_BULLETS) { return }

    $tags = Invoke-NativeQuiet { git -C $Root tag --list 'v[0-9]*' }
    $released = $tags.ExitCode -eq 0 -and @($tags.Out | Where-Object { $_ }).Count -gt 0
    $fix = if ($released) {
        'Cut it down to what this release changes for a player, checked against the build. Or empty the section, and the release writes the entry from the feat:, fix: and perf: commit subjects since the last v* tag'
    } else {
        'The repo has no v* tag, and an emptied section would be filled from every commit subject in its history, so rewrite it as the short list of what the first release does, checked against the build'
    }
    Add-Finding $Name 'changelog-unreleased' 'WARN' "CHANGELOG.md's [Unreleased] holds $bullets bullets (more than $MAX_UNRELEASED_BULLETS). scripts/release.ps1 renames a non-empty [Unreleased] to the version's heading and keeps it as written, reading no commit (New-ChangelogFromCommits), so all $bullets are published as one version's entry, and a bullet written the day a behaviour landed stays after the behaviour is replaced. $fix"
}

$CHECK_TABLE = [ordered]@{
    'install-wrapper'   = ${function:Test-InstallWrapper}
    'delayed-expansion' = ${function:Test-DelayedExpansion}
    'arg-parser'        = ${function:Test-ArgParser}
    'config-block'      = ${function:Test-ConfigBlock}
    'config-pairing'    = ${function:Test-ConfigPairing}
    'shim-marker'       = ${function:Test-ShimMarker}
    'cmd-crlf'          = ${function:Test-CmdCrlf}
    'pixi-tasks'        = ${function:Test-PixiTasks}
    'action-pins'       = ${function:Test-ActionPins}
    'workflow-ref'      = ${function:Test-WorkflowRef}
    'workflow-build'    = ${function:Test-WorkflowBuild}
    'core-pin'          = ${function:Test-CorePin}
    'manifest'          = ${function:Test-Manifest}
    'manifest-seed'     = ${function:Test-ManifestSeed}
    'mod-version'       = ${function:Test-ModVersion}
    'stray-manifest'    = ${function:Test-StrayManifest}
    'license'           = ${function:Test-License}
    'readme'            = ${function:Test-Readme}
    'config-format'        = ${function:Test-ConfigFormat}
    'config-legacy-reader' = ${function:Test-ConfigLegacyReader}
    'config-preserve'      = ${function:Test-ConfigPreserve}
    'config-descriptor'    = ${function:Test-ConfigDescriptor}
    'config-defaults'      = ${function:Test-ConfigDefaults}
    'release-canonical-since' = ${function:Test-ReleaseCanonicalSince}
    'ci-minutes'        = ${function:Test-CiMinutes}
    'pipeline-port'     = ${function:Test-PipelinePort}
    'changelog-unreleased' = ${function:Test-ChangelogUnreleased}
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
    # A mod repo, not every sibling checkout: either a git checkout that
    # vendors this core, or one whose name marks it as a head-tracking mod.
    # lopari, headcam and quickfeed all have scripts/ and a pixi.toml, none of
    # them vendors core and none is named like a mod, and none of these
    # invariants applies to them.
    #
    # Neither signal alone is enough. Vendoring alone (the previous rule)
    # missed homeworld-remastered-collection and kingdom-come-deliverance-2,
    # which pin core but do not carry the `-headtracking` suffix - that is why
    # this rule replaced a bare `-headtracking` name match in the first place.
    # But vendoring alone also misses a mod repo that has not (yet, or ever)
    # taken a core dependency: a-plague-tale-innocence-headtracking,
    # baldurs-gate-3-headtracking, euro-truck-simulator-2-headtracking,
    # generic-freelook-headtracking, poppy-playtime-headtracking,
    # red-dead-redemption-head-tracking, system-shock-2-headtracking,
    # the-long-dark-head-tracking and universal-head-tracking all ship (or are
    # building toward) a head-tracking mod with nothing under
    # `cameraunlock-core` checked out, so the vendoring-only rule scanned none
    # of them. Checking either signal - name or vendoring - is what catches
    # both groups without re-excluding the two that motivated the first
    # widening. Individual checks that need a core pin (core-pin, and any
    # wrapper/action-pin sync unit that needs scripts/*.cmd or
    # .github/workflows to exist) already no-op or WARN gracefully when the
    # thing they need is absent; that is what makes it safe to widen the
    # selection instead of the checks.
    #
    # sync-templates.ps1 and sync-core-notices.ps1 use this same rule.
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

if ($roots.Count -eq 0) { throw "No repos to check under $ReposRoot." }

# One node run over every root, its JSON array read back in root order. The roots go in a file,
# not argv: Windows caps a command line at 32767 characters, which a fleet of a few hundred
# absolute repo paths passes.
function Invoke-NodeOverRoots {
    param([string]$Script, [string[]]$Arguments, [string[]]$Roots)
    $rootsFile = [System.IO.Path]::GetTempFileName()
    try {
        [System.IO.File]::WriteAllLines($rootsFile, $Roots, (New-Object System.Text.UTF8Encoding $false))
        $out = & node (Join-Path $CoreRoot $Script) @Arguments --roots-file $rootsFile
        if ($LASTEXITCODE -ne 0) { throw "$Script $($Arguments -join ' ') failed with exit code $LASTEXITCODE" }
    } finally {
        Remove-Item -LiteralPath $rootsFile
    }
    return @(($out -join "`n") | ConvertFrom-Json)
}

if (@($selected | Where-Object { $_ -in $CONFIG_CHECK_IDS }).Count -gt 0) {
    $states = Invoke-NodeOverRoots 'scripts/check-canonical-config.mjs' @('--json') $roots
    for ($i = 0; $i -lt $roots.Count; $i++) { $CanonicalConfig[$roots[$i]] = $states[$i] }
}

if ('config-descriptor' -in $selected) {
    $reports = Invoke-NodeOverRoots 'scripts/check-config-descriptor.mjs' @('--json') $roots
    for ($i = 0; $i -lt $roots.Count; $i++) { $DescriptorState[$roots[$i]] = $reports[$i] }
}

if ('readme' -in $selected) {
    $results = Invoke-NodeOverRoots 'scripts/generate-readme.mjs' @('--json', '--sections', 'config') $roots
    for ($i = 0; $i -lt $roots.Count; $i++) { $ReadmeConfig[$roots[$i]] = $results[$i] }
}

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
            Write-Host ("  {0,-4} {1,-20} {2}" -f $f.severity, $f.check, $f.message) -ForegroundColor $colour
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
