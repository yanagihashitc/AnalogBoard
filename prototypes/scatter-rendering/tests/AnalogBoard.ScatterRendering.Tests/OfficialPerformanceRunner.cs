using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Windows.Threading;
using AnalogBoard.ScatterRendering.Core;
using AnalogBoard.ScatterRendering.Wpf;

namespace AnalogBoard.ScatterRendering.Tests;

internal static class OfficialPerformanceRunner
{
    private const int HardEventCount = 100_001;
    private const int HeadroomEventCount = 131_072;
    private const int HardRasterSize = 512;
    private const int HeadroomRasterSize = 1_024;
    private const int GmiWaveformCount = 100;
    private const int GmiSampleCount = 2_400;
    private const int AllocationProbeFrameCount = 32;

    public static int Execute(PerformanceRunCommand command)
    {
        ArgumentNullException.ThrowIfNull(command);
        PerformancePathAuthority.ValidateRun(command);
        var observedBytes = File.ReadAllBytes(command.ObservedProfilePath);
        var observedJson = Encoding.UTF8.GetString(observedBytes);
        var observed = PerformanceProfilePreflight.ParseObserved(observedJson);
        string? referenceJson = null;
        byte[]? referenceBytes = null;
        PerformanceProfilePreflightResult preflight;
        if (command.Mode == PerformanceExecutionMode.Official)
        {
            referenceBytes = File.ReadAllBytes(command.ReferenceProfilePath!);
            referenceJson = Encoding.UTF8.GetString(referenceBytes);
            var reference = PerformanceProfilePreflight.ParseReference(referenceJson);
            preflight = PerformanceProfilePreflight.Compare(reference, observed);
            if (!preflight.OfficialEligible)
            {
                throw new PerformanceProfileException(
                    $"Reference profile does not match the live machine: {string.Join(",", preflight.Mismatches)}.");
            }
        }
        else
        {
            preflight = new PerformanceProfilePreflightResult(false, "development_only", []);
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
        var measured = command.Scenario == PerformanceScenario.Headroom
            ? RunHeadroom(command)
            : RunVisualization(command);
        var raw = measured.Raw with
        {
            OfficialEligible = PerformanceRunEligibility.IsEligible(
                command.Mode,
                command.Scenario,
                preflight.OfficialEligible),
        };
        var verdict = PerformanceRunEvaluator.Evaluate(raw);
        var artifact = new PerformanceRawArtifact(
            SchemaId: "analogboard.scatter-rendering.raw-artifact.v1",
            RunnerContractId: "AB-PERF-RUNNER-v1",
            DevelopmentOnly: command.Mode == PerformanceExecutionMode.DryRun,
            OfficialEligible: PerformanceRunEligibility.IsEligible(
                command.Mode,
                command.Scenario,
                preflight.OfficialEligible),
            OfficialAcceptance: false,
            MaySubstituteOfficial: false,
            FixtureId: FixtureIdFor(command.Scenario),
            ProcessId: Environment.ProcessId,
            MachineName: Environment.MachineName,
            StartedAtUtc: measured.StartedAtUtc,
            FinishedAtUtc: measured.FinishedAtUtc,
            ReferenceProfileSha256: referenceJson is null
                ? null
                : Sha256(referenceBytes!),
            ObservedProfileSha256: Sha256(observedBytes),
            ProvenanceSha256: Sha256(provenanceBytes),
            SourceRevision: provenance.SourceRevision,
            SourceDirty: provenance.SourceDirty,
            LoadedRuntime: PerformanceLoadedRuntime.Capture(),
            Workload: PerformanceWorkloadContract.Create(command.Scenario),
            Schedule: measured.Schedule,
            Raw: raw,
            Diagnostics: measured.Diagnostics,
            Verdict: verdict);
        var seal = AtomicPerformanceArtifactWriter.Write(
            command.OutputPath,
            stream => JsonSerializer.Serialize(stream, artifact, PerformanceArtifactJson.Options));
        ValidateWrittenArtifact(command.OutputPath, artifact, seal);
        Console.WriteLine(
            $"PERFORMANCE_ARTIFACT path={seal.Path} sha256={seal.Sha256} size={seal.SizeBytes} " +
            $"mode={command.Mode} scenario={command.Scenario} run={command.RunIndex} verdict={verdict.Status} " +
            $"official_acceptance=false");
        if (command.Mode == PerformanceExecutionMode.DryRun ||
            verdict.Status is PerformanceVerdictStatus.Pass or PerformanceVerdictStatus.Observed)
        {
            return PerformanceExitCodes.Success;
        }

        return verdict.Status == PerformanceVerdictStatus.Fail
            ? PerformanceExitCodes.NumericThresholdFailure
            : PerformanceExitCodes.MeasurementInvalid;
    }

    private static MeasuredPerformanceRun RunVisualization(PerformanceRunCommand command)
    {
        var duration = command.Scenario == PerformanceScenario.Soak
            ? command.Schedule.Soak
            : command.Schedule.Measurement;
        VisualizationMeasurementResult warmupResult;
        using (var warmup = new VisualizationMeasurementWorkload(command.Scenario))
        {
            warmupResult = warmup.Run(command.Schedule.Warmup, collectMemory: false);
        }

        using var workload = new VisualizationMeasurementWorkload(command.Scenario);
        var result = workload.Run(
            duration,
            collectMemory: command.Scenario == PerformanceScenario.Soak);
        var allocationProbe = MeasureAllocationProbe();
        var raw = new PerformanceRawRun(
            SchemaId: "analogboard.scatter-rendering.raw-run.v1",
            RunnerContractId: "AB-PERF-RUNNER-v1",
            Mode: command.Mode,
            Scenario: command.Scenario,
            RunIndex: command.RunIndex,
            OfficialEligible: command.Mode == PerformanceExecutionMode.Official,
            StopwatchFrequency: Stopwatch.Frequency,
            WindowStartTick: result.WindowStartTick,
            WindowEndTick: result.WindowEndTick,
            ScatterFrameDurationTicks: result.ScatterFrameDurationTicks,
            ScatterCompletionTicks: result.ScatterCompletionTicks,
            GmiCompletionTicks: result.GmiCompletionTicks,
            InputLatencyTicks: result.InputLatencyTicks,
            ScatterPublicationDurationTicks: result.ScatterMetrics.PublicationDurationTicks.Samples.ToArray(),
            GmiPublicationDurationTicks: result.GmiMetrics?.PublicationDurationTicks.Samples.ToArray() ?? [],
            PendingFrameMaximum: Math.Max(
                result.ScatterMetrics.PendingFrameMaximum,
                result.GmiMetrics?.PendingFrameMaximum ?? 0),
            PendingCallbackMaximum: Math.Max(
                result.ScatterMetrics.PendingCallbackMaximum,
                result.GmiMetrics?.PendingCallbackMaximum ?? 0),
            CoalescedFrameCount:
                result.ScatterMetrics.CoalescedCount +
                (result.GmiMetrics?.CoalescedCount ?? 0),
            OverwrittenSampleCount:
                OverwrittenSamples(result.ScatterMetrics) +
                (result.GmiMetrics is null ? 0 : OverwrittenSamples(result.GmiMetrics)),
            ProcessAllocatedBytes: result.ProcessAllocatedBytes,
            AllocationProbeFrameCount: AllocationProbeFrameCount,
            OneEventAllocationProbeBytes: allocationProbe.OneEventBytes,
            HardEventAllocationProbeBytes: allocationProbe.HardEventBytes,
            RetainedManagedHeapGrowthBytes: result.RetainedManagedHeapGrowthBytes,
            StartingPrivateBytes: result.StartingPrivateBytes,
            PrivateBytesGrowth: result.PrivateBytesGrowth)
        {
            PlannedWindowDurationTicks = ToStopwatchTicks(duration),
            InputCadenceHz = 20,
            SchedulerFailureCount =
                SchedulerFailureCount(result.ScatterMetrics) +
                (result.GmiMetrics is null ? 0 : SchedulerFailureCount(result.GmiMetrics)),
            PendingWorkAtEnd =
                result.ScatterMetrics.PendingFrameCount +
                result.ScatterMetrics.PendingCallbackCount +
                (result.GmiMetrics?.PendingFrameCount ?? 0) +
                (result.GmiMetrics?.PendingCallbackCount ?? 0),
            MetricSampleCountMismatch =
                MetricSampleCountMismatch(
                    result.ScatterMetrics,
                    result.ScatterGeneration.LongLength) +
                (result.GmiMetrics is null
                    ? 0
                    : MetricSampleCountMismatch(
                        result.GmiMetrics,
                        result.GmiGeneration.LongLength)),
            EventAllocationProbeDeltaBytes = allocationProbe.MaximumDeltaBytes,
            AllocationProbeInvalidPairCount = allocationProbe.InvalidPairCount,
        };
        var diagnostics = new PerformanceRawDiagnostics(
            result.ScatterGeneration,
            result.ScatterProducerStartTicks,
            result.ScatterRenderCompletionTicks,
            result.GmiGeneration,
            result.GmiProducerStartTicks,
            result.GmiRenderCompletionTicks,
            result.InputEnqueueTicks,
            result.InputCompletionTicks,
            result.MemorySampleTicks,
            result.ManagedHeapSamples,
            result.PrivateByteSamples,
            result.ScatterCadenceMissCount,
            result.GmiCadenceMissCount,
            result.InputCadenceMissCount,
            result.ScatterNoLeaseCount,
            result.GmiNoLeaseCount,
            result.ScatterMetrics.AcceptedCount,
            result.ScatterMetrics.RenderedCount,
            result.ScatterMetrics.CoalescedCount,
            result.GmiMetrics?.AcceptedCount ?? 0,
            result.GmiMetrics?.RenderedCount ?? 0,
            result.GmiMetrics?.CoalescedCount ?? 0,
            allocationProbe.OneEventSamples,
            allocationProbe.HardEventSamples,
            allocationProbe.PairedDeltaSamples);
        return new MeasuredPerformanceRun(
            result.StartedAtUtc,
            result.FinishedAtUtc,
            new PerformanceScheduleEvidence(
                Stopwatch.Frequency,
                ToStopwatchTicks(command.Schedule.Warmup),
                warmupResult.WindowEndTick - warmupResult.WindowStartTick,
                ToStopwatchTicks(duration),
                result.WindowEndTick - result.WindowStartTick,
                command.Schedule.HardRunCount),
            raw,
            diagnostics);
    }

    private static MeasuredPerformanceRun RunHeadroom(PerformanceRunCommand command)
    {
        var frame = SyntheticAggregateFrameFactory.Create(
            HeadroomEventCount,
            seed: 0x7C39,
            generation: 1);
        var snapshot = SyntheticGmiSnapshotFactory.Create(
            GmiChannel.FsGmi,
            GmiWaveformCount,
            GmiSampleCount,
            seed: 0x8D4A,
            generation: 1);
        var binnerA = new DensityBinner(HeadroomEventCount);
        var binnerB = new DensityBinner(HeadroomEventCount);
        var gridA = new DensityGridBuffer(HeadroomRasterSize, HeadroomRasterSize);
        var gridB = new DensityGridBuffer(HeadroomRasterSize, HeadroomRasterSize);
        var scatterA = new byte[HeadroomRasterSize * HeadroomRasterSize * DensityRasterizer.BytesPerPixel];
        var scatterB = new byte[scatterA.Length];
        var gmiCoverage = new int[HeadroomRasterSize * HeadroomRasterSize];
        var gmiPixels = new byte[scatterA.Length];
        using var surfaceA = new WriteableBitmapDensitySurface(HeadroomRasterSize, HeadroomRasterSize);
        using var surfaceB = new WriteableBitmapDensitySurface(HeadroomRasterSize, HeadroomRasterSize);
        using var surfaceGmi = new WriteableBitmapDensitySurface(HeadroomRasterSize, HeadroomRasterSize);
        var primary = CreatePrimaryRequest();
        var secondary = CreateSecondaryRequest();
        var generation = 0L;

        void RenderOne()
        {
            binnerA.Bin(frame, primary, gridA);
            binnerB.Bin(frame, secondary, gridB);
            DensityRasterizer.Rasterize(gridA, scatterA);
            DensityRasterizer.Rasterize(gridB, scatterB);
            GmiOverlayRasterizer.Rasterize(
                snapshot,
                new GmiDisplayRange(0.0, 16_383.0),
                HeadroomRasterSize,
                HeadroomRasterSize,
                gmiCoverage,
                gmiPixels);
            generation++;
            surfaceA.Publish(generation, scatterA);
            surfaceB.Publish(generation, scatterB);
            surfaceGmi.Publish(generation, gmiPixels);
        }
        var warmupStart = Stopwatch.GetTimestamp();
        RunLoopFor(command.Schedule.Warmup, RenderOne, samples: null);
        var warmupEnd = Stopwatch.GetTimestamp();
        var startedAtUtc = DateTimeOffset.UtcNow;
        var windowStart = Stopwatch.GetTimestamp();
        var frameDurations = new FixedLongBuffer(MaximumSampleCapacity);
        RunLoopFor(command.Schedule.Measurement, RenderOne, frameDurations);
        var windowEnd = Stopwatch.GetTimestamp();
        var finishedAtUtc = DateTimeOffset.UtcNow;
        var raw = new PerformanceRawRun(
            "analogboard.scatter-rendering.raw-run.v1",
            "AB-PERF-RUNNER-v1",
            command.Mode,
            PerformanceScenario.Headroom,
            command.RunIndex,
            OfficialEligible: false,
            Stopwatch.Frequency,
            windowStart,
            windowEnd,
            frameDurations.Snapshot(),
            [],
            [],
            [],
            [],
            [],
            0,
            0,
            0,
            frameDurations.OverflowCount,
            0,
            1,
            0,
            0,
            0,
            0,
            0)
        {
            PlannedWindowDurationTicks = ToStopwatchTicks(command.Schedule.Measurement),
        };
        var diagnostics = new PerformanceRawDiagnostics(
            [], [], [], [], [], [], [], [], [], [], [], 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, [], [], []);
        return new MeasuredPerformanceRun(
            startedAtUtc,
            finishedAtUtc,
            new PerformanceScheduleEvidence(
                Stopwatch.Frequency,
                ToStopwatchTicks(command.Schedule.Warmup),
                warmupEnd - warmupStart,
                ToStopwatchTicks(command.Schedule.Measurement),
                windowEnd - windowStart,
                command.Schedule.HardRunCount),
            raw,
            diagnostics);
    }

    private static void RunLoopFor(
        TimeSpan duration,
        Action action,
        FixedLongBuffer? samples)
    {
        var durationTicks = ToStopwatchTicks(duration);
        var deadline = Stopwatch.GetTimestamp() + durationTicks;
        while (Stopwatch.GetTimestamp() < deadline)
        {
            var startedAt = Stopwatch.GetTimestamp();
            action();
            samples?.TryAdd(Stopwatch.GetTimestamp() - startedAt);
        }
    }

    private static AllocationProbeResult MeasureAllocationProbe()
    {
        const int repetitions = 4;
        _ = MeasureAllocationProbeForEventCount(1);
        _ = MeasureAllocationProbeForEventCount(HardEventCount);
        var oneEventSamples = new long[repetitions];
        var hardEventSamples = new long[repetitions];
        var deltas = new long[repetitions];
        var invalidPairCount = 0;
        for (var index = 0; index < repetitions; index++)
        {
            if ((index & 1) == 0)
            {
                oneEventSamples[index] = MeasureAllocationProbeForEventCount(1);
                hardEventSamples[index] = MeasureAllocationProbeForEventCount(HardEventCount);
            }
            else
            {
                hardEventSamples[index] = MeasureAllocationProbeForEventCount(HardEventCount);
                oneEventSamples[index] = MeasureAllocationProbeForEventCount(1);
            }

            deltas[index] = hardEventSamples[index] - oneEventSamples[index];
            if (deltas[index] < 0)
            {
                invalidPairCount++;
            }
        }

        return new AllocationProbeResult(
            oneEventSamples,
            hardEventSamples,
            deltas,
            oneEventSamples.Max(),
            hardEventSamples.Max(),
            deltas.Max(),
            invalidPairCount);
    }

    private static long MeasureAllocationProbeForEventCount(int eventCount)
    {
        var dispatcher = Dispatcher.CurrentDispatcher;
        var frame = SyntheticAggregateFrameFactory.Create(
            eventCount,
            seed: 0x5A17,
            generation: 1);
        var binner = new DensityBinner(eventCount);
        var grid = new DensityGridBuffer(HardRasterSize, HardRasterSize);
        var request = CreatePrimaryRequest();
        var lease = new ReusableRasterFrameLease(HardRasterSize, HardRasterSize);
        RasterPixelWriter writer = pixels =>
        {
            binner.Bin(frame, request, grid);
            DensityRasterizer.Rasterize(grid, pixels);
        };
        using var harness = new CombinedVisualizationHarness<ReusableRasterFrameLease, ReusableGmiRasterFrameLease>(
            dispatcher,
            HardRasterSize,
            HardRasterSize,
            _ => { },
            _ => { },
            metricCapacity: 128);
        lease.Prepare(1, writer);
        harness.SubmitScatter(lease);
        PumpDispatcherToIdle(dispatcher);
        var allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
        for (var index = 0; index < AllocationProbeFrameCount; index++)
        {
            lease.Prepare(index + 2L, writer);
            var status = harness.SubmitScatter(lease);
            if (status != FrameSubmissionStatus.Accepted)
            {
                throw new PerformanceMeasurementException(
                    $"Allocation probe submission failed: {status}.");
            }

            PumpDispatcherToIdle(dispatcher);
        }

        return GC.GetAllocatedBytesForCurrentThread() - allocatedBefore;
    }

    private static long OverwrittenSamples(LatestFrameSchedulerMetricsSnapshot metrics) =>
        metrics.PublicationDurationTicks.OverwrittenSampleCount +
        metrics.PublishToDrainLatencyTicks.OverwrittenSampleCount +
        metrics.ProducerAllocatedBytes.OverwrittenSampleCount;

    private static long SchedulerFailureCount(LatestFrameSchedulerMetricsSnapshot metrics) =>
        metrics.StaleGenerationCount +
        metrics.ConcurrentPublisherCount +
        metrics.DisposedRejectionCount +
        metrics.FaultedRejectionCount +
        metrics.PostFailureCount +
        metrics.RenderFailureCount +
        metrics.ReleaseFailureCount +
        metrics.LeaseAlreadyOwnedCount;

    private static long MetricSampleCountMismatch(
        LatestFrameSchedulerMetricsSnapshot metrics,
        long timelineRenderedCount) =>
        Mismatch(metrics.PublicationDurationTicks.TotalSampleCount, metrics.AcceptedCount) +
        Mismatch(metrics.ProducerAllocatedBytes.TotalSampleCount, metrics.AcceptedCount) +
        Mismatch(metrics.PublishToDrainLatencyTicks.TotalSampleCount, metrics.RenderedCount) +
        Mismatch(metrics.RenderedCount, timelineRenderedCount) +
        Mismatch(metrics.AcceptedCount, metrics.RenderedCount + metrics.CoalescedCount);

    private static int Mismatch(long left, long right) => left == right ? 0 : 1;

    private static void ValidateWrittenArtifact(
        string path,
        PerformanceRawArtifact expected,
        PerformanceArtifactSeal seal)
    {
        var bytes = File.ReadAllBytes(path);
        PerformanceJsonValidation.RejectDuplicateProperties(
            Encoding.UTF8.GetString(bytes),
            "Performance raw artifact");
        var actualHash = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
        if (bytes.LongLength != seal.SizeBytes ||
            !StringComparer.Ordinal.Equals(actualHash, seal.Sha256))
        {
            throw new PerformanceArtifactException(
                "Performance artifact size or hash changed after atomic publication.");
        }

        var actual = PerformanceArtifactJson.DeserializeRequired<PerformanceRawArtifact>(
            bytes,
            "Performance raw artifact");
        if (!StringComparer.Ordinal.Equals(expected.SchemaId, actual.SchemaId) ||
            expected.ProcessId != actual.ProcessId ||
            expected.Raw.Scenario != actual.Raw.Scenario ||
            expected.Raw.RunIndex != actual.Raw.RunIndex ||
            actual.OfficialAcceptance)
        {
            throw new PerformanceArtifactException(
                "Performance artifact identity changed after publication.");
        }
    }

    private static DensityBinningRequest CreatePrimaryRequest() =>
        new(
            new DensityAxisOptions(
                0,
                DisplayTransformKind.Linear,
                new DensityRange(-100_000.0, 300_000.0)),
            new DensityAxisOptions(
                1,
                DisplayTransformKind.Asinh,
                new DensityRange(-100_000.0, 300_000.0)),
            DensityOutlierMode.Exclude);

    private static DensityBinningRequest CreateSecondaryRequest() =>
        new(
            new DensityAxisOptions(
                3,
                DisplayTransformKind.Asinh,
                new DensityRange(-100_000.0, 300_000.0)),
            new DensityAxisOptions(
                4,
                DisplayTransformKind.Linear,
                new DensityRange(-100_000.0, 300_000.0)),
            DensityOutlierMode.Exclude);

    private static string FixtureIdFor(PerformanceScenario scenario) => scenario switch
    {
        PerformanceScenario.HardScatter => "AB-P0-R1-HARD-SCATTER-v1",
        PerformanceScenario.HardCombined => "AB-P0-R1-HARD-COMBINED-v1",
        PerformanceScenario.Soak => "AB-P0-R1-HARD-COMBINED-v1",
        PerformanceScenario.Headroom => "AB-P0-R1-HEADROOM-v1",
        _ => throw new PerformanceMeasurementException(
            $"Unsupported performance scenario: {scenario}."),
    };

    private static long ToStopwatchTicks(TimeSpan duration) =>
        checked((long)Math.Ceiling(duration.TotalSeconds * Stopwatch.Frequency));

    private static string Sha256(byte[] bytes) =>
        Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();

    private static void PumpDispatcherToIdle(Dispatcher dispatcher)
    {
        var frame = new DispatcherFrame();
        dispatcher.BeginInvoke(DispatcherPriority.ApplicationIdle, () => frame.Continue = false);
        Dispatcher.PushFrame(frame);
    }

    private const int MaximumSampleCapacity = 65_536;

    private sealed class VisualizationMeasurementWorkload : IDisposable
    {
        private readonly PerformanceScenario _scenario;
        private readonly Dispatcher _dispatcher;
        private readonly MeasurementTimeline _scatterTimeline = new(MaximumSampleCapacity);
        private readonly MeasurementTimeline _gmiTimeline = new(MaximumSampleCapacity);
        private readonly InputProbe _inputProbe;
        private readonly ScatterProducer _scatterProducer;
        private readonly GmiProducer? _gmiProducer;
        private readonly CombinedVisualizationHarness<ReusableRasterFrameLease, ReusableGmiRasterFrameLease> _harness;
        private int _disposed;

        public VisualizationMeasurementWorkload(PerformanceScenario scenario)
        {
            if (scenario is not PerformanceScenario.HardScatter and
                not PerformanceScenario.HardCombined and
                not PerformanceScenario.Soak)
            {
                throw new PerformanceMeasurementException(
                    $"Visualization workload does not support {scenario}.");
            }

            _scenario = scenario;
            _dispatcher = Dispatcher.CurrentDispatcher;
            _inputProbe = new InputProbe(_dispatcher, MaximumSampleCapacity);
            _scatterProducer = new ScatterProducer(_scatterTimeline);
            _gmiProducer = scenario is PerformanceScenario.HardCombined or PerformanceScenario.Soak
                ? new GmiProducer(_gmiTimeline)
                : null;
            _harness = new CombinedVisualizationHarness<ReusableRasterFrameLease, ReusableGmiRasterFrameLease>(
                _dispatcher,
                HardRasterSize,
                HardRasterSize,
                _scatterProducer.Release,
                frame => _gmiProducer?.Release(frame),
                metricCapacity: MaximumSampleCapacity,
                renderStarting: null,
                renderCompleted: (feed, generation) =>
                {
                    var now = Stopwatch.GetTimestamp();
                    if (feed == VisualizationFeed.Scatter)
                    {
                        _scatterTimeline.RecordCompletion(generation, now);
                    }
                    else
                    {
                        _gmiTimeline.RecordCompletion(generation, now);
                    }
                });
            _scatterProducer.Attach(_harness.SubmitScatter);
            _gmiProducer?.Attach(_harness.SubmitGmi);
        }

        public VisualizationMeasurementResult Run(TimeSpan duration, bool collectMemory)
        {
            var startedAtUtc = DateTimeOffset.UtcNow;
            if (collectMemory)
            {
                ForceFullCollection();
            }

            var process = Process.GetCurrentProcess();
            process.Refresh();
            var startingPrivateBytes = collectMemory ? process.PrivateMemorySize64 : 0L;
            var startingManagedHeap = collectMemory ? GC.GetTotalMemory(forceFullCollection: false) : 0L;
            var windowStart = Stopwatch.GetTimestamp();
            var plannedWindowEnd = checked(windowStart + ToStopwatchTicks(duration));
            _scatterTimeline.OpenWindow(windowStart, plannedWindowEnd);
            _gmiTimeline.OpenWindow(windowStart, plannedWindowEnd);
            _inputProbe.OpenWindow(windowStart, plannedWindowEnd);
            var memorySampler = collectMemory
                ? new MemorySampler(windowStart, plannedWindowEnd)
                : null;
            var allocatedBefore = GC.GetTotalAllocatedBytes(precise: true);
            _scatterProducer.Start(windowStart, plannedWindowEnd);
            _gmiProducer?.Start(windowStart, plannedWindowEnd);
            _inputProbe.Start();
            memorySampler?.Start();
            RunDispatcherUntil(_dispatcher, plannedWindowEnd);
            var windowEnd = Stopwatch.GetTimestamp();
            _scatterProducer.Stop();
            _gmiProducer?.Stop();
            _inputProbe.Stop();
            memorySampler?.Stop();
            PumpDispatcherToIdle(_dispatcher);
            var allocatedBytes = GC.GetTotalAllocatedBytes(precise: true) - allocatedBefore;
            var scatterMetrics = _harness.GetScatterMetricsSnapshot();
            var gmiMetrics = _gmiProducer is null
                ? null
                : _harness.GetGmiMetricsSnapshot();
            long retainedGrowth = 0;
            long privateGrowth = 0;
            if (collectMemory)
            {
                ForceFullCollection();
                var endingManagedHeap = GC.GetTotalMemory(forceFullCollection: false);
                process.Refresh();
                retainedGrowth = endingManagedHeap - startingManagedHeap;
                privateGrowth = process.PrivateMemorySize64 - startingPrivateBytes;
            }

            var scatterSnapshot = _scatterTimeline.Snapshot();
            var gmiSnapshot = _gmiTimeline.Snapshot();
            var inputSnapshot = _inputProbe.Snapshot();
            var memorySnapshot = memorySampler?.Snapshot() ?? MemorySampleSnapshot.Empty;
            VerifyProducer(_scatterProducer.Failure, "scatter");
            VerifyProducer(_gmiProducer?.Failure, "GMI");
            VerifyProducer(_inputProbe.Failure, "input");
            VerifyProducer(memorySampler?.Failure, "memory");
            var finishedAtUtc = DateTimeOffset.UtcNow;
            return new VisualizationMeasurementResult(
                startedAtUtc,
                finishedAtUtc,
                windowStart,
                windowEnd,
                scatterSnapshot.Generation,
                scatterSnapshot.ProducerStartTicks,
                scatterSnapshot.RenderCompletionTicks,
                scatterSnapshot.FrameDurationTicks,
                scatterSnapshot.CompletionTicksWithinWindow,
                gmiSnapshot.Generation,
                gmiSnapshot.ProducerStartTicks,
                gmiSnapshot.RenderCompletionTicks,
                gmiSnapshot.CompletionTicksWithinWindow,
                inputSnapshot.EnqueueTicks,
                inputSnapshot.CompletionTicks,
                inputSnapshot.LatencyTicks,
                scatterMetrics,
                gmiMetrics,
                allocatedBytes,
                retainedGrowth,
                startingPrivateBytes,
                privateGrowth,
                memorySnapshot.TimestampTicks,
                memorySnapshot.ManagedHeapBytes,
                memorySnapshot.PrivateBytes,
                _scatterProducer.CadenceMissCount,
                _gmiProducer?.CadenceMissCount ?? 0,
                _inputProbe.CadenceMissCount,
                _scatterProducer.NoLeaseCount,
                _gmiProducer?.NoLeaseCount ?? 0);
        }

        public void Dispose()
        {
            if (Interlocked.Exchange(ref _disposed, 1) != 0)
            {
                return;
            }

            _scatterProducer.Dispose();
            _gmiProducer?.Dispose();
            _inputProbe.Dispose();
            _harness.Dispose();
        }

        private static void VerifyProducer(Exception? failure, string name)
        {
            if (failure is not null)
            {
                throw new PerformanceMeasurementException(
                    $"{name} measurement worker failed.",
                    failure);
            }
        }

        private static void ForceFullCollection()
        {
            GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced, blocking: true, compacting: true);
            GC.WaitForPendingFinalizers();
            GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced, blocking: true, compacting: true);
        }
    }

    private abstract class FixedCadenceProducer<TFrame> : IDisposable
        where TFrame : class, IRasterFrameLease
    {
        private readonly string _name;
        private readonly long _periodTicks;
        private readonly TFrame[] _leases;
        private readonly int[] _leaseStates;
        private Thread? _thread;
        private Func<TFrame, FrameSubmissionStatus>? _submit;
        private long _windowStart;
        private long _windowEnd;
        private int _stop;
        private int _disposed;

        protected FixedCadenceProducer(string name, int cadenceHz, TFrame[] leases)
        {
            _name = name;
            _periodTicks = Math.Max(1, Stopwatch.Frequency / cadenceHz);
            _leases = leases;
            _leaseStates = new int[leases.Length];
        }

        public Exception? Failure { get; private set; }

        public long CadenceMissCount { get; private set; }

        public long NoLeaseCount { get; private set; }

        public void Attach(Func<TFrame, FrameSubmissionStatus> submit) =>
            _submit = submit ?? throw new ArgumentNullException(nameof(submit));

        public void Start(long windowStart, long windowEnd)
        {
            if (_submit is null)
            {
                throw new PerformanceMeasurementException(
                    $"{_name} producer has no scheduler target.");
            }

            _windowStart = windowStart;
            _windowEnd = windowEnd;
            _thread = new Thread(Run)
            {
                IsBackground = true,
                Name = $"P0-R1-{_name}-producer",
            };
            _thread.Start();
        }

        public void Stop()
        {
            Volatile.Write(ref _stop, 1);
            _thread?.Join();
        }

        public void Release(TFrame frame)
        {
            for (var index = 0; index < _leases.Length; index++)
            {
                if (ReferenceEquals(_leases[index], frame))
                {
                    Volatile.Write(ref _leaseStates[index], 0);
                    return;
                }
            }

            throw new PerformanceMeasurementException(
                $"{_name} scheduler released an unknown lease.");
        }

        public void Dispose()
        {
            if (Interlocked.Exchange(ref _disposed, 1) != 0)
            {
                return;
            }

            Stop();
        }

        protected abstract void Prepare(TFrame frame, long generation, long producerStartTick);

        private void Run()
        {
            try
            {
                var generation = 0L;
                var nextDeadline = _windowStart;
                while (Volatile.Read(ref _stop) == 0)
                {
                    WaitUntil(nextDeadline);
                    var now = Stopwatch.GetTimestamp();
                    if (now >= _windowEnd)
                    {
                        return;
                    }

                    var leaseIndex = TryAcquireLease();
                    if (leaseIndex < 0)
                    {
                        NoLeaseCount++;
                    }
                    else
                    {
                        generation++;
                        var lease = _leases[leaseIndex];
                        try
                        {
                            Prepare(lease, generation, now);
                            var status = _submit!(lease);
                            if (status != FrameSubmissionStatus.Accepted)
                            {
                                throw new PerformanceMeasurementException(
                                    $"{_name} submission failed: {status}.");
                            }
                        }
                        catch
                        {
                            Volatile.Write(ref _leaseStates[leaseIndex], 0);
                            throw;
                        }
                    }

                    nextDeadline += _periodTicks;
                    var after = Stopwatch.GetTimestamp();
                    if (after > nextDeadline)
                    {
                        CadenceMissCount++;
                        nextDeadline = after + _periodTicks;
                    }
                }
            }
            catch (Exception exception)
            {
                Failure = exception;
            }
        }

        private int TryAcquireLease()
        {
            for (var index = 0; index < _leaseStates.Length; index++)
            {
                if (Interlocked.CompareExchange(ref _leaseStates[index], 1, 0) == 0)
                {
                    return index;
                }
            }

            return -1;
        }
    }

    private sealed class ScatterProducer : FixedCadenceProducer<ReusableRasterFrameLease>
    {
        private readonly MeasurementTimeline _timeline;
        private readonly AggregateFrame _frame;
        private readonly DensityBinner _binner;
        private readonly DensityGridBuffer _grid;
        private readonly DensityBinningRequest _request;
        private readonly RasterPixelWriter _writer;

        public ScatterProducer(MeasurementTimeline timeline)
            : base(
                "scatter",
                cadenceHz: 60,
                Enumerable.Range(0, 8)
                    .Select(_ => new ReusableRasterFrameLease(HardRasterSize, HardRasterSize))
                    .ToArray())
        {
            _timeline = timeline;
            _frame = SyntheticAggregateFrameFactory.Create(
                HardEventCount,
                seed: 0x5A17,
                generation: 1);
            _binner = new DensityBinner(HardEventCount);
            _grid = new DensityGridBuffer(HardRasterSize, HardRasterSize);
            _request = CreatePrimaryRequest();
            _writer = WritePixels;
        }

        protected override void Prepare(
            ReusableRasterFrameLease frame,
            long generation,
            long producerStartTick)
        {
            _timeline.RecordStart(generation, producerStartTick);
            frame.Prepare(generation, _writer);
        }

        private void WritePixels(Span<byte> pixels)
        {
            _binner.Bin(_frame, _request, _grid);
            DensityRasterizer.Rasterize(_grid, pixels);
        }
    }

    private sealed class GmiProducer : FixedCadenceProducer<ReusableGmiRasterFrameLease>
    {
        private readonly MeasurementTimeline _timeline;
        private readonly ushort[] _source = new ushort[GmiWaveformCount * GmiSampleCount];

        public GmiProducer(MeasurementTimeline timeline)
            : base(
                "gmi",
                cadenceHz: 10,
                Enumerable.Range(0, 4)
                    .Select(_ => new ReusableGmiRasterFrameLease(HardRasterSize, HardRasterSize))
                    .ToArray())
        {
            _timeline = timeline;
            SyntheticGmiSnapshotFactory.Fill(
                _source,
                GmiChannel.FsGmi,
                GmiWaveformCount,
                GmiSampleCount,
                seed: 0x6B28);
        }

        protected override void Prepare(
            ReusableGmiRasterFrameLease frame,
            long generation,
            long producerStartTick)
        {
            _timeline.RecordStart(generation, producerStartTick);
            frame.Prepare(
                generation,
                GmiChannel.FsGmi,
                GmiWaveformCount,
                GmiSampleCount,
                _source,
                new GmiDisplayRange(0.0, 16_383.0));
        }
    }

    private sealed class InputProbe : IDisposable
    {
        private readonly Dispatcher _dispatcher;
        private readonly Action[] _callbacks;
        private readonly long[] _enqueueTicks;
        private readonly long[] _completionTicks;
        private readonly long _periodTicks = Stopwatch.Frequency / 20;
        private Thread? _thread;
        private long _windowStart;
        private long _windowEnd;
        private int _count;
        private int _stop;

        public InputProbe(Dispatcher dispatcher, int capacity)
        {
            _dispatcher = dispatcher;
            _enqueueTicks = new long[capacity];
            _completionTicks = new long[capacity];
            _callbacks = new Action[capacity];
            for (var index = 0; index < capacity; index++)
            {
                var sampleIndex = index;
                _callbacks[index] = () =>
                    Volatile.Write(ref _completionTicks[sampleIndex], Stopwatch.GetTimestamp());
            }
        }

        public Exception? Failure { get; private set; }

        public long CadenceMissCount { get; private set; }

        public void OpenWindow(long start, long end)
        {
            _windowStart = start;
            _windowEnd = end;
        }

        public void Start()
        {
            _thread = new Thread(Run)
            {
                IsBackground = true,
                Name = "P0-R1-input-probe",
            };
            _thread.Start();
        }

        public void Stop()
        {
            Volatile.Write(ref _stop, 1);
            _thread?.Join();
        }

        public InputProbeSnapshot Snapshot()
        {
            var count = Volatile.Read(ref _count);
            var enqueue = _enqueueTicks.AsSpan(0, count).ToArray();
            var completion = _completionTicks.AsSpan(0, count).ToArray();
            var latency = new long[count];
            for (var index = 0; index < count; index++)
            {
                latency[index] = completion[index] > 0
                    ? completion[index] - enqueue[index]
                    : 0;
            }

            return new InputProbeSnapshot(enqueue, completion, latency);
        }

        public void Dispose() => Stop();

        private void Run()
        {
            try
            {
                var nextDeadline = _windowStart;
                while (Volatile.Read(ref _stop) == 0)
                {
                    WaitUntil(nextDeadline);
                    var now = Stopwatch.GetTimestamp();
                    if (now >= _windowEnd)
                    {
                        return;
                    }

                    var index = Volatile.Read(ref _count);
                    if (index >= _callbacks.Length)
                    {
                        throw new PerformanceMeasurementException(
                            "Input probe sample capacity was exhausted.");
                    }

                    Volatile.Write(ref _enqueueTicks[index], now);
                    _dispatcher.BeginInvoke(DispatcherPriority.Input, _callbacks[index]);
                    Volatile.Write(ref _count, index + 1);
                    nextDeadline += _periodTicks;
                    var after = Stopwatch.GetTimestamp();
                    if (after > nextDeadline)
                    {
                        CadenceMissCount++;
                        nextDeadline = after + _periodTicks;
                    }
                }
            }
            catch (Exception exception)
            {
                Failure = exception;
            }
        }
    }

    private sealed class MemorySampler
    {
        private readonly long _windowStart;
        private readonly long _windowEnd;
        private readonly FixedLongBuffer _timestamps = new(1_024);
        private readonly FixedLongBuffer _managedHeap = new(1_024);
        private readonly FixedLongBuffer _privateBytes = new(1_024);
        private Thread? _thread;
        private int _stop;

        public MemorySampler(long windowStart, long windowEnd)
        {
            _windowStart = windowStart;
            _windowEnd = windowEnd;
        }

        public Exception? Failure { get; private set; }

        public void Start()
        {
            _thread = new Thread(Run)
            {
                IsBackground = true,
                Name = "P0-R1-memory-sampler",
            };
            _thread.Start();
        }

        public void Stop()
        {
            Volatile.Write(ref _stop, 1);
            _thread?.Join();
        }

        public MemorySampleSnapshot Snapshot()
        {
            if (_timestamps.OverflowCount != 0 ||
                _managedHeap.OverflowCount != 0 ||
                _privateBytes.OverflowCount != 0)
            {
                throw new PerformanceMeasurementException(
                    "Memory sample capacity was exhausted.");
            }

            return new MemorySampleSnapshot(
                _timestamps.Snapshot(),
                _managedHeap.Snapshot(),
                _privateBytes.Snapshot());
        }

        private void Run()
        {
            try
            {
                var process = Process.GetCurrentProcess();
                var nextDeadline = _windowStart;
                while (Volatile.Read(ref _stop) == 0)
                {
                    WaitUntil(nextDeadline);
                    var now = Stopwatch.GetTimestamp();
                    if (now >= _windowEnd)
                    {
                        return;
                    }

                    process.Refresh();
                    if (!_timestamps.TryAdd(now) ||
                        !_managedHeap.TryAdd(GC.GetTotalMemory(forceFullCollection: false)) ||
                        !_privateBytes.TryAdd(process.PrivateMemorySize64))
                    {
                        throw new PerformanceMeasurementException(
                            "Memory sample capacity was exhausted.");
                    }

                    nextDeadline += Stopwatch.Frequency;
                }
            }
            catch (Exception exception)
            {
                Failure = exception;
            }
        }
    }

    private sealed class MeasurementTimeline
    {
        private readonly long[] _producerStartTicks;
        private readonly long[] _renderCompletionTicks;
        private long _windowStart;
        private long _plannedWindowEnd;
        private long _maximumGeneration;
        private int _overflow;

        public MeasurementTimeline(int capacity)
        {
            _producerStartTicks = new long[capacity + 1];
            _renderCompletionTicks = new long[capacity + 1];
        }

        public void OpenWindow(long start, long plannedEnd)
        {
            _windowStart = start;
            _plannedWindowEnd = plannedEnd;
        }

        public void RecordStart(long generation, long timestamp)
        {
            if (generation < 1 || generation >= _producerStartTicks.Length)
            {
                Volatile.Write(ref _overflow, 1);
                return;
            }

            Volatile.Write(ref _producerStartTicks[generation], timestamp);
            UpdateMaximum(ref _maximumGeneration, generation);
        }

        public void RecordCompletion(long generation, long timestamp)
        {
            if (generation < 1 || generation >= _renderCompletionTicks.Length)
            {
                Volatile.Write(ref _overflow, 1);
                return;
            }

            Volatile.Write(ref _renderCompletionTicks[generation], timestamp);
            UpdateMaximum(ref _maximumGeneration, generation);
        }

        public MeasurementTimelineSnapshot Snapshot()
        {
            if (Volatile.Read(ref _overflow) != 0)
            {
                throw new PerformanceMeasurementException(
                    "Measurement generation capacity was exhausted.");
            }

            var generations = new List<long>();
            var starts = new List<long>();
            var completions = new List<long>();
            var durations = new List<long>();
            var completionsInWindow = new List<long>();
            var maximum = Volatile.Read(ref _maximumGeneration);
            for (var generation = 1L; generation <= maximum; generation++)
            {
                var start = Volatile.Read(ref _producerStartTicks[generation]);
                var completion = Volatile.Read(ref _renderCompletionTicks[generation]);
                if (start < _windowStart || start >= _plannedWindowEnd || completion <= 0)
                {
                    continue;
                }

                generations.Add(generation);
                starts.Add(start);
                completions.Add(completion);
                durations.Add(Math.Max(1, completion - start));
                if (completion < _plannedWindowEnd)
                {
                    completionsInWindow.Add(completion);
                }
            }

            return new MeasurementTimelineSnapshot(
                generations.ToArray(),
                starts.ToArray(),
                completions.ToArray(),
                durations.ToArray(),
                completionsInWindow.ToArray());
        }

        private static void UpdateMaximum(ref long location, long value)
        {
            while (true)
            {
                var current = Volatile.Read(ref location);
                if (value <= current || Interlocked.CompareExchange(ref location, value, current) == current)
                {
                    return;
                }
            }
        }
    }

    private sealed class FixedLongBuffer
    {
        private readonly long[] _values;
        private int _count;
        private long _overflowCount;

        public FixedLongBuffer(int capacity) => _values = new long[capacity];

        public long OverflowCount => Interlocked.Read(ref _overflowCount);

        public bool TryAdd(long value)
        {
            var index = Interlocked.Increment(ref _count) - 1;
            if ((uint)index >= (uint)_values.Length)
            {
                Interlocked.Increment(ref _overflowCount);
                return false;
            }

            Volatile.Write(ref _values[index], value);
            return true;
        }

        public long[] Snapshot()
        {
            var count = Math.Min(Volatile.Read(ref _count), _values.Length);
            return _values.AsSpan(0, count).ToArray();
        }
    }

    private static void RunDispatcherUntil(Dispatcher dispatcher, long deadline)
    {
        var frame = new DispatcherFrame();
        using var timer = new Timer(
            _ => dispatcher.BeginInvoke(
                DispatcherPriority.Send,
                new Action(() => frame.Continue = false)),
            state: null,
            dueTime: ToDueTime(deadline),
            period: Timeout.InfiniteTimeSpan);
        Dispatcher.PushFrame(frame);
    }

    private static TimeSpan ToDueTime(long deadline)
    {
        var remaining = Math.Max(0, deadline - Stopwatch.GetTimestamp());
        return TimeSpan.FromSeconds(remaining / (double)Stopwatch.Frequency);
    }

    private static void WaitUntil(long deadline)
    {
        while (true)
        {
            var remaining = deadline - Stopwatch.GetTimestamp();
            if (remaining <= 0)
            {
                return;
            }

            var milliseconds = remaining * 1_000.0 / Stopwatch.Frequency;
            if (milliseconds > 2.0)
            {
                Thread.Sleep(Math.Max(1, (int)milliseconds - 1));
            }
            else
            {
                Thread.SpinWait(64);
            }
        }
    }
}

