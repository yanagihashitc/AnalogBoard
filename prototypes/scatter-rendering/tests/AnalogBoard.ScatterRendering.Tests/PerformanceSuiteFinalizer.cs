using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using AnalogBoard.ScatterRendering.Core;

namespace AnalogBoard.ScatterRendering.Tests;

internal sealed record PerformanceFinalizeCommand(
    PerformanceExecutionMode Mode,
    string RepositoryRoot,
    string OutputRoot,
    string SessionDirectory,
    string? ReferenceProfilePath,
    string ObservedProfilePath,
    string FinalObservedProfilePath,
    string ProvenancePath,
    string ProcessExitsPath);

internal static class PerformanceFinalizeCommandLine
{
    public static PerformanceFinalizeCommand Parse(string[] args)
    {
        ArgumentNullException.ThrowIfNull(args);
        if (args.Length < 3 ||
            !StringComparer.Ordinal.Equals(args[0], "perf") ||
            !StringComparer.Ordinal.Equals(args[1], "finalize"))
        {
            throw new PerformanceCommandLineException(
                "Performance finalizer must start with 'perf finalize official' or 'perf finalize dry-run'.");
        }

        var mode = args[2] switch
        {
            "official" => PerformanceExecutionMode.Official,
            "dry-run" => PerformanceExecutionMode.DryRun,
            _ => throw new PerformanceCommandLineException(
                $"Unknown performance finalizer mode: '{args[2]}'."),
        };
        if ((args.Length - 3) % 2 != 0)
        {
            throw new PerformanceCommandLineException(
                "Every performance finalizer option requires one value.");
        }

        var options = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var index = 3; index < args.Length; index += 2)
        {
            if (!options.TryAdd(args[index], args[index + 1]))
            {
                throw new PerformanceCommandLineException(
                    $"Performance finalizer option must not be repeated: '{args[index]}'.");
            }
        }

        var allowed = new HashSet<string>(
            [
                "--session-dir",
                "--repository-root",
                "--output-root",
                "--reference-profile",
                "--observed-profile",
                "--final-observed-profile",
                "--provenance",
                "--process-exits",
            ],
            StringComparer.Ordinal);
        var unknown = options.Keys.FirstOrDefault(option => !allowed.Contains(option));
        if (unknown is not null)
        {
            throw new PerformanceCommandLineException(
                $"Unknown performance finalizer option: '{unknown}'.");
        }

        string Required(string name)
        {
            if (!options.TryGetValue(name, out var value) || string.IsNullOrWhiteSpace(value))
            {
                throw new PerformanceCommandLineException(
                    $"Performance finalizer option is required: '{name}'.");
            }

            return value;
        }

        options.TryGetValue("--reference-profile", out var referenceProfile);
        if (mode == PerformanceExecutionMode.Official &&
            string.IsNullOrWhiteSpace(referenceProfile))
        {
            throw new PerformanceCommandLineException(
                "Official performance finalizer requires --reference-profile.");
        }

        return new PerformanceFinalizeCommand(
            mode,
            Required("--repository-root"),
            Required("--output-root"),
            Required("--session-dir"),
            referenceProfile,
            Required("--observed-profile"),
            Required("--final-observed-profile"),
            Required("--provenance"),
            Required("--process-exits"));
    }
}

internal static class PerformanceSessionFileSet
{
    private static readonly string[] RequiredRootFiles =
    [
        "process-exits.json",
        "profile.actual.json",
        "profile.final.json",
        "provenance.json",
    ];

    public static void ValidateRoot(string sessionDirectory)
    {
        var root = Path.GetFullPath(sessionDirectory);
        var directories = Directory.GetDirectories(root, "*", SearchOption.TopDirectoryOnly)
            .Select(Path.GetFileName)
            .Order(StringComparer.Ordinal)
            .ToArray();
        if (!directories.SequenceEqual(["runs"], StringComparer.Ordinal))
        {
            throw new PerformanceArtifactException(
                "Performance session root contains an unsealed or unexpected directory.");
        }

        var files = Directory.GetFiles(root, "*", SearchOption.TopDirectoryOnly)
            .Select(Path.GetFileName)
            .Order(StringComparer.Ordinal)
            .ToArray();
        var initial = RequiredRootFiles.Order(StringComparer.Ordinal).ToArray();
        var retry = RequiredRootFiles.Append("suite.manifest.json")
            .Order(StringComparer.Ordinal)
            .ToArray();
        if (!files.SequenceEqual(initial, StringComparer.Ordinal) &&
            !files.SequenceEqual(retry, StringComparer.Ordinal))
        {
            throw new PerformanceArtifactException(
                "Performance session root contains an unsealed or unexpected file.");
        }
    }
}

internal static class PerformanceSuiteFinalizer
{
    private static readonly (string FileName, PerformanceScenario Scenario, int RunIndex)[] ExpectedRuns =
    [
        ("hard-scatter-01.raw.json", PerformanceScenario.HardScatter, 1),
        ("hard-scatter-02.raw.json", PerformanceScenario.HardScatter, 2),
        ("hard-scatter-03.raw.json", PerformanceScenario.HardScatter, 3),
        ("hard-combined-01.raw.json", PerformanceScenario.HardCombined, 1),
        ("hard-combined-02.raw.json", PerformanceScenario.HardCombined, 2),
        ("hard-combined-03.raw.json", PerformanceScenario.HardCombined, 3),
        ("soak-01.raw.json", PerformanceScenario.Soak, 1),
        ("headroom-01.raw.json", PerformanceScenario.Headroom, 1),
    ];

