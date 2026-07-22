using System.Globalization;
using System.Runtime.InteropServices;

namespace AnalogBoard.ScatterRendering.Core;

public readonly record struct GmiDisplayRange(double Minimum, double Maximum);

/// <summary>
/// Converts one bounded selected-channel snapshot into preallocated density-style BGRA output.
/// </summary>
public static class GmiOverlayRasterizer
{
    public static void Rasterize(
        GmiSnapshot snapshot,
        GmiDisplayRange displayRange,
        int width,
        int height,
        Span<int> coverage,
        Span<byte> bgraPixels)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        ValidateDimension(width, "width");
        ValidateDimension(height, "height");
        var rangeWidth = ValidateRange(displayRange);

        var expectedCoverageLength = checked(width * height);
        if (coverage.Length != expectedCoverageLength)
        {
            throw new GmiRasterValidationException(
                $"GMI coverage length must equal width * height; expected: {expectedCoverageLength}; actual: {coverage.Length}.");
        }

        var expectedPixelLength = checked(expectedCoverageLength * DensityRasterizer.BytesPerPixel);
        if (bgraPixels.Length != expectedPixelLength)
        {
            throw new GmiRasterValidationException(
                $"GMI BGRA pixel length must equal width * height * 4; expected: {expectedPixelLength}; actual: {bgraPixels.Length}.");
        }

        if (MemoryMarshal.AsBytes(coverage).Overlaps(bgraPixels))
        {
            throw new GmiRasterValidationException(
                "GMI coverage and BGRA pixels must not overlap.");
        }

        var values = snapshot.WaveformValues.Span;
        var valueBytes = MemoryMarshal.AsBytes(values);
        if (valueBytes.Overlaps(MemoryMarshal.AsBytes(coverage)) ||
            valueBytes.Overlaps(bgraPixels))
        {
            throw new GmiRasterValidationException(
                "GMI snapshot values must not overlap coverage or BGRA output storage.");
        }

