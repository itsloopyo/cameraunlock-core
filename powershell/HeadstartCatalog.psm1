# HeadstartCatalog: what a mod that ships a Headstart package asks of the package catalog
# (itsloopyo/cameraunlock-catalog) when it publishes a tagged release or a dev build.
#
# A package's manifest is cameraunlock-package.toml, a release asset. The catalog's Pin workflow
# reads it from the repo's latest release (the stable channel) and from the release tagged `dev`
# (the dev channel), holds it to the catalog's rules and lists it. Pin runs after the release is
# out, so a manifest it refuses, or a token that cannot start it, is found once the release is
# already public. Test-HeadstartPackage asks the same questions before anything is published;
# Sync-HeadstartCatalogPin starts Pin after it and waits for the catalog to list the version.
#
# The catalog also names Headstart's own newest release, which installed copies update to.
# Headstart's release workflow calls Sync-HeadstartAppPin once its release is out.
#
# Both take a token for the catalog repo. In release-mod.yml that is CAMERAUNLOCK_CATALOG_TOKEN,
# a fine-grained token with Actions: write and Contents: read on the catalog repo only. A dev
# build published from a developer's machine passes `gh auth token`.

$script:CatalogRepo = 'itsloopyo/cameraunlock-catalog'
$script:CatalogUrl = 'https://catalog.cameraunlock.com/catalog.signed'

function Get-CatalogHeaders {
    param([Parameter(Mandatory)][string]$Token, [string]$Accept = 'application/vnd.github+json')
    return @{ Authorization = "Bearer $Token"; Accept = $Accept }
}

# The stable or dev manifest of a Headstart package differs only in its version and in which
# release its files are downloaded from. A dev build rewrites the manifest the package task wrote
# for the tagged release: its own version, since the catalog lists a version as one build, and
# every file under the dev release, which is where the dev build uploads them.
function New-HeadstartDevManifest {
    [CmdletBinding()]
    param(
        # The manifest the package task wrote, naming the v<Version> release's files.
        [Parameter(Mandatory)][string]$ManifestPath,
        [Parameter(Mandatory)][string]$Repo,
        [Parameter(Mandatory)][string]$Version,
        [Parameter(Mandatory)][string]$DevVersion,
        [Parameter(Mandatory)][string]$DevTag,
        # The names of the assets the dev release will have.
        [Parameter(Mandatory)][string[]]$AssetNames,
        [Parameter(Mandatory)][string]$OutPath
    )

    $text = [System.IO.File]::ReadAllText($ManifestPath)
    $versionLine = '(?m)^version = "' + [regex]::Escape($Version) + '"(?=\r?$)'
    $found = [regex]::Matches($text, $versionLine).Count
    if ($found -ne 1) {
        throw "$ManifestPath has $found lines reading version = `"$Version`"; a dev manifest is made by rewriting exactly one."
    }
    $text = [regex]::Replace($text, $versionLine, ('version = "' + $DevVersion + '"').Replace('$', '$$'))

    $releases = "https://github.com/$Repo/releases/download/"
    $moved = 0
    foreach ($url in [regex]::Matches($text, '(?m)^url = "([^"]*)"\r?$')) {
        $value = $url.Groups[1].Value
        if (-not $value.StartsWith("${releases}v$Version/")) { continue }
        $name = $value.Substring("${releases}v$Version/".Length)
        if ($AssetNames -notcontains $name) {
            throw "$ManifestPath lists $name as a file of this build, and the dev release would not have it. It uploads: $($AssetNames -join ', ')."
        }
        $moved++
    }
    $text = $text.Replace("${releases}v$Version/", "$releases$DevTag/")
    # A file this build made that is still named on the v<Version> release, which a dev build does
    # not make: its url is spelt some other way than the lines above read.
    if ($moved -eq 0 -or $text -match ('(?i)/releases/download/v' + [regex]::Escape($Version) + '/')) {
        throw "$ManifestPath names $moved file(s) under ${releases}v$Version/, as lines reading url = `"...`", and still names release v$Version after they were moved to $DevTag. A dev manifest needs every file of this build spelt that way."
    }

    New-Item -ItemType Directory -Path (Split-Path -Parent $OutPath) -Force | Out-Null
    [System.IO.File]::WriteAllText($OutPath, $text, (New-Object System.Text.UTF8Encoding $false))
}