    public static int Execute(PerformanceFinalizeCommand command)
    {
        ArgumentNullException.ThrowIfNull(command);
        var sessionDirectory = PerformancePathAuthority.ValidateFinalize(command);
        if (!sessionDirectory.EndsWith(".inprogress", StringComparison.Ordinal) ||
            !Directory.Exists(sessionDirectory))
        {
            throw new PerformanceArtifactException(
                "Performance session directory must exist and end with '.inprogress'.");
        }
        var finalDirectory = sessionDirectory[..^".inprogress".Length];
        if (Directory.Exists(finalDirectory))
        {
            throw new PerformanceArtifactException(
                $"Final performance session already exists: '{finalDirectory}'.");
        }

        PerformanceSessionFileSet.ValidateRoot(sessionDirectory);
        ValidateRunFileSet(sessionDirectory);

        var observedBytes = File.ReadAllBytes(command.ObservedProfilePath);
        var observedJson = Encoding.UTF8.GetString(observedBytes);
        var observed = PerformanceProfilePreflight.ParseObserved(observedJson);
        var observedHash = Sha256(observedBytes);
        var finalObservedBytes = File.ReadAllBytes(command.FinalObservedProfilePath);
        var finalObservedJson = Encoding.UTF8.GetString(finalObservedBytes);
        _ = PerformanceProfilePreflight.ParseObserved(finalObservedJson);
        var finalObservedHash = Sha256(finalObservedBytes);
        if (!observedBytes.AsSpan().SequenceEqual(finalObservedBytes))
        {
            throw new PerformanceProfileException(
                "Live performance profile changed during the suite.");
        }
        bool profileEligible;
        string? referenceHash = null;
        PerformanceIdentityReference? referenceIdentity = null;
        if (command.Mode == PerformanceExecutionMode.Official)
        {
            var referenceBytes = File.ReadAllBytes(command.ReferenceProfilePath!);
            var referenceJson = Encoding.UTF8.GetString(referenceBytes);
            var reference = PerformanceProfilePreflight.ParseReference(referenceJson);
            var preflight = PerformanceProfilePreflight.Compare(reference, observed);
            if (!preflight.OfficialEligible)
            {
                throw new PerformanceProfileException(
                    $"Reference profile changed or no longer matches: {string.Join(",", preflight.Mismatches)}.");
            }

            referenceHash = Sha256(referenceBytes);
            referenceIdentity = new PerformanceIdentityReference(
                "docs/reference/scatter-rendering/phase0/performance-reference-profile-v1.json",
                referenceBytes.LongLength,
                referenceHash);
            profileEligible = true;
        }
        else
        {
            profileEligible = false;
        }

        var provenanceBytes = File.ReadAllBytes(command.ProvenancePath);
        var provenanceJson = Encoding.UTF8.GetString(provenanceBytes);
        var provenance = PerformanceRunProvenance.Parse(provenanceJson, command.Mode);
        if (command.Mode == PerformanceExecutionMode.Official)
        {
            PerformanceGitAuthority.RequireOfficialState(
                command.RepositoryRoot,
                provenance.SourceRevision,
                provenance.SourceDirty,
                command.ReferenceProfilePath!);
        }
        else
        {
            PerformanceGitAuthority.RequireHead(command.RepositoryRoot, provenance.SourceRevision);
        }
        var provenanceHash = Sha256(provenanceBytes);
        var processExitsBytes = File.ReadAllBytes(command.ProcessExitsPath);
        PerformanceJsonValidation.RejectDuplicateProperties(
            Encoding.UTF8.GetString(processExitsBytes),
            "Performance process exit ledger");
        var processExits = PerformanceArtifactJson.DeserializeRequired<PerformanceProcessExitLedger>(
            processExitsBytes,
            "Performance process exit ledger");
        var exitRecords = ValidateProcessExits(processExits);
        var processExitsIdentity = new PerformanceIdentityReference(
            "process-exits.json",
            processExitsBytes.LongLength,
            Sha256(processExitsBytes));
        var observedIdentity = new PerformanceIdentityReference(
            "profile.actual.json",
            observedBytes.LongLength,
            observedHash);
        var finalObservedIdentity = new PerformanceIdentityReference(
            "profile.final.json",
            finalObservedBytes.LongLength,
            finalObservedHash);
        var provenanceIdentity = new PerformanceIdentityReference(
            "provenance.json",
            provenanceBytes.LongLength,
            provenanceHash);
        var references = new List<PerformanceArtifactReference>();
        var artifacts = new List<PerformanceRawArtifact>();
        foreach (var expected in ExpectedRuns)
        {
            var path = Path.Combine(sessionDirectory, "runs", expected.FileName);
            var bytes = File.ReadAllBytes(path);
            PerformanceJsonValidation.RejectDuplicateProperties(
                Encoding.UTF8.GetString(bytes),
                "Performance raw artifact");
            var artifact = PerformanceArtifactJson.DeserializeRequired<PerformanceRawArtifact>(
                bytes,
                $"Performance raw artifact {expected.FileName}");
            ValidateArtifact(
                command,
                artifact,
                expected.Scenario,
                expected.RunIndex,
                observedHash,
                referenceHash,
                provenanceHash,
                provenance.SourceRevision,
                provenance.SourceDirty,
                (string)observed.Identity["machine_name"],
                (long)observed.Identity["stopwatch_frequency"],
                exitRecords[(expected.Scenario, expected.RunIndex)]);
            references.Add(new PerformanceArtifactReference(
                Path.Combine("runs", expected.FileName).Replace('\\', '/'),
                bytes.LongLength,
                Sha256(bytes),
                artifact.ProcessId,
                ProcessExitCode: exitRecords[(expected.Scenario, expected.RunIndex)].ExitCode,
                artifact.Raw.Scenario,
                artifact.Raw.RunIndex,
                artifact.Verdict.Status));
            artifacts.Add(artifact);
        }

        if (artifacts.Select(artifact => artifact.ProcessId).Distinct().Count() != ExpectedRuns.Length)
        {
            throw new PerformanceArtifactException(
                "Every performance scenario run must come from a distinct child process.");
        }

        PerformanceSuiteManifest manifest;
        if (command.Mode == PerformanceExecutionMode.DryRun)
        {
            manifest = PerformanceSuiteManifest.CreateDryRun(
                Path.GetFileNameWithoutExtension(sessionDirectory),
                provenance.SourceRevision,
                references,
                observedIdentity,
                finalObservedIdentity,
                provenanceIdentity,
                processExitsIdentity);
        }
        else
        {
            var scatter = artifacts
                .Where(artifact => artifact.Raw.Scenario == PerformanceScenario.HardScatter)
                .OrderBy(artifact => artifact.Raw.RunIndex)
                .Select(artifact => artifact.Verdict)
                .ToArray();
            var combined = artifacts
                .Where(artifact => artifact.Raw.Scenario == PerformanceScenario.HardCombined)
                .OrderBy(artifact => artifact.Raw.RunIndex)
                .Select(artifact => artifact.Verdict)
                .ToArray();
            var soak = artifacts.Single(
                artifact => artifact.Raw.Scenario == PerformanceScenario.Soak).Verdict;
            var suiteVerdict = PerformanceSuiteEvaluator.EvaluateOfficial(
                scatter,
                combined,
                soak,
                headroomObserved: artifacts.Any(
                    artifact => artifact.Raw.Scenario == PerformanceScenario.Headroom &&
                                artifact.Verdict.Status == PerformanceVerdictStatus.Observed),
                profileEligible,
                artifacts.Select(artifact => artifact.ProcessId).ToArray());
            if (!suiteVerdict.OfficialAcceptance)
            {
                throw new PerformanceArtifactException(
                    $"Official performance suite cannot be finalized: {suiteVerdict.Status}.");
            }

            manifest = PerformanceSuiteManifest.CreateOfficial(
                Path.GetFileNameWithoutExtension(sessionDirectory),
                provenance.SourceRevision,
                references,
                referenceIdentity!,
                observedIdentity,
                finalObservedIdentity,
                provenanceIdentity,
                processExitsIdentity);
        }

        var manifestPath = Path.Combine(sessionDirectory, "suite.manifest.json");
        var manifestBytes = JsonSerializer.SerializeToUtf8Bytes(
            manifest,
            PerformanceArtifactJson.Options);
        var manifestSeal = AtomicPerformanceArtifactWriter.WriteOrValidate(
            manifestPath,
            manifestBytes);
        Directory.Move(sessionDirectory, finalDirectory);
        Console.WriteLine(
            $"PERFORMANCE_SUITE path={finalDirectory} mode={command.Mode} status={manifest.Status} " +
            $"official_acceptance={manifest.OfficialAcceptance.ToString().ToLowerInvariant()} " +
            $"manifest_sha256={manifestSeal.Sha256} manifest_size={manifestSeal.SizeBytes}");
        return PerformanceExitCodes.Success;
    }

