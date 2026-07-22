Set-StrictMode -Version Latest

$script:ExpectedSdkVersion = '10.0.302'
$script:ExpectedDesktopRuntimeVersion = '10.0.10'
$script:ExpectedTargetFramework = 'net10.0-windows'
$script:ExpectedOfflineSdkSha512 = '7d170ed75fa9af34c00646621d92011dbd71943952e2787cd15df9be78e6452b55dadef34d7eff77b802e6af4959e071a55855ac649afeac70901c3a2a258716'
$script:ExpectedOfflineSdkSizeBytes = 297545270L
$script:ExpectedOfflineSdkUrl = 'https://builds.dotnet.microsoft.com/dotnet/Sdk/10.0.302/dotnet-sdk-10.0.302-win-x64.zip'
$script:ExpectedOfflineSdkFilename = 'dotnet-sdk-10.0.302-win-x64.zip'
$script:ExpectedOfflineSdkPolicy = 'identity record only; do not download, install, or commit the binary'
$script:ExpectedPrimaryLicensePath = 'C:\Program Files\dotnet\LICENSE.txt'
$script:ExpectedPrimaryLicenseSizeBytes = 9519L
$script:ExpectedPrimaryLicenseSha256 = '7f6839a61ce892b79c6549e2dc5a81fdbd240a0b260f8881216b45b7fda8b45d'
$script:ExpectedThirdPartyNoticesPath = 'C:\Program Files\dotnet\ThirdPartyNotices.txt'
$script:ExpectedThirdPartyNoticesSizeBytes = 78887L
$script:ExpectedThirdPartyNoticesSha256 = 'deb4427a295e1ed474b0d81c5a0d972c1b550b9a715cda939cdfa9236b1b418f'
$script:SolutionRelativePath = 'AnalogBoard.ScatterRenderingPrototype.sln'
$script:TestProjectRelativePath = 'tests\AnalogBoard.ScatterRendering.Tests\AnalogBoard.ScatterRendering.Tests.csproj'
$script:ExpectedProjectPaths = @(
    'src/AnalogBoard.ScatterRendering.Core/AnalogBoard.ScatterRendering.Core.csproj',
    'src/AnalogBoard.ScatterRendering.Wpf/AnalogBoard.ScatterRendering.Wpf.csproj',
    'tests/AnalogBoard.ScatterRendering.Tests/AnalogBoard.ScatterRendering.Tests.csproj'
)

function Assert-P0R1VerificationSelection {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Mode,

        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Configuration,

        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Architecture
    )

    if ($Mode -cne 'Focused') {
        throw [ArgumentException]::new("Mode must be exactly 'Focused'; got '$Mode'.", 'Mode')
    }
    if ($Configuration -cne 'Release') {
        throw [ArgumentException]::new("Configuration must be exactly 'Release'; got '$Configuration'.", 'Configuration')
    }
    if ($Architecture -cne 'x64') {
        throw [ArgumentException]::new("Architecture must be exactly 'x64'; got '$Architecture'.", 'Architecture')
    }

    return [pscustomobject]@{
        Mode = $Mode
        Configuration = $Configuration
        Architecture = $Architecture
    }
}

function Assert-P0R1ToolchainOutput {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [string[]]$SdkVersionLines,

        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [string[]]$RuntimeLines
    )

    $sdkVersions = @($SdkVersionLines | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
    $actualSdk = if ($sdkVersions.Count -eq 1) { $sdkVersions[0] } else { $sdkVersions -join '; ' }
    if ($sdkVersions.Count -ne 1 -or $actualSdk -cne $script:ExpectedSdkVersion) {
        throw [InvalidOperationException]::new(
            "Exact .NET SDK $($script:ExpectedSdkVersion) is required; dotnet --version returned '$actualSdk'."
        )
    }

    $runtimePattern = '^Microsoft\.WindowsDesktop\.App\s+' + [regex]::Escape($script:ExpectedDesktopRuntimeVersion) + '\s+\['
    $desktopRuntime = @($RuntimeLines | Where-Object { $_ -match $runtimePattern })
    if ($desktopRuntime.Count -ne 1) {
        throw [InvalidOperationException]::new(
            "Exact .NET Desktop Runtime $($script:ExpectedDesktopRuntimeVersion) is required; matching entries: $($desktopRuntime.Count)."
        )
    }

    return [pscustomobject]@{
        SdkVersion = $script:ExpectedSdkVersion
        DesktopRuntimeVersion = $script:ExpectedDesktopRuntimeVersion
    }
}

function Get-RequiredFilePath {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$RelativePath
    )

    $path = Join-Path $Root $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw [IO.FileNotFoundException]::new("Required P0-R1 contract file is absent: $RelativePath", $path)
    }
    return (Resolve-Path -LiteralPath $path -ErrorAction Stop).Path
}

