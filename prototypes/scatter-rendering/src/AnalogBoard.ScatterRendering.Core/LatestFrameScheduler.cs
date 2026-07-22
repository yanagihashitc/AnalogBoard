using System.Diagnostics;
using System.Threading;

namespace AnalogBoard.ScatterRendering.Core;

/// <summary>
/// Represents a reusable frame whose ownership transfers to the scheduler on submission.
/// </summary>
public interface ILatestFrameLease
{
    long Generation { get; }

    long PublicationTimestamp { get; set; }

    long SchedulerGeneration { get; }

    /// <summary>
    /// Atomically transfers ownership and captures <see cref="Generation"/> as immutable
    /// scheduler metadata. A false result or exception leaves ownership with the caller.
    /// </summary>
    bool TryAcquireSchedulerOwnership();

    void ReleaseSchedulerOwnership();
}

/// <summary>
/// Posts work without waiting for or invoking the UI callback inline.
/// </summary>
public interface IUiWorkPoster
{
    bool CheckAccess();

    bool TryPost(Action callback);
}

public interface IMonotonicClock
{
    long Frequency { get; }

    long GetTimestamp();
}

public interface IThreadAllocationCounter
{
    long GetAllocatedBytes();
}

public enum FrameSubmissionStatus
{
    Accepted = 0,
    RejectedStaleGeneration = 1,
    RejectedConcurrentPublisher = 2,
    RejectedDisposed = 3,
    RejectedFaulted = 4,
    RejectedPostFailure = 5,
    RejectedLeaseAlreadyOwned = 6,
}