    private static void ValidateArtifact(
        PerformanceFinalizeCommand command,
        PerformanceRawArtifact artifact,
        PerformanceScenario expectedScenario,
        int expectedRunIndex,
        string observedHash,
        string? referenceHash,
        string provenanceHash,
        string sourceRevision,
        bool sourceDirty,
        string machineName,
        long observedStopwatchFrequency,
        PerformanceProcessExitRecord processExit)
    {
        if (artifact.Workload is null ||
            artifact.Schedule is null ||
            artifact.LoadedRuntime is null ||
            artifact.Raw is null ||
            artifact.Diagnostics is null ||
            artifact.Verdict is null)
        {
            throw new PerformanceArtifactException(
                $"Performance raw artifact contains a null contract object: {expectedScenario} run {expectedRunIndex}.");
        }

        if (!StringComparer.Ordinal.Equals(
                artifact.SchemaId,
                "analogboard.scatter-rendering.raw-artifact.v1") ||
            !StringComparer.Ordinal.Equals(artifact.RunnerContractId, "AB-PERF-RUNNER-v1") ||
            !StringComparer.Ordinal.Equals(
                artifact.Raw.SchemaId,
                "analogboard.scatter-rendering.raw-run.v1") ||
            !StringComparer.Ordinal.Equals(artifact.Raw.RunnerContractId, "AB-PERF-RUNNER-v1") ||
            artifact.Raw.Mode != command.Mode ||
            artifact.Raw.Scenario != expectedScenario ||
            artifact.Raw.RunIndex != expectedRunIndex ||
            artifact.Raw.StopwatchFrequency != observedStopwatchFrequency ||
            artifact.Schedule.StopwatchFrequency != observedStopwatchFrequency ||
            artifact.OfficialAcceptance ||
            artifact.MaySubstituteOfficial ||
            artifact.ProcessId <= 0 ||
            artifact.ProcessId != processExit.ProcessId ||
            processExit.ExitCode != PerformanceExitCodes.Success ||
            !StringComparer.Ordinal.Equals(artifact.ObservedProfileSha256, observedHash) ||
            !StringComparer.Ordinal.Equals(artifact.ReferenceProfileSha256, referenceHash) ||
            !StringComparer.Ordinal.Equals(artifact.ProvenanceSha256, provenanceHash) ||
            !StringComparer.Ordinal.Equals(artifact.SourceRevision, sourceRevision) ||
            artifact.SourceDirty != sourceDirty ||
            !StringComparer.Ordinal.Equals(artifact.MachineName, machineName) ||
            !StringComparer.Ordinal.Equals(artifact.FixtureId, FixtureIdFor(expectedScenario)) ||
            artifact.StartedAtUtc == default ||
            artifact.FinishedAtUtc < artifact.StartedAtUtc ||
            !StringComparer.Ordinal.Equals(
                artifact.LoadedRuntime.CoreRuntimeInformationalVersion,
                "10.0.10") ||
            !StringComparer.Ordinal.Equals(
                artifact.LoadedRuntime.DesktopRuntimeInformationalVersion,
                "10.0.10") ||
            !StringComparer.Ordinal.Equals(
                artifact.LoadedRuntime.RollForwardPolicy,
                "Disable") ||
            !artifact.LoadedRuntime.FrameworkDescription.Contains(
                "10.0.10",
                StringComparison.Ordinal) ||
            artifact.Workload != PerformanceWorkloadContract.Create(expectedScenario))
        {
            throw new PerformanceArtifactException(
                $"Performance raw artifact identity mismatch: {expectedScenario} run {expectedRunIndex}.");
        }

        var isHeadroom = expectedScenario == PerformanceScenario.Headroom;
        var officialEligible = PerformanceRunEligibility.IsEligible(
            command.Mode,
            expectedScenario,
            profileEligible: true);
        if (artifact.DevelopmentOnly != (command.Mode == PerformanceExecutionMode.DryRun) ||
            artifact.OfficialEligible != officialEligible ||
            artifact.Raw.OfficialEligible != officialEligible)
        {
            throw new PerformanceArtifactException(
                $"Performance raw artifact eligibility mismatch: {expectedScenario} run {expectedRunIndex}.");
        }

        ValidateRawArrays(artifact.Raw, artifact.Verdict);
        ValidateSchedule(command.Mode, expectedScenario, artifact.Schedule, artifact.Raw);
        if (isHeadroom)
        {
            ValidateHeadroomRaw(artifact.Raw);
        }
        var recalculated = PerformanceRunEvaluator.Evaluate(artifact.Raw);
        if (recalculated.Status != artifact.Verdict.Status ||
            recalculated.Scenario != artifact.Verdict.Scenario ||
            recalculated.RunIndex != artifact.Verdict.RunIndex ||
            recalculated.OfficialCandidate != artifact.Verdict.OfficialCandidate ||
            !recalculated.FailedMetrics.SequenceEqual(artifact.Verdict.FailedMetrics) ||
            !recalculated.IncompleteReasons.SequenceEqual(artifact.Verdict.IncompleteReasons))
        {
            throw new PerformanceArtifactException(
                $"Performance raw artifact verdict is not reproducible: {expectedScenario} run {expectedRunIndex}.");
        }
        if (command.Mode == PerformanceExecutionMode.DryRun &&
            recalculated.Status == PerformanceVerdictStatus.Incomplete)
        {
            throw new PerformanceArtifactException(
                $"Dry-run must exercise a structurally complete raw artifact: {expectedScenario} run {expectedRunIndex}.");
        }

        ValidateDiagnostics(artifact.Diagnostics, artifact.Raw, isHeadroom);
    }

