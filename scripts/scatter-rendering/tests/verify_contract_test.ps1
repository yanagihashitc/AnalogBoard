$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$VerificationRoot = Split-Path -Parent $ScriptDir
$RepositoryRoot = (Resolve-Path -LiteralPath (Join-Path $VerificationRoot '..\..')).Path
$ModulePath = Join-Path $VerificationRoot 'scatter_verification_core.psm1'
$WrapperPath = Join-Path $VerificationRoot 'verify.ps1'

if (-not (Test-Path -LiteralPath $ModulePath -PathType Leaf)) {
    throw "RED: scatter verification core is not implemented: $ModulePath"
}
if (-not (Test-Path -LiteralPath $WrapperPath -PathType Leaf)) {
    throw "RED: scatter verification wrapper is not implemented: $WrapperPath"
}

Import-Module $ModulePath -Force

function Assert-Equal {
    param(
        [Parameter(Mandatory = $true)]$Actual,
        [Parameter(Mandatory = $true)]$Expected,
        [Parameter(Mandatory = $true)][string]$Message
    )

    if ($Actual -ne $Expected) {
        throw "$Message. Expected '$Expected', got '$Actual'."
    }
}

function Assert-Contains {
    param(
        [Parameter(Mandatory = $true)][string[]]$Actual,
        [Parameter(Mandatory = $true)][string]$Expected,
        [Parameter(Mandatory = $true)][string]$Message
    )

    if ($Actual -notcontains $Expected) {
        throw "$Message. Missing '$Expected' in '$($Actual -join ' ')'."
    }
}

function Assert-ThrowsExact {
    param(
        [Parameter(Mandatory = $true)][scriptblock]$Action,
        [Parameter(Mandatory = $true)][string]$ExpectedType,
        [Parameter(Mandatory = $true)][string]$ExpectedMessage
    )

    try {
        & $Action
    }
    catch {
        Assert-Equal -Actual $_.Exception.GetType().FullName -Expected $ExpectedType -Message 'Exception type'
        Assert-Equal -Actual $_.Exception.Message -Expected $ExpectedMessage -Message 'Exception message'
        return
    }

    throw "Expected $ExpectedType with exact message '$ExpectedMessage'."
}

function Assert-ThrowsDynamicOutput {
    param(
        [Parameter(Mandatory = $true)][scriptblock]$Action,
        [Parameter(Mandatory = $true)][string]$ExpectedType,
        [Parameter(Mandatory = $true)][string]$ExpectedMessagePattern
    )

    try {
        & $Action
    }
    catch {
        Assert-Equal -Actual $_.Exception.GetType().FullName -Expected $ExpectedType -Message 'Exception type'
        if ($_.Exception.Message -notlike $ExpectedMessagePattern) {
            throw "Unexpected dynamic error. Expected '$ExpectedMessagePattern', got '$($_.Exception.Message)'."
        }
        return
    }

    throw "Expected $ExpectedType matching dynamic output '$ExpectedMessagePattern'."
}

