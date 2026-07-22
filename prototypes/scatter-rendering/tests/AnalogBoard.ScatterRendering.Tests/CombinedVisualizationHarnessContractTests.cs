using System.Collections.Concurrent;
using System.Windows.Threading;
using AnalogBoard.ScatterRendering.Core;
using AnalogBoard.ScatterRendering.Wpf;

namespace AnalogBoard.ScatterRendering.Tests;

internal static class CombinedVisualizationHarnessContractTests
{
    public static IReadOnlyList<ContractTestCase> Cases { get; } =
    [
        new(nameof(GivenScatterGmiAndInputBacklog_WhenDispatcherPumped_ThenInputAndFeedBoundsRemainIndependent),
            GivenScatterGmiAndInputBacklog_WhenDispatcherPumped_ThenInputAndFeedBoundsRemainIndependent),
        new(nameof(GivenFramesRearmedDuringRendering_WhenDispatcherPumped_ThenInputRunsBeforeContinuedFeeds),
            GivenFramesRearmedDuringRendering_WhenDispatcherPumped_ThenInputRunsBeforeContinuedFeeds),
        new(nameof(GivenOneFeedRenderFailure_WhenDispatcherPumped_ThenOtherFeedStillPublishesAndAllLeasesReturn),
            GivenOneFeedRenderFailure_WhenDispatcherPumped_ThenOtherFeedStillPublishesAndAllLeasesReturn),
        new(nameof(GivenGmiPayloadGenerationReversal_WhenSubmitted_ThenStalePayloadRejectsAndNewPayloadRecovers),
            GivenGmiPayloadGenerationReversal_WhenSubmitted_ThenStalePayloadRejectsAndNewPayloadRecovers),
        new(nameof(GivenThreePreallocatedGmiLeasesBeforeDrain_WhenSubmitted_ThenOnlyNewestPayloadRenders),
            GivenThreePreallocatedGmiLeasesBeforeDrain_WhenSubmitted_ThenOnlyNewestPayloadRenders),
        new(nameof(GivenPendingFeedsAndForeignAccess_WhenDisposed_ThenCallbacksNoOpAndSecondDisposeIsIdempotent),
            GivenPendingFeedsAndForeignAccess_WhenDisposed_ThenCallbacksNoOpAndSecondDisposeIsIdempotent),
        new(nameof(GivenScatterReleaseFailure_WhenDispatcherPumped_ThenGmiFeedRemainsIndependent),
            GivenScatterReleaseFailure_WhenDispatcherPumped_ThenGmiFeedRemainsIndependent),
    ];

    private static void GivenScatterGmiAndInputBacklog_WhenDispatcherPumped_ThenInputAndFeedBoundsRemainIndependent()
    {
        // Given: Two feed schedulers sharing one real Dispatcher and six preallocated pixel leases.
        var dispatcher = Dispatcher.CurrentDispatcher;
        var order = new List<string>();
        var releases = new ConcurrentDictionary<string, int>();
        using var harness = new CombinedVisualizationHarness<TestRasterFrame, TestRasterFrame>(
            dispatcher,
            width: 2,
            height: 2,
            scatterRelease: frame => IncrementRelease(releases, $"scatter-{frame.Identity}"),
            gmiRelease: frame => IncrementRelease(releases, $"gmi-{frame.Identity}"),
            metricCapacity: 16,
            renderStarting: (feed, generation) =>
                order.Add($"{feed.ToString().ToLowerInvariant()}-{generation}"));

        // When: Each feed replaces two pending frames and one higher-priority input is queued.
        for (var generation = 1; generation <= 3; generation++)
        {
            ContractAssert.Equal(
                FrameSubmissionStatus.Accepted,
                harness.SubmitScatter(new TestRasterFrame(generation, (byte)(0x10 + generation))));
            ContractAssert.Equal(
                FrameSubmissionStatus.Accepted,
                harness.SubmitGmi(new TestRasterFrame(generation, (byte)(0x20 + generation))));
        }
        dispatcher.BeginInvoke(DispatcherPriority.Input, () => order.Add("input"));
        PumpDispatcherToIdle(dispatcher);

        // Then: Input leads, only each latest frame renders, and each feed remains pending-one.
        ContractAssert.SequenceEqual(new[] { "input", "scatter-3", "gmi-3" }, order);
        ContractAssert.Equal(3L, harness.ScatterLastPublishedGeneration);
        ContractAssert.Equal(3L, harness.GmiLastPublishedGeneration);
        var scatterMetrics = harness.GetScatterMetricsSnapshot();
        var gmiMetrics = harness.GetGmiMetricsSnapshot();
        ContractAssert.Equal(1, scatterMetrics.PendingFrameMaximum);
        ContractAssert.Equal(1, scatterMetrics.PendingCallbackMaximum);
        ContractAssert.Equal(2L, scatterMetrics.CoalescedCount);
        ContractAssert.Equal(1L, scatterMetrics.RenderedCount);
        ContractAssert.Equal(0, scatterMetrics.PendingCallbackCount);
        ContractAssert.Equal(1, gmiMetrics.PendingFrameMaximum);
        ContractAssert.Equal(1, gmiMetrics.PendingCallbackMaximum);
        ContractAssert.Equal(2L, gmiMetrics.CoalescedCount);
        ContractAssert.Equal(1L, gmiMetrics.RenderedCount);
        ContractAssert.Equal(0, gmiMetrics.PendingCallbackCount);
        ContractAssert.Equal(6, releases.Count);
        ContractAssert.Equal(true, releases.Values.All(count => count == 1));
        var scatterCopy = new byte[16];
        var gmiCopy = new byte[16];
        harness.CopyScatterPixels(scatterCopy);
        harness.CopyGmiPixels(gmiCopy);
        ContractAssert.SequenceEqual(Enumerable.Repeat((byte)0x13, 16), scatterCopy);
        ContractAssert.SequenceEqual(Enumerable.Repeat((byte)0x23, 16), gmiCopy);
    }