function Assert-P0R1RepositoryDependencyContract {
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot)

    $resolvedRoot = (Resolve-Path -LiteralPath $RepositoryRoot -ErrorAction Stop).Path
    $globalJsonPath = Get-RequiredFilePath -Root $resolvedRoot -RelativePath 'global.json'
    $propsPath = Get-RequiredFilePath -Root $resolvedRoot -RelativePath 'prototypes\scatter-rendering\Directory.Build.props'
    $manifestPath = Get-RequiredFilePath -Root $resolvedRoot -RelativePath 'docs\dependencies\analogboard-p0-r1-dependencies.json'

    $globalJson = Get-Content -LiteralPath $globalJsonPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ([string]$globalJson.sdk.version -cne $script:ExpectedSdkVersion -or
        [string]$globalJson.sdk.rollForward -cne 'disable' -or
        $globalJson.sdk.allowPrerelease -ne $false) {
        throw [InvalidOperationException]::new('global.json must pin SDK 10.0.302 with rollForward=disable and allowPrerelease=false.')
    }

    [xml]$props = Get-Content -LiteralPath $propsPath -Raw -Encoding UTF8
    $propertyGroup = $props.Project.PropertyGroup
    if ([string]$propertyGroup.TargetFramework -cne $script:ExpectedTargetFramework -or
        [string]$propertyGroup.Configurations -cne 'Release' -or
        [string]$propertyGroup.Platforms -cne 'x64' -or
        [string]$propertyGroup.PlatformTarget -cne 'x64') {
        throw [InvalidOperationException]::new('Directory.Build.props must pin net10.0-windows, Release, and x64.')
    }
    if ([string]$propertyGroup.RestoreIgnoreFailedSources -cne 'false' -or
        [string]$propertyGroup.NuGetAudit -cne 'false') {
        throw [InvalidOperationException]::new('Directory.Build.props must fail on unavailable sources and disable network audit.')
    }

    $manifestIdentity = Assert-P0R1DependencyManifestContract -ManifestPath $manifestPath
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ([int]$manifest.format_version -ne 1 -or
        [string]$manifest.scope -cne 'P0-R1 bounded real-time visualization path decision') {
        throw [InvalidOperationException]::new('P0-R1 dependency manifest format or scope mismatch.')
    }
    if ([string]$manifest.toolchain.sdk_version -cne $script:ExpectedSdkVersion -or
        [string]$manifest.toolchain.desktop_runtime_version -cne $script:ExpectedDesktopRuntimeVersion -or
        [string]$manifest.toolchain.target_framework -cne $script:ExpectedTargetFramework) {
        throw [InvalidOperationException]::new('P0-R1 dependency manifest toolchain identity mismatch.')
    }
    if ([string]$manifest.offline_sdk.sha512 -cne $script:ExpectedOfflineSdkSha512 -or
        [long]$manifest.offline_sdk.size_bytes -ne $script:ExpectedOfflineSdkSizeBytes) {
        throw [InvalidOperationException]::new('P0-R1 offline SDK hash or size mismatch.')
    }
    if (@($manifest.external_nuget_packages).Count -ne 0 -or
        [string]$manifest.renderer.name -cne 'WPF WriteableBitmap') {
        throw [InvalidOperationException]::new('P0-R1 must use WPF WriteableBitmap with zero external NuGet packages.')
    }

    return [pscustomobject]@{
        SdkVersion = $script:ExpectedSdkVersion
        DesktopRuntimeVersion = $script:ExpectedDesktopRuntimeVersion
        TargetFramework = $script:ExpectedTargetFramework
        ExternalNuGetPackageCount = 0
        InstalledLicense = $manifestIdentity
    }
}

function Assert-P0R1DependencyManifestContract {
    param([Parameter(Mandatory = $true)][string]$ManifestPath)

    $resolvedPath = (Resolve-Path -LiteralPath $ManifestPath -ErrorAction Stop).Path
    $manifest = Get-Content -LiteralPath $resolvedPath -Raw -Encoding UTF8 | ConvertFrom-Json

    if ([int]$manifest.format_version -ne 1 -or
        [string]$manifest.scope -cne 'P0-R1 bounded real-time visualization path decision') {
        throw [InvalidOperationException]::new('P0-R1 dependency manifest format or scope mismatch.')
    }
    if ([string]$manifest.toolchain.sdk_version -cne $script:ExpectedSdkVersion -or
        [string]$manifest.toolchain.desktop_runtime_version -cne $script:ExpectedDesktopRuntimeVersion -or
        [string]$manifest.toolchain.target_framework -cne $script:ExpectedTargetFramework -or
        [string]$manifest.toolchain.architecture -cne 'x64' -or
        [string]$manifest.toolchain.configuration -cne 'Release' -or
        [string]$manifest.toolchain.global_json.path -cne 'global.json' -or
        [string]$manifest.toolchain.global_json.roll_forward -cne 'disable' -or
        $manifest.toolchain.global_json.allow_prerelease -ne $false) {
        throw [InvalidOperationException]::new('P0-R1 toolchain execution contract mismatch.')
    }
    if ([string]$manifest.offline_sdk.url -cne $script:ExpectedOfflineSdkUrl -or
        [string]$manifest.offline_sdk.expected_filename -cne $script:ExpectedOfflineSdkFilename -or
        [string]$manifest.offline_sdk.policy -cne $script:ExpectedOfflineSdkPolicy -or
        [string]$manifest.offline_sdk.sha512 -cne $script:ExpectedOfflineSdkSha512 -or
        [long]$manifest.offline_sdk.size_bytes -ne $script:ExpectedOfflineSdkSizeBytes) {
        throw [InvalidOperationException]::new('P0-R1 offline SDK identity mismatch.')
    }
    if (@($manifest.external_nuget_packages).Count -ne 0 -or
        [string]$manifest.renderer.name -cne 'WPF WriteableBitmap' -or
        [string]$manifest.renderer.framework -cne 'WPF' -or
        [int]$manifest.renderer.external_package_count -ne 0 -or
        [string]$manifest.renderer.raster_buffer_policy -cne 'preallocated') {
        throw [InvalidOperationException]::new('P0-R1 renderer dependency contract mismatch.')
    }
    if ([string]$manifest.restore.config_path -cne 'prototypes/scatter-rendering/NuGet.Config' -or
        $manifest.restore.network_enabled -ne $false -or
        @($manifest.restore.package_sources).Count -ne 0 -or
        @($manifest.restore.fallback_package_folders).Count -ne 0 -or
        $manifest.restore.project_restore_overrides_allowed -ne $false) {
        throw [InvalidOperationException]::new('P0-R1 restore isolation manifest contract mismatch.')
    }

    $primary = $manifest.offline_sdk.license.primary
    if ([string]$primary.name -cne 'MIT' -or
        [string]$primary.installed_path -cne $script:ExpectedPrimaryLicensePath -or
        [long]$primary.size_bytes -ne $script:ExpectedPrimaryLicenseSizeBytes -or
        [string]$primary.sha256 -cne $script:ExpectedPrimaryLicenseSha256) {
        throw [InvalidOperationException]::new(
            'P0-R1 dependency manifest primary installed license evidence mismatch.'
        )
    }

    $notices = $manifest.offline_sdk.license.third_party_notices
    if ([string]$notices.installed_path -cne $script:ExpectedThirdPartyNoticesPath -or
        [long]$notices.size_bytes -ne $script:ExpectedThirdPartyNoticesSizeBytes -or
        [string]$notices.sha256 -cne $script:ExpectedThirdPartyNoticesSha256) {
        throw [InvalidOperationException]::new(
            'P0-R1 dependency manifest installed third-party notices evidence mismatch.'
        )
    }

    return [pscustomobject]@{
        PrimaryInstalledPath = [string]$primary.installed_path
        PrimarySizeBytes = [long]$primary.size_bytes
        PrimarySha256 = [string]$primary.sha256
        ThirdPartyNoticesInstalledPath = [string]$notices.installed_path
        ThirdPartyNoticesSizeBytes = [long]$notices.size_bytes
        ThirdPartyNoticesSha256 = [string]$notices.sha256
    }
}

