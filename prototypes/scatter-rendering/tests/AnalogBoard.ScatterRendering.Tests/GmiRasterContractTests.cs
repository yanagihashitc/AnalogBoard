using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;
using AnalogBoard.ScatterRendering.Core;

namespace AnalogBoard.ScatterRendering.Tests;

internal static class GmiRasterContractTests
{
    public static IReadOnlyList<ContractTestCase> Cases { get; } =
    [
        new(nameof(GivenIndependentGmiOracle_WhenRasterized_ThenLayoutOrientationCoverageAndHashMatch),
            GivenIndependentGmiOracle_WhenRasterized_ThenLayoutOrientationCoverageAndHashMatch),
        new(nameof(GivenEmptyAndMaximumSnapshots_WhenRasterizedTwice_ThenBuffersRemainBoundedAndDeterministic),
            GivenEmptyAndMaximumSnapshots_WhenRasterizedTwice_ThenBuffersRemainBoundedAndDeterministic),
        new(nameof(GivenInvalidRangeShapeOrAliasing_WhenRasterized_ThenTypedFailureLeavesBuffersUnchanged),
            GivenInvalidRangeShapeOrAliasing_WhenRasterized_ThenTypedFailureLeavesBuffersUnchanged),
        new(nameof(GivenPreallocatedMaximumSnapshot_WhenRasterizedRepeatedly_ThenSteadyStateAllocatesNothing),
            GivenPreallocatedMaximumSnapshot_WhenRasterizedRepeatedly_ThenSteadyStateAllocatesNothing),
    ];

    private static void GivenIndependentGmiOracle_WhenRasterized_ThenLayoutOrientationCoverageAndHashMatch()
    {
        // Given: A tracked, independently calculated two-waveform diagonal oracle.
        var fixtureBytes = LoadFixtureBytes();
        var fixture = DeserializeFixture(fixtureBytes);
        var snapshot = GmiSnapshot.Create(
            GmiChannelSchema.Version,
            generation: 1,
            GmiChannel.FsGmi,
            fixture.Oracle.WaveformCount,
            fixture.Oracle.SamplesPerWaveform,
            fixture.Oracle.Values);
        var coverage = new int[fixture.Oracle.Width * fixture.Oracle.Height];
        var pixels = new byte[coverage.Length * DensityRasterizer.BytesPerPixel];

        // When: Rasterizing through the prototype-local display range and reusable buffers.
        GmiOverlayRasterizer.Rasterize(
            snapshot,
            new GmiDisplayRange(fixture.Oracle.DisplayMinimum, fixture.Oracle.DisplayMaximum),
            fixture.Oracle.Width,
            fixture.Oracle.Height,
            coverage,
            pixels);

        // Then: Full fixture identity, waveform/sample direction, Y orientation, and bytes match.
        ContractAssert.Equal("AB-GMI-RASTER-v1", fixture.ContractId);
        ContractAssert.Equal(
            "874fe6b9ea252f7063200655d584e549b5a2fc6e3587693b1b23b5041a52aa08",
            Sha256(fixtureBytes));
        ContractAssert.Equal(
            "P0-R1 prototype display only; not a storage or downstream contract",
            fixture.Scope);
        ContractAssert.Equal("waveform-major [waveform,sample]", fixture.Layout);
        ContractAssert.SequenceEqual(GmiChannelSchema.Names, fixture.SelectedChannelOrder);
        ContractAssert.Equal("sample index increases left to right", fixture.XOrientation);
        ContractAssert.Equal(
            "higher display value maps upward through one density-raster Y flip",
            fixture.YOrientation);
        ContractAssert.Equal(
            "each output column preserves the minimum and maximum over its proportional source-sample interval; duplicate extrema count once; upsampling uses the nearest source sample",
            fixture.HorizontalSampling);
        ContractAssert.Equal(
            "one or two extrema points per waveform per output column",
            fixture.Coverage);
        ContractAssert.Equal(
            "c6015c919cc79e7c746f4b6c0d8b42672e4165cfc0253663bbfe89e04121cc33",
            fixture.DensityRasterContractSha256);
        ContractAssert.Equal(2, fixture.Oracle.ExpectedSourceCoverage.Max());
        ContractAssert.SequenceEqual(fixture.Oracle.ExpectedSourceCoverage, coverage);
        ContractAssert.SequenceEqual(Convert.FromHexString(fixture.Oracle.ExpectedBgraHex), pixels);
        ContractAssert.Equal(fixture.Oracle.ExpectedBgraSha256, Sha256(pixels));

        var upsampled = GmiSnapshot.Create(
            GmiChannelSchema.Version,
            generation: 2,
            GmiChannel.FsGmi,
            fixture.UpsampleOracle.WaveformCount,
            fixture.UpsampleOracle.SamplesPerWaveform,
            fixture.UpsampleOracle.Values);
        var upsampledCoverage = new int[
            fixture.UpsampleOracle.Width * fixture.UpsampleOracle.Height];
        GmiOverlayRasterizer.Rasterize(
            upsampled,
            new GmiDisplayRange(
                fixture.UpsampleOracle.DisplayMinimum,
                fixture.UpsampleOracle.DisplayMaximum),
            fixture.UpsampleOracle.Width,
            fixture.UpsampleOracle.Height,
            upsampledCoverage,
            new byte[upsampledCoverage.Length * DensityRasterizer.BytesPerPixel]);
        ContractAssert.SequenceEqual(
            fixture.UpsampleOracle.ExpectedSourceCoverage,
            upsampledCoverage);
    }