    private static void GivenFramesRearmedDuringRendering_WhenDispatcherPumped_ThenInputRunsBeforeContinuedFeeds()
    {
        // Given: One frame per feed and selectors that publish one successor during rendering.
        var dispatcher = Dispatcher.CurrentDispatcher;
        var order = new List<string>();
        var released = new ConcurrentDictionary<string, int>();
        CombinedVisualizationHarness<TestRasterFrame, TestRasterFrame>? harness = null;
        harness = new CombinedVisualizationHarness<TestRasterFrame, TestRasterFrame>(
            dispatcher,
            width: 2,
            height: 2,
            scatterRelease: frame => IncrementRelease(released, $"scatter-{frame.Identity}"),
            gmiRelease: frame => IncrementRelease(released, $"gmi-{frame.Identity}"),
            metricCapacity: 8,
            renderStarting: (feed, generation) =>
            {
                order.Add($"{feed.ToString().ToLowerInvariant()}-{generation}");
                if (feed == VisualizationFeed.Scatter && generation == 1)
                {
                    dispatcher.BeginInvoke(DispatcherPriority.Input, () => order.Add("input"));
                    harness!.SubmitScatter(new TestRasterFrame(2, 0x12));
                }
                if (feed == VisualizationFeed.Gmi && generation == 1)
                {
                    harness!.SubmitGmi(new TestRasterFrame(2, 0x22));
                }
            });
        using (harness)
        {
            harness.SubmitScatter(new TestRasterFrame(1, 0x11));
            harness.SubmitGmi(new TestRasterFrame(1, 0x21));

            // When: Dispatcher drains finite work; each callback processes only one frame.
            PumpDispatcherToIdle(dispatcher);

            // Then: Input interrupts the background backlog and both feeds complete successors.
            ContractAssert.SequenceEqual(
                new[] { "scatter-1", "input", "gmi-1", "scatter-2", "gmi-2" },
                order);
            ContractAssert.Equal(2L, harness.GetScatterMetricsSnapshot().RenderedCount);
            ContractAssert.Equal(2L, harness.GetGmiMetricsSnapshot().RenderedCount);
            ContractAssert.Equal(4, released.Count);
            ContractAssert.Equal(true, released.Values.All(count => count == 1));
        }
    }