internal static class PerformanceExitCodes
{
    public const int Success = 0;
    public const int CommandLineContract = 2;
    public const int ReferenceProfile = 3;
    public const int MeasurementInvalid = 4;
    public const int NumericThresholdFailure = 5;
    public const int ArtifactFailure = 6;
}

internal sealed record PerformanceRunProvenance(
    string SourceRevision,
    bool SourceDirty,
    string SdkVersion,
    string DesktopRuntimeVersion,
    string TargetFramework,
    string Configuration,
    string Architecture)
{
    public static PerformanceRunProvenance Parse(
        string json,
        PerformanceExecutionMode mode)
    {
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        if (root.ValueKind != JsonValueKind.Object)
        {
            throw new PerformanceMeasurementException(
                "Performance provenance root must be a JSON object.");
        }

        RejectDuplicateProperties(root, "Performance provenance");
        var requiredFields = new HashSet<string>(
            [
                "schema_id",
                "source_revision",
                "source_dirty",
                "sdk_version",
                "desktop_runtime_version",
                "target_framework",
                "configuration",
                "architecture",
            ],
            StringComparer.Ordinal);
        var actualFields = root.EnumerateObject()
            .Select(property => property.Name)
            .ToHashSet(StringComparer.Ordinal);
        if (!requiredFields.SetEquals(actualFields))
        {
            throw new PerformanceMeasurementException(
                "Performance provenance must contain the exact versioned field set.");
        }

        var schemaId = RequiredString(root, "schema_id");
        if (!StringComparer.Ordinal.Equals(
                schemaId,
                "analogboard.scatter-rendering.provenance.v1"))
        {
            throw new PerformanceMeasurementException(
                $"Performance provenance schema mismatch: '{schemaId}'.");
        }

        var sourceRevision = RequiredString(root, "source_revision");
        if (sourceRevision.Length != 40 || !sourceRevision.All(Uri.IsHexDigit))
        {
            throw new PerformanceMeasurementException(
                "Performance provenance source_revision must be a 40-character Git object id.");
        }

        var sourceDirty = RequiredBoolean(root, "source_dirty");
        if (mode == PerformanceExecutionMode.Official && sourceDirty)
        {
            throw new PerformanceMeasurementException(
                "Official performance provenance requires a clean source revision.");
        }

        var provenance = new PerformanceRunProvenance(
            sourceRevision,
            sourceDirty,
            RequiredString(root, "sdk_version"),
            RequiredString(root, "desktop_runtime_version"),
            RequiredString(root, "target_framework"),
            RequiredString(root, "configuration"),
            RequiredString(root, "architecture"));
        var actual = new[]
        {
            provenance.SdkVersion,
            provenance.DesktopRuntimeVersion,
            provenance.TargetFramework,
            provenance.Configuration,
            provenance.Architecture,
        };
        var expected = new[]
        {
            "10.0.302",
            "10.0.10",
            "net10.0-windows",
            "Release",
            "x64",
        };
        if (!actual.SequenceEqual(expected, StringComparer.Ordinal))
        {
            throw new PerformanceMeasurementException(
                "Performance provenance does not match the pinned toolchain contract.");
        }

        return provenance;
    }

    private static void RejectDuplicateProperties(JsonElement element, string description)
    {
        var names = new HashSet<string>(StringComparer.Ordinal);
        foreach (var property in element.EnumerateObject())
        {
            if (!names.Add(property.Name))
            {
                throw new PerformanceMeasurementException(
                    $"{description} must not contain duplicate JSON property names: {property.Name}.");
            }
        }
    }

    private static string RequiredString(JsonElement root, string name)
    {
        if (!root.TryGetProperty(name, out var element) ||
            element.ValueKind != JsonValueKind.String ||
            string.IsNullOrWhiteSpace(element.GetString()))
        {
            throw new PerformanceMeasurementException(
                $"Performance provenance field must be a non-empty string: {name}.");
        }

        return element.GetString()!;
    }

    private static bool RequiredBoolean(JsonElement root, string name)
    {
        if (!root.TryGetProperty(name, out var element) ||
            element.ValueKind is not JsonValueKind.True and not JsonValueKind.False)
        {
            throw new PerformanceMeasurementException(
                $"Performance provenance field must be a JSON boolean: {name}.");
        }

        return element.GetBoolean();
    }
}