function Set-FixtureSolution {
    param(
        [Parameter(Mandatory = $true)][string]$PrototypeRoot,
        [string[]]$Projects = @('Core', 'Wpf', 'Tests'),
        [switch]$IncludeDebug,
        [switch]$IncludeUnexpected
    )

    $catalog = @{
        Core = @{
            Name = 'AnalogBoard.ScatterRendering.Core'
            Path = 'src\AnalogBoard.ScatterRendering.Core\AnalogBoard.ScatterRendering.Core.csproj'
            Guid = '{5BD7B476-EF87-4F58-8CC4-F59D1110DD00}'
        }
        Wpf = @{
            Name = 'AnalogBoard.ScatterRendering.Wpf'
            Path = 'src\AnalogBoard.ScatterRendering.Wpf\AnalogBoard.ScatterRendering.Wpf.csproj'
            Guid = '{F5A0A84B-2AFE-43D7-BC3C-D7597CFD7B7E}'
        }
        Tests = @{
            Name = 'AnalogBoard.ScatterRendering.Tests'
            Path = 'tests\AnalogBoard.ScatterRendering.Tests\AnalogBoard.ScatterRendering.Tests.csproj'
            Guid = '{83C17C7C-D260-48EA-9D55-66EF504D3BAF}'
        }
        Unexpected = @{
            Name = 'Unexpected.Project'
            Path = 'src\Unexpected\Unexpected.csproj'
            Guid = '{11111111-1111-1111-1111-111111111111}'
        }
    }
    $selected = @($Projects)
    if ($IncludeUnexpected) {
        $selected += 'Unexpected'
    }

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add('Microsoft Visual Studio Solution File, Format Version 12.00')
    $lines.Add('# Visual Studio Version 17')
    $lines.Add('VisualStudioVersion = 17.0.31903.59')
    $lines.Add('MinimumVisualStudioVersion = 10.0.40219.1')
    foreach ($key in $selected) {
        $project = $catalog[$key]
        $lines.Add("Project(`"{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}`") = `"$($project.Name)`", `"$($project.Path)`", `"$($project.Guid)`"")
        $lines.Add('EndProject')
    }
    $lines.Add('Global')
    $lines.Add("`tGlobalSection(SolutionConfigurationPlatforms) = preSolution")
    if ($IncludeDebug) {
        $lines.Add("`t`tDebug|x64 = Debug|x64")
    }
    $lines.Add("`t`tRelease|x64 = Release|x64")
    $lines.Add("`tEndGlobalSection")
    $lines.Add("`tGlobalSection(ProjectConfigurationPlatforms) = postSolution")
    foreach ($key in $selected) {
        $guid = $catalog[$key].Guid
        if ($IncludeDebug) {
            $lines.Add("`t`t$guid.Debug|x64.ActiveCfg = Debug|x64")
            $lines.Add("`t`t$guid.Debug|x64.Build.0 = Debug|x64")
        }
        $lines.Add("`t`t$guid.Release|x64.ActiveCfg = Release|x64")
        $lines.Add("`t`t$guid.Release|x64.Build.0 = Release|x64")
    }
    $lines.Add("`tEndGlobalSection")
    $lines.Add('EndGlobal')
    $lines | Set-Content -LiteralPath (Join-Path $PrototypeRoot 'AnalogBoard.ScatterRenderingPrototype.sln') -Encoding UTF8
}

function New-ContractFixture {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [ValidateSet('Empty', 'CoreOnly', 'Complete')]
        [string]$Shape = 'Complete',
        [switch]$IncludeDebug
    )

    $prototypeRoot = Join-Path $Root 'prototypes\scatter-rendering'
    New-Item -ItemType Directory -Force -Path $prototypeRoot | Out-Null
    @'
<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <packageSources><clear /></packageSources>
  <fallbackPackageFolders><clear /></fallbackPackageFolders>
  <auditSources><clear /></auditSources>
</configuration>
'@ | Set-Content -LiteralPath (Join-Path $prototypeRoot 'NuGet.Config') -Encoding UTF8

    if ($Shape -eq 'Empty') {
        return $prototypeRoot
    }

    $coreRoot = Join-Path $prototypeRoot 'src\AnalogBoard.ScatterRendering.Core'
    New-Item -ItemType Directory -Force -Path $coreRoot | Out-Null
    @'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup><TargetFramework>net10.0-windows</TargetFramework><PlatformTarget>x64</PlatformTarget></PropertyGroup>
</Project>
'@ | Set-Content -LiteralPath (Join-Path $coreRoot 'AnalogBoard.ScatterRendering.Core.csproj') -Encoding UTF8

    if ($Shape -eq 'CoreOnly') {
        Set-FixtureSolution -PrototypeRoot $prototypeRoot -Projects @('Core')
        return $prototypeRoot
    }

    $wpfRoot = Join-Path $prototypeRoot 'src\AnalogBoard.ScatterRendering.Wpf'
    $resourceRoot = Join-Path $wpfRoot 'Properties'
    $testsRoot = Join-Path $prototypeRoot 'tests\AnalogBoard.ScatterRendering.Tests'
    New-Item -ItemType Directory -Force -Path $resourceRoot, $testsRoot | Out-Null
    @'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Library</OutputType>
    <TargetFramework>net10.0-windows</TargetFramework>
    <PlatformTarget>x64</PlatformTarget>
    <UseWPF>true</UseWPF>
  </PropertyGroup>
  <ItemGroup>
    <ProjectReference Include="..\AnalogBoard.ScatterRendering.Core\AnalogBoard.ScatterRendering.Core.csproj" />
  </ItemGroup>
</Project>
'@ | Set-Content -LiteralPath (Join-Path $wpfRoot 'AnalogBoard.ScatterRendering.Wpf.csproj') -Encoding UTF8
    @'
<root>
  <data name="SurfaceBufferLengthMismatch"><value>buffer</value></data>
  <data name="SurfaceDisposed"><value>disposed</value></data>
  <data name="SurfaceGenerationNotIncreasing"><value>generation</value></data>
  <data name="SurfaceHeightOutOfRange"><value>height</value></data>
  <data name="SurfaceOwnerMustBeSta"><value>sta</value></data>
  <data name="SurfaceWidthOutOfRange"><value>width</value></data>
  <data name="SurfaceWrongThread"><value>thread</value></data>
</root>
'@ | Set-Content -LiteralPath (Join-Path $resourceRoot 'Resources.resx') -Encoding UTF8
    @'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0-windows</TargetFramework>
    <PlatformTarget>x64</PlatformTarget>
    <UseWPF>true</UseWPF>
  </PropertyGroup>
  <ItemGroup>
    <ProjectReference Include="..\..\src\AnalogBoard.ScatterRendering.Core\AnalogBoard.ScatterRendering.Core.csproj" />
    <ProjectReference Include="..\..\src\AnalogBoard.ScatterRendering.Wpf\AnalogBoard.ScatterRendering.Wpf.csproj" />
  </ItemGroup>
</Project>
'@ | Set-Content -LiteralPath (Join-Path $testsRoot 'AnalogBoard.ScatterRendering.Tests.csproj') -Encoding UTF8
    Set-FixtureSolution -PrototypeRoot $prototypeRoot -IncludeDebug:$IncludeDebug
    return $prototypeRoot
}

function Add-FixtureXml {
    param(
        [Parameter(Mandatory = $true)][string]$ProjectPath,
        [Parameter(Mandatory = $true)][string]$Fragment
    )

    $source = Get-Content -LiteralPath $ProjectPath -Raw -Encoding UTF8
    $source.Replace('</Project>', "$Fragment`r`n</Project>") |
        Set-Content -LiteralPath $ProjectPath -Encoding UTF8
}

$null = Assert-P0R1RepositoryDependencyContract -RepositoryRoot $RepositoryRoot
$installedLicense = Assert-P0R1DependencyManifestContract `
    -ManifestPath (Join-Path $RepositoryRoot 'docs\dependencies\analogboard-p0-r1-dependencies.json')
Assert-Equal -Actual $installedLicense.PrimaryInstalledPath -Expected 'C:\Program Files\dotnet\LICENSE.txt' -Message 'Primary license path'
Assert-Equal -Actual $installedLicense.PrimarySizeBytes -Expected 9519L -Message 'Primary license size'
Assert-Equal -Actual $installedLicense.ThirdPartyNoticesSizeBytes -Expected 78887L -Message 'Third-party notices size'

# Given: The only supported P0-R1 verification tuple.
# When: The selection is validated.
# Then: Focused / Release / x64 passes unchanged.
$selection = Assert-P0R1VerificationSelection -Mode 'Focused' -Configuration 'Release' -Architecture 'x64'
Assert-Equal -Actual $selection.Mode -Expected 'Focused' -Message 'Mode selection'
Assert-Equal -Actual $selection.Configuration -Expected 'Release' -Message 'Configuration selection'
Assert-Equal -Actual $selection.Architecture -Expected 'x64' -Message 'Architecture selection'

# Given: Empty and unsupported selection values.
# When/Then: Every value fails with the exact ArgumentException contract.
$invalidSelections = @(
    @{ Mode = ''; Configuration = 'Release'; Architecture = 'x64'; Parameter = 'Mode'; Value = '' },
    @{ Mode = 'Phase'; Configuration = 'Release'; Architecture = 'x64'; Parameter = 'Mode'; Value = 'Phase' },
    @{ Mode = 'Focused'; Configuration = ''; Architecture = 'x64'; Parameter = 'Configuration'; Value = '' },
    @{ Mode = 'Focused'; Configuration = 'Debug'; Architecture = 'x64'; Parameter = 'Configuration'; Value = 'Debug' },
    @{ Mode = 'Focused'; Configuration = 'Release'; Architecture = ''; Parameter = 'Architecture'; Value = '' },
    @{ Mode = 'Focused'; Configuration = 'Release'; Architecture = 'arm64'; Parameter = 'Architecture'; Value = 'arm64' }
)
foreach ($case in $invalidSelections) {
    $expected = [ArgumentException]::new(
        "$($case.Parameter) must be exactly '$(@{ Mode = 'Focused'; Configuration = 'Release'; Architecture = 'x64' }[$case.Parameter])'; got '$($case.Value)'.",
        $case.Parameter
    ).Message
    Assert-ThrowsExact -Action {
        Assert-P0R1VerificationSelection `
            -Mode $case.Mode `
            -Configuration $case.Configuration `
            -Architecture $case.Architecture
    } -ExpectedType 'System.ArgumentException' -ExpectedMessage $expected
}

