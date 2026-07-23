using System.Collections.Concurrent;
using System.Reflection;
using System.Reflection.Emit;
using AnalogBoard.ScatterRendering.Core;

namespace AnalogBoard.ScatterRendering.Tests;

internal static class LatestFrameSchedulerContractTests
{
    public static IReadOnlyList<ContractTestCase> Cases { get; } =
    [
        new(nameof(GivenTwoFramesBeforeDrain_WhenSubmitted_ThenOnlyLatestRendersWithOnePendingCallback),
            GivenTwoFramesBeforeDrain_WhenSubmitted_ThenOnlyLatestRendersWithOnePendingCallback),
        new(nameof(GivenEqualReversedAndMaximumGenerations_WhenSubmitted_ThenOrderIsExplicit),
            GivenEqualReversedAndMaximumGenerations_WhenSubmitted_ThenOrderIsExplicit),
        new(nameof(GivenFramesPublishedDuringRender_WhenDrained_ThenHandshakePreventsLostWakeupAndMonopoly),
            GivenFramesPublishedDuringRender_WhenDrained_ThenHandshakePreventsLostWakeupAndMonopoly),
        new(nameof(GivenDrainCompletion_WhenDisarmed_ThenAtomicExchangeProvidesFullFence),
            GivenDrainCompletion_WhenDisarmed_ThenAtomicExchangeProvidesFullFence),
        new(nameof(GivenPostOrRenderFailure_WhenScheduled_ThenFramesReturnAndSchedulerFaults),
            GivenPostOrRenderFailure_WhenScheduled_ThenFramesReturnAndSchedulerFaults),
        new(nameof(GivenReleaseCallbackFailureDuringDrain_WhenScheduled_ThenGateCleansUpAndPendingFramesReturnOnce),
            GivenReleaseCallbackFailureDuringDrain_WhenScheduled_ThenGateCleansUpAndPendingFramesReturnOnce),
        new(nameof(GivenSubmissionDependencyFailuresAfterOwnership_WhenSubmitted_ThenLeaseReturnsAndPublisherGateClears),
            GivenSubmissionDependencyFailuresAfterOwnership_WhenSubmitted_ThenLeaseReturnsAndPublisherGateClears),
        new(nameof(GivenPendingFrame_WhenDisposed_ThenReleaseIsExactAndQueuedDrainIsNoOp),
            GivenPendingFrame_WhenDisposed_ThenReleaseIsExactAndQueuedDrainIsNoOp),
        new(nameof(GivenConcurrentProducer_WhenSubmitted_ThenSecondCallRejectsWithoutWaiting),
            GivenConcurrentProducer_WhenSubmitted_ThenSecondCallRejectsWithoutWaiting),
        new(nameof(GivenSameLeaseResubmittedWhilePending_WhenSubmitted_ThenOwnershipRejectsWithoutDoubleRelease),
            GivenSameLeaseResubmittedWhilePending_WhenSubmitted_ThenOwnershipRejectsWithoutDoubleRelease),
        new(nameof(GivenBoundedMetricCapacity_WhenMeasured_ThenNewestTicksAndOverwriteCountsRemainExact),
            GivenBoundedMetricCapacity_WhenMeasured_ThenNewestTicksAndOverwriteCountsRemainExact),
        new(nameof(GivenProducerRecordsDuringSnapshots_WhenObserved_ThenEachRingSnapshotIsInternallyConsistent),
            GivenProducerRecordsDuringSnapshots_WhenObserved_ThenEachRingSnapshotIsInternallyConsistent),
        new(nameof(GivenProducerAndDisposeWhileUiRenderIsBlocked_WhenCompleted_ThenNoWakeupOrLeaseIsLost),
            GivenProducerAndDisposeWhileUiRenderIsBlocked_WhenCompleted_ThenNoWakeupOrLeaseIsLost),
    ];

    private static void GivenTwoFramesBeforeDrain_WhenSubmitted_ThenOnlyLatestRendersWithOnePendingCallback()
    {
        // Given: One manual UI queue, one renderer, and reusable frame leases.
        var poster = new ManualUiWorkPoster();
        var rendered = new List<long>();
        var released = new List<long>();
        using var scheduler = CreateScheduler(poster, rendered, released);

        // When: Two increasing generations arrive before the UI drains once.
        var first = scheduler.Submit(new TestFrame(1));
        var second = scheduler.Submit(new TestFrame(2));
        var beforeDrain = scheduler.GetMetricsSnapshot();

        // Then: One slot/callback remains, the older frame is coalesced, and only gen2 renders.
        ContractAssert.Equal(FrameSubmissionStatus.Accepted, first);
        ContractAssert.Equal(FrameSubmissionStatus.Accepted, second);
        ContractAssert.Equal(1, poster.PendingCount);
        ContractAssert.Equal(1, beforeDrain.PendingFrameCount);
        ContractAssert.Equal(1, beforeDrain.PendingCallbackCount);
        ContractAssert.Equal(1, beforeDrain.PendingFrameMaximum);
        ContractAssert.Equal(1, beforeDrain.PendingCallbackMaximum);
        ContractAssert.Equal(1L, beforeDrain.CoalescedCount);
        ContractAssert.SequenceEqual(new long[] { 1 }, released);

        poster.DrainNext();
        var afterDrain = scheduler.GetMetricsSnapshot();
        ContractAssert.SequenceEqual(new long[] { 2 }, rendered);
        ContractAssert.SequenceEqual(new long[] { 1, 2 }, released);
        ContractAssert.Equal(0, afterDrain.PendingFrameCount);
        ContractAssert.Equal(0, afterDrain.PendingCallbackCount);
        ContractAssert.Equal(2L, afterDrain.LastAcceptedGeneration);
        ContractAssert.Equal(2L, afterDrain.LastRenderedGeneration);
    }

