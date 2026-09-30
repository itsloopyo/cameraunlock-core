# ============================================================================
# The legacy config differential, run only when something it tests changed
# ============================================================================
#
# A mod's config differential (tests/config_differential) proves its import of
# the legacy config file against the published build's own reader. It takes ten
# to forty minutes, and the answer can only change when a file it compiles or
# reads changes: the frozen reader never does, by rule. Running it on every
# release re-proved the same thing, locally and again on the tag build.
#
# So a pass is recorded in tests/config_differential/passed.json, tracked, with
# the files the differential depends on and a hash of each. A release path calls
# Invoke-ConfigDifferential, which re-hashes those files and runs the test only
# when one of them differs. The record travels with the commit, which is how the
# tag build sees the pass the release script made.
#
# What the differential depends on comes from the build that just passed, not
# from a list someone keeps:
#   msbuild  every file the compiler read for the differential executable and
#            the libraries it links, from MSBuild's tracking logs (.tlog).
#   dotnet   every Compile item and project file of the test project and the
#            projects it references, from `dotnet msbuild -getItem`, and the
#            listing of each folder those items come from, so a new file under
#            a compile glob counts too.
#   full     anything else (cargo, a custom runner): every tracked file in the
#            repo and the commit of each submodule. Safe, and rarely skips.
# Every runner adds the whole of tests/config_differential, the committed config
# the migration is compared against, the JavaScript the lint imports and the
# JSON it reads, pixi.toml and the test scripts.
#
# A dependency can only be added by editing a file already on the list (a
# source, a CMakeLists.txt, a project file), so a stale list cannot hide one.
#
# Hashes are git blob ids (`git hash-object`, which applies the repo's line
# ending rules), so a checkout with CRLF endings hashes as the committed file
# does. The files a release bumps the version in are hashed with that version
# masked, or the tag build could never match the record the release script
# made before the bump.
# ============================================================================

Set-StrictMode -Version Latest

$script:StampFormat = 1
$script:StampRelative = 'tests/config_differential/passed.json'
$script:ScanSkip = @('.git', '.pixi', 'node_modules', '.lab', '.vs', 'release')

function Invoke-Git {
    param([Parameter(Mandatory)][string]$Repo, [Parameter(Mandatory)][string[]]$Arguments)
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & git -C $Repo @Arguments 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $prev
    }
    if ($code -ne 0) { throw "git -C $Repo $($Arguments -join ' ') failed ($code): $($out -join ' ')" }
    return @($out | ForEach-Object { "$_" })
}

function Get-DifferentialStampPath {
    param([Parameter(Mandatory)][string]$Root)
    return Join-Path $Root $script:StampRelative
}

function Get-DifferentialStampRelativePath { return $script:StampRelative }

# The repo and each submodule directly under it, each with its tracked files.
function Get-TrackedIndex {
    param([Parameter(Mandatory)][string]$Root)
    $Root = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\', '/')
    $owners = [System.Collections.Generic.List[object]]::new()
    $owners.Add([pscustomobject]@{ Prefix = ''; Dir = $Root; Files = @(Invoke-Git $Root @('ls-files')) })
    $submodules = @()
    if (Test-Path -LiteralPath (Join-Path $Root '.gitmodules')) {
        $submodules = @(Invoke-Git $Root @('config', '-f', '.gitmodules', '--get-regexp', '^submodule\..*\.path$') |
            ForEach-Object { ($_ -split '\s+', 2)[1] })
    }
    foreach ($sub in $submodules) {
        $dir = Join-Path $Root $sub
        if (-not (Test-Path -LiteralPath (Join-Path $dir '.git'))) {
            throw "Submodule $sub is not checked out, so the files the config differential depends on inside it cannot be hashed. Run: git submodule update --init"
        }
        $owners.Add([pscustomobject]@{ Prefix = ($sub -replace '\\', '/') + '/'; Dir = $dir; Files = @(Invoke-Git $dir @('ls-files')) })
    }
    # Absolute path, upper-cased, to repo-relative path: the tracking logs hold
    # upper-cased absolute paths.
    $byAbsolute = @{}
    $tracked = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($o in $owners) {
        foreach ($f in $o.Files) {
            if ($o.Prefix -eq '' -and ($submodules -contains $f)) { continue }
            $rel = $o.Prefix + $f
            [void]$tracked.Add($rel)
            $abs = [System.IO.Path]::GetFullPath((Join-Path $Root $rel)).ToUpperInvariant()
            $byAbsolute[$abs] = $rel
        }
    }
    return [pscustomobject]@{
        Root       = $Root
        Owners     = $owners
        Submodules = $submodules
        Tracked    = $tracked
        ByAbsolute = $byAbsolute
    }
}

