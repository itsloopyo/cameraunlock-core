#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# Tests for scripts/sync-core-notices.ps1
# ============================================================================
# Run: pixi run test-sync-core-notices
#
# Drives the script itself against throwaway mod repos. The pin it stamps is
# the gitlink in the mod repo's index. It used to read HEAD's tree, so in the
# middle of a merge that brought in a core bump (seen in witcher-3) it wrote
# the pre-merge commit over the notices the merge had just brought in.
#
# Branches are built with plumbing (a private index, commit-tree, update-ref)
# so nothing here has to switch branches. The gitlinks name commits that do
# not exist anywhere: the script only reads pointers, never core itself.
# ============================================================================

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$syncScript = Join-Path $PSScriptRoot 'sync-core-notices.ps1'
$sandbox = Join-Path ([System.IO.Path]::GetTempPath()) "cuc-sync-notices-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $sandbox -Force | Out-Null

$pinA  = '1111111111111111111111111111111111111111'
$pinB  = '2222222222222222222222222222222222222222'
$pinC  = '3333333333333333333333333333333333333333'
$stale = '4444444444444444444444444444444444444444'

$script:Failures = 0

function Check {
    param([string]$Name, [bool]$Condition, [string]$Detail)
    if ($Condition) {
        Write-Host "PASS  $Name" -ForegroundColor Green
    } else {
        Write-Host "FAIL  $Name - $Detail" -ForegroundColor Red
        $script:Failures++
    }
}

# git prints progress and conflict reports on stderr, which Windows PowerShell
# 5.1 turns into terminating errors under 'Stop'. Exit codes are checked here
# instead, against what each call is expected to return.
function Invoke-Git {
    param([string]$Repo, [string[]]$GitArgs, [int[]]$AllowedExit = @(0))
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $out = @(& git -C $Repo -c user.email=t@t -c user.name=t -c core.autocrlf=false @GitArgs 2>&1 | ForEach-Object { "$_" })
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
    if ($AllowedExit -notcontains $code) {
        throw "git $($GitArgs -join ' ') exited $code in ${Repo}:`n$($out -join "`n")"
    }
    return $out
}

function Get-NoticesBody {
    param([string]$Commit)
    return "# Third-party notices`n`n## cameraunlock-core`n`n- Pinned commit: ``$Commit```n- Licence: MIT`n"
}

function Read-Notices {
    param([string]$Repo)
    return [System.IO.File]::ReadAllText((Join-Path $Repo 'THIRD-PARTY-NOTICES.md'))
}

# A mod repo on main whose committed pin is $Pin and whose committed notices
# name $Notices.
function New-ModRepo {
    param([string]$Name, [string]$Pin, [string]$Notices)
    $repo = Join-Path $sandbox $Name
    New-Item -ItemType Directory -Path $repo -Force | Out-Null
    Invoke-Git $repo @('init', '-q', '-b', 'main') | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $repo '.gitmodules'),
        "[submodule `"cameraunlock-core`"]`n`tpath = cameraunlock-core`n`turl = https://github.com/itsloopyo/cameraunlock-core.git`n")
    [System.IO.File]::WriteAllText((Join-Path $repo 'THIRD-PARTY-NOTICES.md'), (Get-NoticesBody $Notices))
    [System.IO.File]::WriteAllText((Join-Path $repo 'seed.txt'), "seed`n")
    Invoke-Git $repo @('update-index', '--add', '--cacheinfo', "160000,$Pin,cameraunlock-core") | Out-Null
    Invoke-Git $repo @('add', '--', '.gitmodules', 'THIRD-PARTY-NOTICES.md', 'seed.txt') | Out-Null
    Invoke-Git $repo @('commit', '-q', '-m', 'seed') | Out-Null
    return $repo
}