    private static void GivenEqualReversedAndMaximumGenerations_WhenSubmitted_ThenOrderIsExplicit()
    {
        // Given: Generation three already accepted but not rendered.
        var poster = new ManualUiWorkPoster();
        var rendered = new List<long>();
        var released = new List<long>();
        using var scheduler = CreateScheduler(poster, rendered, released);
        scheduler.Submit(new TestFrame(3));

        // When: Equal/reversed generations and then long.MaxValue are submitted.
        var equal = scheduler.Submit(new TestFrame(3));
        var reversed = scheduler.Submit(new TestFrame(2));
        var maximum = scheduler.Submit(new TestFrame(long.MaxValue));
        poster.DrainNext();

        // Then: Stale frames reject against last accepted, max is accepted without arithmetic overflow.
        var snapshot = scheduler.GetMetricsSnapshot();
        ContractAssert.Equal(FrameSubmissionStatus.RejectedStaleGeneration, equal);
        ContractAssert.Equal(FrameSubmissionStatus.RejectedStaleGeneration, reversed);
        ContractAssert.Equal(FrameSubmissionStatus.Accepted, maximum);
        ContractAssert.Equal(2L, snapshot.StaleGenerationCount);
        ContractAssert.Equal(1L, snapshot.CoalescedCount);
        ContractAssert.Equal(long.MaxValue, snapshot.LastAcceptedGeneration);
        ContractAssert.Equal(long.MaxValue, snapshot.LastRenderedGeneration);
        ContractAssert.SequenceEqual(new long[] { 3, 2, 3, long.MaxValue }, released);
        ContractAssert.SequenceEqual(new long[] { long.MaxValue }, rendered);
    }

    private static void GivenFramesPublishedDuringRender_WhenDrained_ThenHandshakePreventsLostWakeupAndMonopoly()
    {
        // Given: A renderer that publishes two newer frames while consuming generation one.
        var poster = new ManualUiWorkPoster();
        var rendered = new List<long>();
        var released = new List<long>();
        LatestFrameScheduler<TestFrame>? scheduler = null;
        scheduler = new LatestFrameScheduler<TestFrame>(
            poster,
            frame =>
            {
                rendered.Add(frame.Generation);
                if (frame.Generation == 1)
                {
                    scheduler!.Submit(new TestFrame(2));
                    scheduler.Submit(new TestFrame(3));
                }
            },
            frame => released.Add(frame.Generation),
            metricCapacity: 8);
        using (scheduler)
        {
            // When: The first callback renders one frame and the separately rearmed callback drains latest.
            scheduler.Submit(new TestFrame(1));
            poster.DrainNext();

            // Then: One new callback is queued; generation two coalesces without a drain loop.
            ContractAssert.SequenceEqual(new long[] { 1 }, rendered);
            ContractAssert.Equal(1, poster.PendingCount);
            ContractAssert.Equal(1, poster.MaximumPendingCount);
            poster.DrainNext();
            ContractAssert.SequenceEqual(new long[] { 1, 3 }, rendered);
            ContractAssert.SequenceEqual(new long[] { 2, 1, 3 }, released);
            var snapshot = scheduler.GetMetricsSnapshot();
            ContractAssert.Equal(1L, snapshot.CoalescedCount);
            ContractAssert.Equal(3L, snapshot.LastRenderedGeneration);
            ContractAssert.Equal(0, snapshot.PendingFrameCount);
            ContractAssert.Equal(0, snapshot.PendingCallbackCount);
            ContractAssert.Equal(0, poster.PendingCount);
        }
    }

    private static void GivenDrainCompletion_WhenDisarmed_ThenAtomicExchangeProvidesFullFence()
    {
        // Given: The private drain-completion handshake compiled for a concrete frame lease.
        var method = typeof(LatestFrameScheduler<TestFrame>).GetMethod(
            "CompleteDrainAndRearmIfNeeded",
            BindingFlags.Instance | BindingFlags.NonPublic)
            ?? throw new InvalidOperationException("Drain-completion handshake method was not found.");

        // When: Its compiled method calls are inspected without relying on source-file paths.
        var calledMethods = GetCalledMethods(method);

        // Then: Disarming uses Interlocked.Exchange(ref int, int), which provides the required full fence.
        ContractAssert.Equal(
            true,
            calledMethods.Any(
                calledMethod =>
                    calledMethod.DeclaringType == typeof(Interlocked) &&
                    StringComparer.Ordinal.Equals(calledMethod.Name, nameof(Interlocked.Exchange)) &&
                    calledMethod.GetParameters() is
                    [
                        { ParameterType: var firstParameterType },
                        { ParameterType: var secondParameterType },
                    ] &&
                    firstParameterType == typeof(int).MakeByRefType() &&
                    secondParameterType == typeof(int)));
    }

