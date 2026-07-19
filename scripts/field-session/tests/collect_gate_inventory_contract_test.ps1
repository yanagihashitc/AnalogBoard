$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$FieldScriptRoot = Split-Path -Parent $ScriptDir
$ModulePath = Join-Path $FieldScriptRoot 'gate_inventory_core.psm1'
$CollectorPath = Join-Path $FieldScriptRoot 'collect_gate_inventory.ps1'
$DfxOffPath = Join-Path $FieldScriptRoot 'dfx_off.ps1'
$FixtureRoot = Join-Path $ScriptDir 'fixtures\gate-package'
$ExpectedJudgmentBuildId = 'r7-driver-telemetry-graceful-stop-20260716T1314JST'

if (-not (Test-Path -LiteralPath $ModulePath -PathType Leaf)) {
    throw "RED: inventory core module is not implemented: $ModulePath"
}

Import-Module $ModulePath -Force

$collectorSource = Get-Content -LiteralPath $CollectorPath -Raw -Encoding UTF8
if ($collectorSource -notmatch [regex]::Escape("`$ExpectedBuildId = '$ExpectedJudgmentBuildId'")) {
    throw "The canonical collector must pin the current judgment build: $ExpectedJudgmentBuildId"
}

$dfxOffSource = Get-Content -LiteralPath $DfxOffPath -Raw -Encoding UTF8
if ($dfxOffSource -match 'Unplug and replug') {
    throw 'DFX restart failure must stop without instructing the operator to unplug USB.'
}
if ($dfxOffSource -notmatch 'exit 2') {
    throw 'DFX restart failure must return a non-zero stop code.'
}

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

function Assert-Throws {
    param(
        [Parameter(Mandatory = $true)][scriptblock]$Action,
        [Parameter(Mandatory = $true)][string]$ExpectedMessagePattern
    )

    try {
        & $Action
    }
    catch {
        if ($_.Exception.Message -notlike $ExpectedMessagePattern) {
            throw "Unexpected error. Expected '$ExpectedMessagePattern', got '$($_.Exception.Message)'."
        }
        return
    }

    throw "Expected an exception matching '$ExpectedMessagePattern'."
}

$goodManifest = Join-Path $FixtureRoot 'manifest\build_manifest.json'
$badManifest = Join-Path $FixtureRoot 'manifest\build_manifest_bad_hash.json'
$missingRuntimeFileManifest = Join-Path $FixtureRoot 'manifest\build_manifest_missing_runtime_file.json'

$oneDevice = [pscustomobject]@{ InstanceId = 'USB\VID_04B4&PID_FFF3\fixture-one' }
$singleCandidates = ConvertTo-GateObjectArray -InputObject @($oneDevice)
Assert-Equal -Actual $singleCandidates.GetType().FullName -Expected 'System.Object[]' -Message 'A single PnP result must retain array shape'
Assert-Equal -Actual $singleCandidates.Count -Expected 1 -Message 'A single PnP result must have Count=1'
Assert-Equal -Actual $singleCandidates[0].InstanceId -Expected $oneDevice.InstanceId -Message 'The single PnP result must remain addressable by index'

$noCandidates = ConvertTo-GateObjectArray -InputObject @()
Assert-Equal -Actual $noCandidates.GetType().FullName -Expected 'System.Object[]' -Message 'No PnP results must retain empty array shape'
Assert-Equal -Actual $noCandidates.Count -Expected 0 -Message 'No PnP results must have Count=0'

$artifactResults = @(Test-GatePackageArtifacts -PackageRoot $FixtureRoot -BuildManifestPath $goodManifest)
Assert-Equal -Actual $artifactResults.Count -Expected 2 -Message 'Only immutable fixture artifacts must be hash-checked'
Assert-Equal -Actual @($artifactResults | Where-Object { -not $_.Matches }).Count -Expected 0 -Message 'Valid artifact hashes must match'

