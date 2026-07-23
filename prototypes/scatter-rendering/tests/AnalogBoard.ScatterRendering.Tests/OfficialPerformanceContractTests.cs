using System.IO;
using System.Text;
using System.Text.Json;
using AnalogBoard.ScatterRendering.Core;

namespace AnalogBoard.ScatterRendering.Tests;

internal static class OfficialPerformanceContractTests
{
    public static IReadOnlyList<ContractTestCase> Cases { get; } =
    [
        new(nameof(GivenOfficialSchedule_WhenRead_ThenAllWindowsAreExactAndNonOverridable),
            GivenOfficialSchedule_WhenRead_ThenAllWindowsAreExactAndNonOverridable),
        new(nameof(GivenHeadroomScenario_WhenEligibilityDerived_ThenItCanNeverClaimOfficialEligibility),
            GivenHeadroomScenario_WhenEligibilityDerived_ThenItCanNeverClaimOfficialEligibility),
        new(nameof(GivenVersionedScenarios_WhenWorkloadsCreated_ThenFixtureShapesAndLeaseBoundsAreExact),
            GivenVersionedScenarios_WhenWorkloadsCreated_ThenFixtureShapesAndLeaseBoundsAreExact),
        new(nameof(GivenPinnedRuntimeConfiguration_WhenProcessLoaded_ThenCoreAndDesktopPatchAreExact),
            GivenPinnedRuntimeConfiguration_WhenProcessLoaded_ThenCoreAndDesktopPatchAreExact),
        new(nameof(GivenOfficialOutputOutsideCanonicalRoot_WhenValidated_ThenPathAuthorityRejectsIt),
            GivenOfficialOutputOutsideCanonicalRoot_WhenValidated_ThenPathAuthorityRejectsIt),
        new(nameof(GivenRepositoryHeadAndProvenance_WhenCompared_ThenExactRevisionIsRequired),
            GivenRepositoryHeadAndProvenance_WhenCompared_ThenExactRevisionIsRequired),
        new(nameof(GivenOfficialSourceAuthority_WhenRepositoryStateIsDerived_ThenDirtyOrUntrackedInputsAreRejected),
            GivenOfficialSourceAuthority_WhenRepositoryStateIsDerived_ThenDirtyOrUntrackedInputsAreRejected),
        new(nameof(GivenFocusedBuildIdentity_WhenCurrentSourceAndGitMatch_ThenOfficialBinaryIsBound),
            GivenFocusedBuildIdentity_WhenCurrentSourceAndGitMatch_ThenOfficialBinaryIsBound),
        new(nameof(GivenFocusedBuildIdentityMismatch_WhenValidated_ThenOfficialBinaryIsRejected),
            GivenFocusedBuildIdentityMismatch_WhenValidated_ThenOfficialBinaryIsRejected),
        new(nameof(GivenOfficialCliWithDurationOverride_WhenParsed_ThenContractErrorIsRaised),
            GivenOfficialCliWithDurationOverride_WhenParsed_ThenContractErrorIsRaised),
        new(nameof(GivenDryRunCliWithShortSchedule_WhenParsed_ThenOfficialEligibilityRemainsFalse),
            GivenDryRunCliWithShortSchedule_WhenParsed_ThenOfficialEligibilityRemainsFalse),
        new(nameof(GivenDuplicateOrIncompleteReferenceProfile_WhenPreflighted_ThenMeasurementIsRejected),
            GivenDuplicateOrIncompleteReferenceProfile_WhenPreflighted_ThenMeasurementIsRejected),
        new(nameof(GivenDuplicateOrExtraProvenanceField_WhenParsed_ThenExecutionIsRejected),
            GivenDuplicateOrExtraProvenanceField_WhenParsed_ThenExecutionIsRejected),
        new(nameof(GivenOwnerPinnedAndMatchingLiveProfiles_WhenPreflighted_ThenOfficialRunIsEligible),
            GivenOwnerPinnedAndMatchingLiveProfiles_WhenPreflighted_ThenOfficialRunIsEligible),
        new(nameof(GivenHardRunAtExactThresholds_WhenEvaluated_ThenRunPasses),
            GivenHardRunAtExactThresholds_WhenEvaluated_ThenRunPasses),
        new(nameof(GivenFrameStartedInWindowAndCompletedInTail_WhenEvaluated_ThenLatencyAndRateCohortsStayDistinct),
            GivenFrameStartedInWindowAndCompletedInTail_WhenEvaluated_ThenLatencyAndRateCohortsStayDistinct),
        new(nameof(GivenPlannedWindowMeetsExactRates_WhenDispatcherStopsLate_ThenGateUsesFrozenWindow),
            GivenPlannedWindowMeetsExactRates_WhenDispatcherStopsLate_ThenGateUsesFrozenWindow),
        new(nameof(GivenThresholdFailures_WhenSerialized_ThenEveryMetricIdBelongsToPinnedSchema),
            GivenThresholdFailures_WhenSerialized_ThenEveryMetricIdBelongsToPinnedSchema),
        new(nameof(GivenAllocationProbePairIsUnstable_WhenEvaluated_ThenRunIsIncomplete),
            GivenAllocationProbePairIsUnstable_WhenEvaluated_ThenRunIsIncomplete),
        new(nameof(GivenHardRunWithOneThresholdMiss_WhenEvaluated_ThenRunFailsWithoutAggregateRescue),
            GivenHardRunWithOneThresholdMiss_WhenEvaluated_ThenRunFailsWithoutAggregateRescue),
        new(nameof(GivenRawRunWithInvalidTicksOrOverwrite_WhenValidated_ThenRunIsIncomplete),
            GivenRawRunWithInvalidTicksOrOverwrite_WhenValidated_ThenRunIsIncomplete),
        new(nameof(GivenOfficialRunWithWrongWindowOrSparseInput_WhenEvaluated_ThenRunIsIncomplete),
            GivenOfficialRunWithWrongWindowOrSparseInput_WhenEvaluated_ThenRunIsIncomplete),
        new(nameof(GivenSchedulerFailureOrMetricSampleMismatch_WhenEvaluated_ThenRunIsIncomplete),
            GivenSchedulerFailureOrMetricSampleMismatch_WhenEvaluated_ThenRunIsIncomplete),
        new(nameof(GivenNestedDuplicateRawProperty_WhenValidated_ThenArtifactIsRejected),
            GivenNestedDuplicateRawProperty_WhenValidated_ThenArtifactIsRejected),
        new(nameof(GivenRequiredPrimitiveRawFieldIsMissing_WhenDeserialized_ThenArtifactIsRejected),
            GivenRequiredPrimitiveRawFieldIsMissing_WhenDeserialized_ThenArtifactIsRejected),
        new(nameof(GivenSoakAtMemoryBoundaries_WhenEvaluated_ThenBoundariesPassAndOneByteOverFails),
            GivenSoakAtMemoryBoundaries_WhenEvaluated_ThenBoundariesPassAndOneByteOverFails),
        new(nameof(GivenOneOfThreeIndependentRunsFails_WhenSuiteEvaluated_ThenOfficialAcceptanceIsFalse),
            GivenOneOfThreeIndependentRunsFails_WhenSuiteEvaluated_ThenOfficialAcceptanceIsFalse),
        new(nameof(GivenAtomicArtifactWriteFailure_WhenPublished_ThenNoFinalArtifactIsVisible),
            GivenAtomicArtifactWriteFailure_WhenPublished_ThenNoFinalArtifactIsVisible),
        new(nameof(GivenAtomicArtifact_WhenPublished_ThenSealedHashAndSizeMatch),
            GivenAtomicArtifact_WhenPublished_ThenSealedHashAndSizeMatch),
        new(nameof(GivenSealedManifestAfterMoveFailure_WhenRetried_ThenIdenticalPayloadCanResume),
            GivenSealedManifestAfterMoveFailure_WhenRetried_ThenIdenticalPayloadCanResume),
        new(nameof(GivenRetrySessionWithUnsealedRootFile_WhenValidated_ThenFinalizationIsRejected),
            GivenRetrySessionWithUnsealedRootFile_WhenValidated_ThenFinalizationIsRejected),
        new(nameof(GivenDryRunManifest_WhenCreated_ThenOfficialAcceptanceCannotBeClaimed),
            GivenDryRunManifest_WhenCreated_ThenOfficialAcceptanceCannotBeClaimed),
    ];