internal sealed record PerformanceWorkloadContract(
    string ContractId,
    int AggregateSeed,
    int GmiSeed,
    int EventCount,
    int RasterWidth,
    int RasterHeight,
    int ScatterTileCount,
    int GmiTileCount,
    int GmiWaveformCount,
    int GmiSampleCount,
    int ScatterCadenceHz,
    int GmiCadenceHz,
    int InputCadenceHz,
    int ScatterLeasePoolSize,
    int GmiLeasePoolSize,
    string MetricSchemaId,
    string MetricDefinitionsSha256,
    string RawTickUnit,
    string AllocationProbeCounter,
    string CadencePolicy,
    string FrameTimeBoundary,
    string WindowInclusion,
    string PercentileMethod)
{
    public static PerformanceWorkloadContract Create(PerformanceScenario scenario) =>
        new(
            "AB-PERF-RUNNER-v1",
            scenario == PerformanceScenario.Headroom ? 0x7C39 : 0x5A17,
            scenario switch
            {
                PerformanceScenario.HardCombined or PerformanceScenario.Soak => 0x6B28,
                PerformanceScenario.Headroom => 0x8D4A,
                _ => 0,
            },
            scenario == PerformanceScenario.Headroom ? 131_072 : 100_001,
            scenario == PerformanceScenario.Headroom ? 1_024 : 512,
            scenario == PerformanceScenario.Headroom ? 1_024 : 512,
            scenario == PerformanceScenario.Headroom ? 2 : 1,
            scenario is PerformanceScenario.HardCombined or PerformanceScenario.Soak or PerformanceScenario.Headroom
                ? 1
                : 0,
            scenario is PerformanceScenario.HardCombined or PerformanceScenario.Soak or PerformanceScenario.Headroom
                ? 100
                : 0,
            scenario is PerformanceScenario.HardCombined or PerformanceScenario.Soak or PerformanceScenario.Headroom
                ? 2_400
                : 0,
            scenario == PerformanceScenario.Headroom ? 0 : 60,
            scenario is PerformanceScenario.HardCombined or PerformanceScenario.Soak ? 10 : 0,
            scenario == PerformanceScenario.Headroom ? 0 : 20,
            scenario == PerformanceScenario.Headroom ? 0 : 8,
            scenario is PerformanceScenario.HardCombined or PerformanceScenario.Soak ? 4 : 0,
            PerformanceMetricSchema.SchemaId,
            PerformanceMetricEvidence.DefinitionsSha256,
            "stopwatch_ticks",
            scenario == PerformanceScenario.Headroom
                ? "not_applicable"
                : "GC.GetAllocatedBytesForCurrentThread",
            "absolute-deadline-no-catch-up-burst",
            "producer-start-before-binning-to-writepixels-return",
            "producer-start-in-window; completion-latency-includes-drained-tail; delivered-rate-completion-in-window",
            "nearest-rank-ceiling-no-interpolation");
}

