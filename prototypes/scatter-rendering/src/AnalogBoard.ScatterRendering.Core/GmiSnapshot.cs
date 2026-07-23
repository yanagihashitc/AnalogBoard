namespace AnalogBoard.ScatterRendering.Core;

public enum GmiChannel
{
    FsGmi = 0,
    SsGmi = 1,
    FlGmi = 2,
    DGmi = 3,
    BfGmi = 4,
}

public static class GmiChannelSchema
{
    private static readonly string[] OrderedNames =
    [
        "fsGMI",
        "ssGMI",
        "flGMI",
        "dGMI",
        "bfGMI",
    ];

    public const int Version = 1;

    public static IReadOnlyList<string> Names { get; } = Array.AsReadOnly(OrderedNames);

    public static string NameFor(GmiChannel channel)
    {
        if (!Enum.IsDefined(channel))
        {
            throw new GmiSnapshotValidationException(
                $"Selected GMI channel must be a canonical index from 0 to 4; actual: {(int)channel}.");
        }

        return OrderedNames[(int)channel];
    }
}

/// <summary>
/// Carries one bounded selected-channel waveform-major display snapshot.
/// This is sampled aggregate input, not an acquisition raw-stream boundary.
/// </summary>
public sealed class GmiSnapshot
{
    public const int MaximumWaveformCount = 100;
    public const int MaximumSamplesPerWaveform = 2_400;
    public const ushort MaximumAdcValue = 16_383;

    private GmiSnapshot(
        int schemaVersion,
        long generation,
        GmiChannel selectedChannel,
        int waveformCount,
        int samplesPerWaveform,
        ushort[] ownedWaveformValues)
    {
        SchemaVersion = schemaVersion;
        Generation = generation;
        SelectedChannel = selectedChannel;
        SelectedChannelName = GmiChannelSchema.NameFor(selectedChannel);
        WaveformCount = waveformCount;
        SamplesPerWaveform = samplesPerWaveform;
        WaveformValues = ownedWaveformValues;
    }

    public int SchemaVersion { get; }

    public long Generation { get; }

    public GmiChannel SelectedChannel { get; }

    public string SelectedChannelName { get; }

    public int WaveformCount { get; }

    public int SamplesPerWaveform { get; }

    public ReadOnlyMemory<ushort> WaveformValues { get; }

    public static GmiSnapshot Create(
        int schemaVersion,
        long generation,
        GmiChannel selectedChannel,
        int waveformCount,
        int samplesPerWaveform,
        ushort[]? waveformValues)
    {
        var validatedValues = ValidateInputs(
            schemaVersion,
            generation,
            selectedChannel,
            waveformCount,
            samplesPerWaveform,
            waveformValues);
        return CreateValidated(
            schemaVersion,
            generation,
            selectedChannel,
            waveformCount,
            samplesPerWaveform,
            (ushort[])validatedValues.Clone());
    }

    internal static GmiSnapshot CreateFromOwnedBuffer(
        int schemaVersion,
        long generation,
        GmiChannel selectedChannel,
        int waveformCount,
        int samplesPerWaveform,
        ushort[]? ownedWaveformValues)
    {
        var validatedValues = ValidateInputs(
            schemaVersion,
            generation,
            selectedChannel,
            waveformCount,
            samplesPerWaveform,
            ownedWaveformValues);
        return CreateValidated(
            schemaVersion,
            generation,
            selectedChannel,
            waveformCount,
            samplesPerWaveform,
            validatedValues);
    }

    private static ushort[] ValidateInputs(
        int schemaVersion,
        long generation,
        GmiChannel selectedChannel,
        int waveformCount,
        int samplesPerWaveform,
        ushort[]? waveformValues)
    {
        if (waveformValues is null)
        {
            throw new GmiSnapshotValidationException(
                "GMI waveform values must not be null.");
        }

        ValidateMetadata(
            schemaVersion,
            generation,
            selectedChannel,
            waveformCount,
            samplesPerWaveform);
        ValidateWaveformValues(waveformCount, samplesPerWaveform, waveformValues);
        return waveformValues;
    }

    internal static void ValidateMetadata(
        int schemaVersion,
        long generation,
        GmiChannel selectedChannel,
        int waveformCount,
        int samplesPerWaveform)
    {
        if (schemaVersion != GmiChannelSchema.Version)
        {
            throw new GmiSnapshotValidationException(
                $"GMI schema version must be {GmiChannelSchema.Version}; actual: {schemaVersion}.");
        }

        _ = GmiChannelSchema.NameFor(selectedChannel);

        if (generation <= 0)
        {
            throw new GmiSnapshotValidationException(
                $"GMI generation must be greater than zero; actual: {generation}.");
        }

        if (waveformCount < 0 || waveformCount > MaximumWaveformCount)
        {
            throw new GmiSnapshotValidationException(
                $"GMI waveform count must be between 0 and {MaximumWaveformCount} inclusive; actual: {waveformCount}.");
        }

        if (samplesPerWaveform < 1 || samplesPerWaveform > MaximumSamplesPerWaveform)
        {
            throw new GmiSnapshotValidationException(
                $"GMI samples per waveform must be between 1 and {MaximumSamplesPerWaveform} inclusive; actual: {samplesPerWaveform}.");
        }
    }

    internal static void ValidateWaveformValues(
        int waveformCount,
        int samplesPerWaveform,
        ReadOnlySpan<ushort> waveformValues)
    {
        var expectedValueCount = checked(waveformCount * samplesPerWaveform);
        if (waveformValues.Length != expectedValueCount)
        {
            throw new GmiSnapshotValidationException(
                $"GMI waveform value count must equal waveform_count * samples_per_waveform; expected: {expectedValueCount}; actual: {waveformValues.Length}.");
        }

        for (var index = 0; index < waveformValues.Length; index++)
        {
            var value = waveformValues[index];
            if (value > MaximumAdcValue)
            {
                throw new GmiSnapshotValidationException(
                    $"GMI waveform values must be 14-bit ADC counts from 0 to {MaximumAdcValue}; index: {index}; actual: {value}.");
            }
        }
    }

    private static GmiSnapshot CreateValidated(
        int schemaVersion,
        long generation,
        GmiChannel selectedChannel,
        int waveformCount,
        int samplesPerWaveform,
        ushort[] ownedWaveformValues) =>
        new(
            schemaVersion,
            generation,
            selectedChannel,
            waveformCount,
            samplesPerWaveform,
            ownedWaveformValues);
}

public sealed class GmiSnapshotValidationException : Exception
{
    public GmiSnapshotValidationException(string message)
        : base(message)
    {
    }
}