# Given: The exact SDK and Desktop Runtime inventory.
# When: Toolchain output is validated.
# Then: Exact versions are returned without roll-forward.
$toolchain = Assert-P0R1ToolchainOutput `
    -SdkVersionLines @('10.0.302') `
    -RuntimeLines @('Microsoft.NETCore.App 10.0.10 [C:\dotnet]', 'Microsoft.WindowsDesktop.App 10.0.10 [C:\dotnet]')
Assert-Equal -Actual $toolchain.SdkVersion -Expected '10.0.302' -Message 'SDK version'
Assert-Equal -Actual $toolchain.DesktopRuntimeVersion -Expected '10.0.10' -Message 'Desktop Runtime version'

# Given: SDK/runtime versions one patch away and a missing runtime.
# When/Then: Each substitution uses an exact InvalidOperationException contract.
$toolchainFailures = @(
    @{ Sdk = @('10.0.301'); Runtime = @('Microsoft.WindowsDesktop.App 10.0.10 [C:\dotnet]'); Expected = "Exact .NET SDK 10.0.302 is required; dotnet --version returned '10.0.301'." },
    @{ Sdk = @('10.0.303'); Runtime = @('Microsoft.WindowsDesktop.App 10.0.10 [C:\dotnet]'); Expected = "Exact .NET SDK 10.0.302 is required; dotnet --version returned '10.0.303'." },
    @{ Sdk = @('10.0.302'); Runtime = @('Microsoft.WindowsDesktop.App 10.0.9 [C:\dotnet]'); Expected = 'Exact .NET Desktop Runtime 10.0.10 is required; matching entries: 0.' },
    @{ Sdk = @('10.0.302'); Runtime = @(); Expected = 'Exact .NET Desktop Runtime 10.0.10 is required; matching entries: 0.' }
)
foreach ($case in $toolchainFailures) {
    Assert-ThrowsExact -Action {
        Assert-P0R1ToolchainOutput -SdkVersionLines $case.Sdk -RuntimeLines $case.Runtime
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage $case.Expected
}

$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) ('analogboard-p0-r1-contract-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
try {
    # Given: A manifest whose installed primary license size differs by one byte.
    # When/Then: The immutable installed-license evidence fails exactly.
    $invalidManifestPath = Join-Path $temporaryRoot 'invalid-dependencies.json'
    $invalidManifest = Get-Content `
        -LiteralPath (Join-Path $RepositoryRoot 'docs\dependencies\analogboard-p0-r1-dependencies.json') `
        -Raw `
        -Encoding UTF8 | ConvertFrom-Json
    $invalidManifest.offline_sdk.license.primary.size_bytes = 9520
    $invalidManifest | ConvertTo-Json -Depth 10 |
        Set-Content -LiteralPath $invalidManifestPath -Encoding UTF8
    Assert-ThrowsExact -Action {
        Assert-P0R1DependencyManifestContract -ManifestPath $invalidManifestPath
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 dependency manifest primary installed license evidence mismatch.'

    # Given: One drift from each non-license manifest pin group.
    # When/Then: Offline identity, renderer, restore, and execution pins fail loud.
    $manifestDrifts = @(
        @{
            Name = 'offline-url'
            Mutate = { param($value) $value.offline_sdk.url = 'https://example.invalid/sdk.zip' }
            Expected = 'P0-R1 offline SDK identity mismatch.'
        },
        @{
            Name = 'renderer-policy'
            Mutate = { param($value) $value.renderer.raster_buffer_policy = 'per-frame' }
            Expected = 'P0-R1 renderer dependency contract mismatch.'
        },
        @{
            Name = 'restore-network'
            Mutate = { param($value) $value.restore.network_enabled = $true }
            Expected = 'P0-R1 restore isolation manifest contract mismatch.'
        },
        @{
            Name = 'toolchain-architecture'
            Mutate = { param($value) $value.toolchain.architecture = 'arm64' }
            Expected = 'P0-R1 toolchain execution contract mismatch.'
        }
    )
    foreach ($case in $manifestDrifts) {
        $driftPath = Join-Path $temporaryRoot ("manifest-$($case.Name).json")
        $drift = Get-Content `
            -LiteralPath (Join-Path $RepositoryRoot 'docs\dependencies\analogboard-p0-r1-dependencies.json') `
            -Raw `
            -Encoding UTF8 | ConvertFrom-Json
        & $case.Mutate $drift
        $drift | ConvertTo-Json -Depth 10 |
            Set-Content -LiteralPath $driftPath -Encoding UTF8
        Assert-ThrowsExact -Action {
            Assert-P0R1DependencyManifestContract -ManifestPath $driftPath
        } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage $case.Expected
    }

    # Given: Zero projects and no solution.
    # When: The scaffold state is inspected.
    # Then: Pre-scaffold Batch 1 remains an explicit contract-only state.
    $emptyPrototype = New-ContractFixture -Root (Join-Path $temporaryRoot 'empty') -Shape Empty
    $null = Assert-P0R1RestoreIsolation -PrototypeRoot $emptyPrototype
    $emptyState = Get-P0R1PrototypeState -PrototypeRoot $emptyPrototype
    Assert-Equal -Actual $emptyState.Status -Expected 'ContractOnly' -Message 'Zero-project state'
    Assert-Equal -Actual $emptyState.ProjectCount -Expected 0 -Message 'Zero-project count'

    # Given: One of the required three projects.
    # When/Then: A partial scaffold is rejected with the exact count.
    $oneProject = New-ContractFixture -Root (Join-Path $temporaryRoot 'one') -Shape CoreOnly
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $oneProject
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 prototype must contain exactly 3 projects; found 1.'

    # Given: The exact Core/WPF/Tests projects, references, resources, and Release|x64 solution.
    # When: Dependency and scaffold contracts are inspected.
    # Then: The prototype is complete with three projects.
    $completePrototype = New-ContractFixture -Root (Join-Path $temporaryRoot 'complete')
    $null = Assert-P0R1RestoreIsolation -PrototypeRoot $completePrototype
    $completeState = Get-P0R1PrototypeState -PrototypeRoot $completePrototype
    Assert-Equal -Actual $completeState.Status -Expected 'Complete' -Message 'Complete scaffold state'
    Assert-Equal -Actual $completeState.ProjectCount -Expected 3 -Message 'Complete project count'

    # Given: A fourth project file outside the fixed graph.
    # When/Then: The upper project-count boundary is rejected exactly.
    $fourProjects = New-ContractFixture -Root (Join-Path $temporaryRoot 'four')
    $extraRoot = Join-Path $fourProjects 'src\Unexpected'
    New-Item -ItemType Directory -Path $extraRoot | Out-Null
    '<Project Sdk="Microsoft.NET.Sdk" />' |
        Set-Content -LiteralPath (Join-Path $extraRoot 'Unexpected.csproj') -Encoding UTF8
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $fourProjects
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 prototype must contain exactly 3 projects; found 4.'

    # Given: Three files but a solution missing WPF membership.
    # When/Then: Membership fails rather than silently building a subset.
    $missingMember = New-ContractFixture -Root (Join-Path $temporaryRoot 'missing-member')
    Set-FixtureSolution -PrototypeRoot $missingMember -Projects @('Core', 'Tests')
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $missingMember
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 solution project membership mismatch. Missing: AnalogBoard.ScatterRendering.Wpf. Unexpected: none.'

    # Given: The exact members plus an unexpected solution member.
    # When/Then: Unexpected membership is rejected exactly.
    $unexpectedMember = New-ContractFixture -Root (Join-Path $temporaryRoot 'unexpected-member')
    Set-FixtureSolution -PrototypeRoot $unexpectedMember -IncludeUnexpected
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $unexpectedMember
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 solution project membership mismatch. Missing: none. Unexpected: Unexpected.Project.'

    # Given: An exact project name/path whose declaration GUID differs from its mappings.
    # When/Then: The project tuple is rejected before mapping validation.
    $guidMismatch = New-ContractFixture -Root (Join-Path $temporaryRoot 'guid-mismatch')
    $guidMismatchPath = Join-Path $guidMismatch 'AnalogBoard.ScatterRenderingPrototype.sln'
    $guidMismatchSource = Get-Content -LiteralPath $guidMismatchPath -Raw -Encoding UTF8
    $guidMismatchSource = $guidMismatchSource.Replace(
        '"src\AnalogBoard.ScatterRendering.Core\AnalogBoard.ScatterRendering.Core.csproj", "{5BD7B476-EF87-4F58-8CC4-F59D1110DD00}"',
        '"src\AnalogBoard.ScatterRendering.Core\AnalogBoard.ScatterRendering.Core.csproj", "{11111111-1111-1111-1111-111111111111}"'
    )
    Set-Content -LiteralPath $guidMismatchPath -Value $guidMismatchSource -Encoding UTF8
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $guidMismatch
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage "P0-R1 solution project GUID mismatch for 'AnalogBoard.ScatterRendering.Core': '{11111111-1111-1111-1111-111111111111}'."

    # Given: A solution exposing Debug|x64 in addition to Release|x64.
    # When/Then: The extra configuration and mappings are rejected.
    $debugSolution = New-ContractFixture -Root (Join-Path $temporaryRoot 'debug') -IncludeDebug
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $debugSolution
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 solution must expose Release|x64 only. Actual: Debug|x64, Release|x64.'

    # Given: A Release solution with one missing or redirected project build mapping.
    # When/Then: Every project must have exact Release|x64 ActiveCfg and Build.0 mappings.
    foreach ($mappingMutation in @('MissingBuild', 'DebugActive')) {
        $mappingFixture = New-ContractFixture -Root (Join-Path $temporaryRoot ("mapping-$mappingMutation"))
        $mappingPath = Join-Path $mappingFixture 'AnalogBoard.ScatterRenderingPrototype.sln'
        $mappingSource = Get-Content -LiteralPath $mappingPath -Raw -Encoding UTF8
        if ($mappingMutation -eq 'MissingBuild') {
            $mappingSource = $mappingSource.Replace(
                "`t`t{5BD7B476-EF87-4F58-8CC4-F59D1110DD00}.Release|x64.Build.0 = Release|x64`r`n",
                ''
            )
        }
        else {
            $mappingSource = $mappingSource.Replace(
                '{5BD7B476-EF87-4F58-8CC4-F59D1110DD00}.Release|x64.ActiveCfg = Release|x64',
                '{5BD7B476-EF87-4F58-8CC4-F59D1110DD00}.Release|x64.ActiveCfg = Debug|x64'
            )
        }
        Set-Content -LiteralPath $mappingPath -Value $mappingSource -Encoding UTF8
        Assert-ThrowsExact -Action {
            Get-P0R1PrototypeState -PrototypeRoot $mappingFixture
        } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 solution must contain exact Release|x64 ActiveCfg and Build.0 mappings for all three projects.'
    }

    # Given: Invalid WPF metadata or a missing resource file.
    # When/Then: Each WPF scaffold violation has an exact actionable message.
    $wpfFalse = New-ContractFixture -Root (Join-Path $temporaryRoot 'wpf-false')
    $wpfFalsePath = Join-Path $wpfFalse 'src\AnalogBoard.ScatterRendering.Wpf\AnalogBoard.ScatterRendering.Wpf.csproj'
    (Get-Content -LiteralPath $wpfFalsePath -Raw -Encoding UTF8).Replace('<UseWPF>true</UseWPF>', '<UseWPF>false</UseWPF>') |
        Set-Content -LiteralPath $wpfFalsePath -Encoding UTF8
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $wpfFalse
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage "P0-R1 WPF project must set UseWPF=true and OutputType=Library."

    $wpfExe = New-ContractFixture -Root (Join-Path $temporaryRoot 'wpf-exe')
    $wpfExePath = Join-Path $wpfExe 'src\AnalogBoard.ScatterRendering.Wpf\AnalogBoard.ScatterRendering.Wpf.csproj'
    (Get-Content -LiteralPath $wpfExePath -Raw -Encoding UTF8).Replace('<OutputType>Library</OutputType>', '<OutputType>Exe</OutputType>') |
        Set-Content -LiteralPath $wpfExePath -Encoding UTF8
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $wpfExe
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage "P0-R1 WPF project must set UseWPF=true and OutputType=Library."

    $missingResource = New-ContractFixture -Root (Join-Path $temporaryRoot 'missing-resource')
    Remove-Item -LiteralPath (Join-Path $missingResource 'src\AnalogBoard.ScatterRendering.Wpf\Properties\Resources.resx')
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $missingResource
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage "P0-R1 WPF resource is absent: src/AnalogBoard.ScatterRendering.Wpf/Properties/Resources.resx."

    $missingResourceKey = New-ContractFixture -Root (Join-Path $temporaryRoot 'missing-resource-key')
    $missingResourceKeyPath = Join-Path $missingResourceKey 'src\AnalogBoard.ScatterRendering.Wpf\Properties\Resources.resx'
    [xml]$missingResourceKeyXml = Get-Content -LiteralPath $missingResourceKeyPath -Raw -Encoding UTF8
    $missingResourceKeyNode = $missingResourceKeyXml.SelectSingleNode("/root/data[@name='SurfaceWrongThread']")
    $null = $missingResourceKeyNode.ParentNode.RemoveChild($missingResourceKeyNode)
    $missingResourceKeyXml.Save($missingResourceKeyPath)
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $missingResourceKey
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 WPF resource key is absent: SurfaceWrongThread.'

    $messageBoxSource = New-ContractFixture -Root (Join-Path $temporaryRoot 'message-box-source')
    $messageBoxSourcePath = Join-Path $messageBoxSource 'src\AnalogBoard.ScatterRendering.Wpf\Forbidden.cs'
    'internal static class Forbidden { internal static void Show() => System.Windows.MessageBox.Show("blocked"); }' |
        Set-Content -LiteralPath $messageBoxSourcePath -Encoding UTF8
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $messageBoxSource
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'MessageBox is forbidden in P0-R1 WPF source: src/AnalogBoard.ScatterRendering.Wpf/Forbidden.cs.'

    # Given: A test project without WPF support or not the required self-hosted executable.
    # When/Then: UseWPF and OutputType must be exact for direct surface tests.
    $testsLibrary = New-ContractFixture -Root (Join-Path $temporaryRoot 'tests-library')
    $testsLibraryPath = Join-Path $testsLibrary 'tests\AnalogBoard.ScatterRendering.Tests\AnalogBoard.ScatterRendering.Tests.csproj'
    (Get-Content -LiteralPath $testsLibraryPath -Raw -Encoding UTF8).Replace('<OutputType>Exe</OutputType>', '<OutputType>Library</OutputType>') |
        Set-Content -LiteralPath $testsLibraryPath -Encoding UTF8
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $testsLibrary
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 Tests project must set UseWPF=true and OutputType=Exe.'

    $testsWithoutWpf = New-ContractFixture -Root (Join-Path $temporaryRoot 'tests-without-wpf')
    $testsWithoutWpfPath = Join-Path $testsWithoutWpf 'tests\AnalogBoard.ScatterRendering.Tests\AnalogBoard.ScatterRendering.Tests.csproj'
    (Get-Content -LiteralPath $testsWithoutWpfPath -Raw -Encoding UTF8).Replace('<UseWPF>true</UseWPF>', '<UseWPF>false</UseWPF>') |
        Set-Content -LiteralPath $testsWithoutWpfPath -Encoding UTF8
    Assert-ThrowsExact -Action {
        Get-P0R1PrototypeState -PrototypeRoot $testsWithoutWpf
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 Tests project must set UseWPF=true and OutputType=Exe.'

    # Given: A Core project using an undeclared SDK or architecture override.
    # When/Then: Every project-level toolchain identity is fixed before restore.
    foreach ($coreMutation in @('Sdk', 'Platform')) {
        $coreFixture = New-ContractFixture -Root (Join-Path $temporaryRoot ("core-$coreMutation"))
        $corePath = Join-Path $coreFixture 'src\AnalogBoard.ScatterRendering.Core\AnalogBoard.ScatterRendering.Core.csproj'
        $coreSource = Get-Content -LiteralPath $corePath -Raw -Encoding UTF8
        if ($coreMutation -eq 'Sdk') {
            $coreSource = $coreSource.Replace('Sdk="Microsoft.NET.Sdk"', 'Sdk="Forbidden.External.Sdk"')
        }
        else {
            $coreSource = $coreSource.Replace(
                '<TargetFramework>net10.0-windows</TargetFramework>',
                '<TargetFramework>net10.0-windows</TargetFramework><PlatformTarget>arm64</PlatformTarget>'
            )
        }
        Set-Content -LiteralPath $corePath -Value $coreSource -Encoding UTF8
        Assert-ThrowsExact -Action {
            Get-P0R1PrototypeState -PrototypeRoot $coreFixture
        } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 Core project must use Microsoft.NET.Sdk, net10.0-windows, and PlatformTarget=x64.'
    }

    # Given: Every explicit dependency-capable MSBuild element forbidden by the review contract.
    # When/Then: Each element is rejected by name and repository-relative file.
    $forbiddenElements = @(
        @{ Name = 'Reference'; Fragment = '<ItemGroup><Reference Include="System.Xml" /></ItemGroup>' },
        @{ Name = 'COMReference'; Fragment = '<ItemGroup><COMReference Include="Legacy" /></ItemGroup>' },
        @{ Name = 'NativeReference'; Fragment = '<ItemGroup><NativeReference Include="native.manifest" /></ItemGroup>' },
        @{ Name = 'Analyzer'; Fragment = '<ItemGroup><Analyzer Include="analyzer.dll" /></ItemGroup>' },
        @{ Name = 'PackageReference'; Fragment = '<ItemGroup><PackageReference Include="Forbidden.Package" Version="1.0.0" /></ItemGroup>' },
        @{ Name = 'PackageDownload'; Fragment = '<ItemGroup><PackageDownload Include="Forbidden.Package" Version="[1.0.0]" /></ItemGroup>' },
        @{ Name = 'PackageVersion'; Fragment = '<ItemGroup><PackageVersion Include="Forbidden.Package" Version="1.0.0" /></ItemGroup>' },
        @{ Name = 'UsingTask'; Fragment = '<UsingTask TaskName="Forbidden.Task" AssemblyFile="task.dll" />' },
        @{ Name = 'Import'; Fragment = '<Import Project="forbidden.targets" />' }
    )
    foreach ($case in $forbiddenElements) {
        $fixture = New-ContractFixture -Root (Join-Path $temporaryRoot ("forbidden-" + $case.Name.ToLowerInvariant()))
        $coreProject = Join-Path $fixture 'src\AnalogBoard.ScatterRendering.Core\AnalogBoard.ScatterRendering.Core.csproj'
        Add-FixtureXml -ProjectPath $coreProject -Fragment $case.Fragment
        Assert-ThrowsExact -Action {
            Assert-P0R1RestoreIsolation -PrototypeRoot $fixture
        } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage "Forbidden MSBuild element '$($case.Name)' in 'src/AnalogBoard.ScatterRendering.Core/AnalogBoard.ScatterRendering.Core.csproj'."
    }

    # Given: Every restore source/fallback/config override.
    # When/Then: Each property is rejected exactly before dotnet runs.
    $restoreProperties = @(
        'RestoreSources',
        'RestoreAdditionalProjectSources',
        'RestoreFallbackFolders',
        'RestoreAdditionalProjectFallbackFolders',
        'RestoreConfigFile',
        'RestorePackagesPath'
    )
    foreach ($propertyName in $restoreProperties) {
        $fixture = New-ContractFixture -Root (Join-Path $temporaryRoot ("restore-" + $propertyName.ToLowerInvariant()))
        $coreProject = Join-Path $fixture 'src\AnalogBoard.ScatterRendering.Core\AnalogBoard.ScatterRendering.Core.csproj'
        Add-FixtureXml -ProjectPath $coreProject -Fragment "<PropertyGroup><$propertyName>forbidden</$propertyName></PropertyGroup>"
        Assert-ThrowsExact -Action {
            Assert-P0R1RestoreIsolation -PrototypeRoot $fixture
        } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage "Restore override '$propertyName' is forbidden in 'src/AnalogBoard.ScatterRendering.Core/AnalogBoard.ScatterRendering.Core.csproj'."
    }

    # Given: A ProjectReference that escapes the prototype root.
    # When/Then: Resolution outside the root is rejected before file access.
    $outsideReference = New-ContractFixture -Root (Join-Path $temporaryRoot 'outside-reference')
    $outsideTests = Join-Path $outsideReference 'tests\AnalogBoard.ScatterRendering.Tests\AnalogBoard.ScatterRendering.Tests.csproj'
    (Get-Content -LiteralPath $outsideTests -Raw -Encoding UTF8).Replace(
        '..\..\src\AnalogBoard.ScatterRendering.Core\AnalogBoard.ScatterRendering.Core.csproj',
        '..\..\..\outside.csproj'
    ) | Set-Content -LiteralPath $outsideTests -Encoding UTF8
    Assert-ThrowsExact -Action {
        Assert-P0R1RestoreIsolation -PrototypeRoot $outsideReference
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage "ProjectReference from 'tests/AnalogBoard.ScatterRendering.Tests/AnalogBoard.ScatterRendering.Tests.csproj' with Include '..\..\..\outside.csproj' resolves outside the prototype root."

    # Given: A within-root ProjectReference not present in the three-edge allowlist.
    # When/Then: The unexpected edge is rejected exactly.
    $wrongReference = New-ContractFixture -Root (Join-Path $temporaryRoot 'wrong-reference')
    $wrongCore = Join-Path $wrongReference 'src\AnalogBoard.ScatterRendering.Core\AnalogBoard.ScatterRendering.Core.csproj'
    Add-FixtureXml -ProjectPath $wrongCore -Fragment '<ItemGroup><ProjectReference Include="..\..\tests\AnalogBoard.ScatterRendering.Tests\AnalogBoard.ScatterRendering.Tests.csproj" /></ItemGroup>'
    Assert-ThrowsExact -Action {
        Assert-P0R1RestoreIsolation -PrototypeRoot $wrongReference
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage "ProjectReference from 'src/AnalogBoard.ScatterRendering.Core/AnalogBoard.ScatterRendering.Core.csproj' to 'tests/AnalogBoard.ScatterRendering.Tests/AnalogBoard.ScatterRendering.Tests.csproj' is not allowed."

    # Given: One valid bounded final test summary.
    # When: It is parsed.
    # Then: Exact total/passed/failed counts are returned.
    $summary = Get-P0R1TestSummary -OutputLines @('PASS first', 'SUMMARY total=2 passed=2 failed=0')
    Assert-Equal -Actual $summary.Total -Expected 2 -Message 'Summary total'
    Assert-Equal -Actual $summary.Passed -Expected 2 -Message 'Summary passed'
    Assert-Equal -Actual $summary.Failed -Expected 0 -Message 'Summary failed'

    # Given: Missing, duplicate, zero, nonzero, and inconsistent summaries.
    # When/Then: Every malformed outcome is rejected with an exact message.
    $summaryFailures = @(
        @{ Lines = @('PASS only'); Expected = 'Test runner output must contain exactly one SUMMARY line; found 0.' },
        @{ Lines = @('SUMMARY total=1 passed=1 failed=0', 'SUMMARY total=1 passed=1 failed=0'); Expected = 'Test runner output must contain exactly one SUMMARY line; found 2.' },
        @{ Lines = @('SUMMARY total=0 passed=0 failed=0'); Expected = 'Test runner summary must report total>0; got total=0 passed=0 failed=0.' },
        @{ Lines = @('SUMMARY total=3 passed=2 failed=1'); Expected = 'Test runner summary must report failed=0; got total=3 passed=2 failed=1.' },
        @{ Lines = @('SUMMARY total=3 passed=1 failed=0'); Expected = 'Test runner summary is inconsistent: total=3, passed=1, failed=0.' },
        @{ Lines = @('SUMMARY total=1 passed=1 failed=0', 'unexpected trailing output'); Expected = 'Test runner SUMMARY line must be the final non-empty line.' }
    )
    foreach ($case in $summaryFailures) {
        Assert-ThrowsExact -Action {
            Get-P0R1TestSummary -OutputLines $case.Lines
        } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage $case.Expected
    }

    # Given: One bounded development-only observation emitted before the final summary.
    # When: Its JSON contract is parsed.
    # Then: Scope, hard fixture, pending-one, hash, and non-official status are retained.
    $observationJson = '{"schema_id":"analogboard.scatter-rendering.development-observation.v1","development_only":true,"official_acceptance":false,"fixture_id":"AB-P0-R1-HARD-SCATTER-v1","event_count":100001,"width":512,"height":512,"iterations":10,"frame_ms_p95":12.5,"frame_ms_max":14.0,"allocated_bytes_per_frame":128,"core_scheduler_test_double_submit_p99_ms":0.02,"poster_identity":"single_slot_test_double","pending_work_max":1,"logical_pending_work_max":1,"poster_accepted_callbacks":1000,"poster_completed_callbacks":1000,"poster_aborted_callbacks":0,"coalesced_frames":0,"raster_sha256":"255bf3f549baa92d87a65111c37bed815f0b74c3452a387c6a1cc6d168b61780","machine":"fixture"}'
    $observation = Get-P0R1DevelopmentObservation -OutputLines @(
        "OBSERVATION $observationJson",
        'SUMMARY total=1 passed=1 failed=0'
    )
    Assert-Equal -Actual $observation -Expected $observationJson -Message 'Development observation JSON'

    foreach ($invalidObservation in @(
        @{ Json = $observationJson.Replace('"development_only":true', '"development_only":false'); Expected = 'P0-R1 observation must be development-only and must not claim official acceptance.' },
        @{ Json = $observationJson.Replace('"development_only":true', '"development_only":"true"'); Expected = 'P0-R1 development observation field must be a JSON boolean: development_only.' },
        @{ Json = $observationJson.Replace('"official_acceptance":false', '"official_acceptance":"false"'); Expected = 'P0-R1 development observation field must be a JSON boolean: official_acceptance.' },
        @{ Json = $observationJson.Replace('"event_count":100001', '"event_count":100000'); Expected = 'P0-R1 observation must use the 100001-event 512x512 hard scatter fixture.' },
        @{ Json = $observationJson.Replace('"pending_work_max":1', '"pending_work_max":2'); Expected = 'P0-R1 observation pending_work_max must be between 0 and 1.' },
        @{ Json = $observationJson.Replace('"logical_pending_work_max":1', '"logical_pending_work_max":0'); Expected = 'P0-R1 observation physical and logical pending maxima must both equal one.' },
        @{ Json = $observationJson.Replace('"event_count":100001', '"event_count":100000.6'); Expected = 'P0-R1 development observation field must be an integer JSON number: event_count.' },
        @{ Json = $observationJson.Replace('"frame_ms_p95":12.5', '"frame_ms_p95":15.0'); Expected = 'P0-R1 observation frame_ms_max must be greater than or equal to frame_ms_p95.' },
        @{ Json = $observationJson.Replace('"frame_ms_p95":12.5', '"frame_ms_p95":"12.5"'); Expected = 'P0-R1 development observation field must be a JSON number: frame_ms_p95.' },
        @{ Json = $observationJson.Replace('"frame_ms_max":14.0', '"frame_ms_max":"14.0"'); Expected = 'P0-R1 development observation field must be a JSON number: frame_ms_max.' },
        @{ Json = $observationJson.Replace('"core_scheduler_test_double_submit_p99_ms":0.02', '"core_scheduler_test_double_submit_p99_ms":"0.02"'); Expected = 'P0-R1 development observation field must be a JSON number: core_scheduler_test_double_submit_p99_ms.' },
        @{ Json = $observationJson.Replace('"coalesced_frames":0', '"coalesced_frames":0.5'); Expected = 'P0-R1 development observation field must be an integer JSON number: coalesced_frames.' },
        @{ Json = $observationJson.Replace('"poster_completed_callbacks":1000', '"poster_completed_callbacks":999'); Expected = 'P0-R1 observation poster accepted callbacks must equal completed plus aborted callbacks.' },
        @{ Json = $observationJson.Replace('"coalesced_frames":0', '"coalesced_frames":1'); Expected = 'P0-R1 observation fixed scheduler smoke counts mismatch.' },
        @{ Json = $observationJson.Replace('"fixture_id":"AB-P0-R1-HARD-SCATTER-v1"', '"fixture_id":"drift"'); Expected = 'P0-R1 observation fixture identity mismatch.' },
        @{ Json = $observationJson.Replace('"raster_sha256":"255bf3f549baa92d87a65111c37bed815f0b74c3452a387c6a1cc6d168b61780"', '"raster_sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"'); Expected = 'P0-R1 observation raster_sha256 does not match the hard fixture.' },
        @{ Json = $observationJson.Replace('"machine":"fixture"', '"machine":123'); Expected = 'P0-R1 development observation field must be a JSON string: machine.' },
        @{ Json = $observationJson.Replace('"machine":"fixture"', '"machine":" "'); Expected = 'P0-R1 observation machine identity must not be empty.' }
    )) {
        Assert-ThrowsExact -Action {
            Get-P0R1DevelopmentObservation -OutputLines @("OBSERVATION $($invalidObservation.Json)")
        } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage $invalidObservation.Expected
    }
    Assert-ThrowsExact -Action {
        Get-P0R1DevelopmentObservation -OutputLines @(
            "OBSERVATION $($observationJson.Replace(',"coalesced_frames":0', ''))"
        )
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessage 'P0-R1 development observation field is absent: coalesced_frames.'

    # Given: A complete scaffold and a successful fake dotnet boundary.
    # When: Focused verification is orchestrated.
    # Then: Commands are isolated and the bounded test summary reaches the result.
    $calls = [System.Collections.Generic.List[object]]::new()
    $successfulInvoker = {
        param([string[]]$Arguments, [string]$WorkingDirectory)
        $calls.Add([pscustomobject]@{ Arguments = @($Arguments); WorkingDirectory = $WorkingDirectory })
        $output = switch ($Arguments[0]) {
            '--version' { @('10.0.302') }
            '--list-runtimes' { @('Microsoft.WindowsDesktop.App 10.0.10 [C:\dotnet]') }
            'run' { @("OBSERVATION $observationJson", 'PASS fixture', 'SUMMARY total=2 passed=2 failed=0') }
            default { @('fixture command passed') }
        }
        return [pscustomobject]@{ ExitCode = 0; Output = $output }
    }
    $result = Invoke-P0R1FocusedVerification `
        -RepositoryRoot (Split-Path -Parent (Split-Path -Parent $completePrototype)) `
        -PrototypeRoot $completePrototype `
        -Configuration 'Release' `
        -Architecture 'x64' `
        -DotNetInvoker $successfulInvoker
    Assert-Equal -Actual $result.Status -Expected 'Pass' -Message 'Focused status'
    Assert-Equal -Actual $result.TestsTotal -Expected 2 -Message 'Focused tests total'
    Assert-Equal -Actual $result.TestsPassed -Expected 2 -Message 'Focused tests passed'
    Assert-Equal -Actual $result.TestsFailed -Expected 0 -Message 'Focused tests failed'
    Assert-Equal -Actual $result.DevelopmentObservation -Expected $observationJson -Message 'Focused development observation'
    Assert-Equal -Actual $calls.Count -Expected 5 -Message 'dotnet command count'
    Assert-Contains -Actual $calls[2].Arguments -Expected '--configfile' -Message 'Restore config isolation'
    Assert-Contains -Actual $calls[3].Arguments -Expected '--no-restore' -Message 'Build restore isolation'
    Assert-Contains -Actual $calls[4].Arguments -Expected '--no-build' -Message 'Runner build isolation'
    Assert-Contains -Actual $calls[4].Arguments -Expected '--no-restore' -Message 'Runner restore isolation'

    # Given: A dynamic subprocess failure during restore.
    # When/Then: Only the dedicated dynamic-output helper uses pattern matching.
    $failingInvoker = {
        param([string[]]$Arguments, [string]$WorkingDirectory)
        if ($Arguments[0] -eq '--version') {
            return [pscustomobject]@{ ExitCode = 0; Output = @('10.0.302') }
        }
        if ($Arguments[0] -eq '--list-runtimes') {
            return [pscustomobject]@{ ExitCode = 0; Output = @('Microsoft.WindowsDesktop.App 10.0.10 [C:\dotnet]') }
        }
        return [pscustomobject]@{ ExitCode = 17; Output = @('simulated restore failure') }
    }
    Assert-ThrowsDynamicOutput -Action {
        Invoke-P0R1FocusedVerification `
            -RepositoryRoot (Split-Path -Parent (Split-Path -Parent $completePrototype)) `
            -PrototypeRoot $completePrototype `
            -Configuration 'Release' `
            -Architecture 'x64' `
            -DotNetInvoker $failingInvoker
    } -ExpectedType 'System.InvalidOperationException' -ExpectedMessagePattern '*dotnet restore*exit code 17*simulated restore failure*'
}
finally {
    Remove-Item -LiteralPath $temporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
}

# Given: The real Batch 1 scaffold after all repository-local files exist.
# When: Its exact dependency, graph, solution, WPF, and test-runner contracts are checked.
# Then: It is accepted only after Debug mappings are removed.
$realPrototype = Join-Path $RepositoryRoot 'prototypes\scatter-rendering'
$null = Assert-P0R1RestoreIsolation -PrototypeRoot $realPrototype
$realState = Get-P0R1PrototypeState -PrototypeRoot $realPrototype
Assert-Equal -Actual $realState.ProjectCount -Expected 3 -Message 'Real scaffold project count'

$wrapperSource = Get-Content -LiteralPath $WrapperPath -Raw -Encoding UTF8
foreach ($requiredToken in @(
    "[ValidateSet('Focused')]",
    "[ValidateSet('Release')]",
    "[ValidateSet('x64')]",
    "`$env:DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE = '1'",
    'tests_total=',
    'tests_passed=',
    'tests_failed=',
    'development_observation='
)) {
    if ($wrapperSource -notmatch [regex]::Escape($requiredToken)) {
        throw "verify.ps1 is missing required contract token: $requiredToken"
    }
}
$environmentIndex = $wrapperSource.IndexOf("`$env:DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE = '1'", [StringComparison]::Ordinal)
$dotnetIndex = $wrapperSource.IndexOf('Invoke-P0R1FocusedVerification', [StringComparison]::Ordinal)
if ($environmentIndex -lt 0 -or $environmentIndex -gt $dotnetIndex) {
    throw 'The workload-update notification guard must be set before every dotnet invocation.'
}

Write-Host 'PASS: scatter-rendering verification contract tests'

# Test execution:
# powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/scatter-rendering/tests/verify_contract_test.ps1
# Coverage target: every exported wrapper-contract branch; no external PowerShell coverage package is permitted.