    private static void GivenOneFeedRenderFailure_WhenDispatcherPumped_ThenOtherFeedStillPublishesAndAllLeasesReturn()
    {
        // Given: A scatter selector that fails and an independent valid GMI feed.
        var dispatcher = Dispatcher.CurrentDispatcher;
        var released = new ConcurrentDictionary<string, int>();
        var harness = new CombinedVisualizationHarness<TestRasterFrame, TestRasterFrame>(
            dispatcher,
            width: 2,
            height: 2,
            scatterRelease: frame => IncrementRelease(released, $"scatter-{frame.Identity}"),
            gmiRelease: frame => IncrementRelease(released, $"gmi-{frame.Identity}"),
            metricCapacity: 8,
            renderStarting: (feed, _) =>
            {
                if (feed == VisualizationFeed.Scatter)
                {
                    throw new InvalidOperationException("scatter failure");
                }
            });
        harness.SubmitScatter(new TestRasterFrame(1, 0x11));
        harness.SubmitGmi(new TestRasterFrame(1, 0x21));

        // When: Both queued callbacks run, then the harness disposes before later submissions.
        PumpDispatcherToIdle(dispatcher);
        var scatterMetrics = harness.GetScatterMetricsSnapshot();
        var gmiMetrics = harness.GetGmiMetricsSnapshot();
        harness.Dispose();
        var afterScatterDispose = harness.SubmitScatter(new TestRasterFrame(2, 0x12));
        var afterGmiDispose = harness.SubmitGmi(new TestRasterFrame(2, 0x22));

        // Then: Scatter faults only itself, GMI publishes, and disposal returns later leases once.
        ContractAssert.Equal(true, scatterMetrics.IsFaulted);
        ContractAssert.Equal(1L, scatterMetrics.RenderFailureCount);
        ContractAssert.Equal(false, gmiMetrics.IsFaulted);
        ContractAssert.Equal(1L, gmiMetrics.RenderedCount);
        ContractAssert.Equal(1L, harness.GmiLastPublishedGeneration);
        ContractAssert.Equal(FrameSubmissionStatus.RejectedDisposed, afterScatterDispose);
        ContractAssert.Equal(FrameSubmissionStatus.RejectedDisposed, afterGmiDispose);
        ContractAssert.Equal(4, released.Count);
        ContractAssert.Equal(true, released.Values.All(count => count == 1));
    }

    private static void GivenGmiPayloadGenerationReversal_WhenSubmitted_ThenStalePayloadRejectsAndNewPayloadRecovers()
    {
        // Given: Three distinct bounded GMI raster leases whose generations are payload-owned.
        var dispatcher = Dispatcher.CurrentDispatcher;
        var releases = new ConcurrentDictionary<string, int>();
        using var harness = new CombinedVisualizationHarness<TestRasterFrame, ReusableGmiRasterFrameLease>(
            dispatcher,
            width: 2,
            height: 2,
            scatterRelease: _ => { },
            gmiRelease: frame => IncrementRelease(releases, $"gmi-{frame.Generation}"),
            metricCapacity: 8);
        var source = new ushort[] { 0, 1, 2, 3 };
        var range = new GmiDisplayRange(0, 3);
        var generationTwo = new ReusableGmiRasterFrameLease(2, 2);
        var generationOne = new ReusableGmiRasterFrameLease(2, 2);
        var generationThree = new ReusableGmiRasterFrameLease(2, 2);
        generationTwo.Prepare(2, GmiChannel.FsGmi, 1, 4, source, range);
        generationOne.Prepare(1, GmiChannel.FsGmi, 1, 4, source, range);
        source[1] = 3;
        generationThree.Prepare(3, GmiChannel.FsGmi, 1, 4, source, range);

        // When: Generation 2 renders, payload generation 1 is rejected, then 3 renders.
        ContractAssert.Equal(FrameSubmissionStatus.Accepted, harness.SubmitGmi(generationTwo));
        PumpDispatcherToIdle(dispatcher);
        ContractAssert.Equal(
            FrameSubmissionStatus.RejectedStaleGeneration,
            harness.SubmitGmi(generationOne));
        ContractAssert.Equal(FrameSubmissionStatus.Accepted, harness.SubmitGmi(generationThree));
        PumpDispatcherToIdle(dispatcher);

        // Then: Scheduler order comes from each GMI payload, with exact release and recovery.
        var metrics = harness.GetGmiMetricsSnapshot();
        ContractAssert.Equal(2L, metrics.RenderedCount);
        ContractAssert.Equal(1L, metrics.StaleGenerationCount);
        ContractAssert.Equal(3L, harness.GmiLastPublishedGeneration);
        ContractAssert.Equal(3, releases.Count);
        ContractAssert.Equal(true, releases.Values.All(count => count == 1));
    }