    private static void GivenEmptyAndMaximumSnapshots_WhenRasterizedTwice_ThenBuffersRemainBoundedAndDeterministic()
    {
        // Given: Empty and maximum 100-by-2400 selected-channel snapshots at 512 square.
        const int size = 512;
        var empty = SyntheticGmiSnapshotFactory.Create(
            GmiChannel.FsGmi,
            waveformCount: 0,
            samplesPerWaveform: 2_400,
            seed: 17,
            generation: 1);
        var maximum = SyntheticGmiSnapshotFactory.Create(
            GmiChannel.BfGmi,
            waveformCount: 100,
            samplesPerWaveform: 2_400,
            seed: 17,
            generation: 2);
        var coverage = new int[size * size];
        var pixels = new byte[coverage.Length * DensityRasterizer.BytesPerPixel];

        // When: Rasterizing empty, maximum, then the maximum again into the same arrays.
        GmiOverlayRasterizer.Rasterize(
            empty,
            new GmiDisplayRange(0, 16_383),
            size,
            size,
            coverage,
            pixels);
        var emptyHash = Sha256(pixels);
        GmiOverlayRasterizer.Rasterize(
            maximum,
            new GmiDisplayRange(0, 16_383),
            size,
            size,
            coverage,
            pixels);
        var firstMaximum = pixels.ToArray();
        var firstCoverage = coverage.ToArray();
        GmiOverlayRasterizer.Rasterize(
            maximum,
            new GmiDisplayRange(0, 16_383),
            size,
            size,
            coverage,
            pixels);

        // Then: Empty is background, maximum is nonempty, bounded, and byte-identical.
        ContractAssert.Equal(
            "4e256ce791cdeb9fe2cf1f8e7b1b46fba9ed073cb5faaf822236e129104add99",
            emptyHash);
        ContractAssert.Equal(true, firstCoverage.Max() > 0);
        ContractAssert.Equal(true, firstCoverage.Max() <= GmiSnapshot.MaximumWaveformCount * 2);
        ContractAssert.SequenceEqual(firstCoverage, coverage);
        ContractAssert.SequenceEqual(firstMaximum, pixels);
    }

