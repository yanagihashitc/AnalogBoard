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
$script:ExpectedDotNetExecutablePath = 'C:\Program Files\dotnet\dotnet.exe'
$script:SolutionRelativePath = 'AnalogBoard.ScatterRenderingPrototype.sln'
$script:TestProjectRelativePath = 'tests\AnalogBoard.ScatterRendering.Tests\AnalogBoard.ScatterRendering.Tests.csproj'
$script:ExpectedProjectPaths = @(
    'src/AnalogBoard.ScatterRendering.Core/AnalogBoard.ScatterRendering.Core.csproj',
    'src/AnalogBoard.ScatterRendering.Wpf/AnalogBoard.ScatterRendering.Wpf.csproj',
    'tests/AnalogBoard.ScatterRendering.Tests/AnalogBoard.ScatterRendering.Tests.csproj'
)
$script:ExpectedAncestorTerminatorHashes = [ordered]@{
    'prototypes\scatter-rendering\Directory.Build.targets' = '17284cb74605517cbec8a8153505da71a685b28694b67b5ef80608cd79b1c0ea'
    'prototypes\scatter-rendering\Directory.Packages.props' = 'af799a176bf7be0de5e660924f4bde3444170788e9633399a1f65f0b72c9602f'
    'prototypes\scatter-rendering\Directory.Solution.props' = '17284cb74605517cbec8a8153505da71a685b28694b67b5ef80608cd79b1c0ea'
    'prototypes\scatter-rendering\Directory.Solution.targets' = '17284cb74605517cbec8a8153505da71a685b28694b67b5ef80608cd79b1c0ea'
    'prototypes\scatter-rendering\Directory.Build.rsp' = '6391681febc56125b20d287b6cbb9d0bb52f2a600585ac9d8e66025ed45a4693'
}