# Commits on top of $Parent through a private index and points refs/heads/$Branch
# at the result, leaving HEAD, the real index and the working tree alone.
function New-BranchCommit {
    param([string]$Repo, [string]$Branch, [string]$Parent, [string]$Pin, [string]$Notices)
    $index = Join-Path $sandbox "index-$([guid]::NewGuid().ToString('N'))"
    $env:GIT_INDEX_FILE = $index
    try {
        Invoke-Git $Repo @('read-tree', $Parent) | Out-Null
        if ($Pin) {
            Invoke-Git $Repo @('update-index', '--cacheinfo', "160000,$Pin,cameraunlock-core") | Out-Null
        }
        if ($Notices) {
            $file = Join-Path $sandbox "notices-$([guid]::NewGuid().ToString('N')).md"
            [System.IO.File]::WriteAllText($file, (Get-NoticesBody $Notices))
            $blob = @(Invoke-Git $Repo @('hash-object', '-w', '--', $file))[0].Trim()
            Invoke-Git $Repo @('update-index', '--cacheinfo', "100644,$blob,THIRD-PARTY-NOTICES.md") | Out-Null
        }
        $tree = @(Invoke-Git $Repo @('write-tree'))[0].Trim()
    } finally {
        Remove-Item Env:GIT_INDEX_FILE
    }
    $commit = @(Invoke-Git $Repo @('commit-tree', $tree, '-p', $Parent, '-m', "on $Branch"))[0].Trim()
    Invoke-Git $Repo @('update-ref', "refs/heads/$Branch", $commit) | Out-Null
    return $commit
}

# A commit on main that touches neither the pin nor the notices, so a merge
# into it is a real merge and not a fast-forward.
function Add-UnrelatedCommit {
    param([string]$Repo)
    [System.IO.File]::WriteAllText((Join-Path $Repo 'seed.txt'), "moved on`n")
    Invoke-Git $Repo @('add', '--', 'seed.txt') | Out-Null
    Invoke-Git $Repo @('commit', '-q', '-m', 'unrelated') | Out-Null
}

function Invoke-Sync {
    param([string]$Repo, [switch]$Check)
    $argv = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $syncScript, '-Repo', $Repo)
    if ($Check) { $argv += '-Check' }
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $out = @(& powershell @argv 2>&1 | ForEach-Object { "$_" })
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
    return [PSCustomObject]@{ Exit = $code; Output = ($out -join "`n") }
}

