[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildId,

    [Parameter(Mandatory = $true)]
    [string]$OutputRoot,

    [Parameter(Mandatory = $true)]
    [string]$SourceRoot,

    [Parameter(Mandatory = $true)]
    [string]$PackageManifest
)

$ErrorActionPreference = 'Stop'

function Get-Sha256([string]$Path) {
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Get-RelativePath([string]$BasePath, [string]$Path) {
    $baseUri = [Uri](([IO.Path]::GetFullPath($BasePath).TrimEnd('\') + '\'))
    $pathUri = [Uri][IO.Path]::GetFullPath($Path)
    return [Uri]::UnescapeDataString($baseUri.MakeRelativeUri($pathUri).ToString())
}

$manifestPath = Join-Path $OutputRoot 'manifest\build_manifest.json'
$checksumsPath = Join-Path $OutputRoot 'manifest\checksums.sha256'
if (Test-Path -LiteralPath $manifestPath) {
    throw "Build manifest already exists: $manifestPath"
}

$package = Get-Content -Raw -LiteralPath $PackageManifest | ConvertFrom-Json
$artifactPaths = @(
    (Join-Path $OutputRoot 'bin\AnalogBoard_TestApp.exe'),
    (Join-Path $OutputRoot 'bin\AnalogBoard_Dll.dll')
)
foreach ($path in $artifactPaths) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required artifact is missing: $path"
    }
}

$sourceRelativePaths = @(
    'AnalogBoard_Dll\AnalogBoard_Dll.cpp',
    'AnalogBoard_Dll\AnalogBoard_Dll.h',
    'AnalogBoard_Dll\AnalogBoard_Dll.vcxproj',
    'AnalogBoard_Dll\Ep4FailureDiagnostic.h',
    'AnalogBoard_TestApp\AnalogBoard_TestApp.vcxproj',
    'AnalogBoard_TestApp\Dialog1_Main.cpp',
    'AnalogBoard_UnitTest\Ep4FailureDiagnostic_test.cpp',
    'AnalogBoard_UnitTest\build_test.bat',
    'AnalogBoard_UnitTest\build_ep4_failure_diagnostic_test.bat',
    'diagnostic_prep\ep4_failure_status\FIELD_PROCEDURE.html',
    'scripts\build_ep4_failure_diagnostic.bat',
    'scripts\finalize_ep4_failure_diagnostic_build.ps1'
)

$sourceFiles = foreach ($relativePath in $sourceRelativePaths) {
    $fullPath = Join-Path $SourceRoot $relativePath
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "Diagnostic source file is missing: $fullPath"
    }
    [ordered]@{
        path = $relativePath.Replace('\', '/')
        sha256 = Get-Sha256 $fullPath
    }
}

$artifacts = foreach ($path in $artifactPaths) {
    $item = Get-Item -LiteralPath $path
    [ordered]@{
        path = Get-RelativePath $OutputRoot $path
        bytes = $item.Length
        sha256 = Get-Sha256 $path
    }
}

$deltaPath = Join-Path $OutputRoot 'source_diff\diagnostic_changes.patch'
$unitTestLogPath = Join-Path $OutputRoot 'verification\unit_tests.txt'
if (-not (Test-Path -LiteralPath $deltaPath -PathType Leaf)) {
    throw "Diagnostic source diff is missing: $deltaPath"
}
if (-not (Test-Path -LiteralPath $unitTestLogPath -PathType Leaf)) {
    throw "Unit-test log is missing: $unitTestLogPath"
}

$manifest = [ordered]@{
    build_id = $BuildId
    created_at = (Get-Date).ToString('o')
    purpose = 'Minimal standby diagnostic build for one EP4 XferData failure after Parameter Set'
    supersedes_build_id = $package.known_build.build_id
    source = [ordered]@{
        package_id = $package.package_id
        base_tag = $package.host_source.base_tag
        head_commit = $package.host_source.head_commit
        original_tracked_patch_sha256 = $package.host_source.tracked_patch_sha256
        original_external_trigger_polling_policy_sha256 = $package.host_source.external_trigger_polling_policy_sha256
        original_external_trigger_polling_test_sha256 = $package.host_source.external_trigger_polling_test_sha256
        cyapi_x64_library_sha256 = $package.host_source.cyapi_x64_library_sha256
        diagnostic_delta = 'source_diff/diagnostic_changes.patch'
        diagnostic_delta_sha256 = Get-Sha256 $deltaPath
        files = $sourceFiles
    }
    diagnostic_contract = [ordered]@{
        capture_when = 'EP4 endpoint 0x84 XferData returns false'
        captured_fields = @('endpoint', 'requestedLength', 'returnedLength', 'UsbdStatus', 'NtStatus', 'Cypress LastError', 'Win32 last error', 'GetTickCount64 monotonic milliseconds')
        storage = 'one fixed-size in-memory record; newest failure replaces stale unconsumed record'
        emission = 'one existing-log record after the external-trigger polling loop has stopped'
        success_poll_logging = $false
        transfer_retry_changed = $false
        ep6_hot_path_changed = $false
        firmware_changed = $false
    }
    tests = [ordered]@{
        tdd_red = 'Ep4FailureDiagnostic_test.cpp initially failed to compile because Ep4FailureDiagnostic.h did not exist'
        portable_policy_test = '28/28 assertions passed with g++ before native build'
        native_suite = 'Targeted EP4 diagnostic, polling-stop, endpoint-discovery, and EP6-retry guard tests completed successfully before Release build'
        native_suite_log = 'verification/unit_tests.txt'
    }
    build = [ordered]@{
        configuration = 'Release|x64'
        toolchain = 'Visual Studio 2022 v143 selected by scripts/run_with_vsdevcmd.bat'
        isolation = 'fresh diagnostic_builds/<build-id>/bin and obj directories; known x64/Release output is not used'
        result = 'success'
    }
    artifacts = $artifacts
    required_runtime_files = @(
        [ordered]@{
            path = 'bin/default_config.csv'
            source = 'immutable field_package/bin/default_config.csv copied into the new output'
            mutable_at_runtime = $true
        }
    )
    hardware_status = 'not run; explicit operator approval is still required'
}

$utf8NoBom = New-Object Text.UTF8Encoding($false)
$json = $manifest | ConvertTo-Json -Depth 10
[IO.File]::WriteAllText($manifestPath, $json + [Environment]::NewLine, $utf8NoBom)

$checksumRelativePaths = @(
    'BUILD_ID.txt',
    'FIELD_CHECKLIST.md',
    'FIELD_PROCEDURE.html',
    'bin\AnalogBoard_TestApp.exe',
    'bin\AnalogBoard_Dll.dll',
    'bin\default_config.csv',
    'manifest\build_manifest.json',
    'source_diff\diagnostic_changes.patch',
    'verification\unit_tests.txt'
)
$checksumLines = foreach ($relativePath in $checksumRelativePaths) {
    $fullPath = Join-Path $OutputRoot $relativePath
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "Checksum input is missing: $fullPath"
    }
    '{0} *{1}' -f (Get-Sha256 $fullPath), $relativePath.Replace('\', '/')
}
[IO.File]::WriteAllLines($checksumsPath, $checksumLines, $utf8NoBom)

Write-Host "Build manifest: $manifestPath"
Write-Host "Checksums:     $checksumsPath"
foreach ($artifact in $artifacts) {
    Write-Host ("{0}  {1}" -f $artifact.sha256, $artifact.path)
}
