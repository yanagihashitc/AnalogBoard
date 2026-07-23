using AnalogBoard.ScatterRendering.Core;

namespace AnalogBoard.ScatterRendering.Tests;

internal static class RasterFrameLeaseContractTests
{
    public static IReadOnlyList<ContractTestCase> Cases { get; } =
    [
        new(nameof(GivenReusableRasterLease_WhenPreparedAndOwned_ThenGenerationAndStorageAreBounded),
            GivenReusableRasterLease_WhenPreparedAndOwned_ThenGenerationAndStorageAreBounded),
        new(nameof(GivenReusableGmiLease_WhenUpdated_ThenPayloadGenerationAndRasterStayCoupled),
            GivenReusableGmiLease_WhenUpdated_ThenPayloadGenerationAndRasterStayCoupled),
        new(nameof(GivenMaximumReusableGmiLease_WhenUpdatedRepeatedly_ThenSteadyStateAllocatesNothing),
            GivenMaximumReusableGmiLease_WhenUpdatedRepeatedly_ThenSteadyStateAllocatesNothing),
    ];

    private static void GivenReusableRasterLease_WhenPreparedAndOwned_ThenGenerationAndStorageAreBounded()
    {
        // Given: One 2-square reusable BGRA lease and a cached producer fill callback.
        var lease = new ReusableRasterFrameLease(2, 2);
        RasterPixelWriter fill = pixels => pixels.Fill(0xA5);

        // When: Generation 1 is prepared and scheduler ownership is acquired.
        lease.Prepare(1, fill);
        ContractAssert.Equal(true, lease.TryAcquireSchedulerOwnership());

        // Then: Shape, pixels, and the captured generation are inseparable while owned.
        ContractAssert.Equal(
            typeof(ReadOnlySpan<byte>),
            typeof(IRasterFrameLease).GetProperty(nameof(IRasterFrameLease.BgraPixels))!.PropertyType);
        ContractAssert.Equal(
            false,
            typeof(ReusableRasterFrameLease)
                .GetProperties()
                .Any(property => property.PropertyType == typeof(byte[])));
        ContractAssert.Equal(2, lease.Width);
        ContractAssert.Equal(2, lease.Height);
        ContractAssert.Equal(16, lease.BgraPixels.Length);
        ContractAssert.Equal(true, lease.BgraPixels.ToArray().All(value => value == 0xA5));
        ContractAssert.Equal(1L, lease.Generation);
        ContractAssert.Equal(1L, lease.SchedulerGeneration);
        ContractAssert.Throws<RasterFrameLeaseValidationException>(
            () => lease.Prepare(2, fill),
            "Raster frame cannot be prepared while producer or scheduler ownership is active.");
        lease.ReleaseSchedulerOwnership();
        ContractAssert.Throws<RasterFrameLeaseValidationException>(
            () => lease.Prepare(1, fill),
            "Raster frame generation must increase; previous: 1; actual: 1.");

        RasterPixelWriter failAfterMutation = pixels =>
        {
            pixels[0] = 0x00;
            throw new InvalidOperationException("writer failure");
        };
        ContractAssert.Throws<InvalidOperationException>(
            () => lease.Prepare(2, failAfterMutation),
            "writer failure");
        ContractAssert.Equal(false, lease.TryAcquireSchedulerOwnership());
        lease.Prepare(2, fill);
        ContractAssert.Equal(true, lease.TryAcquireSchedulerOwnership());
        lease.ReleaseSchedulerOwnership();
    }

    private static void GivenReusableGmiLease_WhenUpdated_ThenPayloadGenerationAndRasterStayCoupled()
    {
        // Given: One reusable selected-channel lease and two different sampled payloads.
        var lease = new ReusableGmiRasterFrameLease(4, 4);
        var first = new ushort[] { 0, 3, 1, 1, 2, 2, 0, 3 };
        var second = new ushort[] { 3, 0, 2, 2, 1, 1, 3, 0 };
        var range = new GmiDisplayRange(0, 3);

        // When: The first payload is sealed as generation 4 and scheduler-owned.
        lease.Prepare(4, GmiChannel.FsGmi, 1, 8, first, range);
        var firstRaster = lease.BgraPixels.ToArray();
        ContractAssert.Equal(true, lease.TryAcquireSchedulerOwnership());

        // Then: Ownership captures that payload generation and rejects mutation.
        ContractAssert.Equal(4L, lease.Generation);
        ContractAssert.Equal(4L, lease.SchedulerGeneration);
        ContractAssert.Throws<RasterFrameLeaseValidationException>(
            () => lease.Prepare(5, GmiChannel.FsGmi, 1, 8, second, range),
            "Raster frame cannot be prepared while producer or scheduler ownership is active.");
        lease.ReleaseSchedulerOwnership();
        ContractAssert.Throws<RasterFrameLeaseValidationException>(
            () => lease.Prepare(3, GmiChannel.FsGmi, 1, 8, second, range),
            "Raster frame generation must increase; previous: 4; actual: 3.");

        ContractAssert.Throws<GmiRasterValidationException>(
            () => lease.Prepare(
                5,
                GmiChannel.FsGmi,
                1,
                8,
                second,
                new GmiDisplayRange(1, 1)),
            "GMI display range maximum must be greater than minimum; minimum: 1; maximum: 1.");
        ContractAssert.Equal(false, lease.TryAcquireSchedulerOwnership());
        lease.Prepare(5, GmiChannel.FsGmi, 1, 8, second, range);
        ContractAssert.Equal(5L, lease.Generation);
        ContractAssert.SequenceNotEqual(firstRaster, lease.BgraPixels.ToArray());
    }

    private static void GivenMaximumReusableGmiLease_WhenUpdatedRepeatedly_ThenSteadyStateAllocatesNothing()
    {
        // Given: Maximum sampled input and one warmed reusable 512-square GMI lease.
        var source = new ushort[100 * 2_400];
        var lease = new ReusableGmiRasterFrameLease(512, 512);
        var range = new GmiDisplayRange(0, 16_383);
        SyntheticGmiSnapshotFactory.Fill(
            source, GmiChannel.FsGmi, 100, 2_400, seed: 0x6B28);
        lease.Prepare(1, GmiChannel.FsGmi, 100, 2_400, source, range);
        var allocatedBefore = GC.GetAllocatedBytesForCurrentThread();

        // When: Ten changed maximum updates reuse every source/scratch/pixel buffer.
        for (var generation = 2; generation <= 11; generation++)
        {
            SyntheticGmiSnapshotFactory.Fill(
                source, GmiChannel.FsGmi, 100, 2_400, seed: 0x6B28 + generation);
            lease.Prepare(generation, GmiChannel.FsGmi, 100, 2_400, source, range);
        }

        // Then: The production-shaped snapshot-to-raster update path allocates nothing.
        ContractAssert.Equal(0L, GC.GetAllocatedBytesForCurrentThread() - allocatedBefore);
    }
}
