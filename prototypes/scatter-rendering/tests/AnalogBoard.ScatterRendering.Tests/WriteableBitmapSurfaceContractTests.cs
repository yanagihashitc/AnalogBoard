using System.Windows.Media;
using System.Windows.Threading;
using AnalogBoard.ScatterRendering.Wpf;

namespace AnalogBoard.ScatterRendering.Tests;

internal static class WriteableBitmapSurfaceContractTests
{
    public static IReadOnlyList<ContractTestCase> Cases { get; } =
    [
        new(nameof(GivenReusablePixels_WhenPublished_ThenBitmapIdentityAndCopiedBytesRemainExact),
            GivenReusablePixels_WhenPublished_ThenBitmapIdentityAndCopiedBytesRemainExact),
        new(nameof(GivenBoundaryDimensions_WhenConstructed_ThenBgra32ShapeIsBounded),
            GivenBoundaryDimensions_WhenConstructed_ThenBgra32ShapeIsBounded),
        new(nameof(GivenInvalidBufferOrGeneration_WhenPublished_ThenTypedResourceFailureLeavesBitmapUnchanged),
            GivenInvalidBufferOrGeneration_WhenPublished_ThenTypedResourceFailureLeavesBitmapUnchanged),
        new(nameof(GivenNonOwnerThreadOrDisposedSurface_WhenPublished_ThenTypedResourceFailure),
            GivenNonOwnerThreadOrDisposedSurface_WhenPublished_ThenTypedResourceFailure),
        new(nameof(GivenOwnerDispatcher_WhenWorkPosted_ThenCallbackIsQueuedAndThreadAccessIsObservable),
            GivenOwnerDispatcher_WhenWorkPosted_ThenCallbackIsQueuedAndThreadAccessIsObservable),
    ];

    private static void GivenReusablePixels_WhenPublished_ThenBitmapIdentityAndCopiedBytesRemainExact()
    {
        // Given: One owner-STA surface and one caller-owned reusable 2x2 BGRA array.
        using var surface = new WriteableBitmapDensitySurface(2, 2);
        var bitmap = surface.Bitmap;
        var pixels = new byte[]
        {
            0x01, 0x02, 0x03, 0xFF, 0x11, 0x12, 0x13, 0xFF,
            0x21, 0x22, 0x23, 0xFF, 0x31, 0x32, 0x33, 0xFF,
        };
        var firstExpected = pixels.ToArray();

        // When: Publishing, mutating only after return, and publishing the same array again.
        surface.Publish(generation: 1, pixels);
        var firstActual = CopyPixels(surface);
        Array.Fill(pixels, (byte)0x7F);
        surface.Publish(generation: 2, pixels);
        var secondActual = CopyPixels(surface);

        // Then: WPF copied each frame, retained one bitmap, and recorded the latest generation.
        ContractAssert.Equal(true, ReferenceEquals(bitmap, surface.Bitmap));
        ContractAssert.SequenceEqual(firstExpected, firstActual);
        ContractAssert.SequenceEqual(pixels, secondActual);
        ContractAssert.Equal(2L, surface.LastPublishedGeneration);
        ContractAssert.Equal(true, surface.Bitmap.CheckAccess());
    }

    private static void GivenBoundaryDimensions_WhenConstructed_ThenBgra32ShapeIsBounded()
    {
        // Given/When: Constructing the minimum and 1024-square headroom surfaces.
        using var minimum = new WriteableBitmapDensitySurface(1, 1);
        using var headroom = new WriteableBitmapDensitySurface(1024, 1024);

        // Then: Both are exact Bgra32, 96-DPI, checked-stride surfaces.
        ContractAssert.Equal(PixelFormats.Bgra32, minimum.Bitmap.Format);
        ContractAssert.Equal(1, minimum.Bitmap.PixelWidth);
        ContractAssert.Equal(1, minimum.Bitmap.PixelHeight);
        ContractAssert.Equal(4, minimum.SourceStride);
        ContractAssert.Equal(4, minimum.RequiredPixelLength);
        ContractAssert.Equal(96.0, minimum.Bitmap.DpiX);
        ContractAssert.Equal(96.0, minimum.Bitmap.DpiY);
        ContractAssert.Equal(1024, headroom.Bitmap.PixelWidth);
        ContractAssert.Equal(1024, headroom.Bitmap.PixelHeight);
        ContractAssert.Equal(4096, headroom.SourceStride);
        ContractAssert.Equal(4 * 1024 * 1024, headroom.RequiredPixelLength);

        ContractAssert.Throws<DensitySurfaceValidationException>(
            () => new WriteableBitmapDensitySurface(0, 1),
            "Raster surface width must be between 1 and 1024 inclusive; actual: 0.");
        ContractAssert.Throws<DensitySurfaceValidationException>(
            () => new WriteableBitmapDensitySurface(1, 1025),
            "Raster surface height must be between 1 and 1024 inclusive; actual: 1025.");
    }