# The release version is masked in the files a release bumps it in. Everything
# else in them still counts.
function Get-MaskedText {
    param([Parameter(Mandatory)][string]$RelativePath, [Parameter(Mandatory)][AllowEmptyString()][string]$Text)
    $name = [System.IO.Path]::GetFileName($RelativePath)
    $t = $Text -replace "`r`n", "`n"
    switch -Regex ($name) {
        '^CMakeLists\.txt$' { return [regex]::Replace($t, '(?is)(\bproject\s*\([^)]*?\bVERSION\s+)[0-9][0-9A-Za-z.+-]*', '${1}<version>') }
        '^(pixi|Cargo)\.toml$' { return [regex]::Replace($t, '(?m)^(\s*version\s*=\s*)"[^"]*"', '${1}"<version>"') }
        '\.(csproj|props|targets)$' { return [regex]::Replace($t, '<(Version|VersionPrefix|AssemblyVersion|FileVersion|InformationalVersion)>[^<]*</\1>', '<$1><version></$1>') }
        '^launcher-manifest\.json$' { return [regex]::Replace($t, '("version"\s*:\s*)"[^"]*"', '${1}"<version>"') }
    }
    return $null
}

function Test-MaskedFile {
    param([Parameter(Mandatory)][string]$RelativePath)
    return [System.IO.Path]::GetFileName($RelativePath) -match '^(CMakeLists\.txt|pixi\.toml|Cargo\.toml|launcher-manifest\.json)$|\.(csproj|props|targets)$'
}

function Get-Sha256Hex {
    param([Parameter(Mandatory)][AllowEmptyString()][string]$Text)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return (-join ($sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($Text)) | ForEach-Object { $_.ToString('x2') }))
    } finally { $sha.Dispose() }
}

# The parts of core's data/config-format.json one repo's lint reads: the shared
# sections, and the repo's own entry in each section keyed by repo name. Every
# other repo registers itself in the same file, so hashing all of it would undo
# the record at nearly every core bump. Serialised by node with sorted keys, so
# Windows PowerShell 5.1 and pwsh, whose JSON writers differ, hash it alike.
function Get-ConfigFormatSlice {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Repo)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return 'missing' }
    # Single quotes only: Windows PowerShell 5.1 passes a double quote inside a
    # native argument through unescaped, which cuts the script short.
    $script = @'
const fs = require('fs');
const [path, repo] = process.argv.slice(1);
const f = JSON.parse(fs.readFileSync(path, 'utf8'));
const byRepo = ['legacy', 'exempt', 'configs', 'conversion_notes', 'allow_legacy_symbols', 'per_game'];
const shared = ['schema_version', 'normalisations', 'approved_changes'];
const slice = {};
for (const k of shared) slice[k] = f[k] ?? null;
for (const k of byRepo) slice[k] = f[k] && Object.prototype.hasOwnProperty.call(f[k], repo) ? f[k][repo] : null;
const sorted = (v) => Array.isArray(v) ? v.map(sorted)
  : v && typeof v === 'object' ? Object.fromEntries(Object.keys(v).sort().map((k) => [k, sorted(v[k])])) : v;
process.stdout.write(JSON.stringify(sorted(slice)));
'@
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & node -e $script $Path $Repo 2>&1
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $prev }
    if ($code -ne 0) { throw "node could not read $Path for the config differential's record ($code): $($out -join ' ')" }
    return ($out -join '')
}

