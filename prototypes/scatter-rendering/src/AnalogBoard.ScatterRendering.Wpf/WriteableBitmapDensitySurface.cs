using System.Threading;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using AnalogBoard.ScatterRendering.Core;

namespace AnalogBoard.ScatterRendering.Wpf;

/// <summary>
/// Owns one UI-thread WriteableBitmap and copies complete caller-owned BGRA frames into it.
/// </summary>
public sealed class WriteableBitmapDensitySurface : IDisposable
{
    private readonly WriteableBitmap _bitmap;
    private bool _disposed;

    public WriteableBitmapDensitySurface(int width, int height)
    {
        ValidateDimension(width, "SurfaceWidthOutOfRange");
        ValidateDimension(height, "SurfaceHeightOutOfRange");
        if (Thread.CurrentThread.GetApartmentState() != ApartmentState.STA)
        {
            throw new DensitySurfaceValidationException(
                PrototypeStrings.Get("SurfaceOwnerMustBeSta"));
        }

        Width = width;
        Height = height;
        SourceStride = checked(width * DensityRasterizer.BytesPerPixel);
        RequiredPixelLength = checked(SourceStride * height);
        _bitmap = new WriteableBitmap(
            width,
            height,
            96.0,
            96.0,
            PixelFormats.Bgra32,
            palette: null);
    }

    public int Width { get; }

    public int Height { get; }

    public int SourceStride { get; }

    public int RequiredPixelLength { get; }

    public long LastPublishedGeneration { get; private set; }

    public WriteableBitmap Bitmap
    {
        get
        {
            VerifyAvailable();
            return _bitmap;
        }
    }

    /// <summary>
    /// Copies one frame synchronously; the caller may reuse the array after this method returns.
    /// </summary>
    public void Publish(long generation, byte[] bgraPixels)
    {
        VerifyAvailable();
        ArgumentNullException.ThrowIfNull(bgraPixels);
        if (bgraPixels.Length != RequiredPixelLength)
        {
            throw new DensitySurfaceValidationException(
                PrototypeStrings.Format(
                    "SurfaceBufferLengthMismatch",
                    RequiredPixelLength,
                    bgraPixels.Length));
        }

        if (generation <= LastPublishedGeneration)
        {
            throw new DensitySurfaceValidationException(
                PrototypeStrings.Format(
                    "SurfaceGenerationNotIncreasing",
                    LastPublishedGeneration,
                    generation));
        }

        _bitmap.WritePixels(
            new Int32Rect(0, 0, Width, Height),
            bgraPixels,
            SourceStride,
            offset: 0);
        LastPublishedGeneration = generation;
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        VerifyOwnerThread();
        _disposed = true;
    }

    private static void ValidateDimension(
        int value,
        string resourceKey)
    {
        if (value < 1 || value > DensityGridBuffer.MaximumBinsPerAxis)
        {
            throw new DensitySurfaceValidationException(
                PrototypeStrings.Format(
                    resourceKey,
                    DensityGridBuffer.MaximumBinsPerAxis,
                    value));
        }
    }

    private void VerifyAvailable()
    {
        if (_disposed)
        {
            throw new DensitySurfaceValidationException(
                PrototypeStrings.Get("SurfaceDisposed"));
        }

        VerifyOwnerThread();
    }

    private void VerifyOwnerThread()
    {
        if (!_bitmap.Dispatcher.CheckAccess())
        {
            throw new DensitySurfaceValidationException(
                PrototypeStrings.Get("SurfaceWrongThread"));
        }
    }
}

public sealed class DensitySurfaceValidationException : Exception
{
    public DensitySurfaceValidationException(string message)
        : base(message)
    {
    }
}