    private static void GivenPostOrRenderFailure_WhenScheduled_ThenFramesReturnAndSchedulerFaults()
    {
        // Given: One poster that rejects work and one renderer that throws.
        var rejectingPoster = new ManualUiWorkPoster { AcceptPosts = false };
        var postReleased = new List<long>();
        using var postFailure = new LatestFrameScheduler<TestFrame>(
            rejectingPoster,
            _ => throw new InvalidOperationException("must not render"),
            frame => postReleased.Add(frame.Generation),
            metricCapacity: 4);
        var throwingPoster = new ManualUiWorkPoster();
        var renderReleased = new List<long>();
        using var renderFailure = new LatestFrameScheduler<TestFrame>(
            throwingPoster,
            _ => throw new InvalidOperationException("render failure"),
            frame => renderReleased.Add(frame.Generation),
            metricCapacity: 4);

        // When: Posting fails, rendering fails, and later frames target each faulted scheduler.
        var postStatus = postFailure.Submit(new TestFrame(1));
        var postAfterFault = postFailure.Submit(new TestFrame(2));
        renderFailure.Submit(new TestFrame(10));
        throwingPoster.DrainNext();
        var renderAfterFault = renderFailure.Submit(new TestFrame(11));

        // Then: No frame strands, each scheduler faults, and later work rejects immediately.
        ContractAssert.Equal(FrameSubmissionStatus.RejectedPostFailure, postStatus);
        ContractAssert.Equal(FrameSubmissionStatus.RejectedFaulted, postAfterFault);
        ContractAssert.Equal(FrameSubmissionStatus.RejectedFaulted, renderAfterFault);
        ContractAssert.SequenceEqual(new long[] { 1, 2 }, postReleased);
        ContractAssert.SequenceEqual(new long[] { 10, 11 }, renderReleased);
        ContractAssert.Equal(true, postFailure.GetMetricsSnapshot().IsFaulted);
        ContractAssert.Equal(1L, postFailure.GetMetricsSnapshot().PostFailureCount);
        ContractAssert.Equal(true, renderFailure.GetMetricsSnapshot().IsFaulted);
        ContractAssert.Equal(1L, renderFailure.GetMetricsSnapshot().RenderFailureCount);
    }

    private static void GivenReleaseCallbackFailureDuringDrain_WhenScheduled_ThenGateCleansUpAndPendingFramesReturnOnce()
    {
        // Given: A renderer that publishes generation two and a release callback that fails for gen1.
        var poster = new ManualUiWorkPoster();
        var releaseAttempts = new List<long>();
        LatestFrameScheduler<TestFrame>? scheduler = null;
        scheduler = new LatestFrameScheduler<TestFrame>(
            poster,
            frame =>
            {
                if (frame.Generation == 1)
                {
                    scheduler!.Submit(new TestFrame(2));
                }
            },
            frame =>
            {
                releaseAttempts.Add(frame.Generation);
                if (frame.Generation == 1)
                {
                    throw new InvalidOperationException("release failure");
                }
            },
            metricCapacity: 8);
        using (scheduler)
        {
            // When: Generation one drains, its release fails, and a later frame is submitted.
            scheduler.Submit(new TestFrame(1));
            poster.DrainNext();
            var afterFault = scheduler.Submit(new TestFrame(3));
            var snapshot = scheduler.GetMetricsSnapshot();

            // Then: The failure is contained, pending gen2 and rejected gen3 each release once.
            ContractAssert.Equal(FrameSubmissionStatus.RejectedFaulted, afterFault);
            ContractAssert.Equal(true, snapshot.IsFaulted);
            ContractAssert.Equal(1L, snapshot.ReleaseFailureCount);
            ContractAssert.Equal(0, snapshot.PendingFrameCount);
            ContractAssert.Equal(0, snapshot.PendingCallbackCount);
            ContractAssert.SequenceEqual(new long[] { 1, 2, 3 }, releaseAttempts);
        }
    }

    private static void GivenPendingFrame_WhenDisposed_ThenReleaseIsExactAndQueuedDrainIsNoOp()
    {
        // Given: One pending frame and one queued callback.
        var poster = new ManualUiWorkPoster();
        var rendered = new List<long>();
        var released = new List<long>();
        var scheduler = CreateScheduler(poster, rendered, released);
        scheduler.Submit(new TestFrame(1));

        // When: Disposal repeats, the queued callback runs, and a later frame is submitted.
        scheduler.Dispose();
        scheduler.Dispose();
        var beforeQueuedDrain = scheduler.GetMetricsSnapshot();

        // Then: Dispose releases the frame but the physical queued callback retains its arm.
        ContractAssert.Equal(0, beforeQueuedDrain.PendingFrameCount);
        ContractAssert.Equal(1, beforeQueuedDrain.PendingCallbackCount);
        ContractAssert.Equal(1, poster.PendingCount);

        poster.DrainNext();
        var afterDispose = scheduler.Submit(new TestFrame(2));

        // Then: The queued callback disarms itself; leases return exactly once and no rendering occurs.
        ContractAssert.Equal(FrameSubmissionStatus.RejectedDisposed, afterDispose);
        ContractAssert.SequenceEqual(Array.Empty<long>(), rendered);
        ContractAssert.SequenceEqual(new long[] { 1, 2 }, released);
        ContractAssert.Equal(0, scheduler.GetMetricsSnapshot().PendingFrameCount);
        ContractAssert.Equal(0, scheduler.GetMetricsSnapshot().PendingCallbackCount);
    }