function Get-P0R1NormalizedRelativePath {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$FullPath
    )

    $normalizedRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $normalizedPath = [IO.Path]::GetFullPath($FullPath)
    $prefix = $normalizedRoot + '\'
    if (-not $normalizedPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw [InvalidOperationException]::new("Path resolves outside the prototype root: $normalizedPath")
    }
    return $normalizedPath.Substring($prefix.Length).Replace('\', '/')
}

function Assert-P0R1RestoreIsolation {
    param([Parameter(Mandatory = $true)][string]$PrototypeRoot)

    $resolvedRoot = (Resolve-Path -LiteralPath $PrototypeRoot -ErrorAction Stop).Path
    $configPath = Get-RequiredFilePath -Root $resolvedRoot -RelativePath 'NuGet.Config'
    [xml]$config = Get-Content -LiteralPath $configPath -Raw -Encoding UTF8

    foreach ($section in @('packageSources', 'fallbackPackageFolders', 'auditSources')) {
        $clearNodes = @($config.SelectNodes("/configuration/$section/clear"))
        $addNodes = @($config.SelectNodes("/configuration/$section/add"))
        if ($clearNodes.Count -ne 1 -or $addNodes.Count -ne 0) {
            throw [InvalidOperationException]::new("NuGet.Config $section must contain one clear and zero add entries.")
        }
    }

    $definitionFiles = @(
        Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File |
            Where-Object {
                $_.Extension -in @('.csproj', '.props', '.targets') -and
                $_.FullName -notmatch '\\(?:bin|obj)\\'
            }
    )
    $projectFiles = @($definitionFiles | Where-Object { $_.Extension -eq '.csproj' })
    $forbiddenElements = @(
        'Reference',
        'COMReference',
        'NativeReference',
        'Analyzer',
        'PackageReference',
        'PackageDownload',
        'PackageVersion',
        'UsingTask',
        'Import'
    )
    $forbiddenRestoreProperties = @(
        'RestoreSources',
        'RestoreAdditionalProjectSources',
        'RestoreFallbackFolders',
        'RestoreAdditionalProjectFallbackFolders',
        'RestoreConfigFile',
        'RestorePackagesPath'
    )
    $allowedProjectEdges = @(
        'src/AnalogBoard.ScatterRendering.Wpf/AnalogBoard.ScatterRendering.Wpf.csproj|src/AnalogBoard.ScatterRendering.Core/AnalogBoard.ScatterRendering.Core.csproj',
        'tests/AnalogBoard.ScatterRendering.Tests/AnalogBoard.ScatterRendering.Tests.csproj|src/AnalogBoard.ScatterRendering.Core/AnalogBoard.ScatterRendering.Core.csproj',
        'tests/AnalogBoard.ScatterRendering.Tests/AnalogBoard.ScatterRendering.Tests.csproj|src/AnalogBoard.ScatterRendering.Wpf/AnalogBoard.ScatterRendering.Wpf.csproj'
    )
    $observedProjectEdges = [System.Collections.Generic.List[string]]::new()

    foreach ($definitionFile in $definitionFiles) {
        $relativePath = Get-P0R1NormalizedRelativePath -Root $resolvedRoot -FullPath $definitionFile.FullName
        [xml]$definition = Get-Content -LiteralPath $definitionFile.FullName -Raw -Encoding UTF8

        foreach ($elementName in $forbiddenElements) {
            if (@($definition.SelectNodes("//*[local-name()='$elementName']")).Count -gt 0) {
                throw [InvalidOperationException]::new(
                    "Forbidden MSBuild element '$elementName' in '$relativePath'."
                )
            }
        }
        foreach ($propertyName in $forbiddenRestoreProperties) {
            if (@($definition.SelectNodes("//*[local-name()='$propertyName']")).Count -gt 0) {
                throw [InvalidOperationException]::new(
                    "Restore override '$propertyName' is forbidden in '$relativePath'."
                )
            }
        }

        if ($definitionFile.Extension -ne '.csproj') {
            continue
        }
        foreach ($projectReference in @($definition.SelectNodes("//*[local-name()='ProjectReference']"))) {
            $include = [string]$projectReference.Include
            $targetPath = [IO.Path]::GetFullPath((Join-Path $definitionFile.DirectoryName $include))
            $rootPrefix = [IO.Path]::GetFullPath($resolvedRoot).TrimEnd('\') + '\'
            if (-not $targetPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
                throw [InvalidOperationException]::new(
                    "ProjectReference from '$relativePath' with Include '$include' resolves outside the prototype root."
                )
            }
            $targetRelative = Get-P0R1NormalizedRelativePath -Root $resolvedRoot -FullPath $targetPath
            $edge = "$relativePath|$targetRelative"
            if ($allowedProjectEdges -cnotcontains $edge) {
                throw [InvalidOperationException]::new(
                    "ProjectReference from '$relativePath' to '$targetRelative' is not allowed."
                )
            }
            $observedProjectEdges.Add($edge)
        }
    }

    if ($projectFiles.Count -eq 3) {
        $actualEdges = @($observedProjectEdges | Sort-Object -CaseSensitive)
        $expectedEdges = @($allowedProjectEdges | Sort-Object -CaseSensitive)
        if (($actualEdges -join "`n") -cne ($expectedEdges -join "`n")) {
            throw [InvalidOperationException]::new(
                'P0-R1 ProjectReference graph must contain exactly WPF-to-Core, Tests-to-Core, and Tests-to-WPF.'
            )
        }
    }

    return [pscustomobject]@{
        NuGetConfigPath = $configPath
        ProjectCount = $projectFiles.Count
        PackageReferenceCount = 0
    }
}

function Get-P0R1PrototypeState {
    param([Parameter(Mandatory = $true)][string]$PrototypeRoot)

    $resolvedRoot = (Resolve-Path -LiteralPath $PrototypeRoot -ErrorAction Stop).Path
    $projects = @(
        Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File -Filter '*.csproj' |
            Where-Object { $_.FullName -notmatch '\\(?:bin|obj)\\' }
    )
    $solutionPath = Join-Path $resolvedRoot $script:SolutionRelativePath
    $testProjectPath = Join-Path $resolvedRoot $script:TestProjectRelativePath
    $solutionExists = Test-Path -LiteralPath $solutionPath -PathType Leaf

    if ($projects.Count -eq 0 -and -not $solutionExists) {
        return [pscustomobject]@{
            Status = 'ContractOnly'
            ProjectCount = 0
            SolutionPath = $null
            TestProjectPath = $null
        }
    }
    if ($projects.Count -ne 3) {
        throw [InvalidOperationException]::new(
            "P0-R1 prototype must contain exactly 3 projects; found $($projects.Count)."
        )
    }
    if (-not $solutionExists) {
        throw [InvalidOperationException]::new(
            "P0-R1 prototype solution is absent: $($script:SolutionRelativePath)."
        )
    }

    $expectedByName = [ordered]@{
        'AnalogBoard.ScatterRendering.Core' = $script:ExpectedProjectPaths[0]
        'AnalogBoard.ScatterRendering.Wpf' = $script:ExpectedProjectPaths[1]
        'AnalogBoard.ScatterRendering.Tests' = $script:ExpectedProjectPaths[2]
    }
    $expectedGuidByName = [ordered]@{
        'AnalogBoard.ScatterRendering.Core' = '{5BD7B476-EF87-4F58-8CC4-F59D1110DD00}'
        'AnalogBoard.ScatterRendering.Wpf' = '{F5A0A84B-2AFE-43D7-BC3C-D7597CFD7B7E}'
        'AnalogBoard.ScatterRendering.Tests' = '{83C17C7C-D260-48EA-9D55-66EF504D3BAF}'
    }
    foreach ($expectedPath in $script:ExpectedProjectPaths) {
        $nativeRelativePath = $expectedPath.Replace('/', '\')
        if (-not (Test-Path -LiteralPath (Join-Path $resolvedRoot $nativeRelativePath) -PathType Leaf)) {
            throw [InvalidOperationException]::new("Required P0-R1 project is absent: $expectedPath.")
        }
    }

    $solutionSource = Get-Content -LiteralPath $solutionPath -Raw -Encoding UTF8
    $projectMatches = [regex]::Matches(
        $solutionSource,
        '(?m)^Project\("[^"]+"\) = "([^"]+)", "([^"]+)", "([^"]+)"\r?$'
    )
    $actualByName = @{}
    $actualGuidByName = @{}
    foreach ($match in $projectMatches) {
        $actualByName[[string]$match.Groups[1].Value] =
            ([string]$match.Groups[2].Value).Replace('\', '/')
        $actualGuidByName[[string]$match.Groups[1].Value] = [string]$match.Groups[3].Value
    }
    $missingNames = @(
        $expectedByName.Keys |
            Where-Object { -not $actualByName.ContainsKey($_) } |
            Sort-Object
    )
    $unexpectedNames = @(
        $actualByName.Keys |
            Where-Object { -not $expectedByName.Contains($_) } |
            Sort-Object
    )
    if ($missingNames.Count -gt 0 -or $unexpectedNames.Count -gt 0) {
        $missingText = if ($missingNames.Count -eq 0) { 'none' } else { $missingNames -join ', ' }
        $unexpectedText = if ($unexpectedNames.Count -eq 0) { 'none' } else { $unexpectedNames -join ', ' }
        throw [InvalidOperationException]::new(
            "P0-R1 solution project membership mismatch. Missing: $missingText. Unexpected: $unexpectedText."
        )
    }
    foreach ($expectedName in $expectedByName.Keys) {
        if ([string]$actualByName[$expectedName] -cne [string]$expectedByName[$expectedName]) {
            throw [InvalidOperationException]::new(
                "P0-R1 solution path mismatch for '$expectedName': '$($actualByName[$expectedName])'."
            )
        }
        if ([string]$actualGuidByName[$expectedName] -cne [string]$expectedGuidByName[$expectedName]) {
            throw [InvalidOperationException]::new(
                "P0-R1 solution project GUID mismatch for '$expectedName': '$($actualGuidByName[$expectedName])'."
            )
        }
    }

    $configurationMatches = [regex]::Matches(
        $solutionSource,
        '(?m)^\s*(Debug|Release)\|x64\s*=\s*\1\|x64\s*\r?$'
    )
    $configurations = @(
        $configurationMatches |
            ForEach-Object { "$($_.Groups[1].Value)|x64" } |
            Sort-Object -Unique
    )
    if ($configurations.Count -ne 1 -or $configurations[0] -cne 'Release|x64') {
        $actualConfiguration = if ($configurations.Count -eq 0) {
            'none'
        }
        else {
            $configurations -join ', '
        }
        throw [InvalidOperationException]::new(
            "P0-R1 solution must expose Release|x64 only. Actual: $actualConfiguration."
        )
    }

    $expectedProjectMappings = @(
        foreach ($projectName in $expectedByName.Keys) {
            $projectGuid = [string]$actualGuidByName[$projectName]
            "$projectGuid.Release|x64.ActiveCfg = Release|x64"
            "$projectGuid.Release|x64.Build.0 = Release|x64"
        }
    ) | Sort-Object -CaseSensitive
    $projectConfigurationSection = [regex]::Match(
        $solutionSource,
        '(?ms)^\s*GlobalSection\(ProjectConfigurationPlatforms\)\s*=\s*postSolution\s*\r?$.*?^\s*EndGlobalSection\s*\r?$'
    )
    $actualProjectMappings = if ($projectConfigurationSection.Success) {
        @(
            [regex]::Matches(
                $projectConfigurationSection.Value,
                '(?m)^\s*(\{[0-9A-Fa-f-]+\}\.[^\r\n]+?)\s*\r?$'
            ) |
                ForEach-Object { [string]$_.Groups[1].Value } |
                Sort-Object -CaseSensitive
        )
    }
    else {
        @()
    }
    if (($actualProjectMappings -join "`n") -cne ($expectedProjectMappings -join "`n")) {
        throw [InvalidOperationException]::new(
            'P0-R1 solution must contain exact Release|x64 ActiveCfg and Build.0 mappings for all three projects.'
        )
    }

    $coreProjectPath = Join-Path $resolvedRoot $script:ExpectedProjectPaths[0].Replace('/', '\')
    [xml]$coreProject = Get-Content -LiteralPath $coreProjectPath -Raw -Encoding UTF8
    if ([string]$coreProject.Project.Sdk -cne 'Microsoft.NET.Sdk' -or
        [string]$coreProject.Project.PropertyGroup.TargetFramework -cne $script:ExpectedTargetFramework -or
        [string]$coreProject.Project.PropertyGroup.PlatformTarget -cne 'x64') {
        throw [InvalidOperationException]::new(
            'P0-R1 Core project must use Microsoft.NET.Sdk, net10.0-windows, and PlatformTarget=x64.'
        )
    }

    $wpfProjectPath = Join-Path $resolvedRoot $script:ExpectedProjectPaths[1].Replace('/', '\')
    [xml]$wpfProject = Get-Content -LiteralPath $wpfProjectPath -Raw -Encoding UTF8
    if ([string]$wpfProject.Project.Sdk -cne 'Microsoft.NET.Sdk' -or
        [string]$wpfProject.Project.PropertyGroup.TargetFramework -cne $script:ExpectedTargetFramework -or
        [string]$wpfProject.Project.PropertyGroup.PlatformTarget -cne 'x64' -or
        [string]$wpfProject.Project.PropertyGroup.UseWPF -cne 'true' -or
        [string]$wpfProject.Project.PropertyGroup.OutputType -cne 'Library') {
        throw [InvalidOperationException]::new(
            'P0-R1 WPF project must set UseWPF=true and OutputType=Library.'
        )
    }
    $resourceRelativePath = 'src/AnalogBoard.ScatterRendering.Wpf/Properties/Resources.resx'
    $resourcePath = Join-Path $resolvedRoot $resourceRelativePath.Replace('/', '\')
    if (-not (Test-Path -LiteralPath $resourcePath -PathType Leaf)) {
        throw [InvalidOperationException]::new("P0-R1 WPF resource is absent: $resourceRelativePath.")
    }
    [xml]$resources = Get-Content -LiteralPath $resourcePath -Raw -Encoding UTF8
    $requiredResourceKeys = @(
        'SurfaceBufferLengthMismatch',
        'SurfaceDisposed',
        'SurfaceGenerationNotIncreasing',
        'SurfaceHeightOutOfRange',
        'SurfaceOwnerMustBeSta',
        'SurfaceWidthOutOfRange',
        'SurfaceWrongThread'
    )
    $actualResourceKeys = @(
        $resources.SelectNodes('/root/data') |
            ForEach-Object { [string]$_.name }
    )
    foreach ($resourceKey in $requiredResourceKeys) {
        if ($actualResourceKeys -cnotcontains $resourceKey) {
            throw [InvalidOperationException]::new("P0-R1 WPF resource key is absent: $resourceKey.")
        }
    }

    $wpfSourceRoot = Join-Path $resolvedRoot 'src\AnalogBoard.ScatterRendering.Wpf'
    $wpfSources = @(
        Get-ChildItem -LiteralPath $wpfSourceRoot -Filter '*.cs' -File -Recurse |
            Where-Object { $_.FullName -notmatch '[\\/](bin|obj)[\\/]' }
    )
    foreach ($wpfSource in $wpfSources) {
        $wpfSourceText = Get-Content -LiteralPath $wpfSource.FullName -Raw -Encoding UTF8
        if ($wpfSourceText -match '\bMessageBox\b') {
            $wpfSourceRelative = Get-P0R1NormalizedRelativePath -Root $resolvedRoot -FullPath $wpfSource.FullName
            throw [InvalidOperationException]::new(
                "MessageBox is forbidden in P0-R1 WPF source: $wpfSourceRelative."
            )
        }
    }

    [xml]$testsProject = Get-Content -LiteralPath $testProjectPath -Raw -Encoding UTF8
    if ([string]$testsProject.Project.Sdk -cne 'Microsoft.NET.Sdk' -or
        [string]$testsProject.Project.PropertyGroup.TargetFramework -cne $script:ExpectedTargetFramework -or
        [string]$testsProject.Project.PropertyGroup.PlatformTarget -cne 'x64' -or
        [string]$testsProject.Project.PropertyGroup.UseWPF -cne 'true' -or
        [string]$testsProject.Project.PropertyGroup.OutputType -cne 'Exe') {
        throw [InvalidOperationException]::new('P0-R1 Tests project must set UseWPF=true and OutputType=Exe.')
    }

    return [pscustomobject]@{
        Status = 'Complete'
        ProjectCount = $projects.Count
        SolutionPath = (Resolve-Path -LiteralPath $solutionPath).Path
        TestProjectPath = (Resolve-Path -LiteralPath $testProjectPath).Path
    }
}

function Get-P0R1TestSummary {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [string[]]$OutputLines
    )

    $nonEmptyLines = @($OutputLines | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    $summaryLines = @($nonEmptyLines | Where-Object { $_.StartsWith('SUMMARY ', [StringComparison]::Ordinal) })
    if ($summaryLines.Count -ne 1) {
        throw [InvalidOperationException]::new(
            "Test runner output must contain exactly one SUMMARY line; found $($summaryLines.Count)."
        )
    }
    if ($nonEmptyLines[-1] -cne $summaryLines[0]) {
        throw [InvalidOperationException]::new(
            'Test runner SUMMARY line must be the final non-empty line.'
        )
    }

    $match = [regex]::Match(
        $summaryLines[0],
        '^SUMMARY total=([0-9]+) passed=([0-9]+) failed=([0-9]+)$'
    )
    if (-not $match.Success) {
        throw [InvalidOperationException]::new(
            "Test runner SUMMARY format is invalid: '$($summaryLines[0])'."
        )
    }

    $total = [int]$match.Groups[1].Value
    $passed = [int]$match.Groups[2].Value
    $failed = [int]$match.Groups[3].Value
    if ($total -le 0) {
        throw [InvalidOperationException]::new(
            "Test runner summary must report total>0; got total=$total passed=$passed failed=$failed."
        )
    }
    if ($failed -ne 0) {
        throw [InvalidOperationException]::new(
            "Test runner summary must report failed=0; got total=$total passed=$passed failed=$failed."
        )
    }
    if ($passed + $failed -ne $total) {
        throw [InvalidOperationException]::new(
            "Test runner summary is inconsistent: total=$total, passed=$passed, failed=$failed."
        )
    }

    return [pscustomobject]@{
        Total = $total
        Passed = $passed
        Failed = $failed
    }
}

function Get-P0R1DevelopmentObservation {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [string[]]$OutputLines
    )

    $prefix = 'OBSERVATION '
    $observationLines = @(
        $OutputLines |
            Where-Object { $_.StartsWith($prefix, [StringComparison]::Ordinal) }
    )
    if ($observationLines.Count -ne 1) {
        throw [InvalidOperationException]::new(
            "Test runner output must contain exactly one OBSERVATION line; found $($observationLines.Count)."
        )
    }

    $json = $observationLines[0].Substring($prefix.Length)
    try {
        $observation = $json | ConvertFrom-Json -ErrorAction Stop
    }
    catch {
        throw [InvalidOperationException]::new('P0-R1 development observation must be valid JSON.')
    }

    $requiredFields = @(
        'schema_id',
        'development_only',
        'official_acceptance',
        'fixture_id',
        'event_count',
        'width',
        'height',
        'iterations',
        'frame_ms_p95',
        'frame_ms_max',
        'allocated_bytes_per_frame',
        'core_scheduler_test_double_submit_p99_ms',
        'poster_identity',
        'pending_work_max',
        'logical_pending_work_max',
        'poster_accepted_callbacks',
        'poster_completed_callbacks',
        'poster_aborted_callbacks',
        'coalesced_frames',
        'raster_sha256',
        'machine'
    )
    $actualFields = @($observation.PSObject.Properties.Name)
    foreach ($fieldName in $requiredFields) {
        if ($actualFields -cnotcontains $fieldName) {
            throw [InvalidOperationException]::new(
                "P0-R1 development observation field is absent: $fieldName."
            )
        }
    }

    if ([string]$observation.schema_id -cne 'analogboard.scatter-rendering.development-observation.v1') {
        throw [InvalidOperationException]::new('P0-R1 development observation schema mismatch.')
    }
    foreach ($fieldName in @('development_only', 'official_acceptance')) {
        if ($observation.$fieldName -isnot [bool]) {
            throw [InvalidOperationException]::new(
                "P0-R1 development observation field must be a JSON boolean: $fieldName."
            )
        }
    }
    if ($observation.development_only -ne $true -or $observation.official_acceptance -ne $false) {
        throw [InvalidOperationException]::new(
            'P0-R1 observation must be development-only and must not claim official acceptance.'
        )
    }
    if ([string]$observation.fixture_id -cne 'AB-P0-R1-HARD-SCATTER-v1') {
        throw [InvalidOperationException]::new('P0-R1 observation fixture identity mismatch.')
    }

    $integerFields = @(
        'event_count',
        'width',
        'height',
        'iterations',
        'allocated_bytes_per_frame',
        'pending_work_max',
        'logical_pending_work_max',
        'poster_accepted_callbacks',
        'poster_completed_callbacks',
        'poster_aborted_callbacks',
        'coalesced_frames'
    )
    foreach ($fieldName in $integerFields) {
        $value = $observation.$fieldName
        $isInteger = $value -is [sbyte] -or
            $value -is [byte] -or
            $value -is [int16] -or
            $value -is [uint16] -or
            $value -is [int32] -or
            $value -is [uint32] -or
            $value -is [int64] -or
            $value -is [uint64]
        if (-not $isInteger) {
            throw [InvalidOperationException]::new(
                "P0-R1 development observation field must be an integer JSON number: $fieldName."
            )
        }
    }
    if ([int]$observation.event_count -ne 100001 -or
        [int]$observation.width -ne 512 -or
        [int]$observation.height -ne 512) {
        throw [InvalidOperationException]::new(
            'P0-R1 observation must use the 100001-event 512x512 hard scatter fixture.'
        )
    }
    if ([int]$observation.iterations -ne 10) {
        throw [InvalidOperationException]::new('P0-R1 observation iterations must equal the fixed development window of 10.')
    }
    if ([int]$observation.pending_work_max -lt 0 -or [int]$observation.pending_work_max -gt 1) {
        throw [InvalidOperationException]::new(
            'P0-R1 observation pending_work_max must be between 0 and 1.'
        )
    }
    if ([int]$observation.logical_pending_work_max -lt 0 -or [int]$observation.logical_pending_work_max -gt 1) {
        throw [InvalidOperationException]::new(
            'P0-R1 observation logical_pending_work_max must be between 0 and 1.'
        )
    }
    foreach ($fieldName in @(
        'allocated_bytes_per_frame',
        'poster_accepted_callbacks',
        'poster_completed_callbacks',
        'poster_aborted_callbacks',
        'coalesced_frames'
    )) {
        if ([long]$observation.$fieldName -lt 0) {
            throw [InvalidOperationException]::new(
                "P0-R1 observation count must be non-negative: $fieldName."
            )
        }
    }

    foreach ($fieldName in @(
        'frame_ms_p95',
        'frame_ms_max',
        'core_scheduler_test_double_submit_p99_ms'
    )) {
        $value = $observation.$fieldName
        $isNumber = $value -is [sbyte] -or
            $value -is [byte] -or
            $value -is [int16] -or
            $value -is [uint16] -or
            $value -is [int32] -or
            $value -is [uint32] -or
            $value -is [int64] -or
            $value -is [uint64] -or
            $value -is [single] -or
            $value -is [double] -or
            $value -is [decimal]
        if (-not $isNumber) {
            throw [InvalidOperationException]::new(
                "P0-R1 development observation field must be a JSON number: $fieldName."
            )
        }

        $value = [double]$value
        if ([double]::IsNaN($value) -or [double]::IsInfinity($value) -or $value -lt 0) {
            throw [InvalidOperationException]::new(
                "P0-R1 observation metric must be finite and non-negative: $fieldName."
            )
        }
    }
    if ([double]$observation.frame_ms_max -lt [double]$observation.frame_ms_p95) {
        throw [InvalidOperationException]::new(
            'P0-R1 observation frame_ms_max must be greater than or equal to frame_ms_p95.'
        )
    }
    if ([string]$observation.poster_identity -cne 'single_slot_test_double') {
        throw [InvalidOperationException]::new('P0-R1 observation poster identity mismatch.')
    }
    if ([long]$observation.poster_accepted_callbacks -ne
        ([long]$observation.poster_completed_callbacks + [long]$observation.poster_aborted_callbacks)) {
        throw [InvalidOperationException]::new(
            'P0-R1 observation poster accepted callbacks must equal completed plus aborted callbacks.'
        )
    }
    if ([int]$observation.pending_work_max -ne 1 -or
        [int]$observation.logical_pending_work_max -ne 1) {
        throw [InvalidOperationException]::new(
            'P0-R1 observation physical and logical pending maxima must both equal one.'
        )
    }
    if ([long]$observation.poster_accepted_callbacks -ne 1000 -or
        [long]$observation.poster_completed_callbacks -ne 1000 -or
        [long]$observation.poster_aborted_callbacks -ne 0 -or
        [long]$observation.coalesced_frames -ne 0) {
        throw [InvalidOperationException]::new(
            'P0-R1 observation fixed scheduler smoke counts mismatch.'
        )
    }
    if ([string]$observation.raster_sha256 -cne '255bf3f549baa92d87a65111c37bed815f0b74c3452a387c6a1cc6d168b61780') {
        throw [InvalidOperationException]::new(
            'P0-R1 observation raster_sha256 does not match the hard fixture.'
        )
    }
    if ($observation.machine -isnot [string]) {
        throw [InvalidOperationException]::new(
            'P0-R1 development observation field must be a JSON string: machine.'
        )
    }
    if ([string]::IsNullOrWhiteSpace($observation.machine)) {
        throw [InvalidOperationException]::new('P0-R1 observation machine identity must not be empty.')
    }

    return $json
}

function Invoke-DefaultP0R1DotNet {
    param(
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory
    )

    Push-Location -LiteralPath $WorkingDirectory
    try {
        $output = @(& dotnet @Arguments 2>&1 | ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    return [pscustomobject]@{
        ExitCode = $exitCode
        Output = $output
    }
}

function Invoke-CheckedP0R1DotNet {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][scriptblock]$DotNetInvoker
    )

    $result = & $DotNetInvoker -Arguments $Arguments -WorkingDirectory $WorkingDirectory
    if ($null -eq $result -or
        $result.PSObject.Properties.Name -notcontains 'ExitCode' -or
        $result.PSObject.Properties.Name -notcontains 'Output') {
        throw [InvalidOperationException]::new("$Label returned no structured exit/output result.")
    }
    $outputLines = @($result.Output | ForEach-Object { $_.ToString() })
    if ([int]$result.ExitCode -ne 0) {
        throw [InvalidOperationException]::new(
            "$Label failed with exit code $($result.ExitCode): $($outputLines -join [Environment]::NewLine)"
        )
    }
    return $outputLines
}

function Invoke-P0R1FocusedVerification {
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string]$PrototypeRoot,
        [Parameter(Mandatory = $true)][string]$Configuration,
        [Parameter(Mandatory = $true)][string]$Architecture,
        [scriptblock]$DotNetInvoker = ${function:Invoke-DefaultP0R1DotNet}
    )

    $null = Assert-P0R1VerificationSelection `
        -Mode 'Focused' `
        -Configuration $Configuration `
        -Architecture $Architecture
    $resolvedRepositoryRoot = (Resolve-Path -LiteralPath $RepositoryRoot -ErrorAction Stop).Path
    $resolvedPrototypeRoot = (Resolve-Path -LiteralPath $PrototypeRoot -ErrorAction Stop).Path
    $restoreContract = Assert-P0R1RestoreIsolation -PrototypeRoot $resolvedPrototypeRoot

    $sdkLines = Invoke-CheckedP0R1DotNet `
        -Label 'dotnet --version' `
        -Arguments @('--version') `
        -WorkingDirectory $resolvedRepositoryRoot `
        -DotNetInvoker $DotNetInvoker
    $runtimeLines = Invoke-CheckedP0R1DotNet `
        -Label 'dotnet --list-runtimes' `
        -Arguments @('--list-runtimes') `
        -WorkingDirectory $resolvedRepositoryRoot `
        -DotNetInvoker $DotNetInvoker
    $toolchain = Assert-P0R1ToolchainOutput -SdkVersionLines $sdkLines -RuntimeLines $runtimeLines
    $state = Get-P0R1PrototypeState -PrototypeRoot $resolvedPrototypeRoot

    if ($state.Status -eq 'ContractOnly') {
        return [pscustomobject]@{
            Status = 'ContractOnly'
            SdkVersion = $toolchain.SdkVersion
            DesktopRuntimeVersion = $toolchain.DesktopRuntimeVersion
            ProjectCount = 0
            ProductTestsExecuted = $false
        }
    }

    $restoreArguments = @(
        'restore',
        $state.SolutionPath,
        '--configfile', $restoreContract.NuGetConfigPath,
        '--no-cache',
        '--force-evaluate',
        '--disable-parallel'
    )
    $null = Invoke-CheckedP0R1DotNet `
        -Label 'dotnet restore' `
        -Arguments $restoreArguments `
        -WorkingDirectory $resolvedPrototypeRoot `
        -DotNetInvoker $DotNetInvoker

    $buildArguments = @(
        'build',
        $state.SolutionPath,
        '--configuration', $Configuration,
        "-p:Platform=$Architecture",
        '--no-restore'
    )
    $null = Invoke-CheckedP0R1DotNet `
        -Label 'dotnet build' `
        -Arguments $buildArguments `
        -WorkingDirectory $resolvedPrototypeRoot `
        -DotNetInvoker $DotNetInvoker

    $runnerArguments = @(
        'run',
        '--project', $state.TestProjectPath,
        '--configuration', $Configuration,
        "-p:Platform=$Architecture",
        '--no-build',
        '--no-restore',
        '--'
    )
    $runnerOutput = Invoke-CheckedP0R1DotNet `
        -Label 'dotnet test runner' `
        -Arguments $runnerArguments `
        -WorkingDirectory $resolvedPrototypeRoot `
        -DotNetInvoker $DotNetInvoker
    $testSummary = Get-P0R1TestSummary -OutputLines $runnerOutput
    $developmentObservation = Get-P0R1DevelopmentObservation -OutputLines $runnerOutput

    return [pscustomobject]@{
        Status = 'Pass'
        SdkVersion = $toolchain.SdkVersion
        DesktopRuntimeVersion = $toolchain.DesktopRuntimeVersion
        ProjectCount = $state.ProjectCount
        ProductTestsExecuted = $true
        TestsTotal = $testSummary.Total
        TestsPassed = $testSummary.Passed
        TestsFailed = $testSummary.Failed
        DevelopmentObservation = $developmentObservation
    }
}

Export-ModuleMember -Function @(
    'Assert-P0R1VerificationSelection',
    'Assert-P0R1ToolchainOutput',
    'Assert-P0R1RepositoryDependencyContract',
    'Assert-P0R1DependencyManifestContract',
    'Assert-P0R1RestoreIsolation',
    'Get-P0R1PrototypeState',
    'Get-P0R1TestSummary',
    'Get-P0R1DevelopmentObservation',
    'Invoke-P0R1FocusedVerification'
)
