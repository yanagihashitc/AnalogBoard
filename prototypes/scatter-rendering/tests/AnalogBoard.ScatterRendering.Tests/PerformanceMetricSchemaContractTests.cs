using AnalogBoard.ScatterRendering.Core;

namespace AnalogBoard.ScatterRendering.Tests;

internal static class PerformanceMetricSchemaContractTests
{
    public static IReadOnlyList<ContractTestCase> Cases { get; } =
    [
        new(nameof(GivenMetricSchema_WhenIdentityRead_ThenVersionIsStable),
            GivenMetricSchema_WhenIdentityRead_ThenVersionIsStable),
        new(nameof(GivenHardGateMetrics_WhenUnitsResolved_ThenEveryUnitIsExplicit),
            GivenHardGateMetrics_WhenUnitsResolved_ThenEveryUnitIsExplicit),
        new(nameof(GivenMetricSchema_WhenDefinitionsRead_ThenExactOrderedAuthorityIsExposed),
            GivenMetricSchema_WhenDefinitionsRead_ThenExactOrderedAuthorityIsExposed),
        new(nameof(GivenUnknownMetric_WhenUnitResolved_ThenTypedFailure),
            GivenUnknownMetric_WhenUnitResolved_ThenTypedFailure),
    ];

    private static void GivenMetricSchema_WhenIdentityRead_ThenVersionIsStable()
    {
        // Given: The bounded prototype metric schema.
        // When: Reading its public identity.
        var schemaId = PerformanceMetricSchema.SchemaId;

        // Then: Evidence can pin one explicit version rather than an unversioned shape.
        ContractAssert.Equal(
            "analogboard.scatter-rendering.metrics.v1",
            schemaId);
    }

    private static void GivenHardGateMetrics_WhenUnitsResolved_ThenEveryUnitIsExplicit()
    {
        // Given: Every metric required by the P0-R1 hard performance gate.
        var expectedUnits = new Dictionary<string, string>
        {
            [PerformanceMetricNames.ScatterDeliveredRate] = "frames_per_second",
            [PerformanceMetricNames.ScatterFrameTimeP95] = "milliseconds",
            [PerformanceMetricNames.ScatterFrameTimeMax] = "milliseconds",
            [PerformanceMetricNames.GmiDeliveredRate] = "updates_per_second",
            [PerformanceMetricNames.GmiMaxInterval] = "milliseconds",
            [PerformanceMetricNames.UiInputLatencyP95] = "milliseconds",
            [PerformanceMetricNames.UiInputLatencyMax] = "milliseconds",
            [PerformanceMetricNames.ProducerPublicationP99] = "milliseconds",
            [PerformanceMetricNames.PendingWorkMax] = "count",
            [PerformanceMetricNames.CoalescedFrames] = "count",
            [PerformanceMetricNames.ManagedAllocationPerFrame] = "bytes_per_frame",
            [PerformanceMetricNames.ManagedAllocationEventDeltaPerFrame] = "bytes_per_frame",
            [PerformanceMetricNames.RetainedManagedHeapGrowth] = "bytes",
            [PerformanceMetricNames.PrivateBytesGrowth] = "bytes",
        };

        // When: Resolving the unit for each metric.
        var actualUnits = expectedUnits.Keys.ToDictionary(
            metricName => metricName,
            PerformanceMetricSchema.UnitFor);

        // Then: Every raw metric carries an explicit machine-readable unit.
        ContractAssert.Equal(expectedUnits.Count, actualUnits.Count);
        foreach (var expected in expectedUnits)
        {
            ContractAssert.Equal(expected.Value, actualUnits[expected.Key]);
        }
    }

    private static void GivenMetricSchema_WhenDefinitionsRead_ThenExactOrderedAuthorityIsExposed()
    {
        // Given: The exact ordered P0-R1 hard-gate metric contract.
        var expectedDefinitions = new (string Name, string Unit)[]
        {
            (PerformanceMetricNames.ScatterDeliveredRate, "frames_per_second"),
            (PerformanceMetricNames.ScatterFrameTimeP95, "milliseconds"),
            (PerformanceMetricNames.ScatterFrameTimeMax, "milliseconds"),
            (PerformanceMetricNames.GmiDeliveredRate, "updates_per_second"),
            (PerformanceMetricNames.GmiMaxInterval, "milliseconds"),
            (PerformanceMetricNames.UiInputLatencyP95, "milliseconds"),
            (PerformanceMetricNames.UiInputLatencyMax, "milliseconds"),
            (PerformanceMetricNames.ProducerPublicationP99, "milliseconds"),
            (PerformanceMetricNames.PendingWorkMax, "count"),
            (PerformanceMetricNames.CoalescedFrames, "count"),
            (PerformanceMetricNames.ManagedAllocationPerFrame, "bytes_per_frame"),
            (PerformanceMetricNames.ManagedAllocationEventDeltaPerFrame, "bytes_per_frame"),
            (PerformanceMetricNames.RetainedManagedHeapGrowth, "bytes"),
            (PerformanceMetricNames.PrivateBytesGrowth, "bytes"),
        };

        // When: Reading the public production definition authority.
        var actualDefinitions = PerformanceMetricSchema.Definitions;

        // Then: Exactly 14 definitions exist in the pinned order with no extras or gaps.
        ContractAssert.Equal(expectedDefinitions.Length, actualDefinitions.Count);
        for (var index = 0; index < expectedDefinitions.Length; index++)
        {
            ContractAssert.Equal(expectedDefinitions[index].Name, actualDefinitions[index].Name);
            ContractAssert.Equal(expectedDefinitions[index].Unit, actualDefinitions[index].Unit);
        }
    }

    private static void GivenUnknownMetric_WhenUnitResolved_ThenTypedFailure()
    {
        // Given: A metric name outside the versioned schema.
        const string unknownMetric = "unknown.metric";

        // When: Resolving its unit.
        // Then: A typed error prevents unit inference in evidence generation.
        ContractAssert.Throws<MetricSchemaException>(
            () => PerformanceMetricSchema.UnitFor(unknownMetric),
            "Unknown performance metric: 'unknown.metric'.");
    }
}