    private static void GivenInvalidRangeShapeOrAliasing_WhenRasterized_ThenTypedFailureLeavesBuffersUnchanged()
    {
        // Given: One valid 4-square snapshot plus sentinel scratch/pixel buffers.
        var snapshot = SyntheticGmiSnapshotFactory.Create(
            GmiChannel.FsGmi,
            waveformCount: 1,
            samplesPerWaveform: 4,
            seed: 1,
            generation: 1);
        var coverage = Enumerable.Repeat(77, 16).ToArray();
        var pixels = Enumerable.Repeat((byte)0xCC, 64).ToArray();

        // When/Then: Non-finite/reversed ranges and shape errors fail before mutation.
        AssertFailure(
            () => GmiOverlayRasterizer.Rasterize(
                snapshot,
                new GmiDisplayRange(double.NaN, 1),
                4,
                4,
                coverage,
                pixels),
            "GMI display range values must be finite; minimum: NaN; maximum: 1.",
            coverage,
            pixels);
        AssertFailure(
            () => GmiOverlayRasterizer.Rasterize(
                snapshot,
                new GmiDisplayRange(-double.MaxValue, double.MaxValue),
                4,
                4,
                coverage,
                pixels),
            "GMI display range width must be finite and positive; minimum: -1.7976931348623157E+308; maximum: 1.7976931348623157E+308.",
            coverage,
            pixels);
        AssertFailure(
            () => GmiOverlayRasterizer.Rasterize(
                snapshot,
                new GmiDisplayRange(1, 1),
                4,
                4,
                coverage,
                pixels),
            "GMI display range maximum must be greater than minimum; minimum: 1; maximum: 1.",
            coverage,
            pixels);
        AssertFailure(
            () => GmiOverlayRasterizer.Rasterize(
                snapshot,
                new GmiDisplayRange(0, 16_383),
                4,
                4,
                new int[15],
                pixels),
            "GMI coverage length must equal width * height; expected: 16; actual: 15.");
        AssertFailure(
            () => GmiOverlayRasterizer.Rasterize(
                snapshot,
                new GmiDisplayRange(0, 16_383),
                4,
                4,
                coverage,
                new byte[63]),
            "GMI BGRA pixel length must equal width * height * 4; expected: 64; actual: 63.");
        AssertFailure(
            () => GmiOverlayRasterizer.Rasterize(
                snapshot,
                new GmiDisplayRange(0, 16_383),
                0,
                4,
                coverage,
                pixels),
            "GMI raster width must be between 1 and 1024 inclusive; actual: 0.",
            coverage,
            pixels);

        var overlappingStorage = new byte[64];
        var overlappingCoverage = MemoryMarshal.Cast<byte, int>(overlappingStorage.AsSpan());
        ContractAssert.Throws<GmiRasterValidationException>(
            () => RasterizeOverlapping(snapshot, overlappingStorage),
            "GMI coverage and BGRA pixels must not overlap.");
        ContractAssert.SequenceEqual(new int[16], overlappingCoverage.ToArray());

        // Snapshot storage cannot alias either output, even through ReadOnlyMemory recovery.
        var coverageAliasSnapshot = GmiSnapshot.Create(
            1, 1, GmiChannel.FsGmi, 1, 8, new ushort[] { 0, 1, 2, 3, 3, 2, 1, 0 });
        var coverageAliasBefore = coverageAliasSnapshot.WaveformValues.ToArray();
        ContractAssert.Throws<GmiRasterValidationException>(
            () => RasterizeWithSnapshotCoverage(coverageAliasSnapshot),
            "GMI snapshot values must not overlap coverage or BGRA output storage.");
        ContractAssert.SequenceEqual(coverageAliasBefore, coverageAliasSnapshot.WaveformValues.ToArray());

        var pixelAliasSnapshot = GmiSnapshot.Create(
            1, 1, GmiChannel.FsGmi, 1, 32, Enumerable.Range(0, 32).Select(i => (ushort)i).ToArray());
        var pixelAliasBefore = pixelAliasSnapshot.WaveformValues.ToArray();
        ContractAssert.Throws<GmiRasterValidationException>(
            () => RasterizeWithSnapshotPixels(pixelAliasSnapshot),
            "GMI snapshot values must not overlap coverage or BGRA output storage.");
        ContractAssert.SequenceEqual(pixelAliasBefore, pixelAliasSnapshot.WaveformValues.ToArray());
    }

    private static void GivenPreallocatedMaximumSnapshot_WhenRasterizedRepeatedly_ThenSteadyStateAllocatesNothing()
    {
        // Given: One maximum snapshot and one warmed 512-square coverage/pixel pair.
        var snapshot = SyntheticGmiSnapshotFactory.Create(
            GmiChannel.DGmi,
            waveformCount: 100,
            samplesPerWaveform: 2_400,
            seed: 0x6D49,
            generation: 1);
        var coverage = new int[512 * 512];
        var pixels = new byte[coverage.Length * DensityRasterizer.BytesPerPixel];
        var range = new GmiDisplayRange(0, 16_383);
        GmiOverlayRasterizer.Rasterize(snapshot, range, 512, 512, coverage, pixels);
        var allocatedBefore = GC.GetAllocatedBytesForCurrentThread();

        // When: Reusing the same maximum snapshot and buffers for ten frames.
        for (var iteration = 0; iteration < 10; iteration++)
        {
            GmiOverlayRasterizer.Rasterize(snapshot, range, 512, 512, coverage, pixels);
        }

        // Then: The steady-state GMI raster path allocates no managed bytes.
        ContractAssert.Equal(0L, GC.GetAllocatedBytesForCurrentThread() - allocatedBefore);
    }

    private static void RasterizeOverlapping(GmiSnapshot snapshot, byte[] storage)
    {
        Span<byte> pixels = storage;
        var coverage = MemoryMarshal.Cast<byte, int>(pixels);
        GmiOverlayRasterizer.Rasterize(
            snapshot,
            new GmiDisplayRange(0, 16_383),
            4,
            4,
            coverage,
            pixels);
    }