# Throws unless the catalog would take the manifest as release <Tag> of <Repo>, and the token can
# start the catalog's Pin workflow. It runs the catalog's own pin.py, as its main branch has it
# now, in its --check mode, which reads nothing from the network and writes nothing. That needs a
# python (3.11 or later) in the mod's pixi environment.
#
# Only starting Pin proves the token can start it, so the check ends by starting it. Nothing is
# published yet, so that run finds the catalog unchanged.
#
# pin.py cannot see the files of a release that does not exist yet, so their hashes and sizes are
# still checked only by Pin itself.
function Test-HeadstartPackage {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$ManifestPath,
        [Parameter(Mandatory)][string]$Repo,
        # The release the manifest will be an asset of: v<version>, or dev.
        [Parameter(Mandatory)][string]$Tag,
        [Parameter(Mandatory)][string]$Token,
        # The mod's repo root, where `pixi run python` finds the mod's environment.
        [Parameter(Mandatory)][string]$ProjectRoot,
        # The pixi environment the mod packages in, when it is not the default one.
        [string]$PixiEnvironment
    )

    $needs = "The token needs Actions: write and Contents: read on $script:CatalogRepo."
    $api = "https://api.github.com/repos/$script:CatalogRepo"
    $work = Join-Path ([System.IO.Path]::GetTempPath()) "headstart-catalog-$([guid]::NewGuid().ToString('N'))"
    New-Item -ItemType Directory -Path (Join-Path $work 'scripts') -Force | Out-Null
    try {
        try {
            $pin = Invoke-RestMethod -Uri "$api/actions/workflows/pin.yml" -Headers (Get-CatalogHeaders $Token)
        } catch {
            throw "The catalog token cannot see the Pin workflow of $script:CatalogRepo, so it could not start it after the release: $($_.Exception.Message) $needs"
        }
        if ($pin.state -ne 'active') {
            throw "The Pin workflow of $script:CatalogRepo is $($pin.state), so it would not pin the release. Turn it on at https://github.com/$script:CatalogRepo/actions/workflows/pin.yml."
        }
        foreach ($file in 'scripts/pin.py', 'packages.toml', 'catalog.signed') {
            try {
                Invoke-WebRequest -UseBasicParsing -Uri "$api/contents/$($file)?ref=main" `
                    -Headers (Get-CatalogHeaders $Token 'application/vnd.github.raw') -OutFile (Join-Path $work $file)
            } catch {
                throw "The catalog token cannot read $file from $script:CatalogRepo, which the check before a release runs: $($_.Exception.Message) $needs"
            }
        }

        $manifest = (Resolve-Path -LiteralPath $ManifestPath).Path
        Push-Location $ProjectRoot
        # A native command's stderr line is a terminating error under Windows PowerShell 5.1's
        # 'Stop', and pin.py writes what it refuses to stderr.
        $prevEAP = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $pixi = @('run')
        if ($PixiEnvironment) { $pixi += @('-e', $PixiEnvironment) }
        try {
            $output = (& pixi @pixi python -I (Join-Path $work 'scripts/pin.py') --check $manifest $Repo $Tag 2>&1 | ForEach-Object { "$_" }) -join "`n"
            $exit = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $prevEAP
            Pop-Location
        }
        if ($exit -ne 0) {
            # pin.py ends a refusal with one line, `error: <why>`, and exit code 1. Anything else
            # is the check not running: no python 3.11 in the pixi environment, or pin.py failing.
            $last = @($output -split "`n")[-1]
            if ($exit -eq 1 -and $last.StartsWith('error: ')) {
                throw "The Headstart catalog would not take $manifest as release $Tag of ${Repo}: $($last.Substring(7))"
            }
            throw "The Headstart catalog's check of $manifest did not run ('pixi run python' ended $exit). It needs python 3.11 or later in the pixi environment:`n$output"
        }
        Write-Host $output

        try {
            Invoke-RestMethod -Method Post -Uri "$api/actions/workflows/pin.yml/dispatches" -Headers (Get-CatalogHeaders $Token) `
                -ContentType 'application/json' -Body '{"ref":"main"}' | Out-Null
        } catch {
            throw "The catalog token cannot start the Pin workflow of $script:CatalogRepo, so it could not start it after the release: $($_.Exception.Message) $needs"
        }
        Write-Host "The token started the catalog's Pin workflow."
    } finally {
        # Continue: a folder that cannot be removed must not take the place of the error above.
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction Continue
    }
}

# The catalog as it is served now.
function Get-ServedCatalog {
    $bust = "cb$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())"
    # Read as bytes: Invoke-RestMethod's result depends on the content type Pages gives .signed.
    $response = Invoke-WebRequest -UseBasicParsing "${script:CatalogUrl}?$bust"
    $signed = [System.Text.Encoding]::UTF8.GetString($response.RawContentStream.ToArray())
    # The first line is the signature; the rest is the catalog's JSON.
    return ($signed -split "`n", 2)[1] | ConvertFrom-Json
}

# The version the served catalog lists for <Repo> on <Channel>, or $null.
function Get-HeadstartPinnedVersion {
    param([Parameter(Mandatory)][string]$Repo, [Parameter(Mandatory)][string]$Channel)

    $catalog = Get-ServedCatalog
    $entry = @($catalog.packages) | Where-Object { $_.repo -eq $Repo }
    if (-not $entry) { return $null }
    $listed = @($entry)[0].channels.PSObject.Properties[$Channel]
    if (-not $listed) { return $null }
    return $listed.Value.version
}

# The version of Headstart's own release the served catalog names, or $null.
function Get-HeadstartPinnedApp {
    $app = (Get-ServedCatalog).PSObject.Properties['app']
    if (-not $app -or -not $app.Value) { return $null }
    return $app.Value.version
}

# Starts the catalog's Pin workflow, follows that run and waits for the served catalog to list
# <Version> on <Channel>. Throws when Pin fails or the catalog does not come to list it. The
# release is already out by then, so every failure says how to pin it again by hand.
function Sync-HeadstartCatalogPin {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Repo,
        [Parameter(Mandatory)][string]$Version,
        [Parameter(Mandatory)][ValidateSet('stable', 'dev')][string]$Channel,
        [Parameter(Mandatory)][string]$Token
    )

    Sync-CatalogPin -Version $Version -Token $Token -Repo $Repo -Channel $Channel
}

# The same for Headstart's own release: waits for the served catalog to name <Version> as its
# app entry. The catalog names an app release only once its packages.toml has an [app] table.
function Sync-HeadstartAppPin {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Version,
        [Parameter(Mandatory)][string]$Token
    )

    Sync-CatalogPin -Version $Version -Token $Token
}

