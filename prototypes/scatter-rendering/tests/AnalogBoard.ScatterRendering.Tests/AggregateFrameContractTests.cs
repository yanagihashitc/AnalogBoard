using AnalogBoard.ScatterRendering.Core;

namespace AnalogBoard.ScatterRendering.Tests;

internal static class AggregateFrameContractTests
{
    private const int HardEventCount = 100_001;
    private const int HeadroomEventCount = 131_072;

    public static IReadOnlyList<ContractTestCase> Cases { get; } =
    [
        new(nameof(GivenCanonicalSchema_WhenColumnsRead_ThenChannelMajorAhwOrder),
            GivenCanonicalSchema_WhenColumnsRead_ThenChannelMajorAhwOrder),
        new(nameof(GivenZeroEvents_WhenFrameCreated_ThenAccepted),
            GivenZeroEvents_WhenFrameCreated_ThenAccepted),
        new(nameof(GivenOneEvent_WhenFrameCreated_ThenAccepted),
            GivenOneEvent_WhenFrameCreated_ThenAccepted),
        new(nameof(GivenHardEventCount_WhenFrameCreated_ThenAccepted),
            GivenHardEventCount_WhenFrameCreated_ThenAccepted),
        new(nameof(GivenHeadroomEventCount_WhenFrameCreated_ThenAccepted),
            GivenHeadroomEventCount_WhenFrameCreated_ThenAccepted),
        new(nameof(GivenHeadroomPlusOne_WhenFrameCreated_ThenTypedFailure),
            GivenHeadroomPlusOne_WhenFrameCreated_ThenTypedFailure),
        new(nameof(GivenNegativeEventCount_WhenFrameCreated_ThenTypedFailure),
            GivenNegativeEventCount_WhenFrameCreated_ThenTypedFailure),
        new(nameof(GivenNullValues_WhenFrameCreated_ThenTypedFailure),
            GivenNullValues_WhenFrameCreated_ThenTypedFailure),
        new(nameof(GivenNullColumns_WhenFrameCreated_ThenTypedFailure),
            GivenNullColumns_WhenFrameCreated_ThenTypedFailure),
        new(nameof(GivenShapeMismatch_WhenFrameCreated_ThenTypedFailure),
            GivenShapeMismatch_WhenFrameCreated_ThenTypedFailure),
        new(nameof(GivenWrongSchemaVersion_WhenFrameCreated_ThenTypedFailure),
            GivenWrongSchemaVersion_WhenFrameCreated_ThenTypedFailure),
        new(nameof(GivenWrongColumnOrder_WhenFrameCreated_ThenTypedFailure),
            GivenWrongColumnOrder_WhenFrameCreated_ThenTypedFailure),
        new(nameof(GivenZeroGeneration_WhenFrameCreated_ThenTypedFailure),
            GivenZeroGeneration_WhenFrameCreated_ThenTypedFailure),
        new(nameof(GivenGenerationReversal_WhenValidated_ThenTypedFailure),
            GivenGenerationReversal_WhenValidated_ThenTypedFailure),
        new(nameof(GivenEqualGeneration_WhenValidated_ThenTypedFailure),
            GivenEqualGeneration_WhenValidated_ThenTypedFailure),
        new(nameof(GivenSourceMutation_WhenPublicFrameCreated_ThenRawValueAndBitsRemainUnchanged),
            GivenSourceMutation_WhenPublicFrameCreated_ThenRawValueAndBitsRemainUnchanged),
        new(nameof(GivenSameSeed_WhenFixturesGenerated_ThenValuesAreDeterministic),
            GivenSameSeed_WhenFixturesGenerated_ThenValuesAreDeterministic),
        new(nameof(GivenDifferentSeeds_WhenFixturesGenerated_ThenValuesDiffer),
            GivenDifferentSeeds_WhenFixturesGenerated_ThenValuesDiffer),
    ];

