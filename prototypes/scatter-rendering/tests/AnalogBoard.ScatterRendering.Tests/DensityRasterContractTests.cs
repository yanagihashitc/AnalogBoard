using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;
using AnalogBoard.ScatterRendering.Core;

namespace AnalogBoard.ScatterRendering.Tests;

internal static class DensityRasterContractTests
{
    public static IReadOnlyList<ContractTestCase> Cases { get; } =
    [
        new(nameof(GivenIndependentOracle_WhenRasterizedTwice_ThenPaletteOrientationAndHashMatch),
            GivenIndependentOracle_WhenRasterizedTwice_ThenPaletteOrientationAndHashMatch),
        new(nameof(GivenBoundaryRasters_WhenRasterized_ThenEveryPixelIsDeterministicBgra32),
            GivenBoundaryRasters_WhenRasterized_ThenEveryPixelIsDeterministicBgra32),
        new(nameof(GivenInvalidRasterInput_WhenRasterized_ThenTypedFailureLeavesDestinationUnchanged),
            GivenInvalidRasterInput_WhenRasterized_ThenTypedFailureLeavesDestinationUnchanged),
        new(nameof(GivenReusableDestination_WhenRasterizedRepeatedly_ThenSteadyStateAllocatesNothing),
            GivenReusableDestination_WhenRasterizedRepeatedly_ThenSteadyStateAllocatesNothing),
    ];

    private static void GivenIndependentOracle_WhenRasterizedTwice_ThenPaletteOrientationAndHashMatch()
    {
        // Given: An independently calculated asymmetric raster oracle and a reused pixel buffer.
        var fixtureBytes = LoadFixtureBytes();
        var fixture = DeserializeFixture(fixtureBytes);
        var expected = Convert.FromHexString(fixture.Oracle.ExpectedBgraHex);
        var pixels = new byte[expected.Length];

        // When: Rasterizing twice after distinct pre-fills.
        Array.Fill(pixels, (byte)0xA5);
        DensityRasterizer.Rasterize(
            fixture.Oracle.SourceCounts,
            fixture.Oracle.Width,
            fixture.Oracle.Height,
            fixture.Oracle.MaximumCount,
            pixels);
        var first = pixels.ToArray();
        Array.Fill(pixels, (byte)0x5A);
        DensityRasterizer.Rasterize(
            fixture.Oracle.SourceCounts,
            fixture.Oracle.Width,
            fixture.Oracle.Height,
            fixture.Oracle.MaximumCount,
            pixels);

        // Then: BGRA bytes, the single Y flip, full overwrite, and hash all match authority.
        ContractAssert.Equal("AB-DENSITY-RASTER-v1", fixture.ContractId);
        ContractAssert.Equal(
            "c6015c919cc79e7c746f4b6c0d8b42672e4165cfc0253663bbfe89e04121cc33",
            Sha256(fixtureBytes));
        ContractAssert.Equal("Bgra32", fixture.PixelFormat);
        ContractAssert.Equal(255, fixture.Alpha);
        ContractAssert.Equal(
            "linear integer interpolation against declared maximum",
            fixture.Normalization);
        ContractAssert.Equal("#FF0D1117", fixture.BackgroundArgb);
        ContractAssert.SequenceEqual(
            new[] { "#FF440154", "#FF3B528B", "#FF21918C", "#FF5EC962", "#FFFDE725" },
            fixture.PaletteArgb);
        ContractAssert.Equal("row-major y-min-first", fixture.Orientation.Source);
        ContractAssert.Equal("top-down", fixture.Orientation.Destination);
        ContractAssert.Equal("destination_y = height - 1 - source_y", fixture.Orientation.Mapping);
        ContractAssert.SequenceEqual(expected, first);
        ContractAssert.SequenceEqual(expected, pixels);
        ContractAssert.Equal(fixture.Oracle.ExpectedBgraSha256, Sha256(pixels));
    }