$manifestContract = Get-Content -LiteralPath $goodManifest -Raw -Encoding UTF8 | ConvertFrom-Json
Assert-Equal -Actual @($manifestContract.required_runtime_files).Count -Expected 1 -Message 'The mutable config must be declared as a required runtime file'
Assert-Equal -Actual ([string]$manifestContract.required_runtime_files[0].validation) -Expected 'existence_only' -Message 'The mutable config must use existence-only validation'
if ($manifestContract.required_runtime_files[0].PSObject.Properties.Name -contains 'sha256') {
    throw 'Mutable runtime files must not declare sha256 evidence.'
}

Assert-Throws -Action {
    Test-GatePackageArtifacts -PackageRoot $FixtureRoot -BuildManifestPath $missingRuntimeFileManifest
} -ExpectedMessagePattern '*Required runtime file is missing*'

$buildId = Assert-GateBuildId -CurrentBuildId 'fixture-build' -ExpectedBuildId 'fixture-build'
Assert-Equal -Actual $buildId -Expected 'fixture-build' -Message 'Expected build ID must pass'

Assert-Throws -Action {
    Assert-GateBuildId -CurrentBuildId 'other-build' -ExpectedBuildId 'fixture-build'
} -ExpectedMessagePattern '*Build ID mismatch*'

Assert-Throws -Action {
    Test-GatePackageArtifacts -PackageRoot $FixtureRoot -BuildManifestPath $badManifest
} -ExpectedMessagePattern '*Package artifact hash mismatch*'

$gateB = Assert-GateInventoryContext `
    -Stage 'GateB' `
    -CurrentDriverVersion '1.2.3.20' `
    -SampleId 'high-density-batch-20260715' `
    -UsbPortNote 'rear upper USB-A' `
    -CableConfiguration 'Direct'
Assert-Equal -Actual $gateB.ExpectedDriverVersion -Expected '1.2.3.20' -Message 'Gate B driver contract'

$gateC = Assert-GateInventoryContext `
    -Stage 'GateC' `
    -CurrentDriverVersion '1.3.0.4' `
    -SampleId 'high-density-batch-20260715' `
    -UsbPortNote 'rear upper USB-A' `
    -CableConfiguration 'Direct'
Assert-Equal -Actual $gateC.ExpectedDriverVersion -Expected '1.3.0.4' -Message 'Gate C driver contract'

Assert-Throws -Action {
    Assert-GateInventoryContext `
        -Stage 'GateB' `
        -CurrentDriverVersion '1.3.0.4' `
        -SampleId 'high-density-batch-20260715' `
        -UsbPortNote 'rear upper USB-A' `
        -CableConfiguration 'Direct'
} -ExpectedMessagePattern '*Driver version mismatch*'

Assert-Throws -Action {
    Assert-GateInventoryContext `
        -Stage 'GateB' `
        -CurrentDriverVersion '1.2.3.20' `
        -SampleId '' `
        -UsbPortNote 'rear upper USB-A' `
        -CableConfiguration 'Direct'
} -ExpectedMessagePattern '*SampleId is required*'

Assert-Throws -Action {
    Assert-GateInventoryContext `
        -Stage 'GateB' `
        -CurrentDriverVersion '1.2.3.20' `
        -SampleId 'high-density-batch-20260715' `
        -UsbPortNote '' `
        -CableConfiguration 'Direct'
} -ExpectedMessagePattern '*UsbPortNote is required*'

Assert-Throws -Action {
    Assert-GateInventoryContext `
        -Stage 'GateB' `
        -CurrentDriverVersion '1.2.3.20' `
        -SampleId 'high-density-batch-20260715' `
        -UsbPortNote 'rear upper USB-A' `
        -CableConfiguration 'Hub'
} -ExpectedMessagePattern '*CableConfiguration must be Direct*'

Write-Host 'PASS: collect_gate_inventory contract tests'