    private static void GivenCanonicalSchema_WhenColumnsRead_ThenChannelMajorAhwOrder()
    {
        // Given: The canonical gcsa-compatible pulse-feature schema.
        var expectedColumns = new[]
        {
            "FSC_A", "FSC_H", "FSC_W",
            "SSC_A", "SSC_H", "SSC_W",
            "FL1_A", "FL1_H", "FL1_W",
            "FL2_A", "FL2_H", "FL2_W",
            "FL3_A", "FL3_H", "FL3_W",
            "FL4_A", "FL4_H", "FL4_W",
            "FL5_A", "FL5_H", "FL5_W",
            "FL6_A", "FL6_H", "FL6_W",
        };

        // When: Reading the versioned prototype schema.
        var actualColumns = PulseFeatureSchema.Columns;

        // Then: Version 1 exposes exactly 24 channel-major A/H/W columns.
        ContractAssert.Equal(1, PulseFeatureSchema.Version);
        ContractAssert.Equal(24, PulseFeatureSchema.ColumnCount);
        ContractAssert.SequenceEqual(expectedColumns, actualColumns);
    }

    private static void GivenZeroEvents_WhenFrameCreated_ThenAccepted()
    {
        // Given: An empty, correctly shaped feature buffer.
        var values = BuildValues(eventCount: 0);

        // When: Constructing generation one of the aggregate frame.
        var frame = CreateFrame(eventCount: 0, generation: 1, values);

        // Then: The empty frame remains valid and bounded.
        ContractAssert.Equal(0, frame.EventCount);
        ContractAssert.Equal(0, frame.PulseFeatureValues.Length);
    }

    private static void GivenOneEvent_WhenFrameCreated_ThenAccepted()
    {
        // Given: One event with all 24 pulse features.
        var values = BuildValues(eventCount: 1);

        // When: Constructing a bounded aggregate frame.
        var frame = CreateFrame(eventCount: 1, generation: 1, values);

        // Then: The one-event boundary is retained exactly.
        ContractAssert.Equal(1, frame.EventCount);
        ContractAssert.Equal(PulseFeatureSchema.ColumnCount, frame.PulseFeatureValues.Length);
    }

    private static void GivenHardEventCount_WhenFrameCreated_ThenAccepted()
    {
        // Given: The 100,001-event hard scatter fixture boundary.
        var values = BuildValues(HardEventCount);

        // When: Constructing the aggregate frame.
        var frame = CreateFrame(HardEventCount, generation: 1, values);

        // Then: The hard fixture boundary is accepted without truncation.
        ContractAssert.Equal(HardEventCount, frame.EventCount);
    }

    private static void GivenHeadroomEventCount_WhenFrameCreated_ThenAccepted()
    {
        // Given: The 131,072-event observational headroom boundary.
        var values = BuildValues(HeadroomEventCount);

        // When: Constructing the aggregate frame.
        var frame = CreateFrame(HeadroomEventCount, generation: 1, values);

        // Then: The maximum bounded event count is accepted.
        ContractAssert.Equal(HeadroomEventCount, frame.EventCount);
    }

    private static void GivenHeadroomPlusOne_WhenFrameCreated_ThenTypedFailure()
    {
        // Given: One event beyond the bounded observational maximum.
        const int eventCount = HeadroomEventCount + 1;
        var values = BuildValues(eventCount);

        // When: Constructing the aggregate frame.
        // Then: A typed validation error reports the exact rejected boundary.
        ContractAssert.Throws<AggregateFrameValidationException>(
            () => CreateFrame(eventCount, generation: 1, values),
            "Event count must be between 0 and 131072 inclusive; actual: 131073.");
    }

    private static void GivenNegativeEventCount_WhenFrameCreated_ThenTypedFailure()
    {
        // Given: An event count one below the bounded minimum.
        const int eventCount = -1;

        // When: Constructing an aggregate frame.
        // Then: A typed validation error reports the exact rejected lower boundary.
        ContractAssert.Throws<AggregateFrameValidationException>(
            () => CreateFrame(eventCount, generation: 1, Array.Empty<double>()),
            "Event count must be between 0 and 131072 inclusive; actual: -1.");
    }