    private static IReadOnlyDictionary<(PerformanceScenario Scenario, int RunIndex), PerformanceProcessExitRecord>
        ValidateProcessExits(PerformanceProcessExitLedger ledger)
    {
        if (!StringComparer.Ordinal.Equals(
                ledger.SchemaId,
                "analogboard.scatter-rendering.process-exits.v1") ||
            !StringComparer.Ordinal.Equals(ledger.RunnerContractId, "AB-PERF-RUNNER-v1") ||
            ledger.Records is null ||
            ledger.Records.Count != ExpectedRuns.Length)
        {
            throw new PerformanceArtifactException(
                "Performance process exit ledger identity or count mismatch.");
        }

        var records = new Dictionary<
            (PerformanceScenario Scenario, int RunIndex),
            PerformanceProcessExitRecord>();
        foreach (var record in ledger.Records)
        {
            var scenario = record.Scenario switch
            {
                "hard-scatter" => PerformanceScenario.HardScatter,
                "hard-combined" => PerformanceScenario.HardCombined,
                "soak" => PerformanceScenario.Soak,
                "headroom" => PerformanceScenario.Headroom,
                _ => throw new PerformanceArtifactException(
                    $"Unknown performance process exit scenario: '{record.Scenario}'."),
            };
            if (record.ProcessId <= 0 ||
                record.ExitCode != PerformanceExitCodes.Success ||
                !records.TryAdd((scenario, record.RunIndex), record))
            {
                throw new PerformanceArtifactException(
                    "Performance process exit ledger contains an invalid or duplicate record.");
            }
        }

        if (ExpectedRuns.Any(expected => !records.ContainsKey((expected.Scenario, expected.RunIndex))))
        {
            throw new PerformanceArtifactException(
                "Performance process exit ledger does not contain the exact expected run set.");
        }

        return records;
    }