    private static void GivenOfficialSchedule_WhenRead_ThenAllWindowsAreExactAndNonOverridable()
    {
        var schedule = PerformanceRunSchedule.Official;

        ContractAssert.Equal(TimeSpan.FromSeconds(30), schedule.Warmup);
        ContractAssert.Equal(TimeSpan.FromSeconds(60), schedule.Measurement);
        ContractAssert.Equal(3, schedule.HardRunCount);
        ContractAssert.Equal(TimeSpan.FromMinutes(10), schedule.Soak);
        ContractAssert.Equal(false, schedule.AllowsOverrides);
        ContractAssert.Equal("AB-PERF-RUNNER-v1", schedule.RunnerContractId);
    }

    private static void GivenHeadroomScenario_WhenEligibilityDerived_ThenItCanNeverClaimOfficialEligibility()
    {
        ContractAssert.Equal(
            false,
            PerformanceRunEligibility.IsEligible(
                PerformanceExecutionMode.Official,
                PerformanceScenario.Headroom,
                profileEligible: true));
        ContractAssert.Equal(
            true,
            PerformanceRunEligibility.IsEligible(
                PerformanceExecutionMode.Official,
                PerformanceScenario.HardScatter,
                profileEligible: true));
        ContractAssert.Equal(
            false,
            PerformanceRunEligibility.IsEligible(
                PerformanceExecutionMode.DryRun,
                PerformanceScenario.HardScatter,
                profileEligible: true));
    }

    private static void GivenVersionedScenarios_WhenWorkloadsCreated_ThenFixtureShapesAndLeaseBoundsAreExact()
    {
        var hardScatter = PerformanceWorkloadContract.Create(PerformanceScenario.HardScatter);
        var hardCombined = PerformanceWorkloadContract.Create(PerformanceScenario.HardCombined);
        var headroom = PerformanceWorkloadContract.Create(PerformanceScenario.Headroom);

        ContractAssert.Equal(100_001, hardScatter.EventCount);
        ContractAssert.Equal(512, hardScatter.RasterWidth);
        ContractAssert.Equal(512, hardScatter.RasterHeight);
        ContractAssert.Equal(1, hardScatter.ScatterTileCount);
        ContractAssert.Equal(0, hardScatter.GmiTileCount);
        ContractAssert.Equal(8, hardScatter.ScatterLeasePoolSize);
        ContractAssert.Equal(0, hardScatter.GmiLeasePoolSize);
        ContractAssert.Equal(0x5A17, hardScatter.AggregateSeed);
        ContractAssert.Equal(PerformanceMetricSchema.SchemaId, hardScatter.MetricSchemaId);
        ContractAssert.Equal(64, hardScatter.MetricDefinitionsSha256.Length);
        ContractAssert.Equal("stopwatch_ticks", hardScatter.RawTickUnit);
        ContractAssert.Equal(
            "GC.GetAllocatedBytesForCurrentThread",
            hardScatter.AllocationProbeCounter);
        ContractAssert.Equal(1, hardCombined.ScatterTileCount);
        ContractAssert.Equal(1, hardCombined.GmiTileCount);
        ContractAssert.Equal(4, hardCombined.GmiLeasePoolSize);
        ContractAssert.Equal(0x6B28, hardCombined.GmiSeed);
        ContractAssert.Equal(131_072, headroom.EventCount);
        ContractAssert.Equal(1_024, headroom.RasterWidth);
        ContractAssert.Equal(1_024, headroom.RasterHeight);
        ContractAssert.Equal(2, headroom.ScatterTileCount);
        ContractAssert.Equal(1, headroom.GmiTileCount);
        ContractAssert.Equal(100, headroom.GmiWaveformCount);
        ContractAssert.Equal(2_400, headroom.GmiSampleCount);
        ContractAssert.Equal(0x7C39, headroom.AggregateSeed);
        ContractAssert.Equal(0x8D4A, headroom.GmiSeed);
    }

    private static void GivenPinnedRuntimeConfiguration_WhenProcessLoaded_ThenCoreAndDesktopPatchAreExact()
    {
        var runtime = PerformanceLoadedRuntime.Capture();

        ContractAssert.Equal("10.0.10", runtime.CoreRuntimeInformationalVersion);
        ContractAssert.Equal("10.0.10", runtime.DesktopRuntimeInformationalVersion);
        ContractAssert.Equal("Disable", runtime.RollForwardPolicy);
    }

    private static void GivenOfficialOutputOutsideCanonicalRoot_WhenValidated_ThenPathAuthorityRejectsIt()
    {
        var repository = CreateTemporaryDirectory();
        try
        {
            Directory.CreateDirectory(Path.Combine(repository, ".git"));
            var command = new PerformanceRunCommand(
                PerformanceExecutionMode.Official,
                PerformanceScenario.HardScatter,
                1,
                repository,
                Path.Combine(repository, "other-output"),
                Path.Combine(repository, "other-output", "session.inprogress", "runs", "hard-scatter-01.raw.json"),
                Path.Combine(repository, "docs", "reference", "scatter-rendering", "phase0", "performance-reference-profile-v1.json"),
                Path.Combine(repository, "other-output", "session.inprogress", "profile.actual.json"),
                Path.Combine(repository, "other-output", "session.inprogress", "provenance.json"),
                PerformanceRunSchedule.Official);

            ContractAssert.Throws<PerformanceArtifactException>(
                () => PerformancePathAuthority.ValidateRun(command),
                "Performance official output root is outside its canonical session contract.");
        }
        finally
        {
            Directory.Delete(repository, recursive: true);
        }
    }

