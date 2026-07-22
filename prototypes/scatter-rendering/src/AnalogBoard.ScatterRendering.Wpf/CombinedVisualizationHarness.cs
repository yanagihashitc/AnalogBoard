using System.Threading;
using System.Windows;
using System.Windows.Threading;
using AnalogBoard.ScatterRendering.Core;

namespace AnalogBoard.ScatterRendering.Wpf;

/// <summary>
/// Hosts independent bounded scatter and GMI feeds on one WPF dispatcher.
/// </summary>
/// <remarks>
/// Each feed owns its own latest-frame scheduler. Sharing only the dispatcher preserves
/// per-feed pending-one bounds while DispatcherPriority.Background lets input work preempt
/// continued rendering. Pixel copies are diagnostic checkpoint helpers that require
/// caller-owned buffers.
/// </remarks>
public sealed class CombinedVisualizationHarness<TScatterFrame, TGmiFrame> : IDisposable
    where TScatterFrame : class, IRasterFrameLease
    where TGmiFrame : class, IRasterFrameLease
{
    private readonly Dispatcher _dispatcher;
    private readonly WriteableBitmapDensitySurface _scatterSurface;
    private readonly WriteableBitmapDensitySurface _gmiSurface;
    private readonly byte[] _scatterCopyBuffer;
    private readonly byte[] _gmiCopyBuffer;
    private readonly LatestFrameScheduler<TScatterFrame> _scatterScheduler;
    private readonly LatestFrameScheduler<TGmiFrame> _gmiScheduler;

    private long _scatterLastPublishedGeneration;
    private long _gmiLastPublishedGeneration;
    private int _disposed;

    public CombinedVisualizationHarness(
        Dispatcher dispatcher,
        int width,
        int height,
        Action<TScatterFrame> scatterRelease,
        Action<TGmiFrame> gmiRelease,
        int metricCapacity)
        : this(
            dispatcher,
            width,
            height,
            scatterRelease,
            gmiRelease,
            metricCapacity,
            renderStarting: null,
            renderCompleted: null)
    {
    }

    internal CombinedVisualizationHarness(
        Dispatcher dispatcher,
        int width,
        int height,
        Action<TScatterFrame> scatterRelease,
        Action<TGmiFrame> gmiRelease,
        int metricCapacity,
        Action<VisualizationFeed, long>? renderStarting,
        Action<VisualizationFeed, long>? renderCompleted = null)
    {
        ArgumentNullException.ThrowIfNull(dispatcher);
        ArgumentNullException.ThrowIfNull(scatterRelease);
        ArgumentNullException.ThrowIfNull(gmiRelease);
        if (!dispatcher.CheckAccess())
        {
            throw new DensitySurfaceValidationException(
                PrototypeStrings.Get("HarnessOwnerDispatcherRequired"));
        }

        _dispatcher = dispatcher;
        _scatterSurface = new WriteableBitmapDensitySurface(width, height);
        _gmiSurface = new WriteableBitmapDensitySurface(width, height);
        _scatterCopyBuffer = new byte[_scatterSurface.RequiredPixelLength];
        _gmiCopyBuffer = new byte[_gmiSurface.RequiredPixelLength];
        var poster = new DispatcherUiWorkPoster(dispatcher);
        _scatterScheduler = new LatestFrameScheduler<TScatterFrame>(
            poster,
            frame =>
            {
                var generation = frame.SchedulerGeneration;
                renderStarting?.Invoke(VisualizationFeed.Scatter, generation);
                PublishFrame(
                    _scatterSurface,
                    frame,
                    VisualizationFeed.Scatter,
                    _scatterCopyBuffer);
                Volatile.Write(ref _scatterLastPublishedGeneration, generation);
                renderCompleted?.Invoke(VisualizationFeed.Scatter, generation);
            },
            scatterRelease,
            metricCapacity);
        _gmiScheduler = new LatestFrameScheduler<TGmiFrame>(
            poster,
            frame =>
            {
                var generation = frame.SchedulerGeneration;
                renderStarting?.Invoke(VisualizationFeed.Gmi, generation);
                PublishFrame(
                    _gmiSurface,
                    frame,
                    VisualizationFeed.Gmi,
                    _gmiCopyBuffer);
                Volatile.Write(ref _gmiLastPublishedGeneration, generation);
                renderCompleted?.Invoke(VisualizationFeed.Gmi, generation);
            },
            gmiRelease,
            metricCapacity);
    }

    public long ScatterLastPublishedGeneration =>
        Volatile.Read(ref _scatterLastPublishedGeneration);

    public long GmiLastPublishedGeneration =>
        Volatile.Read(ref _gmiLastPublishedGeneration);

    public FrameSubmissionStatus SubmitScatter(TScatterFrame frame) =>
        _scatterScheduler.Submit(frame);

    public FrameSubmissionStatus SubmitGmi(TGmiFrame frame) =>
        _gmiScheduler.Submit(frame);

    public LatestFrameSchedulerMetricsSnapshot GetScatterMetricsSnapshot() =>
        _scatterScheduler.GetMetricsSnapshot();

    public LatestFrameSchedulerMetricsSnapshot GetGmiMetricsSnapshot() =>
        _gmiScheduler.GetMetricsSnapshot();

    public void CopyScatterPixels(byte[] destination) =>
        CopyPixels(_scatterSurface, destination);

    public void CopyGmiPixels(byte[] destination) =>
        CopyPixels(_gmiSurface, destination);

    public void Dispose()
    {
        if (Volatile.Read(ref _disposed) != 0)
        {
            return;
        }

        VerifyOwnerThread();
        if (Interlocked.Exchange(ref _disposed, 1) != 0)
        {
            return;
        }

        _scatterScheduler.Dispose();
        _gmiScheduler.Dispose();
        _scatterSurface.Dispose();
        _gmiSurface.Dispose();
    }

    private static void PublishFrame<TFrame>(
        WriteableBitmapDensitySurface surface,
        TFrame frame,
        VisualizationFeed feed,
        byte[] copyBuffer)
        where TFrame : class, IRasterFrameLease
    {
        if (frame.Width != surface.Width || frame.Height != surface.Height)
        {
            throw new DensitySurfaceValidationException(
                PrototypeStrings.Format(
                    "HarnessFrameShapeMismatch",
                    feed,
                    surface.Width,
                    surface.Height,
                    frame.Width,
                    frame.Height));
        }

        if (frame.BgraPixels.Length != copyBuffer.Length)
        {
            throw new DensitySurfaceValidationException(
                PrototypeStrings.Format(
                    "HarnessCopyBufferLengthMismatch",
                    copyBuffer.Length,
                    frame.BgraPixels.Length));
        }

        frame.BgraPixels.CopyTo(copyBuffer);
        surface.Publish(frame.SchedulerGeneration, copyBuffer);
    }

    private void CopyPixels(
        WriteableBitmapDensitySurface surface,
        byte[] destination)
    {
        VerifyOwnerThread();
        ArgumentNullException.ThrowIfNull(destination);
        if (destination.Length != surface.RequiredPixelLength)
        {
            throw new DensitySurfaceValidationException(
                PrototypeStrings.Format(
                    "HarnessCopyBufferLengthMismatch",
                    surface.RequiredPixelLength,
                    destination.Length));
        }
        surface.Bitmap.CopyPixels(
            new Int32Rect(0, 0, surface.Width, surface.Height),
            destination,
            surface.SourceStride,
            offset: 0);
    }

    private void VerifyOwnerThread()
    {
        if (!_dispatcher.CheckAccess())
        {
            throw new DensitySurfaceValidationException(
                PrototypeStrings.Get("HarnessOwnerDispatcherRequired"));
        }
    }
}

internal enum VisualizationFeed
{
    Scatter = 0,
    Gmi = 1,
}