# One hash per entry. An entry is a repo-relative file, `dir:<path>` (the sorted
# tracked files under that folder), `slice:<path>#<repo>` (Get-ConfigFormatSlice)
# or `submodule:<path>` (its checked-out commit).
function Get-DifferentialHashes {
    param([Parameter(Mandatory)]$Index, [Parameter(Mandatory)][string[]]$Entries)
    $result = [ordered]@{}
    $plain = @{}
    foreach ($e in ($Entries | Sort-Object -Unique)) {
        if ($e -like 'dir:*') {
            # dir:. is the repo's own tracked files. The record is never part of
            # a listing: it is untracked when written and tracked once committed.
            $folder = $e.Substring(4).TrimEnd('/')
            $subPrefixes = @($Index.Owners | Where-Object { $_.Prefix -ne '' } | ForEach-Object { $_.Prefix })
            $names = [System.Collections.Generic.List[string]]::new()
            foreach ($t in $Index.Tracked) {
                if ($t -eq $script:StampRelative) { continue }
                if ($folder -eq '.') {
                    if (@($subPrefixes | Where-Object { $t.StartsWith($_) }).Count -gt 0) { continue }
                } elseif (-not $t.StartsWith($folder + '/', [StringComparison]::OrdinalIgnoreCase)) {
                    continue
                }
                $names.Add($t)
            }
            $names = @($names | Sort-Object)
            $result[$e] = 'dir:' + (Get-Sha256Hex ($names -join "`n"))
            continue
        }
        if ($e -like 'slice:*') {
            $spec = $e.Substring(6)
            $file, $repo = $spec -split '#', 2
            $result[$e] = 'slice:' + (Get-Sha256Hex (Get-ConfigFormatSlice -Path (Join-Path $Index.Root $file) -Repo $repo))
            continue
        }
        if ($e -like 'submodule:*') {
            $dir = Join-Path $Index.Root $e.Substring(10)
            $result[$e] = 'commit:' + @(Invoke-Git $dir @('rev-parse', 'HEAD'))[0].Trim()
            continue
        }
        $abs = Join-Path $Index.Root $e
        if (-not (Test-Path -LiteralPath $abs -PathType Leaf)) { $result[$e] = 'missing'; continue }
        if (Test-MaskedFile $e) {
            $text = [System.IO.File]::ReadAllText($abs)
            $result[$e] = 'masked:' + (Get-Sha256Hex (Get-MaskedText $e $text))
            continue
        }
        $owner = $Index.Owners | Where-Object { $_.Prefix -ne '' -and $e.StartsWith($_.Prefix) } | Select-Object -First 1
        if (-not $owner) { $owner = $Index.Owners[0] }
        if (-not $plain.ContainsKey($owner.Dir)) { $plain[$owner.Dir] = [System.Collections.Generic.List[object]]::new() }
        $plain[$owner.Dir].Add([pscustomobject]@{ Entry = $e; Relative = $e.Substring($owner.Prefix.Length) })
        $result[$e] = $null
    }
    foreach ($dir in $plain.Keys) {
        # Paths as arguments, in batches that stay well inside the command line
        # limit. Piping them to --stdin-paths from Windows PowerShell puts a byte
        # order mark in front of the first one.
        $items = $plain[$dir]
        for ($start = 0; $start -lt $items.Count; $start += 100) {
            $batch = @($items[$start..([Math]::Min($start + 99, $items.Count - 1))])
            $hashes = @(Invoke-Git $dir (@('hash-object', '--') + @($batch | ForEach-Object { $_.Relative })))
            if ($hashes.Count -ne $batch.Count) { throw "git hash-object in $dir returned $($hashes.Count) hashes for $($batch.Count) files" }
            for ($i = 0; $i -lt $batch.Count; $i++) { $result[$batch[$i].Entry] = 'blob:' + $hashes[$i].Trim() }
        }
    }
    $sorted = [ordered]@{}
    foreach ($k in ($result.Keys | Sort-Object)) { $sorted[$k] = $result[$k] }
    return $sorted
}

# Every *.tlog folder under the repo, skipping tool folders by name and the
# submodule checkouts by path: a build tree's own copy of a submodule's targets
# (build/cameraunlock-core) is part of the build and is kept.
function Get-TlogDirectories {
    param([Parameter(Mandatory)][string]$Root, [string[]]$Skip, [string[]]$SkipPaths)
    $found = [System.Collections.Generic.List[string]]::new()
    $queue = [System.Collections.Generic.Queue[string]]::new()
    $queue.Enqueue($Root)
    while ($queue.Count -gt 0) {
        $dir = $queue.Dequeue()
        foreach ($child in [System.IO.Directory]::EnumerateDirectories($dir)) {
            $name = [System.IO.Path]::GetFileName($child)
            if ($Skip -contains $name) { continue }
            if ($SkipPaths -contains $child) { continue }
            if ($name -like '*.tlog') { $found.Add($child); continue }
            $queue.Enqueue($child)
        }
    }
    return $found
}