    private static void GivenBoundaryRasters_WhenRasterized_ThenEveryPixelIsDeterministicBgra32()
    {
        // Given: Minimum, hard, and headroom dimensions plus the largest legal count.
        var fixture = DeserializeFixture(LoadFixtureBytes());
        var oneZero = new byte[4];
        var oneMaximum = new byte[4];
        var extremeCounts = new[] { 1, int.MaxValue };
        var extremePixels = new byte[8];
        var hardCounts = new int[512 * 512];
        var hardPixels = new byte[512 * 512 * DensityRasterizer.BytesPerPixel];
        var headroomCounts = new int[1024 * 1024];
        var headroomPixels = new byte[1024 * 1024 * DensityRasterizer.BytesPerPixel];

        // When: Rasterizing every boundary without changing the caller-owned arrays.
        DensityRasterizer.Rasterize(new[] { 0 }, 1, 1, 0, oneZero);
        DensityRasterizer.Rasterize(new[] { int.MaxValue }, 1, 1, int.MaxValue, oneMaximum);
        DensityRasterizer.Rasterize(extremeCounts, 2, 1, int.MaxValue, extremePixels);
        DensityRasterizer.Rasterize(hardCounts, 512, 512, 0, hardPixels);
        DensityRasterizer.Rasterize(headroomCounts, 1024, 1024, 0, headroomPixels);

        // Then: Background/final palette anchors and the independently pinned zero hashes match.
        ContractAssert.SequenceEqual(new byte[] { 0x17, 0x11, 0x0D, 0xFF }, oneZero);
        ContractAssert.SequenceEqual(new byte[] { 0x25, 0xE7, 0xFD, 0xFF }, oneMaximum);
        ContractAssert.SequenceEqual(
            new byte[] { 0x54, 0x01, 0x44, 0xFF, 0x25, 0xE7, 0xFD, 0xFF },
            extremePixels);
        ContractAssert.Equal(
            fixture.ZeroRasterSha256.Size512,
            Sha256(hardPixels));
        ContractAssert.Equal(
            fixture.ZeroRasterSha256.Size1024,
            Sha256(headroomPixels));
        ContractAssert.Equal(true, hardPixels.Where((_, index) => index % 4 == 3).All(alpha => alpha == 255));
        ContractAssert.Equal(true, headroomPixels.Where((_, index) => index % 4 == 3).All(alpha => alpha == 255));
    }

    private static void GivenInvalidRasterInput_WhenRasterized_ThenTypedFailureLeavesDestinationUnchanged()
    {
        // Given: A sentinel destination and every bounded shape/count failure category.
        var destination = Enumerable.Repeat((byte)0xCC, 16).ToArray();

        // When/Then: Validation fails before any destination byte is changed.
        AssertFailure(
            () => DensityRasterizer.Rasterize(new[] { 0 }, 0, 1, 0, destination),
            "Raster width must be between 1 and 1024 inclusive; actual: 0.",
            destination);
        AssertFailure(
            () => DensityRasterizer.Rasterize(new[] { 0 }, 1, 1025, 0, destination),
            "Raster height must be between 1 and 1024 inclusive; actual: 1025.",
            destination);
        AssertFailure(
            () => DensityRasterizer.Rasterize(new[] { 0, 0, 0 }, 2, 2, 0, destination),
            "Density count length must equal width * height; expected: 4; actual: 3.",
            destination);
        AssertFailure(
            () => DensityRasterizer.Rasterize(new[] { 0, 0, 0, 0 }, 2, 2, 0, new byte[15]),
            "BGRA pixel length must equal width * height * 4; expected: 16; actual: 15.");
        AssertFailure(
            () => DensityRasterizer.Rasterize(new[] { 0, 0, 0, 0 }, 2, 2, -1, destination),
            "Declared maximum count must be non-negative; actual: -1.",
            destination);
        AssertFailure(
            () => DensityRasterizer.Rasterize(new[] { -1, 0, 0, 0 }, 2, 2, 0, destination),
            "Density counts must not contain negative values; index: 0; actual: -1.",
            destination);
        AssertFailure(
            () => DensityRasterizer.Rasterize(new[] { 2, 0, 0, 0 }, 2, 2, 1, destination),
            "Density count must not exceed declared maximum; index: 0; actual: 2; maximum: 1.",
            destination);
        AssertFailure(
            () => DensityRasterizer.Rasterize(new[] { 1, 0, 0, 0 }, 2, 2, 2, destination),
            "Declared maximum count must equal observed maximum; declared: 2; observed: 1.",
            destination);

        var overlappingStorage = new byte[16];
        InitializeOverlappingCounts(overlappingStorage);
        AssertFailure(
            () => RasterizeOverlapping(overlappingStorage),
            "Density counts and BGRA pixels must not overlap.",
            overlappingStorage);
    }