    private static void RasterizeWithSnapshotCoverage(GmiSnapshot snapshot)
    {
        ContractAssert.Equal(
            true,
            MemoryMarshal.TryGetArray(snapshot.WaveformValues, out ArraySegment<ushort> values));
        var coverage = MemoryMarshal.Cast<ushort, int>(values.AsSpan());
        GmiOverlayRasterizer.Rasterize(
            snapshot,
            new GmiDisplayRange(0, 16_383),
            2,
            2,
            coverage,
            new byte[16]);
    }

    private static void RasterizeWithSnapshotPixels(GmiSnapshot snapshot)
    {
        ContractAssert.Equal(
            true,
            MemoryMarshal.TryGetArray(snapshot.WaveformValues, out ArraySegment<ushort> values));
        var pixels = MemoryMarshal.AsBytes(values.AsSpan());
        GmiOverlayRasterizer.Rasterize(
            snapshot,
            new GmiDisplayRange(0, 16_383),
            4,
            4,
            new int[16],
            pixels);
    }

    private static void AssertFailure(
        Action action,
        string expectedMessage,
        int[]? coverage = null,
        byte[]? pixels = null)
    {
        var coverageBefore = coverage?.ToArray();
        var pixelsBefore = pixels?.ToArray();
        ContractAssert.Throws<GmiRasterValidationException>(action, expectedMessage);
        if (coverageBefore is not null && coverage is not null)
        {
            ContractAssert.SequenceEqual(coverageBefore, coverage);
        }
        if (pixelsBefore is not null && pixels is not null)
        {
            ContractAssert.SequenceEqual(pixelsBefore, pixels);
        }
    }

    private static byte[] LoadFixtureBytes() =>
        File.ReadAllBytes(Path.Combine(
            AppContext.BaseDirectory,
            "Fixtures",
            "gmi-raster-contract-v1.json"));

    private static GmiRasterFixture DeserializeFixture(byte[] bytes) =>
        JsonSerializer.Deserialize<GmiRasterFixture>(
            bytes,
            new JsonSerializerOptions
            {
                UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
            })
        ?? throw new InvalidOperationException("GMI raster fixture must deserialize.");

    private static string Sha256(ReadOnlySpan<byte> value) =>
        Convert.ToHexString(SHA256.HashData(value)).ToLowerInvariant();

    private sealed record GmiRasterFixture(
        [property: JsonPropertyName("contract_id")] string ContractId,
        [property: JsonPropertyName("scope")] string Scope,
        [property: JsonPropertyName("layout")] string Layout,
        [property: JsonPropertyName("selected_channel_order")] string[] SelectedChannelOrder,
        [property: JsonPropertyName("x_orientation")] string XOrientation,
        [property: JsonPropertyName("y_orientation")] string YOrientation,
        [property: JsonPropertyName("horizontal_sampling")] string HorizontalSampling,
        [property: JsonPropertyName("coverage")] string Coverage,
        [property: JsonPropertyName("density_raster_contract_sha256")] string DensityRasterContractSha256,
        [property: JsonPropertyName("oracle")] GmiRasterOracle Oracle,
        [property: JsonPropertyName("upsample_oracle")] GmiUpsampleOracle UpsampleOracle);

    private sealed record GmiRasterOracle(
        [property: JsonPropertyName("waveform_count")] int WaveformCount,
        [property: JsonPropertyName("samples_per_waveform")] int SamplesPerWaveform,
        [property: JsonPropertyName("values")] ushort[] Values,
        [property: JsonPropertyName("display_minimum")] double DisplayMinimum,
        [property: JsonPropertyName("display_maximum")] double DisplayMaximum,
        [property: JsonPropertyName("width")] int Width,
        [property: JsonPropertyName("height")] int Height,
        [property: JsonPropertyName("expected_source_coverage")] int[] ExpectedSourceCoverage,
        [property: JsonPropertyName("expected_bgra_hex")] string ExpectedBgraHex,
        [property: JsonPropertyName("expected_bgra_sha256")] string ExpectedBgraSha256);

    private sealed record GmiUpsampleOracle(
        [property: JsonPropertyName("waveform_count")] int WaveformCount,
        [property: JsonPropertyName("samples_per_waveform")] int SamplesPerWaveform,
        [property: JsonPropertyName("values")] ushort[] Values,
        [property: JsonPropertyName("display_minimum")] double DisplayMinimum,
        [property: JsonPropertyName("display_maximum")] double DisplayMaximum,
        [property: JsonPropertyName("width")] int Width,
        [property: JsonPropertyName("height")] int Height,
        [property: JsonPropertyName("expected_source_coverage")] int[] ExpectedSourceCoverage);
}