    private static void GivenRepositoryHeadAndProvenance_WhenCompared_ThenExactRevisionIsRequired()
    {
        var repository = CreateTemporaryDirectory();
        var revision = new string('a', 40);
        try
        {
            var git = Path.Combine(repository, ".git");
            Directory.CreateDirectory(git);
            File.WriteAllText(Path.Combine(git, "HEAD"), revision, Encoding.UTF8);

            PerformanceGitAuthority.RequireHead(repository, revision);
            ContractAssert.Throws<PerformanceMeasurementException>(
                () => PerformanceGitAuthority.RequireHead(repository, new string('b', 40)),
                $"Performance provenance source revision does not match repository HEAD: expected {new string('b', 40)}, actual {revision}.");
        }
        finally
        {
            Directory.Delete(repository, recursive: true);
        }
    }

    private static void GivenOfficialSourceAuthority_WhenRepositoryStateIsDerived_ThenDirtyOrUntrackedInputsAreRejected()
    {
        // Given: A canonical profile and Git command evidence for one exact clean repository revision.
        var repository = CreateTemporaryDirectory();
        var revision = new string('a', 40);
        var relativeProfile = Path.Combine(
            "docs",
            "reference",
            "scatter-rendering",
            "phase0",
            "performance-reference-profile-v1.json");
        var profile = Path.Combine(repository, relativeProfile);
        var assembly = Path.Combine(
            repository,
            "prototypes",
            "scatter-rendering",
            "tests",
            "AnalogBoard.ScatterRendering.Tests",
            "bin",
            "x64",
            "Release",
            "net10.0-windows",
            "AnalogBoard.ScatterRendering.Tests.dll");
        Directory.CreateDirectory(Path.GetDirectoryName(profile)!);
        Directory.CreateDirectory(Path.GetDirectoryName(assembly)!);
        Directory.CreateDirectory(Path.Combine(repository, ".git"));
        File.WriteAllText(profile, "{}", Encoding.UTF8);
        File.WriteAllText(assembly, "fixture", Encoding.UTF8);
        foreach (var measuredInput in new[]
        {
            ".editorconfig",
            ".gitattributes",
            "global.json",
            "docs/reference/scatter-rendering/phase0/display-transform-contract-v1.json",
            "docs/reference/scatter-rendering/phase0/density-raster-contract-v1.json",
            "docs/reference/scatter-rendering/phase0/gmi-raster-contract-v1.json",
            "prototypes/scatter-rendering/x64/Injected.cs",
        })
        {
            var path = Path.Combine(
                repository,
                measuredInput.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, "{}", Encoding.UTF8);
        }
        var measuredSourcePaths = PerformanceBuildIdentity.GetMeasuredSourcePaths(repository);
        const string ignoredSource = "prototypes/scatter-rendering/x64/Injected.cs";
        var cleanOid = new string('b', 40);
        var headSourceLines = measuredSourcePaths
            .Select(path => $"100644 blob {cleanOid}\t{path}")
            .ToArray();
        var rawHashCommandObserved = false;
        var priorPreflight = Environment.GetEnvironmentVariable("P0R1_OFFICIAL_PREFLIGHT");
        var priorGitExecutable = Environment.GetEnvironmentVariable("P0R1_GIT_EXECUTABLE");
        try
        {
            string[] AuthorityCommand(IReadOnlyList<string> arguments)
            {
                ContractAssert.SequenceEqual(
                    new[]
                    {
                        "-c",
                        "core.fsmonitor=false",
                        "-c",
                        "core.untrackedCache=false",
                        "--no-replace-objects",
                    },
                    arguments.Take(5).ToArray());
                return arguments.Skip(5).ToArray();
            }

            PerformanceGitCommandResult InvokeClean(IReadOnlyList<string> arguments)
            {
                var command = AuthorityCommand(arguments);
                if (command is ["hash-object", ..])
                {
                    ContractAssert.SequenceEqual(
                        new[] { "hash-object", "--no-filters", "--" },
                        command.Take(3).ToArray());
                    rawHashCommandObserved = true;
                    return new(
                        0,
                        measuredSourcePaths.Select(_ => cleanOid).ToArray(),
                        []);
                }

                return command switch
                {
                    ["rev-parse", "--show-toplevel"] => new(0, [repository], []),
                    ["rev-parse", "HEAD"] => new(0, [revision], []),
                    ["status", "--porcelain=v1", "--untracked-files=normal"] => new(0, [], []),
                    ["ls-tree", "-r", "HEAD", "--", ..] => new(0, headSourceLines, []),
                    ["ls-files", "-v", "--full-name", "--", ..] =>
                        new(0, measuredSourcePaths.Select(path => $"H {path}").ToArray(), []),
                    ["ls-files", "--error-unmatch", "--full-name", "--", _] =>
                        new(0, [relativeProfile.Replace('\\', '/')], []),
                    _ => new(2, [], ["unexpected Git command"]),
                };
            }

            PerformanceGitCommandResult InvokeDirty(IReadOnlyList<string> arguments) =>
                AuthorityCommand(arguments) is
                    ["status", "--porcelain=v1", "--untracked-files=normal"]
                    ? new(0, [" M tracked.cs"], [])
                    : InvokeClean(arguments);
            PerformanceGitCommandResult InvokeUntracked(IReadOnlyList<string> arguments) =>
                AuthorityCommand(arguments) is
                    ["ls-files", "--error-unmatch", "--full-name", "--", _]
                    ? new(1, [], ["pathspec did not match"])
                    : InvokeClean(arguments);
            PerformanceGitCommandResult InvokeIgnoredSource(IReadOnlyList<string> arguments) =>
                AuthorityCommand(arguments) is ["ls-tree", "-r", "HEAD", "--", ..]
                    ? new(
                        0,
                        headSourceLines
                            .Where(line => !line.EndsWith(ignoredSource, StringComparison.Ordinal))
                            .ToArray(),
                        [])
                    : InvokeClean(arguments);
            PerformanceGitCommandResult InvokeAssumeUnchanged(IReadOnlyList<string> arguments) =>
                AuthorityCommand(arguments) is ["ls-files", "-v", "--full-name", "--", ..]
                    ? new(
                        0,
                        measuredSourcePaths
                            .Select(path => $"{(path == ignoredSource ? 'h' : 'H')} {path}")
                            .ToArray(),
                        [])
                    : InvokeClean(arguments);
            PerformanceGitCommandResult InvokeSkippedAtWorktree(IReadOnlyList<string> arguments) =>
                AuthorityCommand(arguments) is ["ls-tree", "-r", "HEAD", "--", ..]
                    ? new(
                        0,
                        headSourceLines
                            .Append(
                                $"100644 blob {cleanOid}\t" +
                                "prototypes/scatter-rendering/x64/Skipped.cs")
                            .Order(StringComparer.Ordinal)
                            .ToArray(),
                        [])
                    : InvokeClean(arguments);
            PerformanceGitCommandResult InvokeContentMismatch(IReadOnlyList<string> arguments) =>
                AuthorityCommand(arguments) is ["hash-object", "--no-filters", "--", ..]
                    ? new(
                        0,
                        measuredSourcePaths
                            .Select(path => path == ignoredSource ? new string('c', 40) : cleanOid)
                            .ToArray(),
                        [])
                    : InvokeClean(arguments);

            // When: Shared C# authority derives the state rather than trusting provenance.
            Environment.SetEnvironmentVariable("P0R1_OFFICIAL_PREFLIGHT", null);
            Environment.SetEnvironmentVariable("P0R1_GIT_EXECUTABLE", null);
            ContractAssert.Throws<PerformanceMeasurementException>(
                () => PerformanceGitAuthority.RequireOfficialState(
                    repository,
                    revision,
                    declaredSourceDirty: false,
                    profile),
                "Official performance C# entry requires the repository preflight wrapper.");
            PerformanceGitAuthority.RequireOfficialState(
                repository,
                revision,
                declaredSourceDirty: false,
                profile,
                InvokeClean,
                assembly);
            ContractAssert.Equal(true, rawHashCommandObserved);

            // Then: Dirty, untracked, ignored, assume-unchanged, and skipped variants fail typed.
            ContractAssert.Throws<PerformanceMeasurementException>(
                () => PerformanceGitAuthority.RequireOfficialState(
                    repository,
                    revision,
                    declaredSourceDirty: false,
                    profile,
                    InvokeDirty,
                    assembly),
                "Official performance requires an empty Git status.");
            ContractAssert.Throws<PerformanceMeasurementException>(
                () => PerformanceGitAuthority.RequireOfficialState(
                    repository,
                    revision,
                    declaredSourceDirty: false,
                    profile,
                    InvokeUntracked,
                    assembly),
                "Official performance reference profile must be tracked at HEAD.");
            ContractAssert.Throws<PerformanceMeasurementException>(
                () => PerformanceGitAuthority.RequireOfficialState(
                    repository,
                    revision,
                    declaredSourceDirty: false,
                    profile,
                    InvokeIgnoredSource,
                    assembly),
                "Measured performance source paths must exactly match HEAD.");
            ContractAssert.Throws<PerformanceMeasurementException>(
                () => PerformanceGitAuthority.RequireOfficialState(
                    repository,
                    revision,
                    declaredSourceDirty: false,
                    profile,
                    InvokeAssumeUnchanged,
                    assembly),
                "Measured performance source inputs must not use assume-unchanged or skip-worktree index flags.");
            ContractAssert.Throws<PerformanceMeasurementException>(
                () => PerformanceGitAuthority.RequireOfficialState(
                    repository,
                    revision,
                    declaredSourceDirty: false,
                    profile,
                    InvokeSkippedAtWorktree,
                    assembly),
                "Measured performance source paths must exactly match HEAD.");
            ContractAssert.Throws<PerformanceMeasurementException>(
                () => PerformanceGitAuthority.RequireOfficialState(
                    repository,
                    revision,
                    declaredSourceDirty: false,
                    profile,
                    InvokeContentMismatch,
                    assembly),
                "Measured performance working source bytes must match HEAD blobs.");
        }
        finally
        {
            Environment.SetEnvironmentVariable("P0R1_OFFICIAL_PREFLIGHT", priorPreflight);
            Environment.SetEnvironmentVariable("P0R1_GIT_EXECUTABLE", priorGitExecutable);
            Directory.Delete(repository, recursive: true);
        }
    }