internal static class PerformanceMetricEvidence
{
    public static string DefinitionsSha256 { get; } = CreateDefinitionsSha256();

    private static string CreateDefinitionsSha256()
    {
        var canonical = string.Join(
            "\n",
            PerformanceMetricSchema.Definitions.Select(
                definition => $"{definition.Name}={definition.Unit}")) + "\n";
        return Convert.ToHexString(
            SHA256.HashData(Encoding.UTF8.GetBytes(canonical))).ToLowerInvariant();
    }
}

internal sealed record PerformanceScheduleEvidence(
    long StopwatchFrequency,
    long PlannedWarmupDurationTicks,
    long ActualWarmupDurationTicks,
    long PlannedWindowDurationTicks,
    long ActualWindowDurationTicks,
    int HardRunCount);

internal sealed record PerformanceLoadedRuntime(
    string FrameworkDescription,
    string CoreRuntimeInformationalVersion,
    string DesktopRuntimeInformationalVersion,
    string RollForwardPolicy)
{
    public static PerformanceLoadedRuntime Capture() =>
        new(
            System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription,
            InformationalVersion(typeof(object).Assembly),
            InformationalVersion(typeof(Dispatcher).Assembly),
            "Disable");

    private static string InformationalVersion(System.Reflection.Assembly assembly)
    {
        var value = assembly
            .GetCustomAttributes(
                typeof(System.Reflection.AssemblyInformationalVersionAttribute),
                inherit: false)
            .OfType<System.Reflection.AssemblyInformationalVersionAttribute>()
            .SingleOrDefault()?.InformationalVersion;
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new PerformanceMeasurementException(
                $"Loaded runtime assembly has no informational version: {assembly.FullName}.");
        }

        var version = System.Text.RegularExpressions.Regex.Match(
            value,
            @"^\d+\.\d+\.\d+");
        if (!version.Success)
        {
            throw new PerformanceMeasurementException(
                $"Loaded runtime informational version has no semantic patch prefix: {value}.");
        }

        return version.Value;
    }
}