    private static void GivenReusableDestination_WhenRasterizedRepeatedly_ThenSteadyStateAllocatesNothing()
    {
        // Given: One preallocated 512-square count/pixel pair warmed through the rasterizer.
        var counts = new int[512 * 512];
        counts[0] = 1;
        counts[^1] = 8;
        var pixels = new byte[counts.Length * DensityRasterizer.BytesPerPixel];
        DensityRasterizer.Rasterize(counts, 512, 512, 8, pixels);
        var before = GC.GetAllocatedBytesForCurrentThread();

        // When: Reusing the same arrays for ten full rasterizations.
        for (var iteration = 0; iteration < 10; iteration++)
        {
            DensityRasterizer.Rasterize(counts, 512, 512, 8, pixels);
        }

        // Then: The steady-state raster path performs no managed allocation.
        var allocated = GC.GetAllocatedBytesForCurrentThread() - before;
        ContractAssert.Equal(0L, allocated);
    }

    private static byte[] LoadFixtureBytes()
    {
        var path = Path.Combine(
            AppContext.BaseDirectory,
            "Fixtures",
            "density-raster-contract-v1.json");
        return File.ReadAllBytes(path);
    }

    private static RasterFixture DeserializeFixture(byte[] bytes) =>
        JsonSerializer.Deserialize<RasterFixture>(
            bytes,
            new JsonSerializerOptions
            {
                UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
            })
            ?? throw new InvalidOperationException("Density raster fixture must deserialize.");

    private static string Sha256(ReadOnlySpan<byte> value) =>
        Convert.ToHexString(SHA256.HashData(value)).ToLowerInvariant();

    private static void InitializeOverlappingCounts(byte[] storage)
    {
        Span<byte> bytes = storage;
        var counts = MemoryMarshal.Cast<byte, int>(bytes);
        counts[0] = 0;
        counts[1] = 1;
        counts[2] = 2;
        counts[3] = 3;
    }

    private static void RasterizeOverlapping(byte[] storage)
    {
        Span<byte> bytes = storage;
        var counts = MemoryMarshal.Cast<byte, int>(bytes);
        DensityRasterizer.Rasterize(counts, 2, 2, 3, bytes);
    }

    private static void AssertFailure(
        Action action,
        string expectedMessage,
        byte[]? destination = null)
    {
        var before = destination?.ToArray();
        ContractAssert.Throws<DensityRasterValidationException>(action, expectedMessage);
        if (before is not null && destination is not null)
        {
            ContractAssert.SequenceEqual(before, destination);
        }
    }

    private sealed record RasterFixture(
        [property: JsonPropertyName("contract_id")] string ContractId,
        [property: JsonPropertyName("pixel_format")] string PixelFormat,
        [property: JsonPropertyName("alpha")] int Alpha,
        [property: JsonPropertyName("orientation")] OrientationFixture Orientation,
        [property: JsonPropertyName("normalization")] string Normalization,
        [property: JsonPropertyName("background_argb")] string BackgroundArgb,
        [property: JsonPropertyName("palette_argb")] string[] PaletteArgb,
        [property: JsonPropertyName("oracle")] RasterOracleFixture Oracle,
        [property: JsonPropertyName("zero_raster_sha256")] ZeroRasterSha256Fixture ZeroRasterSha256);

    private sealed record OrientationFixture(
        [property: JsonPropertyName("source")] string Source,
        [property: JsonPropertyName("destination")] string Destination,
        [property: JsonPropertyName("mapping")] string Mapping);

    private sealed record ZeroRasterSha256Fixture(
        [property: JsonPropertyName("512x512")] string Size512,
        [property: JsonPropertyName("1024x1024")] string Size1024);

    private sealed record RasterOracleFixture(
        [property: JsonPropertyName("width")] int Width,
        [property: JsonPropertyName("height")] int Height,
        [property: JsonPropertyName("maximum_count")] int MaximumCount,
        [property: JsonPropertyName("source_counts")] int[] SourceCounts,
        [property: JsonPropertyName("expected_bgra_hex")] string ExpectedBgraHex,
        [property: JsonPropertyName("expected_bgra_sha256")] string ExpectedBgraSha256);
}