    private static void GivenFocusedBuildIdentity_WhenCurrentSourceAndGitMatch_ThenOfficialBinaryIsBound()
    {
        // Given: Focused verification embedded the measured source tree and exact Git binary.
        var repository = FindRepositoryRoot();
        var identity = PerformanceBuildIdentity.Capture();

        // When: The executing assembly re-derives both identities before official execution.
        var currentSource = PerformanceBuildIdentity.ComputeMeasuredSourceTree(repository);
        PerformanceBuildIdentity.RequireCurrentSource(
            repository,
            typeof(OfficialPerformanceContractTests).Assembly.Location,
            identity.GitExecutablePath);

        // Then: The embedded source identity is the current measured source tree.
        ContractAssert.Equal(identity.MeasuredSourceTreeSha256, currentSource.Sha256);
        ContractAssert.Equal(true, currentSource.FileCount > 0);
    }

    private static void GivenFocusedBuildIdentityMismatch_WhenValidated_ThenOfficialBinaryIsRejected()
    {
        // Given: The current focused build identity with one source or Git digest replaced.
        var repository = FindRepositoryRoot();
        var identity = PerformanceBuildIdentity.Capture();
        var assemblyPath = typeof(OfficialPerformanceContractTests).Assembly.Location;
        var sourceMismatch = identity with { MeasuredSourceTreeSha256 = new string('0', 64) };
        var gitMismatch = identity with { GitExecutableSha256 = new string('0', 64) };
        var configurationMismatch = identity with { Configuration = "Debug" };
        var coreAssemblyPath = typeof(AggregateFrame).Assembly.Location;

        // When/Then: Either mismatch rejects the binary with a stable typed reason.
        ContractAssert.Throws<PerformanceMeasurementException>(
            () => PerformanceBuildIdentity.RequireCurrentSource(
                repository,
                assemblyPath,
                identity.GitExecutablePath,
                sourceMismatch),
            "Official performance executable was not built from the current measured source tree.");
        ContractAssert.Throws<PerformanceMeasurementException>(
            () => PerformanceBuildIdentity.RequireCurrentSource(
                repository,
                assemblyPath,
                identity.GitExecutablePath,
                gitMismatch),
            "Official performance Git executable hash differs from the build identity.");
        ContractAssert.Throws<PerformanceMeasurementException>(
            () => PerformanceBuildIdentity.RequireDependencyIdentity(
                identity,
                configurationMismatch,
                coreAssemblyPath,
                coreAssemblyPath,
                "Core"),
            "Official performance Core dependency build identity differs from the executing assembly.");
    }

