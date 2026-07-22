using System.Threading;

namespace AnalogBoard.ScatterRendering.Core;

public interface IRasterFrameLease : ILatestFrameLease
{
    int Width { get; }

    int Height { get; }

    ReadOnlySpan<byte> BgraPixels { get; }
}

public delegate void RasterPixelWriter(Span<byte> bgraPixels);

/// <summary>
/// Owns one fixed BGRA buffer whose prepared generation transfers with scheduler ownership.
/// </summary>
public sealed class ReusableRasterFrameLease : IRasterFrameLease
{
    private readonly byte[] _bgraPixels;
    private int _ownershipState;
    private long _generation;
    private long _lastSuccessfulGeneration;
    private long _schedulerGeneration;

    public ReusableRasterFrameLease(int width, int height)
    {
        ValidateDimension(width, "width");
        ValidateDimension(height, "height");
        Width = width;
        Height = height;
        _bgraPixels = new byte[checked(width * height * DensityRasterizer.BytesPerPixel)];
    }

    public int Width { get; }

    public int Height { get; }

    public ReadOnlySpan<byte> BgraPixels => _bgraPixels;

    public long Generation => Volatile.Read(ref _generation);

    public long PublicationTimestamp { get; set; }

    public long SchedulerGeneration => Volatile.Read(ref _schedulerGeneration);

    public void Prepare(long generation, RasterPixelWriter writer)
    {
        ArgumentNullException.ThrowIfNull(writer);
        BeginPrepare(generation);
        Volatile.Write(ref _generation, 0);
        try
        {
            writer(_bgraPixels);
            Volatile.Write(ref _lastSuccessfulGeneration, generation);
            Volatile.Write(ref _generation, generation);
        }
        finally
        {
            Volatile.Write(ref _ownershipState, 0);
        }
    }

    public bool TryAcquireSchedulerOwnership()
    {
        if (Volatile.Read(ref _generation) <= 0 ||
            Interlocked.CompareExchange(ref _ownershipState, 2, 0) != 0)
        {
            return false;
        }

        Volatile.Write(ref _schedulerGeneration, Volatile.Read(ref _generation));
        return true;
    }

    public void ReleaseSchedulerOwnership()
    {
        if (Interlocked.CompareExchange(ref _ownershipState, 0, 2) != 2)
        {
            throw new RasterFrameLeaseValidationException(
                "Raster frame is not scheduler-owned.");
        }
    }

    private void BeginPrepare(long generation)
    {
        if (Interlocked.CompareExchange(ref _ownershipState, 1, 0) != 0)
        {
            throw new RasterFrameLeaseValidationException(
                "Raster frame cannot be prepared while producer or scheduler ownership is active.");
        }

        var previousGeneration = Volatile.Read(ref _lastSuccessfulGeneration);
        if (generation <= previousGeneration)
        {
            Volatile.Write(ref _ownershipState, 0);
            throw new RasterFrameLeaseValidationException(
                $"Raster frame generation must increase; previous: {previousGeneration}; actual: {generation}.");
        }
    }

    internal static void ValidateDimension(int value, string name)
    {
        if (value < 1 || value > DensityGridBuffer.MaximumBinsPerAxis)
        {
            throw new RasterFrameLeaseValidationException(
                $"Raster frame {name} must be between 1 and {DensityGridBuffer.MaximumBinsPerAxis} inclusive; actual: {value}.");
        }
    }
}

/// <summary>
/// Couples one bounded sampled GMI payload, its raster, and its scheduler generation.
/// </summary>
public sealed class ReusableGmiRasterFrameLease : IRasterFrameLease
{
    private readonly byte[] _bgraPixels;
    private readonly ushort[] _waveformValues = new ushort[
        GmiSnapshot.MaximumWaveformCount * GmiSnapshot.MaximumSamplesPerWaveform];
    private readonly int[] _coverage;
    private int _ownershipState;
    private long _generation;
    private long _lastSuccessfulGeneration;
    private long _schedulerGeneration;

    public ReusableGmiRasterFrameLease(int width, int height)
    {
        ReusableRasterFrameLease.ValidateDimension(width, "width");
        ReusableRasterFrameLease.ValidateDimension(height, "height");
        Width = width;
        Height = height;
        _coverage = new int[checked(width * height)];
        _bgraPixels = new byte[checked(width * height * DensityRasterizer.BytesPerPixel)];
    }

    public int Width { get; }

    public int Height { get; }

    public ReadOnlySpan<byte> BgraPixels => _bgraPixels;

    public long Generation => Volatile.Read(ref _generation);

    public long PublicationTimestamp { get; set; }

    public long SchedulerGeneration => Volatile.Read(ref _schedulerGeneration);

    public GmiChannel SelectedChannel { get; private set; }

    public int WaveformCount { get; private set; }

    public int SamplesPerWaveform { get; private set; }

    public void Prepare(
        long generation,
        GmiChannel selectedChannel,
        int waveformCount,
        int samplesPerWaveform,
        ReadOnlySpan<ushort> waveformValues,
        GmiDisplayRange displayRange)
    {
        BeginPrepare(generation);
        Volatile.Write(ref _generation, 0);
        try
        {
            GmiSnapshot.ValidateMetadata(
                GmiChannelSchema.Version,
                generation,
                selectedChannel,
                waveformCount,
                samplesPerWaveform);
            GmiSnapshot.ValidateWaveformValues(
                waveformCount,
                samplesPerWaveform,
                waveformValues);
            waveformValues.CopyTo(_waveformValues);
            GmiOverlayRasterizer.RasterizeValues(
                _waveformValues.AsSpan(0, waveformValues.Length),
                waveformCount,
                samplesPerWaveform,
                displayRange,
                Width,
                Height,
                _coverage,
                _bgraPixels);
            SelectedChannel = selectedChannel;
            WaveformCount = waveformCount;
            SamplesPerWaveform = samplesPerWaveform;
            Volatile.Write(ref _lastSuccessfulGeneration, generation);
            Volatile.Write(ref _generation, generation);
        }
        finally
        {
            Volatile.Write(ref _ownershipState, 0);
        }
    }

    public bool TryAcquireSchedulerOwnership()
    {
        if (Volatile.Read(ref _generation) <= 0 ||
            Interlocked.CompareExchange(ref _ownershipState, 2, 0) != 0)
        {
            return false;
        }

        Volatile.Write(ref _schedulerGeneration, Volatile.Read(ref _generation));
        return true;
    }

    public void ReleaseSchedulerOwnership()
    {
        if (Interlocked.CompareExchange(ref _ownershipState, 0, 2) != 2)
        {
            throw new RasterFrameLeaseValidationException(
                "Raster frame is not scheduler-owned.");
        }
    }

    private void BeginPrepare(long generation)
    {
        if (Interlocked.CompareExchange(ref _ownershipState, 1, 0) != 0)
        {
            throw new RasterFrameLeaseValidationException(
                "Raster frame cannot be prepared while producer or scheduler ownership is active.");
        }

        var previousGeneration = Volatile.Read(ref _lastSuccessfulGeneration);
        if (generation <= previousGeneration)
        {
            Volatile.Write(ref _ownershipState, 0);
            throw new RasterFrameLeaseValidationException(
                $"Raster frame generation must increase; previous: {previousGeneration}; actual: {generation}.");
        }
    }
}

public sealed class RasterFrameLeaseValidationException : Exception
{
    public RasterFrameLeaseValidationException(string message)
        : base(message)
    {
    }
}