    private static void GivenInvalidBufferOrGeneration_WhenPublished_ThenTypedResourceFailureLeavesBitmapUnchanged()
    {
        // Given: A surface containing a known first generation.
        using var surface = new WriteableBitmapDensitySurface(2, 2);
        var first = Enumerable.Repeat((byte)0x2A, 16).ToArray();
        surface.Publish(1, first);
        var expected = CopyPixels(surface);

        // When/Then: Short/long buffers and equal/reversed generations fail before mutation.
        ContractAssert.Throws<DensitySurfaceValidationException>(
            () => surface.Publish(2, new byte[15]),
            "BGRA publication length must equal 16 bytes; actual: 15.");
        ContractAssert.Throws<DensitySurfaceValidationException>(
            () => surface.Publish(2, new byte[17]),
            "BGRA publication length must equal 16 bytes; actual: 17.");
        ContractAssert.Throws<DensitySurfaceValidationException>(
            () => surface.Publish(1, new byte[16]),
            "Publication generation must increase monotonically; previous: 1; actual: 1.");
        ContractAssert.Throws<DensitySurfaceValidationException>(
            () => surface.Publish(0, new byte[16]),
            "Publication generation must increase monotonically; previous: 1; actual: 0.");
        ContractAssert.SequenceEqual(expected, CopyPixels(surface));
        ContractAssert.Equal(1L, surface.LastPublishedGeneration);
    }

    private static void GivenNonOwnerThreadOrDisposedSurface_WhenPublished_ThenTypedResourceFailure()
    {
        // Given: A surface owned by this STA and a publication attempted by a foreign thread.
        var surface = new WriteableBitmapDensitySurface(1, 1);
        Exception? foreignException = null;
        var foreign = new Thread(() =>
        {
            try
            {
                surface.Publish(1, new byte[4]);
            }
            catch (Exception exception)
            {
                foreignException = exception;
            }
        });

        // When: Executing on the foreign thread and then disposing on the owner thread.
        foreign.Start();
        ContractAssert.Equal(true, foreign.Join(TimeSpan.FromSeconds(5)));
        surface.Dispose();
        surface.Dispose();

        // Then: Thread affinity and post-dispose use fail through resource-backed typed errors.
        ContractAssert.Equal(typeof(DensitySurfaceValidationException), foreignException?.GetType()!);
        ContractAssert.Equal(
            "Raster surface publication must run on its owner UI thread.",
            foreignException?.Message!);
        ContractAssert.Throws<DensitySurfaceValidationException>(
            () => surface.Publish(1, new byte[4]),
            "Raster surface has been disposed.");
    }

    private static void GivenOwnerDispatcher_WhenWorkPosted_ThenCallbackIsQueuedAndThreadAccessIsObservable()
    {
        // Given: A poster bound to the current owner-STA Dispatcher.
        var dispatcher = Dispatcher.CurrentDispatcher;
        var poster = new DispatcherUiWorkPoster(dispatcher);
        var callbackInvoked = false;
        var frame = new DispatcherFrame();
        var foreignAccess = true;
        var foreign = new Thread(() => foreignAccess = poster.CheckAccess());
        foreign.Start();
        ContractAssert.Equal(true, foreign.Join(TimeSpan.FromSeconds(5)));

        // When: Posting one callback without pumping inline.
        var accepted = poster.TryPost(() =>
        {
            callbackInvoked = true;
            frame.Continue = false;
        });

        // Then: Work is queued, runs under owner access, and foreign access is false.
        ContractAssert.Equal(true, accepted);
        ContractAssert.Equal(false, callbackInvoked);
        ContractAssert.Equal(true, poster.CheckAccess());
        ContractAssert.Equal(false, foreignAccess);
        Dispatcher.PushFrame(frame);
        ContractAssert.Equal(true, callbackInvoked);

        // Given: Publication and input work queued together on the owner Dispatcher.
        var order = new List<string>();
        var fairnessFrame = new DispatcherFrame();
        void CompleteWhenBothRan()
        {
            if (order.Count == 2)
            {
                fairnessFrame.Continue = false;
            }
        }
        poster.TryPost(() =>
        {
            order.Add("publication");
            CompleteWhenBothRan();
        });
        dispatcher.BeginInvoke(DispatcherPriority.Input, () =>
        {
            order.Add("input");
            CompleteWhenBothRan();
        });

        // When: The Dispatcher pumps both callbacks.
        Dispatcher.PushFrame(fairnessFrame);

        // Then: Input priority is never starved behind publication work.
        ContractAssert.SequenceEqual(new[] { "input", "publication" }, order);
    }

    private static byte[] CopyPixels(WriteableBitmapDensitySurface surface)
    {
        var result = new byte[surface.RequiredPixelLength];
        surface.Bitmap.CopyPixels(result, surface.SourceStride, 0);
        return result;
    }
}
