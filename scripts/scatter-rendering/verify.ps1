[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Focused')]
    [string]$Mode,

    [Parameter(Mandatory = $true)]
    [ValidateSet('Release')]
    [string]$Configuration,

    [Parameter(Mandatory = $true)]
    [ValidateSet('x64')]
    [string]$Architecture
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:DOTNET_NOLOGO = '1'
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = '1'
$env:DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE = '1'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepositoryRoot = (Resolve-Path -LiteralPath (Join-Path $ScriptDir '..\..') -ErrorAction Stop).Path
$PrototypeRoot = (Resolve-Path -LiteralPath (Join-Path $RepositoryRoot 'prototypes\scatter-rendering') -ErrorAction Stop).Path
$ModulePath = Join-Path $ScriptDir 'scatter_verification_core.psm1'

try {
    Import-Module $ModulePath -Force -ErrorAction Stop
    $null = Assert-P0R1VerificationSelection `
        -Mode $Mode `
        -Configuration $Configuration `
        -Architecture $Architecture
    $dependency = Assert-P0R1RepositoryDependencyContract -RepositoryRoot $RepositoryRoot
    $rendererDecision = Assert-P0R1RendererDecisionContract -RepositoryRoot $RepositoryRoot
    $result = Invoke-P0R1FocusedVerification `
        -RepositoryRoot $RepositoryRoot `
        -PrototypeRoot $PrototypeRoot `
        -Configuration $Configuration `
        -Architecture $Architecture

    if (-not $result.ProductTestsExecuted) {
        throw [InvalidOperationException]::new(
            'Focused verification requires the complete P0-R1 prototype; product tests were not executed.'
        )
    }
    Write-Host "P0-R1 verification status=$($result.Status) mode=$Mode configuration=$Configuration architecture=$Architecture"
    Write-Host "sdk=$($dependency.SdkVersion) desktop_runtime=$($dependency.DesktopRuntimeVersion) target=$($dependency.TargetFramework) external_nuget=$($dependency.ExternalNuGetPackageCount)"
    Write-Host "tests_total=$($result.TestsTotal) tests_passed=$($result.TestsPassed) tests_failed=$($result.TestsFailed)"
    Write-Host "development_observation=$($result.DevelopmentObservation)"
    Write-Host "combined_development_observation=$($result.CombinedDevelopmentObservation)"
    Write-Host "headroom_development_observation=$($result.HeadroomDevelopmentObservation)"
    Write-Host "renderer_decision=$($rendererDecision.DecisionId):$($rendererDecision.SelectedCandidateId)"
    exit 0
}
catch {
    [Console]::Error.WriteLine("P0-R1 verification failed: $($_.Exception.Message)")
    exit 2
}
