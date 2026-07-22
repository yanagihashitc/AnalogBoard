using System.Runtime.InteropServices;

namespace AnalogBoard.ScatterRendering.Core;

public static class DensityRasterizer
{
    public const int BytesPerPixel = 4;

    private static readonly byte[] PaletteBlue = [0x54, 0x8B, 0x8C, 0x62, 0x25];
    private static readonly byte[] PaletteGreen = [0x01, 0x52, 0x91, 0xC9, 0xE7];
    private static readonly byte[] PaletteRed = [0x44, 0x3B, 0x21, 0x5E, 0xFD];

    public static void Rasterize(DensityGridBuffer source, Span<byte> bgraPixels)
    {
        ArgumentNullException.ThrowIfNull(source);
        Rasterize(
            source.Counts.Span,
            source.Width,
            source.Height,
            source.MaximumCount,
            bgraPixels);
    }

    public static void Rasterize(
        ReadOnlySpan<int> counts,
        int width,
        int height,
        int maximumCount,
        Span<byte> bgraPixels)
    {
        ValidateDimension(width, "width");
        ValidateDimension(height, "height");

        var expectedCountLength = checked(width * height);
        if (counts.Length != expectedCountLength)
        {
            throw new DensityRasterValidationException(
                $"Density count length must equal width * height; expected: {expectedCountLength}; actual: {counts.Length}.");
        }

        var expectedPixelLength = checked(expectedCountLength * BytesPerPixel);
        if (bgraPixels.Length != expectedPixelLength)
        {
            throw new DensityRasterValidationException(
                $"BGRA pixel length must equal width * height * 4; expected: {expectedPixelLength}; actual: {bgraPixels.Length}.");
        }

        if (MemoryMarshal.AsBytes(counts).Overlaps(bgraPixels))
        {
            throw new DensityRasterValidationException(
                "Density counts and BGRA pixels must not overlap.");
        }

        if (maximumCount < 0)
        {
            throw new DensityRasterValidationException(
                $"Declared maximum count must be non-negative; actual: {maximumCount}.");
        }

        var observedMaximum = 0;
        for (var index = 0; index < counts.Length; index++)
        {
            var count = counts[index];
            if (count < 0)
            {
                throw new DensityRasterValidationException(
                    $"Density counts must not contain negative values; index: {index}; actual: {count}.");
            }

            if (count > maximumCount)
            {
                throw new DensityRasterValidationException(
                    $"Density count must not exceed declared maximum; index: {index}; actual: {count}; maximum: {maximumCount}.");
            }

            if (count > observedMaximum)
            {
                observedMaximum = count;
            }
        }

        if (observedMaximum != maximumCount)
        {
            throw new DensityRasterValidationException(
                $"Declared maximum count must equal observed maximum; declared: {maximumCount}; observed: {observedMaximum}.");
        }

        for (var sourceY = 0; sourceY < height; sourceY++)
        {
            var destinationY = height - 1 - sourceY;
            var sourceRowOffset = sourceY * width;
            var destinationRowOffset = destinationY * width * BytesPerPixel;

            for (var x = 0; x < width; x++)
            {
                var count = counts[sourceRowOffset + x];
                var pixelOffset = destinationRowOffset + (x * BytesPerPixel);
                if (count == 0)
                {
                    bgraPixels[pixelOffset] = 0x17;
                    bgraPixels[pixelOffset + 1] = 0x11;
                    bgraPixels[pixelOffset + 2] = 0x0D;
                    bgraPixels[pixelOffset + 3] = 0xFF;
                    continue;
                }

                var scaled = (long)count * 4;
                var segment = Math.Min((int)(scaled / maximumCount), 3);
                var remainder = scaled - ((long)segment * maximumCount);
                bgraPixels[pixelOffset] = Interpolate(
                    PaletteBlue[segment],
                    PaletteBlue[segment + 1],
                    remainder,
                    maximumCount);
                bgraPixels[pixelOffset + 1] = Interpolate(
                    PaletteGreen[segment],
                    PaletteGreen[segment + 1],
                    remainder,
                    maximumCount);
                bgraPixels[pixelOffset + 2] = Interpolate(
                    PaletteRed[segment],
                    PaletteRed[segment + 1],
                    remainder,
                    maximumCount);
                bgraPixels[pixelOffset + 3] = 0xFF;
            }
        }
    }

    private static byte Interpolate(
        byte start,
        byte end,
        long remainder,
        int maximumCount)
    {
        var numerator =
            ((long)start * (maximumCount - remainder)) +
            ((long)end * remainder) +
            (maximumCount / 2L);
        return (byte)(numerator / maximumCount);
    }

    private static void ValidateDimension(int value, string parameterName)
    {
        if (value < 1 || value > DensityGridBuffer.MaximumBinsPerAxis)
        {
            throw new DensityRasterValidationException(
                $"Raster {parameterName} must be between 1 and {DensityGridBuffer.MaximumBinsPerAxis} inclusive; actual: {value}.");
        }
    }
}

public sealed class DensityRasterValidationException : Exception
{
    public DensityRasterValidationException(string message)
        : base(message)
    {
    }
}