    private static void GivenSubmissionDependencyFailuresAfterOwnership_WhenSubmitted_ThenLeaseReturnsAndPublisherGateClears()
    {
        // Given: Clock, allocation, and lease-property dependencies that fail after ownership transfer.
        var clockReleased = new List<long>();
        using var clockScheduler = new LatestFrameScheduler<TestFrame>(
            new ManualUiWorkPoster(),
            _ => { },
            frame => clockReleased.Add(frame.Generation),
            metricCapacity: 4,
            new ThrowingClock(),
            new ConstantAllocationCounter());

        var allocationPoster = new ManualUiWorkPoster();
        var allocationReleased = new List<long>();
        using var allocationScheduler = new LatestFrameScheduler<TestFrame>(
            allocationPoster,
            _ => { },
            frame => allocationReleased.Add(frame.Generation),
            metricCapacity: 4,
            new ManualClock(1_000, 100),
            new ThrowingOnSecondAllocationCounter());

        var propertyReleased = new List<long>();
        using var propertyScheduler = new LatestFrameScheduler<ThrowingTimestampFrame>(
            new ManualUiWorkPoster(),
            _ => { },
            frame => propertyReleased.Add(frame.Identity),
            metricCapacity: 4,
            new ManualClock(1_000, 100),
            new ConstantAllocationCounter());

        // When: Each dependency fails and a later frame tests whether the publisher gate cleared.
        var clockStatus = clockScheduler.Submit(new TestFrame(1));
        var clockAfter = clockScheduler.Submit(new TestFrame(2));
        var allocationStatus = allocationScheduler.Submit(new TestFrame(10));
        var allocationAfter = allocationScheduler.Submit(new TestFrame(11));
        var propertyStatus = propertyScheduler.Submit(new ThrowingTimestampFrame(20));
        var propertyAfter = propertyScheduler.Submit(new ThrowingTimestampFrame(21));

        // Then: No exception escapes, each scheduler faults, and every transferred lease returns once.
        ContractAssert.Equal(FrameSubmissionStatus.RejectedFaulted, clockStatus);
        ContractAssert.Equal(FrameSubmissionStatus.RejectedFaulted, clockAfter);
        ContractAssert.Equal(FrameSubmissionStatus.Accepted, allocationStatus);
        ContractAssert.Equal(FrameSubmissionStatus.RejectedFaulted, allocationAfter);
        ContractAssert.Equal(FrameSubmissionStatus.RejectedFaulted, propertyStatus);
        ContractAssert.Equal(FrameSubmissionStatus.RejectedFaulted, propertyAfter);
        ContractAssert.SequenceEqual(new long[] { 1, 2 }, clockReleased);
        ContractAssert.SequenceEqual(new long[] { 10, 11 }, allocationReleased);
        ContractAssert.SequenceEqual(new long[] { 20, 21 }, propertyReleased);
        ContractAssert.Equal(0L, clockScheduler.GetMetricsSnapshot().ConcurrentPublisherCount);
        var allocationBeforeDrain = allocationScheduler.GetMetricsSnapshot();
        ContractAssert.Equal(0L, allocationBeforeDrain.ConcurrentPublisherCount);
        ContractAssert.Equal(1L, allocationBeforeDrain.AcceptedCount);
        ContractAssert.Equal(0L, allocationBeforeDrain.RenderedCount);
        ContractAssert.Equal(1, allocationBeforeDrain.PendingCallbackCount);
        ContractAssert.Equal(1, allocationPoster.PendingCount);
        ContractAssert.Equal(0L, propertyScheduler.GetMetricsSnapshot().ConcurrentPublisherCount);

        allocationPoster.DrainNext();
        ContractAssert.Equal(0, allocationScheduler.GetMetricsSnapshot().PendingCallbackCount);
        ContractAssert.Equal(0, allocationPoster.PendingCount);
    }