internal sealed record PerformanceRawArtifact(
    string SchemaId,
    string RunnerContractId,
    bool DevelopmentOnly,
    bool OfficialEligible,
    bool OfficialAcceptance,
    bool MaySubstituteOfficial,
    string FixtureId,
    int ProcessId,
    string MachineName,
    DateTimeOffset StartedAtUtc,
    DateTimeOffset FinishedAtUtc,
    string? ReferenceProfileSha256,
    string ObservedProfileSha256,
    string ProvenanceSha256,
    string SourceRevision,
    bool SourceDirty,
    PerformanceLoadedRuntime LoadedRuntime,
    PerformanceWorkloadContract Workload,
    PerformanceScheduleEvidence Schedule,
    PerformanceRawRun Raw,
    PerformanceRawDiagnostics Diagnostics,
    PerformanceRunVerdict Verdict);

internal sealed record PerformanceRawDiagnostics(
    long[] ScatterGeneration,
    long[] ScatterProducerStartTicks,
    long[] ScatterRenderCompletionTicks,
    long[] GmiGeneration,
    long[] GmiProducerStartTicks,
    long[] GmiRenderCompletionTicks,
    long[] InputEnqueueTicks,
    long[] InputCompletionTicks,
    long[] MemorySampleTicks,
    long[] ManagedHeapSamples,
    long[] PrivateByteSamples,
    long ScatterCadenceMissCount,
    long GmiCadenceMissCount,
    long InputCadenceMissCount,
    long ScatterNoLeaseCount,
    long GmiNoLeaseCount,
    long ScatterAcceptedCount,
    long ScatterRenderedCount,
    long ScatterCoalescedCount,
    long GmiAcceptedCount,
    long GmiRenderedCount,
    long GmiCoalescedCount,
    long[] OneEventAllocationProbeSamples,
    long[] HardEventAllocationProbeSamples,
    long[] PairedAllocationDeltaSamples);

