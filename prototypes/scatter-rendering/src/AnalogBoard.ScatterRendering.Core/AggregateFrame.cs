namespace AnalogBoard.ScatterRendering.Core;

public sealed class AggregateFrame
{
    public const int MaximumEventCount = 131_072;

    private AggregateFrame(
        int featureSchemaVersion,
        long generation,
        int eventCount,
        IReadOnlyList<string> pulseFeatureColumns,
        double[] pulseFeatureValues)
    {
        FeatureSchemaVersion = featureSchemaVersion;
        Generation = generation;
        EventCount = eventCount;
        PulseFeatureColumns = pulseFeatureColumns;
        PulseFeatureValues = pulseFeatureValues;
    }

    public int FeatureSchemaVersion { get; }

    public long Generation { get; }

    public int EventCount { get; }

    public IReadOnlyList<string> PulseFeatureColumns { get; }

    public ReadOnlyMemory<double> PulseFeatureValues { get; }

    public static AggregateFrame Create(
        int featureSchemaVersion,
        long generation,
        int eventCount,
        IReadOnlyList<string>? pulseFeatureColumns,
        double[]? pulseFeatureValues)
    {
        var validatedValues = ValidateInputs(
            featureSchemaVersion,
            generation,
            eventCount,
            pulseFeatureColumns,
            pulseFeatureValues);

        return CreateValidated(
            featureSchemaVersion,
            generation,
            eventCount,
            (double[])validatedValues.Clone());
    }

    /// <summary>
    /// Validates the frame contract and transfers ownership of the supplied buffer.
    /// </summary>
    internal static AggregateFrame CreateFromOwnedBuffer(
        int featureSchemaVersion,
        long generation,
        int eventCount,
        IReadOnlyList<string>? pulseFeatureColumns,
        double[]? ownedPulseFeatureValues)
    {
        var validatedValues = ValidateInputs(
            featureSchemaVersion,
            generation,
            eventCount,
            pulseFeatureColumns,
            ownedPulseFeatureValues);

        return CreateValidated(
            featureSchemaVersion,
            generation,
            eventCount,
            validatedValues);
    }

    private static double[] ValidateInputs(
        int featureSchemaVersion,
        long generation,
        int eventCount,
        IReadOnlyList<string>? pulseFeatureColumns,
        double[]? pulseFeatureValues)
    {
        ValidateEventCount(eventCount);

        if (pulseFeatureValues is null)
        {
            throw new AggregateFrameValidationException(
                "Pulse feature values must not be null.");
        }

        if (pulseFeatureColumns is null)
        {
            throw new AggregateFrameValidationException(
                "Pulse feature columns must not be null.");
        }

        if (featureSchemaVersion != PulseFeatureSchema.Version)
        {
            throw new AggregateFrameValidationException(
                $"Feature schema version must be {PulseFeatureSchema.Version}; actual: {featureSchemaVersion}.");
        }

        if (!PulseFeatureSchema.IsCanonicalOrder(pulseFeatureColumns))
        {
            throw new AggregateFrameValidationException(
                "Pulse feature columns must match feature schema version 1 in canonical order.");
        }

        var expectedValueCount = checked(eventCount * PulseFeatureSchema.ColumnCount);
        if (pulseFeatureValues.Length != expectedValueCount)
        {
            throw new AggregateFrameValidationException(
                $"Pulse feature value count must equal event_count * {PulseFeatureSchema.ColumnCount}; expected: {expectedValueCount}, actual: {pulseFeatureValues.Length}.");
        }

        if (generation <= 0)
        {
            throw new AggregateFrameValidationException(
                $"Generation must be greater than zero; actual: {generation}.");
        }

        return pulseFeatureValues;
    }

    private static AggregateFrame CreateValidated(
        int featureSchemaVersion,
        long generation,
        int eventCount,
        double[] ownedPulseFeatureValues)
    {
        return new AggregateFrame(
            featureSchemaVersion,
            generation,
            eventCount,
            PulseFeatureSchema.Columns,
            ownedPulseFeatureValues);
    }

    internal static void ValidateEventCount(int eventCount)
    {
        if (eventCount < 0 || eventCount > MaximumEventCount)
        {
            throw new AggregateFrameValidationException(
                $"Event count must be between 0 and {MaximumEventCount} inclusive; actual: {eventCount}.");
        }
    }
}