# With <Repo> and <Channel> the version is that package's; without them, Headstart's own.
function Sync-CatalogPin {
    param(
        [Parameter(Mandatory)][string]$Version,
        [Parameter(Mandatory)][string]$Token,
        [string]$Repo,
        [string]$Channel
    )

    $Where = if ($Repo) { "on its $Channel channel for $Repo" } else { "as Headstart's own release" }

    $api = "https://api.github.com/repos/$script:CatalogRepo/actions"
    $actions = "https://github.com/$script:CatalogRepo/actions"
    $headers = Get-CatalogHeaders $Token
    # Pin reads every package's releases afresh, so starting it again is the whole recovery.
    # Running the release again is not: it would stop at creating the release that already exists.
    $recover = "Once that is fixed, run 'gh workflow run pin.yml --repo $script:CatalogRepo --ref main'; the release is out, only the catalog is behind."

    # Runner and API clocks can differ by seconds. A Pin run made a little before this ask still
    # started after this release was created, so it pins it as well.
    $asked = [DateTime]::UtcNow.AddSeconds(-30)
    Invoke-RestMethod -Method Post -Uri "$api/workflows/pin.yml/dispatches" -Headers $headers `
        -ContentType 'application/json' -Body '{"ref":"main"}' | Out-Null
    Write-Host "Asked the Headstart catalog to pin $Version; waiting for the Pin run..."
    $deadline = [DateTime]::UtcNow.AddMinutes(5)
    $run = $null
    while ([DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Seconds 15
        $runs = (Invoke-RestMethod -Uri "$api/workflows/pin.yml/runs?event=workflow_dispatch&per_page=10" -Headers $headers).workflow_runs
        # Newest first. Pin's concurrency group cancels a waiting run that a newer one replaces,
        # and the newer one pins this release too, so the newest is the one to follow.
        $run = @($runs | Where-Object { ([DateTimeOffset]$_.created_at).UtcDateTime -ge $asked }) | Select-Object -First 1
        if (-not $run -or $run.status -ne 'completed') {
            Write-Host "  Pin run $(if ($run) { "$($run.html_url) is $($run.status)" } else { 'not started yet' })..."
            continue
        }
        if ($run.conclusion -eq 'success') { break }
        if ($run.conclusion -eq 'cancelled') { continue }
        throw "The Pin run $($run.html_url) ended $($run.conclusion), so the Headstart catalog does not list $Version. Its log names what it refused. $recover"
    }
    if (-not $run -or $run.conclusion -ne 'success') {
        throw "No Pin run asked for at $($asked.ToString('u')) finished within 5 minutes$(if ($run) { "; the last was $($run.html_url), $($run.status) $($run.conclusion)" }). Check $actions. $recover"
    }

    Write-Host "Pin run $($run.html_url) succeeded; waiting for $script:CatalogUrl to list $Version..."
    $pinned = $null
    $failure = $null
    $deadline = [DateTime]::UtcNow.AddMinutes(5)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $pinned = if ($Repo) { Get-HeadstartPinnedVersion -Repo $Repo -Channel $Channel } else { Get-HeadstartPinnedApp }
            $failure = $null
        } catch {
            # Pages deploys the Pin run's push a little after it; a fetch that keeps failing runs into the deadline.
            $failure = $_.Exception.Message
        }
        if ($pinned -eq $Version) {
            Write-Host "The Headstart catalog lists $Version $Where."
            return
        }
        Write-Host "  catalog lists $(if ($failure) { "nothing: $failure" } elseif ($pinned) { "$pinned $Where" } else { "no version $Where" })..."
        Start-Sleep -Seconds 15
    }
    $last = if ($failure) { "the last fetch failed: $failure" } else { "it still lists $(if ($pinned) { $pinned } else { 'no version' })" }
    throw "Pin succeeded but $script:CatalogUrl did not list $Version $Where within 5 minutes ($last). Check the catalog repo's Pages deployment at $actions. $recover"
}

Export-ModuleMember -Function New-HeadstartDevManifest, Test-HeadstartPackage, Sync-HeadstartCatalogPin, Sync-HeadstartAppPin