        RasterizeValidated(
            values,
            snapshot.WaveformCount,
            snapshot.SamplesPerWaveform,
            displayRange,
            rangeWidth,
            width,
            height,
            coverage,
            bgraPixels);
    }

    internal static void RasterizeValues(
        ReadOnlySpan<ushort> values,
        int waveformCount,
        int samplesPerWaveform,
        GmiDisplayRange displayRange,
        int width,
        int height,
        Span<int> coverage,
        Span<byte> bgraPixels)
    {
        GmiSnapshot.ValidateMetadata(
            GmiChannelSchema.Version,
            generation: 1,
            GmiChannel.FsGmi,
            waveformCount,
            samplesPerWaveform);
        GmiSnapshot.ValidateWaveformValues(waveformCount, samplesPerWaveform, values);
        ValidateDimension(width, "width");
        ValidateDimension(height, "height");
        var rangeWidth = ValidateRange(displayRange);
        var expectedCoverageLength = checked(width * height);
        if (coverage.Length != expectedCoverageLength)
        {
            throw new GmiRasterValidationException(
                $"GMI coverage length must equal width * height; expected: {expectedCoverageLength}; actual: {coverage.Length}.");
        }
        var expectedPixelLength = checked(expectedCoverageLength * DensityRasterizer.BytesPerPixel);
        if (bgraPixels.Length != expectedPixelLength)
        {
            throw new GmiRasterValidationException(
                $"GMI BGRA pixel length must equal width * height * 4; expected: {expectedPixelLength}; actual: {bgraPixels.Length}.");
        }
        if (MemoryMarshal.AsBytes(coverage).Overlaps(bgraPixels))
        {
            throw new GmiRasterValidationException(
                "GMI coverage and BGRA pixels must not overlap.");
        }

        RasterizeValidated(
            values,
            waveformCount,
            samplesPerWaveform,
            displayRange,
            rangeWidth,
            width,
            height,
            coverage,
            bgraPixels);
    }

    private static void RasterizeValidated(
        ReadOnlySpan<ushort> values,
        int waveformCount,
        int sampleCount,
        GmiDisplayRange displayRange,
        double rangeWidth,
        int width,
        int height,
        Span<int> coverage,
        Span<byte> bgraPixels)
    {
        coverage.Clear();
        var maximumCoverage = 0;
        for (var waveformIndex = 0;
             waveformIndex < waveformCount;
             waveformIndex++)
        {
            var waveformOffset = waveformIndex * sampleCount;
            for (var x = 0; x < width; x++)
            {
                int firstSample;
                int endSampleExclusive;
                if (width <= sampleCount)
                {
                    firstSample = (int)(((long)x * sampleCount) / width);
                    endSampleExclusive = (int)(((long)(x + 1) * sampleCount) / width);
                }
                else
                {
                    if (width == 1)
                    {
                        firstSample = 0;
                    }
                    else
                    {
                        var denominator = width - 1L;
                        var numerator = (long)x * (sampleCount - 1);
                        firstSample = (int)(((2 * numerator) + denominator) /
                            (2 * denominator));
                    }
                    endSampleExclusive = firstSample + 1;
                }

                var minimum = values[waveformOffset + firstSample];
                var maximum = minimum;
                for (var sampleIndex = firstSample + 1;
                     sampleIndex < endSampleExclusive;
                     sampleIndex++)
                {
                    var value = values[waveformOffset + sampleIndex];
                    minimum = Math.Min(minimum, value);
                    maximum = Math.Max(maximum, value);
                }

                maximumCoverage = AddCoverage(
                    minimum,
                    x,
                    displayRange,
                    rangeWidth,
                    width,
                    height,
                    coverage,
                    maximumCoverage);
                if (maximum != minimum)
                {
                    maximumCoverage = AddCoverage(
                        maximum,
                        x,
                        displayRange,
                        rangeWidth,
                        width,
                        height,
                        coverage,
                        maximumCoverage);
                }
            }
        }

        DensityRasterizer.Rasterize(
            coverage,
            width,
            height,
            maximumCoverage,
            bgraPixels);
    }

    private static int AddCoverage(
        ushort value,
        int x,
        GmiDisplayRange displayRange,
        double rangeWidth,
        int width,
        int height,
        Span<int> coverage,
        int maximumCoverage)
    {
        var normalized = Math.Clamp(
            (value - displayRange.Minimum) / rangeWidth,
            0.0,
            1.0);
        var sourceY = height == 1
            ? 0
            : (int)Math.Floor((normalized * (height - 1)) + 0.5);
        var coverageIndex = (sourceY * width) + x;
        var pointCoverage = ++coverage[coverageIndex];
        return Math.Max(pointCoverage, maximumCoverage);
    }

    private static double ValidateRange(GmiDisplayRange displayRange)
    {
        if (!double.IsFinite(displayRange.Minimum) ||
            !double.IsFinite(displayRange.Maximum))
        {
            throw new GmiRasterValidationException(
                $"GMI display range values must be finite; minimum: {Format(displayRange.Minimum)}; maximum: {Format(displayRange.Maximum)}.");
        }

        var rangeWidth = displayRange.Maximum - displayRange.Minimum;
        if (displayRange.Maximum <= displayRange.Minimum)
        {
            throw new GmiRasterValidationException(
                $"GMI display range maximum must be greater than minimum; minimum: {Format(displayRange.Minimum)}; maximum: {Format(displayRange.Maximum)}.");
        }

        if (!double.IsFinite(rangeWidth) || rangeWidth <= 0)
        {
            throw new GmiRasterValidationException(
                $"GMI display range width must be finite and positive; minimum: {Format(displayRange.Minimum)}; maximum: {Format(displayRange.Maximum)}.");
        }

        return rangeWidth;
    }

    private static void ValidateDimension(int value, string parameterName)
    {
        if (value < 1 || value > DensityGridBuffer.MaximumBinsPerAxis)
        {
            throw new GmiRasterValidationException(
                $"GMI raster {parameterName} must be between 1 and {DensityGridBuffer.MaximumBinsPerAxis} inclusive; actual: {value}.");
        }
    }

    private static string Format(double value) =>
        value.ToString("R", CultureInfo.InvariantCulture);
}

public sealed class GmiRasterValidationException : Exception
{
    public GmiRasterValidationException(string message)
        : base(message)
    {
    }
}