    private static void GivenPendingFeedsAndForeignAccess_WhenDisposed_ThenCallbacksNoOpAndSecondDisposeIsIdempotent()
    {
        // Given: Two coalescing feeds with queued physical callbacks and an owner Dispatcher.
        var dispatcher = Dispatcher.CurrentDispatcher;
        var releases = new ConcurrentDictionary<string, int>();
        var harness = new CombinedVisualizationHarness<TestRasterFrame, TestRasterFrame>(
            dispatcher,
            2,
            2,
            frame => IncrementRelease(releases, $"scatter-{frame.Identity}"),
            frame => IncrementRelease(releases, $"gmi-{frame.Identity}"),
            metricCapacity: 8);
        for (var generation = 1; generation <= 3; generation++)
        {
            harness.SubmitScatter(new TestRasterFrame(generation, (byte)generation));
            harness.SubmitGmi(new TestRasterFrame(generation, (byte)(generation + 10)));
        }

        // When: Foreign access fails by resource, owner disposal releases pending, then foreign re-disposal occurs.
        Exception? foreignAccessFailure = null;
        var foreignAccess = new Thread(() =>
        {
            try
            {
                harness.CopyScatterPixels(new byte[16]);
            }
            catch (Exception exception)
            {
                foreignAccessFailure = exception;
            }
        });
        foreignAccess.Start();
        foreignAccess.Join();
        harness.Dispose();
        Exception? foreignDisposeFailure = null;
        var foreignDispose = new Thread(() =>
        {
            try
            {
                harness.Dispose();
            }
            catch (Exception exception)
            {
                foreignDisposeFailure = exception;
            }
        });
        foreignDispose.Start();
        foreignDispose.Join();
        PumpDispatcherToIdle(dispatcher);

        // Then: The resource failure is exact; disposal is idempotent and all six leases return once.
        ContractAssert.Equal(true, foreignAccessFailure is DensitySurfaceValidationException);
        ContractAssert.Equal(
            "Combined visualization harness access must run on its owner dispatcher.",
            foreignAccessFailure?.Message ?? string.Empty);
        ContractAssert.Equal(true, foreignDisposeFailure is null);
        ContractAssert.Equal(6, releases.Count);
        ContractAssert.Equal(true, releases.Values.All(count => count == 1));
        ContractAssert.Equal(0, harness.GetScatterMetricsSnapshot().PendingCallbackCount);
        ContractAssert.Equal(0, harness.GetGmiMetricsSnapshot().PendingCallbackCount);
    }

    private static void GivenThreePreallocatedGmiLeasesBeforeDrain_WhenSubmitted_ThenOnlyNewestPayloadRenders()
    {
        // Given: A fixed three-lease GMI pool prepared with distinct payload generations.
        var dispatcher = Dispatcher.CurrentDispatcher;
        var releases = new ConcurrentDictionary<string, int>();
        using var harness = new CombinedVisualizationHarness<TestRasterFrame, ReusableGmiRasterFrameLease>(
            dispatcher,
            2,
            2,
            scatterRelease: _ => { },
            gmiRelease: frame => IncrementRelease(releases, $"gmi-{frame.Generation}"),
            metricCapacity: 8);
        var leases = Enumerable.Range(1, 3)
            .Select(generation =>
            {
                var lease = new ReusableGmiRasterFrameLease(2, 2);
                var source = new ushort[] { 0, (ushort)generation, 2, 3 };
                lease.Prepare(
                    generation,
                    GmiChannel.FsGmi,
                    1,
                    4,
                    source,
                    new GmiDisplayRange(0, 3));
                return lease;
            })
            .ToArray();

        // When: Producer submits all three without waiting for the owner Dispatcher.
        foreach (var lease in leases)
        {
            ContractAssert.Equal(FrameSubmissionStatus.Accepted, harness.SubmitGmi(lease));
        }
        PumpDispatcherToIdle(dispatcher);

        // Then: Pending-one coalesces twice, generation 3 renders, and the fixed pool returns.
        var metrics = harness.GetGmiMetricsSnapshot();
        ContractAssert.Equal(2L, metrics.CoalescedCount);
        ContractAssert.Equal(1L, metrics.RenderedCount);
        ContractAssert.Equal(3L, harness.GmiLastPublishedGeneration);
        ContractAssert.Equal(3, releases.Count);
        ContractAssert.Equal(true, releases.Values.All(count => count == 1));
        var copied = new byte[16];
        harness.CopyGmiPixels(copied);
        ContractAssert.SequenceEqual(leases[2].BgraPixels.ToArray(), copied);
    }