    private static void GivenOfficialCliWithDurationOverride_WhenParsed_ThenContractErrorIsRaised()
    {
        var args = OfficialRunArguments().Concat(["--warmup-ms", "10"]).ToArray();

        ContractAssert.Throws<PerformanceCommandLineException>(
            () => PerformanceCommandLine.Parse(args),
            "Official performance schedule does not accept duration overrides.");
    }

    private static void GivenDryRunCliWithShortSchedule_WhenParsed_ThenOfficialEligibilityRemainsFalse()
    {
        var args = OfficialRunArguments().ToArray();
        args[1] = "dry-run";
        var command = PerformanceCommandLine.Parse(
            args.Concat(
            [
                "--warmup-ms", "5",
                "--measurement-ms", "20",
                "--soak-ms", "30",
            ]).ToArray());

        ContractAssert.Equal(PerformanceExecutionMode.DryRun, command.Mode);
        ContractAssert.Equal(false, command.OfficialEligible);
        ContractAssert.Equal(TimeSpan.FromMilliseconds(5), command.Schedule.Warmup);
        ContractAssert.Equal(TimeSpan.FromMilliseconds(20), command.Schedule.Measurement);
        ContractAssert.Equal(TimeSpan.FromMilliseconds(30), command.Schedule.Soak);
    }

    private static void GivenDuplicateOrIncompleteReferenceProfile_WhenPreflighted_ThenMeasurementIsRejected()
    {
        var valid = CreateProfileJson("owner_pinned", remoteSession: false);
        var duplicate = valid.Replace(
            "\"profile_id\":\"AB-PERF-REF-v1\"",
            "\"profile_id\":\"AB-PERF-REF-v1\",\"profile_id\":\"AB-PERF-REF-v1\"");
        var incomplete = valid.Replace("\"gpu_driver_version\":\"32.0.15.7283\"", "\"gpu_driver_version\":null");

        ContractAssert.Throws<PerformanceProfileException>(
            () => PerformanceProfilePreflight.ParseReference(duplicate),
            "Performance profile must not contain duplicate JSON property names: profile_id.");
        ContractAssert.Throws<PerformanceProfileException>(
            () => PerformanceProfilePreflight.ParseReference(incomplete),
            "Official reference profile field must be a non-empty string: gpu_driver_version.");
    }

    private static void GivenOwnerPinnedAndMatchingLiveProfiles_WhenPreflighted_ThenOfficialRunIsEligible()
    {
        var reference = PerformanceProfilePreflight.ParseReference(
            CreateProfileJson("owner_pinned", remoteSession: false));
        var observed = PerformanceProfilePreflight.ParseObserved(
            CreateProfileJson("live_observation", remoteSession: false));

        var result = PerformanceProfilePreflight.Compare(reference, observed);

        ContractAssert.Equal(true, result.OfficialEligible);
        ContractAssert.Equal("eligible", result.Status);
        ContractAssert.Equal(0, result.Mismatches.Count);
    }

    private static void GivenDuplicateOrExtraProvenanceField_WhenParsed_ThenExecutionIsRejected()
    {
        const string valid = "{" +
            "\"schema_id\":\"analogboard.scatter-rendering.provenance.v1\"," +
            "\"source_revision\":\"0123456789012345678901234567890123456789\"," +
            "\"source_dirty\":false," +
            "\"sdk_version\":\"10.0.302\"," +
            "\"desktop_runtime_version\":\"10.0.10\"," +
            "\"target_framework\":\"net10.0-windows\"," +
            "\"configuration\":\"Release\"," +
            "\"architecture\":\"x64\"}";
        var duplicate = valid.Replace(
            "\"source_dirty\":false",
            "\"source_dirty\":false,\"source_dirty\":false");
        var extra = valid.Replace("}", ",\"untracked\":true}");

        ContractAssert.Throws<PerformanceMeasurementException>(
            () => PerformanceRunProvenance.Parse(duplicate, PerformanceExecutionMode.Official),
            "Performance provenance must not contain duplicate JSON property names: source_dirty.");
        ContractAssert.Throws<PerformanceMeasurementException>(
            () => PerformanceRunProvenance.Parse(extra, PerformanceExecutionMode.Official),
            "Performance provenance must contain the exact versioned field set.");
    }

    private static void GivenHardRunAtExactThresholds_WhenEvaluated_ThenRunPasses()
    {
        var raw = CreatePassingHardRun(PerformanceScenario.HardCombined);

        var verdict = PerformanceRunEvaluator.Evaluate(raw);

        ContractAssert.Equal(PerformanceVerdictStatus.Pass, verdict.Status);
        ContractAssert.Equal(true, verdict.OfficialCandidate);
        ContractAssert.Equal(0, verdict.FailedMetrics.Count);
    }

    private static void GivenFrameStartedInWindowAndCompletedInTail_WhenEvaluated_ThenLatencyAndRateCohortsStayDistinct()
    {
        var raw = CreatePassingHardRun(PerformanceScenario.HardScatter) with
        {
            ScatterFrameDurationTicks = RepeatTicks(1_801, 33.3),
            ScatterPublicationDurationTicks = RepeatTicks(1_802, 1.0),
            CoalescedFrameCount = 1,
        };

        var verdict = PerformanceRunEvaluator.Evaluate(raw);

        ContractAssert.Equal(PerformanceVerdictStatus.Pass, verdict.Status);
    }

    private static void GivenPlannedWindowMeetsExactRates_WhenDispatcherStopsLate_ThenGateUsesFrozenWindow()
    {
        // Given: Exactly 1,800 scatter and 300 GMI completions in the frozen 60-second window.
        var raw = CreatePassingHardRun(PerformanceScenario.HardCombined) with
        {
            WindowEndTick = CreatePassingHardRun(PerformanceScenario.HardCombined).WindowEndTick + 1,
        };

        // When: Dispatcher shutdown observes the end one tick after the planned boundary.
        var verdict = PerformanceRunEvaluator.Evaluate(raw);

        // Then: Shutdown latency does not turn exact 30 fps and 5 Hz cohorts into a miss.
        ContractAssert.Equal(PerformanceVerdictStatus.Pass, verdict.Status);
    }