    private static void ValidateHeadroomRaw(PerformanceRawRun raw)
    {
        if (raw.ScatterFrameDurationTicks.Length == 0 ||
            raw.ScatterFrameDurationTicks.Any(value => value <= 0) ||
            raw.OverwrittenSampleCount != 0 ||
            raw.ScatterCompletionTicks.Length != 0 ||
            raw.GmiCompletionTicks.Length != 0 ||
            raw.InputLatencyTicks.Length != 0 ||
            raw.ScatterPublicationDurationTicks.Length != 0 ||
            raw.GmiPublicationDurationTicks.Length != 0 ||
            raw.PendingFrameMaximum != 0 ||
            raw.PendingCallbackMaximum != 0 ||
            raw.CoalescedFrameCount != 0 ||
            raw.ProcessAllocatedBytes != 0 ||
            raw.AllocationProbeFrameCount != 1 ||
            raw.OneEventAllocationProbeBytes != 0 ||
            raw.HardEventAllocationProbeBytes != 0 ||
            raw.RetainedManagedHeapGrowthBytes != 0 ||
            raw.StartingPrivateBytes != 0 ||
            raw.PrivateBytesGrowth != 0 ||
            raw.InputCadenceHz != 0 ||
            raw.SchedulerFailureCount != 0 ||
            raw.PendingWorkAtEnd != 0 ||
            raw.MetricSampleCountMismatch != 0 ||
            raw.EventAllocationProbeDeltaBytes != 0 ||
            raw.AllocationProbeInvalidPairCount != 0)
        {
            throw new PerformanceArtifactException(
                "Headroom raw artifact must remain a structurally complete observation without hard-gate metrics.");
        }
    }

    private static void ValidateRawArrays(
        PerformanceRawRun raw,
        PerformanceRunVerdict verdict)
    {
        if (raw.ScatterFrameDurationTicks is null ||
            raw.ScatterCompletionTicks is null ||
            raw.GmiCompletionTicks is null ||
            raw.InputLatencyTicks is null ||
            raw.ScatterPublicationDurationTicks is null ||
            raw.GmiPublicationDurationTicks is null ||
            verdict.FailedMetrics is null ||
            verdict.IncompleteReasons is null)
        {
            throw new PerformanceArtifactException(
                "Performance raw metric and verdict arrays must not be null.");
        }
    }

    private static void ValidateRunFileSet(string sessionDirectory)
    {
        var runsDirectory = Path.Combine(sessionDirectory, "runs");
        if (!Directory.Exists(runsDirectory))
        {
            throw new PerformanceArtifactException(
                "Performance session runs directory is absent.");
        }

        var expected = ExpectedRuns
            .Select(run => run.FileName)
            .Order(StringComparer.Ordinal)
            .ToArray();
        var actual = Directory.GetFiles(runsDirectory, "*", SearchOption.TopDirectoryOnly)
            .Select(Path.GetFileName)
            .Order(StringComparer.Ordinal)
            .ToArray();
        if (!actual.SequenceEqual(expected, StringComparer.Ordinal))
        {
            throw new PerformanceArtifactException(
                "Performance session must contain exactly the eight versioned raw artifacts.");
        }
    }

    private static string FixtureIdFor(PerformanceScenario scenario) => scenario switch
    {
        PerformanceScenario.HardScatter => "AB-P0-R1-HARD-SCATTER-v1",
        PerformanceScenario.HardCombined => "AB-P0-R1-HARD-COMBINED-v1",
        PerformanceScenario.Soak => "AB-P0-R1-HARD-COMBINED-v1",
        PerformanceScenario.Headroom => "AB-P0-R1-HEADROOM-v1",
        _ => throw new PerformanceArtifactException(
            $"Unsupported performance scenario: {scenario}."),
    };

    private static void ValidateSchedule(
        PerformanceExecutionMode mode,
        PerformanceScenario scenario,
        PerformanceScheduleEvidence schedule,
        PerformanceRawRun raw)
    {
        if (schedule.StopwatchFrequency <= 0 ||
            schedule.StopwatchFrequency != raw.StopwatchFrequency ||
            schedule.HardRunCount != 3 ||
            schedule.PlannedWarmupDurationTicks <= 0 ||
            schedule.ActualWarmupDurationTicks < schedule.PlannedWarmupDurationTicks ||
            schedule.PlannedWindowDurationTicks <= 0 ||
            schedule.PlannedWindowDurationTicks != raw.PlannedWindowDurationTicks ||
            schedule.ActualWindowDurationTicks != raw.WindowEndTick - raw.WindowStartTick ||
            schedule.ActualWindowDurationTicks < schedule.PlannedWindowDurationTicks)
        {
            throw new PerformanceArtifactException(
                $"Performance raw artifact schedule mismatch: {scenario} run {raw.RunIndex}.");
        }

        var frequency = schedule.StopwatchFrequency;
        if (mode == PerformanceExecutionMode.Official)
        {
            var expectedWarmup = DurationTicks(30, frequency);
            var expectedWindow = DurationTicks(
                scenario == PerformanceScenario.Soak ? 600 : 60,
                frequency);
            if (schedule.PlannedWarmupDurationTicks != expectedWarmup ||
                schedule.PlannedWindowDurationTicks != expectedWindow)
            {
                throw new PerformanceArtifactException(
                    $"Official performance schedule mismatch: {scenario} run {raw.RunIndex}.");
            }
        }
        else
        {
            var maximumDryRun = DurationTicks(10, frequency);
            if (schedule.PlannedWarmupDurationTicks > maximumDryRun ||
                schedule.PlannedWindowDurationTicks > maximumDryRun)
            {
                throw new PerformanceArtifactException(
                    $"Dry-run performance schedule exceeds its contract: {scenario} run {raw.RunIndex}.");
            }
        }
    }

