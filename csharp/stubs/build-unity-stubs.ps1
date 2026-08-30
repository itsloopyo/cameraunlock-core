#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
    Compiles the shared Unity reference stubs into a mod's libs directory.

.DESCRIPTION
    One implementation of the stub build that fifteen mod repos each carried a copy of.
    A repo's scripts/setup-libs.ps1 now provisions its loader DLLs and calls this.

    cameraunlock-core's own build does not use this script - it compiles the same two
    sources through csharp/stubs/UnityEngine and csharp/stubs/UnityEngine.UI so that
    `dotnet build csharp` needs nothing but the SDK. This script exists for the mods,
    whose libs directory also has to hold their loader and game assemblies.

    The assembly a type is compiled into is baked into every reference the compiler emits,
    and nothing catches a mismatch until the code runs in the game. So the split here is not
    cosmetic:

      UnityEngine.dll     every UnityEngine type from UnityStubs.cs. Modern Unity ships
                          UnityEngine.dll as a facade that type-forwards to the split
                          modules, so a reference resolved here still binds at runtime.
      UnityEngine.UI.dll  Graphic, Image, RawImage, Text. These live in UnityEngine.UI in
                          every shipped Unity, and compiling them into UnityEngine.dll
                          instead is what left CrosshairUtility unable to see them.
      the modules         empty. A type declared in both UnityEngine.dll and a module
                          assembly is CS0433 at every use site.

.PARAMETER OutputPath
    Directory the stub assemblies are written to - the mod's libs/ or lib/ folder.

.PARAMETER TargetFramework
    net35 for pre-2017.3 Unity (Gone Home, Obra Dinn, Painscreek), net472 or net48 for
    modern Unity. Must match the mod's own TargetFramework.

.PARAMETER ExtraAssembly
    Additional "AssemblyName=path\to\Source.cs" pairs, for the game's own types. Valheim
    compiles libs/ValheimStubs.cs into assembly_valheim.dll this way. Repeatable. Each
    source is compiled against the UnityEngine and UnityEngine.UI stubs.

.PARAMETER EmptyModule
    Module assemblies produced with no types. Defaults to the set core's own projects and
    the fleet's csprojs reference, minus UnityEngine.InputLegacyModule on net35 - that
    module does not exist before Unity 2017.3, and producing it makes the mod emit
    [UnityEngine.InputLegacyModule]Input against a game that cannot resolve it.

    Invoke this script with `powershell -Command "& path	ouild-unity-stubs.ps1 ..."`,
    NOT with `powershell -File`. Under -File every remaining argument is a literal string,
    so `-EmptyModule A,B,C` binds as ONE element and produces a single assembly named
    "A,B,C". The check below turns that into an error rather than a wrongly named DLL.

.PARAMETER SkipUI
    Do not build UnityEngine.UI.dll. For net35 mods on a Unity that predates uGUI.

.EXAMPLE
    ./cameraunlock-core/csharp/stubs/build-unity-stubs.ps1 -OutputPath src/MyMod/libs -TargetFramework net472