    private static void GivenNullValues_WhenFrameCreated_ThenTypedFailure()
    {
        // Given: A declared event with no pulse-feature storage.
        double[]? values = null;

        // When: Constructing the aggregate frame.
        // Then: A typed validation error identifies the null field exactly.
        ContractAssert.Throws<AggregateFrameValidationException>(
            () => AggregateFrame.Create(
                featureSchemaVersion: PulseFeatureSchema.Version,
                generation: 1,
                eventCount: 1,
                pulseFeatureColumns: PulseFeatureSchema.Columns,
                pulseFeatureValues: values),
            "Pulse feature values must not be null.");
    }

    private static void GivenNullColumns_WhenFrameCreated_ThenTypedFailure()
    {
        // Given: One complete event without schema columns.
        IReadOnlyList<string>? columns = null;
        var values = BuildValues(eventCount: 1);

        // When: Constructing the aggregate frame.
        // Then: A typed validation error identifies the null field exactly.
        ContractAssert.Throws<AggregateFrameValidationException>(
            () => AggregateFrame.Create(
                featureSchemaVersion: PulseFeatureSchema.Version,
                generation: 1,
                eventCount: 1,
                pulseFeatureColumns: columns,
                pulseFeatureValues: values),
            "Pulse feature columns must not be null.");
    }

    private static void GivenShapeMismatch_WhenFrameCreated_ThenTypedFailure()
    {
        // Given: One event whose flat buffer is one feature short.
        var values = new double[PulseFeatureSchema.ColumnCount - 1];

        // When: Constructing the aggregate frame.
        // Then: A typed validation error reports expected and actual element counts.
        ContractAssert.Throws<AggregateFrameValidationException>(
            () => CreateFrame(eventCount: 1, generation: 1, values),
            "Pulse feature value count must equal event_count * 24; expected: 24, actual: 23.");
    }

    private static void GivenWrongSchemaVersion_WhenFrameCreated_ThenTypedFailure()
    {
        // Given: A correctly shaped frame labeled with an unsupported schema version.
        var values = BuildValues(eventCount: 1);

        // When: Constructing the aggregate frame.
        // Then: A typed validation error preserves the integer schema authority.
        ContractAssert.Throws<AggregateFrameValidationException>(
            () => AggregateFrame.Create(
                featureSchemaVersion: PulseFeatureSchema.Version + 1,
                generation: 1,
                eventCount: 1,
                pulseFeatureColumns: PulseFeatureSchema.Columns,
                pulseFeatureValues: values),
            "Feature schema version must be 1; actual: 2.");
    }

    private static void GivenWrongColumnOrder_WhenFrameCreated_ThenTypedFailure()
    {
        // Given: All canonical columns with the first two entries swapped.
        var columns = PulseFeatureSchema.Columns.ToArray();
        (columns[0], columns[1]) = (columns[1], columns[0]);
        var values = BuildValues(eventCount: 1);

        // When: Constructing the aggregate frame.
        // Then: A typed validation error rejects a shape-compatible schema permutation.
        ContractAssert.Throws<AggregateFrameValidationException>(
            () => AggregateFrame.Create(
                featureSchemaVersion: PulseFeatureSchema.Version,
                generation: 1,
                eventCount: 1,
                pulseFeatureColumns: columns,
                pulseFeatureValues: values),
            "Pulse feature columns must match feature schema version 1 in canonical order.");
    }

    private static void GivenZeroGeneration_WhenFrameCreated_ThenTypedFailure()
    {
        // Given: A correctly shaped frame with the invalid zero generation.
        var values = BuildValues(eventCount: 1);

        // When: Constructing the aggregate frame.
        // Then: A typed validation error rejects generation zero explicitly.
        ContractAssert.Throws<AggregateFrameValidationException>(
            () => CreateFrame(eventCount: 1, generation: 0, values),
            "Generation must be greater than zero; actual: 0.");
    }

