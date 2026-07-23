using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;
using System.Windows.Threading;
using AnalogBoard.ScatterRendering.Core;
using AnalogBoard.ScatterRendering.Wpf;

namespace AnalogBoard.ScatterRendering.Tests;

internal static class Batch4DevelopmentObservationTests
{
    private const int CombinedEventCount = 100_001;
    private const int HeadroomEventCount = 131_072;
    private const int CombinedRasterSize = 512;
    private const int HeadroomRasterSize = 1_024;
    private const int GmiWaveformCount = 100;
    private const int GmiSampleCount = 2_400;

    public static IReadOnlyList<ContractTestCase> Cases { get; } =
    [
        new(nameof(GivenHardCombinedFixtureOnCompatiblePc_WhenMeasured_ThenNonOfficialObservationIsEmitted),
            GivenHardCombinedFixtureOnCompatiblePc_WhenMeasured_ThenNonOfficialObservationIsEmitted),
        new(nameof(GivenThreeTileHeadroomFixtureOnCompatiblePc_WhenMeasured_ThenSeparateNonOfficialObservationIsEmitted),
            GivenThreeTileHeadroomFixtureOnCompatiblePc_WhenMeasured_ThenSeparateNonOfficialObservationIsEmitted),
    ];

    private static void GivenHardCombinedFixtureOnCompatiblePc_WhenMeasured_ThenNonOfficialObservationIsEmitted()
    {
        const int warmupIterations = 3;
        const int measurementIterations = 10;
        var dispatcher = Dispatcher.CurrentDispatcher;
        var aggregateFrame = SyntheticAggregateFrameFactory.Create(
            CombinedEventCount,
            seed: 0x5A17,
            generation: 1);
        var binner = new DensityBinner(CombinedEventCount);
        var grid = new DensityGridBuffer(CombinedRasterSize, CombinedRasterSize);
        var request = CreatePrimaryRequest();
        var scatterLease = new ReusableRasterFrameLease(CombinedRasterSize, CombinedRasterSize);
        var gmiLease = new ReusableGmiRasterFrameLease(CombinedRasterSize, CombinedRasterSize);
        var gmiSource = new ushort[GmiWaveformCount * GmiSampleCount];
        var gmiRange = new GmiDisplayRange(0.0, 16_383.0);
        RasterPixelWriter scatterWriter = pixels =>
        {
            binner.Bin(aggregateFrame, request, grid);
            DensityRasterizer.Rasterize(grid, pixels);
        };
        var iterationTicks = new long[measurementIterations];
        var inputLatencyTicks = new long[measurementIterations];
        var gmiRenderTicks = new long[measurementIterations];
        var gmiIndex = 0;
        var inputCompletionCount = 0;
        var measuring = false;
        using var scatterRenderStarting = new AutoResetEvent(initialState: false);
        using var inputQueued = new AutoResetEvent(initialState: false);
        var inputProbe = new Thread(() =>
        {
            for (var index = 0; index < measurementIterations; index++)
            {
                scatterRenderStarting.WaitOne();
                var sampleIndex = index;
                var queuedAt = Stopwatch.GetTimestamp();
                dispatcher.BeginInvoke(DispatcherPriority.Input, () =>
                {
                    inputLatencyTicks[sampleIndex] = Stopwatch.GetTimestamp() - queuedAt;
                    Interlocked.Increment(ref inputCompletionCount);
                });
                inputQueued.Set();
            }
        });

        using var harness = new CombinedVisualizationHarness<ReusableRasterFrameLease, ReusableGmiRasterFrameLease>(
            dispatcher,
            CombinedRasterSize,
            CombinedRasterSize,
            _ => { },
            _ => { },
            metricCapacity: 64,
            renderStarting: (feed, _) =>
            {
                if (!measuring)
                {
                    return;
                }

                if (feed == VisualizationFeed.Scatter)
                {
                    scatterRenderStarting.Set();
                    inputQueued.WaitOne();
                }
                else
                {
                    gmiRenderTicks[gmiIndex++] = Stopwatch.GetTimestamp();
                }
            });

        void PreparePixels(long generation)
        {
            scatterLease.Prepare(generation, scatterWriter);
            SyntheticGmiSnapshotFactory.Fill(
                gmiSource,
                GmiChannel.FsGmi,
                GmiWaveformCount,
                GmiSampleCount,
                seed: 0x6B28 + checked((int)generation));
            gmiLease.Prepare(
                generation,
                GmiChannel.FsGmi,
                GmiWaveformCount,
                GmiSampleCount,
                gmiSource,
                gmiRange);
        }

        void SubmitAndDrain()
        {
            ContractAssert.Equal(FrameSubmissionStatus.Accepted, harness.SubmitScatter(scatterLease));
            ContractAssert.Equal(FrameSubmissionStatus.Accepted, harness.SubmitGmi(gmiLease));
            PumpDispatcherToIdle(dispatcher);
        }

        for (var iteration = 0; iteration < warmupIterations; iteration++)
        {
            PreparePixels(iteration + 1L);
            SubmitAndDrain();
        }

        measuring = true;
        inputProbe.Start();
        var allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
        var measurementStartedAt = Stopwatch.GetTimestamp();
        for (var iteration = 0; iteration < measurementIterations; iteration++)
        {
            var startedAt = Stopwatch.GetTimestamp();
            PreparePixels(warmupIterations + iteration + 1L);
            SubmitAndDrain();
            iterationTicks[iteration] = Stopwatch.GetTimestamp() - startedAt;
        }
        var measurementFinishedAt = Stopwatch.GetTimestamp();
        var allocatedBytes = GC.GetAllocatedBytesForCurrentThread() - allocatedBefore;
        measuring = false;
        inputProbe.Join();

        var scatterMetrics = harness.GetScatterMetricsSnapshot();
        var gmiMetrics = harness.GetGmiMetricsSnapshot();
        ContractAssert.Equal(measurementIterations, inputCompletionCount);
        ContractAssert.Equal(measurementIterations, gmiIndex);
        ContractAssert.Equal(1, scatterMetrics.PendingFrameMaximum);
        ContractAssert.Equal(1, scatterMetrics.PendingCallbackMaximum);
        ContractAssert.Equal(1, gmiMetrics.PendingFrameMaximum);
        ContractAssert.Equal(1, gmiMetrics.PendingCallbackMaximum);
        ContractAssert.Equal(warmupIterations + measurementIterations, scatterMetrics.RenderedCount);
        ContractAssert.Equal(warmupIterations + measurementIterations, gmiMetrics.RenderedCount);

        var elapsedSeconds = ToSeconds(measurementFinishedAt - measurementStartedAt);
        var gmiMaximumIntervalMilliseconds = 0.0;
        for (var index = 1; index < gmiRenderTicks.Length; index++)
        {
            gmiMaximumIntervalMilliseconds = Math.Max(
                gmiMaximumIntervalMilliseconds,
                ToMilliseconds(gmiRenderTicks[index] - gmiRenderTicks[index - 1]));
        }

        var observation = new Dictionary<string, object>
        {
            ["schema_id"] = "analogboard.scatter-rendering.combined-development-observation.v1",
            ["observation_kind"] = "hard-combined-compatible-pc-smoke",
            ["development_only"] = true,
            ["official_acceptance"] = false,
            ["may_substitute_official"] = false,
            ["fixture_id"] = "AB-P0-R1-HARD-COMBINED-v1",
            ["event_count"] = CombinedEventCount,
            ["scatter_width"] = CombinedRasterSize,
            ["scatter_height"] = CombinedRasterSize,
            ["gmi_width"] = CombinedRasterSize,
            ["gmi_height"] = CombinedRasterSize,
            ["selected_gmi_channel"] = GmiChannelSchema.NameFor(GmiChannel.FsGmi),
            ["gmi_waveform_count"] = GmiWaveformCount,
            ["gmi_sample_count"] = GmiSampleCount,
            ["warmup_iterations"] = warmupIterations,
            ["measurement_iterations"] = measurementIterations,
            ["combined_iteration_ms_p95"] = PercentileMilliseconds(iterationTicks, 0.95),
            ["combined_iteration_ms_max"] = MaximumMilliseconds(iterationTicks),
            ["scatter_delivered_update_rate_hz"] = measurementIterations / elapsedSeconds,
            ["gmi_delivered_update_rate_hz"] = measurementIterations / elapsedSeconds,
            ["gmi_max_update_interval_ms"] = gmiMaximumIntervalMilliseconds,
            ["input_latency_ms_p95"] = PercentileMilliseconds(inputLatencyTicks, 0.95),
            ["input_latency_ms_max"] = MaximumMilliseconds(inputLatencyTicks),
            ["allocated_bytes_per_iteration"] = allocatedBytes / measurementIterations,
            ["scatter_publish_p99_ms"] = scatterMetrics.PublicationDurationP99Milliseconds,
            ["gmi_publish_p99_ms"] = gmiMetrics.PublicationDurationP99Milliseconds,
            ["scatter_pending_frame_max"] = scatterMetrics.PendingFrameMaximum,
            ["scatter_pending_callback_max"] = scatterMetrics.PendingCallbackMaximum,
            ["gmi_pending_frame_max"] = gmiMetrics.PendingFrameMaximum,
            ["gmi_pending_callback_max"] = gmiMetrics.PendingCallbackMaximum,
            ["scatter_rendered_count"] = scatterMetrics.RenderedCount,
            ["gmi_rendered_count"] = gmiMetrics.RenderedCount,
            ["scatter_coalesced_count"] = scatterMetrics.CoalescedCount,
            ["gmi_coalesced_count"] = gmiMetrics.CoalescedCount,
            ["input_sentinel_count"] = inputCompletionCount,
            ["dispatcher_priority_contract"] = "Input_above_Background",
            ["scatter_raster_sha256"] = Sha256(scatterLease.BgraPixels),
            ["gmi_raster_sha256"] = Sha256(gmiLease.BgraPixels),
            ["machine"] = Environment.MachineName,
        };
        Console.WriteLine($"COMBINED_DEVELOPMENT_OBSERVATION {JsonSerializer.Serialize(observation)}");
    }

