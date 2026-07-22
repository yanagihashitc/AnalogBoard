using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;
using AnalogBoard.ScatterRendering.Core;
using AnalogBoard.ScatterRendering.Wpf;

namespace AnalogBoard.ScatterRendering.Tests;

internal static class DevelopmentPerformanceObservationTests
{
    private const int EventCount = 100_001;
    private const int RasterSize = 512;
    private const int WarmupIterations = 3;
    private const int MeasurementIterations = 10;
    private const int PublicationIterations = 1_000;

    public static IReadOnlyList<ContractTestCase> Cases { get; } =
    [
        new(nameof(GivenHardScatterFixtureOnCompatiblePc_WhenMeasured_ThenBoundedNonOfficialObservationIsEmitted),
            GivenHardScatterFixtureOnCompatiblePc_WhenMeasured_ThenBoundedNonOfficialObservationIsEmitted),
    ];

    private static void GivenHardScatterFixtureOnCompatiblePc_WhenMeasured_ThenBoundedNonOfficialObservationIsEmitted()
    {
        // Given: The hard scatter fixture and fully preallocated bin/raster/publication state.
        var frame = SyntheticAggregateFrameFactory.Create(EventCount, seed: 0x5A17, generation: 1);
        var binner = new DensityBinner(EventCount);
        var grid = new DensityGridBuffer(RasterSize, RasterSize);
        var pixels = new byte[RasterSize * RasterSize * DensityRasterizer.BytesPerPixel];
        var request = new DensityBinningRequest(
            new DensityAxisOptions(
                FeatureIndex: 0,
                Transform: DisplayTransformKind.Linear,
                RawRange: new DensityRange(-100_000.0, 300_000.0)),
            new DensityAxisOptions(
                FeatureIndex: 1,
                Transform: DisplayTransformKind.Asinh,
                RawRange: new DensityRange(-100_000.0, 300_000.0)),
            DensityOutlierMode.Exclude);
        using var surface = new WriteableBitmapDensitySurface(RasterSize, RasterSize);
        long publicationGeneration = 0;

        void RenderOne()
        {
            binner.Bin(frame, request, grid);
            DensityRasterizer.Rasterize(grid, pixels);
            surface.Publish(++publicationGeneration, pixels);
        }

        for (var iteration = 0; iteration < WarmupIterations; iteration++)
        {
            RenderOne();
        }

        // When: Measuring ten frames plus a separate 1,000-submit pending-one scheduler smoke.
        var frameTicks = new long[MeasurementIterations];
        var allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
        for (var iteration = 0; iteration < MeasurementIterations; iteration++)
        {
            var startedAt = Stopwatch.GetTimestamp();
            RenderOne();
            frameTicks[iteration] = Stopwatch.GetTimestamp() - startedAt;
        }
        var allocatedBytes = GC.GetAllocatedBytesForCurrentThread() - allocatedBefore;

        var poster = new SingleSlotUiWorkPoster();
        var schedulerFrame = new ReusableObservationFrame();
        using var scheduler = new LatestFrameScheduler<ReusableObservationFrame>(
            poster,
            _ => { },
            _ => { },
            metricCapacity: 2_048);
        for (var iteration = 1; iteration <= PublicationIterations; iteration++)
        {
            schedulerFrame.GenerationValue = iteration;
            ContractAssert.Equal(FrameSubmissionStatus.Accepted, scheduler.Submit(schedulerFrame));
            poster.Drain();
        }
        var schedulerMetrics = scheduler.GetMetricsSnapshot();

        // Then: Emit bounded compatible-PC numbers without applying or claiming official gates.
        Array.Sort(frameTicks);
        var p95Index = Math.Max(0, (int)Math.Ceiling(0.95 * frameTicks.Length) - 1);
        var frameP95Milliseconds = ToMilliseconds(frameTicks[p95Index]);
        var frameMaxMilliseconds = ToMilliseconds(frameTicks[^1]);
        var allocatedBytesPerFrame = allocatedBytes / MeasurementIterations;
        var rasterSha256 = Convert.ToHexString(SHA256.HashData(pixels)).ToLowerInvariant();

        ContractAssert.Equal(1, schedulerMetrics.PendingFrameMaximum);
        ContractAssert.Equal(1, schedulerMetrics.PendingCallbackMaximum);
        ContractAssert.Equal(1, poster.MaximumPendingCount);
        ContractAssert.Equal(0L, schedulerMetrics.CoalescedCount);
        ContractAssert.Equal(PublicationIterations, schedulerMetrics.RenderedCount);
        ContractAssert.Equal(PublicationIterations, poster.AcceptedCallbackCount);
        ContractAssert.Equal(PublicationIterations, poster.CompletedCallbackCount);
        ContractAssert.Equal(0L, poster.AbortedCallbackCount);
        ContractAssert.Equal(true, double.IsFinite(frameP95Milliseconds));
        ContractAssert.Equal(true, frameMaxMilliseconds >= frameP95Milliseconds);
        ContractAssert.Equal(64, rasterSha256.Length);

        var observation = new Dictionary<string, object>
        {
            ["schema_id"] = "analogboard.scatter-rendering.development-observation.v1",
            ["development_only"] = true,
            ["official_acceptance"] = false,
            ["fixture_id"] = "AB-P0-R1-HARD-SCATTER-v1",
            ["event_count"] = EventCount,
            ["width"] = RasterSize,
            ["height"] = RasterSize,
            ["iterations"] = MeasurementIterations,
            ["frame_ms_p95"] = frameP95Milliseconds,
            ["frame_ms_max"] = frameMaxMilliseconds,
            ["allocated_bytes_per_frame"] = allocatedBytesPerFrame,
            ["core_scheduler_test_double_submit_p99_ms"] = schedulerMetrics.PublicationDurationP99Milliseconds,
            ["poster_identity"] = "single_slot_test_double",
            ["pending_work_max"] = poster.MaximumPendingCount,
            ["logical_pending_work_max"] = schedulerMetrics.PendingCallbackMaximum,
            ["poster_accepted_callbacks"] = poster.AcceptedCallbackCount,
            ["poster_completed_callbacks"] = poster.CompletedCallbackCount,
            ["poster_aborted_callbacks"] = poster.AbortedCallbackCount,
            ["coalesced_frames"] = schedulerMetrics.CoalescedCount,
            ["raster_sha256"] = rasterSha256,
            ["machine"] = Environment.MachineName,
        };
        Console.WriteLine($"OBSERVATION {JsonSerializer.Serialize(observation)}");
    }