/// <summary>
/// Coalesces a single-producer feed into at most one pending frame and one UI callback.
/// </summary>
/// <remarks>
/// The release callback must be thread-safe and nonblocking. Submit transfers newly acquired
/// leases, including ordinary rejected submissions, and releases each exactly once. Resubmitting
/// an already-owned lease is rejected without a second transfer or release.
/// Metric snapshots allocate bounded copies and should be captured at checkpoint boundaries.
/// </remarks>
public sealed class LatestFrameScheduler<TFrame> : IDisposable
    where TFrame : class, ILatestFrameLease
{
    private const int MaximumMetricCapacity = 65_536;

    private readonly IUiWorkPoster _poster;
    private readonly Action<TFrame> _renderer;
    private readonly Action<TFrame> _release;
    private readonly IMonotonicClock _clock;
    private readonly IThreadAllocationCounter _allocationCounter;
    private readonly Action _drainCallback;
    private readonly FixedMetricRing _publicationDurations;
    private readonly FixedMetricRing _publishToDrainLatencies;
    private readonly FixedMetricRing _producerAllocatedBytes;

    private TFrame? _latest;
    private int _drainArmed;
    private int _disposed;
    private int _faulted;
    private int _publishActive;
    private long _lastAcceptedGeneration;
    private long _lastRenderedGeneration;
    private long _acceptedCount;
    private long _renderedCount;
    private long _coalescedCount;
    private long _staleGenerationCount;
    private long _concurrentPublisherCount;
    private long _disposedRejectionCount;
    private long _faultedRejectionCount;
    private long _postFailureCount;
    private long _renderFailureCount;
    private long _releaseFailureCount;
    private long _leaseAlreadyOwnedCount;
    private int _pendingFrameMaximum;
    private int _pendingCallbackMaximum;

    public LatestFrameScheduler(
        IUiWorkPoster poster,
        Action<TFrame> renderer,
        Action<TFrame> release,
        int metricCapacity = 1_024,
        IMonotonicClock? clock = null,
        IThreadAllocationCounter? allocationCounter = null)
    {
        ArgumentNullException.ThrowIfNull(poster);
        ArgumentNullException.ThrowIfNull(renderer);
        ArgumentNullException.ThrowIfNull(release);
        if (metricCapacity < 1 || metricCapacity > MaximumMetricCapacity)
        {
            throw new LatestFrameSchedulerValidationException(
                $"Metric capacity must be between 1 and {MaximumMetricCapacity} inclusive; actual: {metricCapacity}.");
        }

        _poster = poster;
        _renderer = renderer;
        _release = release;
        _clock = clock ?? StopwatchMonotonicClock.Instance;
        _allocationCounter = allocationCounter ?? GcThreadAllocationCounter.Instance;
        if (_clock.Frequency <= 0)
        {
            throw new LatestFrameSchedulerValidationException(
                $"Monotonic clock frequency must be positive; actual: {_clock.Frequency}.");
        }

        _drainCallback = DrainOnce;
        _publicationDurations = new FixedMetricRing(metricCapacity);
        _publishToDrainLatencies = new FixedMetricRing(metricCapacity);
        _producerAllocatedBytes = new FixedMetricRing(metricCapacity);
    }

    /// <summary>
    /// Transfers one frame lease without awaiting or locking the UI.
    /// </summary>
    public FrameSubmissionStatus Submit(TFrame frame)
    {
        ArgumentNullException.ThrowIfNull(frame);

        bool acquired;
        try
        {
            acquired = frame.TryAcquireSchedulerOwnership();
        }
        catch (Exception)
        {
            Interlocked.Exchange(ref _faulted, 1);
            return FrameSubmissionStatus.RejectedFaulted;
        }

        if (!acquired)
        {
            Interlocked.Increment(ref _leaseAlreadyOwnedCount);
            return FrameSubmissionStatus.RejectedLeaseAlreadyOwned;
        }

        if (Interlocked.CompareExchange(ref _publishActive, 1, 0) != 0)
        {
            Interlocked.Increment(ref _concurrentPublisherCount);
            return TryRelease(frame)
                ? FrameSubmissionStatus.RejectedConcurrentPublisher
                : FrameSubmissionStatus.RejectedFaulted;
        }

        TFrame? directlyOwned = frame;
        var status = FrameSubmissionStatus.RejectedFaulted;
        var startedAt = 0L;
        var allocatedBefore = 0L;
        var clockStarted = false;
        var allocationStarted = false;
        try
        {
            startedAt = _clock.GetTimestamp();
            clockStarted = true;
            allocatedBefore = _allocationCounter.GetAllocatedBytes();
            allocationStarted = true;
            status = SubmitOwnedFrame(frame, startedAt, ref directlyOwned);
        }
        catch (Exception)
        {
            status = FrameSubmissionStatus.RejectedFaulted;
            FaultAndReleasePendingPreservingArm();
            ReleaseDirectlyOwned(ref directlyOwned);
        }
        finally
        {
            try
            {
                if (!TryRecordSubmissionMetrics(
                        clockStarted,
                        startedAt,
                        allocationStarted,
                        allocatedBefore))
                {
                    FaultAndReleasePendingPreservingArm();
                    ReleaseDirectlyOwned(ref directlyOwned);
                }
            }
            finally
            {
                Volatile.Write(ref _publishActive, 0);
            }
        }

        return status;
    }

    private FrameSubmissionStatus SubmitOwnedFrame(
        TFrame frame,
        long startedAt,
        ref TFrame? directlyOwned)
    {
        if (Volatile.Read(ref _disposed) != 0)
        {
            Interlocked.Increment(ref _disposedRejectionCount);
            return ReleaseDirectlyOwned(ref directlyOwned)
                ? FrameSubmissionStatus.RejectedDisposed
                : FrameSubmissionStatus.RejectedFaulted;
        }

        if (Volatile.Read(ref _faulted) != 0)
        {
            Interlocked.Increment(ref _faultedRejectionCount);
            ReleaseDirectlyOwned(ref directlyOwned);
            return FrameSubmissionStatus.RejectedFaulted;
        }

        var generation = frame.SchedulerGeneration;
        var previousGeneration = Volatile.Read(ref _lastAcceptedGeneration);
        if (generation <= previousGeneration)
        {
            Interlocked.Increment(ref _staleGenerationCount);
            return ReleaseDirectlyOwned(ref directlyOwned)
                ? FrameSubmissionStatus.RejectedStaleGeneration
                : FrameSubmissionStatus.RejectedFaulted;
        }

        frame.PublicationTimestamp = startedAt;
        Volatile.Write(ref _lastAcceptedGeneration, generation);
        var replaced = Interlocked.Exchange(ref _latest, frame);
        directlyOwned = null;
        Volatile.Write(ref _pendingFrameMaximum, 1);
        if (replaced is not null)
        {
            Interlocked.Increment(ref _coalescedCount);
            if (!TryRelease(replaced))
            {
                ReleasePending();
                return FrameSubmissionStatus.RejectedFaulted;
            }
        }

        if (Volatile.Read(ref _disposed) != 0)
        {
            ReleasePending();
            Interlocked.Increment(ref _disposedRejectionCount);
            return FrameSubmissionStatus.RejectedDisposed;
        }

        if (Interlocked.CompareExchange(ref _drainArmed, 1, 0) == 0)
        {
            Volatile.Write(ref _pendingCallbackMaximum, 1);
            if (!TryPostDrain())
            {
                return FrameSubmissionStatus.RejectedPostFailure;
            }
        }

        Interlocked.Increment(ref _acceptedCount);
        return FrameSubmissionStatus.Accepted;
    }

    private bool TryRecordSubmissionMetrics(
        bool clockStarted,
        long startedAt,
        bool allocationStarted,
        long allocatedBefore)
    {
        try
        {
            if (allocationStarted)
            {
                var allocatedAfter = _allocationCounter.GetAllocatedBytes();
                _producerAllocatedBytes.Record(Math.Max(0, allocatedAfter - allocatedBefore));
            }

            if (clockStarted)
            {
                var finishedAt = _clock.GetTimestamp();
                _publicationDurations.Record(Math.Max(0, finishedAt - startedAt));
            }

            return true;
        }
        catch (Exception)
        {
            Interlocked.Exchange(ref _faulted, 1);
            return false;
        }
    }

    private bool ReleaseDirectlyOwned(ref TFrame? frame)
    {
        var owned = frame;
        frame = null;
        return owned is null || TryRelease(owned);
    }

    public LatestFrameSchedulerMetricsSnapshot GetMetricsSnapshot()
    {
        var publicationSnapshot = _publicationDurations.Snapshot();
        return new LatestFrameSchedulerMetricsSnapshot(
            AcceptedCount: Interlocked.Read(ref _acceptedCount),
            RenderedCount: Interlocked.Read(ref _renderedCount),
            CoalescedCount: Interlocked.Read(ref _coalescedCount),
            StaleGenerationCount: Interlocked.Read(ref _staleGenerationCount),
            ConcurrentPublisherCount: Interlocked.Read(ref _concurrentPublisherCount),
            DisposedRejectionCount: Interlocked.Read(ref _disposedRejectionCount),
            FaultedRejectionCount: Interlocked.Read(ref _faultedRejectionCount),
            PostFailureCount: Interlocked.Read(ref _postFailureCount),
            RenderFailureCount: Interlocked.Read(ref _renderFailureCount),
            ReleaseFailureCount: Interlocked.Read(ref _releaseFailureCount),
            LeaseAlreadyOwnedCount: Interlocked.Read(ref _leaseAlreadyOwnedCount),
            LastAcceptedGeneration: Volatile.Read(ref _lastAcceptedGeneration),
            LastRenderedGeneration: Volatile.Read(ref _lastRenderedGeneration),
            PendingFrameCount: Volatile.Read(ref _latest) is null ? 0 : 1,
            PendingCallbackCount: Volatile.Read(ref _drainArmed),
            PendingFrameMaximum: Volatile.Read(ref _pendingFrameMaximum),
            PendingCallbackMaximum: Volatile.Read(ref _pendingCallbackMaximum),
            IsDisposed: Volatile.Read(ref _disposed) != 0,
            IsFaulted: Volatile.Read(ref _faulted) != 0,
            StopwatchFrequency: _clock.Frequency,
            PublicationDurationTicks: publicationSnapshot,
            PublishToDrainLatencyTicks: _publishToDrainLatencies.Snapshot(),
            ProducerAllocatedBytes: _producerAllocatedBytes.Snapshot(),
            PublicationDurationP99Milliseconds: CalculatePercentileMilliseconds(
                publicationSnapshot.Samples,
                percentile: 0.99,
                _clock.Frequency));
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0)
        {
            return;
        }

        ReleasePending();
    }

    private bool TryPostDrain()
    {
        try
        {
            if (_poster.TryPost(_drainCallback))
            {
                return true;
            }
        }
        catch (Exception)
        {
            // A posting boundary failure transitions the scheduler to a bounded fault state.
        }

        Interlocked.Increment(ref _postFailureCount);
        FaultAndReleasePendingPreservingArm();
        Volatile.Write(ref _drainArmed, 0);
        return false;
    }

    private void DrainOnce()
    {
        if (Volatile.Read(ref _disposed) != 0 || Volatile.Read(ref _faulted) != 0)
        {
            ReleasePending();
            Volatile.Write(ref _drainArmed, 0);
            return;
        }

        if (!_poster.CheckAccess())
        {
            Interlocked.Increment(ref _renderFailureCount);
            FaultAndReleasePendingPreservingArm();
            Volatile.Write(ref _drainArmed, 0);
            return;
        }

        var frame = Interlocked.Exchange(ref _latest, null);
        if (frame is not null)
        {
            try
            {
                var latency = _clock.GetTimestamp() - frame.PublicationTimestamp;
                var renderedGeneration = frame.SchedulerGeneration;
                _publishToDrainLatencies.Record(Math.Max(0, latency));
                _renderer(frame);
                Volatile.Write(ref _lastRenderedGeneration, renderedGeneration);
                Interlocked.Increment(ref _renderedCount);
            }
            catch (Exception)
            {
                Interlocked.Increment(ref _renderFailureCount);
                Interlocked.Exchange(ref _faulted, 1);
            }
            finally
            {
                TryRelease(frame);
            }
        }

        if (Volatile.Read(ref _faulted) != 0)
        {
            ReleasePending();
            Volatile.Write(ref _drainArmed, 0);
            return;
        }

        CompleteDrainAndRearmIfNeeded();
    }

    private void CompleteDrainAndRearmIfNeeded()
    {
        Volatile.Write(ref _drainArmed, 0);
        if (Volatile.Read(ref _disposed) != 0 || Volatile.Read(ref _faulted) != 0)
        {
            ReleasePending();
            return;
        }

        if (Volatile.Read(ref _latest) is not null &&
            Interlocked.CompareExchange(ref _drainArmed, 1, 0) == 0)
        {
            Volatile.Write(ref _pendingCallbackMaximum, 1);
            TryPostDrain();
        }
    }

    private void FaultAndReleasePendingPreservingArm()
    {
        Interlocked.Exchange(ref _faulted, 1);
        ReleasePending();
    }

    private void ReleasePending()
    {
        var pending = Interlocked.Exchange(ref _latest, null);
        if (pending is not null)
        {
            TryRelease(pending);
        }
    }

    private bool TryRelease(TFrame frame)
    {
        try
        {
            frame.ReleaseSchedulerOwnership();
            _release(frame);
            return true;
        }
        catch (Exception)
        {
            Interlocked.Increment(ref _releaseFailureCount);
            Interlocked.Exchange(ref _faulted, 1);
            return false;
        }
    }

    private static double CalculatePercentileMilliseconds(
        IReadOnlyList<long> samples,
        double percentile,
        long frequency)
    {
        if (samples.Count == 0)
        {
            return double.NaN;
        }

        var sorted = samples.ToArray();
        Array.Sort(sorted);
        var index = Math.Max(0, (int)Math.Ceiling(percentile * sorted.Length) - 1);
        return sorted[index] * 1_000.0 / frequency;
    }

    private sealed class FixedMetricRing
    {
        private readonly long[] _values;
        private int _nextIndex;
        private int _count;
        private long _totalSampleCount;
        private long _overwrittenSampleCount;
        private long _writeVersion;

        public FixedMetricRing(int capacity)
        {
            _values = new long[capacity];
        }

        public void Record(long value)
        {
            Interlocked.Increment(ref _writeVersion);
            try
            {
                _values[_nextIndex] = value;
                _nextIndex = (_nextIndex + 1) % _values.Length;
                if (_count < _values.Length)
                {
                    _count++;
                }
                else
                {
                    _overwrittenSampleCount++;
                }

                _totalSampleCount++;
            }
            finally
            {
                Interlocked.Increment(ref _writeVersion);
            }
        }

        public BoundedMetricSnapshot Snapshot()
        {
            var scratch = new long[_values.Length];
            var spinner = new SpinWait();
            while (true)
            {
                var versionBefore = Volatile.Read(ref _writeVersion);
                if ((versionBefore & 1) != 0)
                {
                    spinner.SpinOnce();
                    continue;
                }

                var count = Volatile.Read(ref _count);
                var nextIndex = Volatile.Read(ref _nextIndex);
                var totalSampleCount = Volatile.Read(ref _totalSampleCount);
                var overwrittenSampleCount = Volatile.Read(ref _overwrittenSampleCount);
                var start = count == _values.Length ? nextIndex : 0;
                for (var index = 0; index < count; index++)
                {
                    scratch[index] = _values[(start + index) % _values.Length];
                }

                var versionAfter = Volatile.Read(ref _writeVersion);
                if (versionBefore != versionAfter || (versionAfter & 1) != 0)
                {
                    spinner.SpinOnce();
                    continue;
                }

                var samples = count == scratch.Length
                    ? scratch
                    : scratch.AsSpan(0, count).ToArray();
                return new BoundedMetricSnapshot(
                    samples,
                    totalSampleCount,
                    overwrittenSampleCount);
            }
        }
    }
}

