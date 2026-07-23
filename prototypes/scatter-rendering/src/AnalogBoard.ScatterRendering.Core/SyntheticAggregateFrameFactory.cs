namespace AnalogBoard.ScatterRendering.Core;

public static class SyntheticAggregateFrameFactory
{
    private const ulong SplitMixIncrement = 0x9E3779B97F4A7C15UL;
    private const double UnitScale = 1.0 / (1UL << 53);

    public static AggregateFrame Create(int eventCount, int seed, long generation)
    {
        AggregateFrame.ValidateEventCount(eventCount);

        var values = new double[checked(eventCount * PulseFeatureSchema.ColumnCount)];
        var state = unchecked((ulong)(long)seed);

        for (var eventIndex = 0; eventIndex < eventCount; eventIndex++)
        {
            var eventOffset = eventIndex * PulseFeatureSchema.ColumnCount;
            for (var columnIndex = 0;
                 columnIndex < PulseFeatureSchema.ColumnCount;
                 columnIndex++)
            {
                var channelIndex = columnIndex / 3;
                var featureIndex = columnIndex % 3;
                var unit = NextUnitInterval(ref state);
                var signedNoise = (unit * 2.0) - 1.0;
                var channelCenter = channelIndex < 2
                    ? 45_000.0 + (channelIndex * 20_000.0)
                    : (channelIndex - 4) * 1_500.0;
                var featureScale = featureIndex switch
                {
                    0 => 1.0,
                    1 => 0.65,
                    _ => 0.08,
                };
                var eventTrend = (eventIndex % 257) * (featureIndex + 1) * 0.125;

                values[eventOffset + columnIndex] =
                    (channelCenter * featureScale) +
                    (signedNoise * (7_500.0 + (channelIndex * 350.0))) +
                    eventTrend;
            }
        }

        return AggregateFrame.CreateFromOwnedBuffer(
            PulseFeatureSchema.Version,
            generation,
            eventCount,
            PulseFeatureSchema.Columns,
            values);
    }

    private static double NextUnitInterval(ref ulong state)
    {
        state += SplitMixIncrement;
        var mixed = state;
        mixed = (mixed ^ (mixed >> 30)) * 0xBF58476D1CE4E5B9UL;
        mixed = (mixed ^ (mixed >> 27)) * 0x94D049BB133111EBUL;
        mixed ^= mixed >> 31;
        return (mixed >> 11) * UnitScale;
    }
}