    private static void GivenConcurrentProducer_WhenSubmitted_ThenSecondCallRejectsWithoutWaiting()
    {
        // Given: A first generation getter held after entering Submit and a second producer call.
        var poster = new ManualUiWorkPoster();
        var entered = new ManualResetEventSlim(false);
        var proceed = new ManualResetEventSlim(false);
        var released = new ConcurrentQueue<long>();
        using var scheduler = new LatestFrameScheduler<BlockingTestFrame>(
            poster,
            _ => { },
            frame => released.Enqueue(frame.GenerationWithoutBlocking),
            metricCapacity: 4);
        var firstFrame = new BlockingTestFrame(1, entered, proceed);
        FrameSubmissionStatus firstStatus = default;
        var firstThread = new Thread(() => firstStatus = scheduler.Submit(firstFrame));
        firstThread.Start();
        ContractAssert.Equal(true, entered.Wait(TimeSpan.FromSeconds(5)));

        // When: A second thread submits while the first still owns the producer gate.
        var before = Environment.TickCount64;
        var secondStatus = scheduler.Submit(new BlockingTestFrame(2));
        var elapsed = Environment.TickCount64 - before;
        proceed.Set();
        ContractAssert.Equal(true, firstThread.Join(TimeSpan.FromSeconds(5)));

        // Then: The concurrent call rejects promptly instead of waiting for the producer/UI.
        ContractAssert.Equal(FrameSubmissionStatus.Accepted, firstStatus);
        ContractAssert.Equal(FrameSubmissionStatus.RejectedConcurrentPublisher, secondStatus);
        ContractAssert.Equal(true, elapsed < 1000);
        ContractAssert.Equal(true, released.Contains(2));
        ContractAssert.Equal(1L, scheduler.GetMetricsSnapshot().ConcurrentPublisherCount);
    }

    private static void GivenSameLeaseResubmittedWhilePending_WhenSubmitted_ThenOwnershipRejectsWithoutDoubleRelease()
    {
        // Given: One mutable lease already transferred to the scheduler as generation one.
        var poster = new ManualUiWorkPoster();
        var rendered = new List<long>();
        var releaseCount = 0;
        using var scheduler = new LatestFrameScheduler<MutableTestFrame>(
            poster,
            frame => rendered.Add(frame.SchedulerGeneration),
            _ => releaseCount++,
            metricCapacity: 4);
        var frame = new MutableTestFrame(1);
        scheduler.Submit(frame);

        // When: The caller illegally mutates and resubmits the same outstanding instance.
        frame.GenerationValue = 2;
        var duplicate = scheduler.Submit(frame);
        poster.DrainNext();

        // Then: Ownership rejects the alias, captured generation stays one, and release is once.
        var snapshot = scheduler.GetMetricsSnapshot();
        ContractAssert.Equal(FrameSubmissionStatus.RejectedLeaseAlreadyOwned, duplicate);
        ContractAssert.Equal(1L, snapshot.LeaseAlreadyOwnedCount);
        ContractAssert.Equal(1L, snapshot.LastAcceptedGeneration);
        ContractAssert.Equal(1L, snapshot.LastRenderedGeneration);
        ContractAssert.SequenceEqual(new long[] { 1 }, rendered);
        ContractAssert.Equal(1, releaseCount);
    }

    private static void GivenBoundedMetricCapacity_WhenMeasured_ThenNewestTicksAndOverwriteCountsRemainExact()
    {
        // Given: Injected clocks/allocation counters and one-sample bounded metric rings.
        var clock = new ManualClock(frequency: 1_000, value: 100);
        var allocation = new ManualAllocationCounter(1_000);
        var poster = new ManualUiWorkPoster();
        var postNumber = 0;
        poster.OnPost = () =>
        {
            postNumber++;
            if (postNumber == 1)
            {
                clock.Value = 110;
                allocation.Value = 1_040;
            }
            else
            {
                clock.Value = 215;
                allocation.Value = 2_024;
            }
        };
        using var scheduler = new LatestFrameScheduler<TestFrame>(
            poster,
            _ => { },
            _ => { },
            metricCapacity: 1,
            clock,
            allocation);

        // When: Two publish/drain samples exceed capacity one.
        scheduler.Submit(new TestFrame(1));
        clock.Value = 170;
        poster.DrainNext();
        clock.Value = 200;
        allocation.Value = 2_000;
        scheduler.Submit(new TestFrame(2));
        clock.Value = 250;
        poster.DrainNext();
        var snapshot = scheduler.GetMetricsSnapshot();

        // Then: Newest raw ticks/bytes survive and overwrite accounting remains explicit.
        ContractAssert.SequenceEqual(new long[] { 15 }, snapshot.PublicationDurationTicks.Samples);
        ContractAssert.SequenceEqual(new long[] { 50 }, snapshot.PublishToDrainLatencyTicks.Samples);
        ContractAssert.SequenceEqual(new long[] { 24 }, snapshot.ProducerAllocatedBytes.Samples);
        ContractAssert.Equal(2L, snapshot.PublicationDurationTicks.TotalSampleCount);
        ContractAssert.Equal(1L, snapshot.PublicationDurationTicks.OverwrittenSampleCount);
        ContractAssert.Equal(1_000L, snapshot.StopwatchFrequency);
        ContractAssert.Equal(15.0, snapshot.PublicationDurationP99Milliseconds);
    }