    private static void GivenScatterReleaseFailure_WhenDispatcherPumped_ThenGmiFeedRemainsIndependent()
    {
        // Given: Scatter release fails after returning lease ownership; GMI release stays valid.
        var dispatcher = Dispatcher.CurrentDispatcher;
        var releases = new ConcurrentDictionary<string, int>();
        using var harness = new CombinedVisualizationHarness<TestRasterFrame, TestRasterFrame>(
            dispatcher,
            2,
            2,
            scatterRelease: frame =>
            {
                IncrementRelease(releases, $"scatter-{frame.Identity}");
                throw new InvalidOperationException("scatter release failure");
            },
            gmiRelease: frame => IncrementRelease(releases, $"gmi-{frame.Identity}"),
            metricCapacity: 8);

        // When: Both initial frames drain, then each feed receives one later submission.
        harness.SubmitScatter(new TestRasterFrame(1, 0x11));
        harness.SubmitGmi(new TestRasterFrame(1, 0x21));
        PumpDispatcherToIdle(dispatcher);
        var scatterStatus = harness.SubmitScatter(new TestRasterFrame(2, 0x12));
        var gmiStatus = harness.SubmitGmi(new TestRasterFrame(2, 0x22));
        PumpDispatcherToIdle(dispatcher);

        // Then: Only scatter faults; GMI publishes both generations and every ownership returns.
        ContractAssert.Equal(FrameSubmissionStatus.RejectedFaulted, scatterStatus);
        ContractAssert.Equal(FrameSubmissionStatus.Accepted, gmiStatus);
        var scatterMetrics = harness.GetScatterMetricsSnapshot();
        ContractAssert.Equal(true, scatterMetrics.IsFaulted);
        ContractAssert.Equal(2L, scatterMetrics.ReleaseFailureCount);
        ContractAssert.Equal(false, harness.GetGmiMetricsSnapshot().IsFaulted);
        ContractAssert.Equal(2L, harness.GmiLastPublishedGeneration);
        ContractAssert.Equal(4, releases.Count);
        ContractAssert.Equal(true, releases.Values.All(count => count == 1));
    }

    private static void PumpDispatcherToIdle(Dispatcher dispatcher)
    {
        var frame = new DispatcherFrame();
        dispatcher.BeginInvoke(DispatcherPriority.ApplicationIdle, () => frame.Continue = false);
        Dispatcher.PushFrame(frame);
    }

    private static void IncrementRelease(
        ConcurrentDictionary<string, int> releases,
        string identity) =>
        releases.AddOrUpdate(identity, 1, (_, count) => count + 1);

    private sealed class TestRasterFrame : IRasterFrameLease
    {
        private readonly byte[] _bgraPixels;
        private int _schedulerOwned;
        private long _schedulerGeneration;

        public TestRasterFrame(long identity, byte pixelValue)
        {
            Identity = identity;
            _bgraPixels = Enumerable.Repeat(pixelValue, 16).ToArray();
        }

        public long Identity { get; }

        public long Generation => Identity;

        public int Width => 2;

        public int Height => 2;

        public ReadOnlySpan<byte> BgraPixels => _bgraPixels;

        public long PublicationTimestamp { get; set; }

        public long SchedulerGeneration => Volatile.Read(ref _schedulerGeneration);

        public bool TryAcquireSchedulerOwnership()
        {
            if (Interlocked.CompareExchange(ref _schedulerOwned, 1, 0) != 0)
            {
                return false;
            }

            Volatile.Write(ref _schedulerGeneration, Generation);
            return true;
        }

        public void ReleaseSchedulerOwnership()
        {
            if (Interlocked.Exchange(ref _schedulerOwned, 0) != 1)
            {
                throw new InvalidOperationException("Combined test frame is not scheduler-owned.");
            }
        }
    }
}
