namespace AnalogBoard.ScatterRendering.Core;

public static class PerformanceMetricNames
{
    public const string ScatterDeliveredRate = "scatter.delivered_rate";
    public const string ScatterFrameTimeP95 = "scatter.frame_time_p95";
    public const string ScatterFrameTimeMax = "scatter.frame_time_max";
    public const string GmiDeliveredRate = "gmi.delivered_rate";
    public const string GmiMaxInterval = "gmi.max_interval";
    public const string UiInputLatencyP95 = "ui.input_latency_p95";
    public const string UiInputLatencyMax = "ui.input_latency_max";
    public const string ProducerPublicationP99 = "producer.publication_p99";
    public const string PendingWorkMax = "producer.pending_work_max";
    public const string CoalescedFrames = "producer.coalesced_frames";
    public const string ManagedAllocationPerFrame = "managed.allocation_per_frame";
    public const string ManagedAllocationEventDeltaPerFrame = "managed.allocation_event_delta_per_frame";
    public const string RetainedManagedHeapGrowth = "managed.retained_heap_growth";
    public const string PrivateBytesGrowth = "process.private_bytes_growth";
}

public sealed record PerformanceMetricDefinition(string Name, string Unit);

public static class PerformanceMetricSchema
{
    private static readonly PerformanceMetricDefinition[] OrderedDefinitions =
    [
        new(PerformanceMetricNames.ScatterDeliveredRate, "frames_per_second"),
        new(PerformanceMetricNames.ScatterFrameTimeP95, "milliseconds"),
        new(PerformanceMetricNames.ScatterFrameTimeMax, "milliseconds"),
        new(PerformanceMetricNames.GmiDeliveredRate, "updates_per_second"),
        new(PerformanceMetricNames.GmiMaxInterval, "milliseconds"),
        new(PerformanceMetricNames.UiInputLatencyP95, "milliseconds"),
        new(PerformanceMetricNames.UiInputLatencyMax, "milliseconds"),
        new(PerformanceMetricNames.ProducerPublicationP99, "milliseconds"),
        new(PerformanceMetricNames.PendingWorkMax, "count"),
        new(PerformanceMetricNames.CoalescedFrames, "count"),
        new(PerformanceMetricNames.ManagedAllocationPerFrame, "bytes_per_frame"),
        new(PerformanceMetricNames.ManagedAllocationEventDeltaPerFrame, "bytes_per_frame"),
        new(PerformanceMetricNames.RetainedManagedHeapGrowth, "bytes"),
        new(PerformanceMetricNames.PrivateBytesGrowth, "bytes"),
    ];

    private static readonly IReadOnlyDictionary<string, string> UnitsByName =
        OrderedDefinitions.ToDictionary(
            definition => definition.Name,
            definition => definition.Unit,
            StringComparer.Ordinal);

    public const string SchemaId = "analogboard.scatter-rendering.metrics.v1";

    public static IReadOnlyList<PerformanceMetricDefinition> Definitions { get; } =
        Array.AsReadOnly(OrderedDefinitions);

    public static string UnitFor(string metricName)
    {
        if (metricName is not null && UnitsByName.TryGetValue(metricName, out var unit))
        {
            return unit;
        }

        throw new MetricSchemaException(
            $"Unknown performance metric: '{metricName}'.");
    }
}

public sealed class MetricSchemaException : Exception
{
    public MetricSchemaException(string message)
        : base(message)
    {
    }
}