    private static void GivenProducerRecordsDuringSnapshots_WhenObserved_ThenEachRingSnapshotIsInternallyConsistent()
    {
        // Given: A producer writing monotonically numbered durations into a two-sample ring.
        var poster = new ManualUiWorkPoster { AcceptPosts = false };
        var clock = new IncreasingDurationClock();
        var allocation = new ConstantAllocationCounter();
        using var scheduler = new LatestFrameScheduler<TestFrame>(
            poster,
            _ => { },
            _ => { },
            metricCapacity: 2,
            clock,
            allocation);
        var reusableFrame = new TestFrame(1);
        var failures = new ConcurrentQueue<string>();
        var producerStarted = new ManualResetEventSlim(false);
        var stopProducer = 0;
        var producer = new Thread(() =>
        {
            producerStarted.Set();
            while (Volatile.Read(ref stopProducer) == 0)
            {
                scheduler.Submit(reusableFrame);
            }
        });

        // When: The reader controls a fixed observation window while the producer keeps writing.
        producer.Start();
        ContractAssert.Equal(true, producerStarted.Wait(TimeSpan.FromSeconds(5)));
        var snapshotCount = 0;
        var fullRingCount = 0;
        var maximumTotalSampleCount = 0L;
        for (var observation = 0; observation < 2_000 && failures.IsEmpty; observation++)
        {
            try
            {
                var ring = scheduler.GetMetricsSnapshot().PublicationDurationTicks;
                snapshotCount++;
                maximumTotalSampleCount = Math.Max(maximumTotalSampleCount, ring.TotalSampleCount);
                if (ring.Samples.Count == 2 &&
                    (ring.Samples[0] != ring.TotalSampleCount - 1 ||
                     ring.Samples[1] != ring.TotalSampleCount ||
                     ring.OverwrittenSampleCount != ring.TotalSampleCount - 2))
                {
                    failures.Enqueue(
                        $"mixed snapshot: [{string.Join(',', ring.Samples)}], total={ring.TotalSampleCount}, overwritten={ring.OverwrittenSampleCount}");
                }
                if (ring.Samples.Count == 2)
                {
                    fullRingCount++;
                }
            }
            catch (Exception exception)
            {
                failures.Enqueue(exception.GetType().Name);
            }
        }
        Volatile.Write(ref stopProducer, 1);
        ContractAssert.Equal(true, producer.Join(TimeSpan.FromSeconds(10)));

        // Then: The race was exercised and every returned snapshot is one stable writer sequence.
        ContractAssert.SequenceEqual(Array.Empty<string>(), failures);
        ContractAssert.Equal(2_000, snapshotCount);
        ContractAssert.Equal(true, fullRingCount > 0);
        ContractAssert.Equal(true, maximumTotalSampleCount > 2);
    }

    private static void GivenProducerAndDisposeWhileUiRenderIsBlocked_WhenCompleted_ThenNoWakeupOrLeaseIsLost()
    {
        // Given: A real UI thread blocked inside gen1 rendering and a separate producer thread.
        var poster = new ManualUiWorkPoster();
        var renderEntered = new ManualResetEventSlim(false);
        var allowRenderToFinish = new ManualResetEventSlim(false);
        var rendered = new ConcurrentQueue<long>();
        var releaseCounts = new ConcurrentDictionary<long, int>();
        using var scheduler = new LatestFrameScheduler<TestFrame>(
            poster,
            frame =>
            {
                rendered.Enqueue(frame.SchedulerGeneration);
                if (frame.SchedulerGeneration == 1)
                {
                    renderEntered.Set();
                    allowRenderToFinish.Wait();
                }
            },
            frame => releaseCounts.AddOrUpdate(frame.SchedulerGeneration, 1, (_, count) => count + 1),
            metricCapacity: 16);
        scheduler.Submit(new TestFrame(1));
        var uiThread = new Thread(poster.DrainNext);
        uiThread.Start();
        ContractAssert.Equal(true, renderEntered.Wait(TimeSpan.FromSeconds(5)));

        // When: Producer submissions coalesce while UI is in-flight, then UI is allowed to rearm.
        var producerThread = new Thread(() =>
        {
            scheduler.Submit(new TestFrame(2));
            scheduler.Submit(new TestFrame(3));
        });
        producerThread.Start();
        ContractAssert.Equal(true, producerThread.Join(TimeSpan.FromSeconds(5)));
        allowRenderToFinish.Set();
        ContractAssert.Equal(true, uiThread.Join(TimeSpan.FromSeconds(5)));

        // Then: One physical callback is pending, gen2 returned once, and the latest gen3 drains.
        ContractAssert.Equal(1, poster.PendingCount);
        ContractAssert.Equal(1, poster.MaximumPendingCount);
        poster.DrainNext();
        ContractAssert.SequenceEqual(new long[] { 1, 3 }, rendered);
        ContractAssert.Equal(1, releaseCounts[1]);
        ContractAssert.Equal(1, releaseCounts[2]);
        ContractAssert.Equal(1, releaseCounts[3]);
        ContractAssert.Equal(1, scheduler.GetMetricsSnapshot().PendingCallbackMaximum);

        // Given: A second in-flight UI render with a pending newer frame.
        var disposePoster = new ManualUiWorkPoster();
        var disposeRenderEntered = new ManualResetEventSlim(false);
        var allowDisposedRenderToFinish = new ManualResetEventSlim(false);
        var disposeReleaseCounts = new ConcurrentDictionary<long, int>();
        var disposeScheduler = new LatestFrameScheduler<TestFrame>(
            disposePoster,
            frame =>
            {
                disposeRenderEntered.Set();
                allowDisposedRenderToFinish.Wait();
            },
            frame => disposeReleaseCounts.AddOrUpdate(frame.SchedulerGeneration, 1, (_, count) => count + 1),
            metricCapacity: 8);
        disposeScheduler.Submit(new TestFrame(10));
        var disposeUiThread = new Thread(disposePoster.DrainNext);
        disposeUiThread.Start();
        ContractAssert.Equal(true, disposeRenderEntered.Wait(TimeSpan.FromSeconds(5)));
        disposeScheduler.Submit(new TestFrame(11));

        // When: Dispose races the active render, then that render completes.
        disposeScheduler.Dispose();
        allowDisposedRenderToFinish.Set();
        ContractAssert.Equal(true, disposeUiThread.Join(TimeSpan.FromSeconds(5)));

        // Then: Current/pending leases return once, no new callback is queued, and gate is clear.
        var disposedSnapshot = disposeScheduler.GetMetricsSnapshot();
        ContractAssert.Equal(1, disposeReleaseCounts[10]);
        ContractAssert.Equal(1, disposeReleaseCounts[11]);
        ContractAssert.Equal(0, disposePoster.PendingCount);
        ContractAssert.Equal(0, disposedSnapshot.PendingFrameCount);
        ContractAssert.Equal(0, disposedSnapshot.PendingCallbackCount);
    }