public sealed record BoundedMetricSnapshot(
    IReadOnlyList<long> Samples,
    long TotalSampleCount,
    long OverwrittenSampleCount);

public sealed record LatestFrameSchedulerMetricsSnapshot(
    long AcceptedCount,
    long RenderedCount,
    long CoalescedCount,
    long StaleGenerationCount,
    long ConcurrentPublisherCount,
    long DisposedRejectionCount,
    long FaultedRejectionCount,
    long PostFailureCount,
    long RenderFailureCount,
    long ReleaseFailureCount,
    long LeaseAlreadyOwnedCount,
    long LastAcceptedGeneration,
    long LastRenderedGeneration,
    int PendingFrameCount,
    int PendingCallbackCount,
    int PendingFrameMaximum,
    int PendingCallbackMaximum,
    bool IsDisposed,
    bool IsFaulted,
    long StopwatchFrequency,
    BoundedMetricSnapshot PublicationDurationTicks,
    BoundedMetricSnapshot PublishToDrainLatencyTicks,
    BoundedMetricSnapshot ProducerAllocatedBytes,
    double PublicationDurationP99Milliseconds);

public sealed class LatestFrameSchedulerValidationException : Exception
{
    public LatestFrameSchedulerValidationException(string message)
        : base(message)
    {
    }
}

internal sealed class StopwatchMonotonicClock : IMonotonicClock
{
    public static StopwatchMonotonicClock Instance { get; } = new();

    public long Frequency => Stopwatch.Frequency;

    public long GetTimestamp() => Stopwatch.GetTimestamp();
}

internal sealed class GcThreadAllocationCounter : IThreadAllocationCounter
{
    public static GcThreadAllocationCounter Instance { get; } = new();

    public long GetAllocatedBytes() => GC.GetAllocatedBytesForCurrentThread();
}