# A tlog is UTF-16 with a byte order mark from the compiler and linker, and
# UTF-8 from custom build steps. A line starting with ^ names the source the
# lines after it belong to: a file read, in a read log, and not a file written,
# in a write log.
function Read-TlogPaths {
    param([Parameter(Mandatory)][string]$Path, [switch]$SkipSourceMarkers)
    $paths = [System.Collections.Generic.List[string]]::new()
    $reader = [System.IO.StreamReader]::new($Path, [System.Text.Encoding]::UTF8, $true)
    try { $lines = $reader.ReadToEnd() -split "`r?`n" } finally { $reader.Dispose() }
    foreach ($line in $lines) {
        $l = $line.TrimStart([char]0xFEFF)
        if ($l.StartsWith('^')) {
            if ($SkipSourceMarkers) { continue }
            $l = $l.Substring(1)
        }
        foreach ($part in ($l -split '\|')) {
            $p = $part.Trim()
            if ($p) { $paths.Add($p.ToUpperInvariant()) }
        }
    }
    return $paths
}

# Every tracked file the compiler read for the differential executable and for
# each library it links, followed through the libraries built in the same tree.
function Get-MsbuildDependencies {
    param([Parameter(Mandatory)]$Index)
    $skipPaths = @($Index.Submodules | ForEach-Object { [System.IO.Path]::GetFullPath((Join-Path $Index.Root $_)).TrimEnd('\', '/') })
    $tlogDirs = Get-TlogDirectories -Root $Index.Root -Skip $script:ScanSkip -SkipPaths $skipPaths
    $writer = @{}
    $roots = [System.Collections.Generic.List[string]]::new()
    foreach ($d in $tlogDirs) {
        foreach ($w in [System.IO.Directory]::EnumerateFiles($d, '*.write.*.tlog')) {
            foreach ($p in (Read-TlogPaths $w -SkipSourceMarkers)) {
                $writer[$p] = $d
                if ($p -match '\\[^\\]*DIFFERENTIAL[^\\]*\.EXE$' -and -not $roots.Contains($d)) { $roots.Add($d) }
            }
        }
    }
    if ($roots.Count -eq 0) { return $null }

    $files = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $queue = [System.Collections.Generic.Queue[string]]::new()
    foreach ($r in $roots) { if ($seen.Add($r)) { $queue.Enqueue($r) } }
    while ($queue.Count -gt 0) {
        $d = $queue.Dequeue()
        foreach ($r in [System.IO.Directory]::EnumerateFiles($d, '*.read.*.tlog')) {
            foreach ($p in (Read-TlogPaths $r)) {
                if ($Index.ByAbsolute.ContainsKey($p)) { [void]$files.Add($Index.ByAbsolute[$p]) }
                if ($writer.ContainsKey($p) -and $seen.Add($writer[$p])) { $queue.Enqueue($writer[$p]) }
            }
        }
    }
    foreach ($t in $Index.Tracked) {
        if ($t -match '(^|/)CMakeLists\.txt$|\.cmake$' -and -not ($Index.Owners | Where-Object { $_.Prefix -ne '' -and $t.StartsWith($_.Prefix) })) {
            [void]$files.Add($t)
        }
    }
    return @($files)
}

# One evaluation of a project: its compile items, project references and the
# project files it imports.
function Invoke-DotnetEvaluation {
    param([Parameter(Mandatory)][string]$Project, [string]$TargetFramework)
    $msbuildArgs = @($Project, '-nologo', '-getItem:Compile', '-getItem:ProjectReference',
        '-getProperty:MSBuildAllProjects', '-getProperty:TargetFrameworks', '-p:Configuration=Release')
    if ($TargetFramework) { $msbuildArgs += "-p:TargetFramework=$TargetFramework" }
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $json = & dotnet msbuild @msbuildArgs 2>&1
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $prev }
    if ($code -ne 0) { throw "dotnet msbuild -getItem on $Project failed ($code): $($json -join ' ')" }
    $data = ($json -join "`n") | ConvertFrom-Json
    return [pscustomobject]@{
        Compile          = if ($data.Items.PSObject.Properties['Compile']) { @($data.Items.Compile) } else { @() }
        References       = if ($data.Items.PSObject.Properties['ProjectReference']) { @($data.Items.ProjectReference) } else { @() }
        Imports          = @($data.Properties.MSBuildAllProjects -split ';' | Where-Object { $_ })
        TargetFrameworks = @("$($data.Properties.TargetFrameworks)" -split ';' | Where-Object { $_ })
    }
}

function Get-DotnetDependencies {
    param([Parameter(Mandatory)]$Index, [Parameter(Mandatory)][string]$Project)
    $files = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $queue = [System.Collections.Generic.Queue[string]]::new()
    $queue.Enqueue([System.IO.Path]::GetFullPath((Join-Path $Index.Root $Project)))
    while ($queue.Count -gt 0) {
        $proj = $queue.Dequeue()
        if (-not $seen.Add($proj)) { continue }
        # A multi-targeted project lists its items only per target framework,
        # so it is evaluated once for each and the answers are joined.
        $evaluations = @(Invoke-DotnetEvaluation -Project $proj)
        if ($evaluations[0].TargetFrameworks.Count -gt 0) {
            $evaluations += @($evaluations[0].TargetFrameworks | ForEach-Object { Invoke-DotnetEvaluation -Project $proj -TargetFramework $_ })
        }
        $paths = [System.Collections.Generic.List[string]]::new()
        $paths.Add($proj)
        $compile = @($evaluations | ForEach-Object { $_.Compile })
        $references = @($evaluations | ForEach-Object { $_.References })
        foreach ($e in $evaluations) { foreach ($p in $e.Imports) { $paths.Add($p) } }
        foreach ($item in $compile) {
            $paths.Add($item.FullPath)
            $rel = $Index.ByAbsolute[[System.IO.Path]::GetFullPath($item.FullPath).ToUpperInvariant()]
            if ($rel) {
                $folder = [System.IO.Path]::GetDirectoryName($rel) -replace '\\', '/'
                [void]$files.Add("dir:$folder")
            }
        }
        foreach ($ref in $references) { $queue.Enqueue([System.IO.Path]::GetFullPath($ref.FullPath)) }
        foreach ($p in $paths) {
            $key = [System.IO.Path]::GetFullPath($p).ToUpperInvariant()
            if ($Index.ByAbsolute.ContainsKey($key)) { [void]$files.Add($Index.ByAbsolute[$key]) }
        }
    }
    return @($files)
}

# The JavaScript the differential's lint runs, followed through relative imports,
# and the JSON files it names that exist next to it or in a submodule's data/.
function Get-ScriptDependencies {
    param([Parameter(Mandatory)]$Index)
    $files = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $queue = [System.Collections.Generic.Queue[string]]::new()
    foreach ($t in $Index.Tracked) {
        if ($t -like 'tests/config_differential/*' -and $t -match '\.(mjs|js|cjs)$') { $queue.Enqueue($t) }
    }
    while ($queue.Count -gt 0) {
        $rel = $queue.Dequeue()
        if (-not $files.Add($rel)) { continue }
        $abs = Join-Path $Index.Root $rel
        if (-not (Test-Path -LiteralPath $abs -PathType Leaf)) { continue }
        $text = [System.IO.File]::ReadAllText($abs)
        $dir = [System.IO.Path]::GetDirectoryName($abs)
        foreach ($m in [regex]::Matches($text, '(?:\bfrom\s*|\bimport\s*\(\s*|\bimport\s+)["''](\.{1,2}/[^"'']+)["'']')) {
            $key = [System.IO.Path]::GetFullPath((Join-Path $dir $m.Groups[1].Value)).ToUpperInvariant()
            if ($Index.ByAbsolute.ContainsKey($key)) { $queue.Enqueue($Index.ByAbsolute[$key]) }
        }
        foreach ($m in [regex]::Matches($text, '["'']([^"''\s]+\.json)["'']')) {
            $name = $m.Groups[1].Value
            $candidates = @([System.IO.Path]::GetFullPath((Join-Path $dir $name)))
            foreach ($sub in $Index.Submodules) {
                $candidates += [System.IO.Path]::GetFullPath((Join-Path (Join-Path $Index.Root $sub) (Join-Path 'data' ([System.IO.Path]::GetFileName($name)))))
            }
            foreach ($c in $candidates) {
                $key = $c.ToUpperInvariant()
                if ($Index.ByAbsolute.ContainsKey($key)) { [void]$files.Add($Index.ByAbsolute[$key]) }
            }
        }
    }
    return @($files)
}

function Get-TestDifferentialCommand {
    param([Parameter(Mandatory)][string]$Root)
    $pixi = Join-Path $Root 'pixi.toml'
    if (-not (Test-Path -LiteralPath $pixi)) { return '' }
    foreach ($line in [System.IO.File]::ReadAllLines($pixi)) {
        if ($line -match '^\s*test-differential\s*=') { return $line }
    }
    return ''
}

# What the differential depends on, from the build that just ran it.
function Get-DifferentialInputs {
    param([Parameter(Mandatory)][string]$Root)
    $index = Get-TrackedIndex -Root $Root
    $entries = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $runner = $null

    $task = Get-TestDifferentialCommand -Root $index.Root
    if ($task -match 'dotnet\s+test\s+"?([^"\s]+\.csproj)') {
        $runner = 'dotnet'
        foreach ($f in (Get-DotnetDependencies -Index $index -Project $Matches[1])) { [void]$entries.Add($f) }
        foreach ($t in $index.Tracked) {
            if ($t -match '(^|/)Directory\.Build\.(props|targets)$') { [void]$entries.Add($t) }
        }
    } else {
        $msbuild = Get-MsbuildDependencies -Index $index
        if ($null -ne $msbuild) {
            $runner = 'msbuild'
            foreach ($f in $msbuild) { [void]$entries.Add($f) }
        }
    }
    if (-not $runner) {
        $runner = 'full'
        foreach ($t in $index.Tracked) {
            if ($index.Owners | Where-Object { $_.Prefix -ne '' -and $t.StartsWith($_.Prefix) }) { continue }
            [void]$entries.Add($t)
        }
        foreach ($sub in $index.Submodules) { [void]$entries.Add("submodule:$($sub -replace '\\', '/')") }
        # A file added anywhere counts too.
        [void]$entries.Add('dir:.')
    }

    foreach ($t in $index.Tracked) {
        if ($t -like 'tests/config_differential/*') { [void]$entries.Add($t) }
    }
    [void]$entries.Add('dir:tests/config_differential')
    foreach ($f in (Get-ScriptDependencies -Index $index)) { [void]$entries.Add($f) }
    foreach ($f in @('pixi.toml', 'scripts/run-tests.ps1', 'scripts/check-differential-provenance.ps1')) {
        if ($index.Tracked.Contains($f)) { [void]$entries.Add($f) }
    }
    $repoName = Split-Path -Leaf $index.Root
    foreach ($sub in $index.Submodules) {
        $format = Join-Path (Join-Path $index.Root $sub) 'data/config-format.json'
        if (-not (Test-Path -LiteralPath $format)) { continue }
        $configs = (Get-Content -LiteralPath $format -Raw | ConvertFrom-Json).configs
        $mine = $configs.PSObject.Properties | Where-Object { $_.Name -eq $repoName } | Select-Object -First 1
        if ($mine) {
            foreach ($c in @($mine.Value)) { if ($index.Tracked.Contains($c.committed)) { [void]$entries.Add($c.committed) } }
        }
    }
    foreach ($sub in $index.Submodules) {
        $format = ($sub -replace '\\', '/') + '/data/config-format.json'
        if ($entries.Remove($format)) { [void]$entries.Add("slice:$format#$repoName") }
    }
    [void]$entries.Remove($script:StampRelative)
    return [pscustomobject]@{ Runner = $runner; Index = $index; Entries = @($entries | Sort-Object) }
}

function Read-DifferentialStamp {
    param([Parameter(Mandatory)][string]$Root)
    $path = Get-DifferentialStampPath -Root $Root
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    $stamp = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    if ($stamp.format -ne $script:StampFormat) { return $null }
    return $stamp
}

# Which recorded inputs differ now: an empty list means the recorded pass still
# holds for this tree.
function Get-DifferentialChanges {
    param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)]$Stamp)
    $recorded = [ordered]@{}
    foreach ($p in $Stamp.files.PSObject.Properties) { $recorded[$p.Name] = $p.Value }
    if ($recorded.Count -eq 0) { return @('(the record lists no files)') }
    $index = Get-TrackedIndex -Root $Root
    $now = Get-DifferentialHashes -Index $index -Entries @($recorded.Keys)
    return @($recorded.Keys | Where-Object { $now[$_] -ne $recorded[$_] })
}