    private static LatestFrameScheduler<TestFrame> CreateScheduler(
        ManualUiWorkPoster poster,
        List<long> rendered,
        List<long> released) =>
        new(
            poster,
            frame => rendered.Add(frame.Generation),
            frame => released.Add(frame.Generation),
            metricCapacity: 8);

    private static IReadOnlyList<MethodBase> GetCalledMethods(MethodInfo method)
    {
        var methodBody = method.GetMethodBody()
            ?? throw new InvalidOperationException($"Method '{method.Name}' has no IL body.");
        var il = methodBody.GetILAsByteArray()
            ?? throw new InvalidOperationException($"Method '{method.Name}' has no IL bytes.");
        var module = method.Module;
        var declaringTypeArguments = method.DeclaringType?.GetGenericArguments();
        var methodArguments = method.GetGenericArguments();
        var calledMethods = new List<MethodBase>();

        for (var offset = 0; offset < il.Length;)
        {
            var opcodeValue = (ushort)il[offset++];
            if (opcodeValue == 0xfe)
            {
                opcodeValue = (ushort)(0xfe00 | il[offset++]);
            }

            var opcode = OpCodesByValue[opcodeValue];
            if (opcode.OperandType == OperandType.InlineMethod)
            {
                var token = BitConverter.ToInt32(il, offset);
                calledMethods.Add(
                    module.ResolveMethod(token, declaringTypeArguments, methodArguments)
                    ?? throw new InvalidOperationException(
                        $"Method token '{token}' in '{method.Name}' could not be resolved."));
            }

            offset += GetOperandSize(opcode.OperandType, il, offset);
        }

        return calledMethods;
    }

    private static int GetOperandSize(OperandType operandType, byte[] il, int operandOffset) =>
        operandType switch
        {
            OperandType.InlineNone => 0,
            OperandType.ShortInlineBrTarget or
            OperandType.ShortInlineI or
            OperandType.ShortInlineVar => 1,
            OperandType.InlineVar => 2,
            OperandType.InlineBrTarget or
            OperandType.InlineField or
            OperandType.InlineI or
            OperandType.InlineMethod or
            OperandType.InlineSig or
            OperandType.InlineString or
            OperandType.InlineTok or
            OperandType.InlineType or
            OperandType.ShortInlineR => 4,
            OperandType.InlineI8 or
            OperandType.InlineR => 8,
            OperandType.InlineSwitch => 4 + (BitConverter.ToInt32(il, operandOffset) * 4),
            _ => throw new InvalidOperationException($"Unsupported IL operand type '{operandType}'."),
        };

    private static readonly IReadOnlyDictionary<ushort, OpCode> OpCodesByValue =
        typeof(OpCodes)
            .GetFields(BindingFlags.Public | BindingFlags.Static)
            .Select(field => (OpCode)(field.GetValue(null)
                ?? throw new InvalidOperationException($"Opcode field '{field.Name}' has no value.")))
            .ToDictionary(opcode => unchecked((ushort)opcode.Value));

    private sealed class TestFrame : ILatestFrameLease
    {
        private int _schedulerOwned;
        private long _schedulerGeneration;

        public TestFrame(long generation)
        {
            Generation = generation;
        }

