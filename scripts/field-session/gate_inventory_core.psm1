Set-StrictMode -Version Latest

function ConvertTo-GateObjectArray {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [AllowNull()]
        [object[]]$InputObject
    )

    Write-Output -NoEnumerate @($InputObject)
}

function Get-GateExpectedDriverVersion {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('GateB', 'GateC', 'GateCRollback')]
        [string]$Stage
    )

    switch ($Stage) {
        'GateB' { return '1.2.3.20' }
        'GateC' { return '1.3.0.4' }
        'GateCRollback' { return '1.2.3.20' }
    }
}

function Assert-GateBuildId {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$CurrentBuildId,

        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$ExpectedBuildId
    )

    $current = $CurrentBuildId.Trim()
    $expected = $ExpectedBuildId.Trim()
    if ([string]::IsNullOrWhiteSpace($expected)) {
        throw 'ExpectedBuildId must not be empty.'
    }
    if ($current -ne $expected) {
        throw "Build ID mismatch. Expected '$expected', got '$current'."
    }

    return $current
}

function Assert-GateInventoryContext {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('GateB', 'GateC', 'GateCRollback')]
        [string]$Stage,

        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$CurrentDriverVersion,

        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$SampleId,

        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$UsbPortNote,

        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$CableConfiguration
    )

    if ([string]::IsNullOrWhiteSpace($SampleId)) {
        throw 'SampleId is required and must identify the sample or batch used for this gate block.'
    }
    if ([string]::IsNullOrWhiteSpace($UsbPortNote)) {
        throw 'UsbPortNote is required and must identify the physical PC port.'
    }
    if ($CableConfiguration.Trim() -ne 'Direct') {
        throw 'CableConfiguration must be Direct. Hubs, extension sections, and SW-SEG-01 are not allowed.'
    }

    $expectedDriverVersion = Get-GateExpectedDriverVersion -Stage $Stage
    if ($CurrentDriverVersion.Trim() -ne $expectedDriverVersion) {
        throw "Driver version mismatch for $Stage. Expected '$expectedDriverVersion', got '$CurrentDriverVersion'."
    }

    [pscustomobject]@{
        Stage = $Stage
        ExpectedDriverVersion = $expectedDriverVersion
        CurrentDriverVersion = $CurrentDriverVersion.Trim()
        SampleId = $SampleId.Trim()
        UsbPortNote = $UsbPortNote.Trim()
        CableConfiguration = 'Direct'
    }
}

function Assert-GateRequiredRuntimeFiles {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PackageRoot,

        [Parameter(Mandatory = $true)]
        [string]$BuildManifestPath
    )

    $resolvedPackageRoot = (Resolve-Path -LiteralPath $PackageRoot -ErrorAction Stop).Path
    $resolvedManifestPath = (Resolve-Path -LiteralPath $BuildManifestPath -ErrorAction Stop).Path
    $manifest = Get-Content -LiteralPath $resolvedManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $manifestProperties = @($manifest.PSObject.Properties.Name)
    if ($manifestProperties -notcontains 'required_runtime_files') {
        throw "Build manifest contains no required_runtime_files contract: $resolvedManifestPath"
    }

    $runtimeFiles = @($manifest.required_runtime_files)
    if ($runtimeFiles.Count -eq 0) {
        throw "Build manifest contains no required runtime files: $resolvedManifestPath"
    }

    foreach ($runtimeFile in $runtimeFiles) {
        $properties = @($runtimeFile.PSObject.Properties.Name)
        $relativePath = [string]$runtimeFile.path
        if ([string]::IsNullOrWhiteSpace($relativePath)) {
            throw "Required runtime file path must not be empty: $resolvedManifestPath"
        }
        if ($properties -contains 'sha256') {
            throw "Required runtime file must not declare sha256: $relativePath"
        }
        if ($runtimeFile.mutable -ne $true -or [string]$runtimeFile.validation -ne 'existence_only') {
            throw "Required runtime file must be mutable with existence_only validation: $relativePath"
        }
        if ([IO.Path]::IsPathRooted($relativePath) -or $relativePath -match '(^|[\\/])\.\.([\\/]|$)') {
            throw "Required runtime file path must remain inside the package: $relativePath"
        }

        $runtimePath = Join-Path $resolvedPackageRoot ($relativePath -replace '/', [IO.Path]::DirectorySeparatorChar)
        if (-not (Test-Path -LiteralPath $runtimePath -PathType Leaf)) {
            throw "Required runtime file is missing: $relativePath"
        }
    }
}

function Test-GatePackageArtifacts {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PackageRoot,

        [Parameter(Mandatory = $true)]
        [string]$BuildManifestPath
    )

    $resolvedPackageRoot = (Resolve-Path -LiteralPath $PackageRoot -ErrorAction Stop).Path
    $resolvedManifestPath = (Resolve-Path -LiteralPath $BuildManifestPath -ErrorAction Stop).Path
    $manifest = Get-Content -LiteralPath $resolvedManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json

    if (-not $manifest.artifacts -or @($manifest.artifacts).Count -eq 0) {
        throw "Build manifest contains no artifacts: $resolvedManifestPath"
    }

    $null = Assert-GateRequiredRuntimeFiles -PackageRoot $resolvedPackageRoot -BuildManifestPath $resolvedManifestPath

    $results = @()
    foreach ($artifact in @($manifest.artifacts)) {
        $relativePath = [string]$artifact.path
        $expectedSha256 = ([string]$artifact.sha256).ToLowerInvariant()
        $artifactPath = Join-Path $resolvedPackageRoot ($relativePath -replace '/', [IO.Path]::DirectorySeparatorChar)
        if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
            throw "Package artifact is missing: $relativePath"
        }

        $file = Get-Item -LiteralPath $artifactPath
        $actualSha256 = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash.ToLowerInvariant()
        $matches = $actualSha256 -eq $expectedSha256
        $results += [pscustomobject]@{
            Path = $relativePath
            Bytes = [long]$file.Length
            LastWriteTimeUtc = $file.LastWriteTimeUtc.ToString('o')
            ExpectedSha256 = $expectedSha256
            ActualSha256 = $actualSha256
            Matches = $matches
        }
    }

    $mismatches = @($results | Where-Object { -not $_.Matches })
    if ($mismatches.Count -gt 0) {
        $paths = @($mismatches | ForEach-Object { $_.Path }) -join ', '
        throw "Package artifact hash mismatch: $paths"
    }

    return $results
}

Export-ModuleMember -Function @(
    'ConvertTo-GateObjectArray',
    'Get-GateExpectedDriverVersion',
    'Assert-GateBuildId',
    'Assert-GateInventoryContext',
    'Assert-GateRequiredRuntimeFiles',
    'Test-GatePackageArtifacts'
)