    private static long DurationTicks(int seconds, long frequency)
    {
        if (frequency <= 0 || frequency > long.MaxValue / seconds)
        {
            throw new PerformanceArtifactException(
                "Performance schedule stopwatch frequency is out of range.");
        }

        return seconds * frequency;
    }

    private static void ValidateDiagnostics(
        PerformanceRawDiagnostics diagnostics,
        PerformanceRawRun raw,
        bool isHeadroom)
    {
        if (diagnostics.ScatterGeneration is null ||
            diagnostics.ScatterProducerStartTicks is null ||
            diagnostics.ScatterRenderCompletionTicks is null ||
            diagnostics.GmiGeneration is null ||
            diagnostics.GmiProducerStartTicks is null ||
            diagnostics.GmiRenderCompletionTicks is null ||
            diagnostics.InputEnqueueTicks is null ||
            diagnostics.InputCompletionTicks is null ||
            diagnostics.MemorySampleTicks is null ||
            diagnostics.ManagedHeapSamples is null ||
            diagnostics.PrivateByteSamples is null ||
            diagnostics.OneEventAllocationProbeSamples is null ||
            diagnostics.HardEventAllocationProbeSamples is null ||
            diagnostics.PairedAllocationDeltaSamples is null)
        {
            throw new PerformanceArtifactException(
                "Performance raw diagnostic arrays must not be null.");
        }

        if (isHeadroom)
        {
            if (diagnostics.ScatterGeneration.Length != 0 ||
                diagnostics.ScatterProducerStartTicks.Length != 0 ||
                diagnostics.ScatterRenderCompletionTicks.Length != 0 ||
                diagnostics.GmiGeneration.Length != 0 ||
                diagnostics.GmiProducerStartTicks.Length != 0 ||
                diagnostics.GmiRenderCompletionTicks.Length != 0 ||
                diagnostics.InputEnqueueTicks.Length != 0 ||
                diagnostics.InputCompletionTicks.Length != 0 ||
                diagnostics.MemorySampleTicks.Length != 0 ||
                diagnostics.ManagedHeapSamples.Length != 0 ||
                diagnostics.PrivateByteSamples.Length != 0 ||
                diagnostics.OneEventAllocationProbeSamples.Length != 0 ||
                diagnostics.HardEventAllocationProbeSamples.Length != 0 ||
                diagnostics.PairedAllocationDeltaSamples.Length != 0)
            {
                throw new PerformanceArtifactException(
                    "Headroom diagnostic arrays must remain empty and observational.");
            }

            return;
        }

        if (diagnostics.ScatterGeneration.Length != diagnostics.ScatterProducerStartTicks.Length ||
            diagnostics.ScatterGeneration.Length != diagnostics.ScatterRenderCompletionTicks.Length ||
            diagnostics.GmiGeneration.Length != diagnostics.GmiProducerStartTicks.Length ||
            diagnostics.GmiGeneration.Length != diagnostics.GmiRenderCompletionTicks.Length ||
            diagnostics.InputEnqueueTicks.Length != diagnostics.InputCompletionTicks.Length)
        {
            throw new PerformanceArtifactException(
                "Performance raw diagnostic arrays have inconsistent lengths.");
        }

        if (diagnostics.MemorySampleTicks.Length != diagnostics.ManagedHeapSamples.Length ||
            diagnostics.MemorySampleTicks.Length != diagnostics.PrivateByteSamples.Length)
        {
            throw new PerformanceArtifactException(
                "Performance memory diagnostic arrays have inconsistent lengths.");
        }

        if (raw.WindowStartTick > long.MaxValue - raw.PlannedWindowDurationTicks)
        {
            throw new PerformanceArtifactException(
                "Performance diagnostic measurement window overflows the monotonic clock range.");
        }
        var plannedWindowEnd = raw.WindowStartTick + raw.PlannedWindowDurationTicks;
        var scatterDurations = diagnostics.ScatterProducerStartTicks
            .Zip(
                diagnostics.ScatterRenderCompletionTicks,
                (start, completion) => Math.Max(1, completion - start))
            .ToArray();
        var scatterInWindow = diagnostics.ScatterRenderCompletionTicks
            .Where(completion => completion < plannedWindowEnd)
            .ToArray();
        var gmiInWindow = diagnostics.GmiRenderCompletionTicks
            .Where(completion => completion < plannedWindowEnd)
            .ToArray();
        var inputLatencies = diagnostics.InputEnqueueTicks
            .Zip(
                diagnostics.InputCompletionTicks,
                (enqueue, completion) => completion > 0 ? completion - enqueue : 0)
            .ToArray();
        if (!scatterDurations.SequenceEqual(raw.ScatterFrameDurationTicks) ||
            !scatterInWindow.SequenceEqual(raw.ScatterCompletionTicks) ||
            !gmiInWindow.SequenceEqual(raw.GmiCompletionTicks) ||
            !inputLatencies.SequenceEqual(raw.InputLatencyTicks))
        {
            throw new PerformanceArtifactException(
                "Performance raw diagnostic timestamps do not reproduce metric cohorts.");
        }

        if (!StrictlyIncreasing(diagnostics.ScatterGeneration) ||
            !StrictlyIncreasing(diagnostics.GmiGeneration) ||
            !StartsAndCompletionsAreValid(
                diagnostics.ScatterProducerStartTicks,
                diagnostics.ScatterRenderCompletionTicks,
                raw.WindowStartTick,
                plannedWindowEnd) ||
            !StartsAndCompletionsAreValid(
                diagnostics.GmiProducerStartTicks,
                diagnostics.GmiRenderCompletionTicks,
                raw.WindowStartTick,
                plannedWindowEnd) ||
            !StartsAndCompletionsAreValid(
                diagnostics.InputEnqueueTicks,
                diagnostics.InputCompletionTicks,
                raw.WindowStartTick,
                plannedWindowEnd) ||
            !OrderedWithinInclusive(
                diagnostics.MemorySampleTicks,
                raw.WindowStartTick,
                plannedWindowEnd))
        {
            throw new PerformanceArtifactException(
                "Performance diagnostic clocks or generations violate the measurement window contract.");
        }

        if (diagnostics.ScatterAcceptedCount != raw.ScatterPublicationDurationTicks.LongLength ||
            diagnostics.ScatterRenderedCount != diagnostics.ScatterGeneration.LongLength ||
            diagnostics.ScatterAcceptedCount !=
                diagnostics.ScatterRenderedCount + diagnostics.ScatterCoalescedCount ||
            diagnostics.GmiAcceptedCount != raw.GmiPublicationDurationTicks.LongLength ||
            diagnostics.GmiRenderedCount != diagnostics.GmiGeneration.LongLength ||
            diagnostics.GmiAcceptedCount !=
                diagnostics.GmiRenderedCount + diagnostics.GmiCoalescedCount ||
            raw.CoalescedFrameCount !=
                diagnostics.ScatterCoalescedCount + diagnostics.GmiCoalescedCount)
        {
            throw new PerformanceArtifactException(
                "Performance scheduler counters do not reproduce publication/render/coalesce cohorts.");
        }

        if (diagnostics.OneEventAllocationProbeSamples.Length != 4 ||
            diagnostics.HardEventAllocationProbeSamples.Length != 4 ||
            diagnostics.PairedAllocationDeltaSamples.Length != 4)
        {
            throw new PerformanceArtifactException(
                "Performance allocation probe must contain four balanced pairs.");
        }
        for (var index = 0; index < 4; index++)
        {
            if (diagnostics.PairedAllocationDeltaSamples[index] !=
                diagnostics.HardEventAllocationProbeSamples[index] -
                diagnostics.OneEventAllocationProbeSamples[index])
            {
                throw new PerformanceArtifactException(
                    "Performance allocation probe paired delta is not reproducible.");
            }
        }
        if (raw.OneEventAllocationProbeBytes != diagnostics.OneEventAllocationProbeSamples.Max() ||
            raw.HardEventAllocationProbeBytes != diagnostics.HardEventAllocationProbeSamples.Max() ||
            raw.EventAllocationProbeDeltaBytes != diagnostics.PairedAllocationDeltaSamples.Max() ||
            raw.AllocationProbeInvalidPairCount !=
                diagnostics.PairedAllocationDeltaSamples.Count(delta => delta < 0))
        {
            throw new PerformanceArtifactException(
                "Performance allocation probe summary is not reproducible.");
        }

        if (raw.Scenario == PerformanceScenario.Soak)
        {
            var plannedSeconds = raw.PlannedWindowDurationTicks /
                (double)raw.StopwatchFrequency;
            var minimumSamples = Math.Max(1, (int)Math.Floor(plannedSeconds) - 1);
            if (diagnostics.MemorySampleTicks.Length < minimumSamples)
            {
                throw new PerformanceArtifactException(
                    "Performance soak memory diagnostic cohort is incomplete.");
            }
        }
        else if (diagnostics.MemorySampleTicks.Length != 0)
        {
            throw new PerformanceArtifactException(
                "Non-soak performance runs must not contain memory sample cohorts.");
        }
    }