        public long Generation { get; }

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
                throw new InvalidOperationException("Test frame is not scheduler-owned.");
            }
        }
    }

    private sealed class BlockingTestFrame : ILatestFrameLease
    {
        private readonly ManualResetEventSlim? _entered;
        private readonly ManualResetEventSlim? _proceed;
        private int _schedulerOwned;
        private long _schedulerGeneration;

        public BlockingTestFrame(
            long generation,
            ManualResetEventSlim? entered = null,
            ManualResetEventSlim? proceed = null)
        {
            GenerationWithoutBlocking = generation;
            _entered = entered;
            _proceed = proceed;
        }

        public long Generation
        {
            get
            {
                _entered?.Set();
                _proceed?.Wait();
                return GenerationWithoutBlocking;
            }
        }

        public long GenerationWithoutBlocking { get; }

        public long PublicationTimestamp { get; set; }

        public long SchedulerGeneration
        {
            get
            {
                _entered?.Set();
                _proceed?.Wait();
                return Volatile.Read(ref _schedulerGeneration);
            }
        }

        public bool TryAcquireSchedulerOwnership()
        {
            if (Interlocked.CompareExchange(ref _schedulerOwned, 1, 0) != 0)
            {
                return false;
            }

            Volatile.Write(ref _schedulerGeneration, GenerationWithoutBlocking);
            return true;
        }

        public void ReleaseSchedulerOwnership()
        {
            if (Interlocked.Exchange(ref _schedulerOwned, 0) != 1)
            {
                throw new InvalidOperationException("Blocking test frame is not scheduler-owned.");
            }
        }
    }

    private sealed class MutableTestFrame : ILatestFrameLease
    {
        private int _schedulerOwned;
        private long _schedulerGeneration;

        public MutableTestFrame(long generation)
        {
            GenerationValue = generation;
        }

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
                throw new InvalidOperationException("Mutable test frame is not scheduler-owned.");
            }
        }
    }

    private sealed class ThrowingTimestampFrame : ILatestFrameLease
    {
        private int _schedulerOwned;
        private long _schedulerGeneration;

        public ThrowingTimestampFrame(long identity)
        {
            Identity = identity;
        }

        public long Identity { get; }

        public long Generation => Identity;

        public long PublicationTimestamp
        {
            get => 0;
            set => throw new InvalidOperationException("timestamp failure");
        }

        public long SchedulerGeneration => Volatile.Read(ref _schedulerGeneration);

        public bool TryAcquireSchedulerOwnership()
        {
            if (Interlocked.CompareExchange(ref _schedulerOwned, 1, 0) != 0)
            {
                return false;
            }

            Volatile.Write(ref _schedulerGeneration, Identity);
            return true;
        }

        public void ReleaseSchedulerOwnership()
        {
            if (Interlocked.Exchange(ref _schedulerOwned, 0) != 1)
            {
                throw new InvalidOperationException("Throwing timestamp frame is not scheduler-owned.");
            }
        }
    }

    private sealed class ManualUiWorkPoster : IUiWorkPoster
    {
        private readonly object _sync = new();
        private readonly Queue<Action> _pending = new();
        private int _callbackThreadId;

        public bool AcceptPosts { get; set; } = true;

        public Action? OnPost { get; set; }

        public int PendingCount
        {
            get
            {
                lock (_sync)
                {
                    return _pending.Count;
                }
            }
        }

        public int MaximumPendingCount { get; private set; }

        public bool CheckAccess() =>
            Volatile.Read(ref _callbackThreadId) == Environment.CurrentManagedThreadId;

        public bool TryPost(Action callback)
        {
            OnPost?.Invoke();
            if (!AcceptPosts)
            {
                return false;
            }

            lock (_sync)
            {
                _pending.Enqueue(callback);
                MaximumPendingCount = Math.Max(MaximumPendingCount, _pending.Count);
            }
            return true;
        }

        public void DrainNext()
        {
            Action callback;
            lock (_sync)
            {
                callback = _pending.Dequeue();
            }
            Volatile.Write(ref _callbackThreadId, Environment.CurrentManagedThreadId);
            try
            {
                callback();
            }
            finally
            {
                Volatile.Write(ref _callbackThreadId, 0);
            }
        }
    }

    private sealed class ManualClock : IMonotonicClock
    {
        public ManualClock(long frequency, long value)
        {
            Frequency = frequency;
            Value = value;
        }

        public long Frequency { get; }

        public long Value { get; set; }

        public long GetTimestamp() => Value;
    }

    private sealed class ManualAllocationCounter : IThreadAllocationCounter
    {
        public ManualAllocationCounter(long value)
        {
            Value = value;
        }

        public long Value { get; set; }

        public long GetAllocatedBytes() => Value;
    }

    private sealed class IncreasingDurationClock : IMonotonicClock
    {
        private long _callCount;
        private long _duration;

        public long Frequency => 1_000;

        public long GetTimestamp()
        {
            var call = Interlocked.Increment(ref _callCount);
            return (call & 1) == 1
                ? 0
                : Interlocked.Increment(ref _duration);
        }
    }

    private sealed class ConstantAllocationCounter : IThreadAllocationCounter
    {
        public long GetAllocatedBytes() => 0;
    }

    private sealed class ThrowingClock : IMonotonicClock
    {
        public long Frequency => 1_000;

        public long GetTimestamp() => throw new InvalidOperationException("clock failure");
    }

    private sealed class ThrowingOnSecondAllocationCounter : IThreadAllocationCounter
    {
        private int _callCount;

        public long GetAllocatedBytes()
        {
            if (Interlocked.Increment(ref _callCount) == 2)
            {
                throw new InvalidOperationException("allocation counter failure");
            }

            return 0;
        }
    }
}
