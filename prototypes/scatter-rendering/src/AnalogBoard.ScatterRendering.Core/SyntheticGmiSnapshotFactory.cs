namespace AnalogBoard.ScatterRendering.Core;

public static class SyntheticGmiSnapshotFactory
{
    private const int TrianglePeriod = 480;
    private const int TriangleHalfPeriod = TrianglePeriod / 2;

    public static GmiSnapshot Create(
        GmiChannel selectedChannel,
        int waveformCount,
        int samplesPerWaveform,
        int seed,
        long generation)
    {
        GmiSnapshot.ValidateMetadata(
            GmiChannelSchema.Version,
            generation,
            selectedChannel,
            waveformCount,
            samplesPerWaveform);
        var values = new ushort[checked(waveformCount * samplesPerWaveform)];
        Fill(values, selectedChannel, waveformCount, samplesPerWaveform, seed);

        return GmiSnapshot.CreateFromOwnedBuffer(
            GmiChannelSchema.Version,
            generation,
            selectedChannel,
            waveformCount,
            samplesPerWaveform,
            values);
    }

    public static void Fill(
        Span<ushort> destination,
        GmiChannel selectedChannel,
        int waveformCount,
        int samplesPerWaveform,
        int seed)
    {
        GmiSnapshot.ValidateMetadata(
            GmiChannelSchema.Version,
            generation: 1,
            selectedChannel,
            waveformCount,
            samplesPerWaveform);
        var expectedValueCount = checked(waveformCount * samplesPerWaveform);
        if (destination.Length != expectedValueCount)
        {
            throw new GmiSnapshotValidationException(
                $"GMI waveform value count must equal waveform_count * samples_per_waveform; expected: {expectedValueCount}; actual: {destination.Length}.");
        }

        var unsignedSeed = unchecked((uint)seed);
        var channelIndex = (int)selectedChannel;

        for (var waveformIndex = 0; waveformIndex < waveformCount; waveformIndex++)
        {
            var waveformOffset = waveformIndex * samplesPerWaveform;
            var phase = (int)((unsignedSeed +
                ((uint)waveformIndex * 37U) +
                ((uint)channelIndex * 53U)) % TrianglePeriod);
            var baseline = 2_048 + (channelIndex * 1_024) + (waveformIndex * 3);
            for (var sampleIndex = 0; sampleIndex < samplesPerWaveform; sampleIndex++)
            {
                var position = (sampleIndex + phase) % TrianglePeriod;
                var triangle = position <= TriangleHalfPeriod
                    ? position
                    : TrianglePeriod - position;
                destination[waveformOffset + sampleIndex] = checked((ushort)(baseline + (triangle * 24)));
            }
        }
    }
}