    private static bool StrictlyIncreasing(IReadOnlyList<long> values)
    {
        var previous = 0L;
        foreach (var value in values)
        {
            if (value <= previous)
            {
                return false;
            }

            previous = value;
        }

        return true;
    }

    private static bool StartsAndCompletionsAreValid(
        IReadOnlyList<long> starts,
        IReadOnlyList<long> completions,
        long windowStart,
        long plannedWindowEnd)
    {
        for (var index = 0; index < starts.Count; index++)
        {
            if (starts[index] < windowStart ||
                starts[index] >= plannedWindowEnd ||
                completions[index] < starts[index])
            {
                return false;
            }
        }

        return true;
    }

    private static bool OrderedWithinInclusive(
        IReadOnlyList<long> values,
        long start,
        long end)
    {
        var previous = start;
        foreach (var value in values)
        {
            if (value < previous || value < start || value > end)
            {
                return false;
            }

            previous = value;
        }

        return true;
    }

    private static string Sha256(byte[] bytes) =>
        Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
}

internal sealed record PerformanceArtifactReference(
    string Path,
    long SizeBytes,
    string Sha256,
    int ProcessId,
    int ProcessExitCode,
    PerformanceScenario Scenario,
    int RunIndex,
    PerformanceVerdictStatus Verdict);