    private static void GivenThreeTileHeadroomFixtureOnCompatiblePc_WhenMeasured_ThenSeparateNonOfficialObservationIsEmitted()
    {
        const int warmupIterations = 1;
        const int measurementIterations = 3;
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
        var scatterPixelsA = new byte[
            HeadroomRasterSize * HeadroomRasterSize * DensityRasterizer.BytesPerPixel];
        var scatterPixelsB = new byte[
            HeadroomRasterSize * HeadroomRasterSize * DensityRasterizer.BytesPerPixel];
        var gmiCoverage = new int[HeadroomRasterSize * HeadroomRasterSize];
        var gmiPixels = new byte[
            HeadroomRasterSize * HeadroomRasterSize * DensityRasterizer.BytesPerPixel];
        using var surfaceA = new WriteableBitmapDensitySurface(HeadroomRasterSize, HeadroomRasterSize);
        using var surfaceB = new WriteableBitmapDensitySurface(HeadroomRasterSize, HeadroomRasterSize);
        using var gmiSurface = new WriteableBitmapDensitySurface(HeadroomRasterSize, HeadroomRasterSize);
        var generation = 0L;

        void RenderThreeTiles()
        {
            binnerA.Bin(frame, CreatePrimaryRequest(), gridA);
            binnerB.Bin(frame, CreateSecondaryRequest(), gridB);
            DensityRasterizer.Rasterize(gridA, scatterPixelsA);
            DensityRasterizer.Rasterize(gridB, scatterPixelsB);
            GmiOverlayRasterizer.Rasterize(
                snapshot,
                new GmiDisplayRange(0.0, 16_383.0),
                HeadroomRasterSize,
                HeadroomRasterSize,
                gmiCoverage,
                gmiPixels);
            generation++;
            surfaceA.Publish(generation, scatterPixelsA);
            surfaceB.Publish(generation, scatterPixelsB);
            gmiSurface.Publish(generation, gmiPixels);
        }

        for (var iteration = 0; iteration < warmupIterations; iteration++)
        {
            RenderThreeTiles();
        }

        var frameTicks = new long[measurementIterations];
        var allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
        for (var iteration = 0; iteration < measurementIterations; iteration++)
        {
            var startedAt = Stopwatch.GetTimestamp();
            RenderThreeTiles();
            frameTicks[iteration] = Stopwatch.GetTimestamp() - startedAt;
        }
        var allocatedBytes = GC.GetAllocatedBytesForCurrentThread() - allocatedBefore;

        var observation = new Dictionary<string, object>
        {
            ["schema_id"] = "analogboard.scatter-rendering.headroom-development-observation.v1",
            ["observation_kind"] = "three-tile-headroom-compatible-pc",
            ["development_only"] = true,
            ["official_acceptance"] = false,
            ["may_substitute_hard_gate"] = false,
            ["fixture_id"] = "AB-P0-R1-HEADROOM-v1",
            ["event_count"] = HeadroomEventCount,
            ["width"] = HeadroomRasterSize,
            ["height"] = HeadroomRasterSize,
            ["tile_count"] = 3,
            ["scatter_tile_count"] = 2,
            ["gmi_tile_count"] = 1,
            ["selected_gmi_channel"] = GmiChannelSchema.NameFor(GmiChannel.FsGmi),
            ["gmi_waveform_count"] = GmiWaveformCount,
            ["gmi_sample_count"] = GmiSampleCount,
            ["warmup_iterations"] = warmupIterations,
            ["measurement_iterations"] = measurementIterations,
            ["three_tile_iteration_ms_p95"] = PercentileMilliseconds(frameTicks, 0.95),
            ["three_tile_iteration_ms_max"] = MaximumMilliseconds(frameTicks),
            ["allocated_bytes_per_iteration"] = allocatedBytes / measurementIterations,
            ["scatter_a_raster_sha256"] = Sha256(scatterPixelsA),
            ["scatter_b_raster_sha256"] = Sha256(scatterPixelsB),
            ["gmi_raster_sha256"] = Sha256(gmiPixels),
            ["machine"] = Environment.MachineName,
        };
        Console.WriteLine($"HEADROOM_DEVELOPMENT_OBSERVATION {JsonSerializer.Serialize(observation)}");
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

    private static double PercentileMilliseconds(long[] values, double percentile)
    {
        var sorted = (long[])values.Clone();
        Array.Sort(sorted);
        var index = Math.Max(0, (int)Math.Ceiling(percentile * sorted.Length) - 1);
        return ToMilliseconds(sorted[index]);
    }

    private static double MaximumMilliseconds(long[] values) =>
        ToMilliseconds(values.Max());

    private static double ToMilliseconds(long ticks) =>
        ticks * 1_000.0 / Stopwatch.Frequency;

    private static double ToSeconds(long ticks) =>
        ticks / (double)Stopwatch.Frequency;

    private static string Sha256(ReadOnlySpan<byte> pixels) =>
        Convert.ToHexString(SHA256.HashData(pixels)).ToLowerInvariant();

    private static void PumpDispatcherToIdle(Dispatcher dispatcher)
    {
        var frame = new DispatcherFrame();
        dispatcher.BeginInvoke(DispatcherPriority.ApplicationIdle, () => frame.Continue = false);
        Dispatcher.PushFrame(frame);
    }

}
