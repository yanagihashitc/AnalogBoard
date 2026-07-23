using AnalogBoard.ScatterRendering.Core;

namespace AnalogBoard.ScatterRendering.Tests;

internal static class GmiSnapshotContractTests
{
    public static IReadOnlyList<ContractTestCase> Cases { get; } =
    [
        new(nameof(GivenCanonicalSelectedChannel_WhenSnapshotCreated_ThenShapeOrderAndOwnershipAreExact),
            GivenCanonicalSelectedChannel_WhenSnapshotCreated_ThenShapeOrderAndOwnershipAreExact),
        new(nameof(GivenWaveformAndSampleBoundaries_WhenSnapshotsCreated_ThenOnlyBoundedShapesAreAccepted),
            GivenWaveformAndSampleBoundaries_WhenSnapshotsCreated_ThenOnlyBoundedShapesAreAccepted),
        new(nameof(GivenInvalidSchemaChannelShapeOrGeneration_WhenCreated_ThenTypedFailure),
            GivenInvalidSchemaChannelShapeOrGeneration_WhenCreated_ThenTypedFailure),
        new(nameof(GivenSyntheticSelectedChannelSnapshots_WhenGenerated_ThenValuesAreFourteenBitAndDeterministic),
            GivenSyntheticSelectedChannelSnapshots_WhenGenerated_ThenValuesAreFourteenBitAndDeterministic),
    ];

    private static void GivenCanonicalSelectedChannel_WhenSnapshotCreated_ThenShapeOrderAndOwnershipAreExact()
    {
        // Given: Two waveform-major fsGMI traces with three raw ADC-linear samples each.
        var values = new ushort[] { 1, 2, 3, 11, 12, 13 };
        var expected = values.ToArray();

        // When: The public boundary creates one selected-channel snapshot and the source mutates.
        var snapshot = GmiSnapshot.Create(
            schemaVersion: GmiChannelSchema.Version,
            generation: 7,
            selectedChannel: GmiChannel.FsGmi,
            waveformCount: 2,
            samplesPerWaveform: 3,
            waveformValues: values);
        Array.Fill(values, (ushort)99);

        // Then: Canonical order, waveform-major shape, and cloned ownership remain exact.
        ContractAssert.Equal(1, GmiChannelSchema.Version);
        ContractAssert.SequenceEqual(
            new[] { "fsGMI", "ssGMI", "flGMI", "dGMI", "bfGMI" },
            GmiChannelSchema.Names);
        ContractAssert.Equal(7L, snapshot.Generation);
        ContractAssert.Equal(GmiChannel.FsGmi, snapshot.SelectedChannel);
        ContractAssert.Equal("fsGMI", snapshot.SelectedChannelName);
        ContractAssert.Equal(2, snapshot.WaveformCount);
        ContractAssert.Equal(3, snapshot.SamplesPerWaveform);
        ContractAssert.SequenceEqual(expected, snapshot.WaveformValues.ToArray());
    }

    private static void GivenWaveformAndSampleBoundaries_WhenSnapshotsCreated_ThenOnlyBoundedShapesAreAccepted()
    {
        // Given/When: The explicit 0/1/99/100 waveform and 2399/2400 sample boundaries.
        foreach (var waveformCount in new[] { 0, 1, 99, 100 })
        {
            var snapshot = CreateSnapshot(waveformCount, samplesPerWaveform: 2_400);
            ContractAssert.Equal(waveformCount, snapshot.WaveformCount);
        }
        foreach (var sampleCount in new[] { 1, 2_399, 2_400 })
        {
            var snapshot = CreateSnapshot(waveformCount: 1, sampleCount);
            ContractAssert.Equal(sampleCount, snapshot.SamplesPerWaveform);
        }

        // Then: One step outside either ceiling and the zero-sample shape fail loud.
        ContractAssert.Throws<GmiSnapshotValidationException>(
            () => CreateSnapshot(waveformCount: 101, samplesPerWaveform: 2_400),
            "GMI waveform count must be between 0 and 100 inclusive; actual: 101.");
        ContractAssert.Throws<GmiSnapshotValidationException>(
            () => CreateSnapshot(waveformCount: -1, samplesPerWaveform: 2_400),
            "GMI waveform count must be between 0 and 100 inclusive; actual: -1.");
        ContractAssert.Throws<GmiSnapshotValidationException>(
            () => CreateSnapshot(waveformCount: 1, samplesPerWaveform: 2_401),
            "GMI samples per waveform must be between 1 and 2400 inclusive; actual: 2401.");
        ContractAssert.Throws<GmiSnapshotValidationException>(
            () => CreateSnapshot(waveformCount: 1, samplesPerWaveform: 0),
            "GMI samples per waveform must be between 1 and 2400 inclusive; actual: 0.");
    }