function Get-P0R1SanitizedDotNetEnvironment {
    param(
        [Collections.IDictionary]$Environment =
            ([Environment]::GetEnvironmentVariables([EnvironmentVariableTarget]::Process))
    )

    $allowedNames = @(
        'ALLUSERSPROFILE',
        'APPDATA',
        'CommonProgramFiles',
        'CommonProgramFiles(x86)',
        'CommonProgramW6432',
        'COMPUTERNAME',
        'ComSpec',
        'HOMEDRIVE',
        'HOMEPATH',
        'LOCALAPPDATA',
        'NUMBER_OF_PROCESSORS',
        'OS',
        'PATHEXT',
        'PROCESSOR_ARCHITECTURE',
        'PROCESSOR_IDENTIFIER',
        'PROCESSOR_LEVEL',
        'PROCESSOR_REVISION',
        'ProgramData',
        'ProgramFiles',
        'ProgramFiles(x86)',
        'ProgramW6432',
        'PUBLIC',
        'SystemDrive',
        'SystemRoot',
        'TEMP',
        'TMP',
        'USERDOMAIN',
        'USERNAME',
        'USERPROFILE',
        'WINDIR'
    )
    $sanitized = [Collections.Generic.Dictionary[string, string]]::new(
        [StringComparer]::OrdinalIgnoreCase
    )
    foreach ($allowedName in $allowedNames) {
        foreach ($key in $Environment.Keys) {
            if ([string]::Equals([string]$key, $allowedName, [StringComparison]::OrdinalIgnoreCase)) {
                $value = [string]$Environment[$key]
                if (-not [string]::IsNullOrEmpty($value)) {
                    $sanitized[$allowedName] = $value
                }
                break
            }
        }
    }

    $sanitized['DOTNET_CLI_TELEMETRY_OPTOUT'] = '1'
    $sanitized['DOTNET_NOLOGO'] = '1'
    $sanitized['DOTNET_SKIP_FIRST_TIME_EXPERIENCE'] = '1'
    $sanitized['DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE'] = '1'
    $sanitized['DOTNET_MULTILEVEL_LOOKUP'] = '0'
    $sanitized['MSBUILDDISABLENODEREUSE'] = '1'
    $sanitized['DOTNET_CLI_USE_MSBUILD_SERVER'] = '0'
    return $sanitized
}

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
    if ([string]$propertyGroup.DiscoverEditorConfigFiles -cne 'false' -or
        [string]$propertyGroup.DiscoverGlobalAnalyzerConfigFiles -cne 'false') {
        throw [InvalidOperationException]::new(
            'Directory.Build.props must disable ancestor editor/global analyzer config discovery.'
        )
    }
    if ([string]$propertyGroup.UseSharedCompilation -cne 'false') {
        throw [InvalidOperationException]::new(
            'Directory.Build.props must disable shared compiler-server reuse.'
        )
    }

    foreach ($relativePath in $script:ExpectedAncestorTerminatorHashes.Keys) {
        $terminatorPath = Get-RequiredFilePath -Root $resolvedRoot -RelativePath $relativePath
        $actualHash = (Get-FileHash -LiteralPath $terminatorPath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -cne $script:ExpectedAncestorTerminatorHashes[$relativePath]) {
            throw [InvalidOperationException]::new(
                "P0-R1 build ancestor terminator hash mismatch: $($relativePath.Replace('\', '/'))."
            )
        }
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

function Clear-P0R1GeneratedBuildRoots {
    param(
        [Parameter(Mandatory = $true)][string]$PrototypeRoot
    )

    $resolvedRoot = (Resolve-Path -LiteralPath $PrototypeRoot -ErrorAction Stop).Path
    $rootPrefix = $resolvedRoot.TrimEnd('\') + '\'
    $removedRootCount = 0
    foreach ($projectRelativePath in $script:ExpectedProjectPaths) {
        $projectPath = Join-Path $resolvedRoot $projectRelativePath
        $projectRoot = Split-Path -Parent $projectPath
        foreach ($generatedName in @('bin', 'obj')) {
            $generatedPath = [IO.Path]::GetFullPath((Join-Path $projectRoot $generatedName))
            if (-not $generatedPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
                throw [InvalidOperationException]::new(
                    "P0-R1 generated root resolves outside the prototype: $generatedPath."
                )
            }
            if (-not (Test-Path -LiteralPath $generatedPath)) {
                continue
            }
            $generatedItem = Get-Item -LiteralPath $generatedPath -Force
            if (-not $generatedItem.PSIsContainer -or
                ($generatedItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw [InvalidOperationException]::new(
                    "P0-R1 generated root must be a non-reparse directory: $generatedPath."
                )
            }
            Remove-Item -LiteralPath $generatedPath -Recurse -Force
            $removedRootCount++
        }
    }

    return [pscustomobject]@{
        RemovedRootCount = $removedRootCount
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
        'HarnessCopyBufferLengthMismatch',
        'HarnessFrameShapeMismatch',
        'HarnessOwnerDispatcherRequired',
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

function Assert-P0R1NoDuplicateJsonProperties {
    param(
        [Parameter(Mandatory = $true)][string]$Json,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $containers = [System.Collections.Generic.List[object]]::new()
    $index = 0
    while ($index -lt $Json.Length) {
        $character = $Json[$index]
        if ($character -eq '"') {
            $start = $index
            $index++
            while ($index -lt $Json.Length) {
                if ($Json[$index] -eq '\') {
                    $index += 2
                    continue
                }
                if ($Json[$index] -eq '"') {
                    break
                }
                $index++
            }
            if ($index -ge $Json.Length) {
                break
            }

            $next = $index + 1
            while ($next -lt $Json.Length -and [char]::IsWhiteSpace($Json[$next])) {
                $next++
            }
            if ($next -lt $Json.Length -and $Json[$next] -eq ':' -and
                $containers.Count -gt 0) {
                $container = $containers[$containers.Count - 1]
                if ($container.Kind -ceq 'Object') {
                    $rawPropertyName = $Json.Substring($start, ($index - $start) + 1)
                    try {
                        $propertyName = ConvertFrom-Json -InputObject $rawPropertyName -ErrorAction Stop
                    }
                    catch {
                        break
                    }
                    if (-not $container.Names.Add([string]$propertyName)) {
                        throw [InvalidOperationException]::new(
                            "P0-R1 $Label must not contain duplicate JSON property names: $propertyName."
                        )
                    }
                }
            }
        }
        elseif ($character -eq '{') {
            $containers.Add([pscustomobject]@{
                Kind = 'Object'
                Names = [System.Collections.Generic.HashSet[string]]::new(
                    [StringComparer]::Ordinal
                )
            })
        }
        elseif ($character -eq '[') {
            $containers.Add([pscustomobject]@{ Kind = 'Array'; Names = $null })
        }
        elseif (($character -eq '}' -or $character -eq ']') -and
            $containers.Count -gt 0) {
            $containers.RemoveAt($containers.Count - 1)
        }
        $index++
    }
}

function Get-P0R1MeasuredSourceTreeHash {
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot
    )

    $resolvedRoot = (Resolve-Path -LiteralPath $RepositoryRoot -ErrorAction Stop).Path
    $prototypeRoot = Join-Path $resolvedRoot 'prototypes\scatter-rendering'
    if (-not (Test-Path -LiteralPath $prototypeRoot -PathType Container)) {
        throw [InvalidOperationException]::new(
            'P0-R1 measured source root is absent: prototypes/scatter-rendering.'
        )
    }

    $entries = [System.Collections.Generic.SortedDictionary[string, string]]::new(
        [StringComparer]::Ordinal
    )
    foreach ($file in Get-ChildItem -LiteralPath $prototypeRoot -Recurse -File -Force) {
        $relativePath = $file.FullName.Substring($resolvedRoot.Length + 1).Replace('\', '/')
        if ($relativePath -match '(^|/)(bin|obj)/') {
            continue
        }
        $entries.Add(
            $relativePath,
            (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        )
    }

    foreach ($relativePath in @(
        'global.json',
        'docs/reference/scatter-rendering/phase0/display-transform-contract-v1.json',
        'docs/reference/scatter-rendering/phase0/density-raster-contract-v1.json',
        'docs/reference/scatter-rendering/phase0/gmi-raster-contract-v1.json'
    )) {
        $absolutePath = Join-Path $resolvedRoot $relativePath
        if (-not (Test-Path -LiteralPath $absolutePath -PathType Leaf)) {
            throw [InvalidOperationException]::new(
                "P0-R1 measured external build/run input is absent: $relativePath."
            )
        }
        $entries.Add(
            $relativePath,
            (Get-FileHash -LiteralPath $absolutePath -Algorithm SHA256).Hash.ToLowerInvariant()
        )
    }

    $ancestorBuildCandidates = @(
        'Directory.Build.props',
        'Directory.Build.targets',
        'Directory.Packages.props',
        'prototypes/Directory.Build.props',
        'prototypes/Directory.Build.targets',
        'prototypes/Directory.Packages.props'
    )
    foreach ($relativePath in $ancestorBuildCandidates) {
        $absolutePath = Join-Path $resolvedRoot $relativePath
        $value = if (Test-Path -LiteralPath $absolutePath -PathType Leaf) {
            (Get-FileHash -LiteralPath $absolutePath -Algorithm SHA256).Hash.ToLowerInvariant()
        }
        else {
            '<absent>'
        }
        $entries.Add($relativePath, $value)
    }

    $material = [Text.StringBuilder]::new()
    foreach ($entry in $entries.GetEnumerator()) {
        $null = $material.Append($entry.Key).Append('=').Append($entry.Value).Append("`n")
    }

    $encoding = [Text.UTF8Encoding]::new($false)
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $hash = ([BitConverter]::ToString(
            $sha256.ComputeHash($encoding.GetBytes($material.ToString()))
        )).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $sha256.Dispose()
    }

    return [pscustomobject]@{
        Contract = 'sha256(ordinal relative-path=sha256(file) LF; every non-bin/obj prototype file including five tracked ancestor-search terminators, plus global.json and three linked fixtures; six in-repository ancestor candidates use sha256(file) or <absent>)'
        FileCount = @($entries.Values | Where-Object { $_ -cne '<absent>' }).Count
        Sha256 = $hash
    }
}

function Assert-P0R1MeasuredEvidenceSourceContract {
    param(
        [Parameter(Mandatory = $true)][string]$EvidencePath,
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][pscustomobject]$MeasuredSourceTree
    )

    $json = Get-Content -LiteralPath $EvidencePath -Raw -Encoding UTF8
    Assert-P0R1NoDuplicateJsonProperties -Json $json -Label $Label
    try {
        $evidence = $json | ConvertFrom-Json -ErrorAction Stop
    }
    catch {
        throw [InvalidOperationException]::new("P0-R1 $Label must be valid JSON.")
    }
    if ($null -eq $evidence.source) {
        throw [InvalidOperationException]::new("P0-R1 $Label source identity is absent.")
    }

    $requiredSourceFields = @(
        'branch', 'base_commit', 'state', 'measured_source_tree_contract',
        'measured_source_file_count', 'measured_source_tree_sha256'
    )
    $actualSourceFields = @($evidence.source.PSObject.Properties.Name)
    foreach ($fieldName in $requiredSourceFields) {
        if ($actualSourceFields -cnotcontains $fieldName) {
            throw [InvalidOperationException]::new(
                "P0-R1 $Label source identity field is absent: $fieldName."
            )
        }
    }
    if ($actualSourceFields.Count -ne $requiredSourceFields.Count) {
        throw [InvalidOperationException]::new(
            "P0-R1 $Label source identity must contain the exact field set."
        )
    }
    foreach ($fieldName in @(
        'branch', 'base_commit', 'state', 'measured_source_tree_contract',
        'measured_source_tree_sha256'
    )) {
        if ($evidence.source.$fieldName -isnot [string] -or
            [string]::IsNullOrWhiteSpace($evidence.source.$fieldName)) {
            throw [InvalidOperationException]::new(
                "P0-R1 $Label source identity field must be a non-empty JSON string: $fieldName."
            )
        }
    }
    if (-not (Test-P0R1JsonInteger -Value $evidence.source.measured_source_file_count) -or
        [int]$evidence.source.measured_source_file_count -lt 1) {
        throw [InvalidOperationException]::new(
            "P0-R1 $Label measured source file count must be a positive integer."
        )
    }
    if ($evidence.source.branch -cne 'perf/phase0-scatter-prototype' -or
        $evidence.source.base_commit -cne 'f9472c81a2f014a97a0ae186d82e5bc46bab84f9' -or
        $evidence.source.state -cne
            'Batch 4 pre-commit worktree; publication commit is recorded by the next checkpoint') {
        throw [InvalidOperationException]::new(
            "P0-R1 $Label source branch/base/state identity mismatch."
        )
    }
    if ($evidence.source.measured_source_tree_contract -cne $MeasuredSourceTree.Contract -or
        [int]$evidence.source.measured_source_file_count -ne $MeasuredSourceTree.FileCount -or
        $evidence.source.measured_source_tree_sha256 -cne $MeasuredSourceTree.Sha256) {
        throw [InvalidOperationException]::new(
            "P0-R1 $Label measured source tree hash does not match current prototype sources."
        )
    }
    $expectedMeasurementCommand =
        'dotnet run --project tests/AnalogBoard.ScatterRendering.Tests/' +
        'AnalogBoard.ScatterRendering.Tests.csproj --configuration Release ' +
        '-p:Platform=x64 --no-build --no-restore --disable-build-servers --'
    if ($evidence.measurement_command -isnot [string] -or
        $evidence.measurement_command -cne $expectedMeasurementCommand) {
        throw [InvalidOperationException]::new(
            "P0-R1 $Label measurement command does not match the isolated runner contract."
        )
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
    Assert-P0R1NoDuplicateJsonProperties `
        -Json $json `
        -Label 'development observation'
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
    if ($actualFields.Count -ne $requiredFields.Count) {
        throw [InvalidOperationException]::new(
            'P0-R1 development observation must contain only the exact versioned field set.'
        )
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

function Test-P0R1JsonInteger {
    param([object]$Value)

    return $Value -is [sbyte] -or
        $Value -is [byte] -or
        $Value -is [int16] -or
        $Value -is [uint16] -or
        $Value -is [int32] -or
        $Value -is [uint32] -or
        $Value -is [int64] -or
        $Value -is [uint64]
}

function Test-P0R1JsonNumber {
    param([object]$Value)

    return (Test-P0R1JsonInteger -Value $Value) -or
        $Value -is [single] -or
        $Value -is [double] -or
        $Value -is [decimal]
}

function ConvertFrom-P0R1ExactObservationLine {
    param(
        [Parameter(Mandatory = $true)][string[]]$OutputLines,
        [Parameter(Mandatory = $true)][string]$Prefix,
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string[]]$RequiredFields
    )

    $matchingLines = @(
        $OutputLines |
            Where-Object { $_.StartsWith($Prefix, [StringComparison]::Ordinal) }
    )
    if ($matchingLines.Count -ne 1) {
        throw [InvalidOperationException]::new(
            "Test runner output must contain exactly one $Label line; found $($matchingLines.Count)."
        )
    }

    $json = $matchingLines[0].Substring($Prefix.Length)
    Assert-P0R1NoDuplicateJsonProperties -Json $json -Label $Label
    try {
        $value = $json | ConvertFrom-Json -ErrorAction Stop
    }
    catch {
        throw [InvalidOperationException]::new("P0-R1 $Label must be valid JSON.")
    }

    $actualFields = @($value.PSObject.Properties.Name)
    foreach ($fieldName in $RequiredFields) {
        if ($actualFields -cnotcontains $fieldName) {
            throw [InvalidOperationException]::new(
                "P0-R1 $Label field is absent: $fieldName."
            )
        }
    }
    if ($actualFields.Count -ne $RequiredFields.Count) {
        throw [InvalidOperationException]::new(
            "P0-R1 $Label must contain only the exact versioned field set."
        )
    }

    return [pscustomobject]@{
        Json = $json
        Value = $value
    }
}

function Get-P0R1CombinedDevelopmentObservation {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [string[]]$OutputLines
    )

    $requiredFields = @(
        'schema_id',
        'observation_kind',
        'development_only',
        'official_acceptance',
        'may_substitute_official',
        'fixture_id',
        'event_count',
        'scatter_width',
        'scatter_height',
        'gmi_width',
        'gmi_height',
        'selected_gmi_channel',
        'gmi_waveform_count',
        'gmi_sample_count',
        'warmup_iterations',
        'measurement_iterations',
        'combined_iteration_ms_p95',
        'combined_iteration_ms_max',
        'scatter_delivered_update_rate_hz',
        'gmi_delivered_update_rate_hz',
        'gmi_max_update_interval_ms',
        'input_latency_ms_p95',
        'input_latency_ms_max',
        'allocated_bytes_per_iteration',
        'scatter_publish_p99_ms',
        'gmi_publish_p99_ms',
        'scatter_pending_frame_max',
        'scatter_pending_callback_max',
        'gmi_pending_frame_max',
        'gmi_pending_callback_max',
        'scatter_rendered_count',
        'gmi_rendered_count',
        'scatter_coalesced_count',
        'gmi_coalesced_count',
        'input_sentinel_count',
        'dispatcher_priority_contract',
        'scatter_raster_sha256',
        'gmi_raster_sha256',
        'machine'
    )
    $parsed = ConvertFrom-P0R1ExactObservationLine `
        -OutputLines $OutputLines `
        -Prefix 'COMBINED_DEVELOPMENT_OBSERVATION ' `
        -Label 'combined development observation' `
        -RequiredFields $requiredFields
    $observation = $parsed.Value

    foreach ($fieldName in @('development_only', 'official_acceptance', 'may_substitute_official')) {
        if ($observation.$fieldName -isnot [bool]) {
            throw [InvalidOperationException]::new(
                "P0-R1 combined observation field must be a JSON boolean: $fieldName."
            )
        }
    }
    if (-not $observation.development_only -or
        $observation.official_acceptance -or
        $observation.may_substitute_official) {
        throw [InvalidOperationException]::new(
            'P0-R1 combined observation must remain development-only, non-official, and non-substituting.'
        )
    }

    $expectedStrings = [ordered]@{
        schema_id = 'analogboard.scatter-rendering.combined-development-observation.v1'
        observation_kind = 'hard-combined-compatible-pc-smoke'
        fixture_id = 'AB-P0-R1-HARD-COMBINED-v1'
        selected_gmi_channel = 'fsGMI'
        dispatcher_priority_contract = 'Input_above_Background'
        scatter_raster_sha256 = '255bf3f549baa92d87a65111c37bed815f0b74c3452a387c6a1cc6d168b61780'
        gmi_raster_sha256 = 'd02e42158d6b89d39b342a307557dcc3013f908f49e6e7f3d4f43edbb4393c88'
    }
    foreach ($fieldName in $expectedStrings.Keys) {
        if ($observation.$fieldName -isnot [string] -or
            $observation.$fieldName -cne $expectedStrings[$fieldName]) {
            throw [InvalidOperationException]::new(
                "P0-R1 combined observation identity mismatch: $fieldName."
            )
        }
    }
    if ($observation.machine -isnot [string] -or
        [string]::IsNullOrWhiteSpace($observation.machine)) {
        throw [InvalidOperationException]::new(
            'P0-R1 combined observation machine identity must be a non-empty JSON string.'
        )
    }

    $integerFields = @(
        'event_count', 'scatter_width', 'scatter_height', 'gmi_width', 'gmi_height',
        'gmi_waveform_count', 'gmi_sample_count', 'warmup_iterations',
        'measurement_iterations', 'allocated_bytes_per_iteration',
        'scatter_pending_frame_max', 'scatter_pending_callback_max',
        'gmi_pending_frame_max', 'gmi_pending_callback_max',
        'scatter_rendered_count', 'gmi_rendered_count',
        'scatter_coalesced_count', 'gmi_coalesced_count', 'input_sentinel_count'
    )
    foreach ($fieldName in $integerFields) {
        if (-not (Test-P0R1JsonInteger -Value $observation.$fieldName)) {
            throw [InvalidOperationException]::new(
                "P0-R1 combined observation field must be an integer JSON number: $fieldName."
            )
        }
        if ([long]$observation.$fieldName -lt 0) {
            throw [InvalidOperationException]::new(
                "P0-R1 combined observation integer field must be non-negative: $fieldName."
            )
        }
    }
    if ([int]$observation.event_count -ne 100001 -or
        [int]$observation.scatter_width -ne 512 -or
        [int]$observation.scatter_height -ne 512 -or
        [int]$observation.gmi_width -ne 512 -or
        [int]$observation.gmi_height -ne 512 -or
        [int]$observation.gmi_waveform_count -ne 100 -or
        [int]$observation.gmi_sample_count -ne 2400) {
        throw [InvalidOperationException]::new(
            'P0-R1 combined observation fixture shape mismatch.'
        )
    }
    if ([int]$observation.warmup_iterations -ne 3 -or
        [int]$observation.measurement_iterations -ne 10 -or
        [long]$observation.scatter_rendered_count -ne 13 -or
        [long]$observation.gmi_rendered_count -ne 13 -or
        [int]$observation.input_sentinel_count -ne 10 -or
        [long]$observation.scatter_coalesced_count -ne 0 -or
        [long]$observation.gmi_coalesced_count -ne 0) {
        throw [InvalidOperationException]::new(
            'P0-R1 combined observation fixed iteration counts are inconsistent.'
        )
    }
    if ([int]$observation.scatter_pending_frame_max -ne 1 -or
        [int]$observation.scatter_pending_callback_max -ne 1 -or
        [int]$observation.gmi_pending_frame_max -ne 1 -or
        [int]$observation.gmi_pending_callback_max -ne 1) {
        throw [InvalidOperationException]::new(
            'P0-R1 combined observation pending maxima must equal one per feed.'
        )
    }

    $numberFields = @(
        'combined_iteration_ms_p95', 'combined_iteration_ms_max',
        'scatter_delivered_update_rate_hz', 'gmi_delivered_update_rate_hz',
        'gmi_max_update_interval_ms', 'input_latency_ms_p95', 'input_latency_ms_max',
        'scatter_publish_p99_ms', 'gmi_publish_p99_ms'
    )
    foreach ($fieldName in $numberFields) {
        if (-not (Test-P0R1JsonNumber -Value $observation.$fieldName)) {
            throw [InvalidOperationException]::new(
                "P0-R1 combined observation field must be a JSON number: $fieldName."
            )
        }
        $number = [double]$observation.$fieldName
        if ([double]::IsNaN($number) -or [double]::IsInfinity($number) -or $number -lt 0) {
            throw [InvalidOperationException]::new(
                "P0-R1 combined observation metric must be finite and non-negative: $fieldName."
            )
        }
    }
    if ([double]$observation.scatter_delivered_update_rate_hz -le 0 -or
        [double]$observation.gmi_delivered_update_rate_hz -le 0) {
        throw [InvalidOperationException]::new(
            'P0-R1 combined observation delivered update rates must be positive.'
        )
    }
    if ([double]$observation.combined_iteration_ms_max -lt
            [double]$observation.combined_iteration_ms_p95 -or
        [double]$observation.input_latency_ms_max -lt
            [double]$observation.input_latency_ms_p95) {
        throw [InvalidOperationException]::new(
            'P0-R1 combined observation maxima must be greater than or equal to p95.'
        )
    }

    return $parsed.Json
}

function Get-P0R1HeadroomDevelopmentObservation {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [string[]]$OutputLines
    )

    $requiredFields = @(
        'schema_id', 'observation_kind', 'development_only', 'official_acceptance',
        'may_substitute_hard_gate', 'fixture_id', 'event_count', 'width', 'height',
        'tile_count', 'scatter_tile_count', 'gmi_tile_count', 'selected_gmi_channel',
        'gmi_waveform_count', 'gmi_sample_count', 'warmup_iterations',
        'measurement_iterations', 'three_tile_iteration_ms_p95',
        'three_tile_iteration_ms_max', 'allocated_bytes_per_iteration',
        'scatter_a_raster_sha256', 'scatter_b_raster_sha256', 'gmi_raster_sha256',
        'machine'
    )
    $parsed = ConvertFrom-P0R1ExactObservationLine `
        -OutputLines $OutputLines `
        -Prefix 'HEADROOM_DEVELOPMENT_OBSERVATION ' `
        -Label 'headroom development observation' `
        -RequiredFields $requiredFields
    $observation = $parsed.Value

    foreach ($fieldName in @('development_only', 'official_acceptance', 'may_substitute_hard_gate')) {
        if ($observation.$fieldName -isnot [bool]) {
            throw [InvalidOperationException]::new(
                "P0-R1 headroom observation field must be a JSON boolean: $fieldName."
            )
        }
    }
    if (-not $observation.development_only -or
        $observation.official_acceptance -or
        $observation.may_substitute_hard_gate) {
        throw [InvalidOperationException]::new(
            'P0-R1 headroom observation must remain development-only, non-official, and non-substituting.'
        )
    }

    $expectedStrings = [ordered]@{
        schema_id = 'analogboard.scatter-rendering.headroom-development-observation.v1'
        observation_kind = 'three-tile-headroom-compatible-pc'
        fixture_id = 'AB-P0-R1-HEADROOM-v1'
        selected_gmi_channel = 'fsGMI'
        scatter_a_raster_sha256 = '0ffd755ea97e90ab1f8a186cc20e58f30378196be0d6ba3b1d068cb5b59afa45'
        scatter_b_raster_sha256 = '9c1c255558b089f96eeb6336c55b0fbe254b05f47ea815c6e230ca05e8b31b17'
        gmi_raster_sha256 = '417e8cf7f21ab09680e98a2aa6b7aabd0479a3457a52e55f15b6b3d41bd1156b'
    }
    foreach ($fieldName in $expectedStrings.Keys) {
        if ($observation.$fieldName -isnot [string] -or
            $observation.$fieldName -cne $expectedStrings[$fieldName]) {
            throw [InvalidOperationException]::new(
                "P0-R1 headroom observation identity mismatch: $fieldName."
            )
        }
    }
    if ($observation.machine -isnot [string] -or
        [string]::IsNullOrWhiteSpace($observation.machine)) {
        throw [InvalidOperationException]::new(
            'P0-R1 headroom observation machine identity must be a non-empty JSON string.'
        )
    }

    $integerFields = @(
        'event_count', 'width', 'height', 'tile_count', 'scatter_tile_count',
        'gmi_tile_count', 'gmi_waveform_count', 'gmi_sample_count',
        'warmup_iterations', 'measurement_iterations', 'allocated_bytes_per_iteration'
    )
    foreach ($fieldName in $integerFields) {
        if (-not (Test-P0R1JsonInteger -Value $observation.$fieldName)) {
            throw [InvalidOperationException]::new(
                "P0-R1 headroom observation field must be an integer JSON number: $fieldName."
            )
        }
        if ([long]$observation.$fieldName -lt 0) {
            throw [InvalidOperationException]::new(
                "P0-R1 headroom observation integer field must be non-negative: $fieldName."
            )
        }
    }
    if ([int]$observation.event_count -ne 131072 -or
        [int]$observation.width -ne 1024 -or
        [int]$observation.height -ne 1024 -or
        [int]$observation.tile_count -ne 3 -or
        [int]$observation.scatter_tile_count -ne 2 -or
        [int]$observation.gmi_tile_count -ne 1) {
        throw [InvalidOperationException]::new(
            'P0-R1 headroom observation must use 131072 events, 1024-square output, and exactly three tiles.'
        )
    }
    if ([int]$observation.gmi_waveform_count -ne 100 -or
        [int]$observation.gmi_sample_count -ne 2400 -or
        [int]$observation.warmup_iterations -ne 1 -or
        [int]$observation.measurement_iterations -ne 3) {
        throw [InvalidOperationException]::new(
            'P0-R1 headroom observation bounded GMI or iteration identity mismatch.'
        )
    }
    foreach ($fieldName in @('three_tile_iteration_ms_p95', 'three_tile_iteration_ms_max')) {
        if (-not (Test-P0R1JsonNumber -Value $observation.$fieldName)) {
            throw [InvalidOperationException]::new(
                "P0-R1 headroom observation field must be a JSON number: $fieldName."
            )
        }
        $number = [double]$observation.$fieldName
        if ([double]::IsNaN($number) -or [double]::IsInfinity($number) -or $number -lt 0) {
            throw [InvalidOperationException]::new(
                "P0-R1 headroom observation metric must be finite and non-negative: $fieldName."
            )
        }
    }
    if ([double]$observation.three_tile_iteration_ms_max -lt
        [double]$observation.three_tile_iteration_ms_p95) {
        throw [InvalidOperationException]::new(
            'P0-R1 headroom observation maximum must be greater than or equal to p95.'
        )
    }

    return $parsed.Json
}

function Assert-P0R1RendererDecisionContract {
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot
    )

    $resolvedRoot = (Resolve-Path -LiteralPath $RepositoryRoot -ErrorAction Stop).Path
    $relativeDecisionPath = 'docs/reference/scatter-rendering/phase0/renderer-decision-v1.json'
    $decisionPath = Join-Path $resolvedRoot $relativeDecisionPath
    if (-not (Test-Path -LiteralPath $decisionPath -PathType Leaf)) {
        throw [InvalidOperationException]::new(
            "P0-R1 renderer decision file is absent: $relativeDecisionPath."
        )
    }
    $decisionJson = Get-Content -LiteralPath $decisionPath -Raw -Encoding UTF8
    Assert-P0R1NoDuplicateJsonProperties `
        -Json $decisionJson `
        -Label 'renderer decision'
    try {
        $decision = $decisionJson | ConvertFrom-Json -ErrorAction Stop
    }
    catch {
        throw [InvalidOperationException]::new('P0-R1 renderer decision must be valid JSON.')
    }

    $requiredFields = @(
        'schema_id', 'decision_id', 'status', 'scope', 'development_only',
        'official_acceptance', 'production_throughput_guarantee',
        'selected_candidate_id', 'dependency_contract', 'selected_path',
        'candidates', 'evidence', 'stop_conditions', 'residual_limits'
    )
    $actualFields = @($decision.PSObject.Properties.Name)
    foreach ($fieldName in $requiredFields) {
        if ($actualFields -cnotcontains $fieldName) {
            throw [InvalidOperationException]::new(
                "P0-R1 renderer decision field is absent: $fieldName."
            )
        }
    }
    if ($actualFields.Count -ne $requiredFields.Count) {
        throw [InvalidOperationException]::new(
            'P0-R1 renderer decision must contain only the exact versioned field set.'
        )
    }

    $expectedStrings = [ordered]@{
        schema_id = 'analogboard.scatter-rendering.renderer-decision.v1'
        decision_id = 'P0-R1-RENDERER-v1'
        status = 'selected_for_phase_checkpoint'
        scope = 'P0-R1 bounded visualization prototype only'
        selected_candidate_id = 'wpf-writeablebitmap-preallocated'
    }
    foreach ($fieldName in $expectedStrings.Keys) {
        if ($decision.$fieldName -isnot [string] -or
            $decision.$fieldName -cne $expectedStrings[$fieldName]) {
            throw [InvalidOperationException]::new(
                "P0-R1 renderer decision identity mismatch: $fieldName."
            )
        }
    }
    foreach ($fieldName in @(
        'development_only', 'official_acceptance', 'production_throughput_guarantee'
    )) {
        if ($decision.$fieldName -isnot [bool]) {
            throw [InvalidOperationException]::new(
                "P0-R1 renderer decision field must be a JSON boolean: $fieldName."
            )
        }
    }
    if (-not $decision.development_only -or
        $decision.official_acceptance -or
        $decision.production_throughput_guarantee) {
        throw [InvalidOperationException]::new(
            'P0-R1 renderer decision must remain development-only, non-official, and not a production throughput guarantee.'
        )
    }

    $dependency = $decision.dependency_contract
    $dependencyFields = @(
        'framework', 'external_nuget_package_count', 'manifest_path',
        'manifest_sha256', 'license_name', 'license_sha256',
        'third_party_notices_sha256'
    )
    if (@($dependency.PSObject.Properties.Name).Count -ne $dependencyFields.Count) {
        throw [InvalidOperationException]::new(
            'P0-R1 renderer dependency decision must contain the exact field set.'
        )
    }
    foreach ($fieldName in $dependencyFields) {
        if (@($dependency.PSObject.Properties.Name) -cnotcontains $fieldName) {
            throw [InvalidOperationException]::new(
                "P0-R1 renderer dependency decision field is absent: $fieldName."
            )
        }
    }
    if (-not (Test-P0R1JsonInteger -Value $dependency.external_nuget_package_count) -or
        [int]$dependency.external_nuget_package_count -ne 0) {
        throw [InvalidOperationException]::new(
            'P0-R1 renderer decision external NuGet package count must be integer zero.'
        )
    }
    $expectedDependencyStrings = [ordered]@{
        framework = 'WPF in Microsoft.WindowsDesktop.App 10.0.10'
        manifest_path = 'docs/dependencies/analogboard-p0-r1-dependencies.json'
        manifest_sha256 = 'f1baebfb5cd4334563f7ab6b42ed3f6bf867e8d5f6199de62aa6effbeb6af534'
        license_name = 'MIT'
        license_sha256 = '7f6839a61ce892b79c6549e2dc5a81fdbd240a0b260f8881216b45b7fda8b45d'
        third_party_notices_sha256 = 'deb4427a295e1ed474b0d81c5a0d972c1b550b9a715cda939cdfa9236b1b418f'
    }
    foreach ($fieldName in $expectedDependencyStrings.Keys) {
        if ($dependency.$fieldName -isnot [string] -or
            $dependency.$fieldName -cne $expectedDependencyStrings[$fieldName]) {
            throw [InvalidOperationException]::new(
                "P0-R1 renderer dependency decision identity mismatch: $fieldName."
            )
        }
    }

    $selectedPathFields = @(
        'framework_surface', 'pixel_contract', 'publication_api', 'rasterization',
        'composition', 'fallback', 'maintenance_boundary', 'phase2_boundary'
    )
    if (@($decision.selected_path.PSObject.Properties.Name).Count -ne $selectedPathFields.Count) {
        throw [InvalidOperationException]::new(
            'P0-R1 selected renderer path must contain the exact field set.'
        )
    }
    foreach ($fieldName in $selectedPathFields) {
        if (@($decision.selected_path.PSObject.Properties.Name) -cnotcontains $fieldName -or
            $decision.selected_path.$fieldName -isnot [string] -or
            [string]::IsNullOrWhiteSpace($decision.selected_path.$fieldName)) {
            throw [InvalidOperationException]::new(
                "P0-R1 selected renderer path field is absent or empty: $fieldName."
            )
        }
    }
    if ($decision.selected_path.framework_surface -cne
            'System.Windows.Media.Imaging.WriteableBitmap' -or
        $decision.selected_path.pixel_contract -cne 'preallocated opaque BGRA32' -or
        $decision.selected_path.publication_api -cne 'WritePixels') {
        throw [InvalidOperationException]::new(
            'P0-R1 selected renderer path does not match the frozen WriteableBitmap contract.'
        )
    }

    $candidateFields = @(
        'candidate_id', 'disposition', 'correctness', 'performance',
        'dependency_license', 'cpu_gpu_behavior', 'fallback', 'maintenance',
        'phase2_portability'
    )
    $expectedCandidates = [ordered]@{
        'wpf-writeablebitmap-preallocated' = 'selected_for_phase_checkpoint'
        'wpf-writeablebitmap-direct-backbuffer' = 'rejected_for_phase0'
        'wpf-vector-or-rendertargetbitmap-events' = 'rejected_contract_mismatch'
    }
    if ($decision.candidates -isnot [array] -or
        @($decision.candidates).Count -ne $expectedCandidates.Count) {
        throw [InvalidOperationException]::new(
            'P0-R1 renderer decision must contain exactly three candidate evaluations.'
        )
    }
    for ($index = 0; $index -lt $expectedCandidates.Count; $index++) {
        $candidate = @($decision.candidates)[$index]
        if (@($candidate.PSObject.Properties.Name).Count -ne $candidateFields.Count) {
            throw [InvalidOperationException]::new(
                "P0-R1 renderer candidate must contain the exact field set at index $index."
            )
        }
        foreach ($fieldName in $candidateFields) {
            if (@($candidate.PSObject.Properties.Name) -cnotcontains $fieldName -or
                $candidate.$fieldName -isnot [string] -or
                [string]::IsNullOrWhiteSpace($candidate.$fieldName)) {
                throw [InvalidOperationException]::new(
                    "P0-R1 renderer candidate field is absent or empty at index $index`: $fieldName."
                )
            }
        }
        $expectedId = @($expectedCandidates.Keys)[$index]
        if ($candidate.candidate_id -cne $expectedId -or
            $candidate.disposition -cne $expectedCandidates[$expectedId]) {
            throw [InvalidOperationException]::new(
                "P0-R1 renderer candidate identity or disposition mismatch at index $index."
            )
        }
    }

    $expectedEvidence = [ordered]@{
        density_raster_contract_path = 'docs/reference/scatter-rendering/phase0/density-raster-contract-v1.json'
        density_raster_contract_sha256 = 'c6015c919cc79e7c746f4b6c0d8b42672e4165cfc0253663bbfe89e04121cc33'
        gmi_raster_contract_path = 'docs/reference/scatter-rendering/phase0/gmi-raster-contract-v1.json'
        gmi_raster_contract_sha256 = '874fe6b9ea252f7063200655d584e549b5a2fc6e3587693b1b23b5041a52aa08'
        combined_development_path = 'docs/reference/scatter-rendering/phase0/batch4-combined-development-observation.json'
        combined_development_sha256 = '71bfdbe672fef7d56cd72da9af5e313751fe08e6d592bde4ab5080ef5ef0c180'
        headroom_development_path = 'docs/reference/scatter-rendering/phase0/batch4-headroom-development-observation.json'
        headroom_development_sha256 = '991a0aaa5411ee05dc304d9b381abae30c22ad8f302a2225301fec5b57920f69'
    }
    if (@($decision.evidence.PSObject.Properties.Name).Count -ne $expectedEvidence.Count) {
        throw [InvalidOperationException]::new(
            'P0-R1 renderer decision evidence must contain the exact field set.'
        )
    }
    foreach ($fieldName in $expectedEvidence.Keys) {
        if (@($decision.evidence.PSObject.Properties.Name) -cnotcontains $fieldName -or
            $decision.evidence.$fieldName -isnot [string] -or
            $decision.evidence.$fieldName -cne $expectedEvidence[$fieldName]) {
            throw [InvalidOperationException]::new(
                "P0-R1 renderer decision evidence mismatch: $fieldName."
            )
        }
    }

    $hashedFiles = @(
        @{ Path = $dependency.manifest_path; Hash = $dependency.manifest_sha256 },
        @{ Path = $decision.evidence.density_raster_contract_path; Hash = $decision.evidence.density_raster_contract_sha256 },
        @{ Path = $decision.evidence.gmi_raster_contract_path; Hash = $decision.evidence.gmi_raster_contract_sha256 },
        @{ Path = $decision.evidence.combined_development_path; Hash = $decision.evidence.combined_development_sha256 },
        @{ Path = $decision.evidence.headroom_development_path; Hash = $decision.evidence.headroom_development_sha256 }
    )
    foreach ($item in $hashedFiles) {
        $path = Join-Path $resolvedRoot $item.Path
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw [InvalidOperationException]::new(
                "P0-R1 renderer decision evidence file is absent: $($item.Path)."
            )
        }
        $actualHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -cne $item.Hash) {
            throw [InvalidOperationException]::new(
                "P0-R1 renderer decision evidence hash mismatch: $($item.Path)."
            )
        }
    }

    $measuredSourceTree = Get-P0R1MeasuredSourceTreeHash -RepositoryRoot $resolvedRoot
    Assert-P0R1MeasuredEvidenceSourceContract `
        -EvidencePath (Join-Path $resolvedRoot $decision.evidence.combined_development_path) `
        -Label 'combined development evidence' `
        -MeasuredSourceTree $measuredSourceTree
    Assert-P0R1MeasuredEvidenceSourceContract `
        -EvidencePath (Join-Path $resolvedRoot $decision.evidence.headroom_development_path) `
        -Label 'headroom development evidence' `
        -MeasuredSourceTree $measuredSourceTree

    foreach ($fieldName in @('stop_conditions', 'residual_limits')) {
        $items = @($decision.$fieldName)
        if ($decision.$fieldName -isnot [array] -or $items.Count -ne 3) {
            throw [InvalidOperationException]::new(
                "P0-R1 renderer decision $fieldName must contain exactly three entries."
            )
        }
        foreach ($item in $items) {
            if ($item -isnot [string] -or [string]::IsNullOrWhiteSpace($item)) {
                throw [InvalidOperationException]::new(
                    "P0-R1 renderer decision $fieldName entries must be non-empty JSON strings."
                )
            }
        }
    }

    return [pscustomobject]@{
        DecisionId = $decision.decision_id
        SelectedCandidateId = $decision.selected_candidate_id
        Status = $decision.status
        Path = $relativeDecisionPath
        Sha256 = (Get-FileHash -LiteralPath $decisionPath -Algorithm SHA256).Hash.ToLowerInvariant()
        MeasuredSourceTreeSha256 = $measuredSourceTree.Sha256
    }
}

function Invoke-P0R1SanitizedDotNet {
    param(
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory
    )

    $resolvedWorkingDirectory = (Resolve-Path -LiteralPath $WorkingDirectory -ErrorAction Stop).Path
    if (-not (Test-Path -LiteralPath $script:ExpectedDotNetExecutablePath -PathType Leaf)) {
        throw [IO.FileNotFoundException]::new(
            "Required dotnet executable is absent: $($script:ExpectedDotNetExecutablePath)",
            $script:ExpectedDotNetExecutablePath
        )
    }

    $originalEnvironment = [Environment]::GetEnvironmentVariables([EnvironmentVariableTarget]::Process)
    $sanitizedEnvironment = Get-P0R1SanitizedDotNetEnvironment -Environment $originalEnvironment
    try {
        foreach ($key in @($originalEnvironment.Keys)) {
            [Environment]::SetEnvironmentVariable(
                [string]$key,
                $null,
                [EnvironmentVariableTarget]::Process
            )
        }
        foreach ($entry in $sanitizedEnvironment.GetEnumerator()) {
            [Environment]::SetEnvironmentVariable(
                $entry.Key,
                $entry.Value,
                [EnvironmentVariableTarget]::Process
            )
        }

        Push-Location -LiteralPath $resolvedWorkingDirectory
        try {
            $output = @(
                & $script:ExpectedDotNetExecutablePath @Arguments 2>&1 |
                    ForEach-Object { $_.ToString() }
            )
            $exitCode = $LASTEXITCODE
        }
        finally {
            Pop-Location
        }
    }
    finally {
        $currentEnvironment = [Environment]::GetEnvironmentVariables([EnvironmentVariableTarget]::Process)
        foreach ($key in @($currentEnvironment.Keys)) {
            [Environment]::SetEnvironmentVariable(
                [string]$key,
                $null,
                [EnvironmentVariableTarget]::Process
            )
        }
        foreach ($key in @($originalEnvironment.Keys)) {
            [Environment]::SetEnvironmentVariable(
                [string]$key,
                [string]$originalEnvironment[$key],
                [EnvironmentVariableTarget]::Process
            )
        }
    }
    return [pscustomobject]@{
        ExitCode = $exitCode
        Output = $output
    }
}

function Invoke-DefaultP0R1DotNet {
    param(
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory
    )

    return Invoke-P0R1SanitizedDotNet -Arguments $Arguments -WorkingDirectory $WorkingDirectory
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

    $null = Clear-P0R1GeneratedBuildRoots -PrototypeRoot $resolvedPrototypeRoot

    $restoreArguments = @(
        'restore',
        $state.SolutionPath,
        '--configfile', $restoreContract.NuGetConfigPath,
        '--no-cache',
        '--force-evaluate',
        '--disable-parallel',
        '--disable-build-servers'
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
        '--no-restore',
        '--disable-build-servers'
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
        '--disable-build-servers',
        '--'
    )
    $runnerOutput = Invoke-CheckedP0R1DotNet `
        -Label 'dotnet test runner' `
        -Arguments $runnerArguments `
        -WorkingDirectory $resolvedPrototypeRoot `
        -DotNetInvoker $DotNetInvoker
    $testSummary = Get-P0R1TestSummary -OutputLines $runnerOutput
    $developmentObservation = Get-P0R1DevelopmentObservation -OutputLines $runnerOutput
    $combinedDevelopmentObservation = Get-P0R1CombinedDevelopmentObservation -OutputLines $runnerOutput
    $headroomDevelopmentObservation = Get-P0R1HeadroomDevelopmentObservation -OutputLines $runnerOutput

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
        CombinedDevelopmentObservation = $combinedDevelopmentObservation
        HeadroomDevelopmentObservation = $headroomDevelopmentObservation
    }
}

Export-ModuleMember -Function @(
    'Get-P0R1SanitizedDotNetEnvironment',
    'Invoke-P0R1SanitizedDotNet',
    'Assert-P0R1VerificationSelection',
    'Assert-P0R1ToolchainOutput',
    'Assert-P0R1RepositoryDependencyContract',
    'Assert-P0R1DependencyManifestContract',
    'Assert-P0R1RestoreIsolation',
    'Clear-P0R1GeneratedBuildRoots',
    'Get-P0R1PrototypeState',
    'Get-P0R1TestSummary',
    'Get-P0R1MeasuredSourceTreeHash',
    'Get-P0R1DevelopmentObservation',
    'Get-P0R1CombinedDevelopmentObservation',
    'Get-P0R1HeadroomDevelopmentObservation',
    'Assert-P0R1RendererDecisionContract',
    'Invoke-P0R1FocusedVerification'
)