function Write-DifferentialStamp {
    param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)]$Inputs)
    $hashes = Get-DifferentialHashes -Index $Inputs.Index -Entries $Inputs.Entries
    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add('{')
    $lines.Add("  ""format"": $script:StampFormat,")
    $lines.Add("  ""runner"": ""$($Inputs.Runner)"",")
    $lines.Add("  ""passed"": ""$((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))"",")
    $lines.Add('  "files": {')
    $keys = @($hashes.Keys)
    for ($i = 0; $i -lt $keys.Count; $i++) {
        $comma = if ($i -lt $keys.Count - 1) { ',' } else { '' }
        $lines.Add("    ""$($keys[$i] -replace '\\', '\\' -replace '"', '\"')"": ""$($hashes[$keys[$i]])""$comma")
    }
    $lines.Add('  }')
    $lines.Add('}')
    [System.IO.File]::WriteAllText((Get-DifferentialStampPath -Root $Root), (($lines -join "`n") + "`n"), [System.Text.UTF8Encoding]::new($false))
    return $keys.Count
}

<#
.SYNOPSIS
    Runs the legacy config differential unless its recorded pass still holds.
.PARAMETER Root
    The mod repo.
.PARAMETER Environment
    The pixi environment to run test-differential in, when not the default.
.PARAMETER NoRecord
    Check the record and run the test when it no longer holds, but never write
    the record. For a tag build, which cannot commit what it records.
