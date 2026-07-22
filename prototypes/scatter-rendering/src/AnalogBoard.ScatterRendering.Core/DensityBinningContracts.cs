namespace AnalogBoard.ScatterRendering.Core;

public enum DensityOutlierMode
{
    Exclude = 0,
    ClampToEdge = 1,
}

public readonly record struct DensityRange(double Minimum, double Maximum);

public readonly record struct DensityAxisOptions(
    int FeatureIndex,
    DisplayTransformKind Transform,
    DensityRange? RawRange = null);

public readonly record struct DensityBinningRequest(
    DensityAxisOptions XAxis,
    DensityAxisOptions YAxis,
    DensityOutlierMode OutlierMode);

public sealed class DensityBinningValidationException : ArgumentException
{
    public DensityBinningValidationException(string message)
        : base(message)
    {
    }
}