internal sealed record PerformanceIdentityReference(
    string Path,
    long SizeBytes,
    string Sha256);

internal sealed record PerformanceProcessExitLedger(
    string SchemaId,
    string RunnerContractId,
    IReadOnlyList<PerformanceProcessExitRecord> Records);

internal sealed record PerformanceProcessExitRecord(
    string Scenario,
    int RunIndex,
    int ProcessId,
    int ExitCode);

internal sealed record PerformanceSuiteManifest(
    string SchemaId,
    string RunnerContractId,
    string MetricSchemaId,
    string MetricDefinitionsSha256,
    string SessionId,
    bool DevelopmentOnly,
    bool OfficialEligible,
    bool OfficialAcceptance,
    bool MaySubstituteOfficial,
    string Status,
    string SourceRevision,
    string SummaryMethod,
    PerformanceIdentityReference? ReferenceProfile,
    PerformanceIdentityReference ObservedProfile,
    PerformanceIdentityReference FinalObservedProfile,
    PerformanceIdentityReference Provenance,
    PerformanceIdentityReference ProcessExits,
    IReadOnlyList<PerformanceArtifactReference> Artifacts)
{
    public static PerformanceSuiteManifest CreateDryRun(
        string sessionId,
        string sourceRevision,
        IReadOnlyList<PerformanceArtifactReference> artifacts,
        PerformanceIdentityReference observedProfile,
        PerformanceIdentityReference finalObservedProfile,
        PerformanceIdentityReference provenance,
        PerformanceIdentityReference processExits) =>
        new(
            "analogboard.scatter-rendering.suite-manifest.v1",
            "AB-PERF-RUNNER-v1",
            PerformanceMetricSchema.SchemaId,
            PerformanceMetricEvidence.DefinitionsSha256,
            sessionId,
            DevelopmentOnly: true,
            OfficialEligible: false,
            OfficialAcceptance: false,
            MaySubstituteOfficial: false,
            "not_evaluated",
            sourceRevision,
            "per-run nearest-rank from sealed raw ticks; no cross-run averaging or rescue",
            ReferenceProfile: null,
            observedProfile,
            finalObservedProfile,
            provenance,
            processExits,
            artifacts);

    public static PerformanceSuiteManifest CreateOfficial(
        string sessionId,
        string sourceRevision,
        IReadOnlyList<PerformanceArtifactReference> artifacts,
        PerformanceIdentityReference referenceProfile,
        PerformanceIdentityReference observedProfile,
        PerformanceIdentityReference finalObservedProfile,
        PerformanceIdentityReference provenance,
        PerformanceIdentityReference processExits) =>
        new(
            "analogboard.scatter-rendering.suite-manifest.v1",
            "AB-PERF-RUNNER-v1",
            PerformanceMetricSchema.SchemaId,
            PerformanceMetricEvidence.DefinitionsSha256,
            sessionId,
            DevelopmentOnly: false,
            OfficialEligible: true,
            OfficialAcceptance: true,
            MaySubstituteOfficial: false,
            "pass",
            sourceRevision,
            "all three independent scatter and combined runs pass individually; soak passes; headroom observed separately; nearest-rank from sealed raw ticks",
            referenceProfile,
            observedProfile,
            finalObservedProfile,
            provenance,
            processExits,
            artifacts);
}

internal static class PerformanceJsonValidation
{
    public static void RejectDuplicateProperties(string json, string description)
    {
        ArgumentNullException.ThrowIfNull(json);
        ArgumentException.ThrowIfNullOrWhiteSpace(description);
        using var document = JsonDocument.Parse(json);
        Visit(document.RootElement, description, path: null);
    }

    private static void Visit(JsonElement element, string description, string? path)
    {
        if (element.ValueKind == JsonValueKind.Object)
        {
            var names = new HashSet<string>(StringComparer.Ordinal);
            foreach (var property in element.EnumerateObject())
            {
                var propertyPath = path is null
                    ? property.Name
                    : $"{path}.{property.Name}";
                if (!names.Add(property.Name))
                {
                    throw new PerformanceArtifactException(
                        $"{description} must not contain duplicate JSON property names: {propertyPath}.");
                }

                Visit(property.Value, description, propertyPath);
            }
        }
        else if (element.ValueKind == JsonValueKind.Array)
        {
            var index = 0;
            foreach (var item in element.EnumerateArray())
            {
                Visit(item, description, $"{path}[{index}]");
                index++;
            }
        }
    }
}

internal static class PerformanceArtifactJson
{
    public static JsonSerializerOptions Options { get; } = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
        WriteIndented = true,
        RespectRequiredConstructorParameters = true,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
        Converters = { new JsonStringEnumConverter(JsonNamingPolicy.SnakeCaseLower) },
    };

    public static T DeserializeRequired<T>(byte[] bytes, string description)
        where T : class
    {
        ArgumentNullException.ThrowIfNull(bytes);
        ArgumentException.ThrowIfNullOrWhiteSpace(description);
        try
        {
            return JsonSerializer.Deserialize<T>(bytes, Options)
                ?? throw new PerformanceArtifactException(
                    $"{description} does not contain the complete versioned field set.");
        }
        catch (JsonException)
        {
            throw new PerformanceArtifactException(
                $"{description} does not contain the complete versioned field set.");
        }
    }
}