.EXAMPLE
    ./cameraunlock-core/csharp/stubs/build-unity-stubs.ps1 -OutputPath src/ValheimHeadTracking/libs `
        -TargetFramework net48 -ExtraAssembly "assembly_valheim=src/ValheimHeadTracking/libs/ValheimStubs.cs"
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [ValidateSet('net35', 'net472', 'net48')]
    [string]$TargetFramework = 'net472',

    [string[]]$ExtraAssembly = @(),

    [string[]]$EmptyModule,

    [switch]$SkipUI
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $PSBoundParameters.ContainsKey('EmptyModule')) {
    $EmptyModule = @(
        'UnityEngine.CoreModule',
        'UnityEngine.IMGUIModule',
        'UnityEngine.UIModule',
        'UnityEngine.TextRenderingModule',
        'UnityEngine.AnimationModule',
        'UnityEngine.PhysicsModule'
    )
    # UnityEngine.InputLegacyModule exists only from Unity 2017.3. Before that, Input lives
    # in UnityEngine.dll. Producing the module here would satisfy the Exists() condition on
    # CameraUnlock.Core.Unity's reference to it, and the mod would then emit
    # [UnityEngine.InputLegacyModule]Input against a game that has no such assembly.
    if ($TargetFramework -ne 'net35') {
        $EmptyModule += 'UnityEngine.InputLegacyModule'
    }
}

foreach ($moduleName in $EmptyModule) {
    if ($moduleName -like '*,*') {
        throw ("EmptyModule element '$moduleName' contains a comma, so the list arrived as " +
               "one string. Launching with 'powershell -File' passes every argument as a " +
               "literal and would produce a single assembly with that name. Launch with " +
               "'powershell -Command' and the call operator instead.")
    }
}

$stubsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$unityStubs = Join-Path $stubsDir 'UnityStubs.cs'
$uiStubs = Join-Path $stubsDir 'UnityUIStubs.cs'

if (-not (Test-Path $unityStubs)) {
    throw "Shared stub source not found at $unityStubs"
}

# .NET's notion of the current directory is not PowerShell's, so a relative path has to be
# resolved against Get-Location before GetFullPath sees it.
function Resolve-FullPath([string]$path) {
    if (-not [System.IO.Path]::IsPathRooted($path)) {
        $path = Join-Path (Get-Location).Path $path
    }
    return [System.IO.Path]::GetFullPath($path)
}

$OutputPath = Resolve-FullPath $OutputPath
if (-not (Test-Path $OutputPath)) {
    New-Item -ItemType Directory -Path $OutputPath -Force | Out-Null
}

# net35 has no C# 9. The shared sources are written to C# 7.3 so one file serves every
# target; pinning the version here means a net472 build cannot quietly accept syntax that
# then fails on the net35 mods.
$langVersion = '7.3'

$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("cameraunlock-stubs-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch -Force | Out-Null

function Build-Stub {
    param(
        [string]$AssemblyName,
        [string[]]$Sources,
        [string[]]$References = @()
    )

    $compileItems = ($Sources | ForEach-Object {
        '    <Compile Include="' + (Resolve-FullPath $_) + '" />'
    }) -join "`n"

    $referenceItems = ($References | ForEach-Object {
        '    <Reference Include="' + $_ + '"><HintPath>' + (Join-Path $OutputPath ($_ + '.dll')) + '</HintPath></Reference>'
    }) -join "`n"

    $proj = @"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>$TargetFramework</TargetFramework>
    <LangVersion>$langVersion</LangVersion>
    <AssemblyName>$AssemblyName</AssemblyName>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <GenerateAssemblyInfo>false</GenerateAssemblyInfo>
    <AssemblyVersion>0.0.0.0</AssemblyVersion>
    <DebugType>none</DebugType>
    <!-- Shipped Unity and game assemblies are AssemblyVersion 0.0.0.0 (verified against
         UnityEngine.PhysicsModule.dll and a game's Assembly-CSharp.dll). Mono's binder
         rejects a plugin whose reference names a version the loaded assembly does not
         have, so a stub built at 1.0.0.0 produces a mod that will not load. -->
    <NoWarn>CS0169;CS0649;CS0067;CS0660;CS0661;CS0108;CS0114</NoWarn>
  </PropertyGroup>
  <ItemGroup>
$compileItems
$referenceItems
  </ItemGroup>
</Project>
"@

    $projPath = Join-Path $scratch ("Stub_" + $AssemblyName + ".csproj")
    $proj | Out-File -FilePath $projPath -Encoding utf8

    $output = & dotnet build $projPath -c Release -o $OutputPath --nologo -v q
    if ($LASTEXITCODE -ne 0) {
        $output | Write-Host
        throw "Failed to build $AssemblyName stub"
    }
    Write-Host "  $AssemblyName.dll" -ForegroundColor Green
}

try {
    Write-Host "Building Unity stub assemblies ($TargetFramework) into $OutputPath" -ForegroundColor Cyan

    Build-Stub -AssemblyName 'UnityEngine' -Sources @($unityStubs)

    $uiBuilt = $false
    if (-not $SkipUI) {
        if (-not (Test-Path $uiStubs)) { throw "Shared UI stub source not found at $uiStubs" }
        Build-Stub -AssemblyName 'UnityEngine.UI' -Sources @($uiStubs) -References @('UnityEngine')
        $uiBuilt = $true
    }

    # An empty module must not be built before UnityEngine.UI: -o writes into the same
    # directory, and a module named in $EmptyModule that also carries types would silently
    # overwrite the real one.
    $emptySource = Join-Path $scratch 'EmptyStub.cs'
    '// intentionally empty' | Out-File -FilePath $emptySource -Encoding utf8
    foreach ($moduleName in $EmptyModule) {
        if ($moduleName -eq 'UnityEngine' -or ($uiBuilt -and $moduleName -eq 'UnityEngine.UI')) {
            throw "EmptyModule may not name $moduleName - it carries types"
        }
        Build-Stub -AssemblyName $moduleName -Sources @($emptySource)
    }

    foreach ($pair in $ExtraAssembly) {
        $split = $pair.IndexOf('=')
        if ($split -lt 1) {
            throw "ExtraAssembly entries look like 'AssemblyName=path\to\Source.cs', got '$pair'"
        }
        $name = $pair.Substring(0, $split)
        $source = $pair.Substring($split + 1)
        if (-not (Test-Path $source)) { throw "ExtraAssembly source not found: $source" }

        $refs = @('UnityEngine')
        if ($uiBuilt) { $refs += 'UnityEngine.UI' }
        Build-Stub -AssemblyName $name -Sources @($source) -References $refs
    }

    Get-ChildItem -Path $OutputPath -Filter '*.deps.json' -ErrorAction SilentlyContinue | Remove-Item -Force
    Get-ChildItem -Path $OutputPath -Filter '*.pdb' -ErrorAction SilentlyContinue | Remove-Item -Force

    Write-Host "Stub assemblies ready" -ForegroundColor Green
}
finally {
    Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue
}