try {
    # --- a clean repo still gets the committed pin ---------------------------

    $repo = New-ModRepo 'clean' $pinA $stale
    $run = Invoke-Sync $repo
    Check 'a clean repo is stamped with its committed pin' `
        ((Read-Notices $repo).Contains("``$pinA``") -and $run.Exit -eq 0) "exit $($run.Exit), notices: $(Read-Notices $repo)"

    # --- the witcher-3 case: a merge in progress brings a bump and its notices

    $repo = New-ModRepo 'merge-with-notices' $pinA $pinA
    $base = @(Invoke-Git $repo @('rev-parse', 'HEAD'))[0].Trim()
    New-BranchCommit $repo 'bump' $base $pinB $pinB | Out-Null
    Add-UnrelatedCommit $repo
    Invoke-Git $repo @('merge', '--no-ff', '--no-commit', 'bump') | Out-Null

    $staged = @(Invoke-Git $repo @('rev-parse', ':cameraunlock-core'))[0].Trim()
    $headPin = @(Invoke-Git $repo @('rev-parse', 'HEAD:cameraunlock-core'))[0].Trim()
    Check 'fixture: the merge staged the bumped pin while HEAD keeps the old one' `
        ($staged -eq $pinB -and $headPin -eq $pinA) "index $staged, HEAD $headPin"

    $run = Invoke-Sync $repo -Check
    Check 'mid-merge, -Check accepts notices that name the merged pin' ($run.Exit -eq 0) "exit $($run.Exit): $($run.Output)"

    $run = Invoke-Sync $repo
    $written = Read-Notices $repo
    Check 'mid-merge, the notices keep the merged pin' ($written.Contains("``$pinB``")) "notices: $written"
    Check 'mid-merge, the pre-merge pin is not written back' (-not $written.Contains($pinA)) "notices: $written"

    # --- a merge in progress that brings only the pointer ---------------------

    $repo = New-ModRepo 'merge-pointer-only' $pinA $pinA
    $base = @(Invoke-Git $repo @('rev-parse', 'HEAD'))[0].Trim()
    New-BranchCommit $repo 'bump' $base $pinB $null | Out-Null
    Add-UnrelatedCommit $repo
    Invoke-Git $repo @('merge', '--no-ff', '--no-commit', 'bump') | Out-Null

    $run = Invoke-Sync $repo -Check
    Check 'mid-merge, -Check reports notices that still name the pre-merge pin' `
        ($run.Exit -eq 1 -and $run.Output -match 'stale') "exit $($run.Exit): $($run.Output)"
    Check '-Check leaves the notices alone' ((Read-Notices $repo).Contains("``$pinA``")) "notices: $(Read-Notices $repo)"

    $run = Invoke-Sync $repo
    Check 'mid-merge, the notices are restamped to the merged pin' `
        ((Read-Notices $repo).Contains("``$pinB``") -and $run.Exit -eq 0) "exit $($run.Exit), notices: $(Read-Notices $repo)"

    # --- a bump staged but not committed --------------------------------------

    $repo = New-ModRepo 'staged-bump' $pinA $pinA
    Invoke-Git $repo @('update-index', '--cacheinfo', "160000,$pinB,cameraunlock-core") | Out-Null
    $run = Invoke-Sync $repo
    Check 'a staged bump is stamped with the staged pin' `
        ((Read-Notices $repo).Contains("``$pinB``") -and $run.Exit -eq 0) "exit $($run.Exit), notices: $(Read-Notices $repo)"

    # --- a conflicted pointer has no pin to stamp -----------------------------

    $repo = New-ModRepo 'merge-conflict' $pinA $pinA
    $base = @(Invoke-Git $repo @('rev-parse', 'HEAD'))[0].Trim()
    New-BranchCommit $repo 'bump' $base $pinC $null | Out-Null
    Invoke-Git $repo @('update-index', '--cacheinfo', "160000,$pinB,cameraunlock-core") | Out-Null
    Invoke-Git $repo @('commit', '-q', '-m', 'bump on main') | Out-Null
    Invoke-Git $repo @('merge', '--no-ff', '--no-commit', 'bump') -AllowedExit @(1) | Out-Null

    $stages = @(Invoke-Git $repo @('ls-files', '--stage', '--', 'cameraunlock-core'))
    Check 'fixture: the merge left the pointer conflicted' ($stages.Count -ge 2) "ls-files: $($stages -join ' | ')"

    $run = Invoke-Sync $repo
    Check 'a conflicted pointer fails the sync' `
        ($run.Exit -ne 0 -and $run.Output -match 'unresolved merge conflict') "exit $($run.Exit): $($run.Output)"
    Check 'a conflicted pointer leaves the notices alone' ((Read-Notices $repo).Contains("``$pinA``")) "notices: $(Read-Notices $repo)"

    # --- no index entry falls back to HEAD's tree -----------------------------

    $repo = New-ModRepo 'unstaged-removal' $pinA $stale
    Invoke-Git $repo @('rm', '-q', '--cached', '--', 'cameraunlock-core') | Out-Null
    $left = @(Invoke-Git $repo @('ls-files', '--stage', '--', 'cameraunlock-core'))
    Check 'fixture: the index has no cameraunlock-core entry' ($left.Count -eq 0) "ls-files: $($left -join ' | ')"
    $run = Invoke-Sync $repo
    Check 'with no index entry, the pin comes from HEAD' `
        ((Read-Notices $repo).Contains("``$pinA``") -and $run.Exit -eq 0) "exit $($run.Exit), notices: $(Read-Notices $repo)"
} finally {
    Remove-Item -LiteralPath $sandbox -Recurse -Force
}

Write-Host ''
if ($script:Failures -gt 0) {
    Write-Host "$($script:Failures) check(s) failed." -ForegroundColor Red
    exit 1
}
Write-Host 'All checks passed.' -ForegroundColor Green
