namespace AnalogBoard.ScatterRendering.Core;

public sealed class DensityGridBuffer
{
    public const int MaximumBinsPerAxis = 1_024;

    private readonly int[] _counts;
    private readonly double[] _xEdges;
    private readonly double[] _yEdges;

    public DensityGridBuffer(int width, int height)
    {
        ValidateDimension(width, nameof(width));
        ValidateDimension(height, nameof(height));

        Width = width;
        Height = height;
        _counts = new int[checked(width * height)];
        _xEdges = new double[width + 1];
        _yEdges = new double[height + 1];
    }

    public int Width { get; }

    public int Height { get; }

    public ReadOnlyMemory<int> Counts => _counts;

    public ReadOnlyMemory<double> XEdges => _xEdges;

    public ReadOnlyMemory<double> YEdges => _yEdges;

    public long Generation { get; private set; }

    public int InputEventCount { get; private set; }

    public int NonFinitePairCount { get; private set; }

    public int OutOfRangePairCount { get; private set; }

    public int ClampedPairCount { get; private set; }

    public int BinnedEventCount { get; private set; }

    public int MaximumCount { get; private set; }

    public double XBiexponentialWidth { get; private set; }

    public double YBiexponentialWidth { get; private set; }

    internal Span<int> WritableCounts => _counts;

    internal Span<double> WritableXEdges => _xEdges;

    internal Span<double> WritableYEdges => _yEdges;

    internal void Reset(long generation, int inputEventCount)
    {
        Array.Clear(_counts);
        Array.Clear(_xEdges);
        Array.Clear(_yEdges);
        Generation = generation;
        InputEventCount = inputEventCount;
        NonFinitePairCount = 0;
        OutOfRangePairCount = 0;
        ClampedPairCount = 0;
        BinnedEventCount = 0;
        MaximumCount = 0;
        XBiexponentialWidth = 0.0;
        YBiexponentialWidth = 0.0;
    }

    internal void SetTransformMetadata(
        FittedDisplayTransform xTransform,
        FittedDisplayTransform yTransform)
    {
        XBiexponentialWidth = xTransform.BiexponentialWidth;
        YBiexponentialWidth = yTransform.BiexponentialWidth;
    }

    internal void SetAccounting(
        int nonFinitePairCount,
        int outOfRangePairCount,
        int clampedPairCount,
        int binnedEventCount,
        int maximumCount)
    {
        NonFinitePairCount = nonFinitePairCount;
        OutOfRangePairCount = outOfRangePairCount;
        ClampedPairCount = clampedPairCount;
        BinnedEventCount = binnedEventCount;
        MaximumCount = maximumCount;
    }

    private static void ValidateDimension(int value, string parameterName)
    {
        if (value < 1 || value > MaximumBinsPerAxis)
        {
            throw new DensityBinningValidationException(
                $"Density grid {parameterName} must be between 1 and {MaximumBinsPerAxis} inclusive; actual: {value}.");
        }
    }
}