    private static void GivenThresholdFailures_WhenSerialized_ThenEveryMetricIdBelongsToPinnedSchema()
    {
        // Given: Feed publication and the two pending-work boundaries each miss independently.
        var scatterPublication = CreatePassingHardRun(PerformanceScenario.HardScatter) with
        {
            ScatterPublicationDurationTicks = RepeatTicks(1_800, 1.0001),
        };
        var gmiPublication = CreatePassingHardRun(PerformanceScenario.HardCombined) with
        {
            GmiPublicationDurationTicks = RepeatTicks(300, 1.0001),
        };
        var pendingFrame = CreatePassingHardRun(PerformanceScenario.HardScatter) with
        {
            PendingFrameMaximum = 2,
        };
        var pendingCallback = CreatePassingHardRun(PerformanceScenario.HardScatter) with
        {
            PendingCallbackMaximum = 2,
        };

        // When: The evaluator emits machine-readable failed metric identifiers.
        var failures = new[]
        {
            PerformanceRunEvaluator.Evaluate(scatterPublication),
            PerformanceRunEvaluator.Evaluate(gmiPublication),
            PerformanceRunEvaluator.Evaluate(pendingFrame),
            PerformanceRunEvaluator.Evaluate(pendingCallback),
        }.SelectMany(verdict => verdict.FailedMetrics).ToArray();

        // Then: Every identifier resolves through the pinned schema and its unit map.
        ContractAssert.SequenceEqual(
            new[]
            {
                PerformanceMetricNames.ProducerPublicationP99,
                PerformanceMetricNames.ProducerPublicationP99,
                PerformanceMetricNames.PendingWorkMax,
                PerformanceMetricNames.PendingWorkMax,
            },
            failures);
        foreach (var metric in failures)
        {
            ContractAssert.Equal(false, string.IsNullOrWhiteSpace(PerformanceMetricSchema.UnitFor(metric)));
        }
    }

    private static void GivenAllocationProbePairIsUnstable_WhenEvaluated_ThenRunIsIncomplete()
    {
        var raw = CreatePassingHardRun(PerformanceScenario.HardScatter) with
        {
            OneEventAllocationProbeBytes = 64_000,
            HardEventAllocationProbeBytes = 63_000,
            EventAllocationProbeDeltaBytes = 0,
            AllocationProbeInvalidPairCount = 1,
        };

        var verdict = PerformanceRunEvaluator.Evaluate(raw);

        ContractAssert.Equal(PerformanceVerdictStatus.Incomplete, verdict.Status);
    }

    private static void GivenHardRunWithOneThresholdMiss_WhenEvaluated_ThenRunFailsWithoutAggregateRescue()
    {
        var raw = CreatePassingHardRun(PerformanceScenario.HardScatter) with
        {
            ScatterFrameDurationTicks =
            [
                .. RepeatTicks(1_799, 33.3),
                MillisecondsToTicks(100.0001),
            ],
        };

        var verdict = PerformanceRunEvaluator.Evaluate(raw);

        ContractAssert.Equal(PerformanceVerdictStatus.Fail, verdict.Status);
        ContractAssert.SequenceEqual(
            new[] { "scatter.frame_time_max" },
            verdict.FailedMetrics);
    }

    private static void GivenRawRunWithInvalidTicksOrOverwrite_WhenValidated_ThenRunIsIncomplete()
    {
        var nonMonotonic = CreatePassingHardRun(PerformanceScenario.HardCombined) with
        {
            GmiCompletionTicks = [1_000, 999],
        };
        var overwritten = CreatePassingHardRun(PerformanceScenario.HardCombined) with
        {
            OverwrittenSampleCount = 1,
        };

        ContractAssert.Equal(
            PerformanceVerdictStatus.Incomplete,
            PerformanceRunEvaluator.Evaluate(nonMonotonic).Status);
        ContractAssert.Equal(
            PerformanceVerdictStatus.Incomplete,
            PerformanceRunEvaluator.Evaluate(overwritten).Status);
    }

    private static void GivenOfficialRunWithWrongWindowOrSparseInput_WhenEvaluated_ThenRunIsIncomplete()
    {
        var wrongWindow = CreatePassingHardRun(PerformanceScenario.HardScatter) with
        {
            PlannedWindowDurationTicks = 59L * 10_000_000,
        };
        var sparseInput = CreatePassingHardRun(PerformanceScenario.HardScatter) with
        {
            InputLatencyTicks = RepeatTicks(2, 1.0),
        };

        ContractAssert.Equal(
            PerformanceVerdictStatus.Incomplete,
            PerformanceRunEvaluator.Evaluate(wrongWindow).Status);
        ContractAssert.Equal(
            PerformanceVerdictStatus.Incomplete,
            PerformanceRunEvaluator.Evaluate(sparseInput).Status);
    }

    private static void GivenSchedulerFailureOrMetricSampleMismatch_WhenEvaluated_ThenRunIsIncomplete()
    {
        var schedulerFailure = CreatePassingHardRun(PerformanceScenario.HardScatter) with
        {
            SchedulerFailureCount = 1,
        };
        var sampleMismatch = CreatePassingHardRun(PerformanceScenario.HardScatter) with
        {
            MetricSampleCountMismatch = 1,
        };

        ContractAssert.Equal(
            PerformanceVerdictStatus.Incomplete,
            PerformanceRunEvaluator.Evaluate(schedulerFailure).Status);
        ContractAssert.Equal(
            PerformanceVerdictStatus.Incomplete,
            PerformanceRunEvaluator.Evaluate(sampleMismatch).Status);
    }

    private static void GivenNestedDuplicateRawProperty_WhenValidated_ThenArtifactIsRejected()
    {
        const string json = "{\"official_acceptance\":false,\"raw\":{" +
            "\"run_index\":1,\"run_index\":2}}";

        ContractAssert.Throws<PerformanceArtifactException>(
            () => PerformanceJsonValidation.RejectDuplicateProperties(
                json,
                "Performance raw artifact"),
            "Performance raw artifact must not contain duplicate JSON property names: raw.run_index.");
    }

    private static void GivenRequiredPrimitiveRawFieldIsMissing_WhenDeserialized_ThenArtifactIsRejected()
    {
        var raw = CreatePassingHardRun(PerformanceScenario.HardScatter);
        var json = JsonSerializer.Serialize(raw, PerformanceArtifactJson.Options);
        var marker = $"  \"process_allocated_bytes\": {raw.ProcessAllocatedBytes},\r\n";
        var missing = json.Replace(marker, string.Empty, StringComparison.Ordinal);
        if (StringComparer.Ordinal.Equals(json, missing))
        {
            marker = $"  \"process_allocated_bytes\": {raw.ProcessAllocatedBytes},\n";
            missing = json.Replace(marker, string.Empty, StringComparison.Ordinal);
        }

        ContractAssert.Throws<PerformanceArtifactException>(
            () => PerformanceArtifactJson.DeserializeRequired<PerformanceRawRun>(
                Encoding.UTF8.GetBytes(missing),
                "Performance raw run"),
            "Performance raw run does not contain the complete versioned field set.");
    }