internal sealed record MeasuredPerformanceRun(
    DateTimeOffset StartedAtUtc,
    DateTimeOffset FinishedAtUtc,
    PerformanceScheduleEvidence Schedule,
    PerformanceRawRun Raw,
    PerformanceRawDiagnostics Diagnostics);

internal sealed record VisualizationMeasurementResult(
    DateTimeOffset StartedAtUtc,
    DateTimeOffset FinishedAtUtc,
    long WindowStartTick,
    long WindowEndTick,
    long[] ScatterGeneration,
    long[] ScatterProducerStartTicks,
    long[] ScatterRenderCompletionTicks,
    long[] ScatterFrameDurationTicks,
    long[] ScatterCompletionTicks,
    long[] GmiGeneration,
    long[] GmiProducerStartTicks,
    long[] GmiRenderCompletionTicks,
    long[] GmiCompletionTicks,
    long[] InputEnqueueTicks,
    long[] InputCompletionTicks,
    long[] InputLatencyTicks,
    LatestFrameSchedulerMetricsSnapshot ScatterMetrics,
    LatestFrameSchedulerMetricsSnapshot? GmiMetrics,
    long ProcessAllocatedBytes,
    long RetainedManagedHeapGrowthBytes,
    long StartingPrivateBytes,
    long PrivateBytesGrowth,
    long[] MemorySampleTicks,
    long[] ManagedHeapSamples,
    long[] PrivateByteSamples,
    long ScatterCadenceMissCount,
    long GmiCadenceMissCount,
    long InputCadenceMissCount,
    long ScatterNoLeaseCount,
    long GmiNoLeaseCount);

internal sealed record MeasurementTimelineSnapshot(
    long[] Generation,
    long[] ProducerStartTicks,
    long[] RenderCompletionTicks,
    long[] FrameDurationTicks,
    long[] CompletionTicksWithinWindow);

internal sealed record InputProbeSnapshot(
    long[] EnqueueTicks,
    long[] CompletionTicks,
    long[] LatencyTicks);

internal sealed record MemorySampleSnapshot(
    long[] TimestampTicks,
    long[] ManagedHeapBytes,
    long[] PrivateBytes)
{
    public static MemorySampleSnapshot Empty { get; } = new([], [], []);
}

internal sealed record AllocationProbeResult(
    long[] OneEventSamples,
    long[] HardEventSamples,
    long[] PairedDeltaSamples,
    long OneEventBytes,
    long HardEventBytes,
    long MaximumDeltaBytes,
    int InvalidPairCount);

internal sealed class PerformanceMeasurementException : Exception
{
    public PerformanceMeasurementException(string message)
        : base(message)
    {
    }

    public PerformanceMeasurementException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}

internal sealed class PerformanceArtifactException : Exception
{
    public PerformanceArtifactException(string message)
        : base(message)
    {
    }
}