.PARAMETER Force
    Run the test whatever the record says.
.OUTPUTS
    Nothing. Throws when the test fails.
#>
function Invoke-ConfigDifferential {
    param(
        [Parameter(Mandatory)][string]$Root,
        [string]$Environment,
        [switch]$NoRecord,
        [switch]$Force
    )
    $Root = (Resolve-Path -LiteralPath $Root).Path
    if (-not (Test-Path -LiteralPath (Join-Path $Root 'tests/config_differential') -PathType Container)) {
        Write-Host 'No tests/config_differential, so there is no config differential to run.'
        return
    }

    $stamp = Read-DifferentialStamp -Root $Root
    if ($Force) {
        Write-Host 'Config differential: -Force, running it whatever the record says.' -ForegroundColor Cyan
    } elseif ($null -eq $stamp) {
        Write-Host "Config differential: no recorded pass in $script:StampRelative, running it." -ForegroundColor Cyan
    } else {
        $changed = @(Get-DifferentialChanges -Root $Root -Stamp $stamp)
        $count = @($stamp.files.PSObject.Properties).Count
        if ($changed.Count -eq 0) {
            Write-Host "Config differential: skipped. It passed at $($stamp.passed), and none of the $count files it depends on ($($stamp.runner)) has changed since." -ForegroundColor Green
            return
        }
        $shown = @($changed | Select-Object -First 10)
        $more = if ($changed.Count -gt $shown.Count) { " and $($changed.Count - $shown.Count) more" } else { '' }
        Write-Host "Config differential: running it, $($changed.Count) of the $count files it depends on changed since it passed: $($shown -join ', ')$more" -ForegroundColor Cyan
    }

    Push-Location $Root
    try {
        $global:LASTEXITCODE = 0
        if ($Environment) { & pixi run -e $Environment test-differential } else { & pixi run test-differential }
        if ($LASTEXITCODE -ne 0) { throw "pixi run test-differential failed ($LASTEXITCODE)" }
    } finally { Pop-Location }

    if ($NoRecord) { return }
    $inputs = Get-DifferentialInputs -Root $Root
    $n = Write-DifferentialStamp -Root $Root -Inputs $inputs
    Write-Host "Config differential: passed. Recorded $n files it depends on ($($inputs.Runner)) in $script:StampRelative; commit it with the release." -ForegroundColor Green
}

Export-ModuleMember -Function @(
    'Invoke-ConfigDifferential',
    'Get-DifferentialInputs',
    'Get-DifferentialChanges',
    'Get-DifferentialHashes',
    'Get-DifferentialStampPath',
    'Get-DifferentialStampRelativePath',
    'Get-MaskedText',
    'Get-TrackedIndex',
    'Read-DifferentialStamp',
    'Write-DifferentialStamp'
)