    private static void GivenSoakAtMemoryBoundaries_WhenEvaluated_ThenBoundariesPassAndOneByteOverFails()
    {
        const long startPrivateBytes = 400L * 1024 * 1024;
        var boundary = CreatePassingSoak() with
        {
            RetainedManagedHeapGrowthBytes = 8L * 1024 * 1024,
            StartingPrivateBytes = startPrivateBytes,
            PrivateBytesGrowth = 40L * 1024 * 1024,
        };
        var over = boundary with
        {
            PrivateBytesGrowth = (40L * 1024 * 1024) + 1,
        };

        ContractAssert.Equal(
            PerformanceVerdictStatus.Pass,
            PerformanceRunEvaluator.Evaluate(boundary).Status);
        ContractAssert.Equal(
            PerformanceVerdictStatus.Fail,
            PerformanceRunEvaluator.Evaluate(over).Status);
    }

    private static void GivenOneOfThreeIndependentRunsFails_WhenSuiteEvaluated_ThenOfficialAcceptanceIsFalse()
    {
        var passing = PerformanceRunEvaluator.Evaluate(
            CreatePassingHardRun(PerformanceScenario.HardScatter));
        var failing = passing with
        {
            Status = PerformanceVerdictStatus.Fail,
            FailedMetrics = ["scatter.delivered_rate"],
        };
        var combined = PerformanceRunEvaluator.Evaluate(
            CreatePassingHardRun(PerformanceScenario.HardCombined));
        var soak = PerformanceRunEvaluator.Evaluate(CreatePassingSoak());

        var verdict = PerformanceSuiteEvaluator.EvaluateOfficial(
            [passing, passing, failing],
            [combined, combined, combined],
            soak,
            headroomObserved: true,
            profileEligible: true,
            childProcessIds: [101, 102, 103, 104, 105, 106, 107, 108]);

        ContractAssert.Equal(false, verdict.OfficialAcceptance);
        ContractAssert.Equal("fail", verdict.Status);
    }

    private static void GivenAtomicArtifactWriteFailure_WhenPublished_ThenNoFinalArtifactIsVisible()
    {
        var directory = CreateTemporaryDirectory();
        var path = Path.Combine(directory, "run.raw.json");
        try
        {
            ContractAssert.Throws<InvalidOperationException>(
                () => AtomicPerformanceArtifactWriter.Write(
                    path,
                    stream =>
                    {
                        stream.Write(Encoding.UTF8.GetBytes("partial"));
                        throw new InvalidOperationException("injected write failure");
                    }),
                "injected write failure");
            ContractAssert.Equal(false, File.Exists(path));
            ContractAssert.Equal(0, Directory.GetFiles(directory, "*.partial").Length);
        }
        finally
        {
            Directory.Delete(directory, recursive: true);
        }
    }

    private static void GivenAtomicArtifact_WhenPublished_ThenSealedHashAndSizeMatch()
    {
        var directory = CreateTemporaryDirectory();
        var path = Path.Combine(directory, "run.raw.json");
        var payload = Encoding.UTF8.GetBytes("{\"schema_id\":\"fixture\"}");
        try
        {
            var seal = AtomicPerformanceArtifactWriter.Write(
                path,
                stream => stream.Write(payload));

            ContractAssert.Equal(payload.LongLength, seal.SizeBytes);
            ContractAssert.Equal(64, seal.Sha256.Length);
            ContractAssert.SequenceEqual(payload, File.ReadAllBytes(path));
        }
        finally
        {
            Directory.Delete(directory, recursive: true);
        }
    }

    private static void GivenSealedManifestAfterMoveFailure_WhenRetried_ThenIdenticalPayloadCanResume()
    {
        var directory = CreateTemporaryDirectory();
        var path = Path.Combine(directory, "suite.manifest.json");
        var payload = Encoding.UTF8.GetBytes("{\"status\":\"pass\"}");
        try
        {
            var first = AtomicPerformanceArtifactWriter.WriteOrValidate(path, payload);
            var retry = AtomicPerformanceArtifactWriter.WriteOrValidate(path, payload);

            ContractAssert.Equal(first.Sha256, retry.Sha256);
            ContractAssert.Equal(first.SizeBytes, retry.SizeBytes);
            ContractAssert.Throws<PerformanceArtifactException>(
                () => AtomicPerformanceArtifactWriter.WriteOrValidate(
                    path,
                    Encoding.UTF8.GetBytes("{\"status\":\"fail\"}")),
                $"Existing performance artifact does not match retry payload: '{Path.GetFullPath(path)}'.");
        }
        finally
        {
            Directory.Delete(directory, recursive: true);
        }
    }

    private static void GivenRetrySessionWithUnsealedRootFile_WhenValidated_ThenFinalizationIsRejected()
    {
        // Given: A structurally complete retry root plus failure evidence written after a move failure.
        var session = CreateTemporaryDirectory();
        Directory.CreateDirectory(Path.Combine(session, "runs"));
        foreach (var file in new[]
        {
            "profile.actual.json",
            "profile.final.json",
            "provenance.json",
            "process-exits.json",
            "suite.manifest.json",
            "failure.json",
        })
        {
            File.WriteAllText(Path.Combine(session, file), "{}", Encoding.UTF8);
        }

        try
        {
            // When/Then: An unsealed root file prevents the retry from moving the directory.
            ContractAssert.Throws<PerformanceArtifactException>(
                () => PerformanceSessionFileSet.ValidateRoot(session),
                "Performance session root contains an unsealed or unexpected file.");
        }
        finally
        {
            Directory.Delete(session, recursive: true);
        }
    }

    private static void GivenDryRunManifest_WhenCreated_ThenOfficialAcceptanceCannotBeClaimed()
    {
        var manifest = PerformanceSuiteManifest.CreateDryRun(
            "session-fixture",
            "0123456789012345678901234567890123456789",
            [],
            new PerformanceIdentityReference("profile.actual.json", 10, new string('a', 64)),
            new PerformanceIdentityReference("profile.final.json", 10, new string('a', 64)),
            new PerformanceIdentityReference("provenance.json", 10, new string('b', 64)),
            new PerformanceIdentityReference("process-exits.json", 10, new string('c', 64)));

        ContractAssert.Equal(true, manifest.DevelopmentOnly);
        ContractAssert.Equal(false, manifest.OfficialEligible);
        ContractAssert.Equal(false, manifest.OfficialAcceptance);
        ContractAssert.Equal(false, manifest.MaySubstituteOfficial);
        ContractAssert.Equal("not_evaluated", manifest.Status);
    }

    private static IEnumerable<string> OfficialRunArguments() =>
    [
        "perf",
        "official",
        "--scenario", "hard-scatter",
        "--run-index", "1",
        "--repository-root", "repository",
        "--output-root", "output-root",
        "--output", "run.raw.json",
        "--reference-profile", "reference.json",
        "--observed-profile", "observed.json",
        "--provenance", "provenance.json",
    ];