    private static double ToMilliseconds(long ticks) =>
        ticks * 1_000.0 / Stopwatch.Frequency;

    private sealed class ReusableObservationFrame : ILatestFrameLease
    {
        private int _schedulerOwned;
        private long _schedulerGeneration;

        public long GenerationValue { get; set; }

        public long Generation => GenerationValue;

        public long PublicationTimestamp { get; set; }

        public long SchedulerGeneration => Volatile.Read(ref _schedulerGeneration);

        public bool TryAcquireSchedulerOwnership()
        {
            if (Interlocked.CompareExchange(ref _schedulerOwned, 1, 0) != 0)
            {
                return false;
            }

            Volatile.Write(ref _schedulerGeneration, GenerationValue);
            return true;
        }

        public void ReleaseSchedulerOwnership()
        {
            if (Interlocked.Exchange(ref _schedulerOwned, 0) != 1)
            {
                throw new InvalidOperationException("Observation frame is not scheduler-owned.");
            }
        }
    }

    private sealed class SingleSlotUiWorkPoster : IUiWorkPoster
    {
        private Action? _pending;
        private bool _insideCallback;

        public int MaximumPendingCount { get; private set; }

        public long AcceptedCallbackCount { get; private set; }

        public long CompletedCallbackCount { get; private set; }

        public long AbortedCallbackCount { get; private set; }

        public bool CheckAccess() => _insideCallback;

        public bool TryPost(Action callback)
        {
            if (_pending is not null)
            {
                return false;
            }

            _pending = callback;
            AcceptedCallbackCount++;
            MaximumPendingCount = 1;
            return true;
        }

        public void Drain()
        {
            var callback = _pending
                ?? throw new InvalidOperationException("Development poster has no pending callback.");
            _pending = null;
            _insideCallback = true;
            try
            {
                callback();
            }
            finally
            {
                _insideCallback = false;
                CompletedCallbackCount++;
            }
        }
    }
}