    private static void GivenInvalidSchemaChannelShapeOrGeneration_WhenCreated_ThenTypedFailure()
    {
        // Given: Small buffers that isolate each invalid contract field.
        var values = new ushort[] { 1, 2 };

        // When/Then: Schema, channel, shape, generation, and missing storage fail exactly.
        ContractAssert.Throws<GmiSnapshotValidationException>(
            () => GmiSnapshot.Create(2, 1, GmiChannel.FsGmi, 1, 2, values),
            "GMI schema version must be 1; actual: 2.");
        ContractAssert.Throws<GmiSnapshotValidationException>(
            () => GmiSnapshot.Create(1, 1, (GmiChannel)5, 1, 2, values),
            "Selected GMI channel must be a canonical index from 0 to 4; actual: 5.");
        ContractAssert.Throws<GmiSnapshotValidationException>(
            () => GmiSnapshot.Create(1, 0, GmiChannel.FsGmi, 1, 2, values),
            "GMI generation must be greater than zero; actual: 0.");
        ContractAssert.Throws<GmiSnapshotValidationException>(
            () => GmiSnapshot.Create(1, 1, GmiChannel.FsGmi, 1, 2, new ushort[] { 1 }),
            "GMI waveform value count must equal waveform_count * samples_per_waveform; expected: 2; actual: 1.");
        ContractAssert.Throws<GmiSnapshotValidationException>(
            () => GmiSnapshot.Create(1, 1, GmiChannel.FsGmi, 1, 2, null),
            "GMI waveform values must not be null.");
        ContractAssert.Throws<GmiSnapshotValidationException>(
            () => GmiSnapshot.Create(1, 1, GmiChannel.FsGmi, 1, 2, new ushort[] { 1, 16_384 }),
            "GMI waveform values must be 14-bit ADC counts from 0 to 16383; index: 1; actual: 16384.");

        var adcBoundaries = GmiSnapshot.Create(
            1,
            1,
            GmiChannel.FsGmi,
            1,
            2,
            new ushort[] { 0, 16_383 });
        ContractAssert.SequenceEqual(new ushort[] { 0, 16_383 }, adcBoundaries.WaveformValues.ToArray());
    }

    private static void GivenSyntheticSelectedChannelSnapshots_WhenGenerated_ThenValuesAreFourteenBitAndDeterministic()
    {
        // Given/When: Same and different deterministic selected-channel fixture identities.
        var first = SyntheticGmiSnapshotFactory.Create(
            GmiChannel.FlGmi,
            waveformCount: 100,
            samplesPerWaveform: 2_400,
            seed: 0x6D49,
            generation: 1);
        var same = SyntheticGmiSnapshotFactory.Create(
            GmiChannel.FlGmi,
            waveformCount: 100,
            samplesPerWaveform: 2_400,
            seed: 0x6D49,
            generation: 1);
        var different = SyntheticGmiSnapshotFactory.Create(
            GmiChannel.BfGmi,
            waveformCount: 100,
            samplesPerWaveform: 2_400,
            seed: 0x6D49,
            generation: 1);

        // Then: Same identity is bit-stable, channel identity changes values, and all remain 14-bit.
        ContractAssert.SequenceEqual(first.WaveformValues.ToArray(), same.WaveformValues.ToArray());
        ContractAssert.SequenceNotEqual(first.WaveformValues.ToArray(), different.WaveformValues.ToArray());
        ContractAssert.Equal(true, first.WaveformValues.Span.ToArray().All(value => value <= 16_383));
        ContractAssert.Equal(240_000, first.WaveformValues.Length);

        // Invalid factory inputs fail with the snapshot contract before allocation or fill.
        ContractAssert.Throws<GmiSnapshotValidationException>(
            () => SyntheticGmiSnapshotFactory.Create(
                GmiChannel.FsGmi, 101, 2_400, seed: 1, generation: 1),
            "GMI waveform count must be between 0 and 100 inclusive; actual: 101.");
        ContractAssert.Throws<GmiSnapshotValidationException>(
            () => SyntheticGmiSnapshotFactory.Create(
                GmiChannel.FsGmi, 1, 2_401, seed: 1, generation: 1),
            "GMI samples per waveform must be between 1 and 2400 inclusive; actual: 2401.");
        ContractAssert.Throws<GmiSnapshotValidationException>(
            () => SyntheticGmiSnapshotFactory.Create(
                (GmiChannel)int.MaxValue, 1, 1, seed: 1, generation: 1),
            "Selected GMI channel must be a canonical index from 0 to 4; actual: 2147483647.");
        ContractAssert.Throws<GmiSnapshotValidationException>(
            () => SyntheticGmiSnapshotFactory.Create(
                GmiChannel.FsGmi, 1, 1, seed: 1, generation: 0),
            "GMI generation must be greater than zero; actual: 0.");
    }

    private static GmiSnapshot CreateSnapshot(int waveformCount, int samplesPerWaveform)
    {
        var valueCount = waveformCount < 0 || samplesPerWaveform < 0
            ? 0
            : checked(waveformCount * samplesPerWaveform);
        return GmiSnapshot.Create(
            GmiChannelSchema.Version,
            generation: 1,
            GmiChannel.FsGmi,
            waveformCount,
            samplesPerWaveform,
            new ushort[valueCount]);
    }
}