    private static PerformanceRawRun CreatePassingHardRun(PerformanceScenario scenario)
    {
        const int frequency = 10_000_000;
        const long windowStart = 1_000_000;
        const long windowEnd = windowStart + (60L * frequency);
        var scatterCompletions = EvenTicks(windowStart, windowEnd, 1_800);
        var gmiCompletions = scenario == PerformanceScenario.HardCombined
            ? EvenTicks(windowStart, windowEnd, 300)
            : [];

        return new PerformanceRawRun(
            SchemaId: "analogboard.scatter-rendering.raw-run.v1",
            RunnerContractId: "AB-PERF-RUNNER-v1",
            Mode: PerformanceExecutionMode.Official,
            Scenario: scenario,
            RunIndex: 1,
            OfficialEligible: true,
            StopwatchFrequency: frequency,
            WindowStartTick: windowStart,
            WindowEndTick: windowEnd,
            ScatterFrameDurationTicks: RepeatTicks(1_800, 33.3),
            ScatterCompletionTicks: scatterCompletions,
            GmiCompletionTicks: gmiCompletions,
            InputLatencyTicks: RepeatTicks(1_200, 100.0),
            ScatterPublicationDurationTicks: RepeatTicks(1_800, 1.0),
            GmiPublicationDurationTicks: scenario == PerformanceScenario.HardCombined
                ? RepeatTicks(300, 1.0)
                : [],
            PendingFrameMaximum: 1,
            PendingCallbackMaximum: 1,
            CoalescedFrameCount: 0,
            OverwrittenSampleCount: 0,
            ProcessAllocatedBytes: 1_800L * 64 * 1024,
            AllocationProbeFrameCount: 32,
            OneEventAllocationProbeBytes: 32L * 1_024,
            HardEventAllocationProbeBytes: 32L * 9 * 1_024,
            RetainedManagedHeapGrowthBytes: 0,
            StartingPrivateBytes: 0,
            PrivateBytesGrowth: 0)
        {
            PlannedWindowDurationTicks = 60L * frequency,
            InputCadenceHz = 20,
            EventAllocationProbeDeltaBytes = 32L * 8 * 1024,
        };
    }

    private static PerformanceRawRun CreatePassingSoak()
    {
        const int frequency = 10_000_000;
        const long windowStart = 1_000_000;
        const long windowEnd = windowStart + (600L * frequency);
        return CreatePassingHardRun(PerformanceScenario.HardCombined) with
        {
            Scenario = PerformanceScenario.Soak,
            WindowEndTick = windowEnd,
            ScatterFrameDurationTicks = RepeatTicks(18_000, 33.3),
            ScatterCompletionTicks = EvenTicks(windowStart, windowEnd, 18_000),
            GmiCompletionTicks = EvenTicks(windowStart, windowEnd, 3_000),
            InputLatencyTicks = RepeatTicks(12_000, 100.0),
            ScatterPublicationDurationTicks = RepeatTicks(18_000, 1.0),
            GmiPublicationDurationTicks = RepeatTicks(3_000, 1.0),
            ProcessAllocatedBytes = 18_000L * 64 * 1024,
            RetainedManagedHeapGrowthBytes = 8L * 1024 * 1024,
            StartingPrivateBytes = 256L * 1024 * 1024,
            PrivateBytesGrowth = 32L * 1024 * 1024,
            PlannedWindowDurationTicks = 600L * frequency,
        };
    }

    private static long[] EvenTicks(long start, long end, int count)
    {
        var values = new long[count];
        for (var index = 0; index < count; index++)
        {
            values[index] = start + ((index + 1L) * (end - start) / (count + 1L));
        }

        return values;
    }

    private static long[] RepeatTicks(int count, double milliseconds) =>
        Enumerable.Repeat(MillisecondsToTicks(milliseconds), count).ToArray();

    private static long MillisecondsToTicks(double milliseconds) =>
        checked((long)Math.Round(milliseconds * 10_000.0));

    private static string CreateProfileJson(string status, bool remoteSession) =>
        "{" +
        "\"schema_id\":\"analogboard.scatter-rendering.reference-profile.v1\"," +
        "\"profile_id\":\"AB-PERF-REF-v1\"," +
        $"\"profile_status\":\"{status}\"," +
        "\"owner_approval_id\":\"owner-approved-fixture\"," +
        "\"manufacturer\":\"Dell Inc.\"," +
        "\"model\":\"Precision 3680\"," +
        "\"machine_name\":\"ANALYZER_S1\"," +
        "\"os_product\":\"Microsoft Windows 11 Pro\"," +
        "\"os_version\":\"10.0.26200\"," +
        "\"os_build\":\"26200\"," +
        "\"cpu\":\"Intel(R) Core(TM) i9-14900\"," +
        "\"ram_bytes\":68390989824," +
        "\"gpu_name\":\"NVIDIA fixture\"," +
        "\"gpu_driver_version\":\"32.0.15.7283\"," +
        "\"display_width\":1920," +
        "\"display_height\":1080," +
        "\"display_refresh_hz\":60," +
        "\"display_dpi_x\":96," +
        "\"display_dpi_y\":96," +
        "\"power_scheme_guid\":\"381b4222-f694-41f0-9685-ff5bb260df2e\"," +
        "\"storage_model\":\"NVMe PC SN820 NVMe WD 4096GB\"," +
        "\"storage_serial\":\"fixture-serial\"," +
        "\"storage_bus_type\":\"NVMe\"," +
        "\"monotonic_clock\":\"System.Diagnostics.Stopwatch\"," +
        "\"stopwatch_frequency\":10000000," +
        "\"sdk_version\":\"10.0.302\"," +
        "\"desktop_runtime_version\":\"10.0.10\"," +
        "\"target_framework\":\"net10.0-windows\"," +
        "\"configuration\":\"Release\"," +
        "\"architecture\":\"x64\"," +
        $"\"remote_session\":{remoteSession.ToString().ToLowerInvariant()}" +
        "}";

    private static string CreateTemporaryDirectory()
    {
        var path = Path.Combine(
            Path.GetTempPath(),
            $"analogboard-performance-contract-{Guid.NewGuid():N}");
        Directory.CreateDirectory(path);
        return path;
    }

    private static string FindRepositoryRoot()
    {
        for (var current = new DirectoryInfo(AppContext.BaseDirectory);
             current is not null;
             current = current.Parent)
        {
            if (File.Exists(Path.Combine(current.FullName, "global.json")) &&
                Directory.Exists(Path.Combine(current.FullName, "prototypes", "scatter-rendering")))
            {
                return current.FullName;
            }
        }

        throw new InvalidOperationException("AnalogBoard repository root was not found from the test assembly.");
    }
}