    private static void GivenGenerationReversal_WhenValidated_ThenTypedFailure()
    {
        // Given: A previously observed generation followed by an older generation.
        const long previousGeneration = 2;
        const long nextGeneration = 1;

        // When: Validating publication order.
        // Then: A typed validation error rejects reversal rather than rendering stale data.
        ContractAssert.Throws<AggregateFrameValidationException>(
            () => FrameGenerationGuard.EnsureNewer(previousGeneration, nextGeneration),
            "Generation must increase monotonically; previous: 2, actual: 1.");
    }

    private static void GivenEqualGeneration_WhenValidated_ThenTypedFailure()
    {
        // Given: A publication with the same generation as the previous frame.
        const long generation = 2;

        // When: Validating publication order.
        // Then: A typed validation error rejects equality rather than republishing stale data.
        ContractAssert.Throws<AggregateFrameValidationException>(
            () => FrameGenerationGuard.EnsureNewer(generation, generation),
            "Generation must increase monotonically; previous: 2, actual: 2.");
    }

    private static void GivenSourceMutation_WhenPublicFrameCreated_ThenRawValueAndBitsRemainUnchanged()
    {
        // Given: One public caller-owned raw feature buffer with a known first value.
        var values = BuildValues(eventCount: 1);
        values[0] = 123.25;
        var expectedValue = values[0];
        var expectedBits = BitConverter.DoubleToInt64Bits(expectedValue);

        // When: Creating the frame and then mutating the caller's source buffer.
        var frame = CreateFrame(eventCount: 1, generation: 1, values);
        values[0] = -987.5;

        // Then: The frame retains the original raw value and exact binary64 bits.
        var actualValue = frame.PulseFeatureValues.Span[0];
        ContractAssert.Equal(expectedValue, actualValue);
        ContractAssert.Equal(expectedBits, BitConverter.DoubleToInt64Bits(actualValue));
    }

    private static void GivenSameSeed_WhenFixturesGenerated_ThenValuesAreDeterministic()
    {
        // Given: The same seed, event count, and generation.
        const int seed = 42;

        // When: Generating the aggregate fixture twice.
        var first = SyntheticAggregateFrameFactory.Create(
            eventCount: HardEventCount,
            seed,
            generation: 1);
        var second = SyntheticAggregateFrameFactory.Create(
            eventCount: HardEventCount,
            seed,
            generation: 1);

        // Then: Schema, ordering, and all raw feature values are identical.
        ContractAssert.SequenceEqual(
            first.PulseFeatureColumns,
            second.PulseFeatureColumns);
        ContractAssert.SequenceEqual(
            first.PulseFeatureValues.ToArray(),
            second.PulseFeatureValues.ToArray());
    }

    private static void GivenDifferentSeeds_WhenFixturesGenerated_ThenValuesDiffer()
    {
        // Given: The same bounded shape with two different seeds.
        const int eventCount = 257;

        // When: Generating both aggregate fixtures.
        var first = SyntheticAggregateFrameFactory.Create(
            eventCount,
            seed: 41,
            generation: 1);
        var second = SyntheticAggregateFrameFactory.Create(
            eventCount,
            seed: 42,
            generation: 1);

        // Then: The raw pulse-feature payloads differ.
        ContractAssert.SequenceNotEqual(
            first.PulseFeatureValues.ToArray(),
            second.PulseFeatureValues.ToArray());
    }

    private static AggregateFrame CreateFrame(
        int eventCount,
        long generation,
        double[] values)
    {
        return AggregateFrame.Create(
            featureSchemaVersion: PulseFeatureSchema.Version,
            generation,
            eventCount,
            pulseFeatureColumns: PulseFeatureSchema.Columns,
            pulseFeatureValues: values);
    }

    private static double[] BuildValues(int eventCount)
    {
        return new double[checked(eventCount * PulseFeatureSchema.ColumnCount)];
    }
}
