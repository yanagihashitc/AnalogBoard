namespace AnalogBoard.ScatterRendering.Core;

public sealed class DensityBinner
{
    private readonly double[] _xScratch;
    private readonly double[] _yScratch;

    public DensityBinner(int maximumEventCount = AggregateFrame.MaximumEventCount)
    {
        if (maximumEventCount < 0 || maximumEventCount > AggregateFrame.MaximumEventCount)
        {
            throw new DensityBinningValidationException(
                $"Binner capacity must be between 0 and {AggregateFrame.MaximumEventCount} inclusive; actual: {maximumEventCount}.");
        }

        MaximumEventCount = maximumEventCount;
        _xScratch = new double[maximumEventCount];
        _yScratch = new double[maximumEventCount];
    }

    public int MaximumEventCount { get; }

    public void Bin(
        AggregateFrame frame,
        DensityBinningRequest request,
        DensityGridBuffer destination)
    {
        ArgumentNullException.ThrowIfNull(frame);
        ArgumentNullException.ThrowIfNull(destination);
        ValidateRequest(frame, request);
        if (frame.EventCount > MaximumEventCount)
        {
            throw new DensityBinningValidationException(
                $"Frame event count exceeds binner capacity; frame: {frame.EventCount}, capacity: {MaximumEventCount}.");
        }

        destination.Reset(frame.Generation, frame.EventCount);

        var values = frame.PulseFeatureValues.Span;
        var finitePairCount = CopyFinitePairs(
            values,
            frame.EventCount,
            request.XAxis.FeatureIndex,
            request.YAxis.FeatureIndex,
            out var nonFinitePairCount);
        var xTransform = FittedDisplayTransform.FitFromOwnedFiniteBuffer(
            request.XAxis.Transform,
            _xScratch.AsSpan(0, finitePairCount));
        var yTransform = FittedDisplayTransform.FitFromOwnedFiniteBuffer(
            request.YAxis.Transform,
            _yScratch.AsSpan(0, finitePairCount));
        destination.SetTransformMetadata(xTransform, yTransform);

        var xDomain = ResolveDomain(
            values,
            frame.EventCount,
            request.XAxis,
            request.YAxis.FeatureIndex,
            xTransform);
        var yDomain = ResolveDomain(
            values,
            frame.EventCount,
            request.YAxis,
            request.XAxis.FeatureIndex,
            yTransform);

        if (!xDomain.HasValue || !yDomain.HasValue)
        {
            destination.SetAccounting(
                nonFinitePairCount,
                outOfRangePairCount: 0,
                clampedPairCount: 0,
                binnedEventCount: 0,
                maximumCount: 0);
            return;
        }

        WriteEdges(destination.WritableXEdges, xDomain.Value);
        WriteEdges(destination.WritableYEdges, yDomain.Value);
        BinFinitePairs(
            values,
            frame.EventCount,
            request,
            destination,
            xTransform,
            yTransform,
            xDomain.Value,
            yDomain.Value,
            nonFinitePairCount);
    }

    private int CopyFinitePairs(
        ReadOnlySpan<double> values,
        int eventCount,
        int xFeatureIndex,
        int yFeatureIndex,
        out int nonFinitePairCount)
    {
        var finitePairCount = 0;
        nonFinitePairCount = 0;
        for (var eventIndex = 0; eventIndex < eventCount; eventIndex++)
        {
            var offset = eventIndex * PulseFeatureSchema.ColumnCount;
            var xValue = values[offset + xFeatureIndex];
            var yValue = values[offset + yFeatureIndex];
            if (!double.IsFinite(xValue) || !double.IsFinite(yValue))
            {
                nonFinitePairCount++;
                continue;
            }

            _xScratch[finitePairCount] = xValue;
            _yScratch[finitePairCount] = yValue;
            finitePairCount++;
        }
        return finitePairCount;
    }

    private static DensityRange? ResolveDomain(
        ReadOnlySpan<double> values,
        int eventCount,
        DensityAxisOptions axis,
        int pairedFeatureIndex,
        FittedDisplayTransform transform)
    {
        if (axis.RawRange is { } rawRange)
        {
            return new DensityRange(
                transform.Forward(rawRange.Minimum),
                transform.Forward(rawRange.Maximum));
        }

        var hasValue = false;
        var minimum = double.PositiveInfinity;
        var maximum = double.NegativeInfinity;
        for (var eventIndex = 0; eventIndex < eventCount; eventIndex++)
        {
            var offset = eventIndex * PulseFeatureSchema.ColumnCount;
            var axisValue = values[offset + axis.FeatureIndex];
            var pairedValue = values[offset + pairedFeatureIndex];
            if (!double.IsFinite(axisValue) || !double.IsFinite(pairedValue))
            {
                continue;
            }

            var displayValue = transform.Forward(axisValue);
            minimum = Math.Min(minimum, displayValue);
            maximum = Math.Max(maximum, displayValue);
            hasValue = true;
        }

        return hasValue ? new DensityRange(minimum, maximum) : null;
    }

    private static void WriteEdges(Span<double> edges, DensityRange domain)
    {
        var binCount = edges.Length - 1;
        if (domain.Maximum == domain.Minimum)
        {
            for (var index = 0; index < edges.Length; index++)
            {
                edges[index] = domain.Minimum + index;
            }
            return;
        }

        var delta = domain.Maximum - domain.Minimum;
        if (double.IsFinite(delta))
        {
            var width = delta / binCount;
            for (var index = 0; index < edges.Length; index++)
            {
                edges[index] = domain.Minimum + (index * width);
            }
        }
        else
        {
            for (var index = 0; index < edges.Length; index++)
            {
                var fraction = (double)index / binCount;
                edges[index] = (domain.Minimum * (1.0 - fraction)) +
                    (domain.Maximum * fraction);
            }
        }

        edges[0] = domain.Minimum;
        edges[^1] = domain.Maximum;
    }

    private static void BinFinitePairs(
        ReadOnlySpan<double> values,
        int eventCount,
        DensityBinningRequest request,
        DensityGridBuffer destination,
        FittedDisplayTransform xTransform,
        FittedDisplayTransform yTransform,
        DensityRange xDomain,
        DensityRange yDomain,
        int nonFinitePairCount)
    {
        var outOfRangePairCount = 0;
        var clampedPairCount = 0;
        var binnedEventCount = 0;
        var maximumCount = 0;
        var counts = destination.WritableCounts;

        for (var eventIndex = 0; eventIndex < eventCount; eventIndex++)
        {
            var offset = eventIndex * PulseFeatureSchema.ColumnCount;
            var rawX = values[offset + request.XAxis.FeatureIndex];
            var rawY = values[offset + request.YAxis.FeatureIndex];
            if (!double.IsFinite(rawX) || !double.IsFinite(rawY))
            {
                continue;
            }

            var x = xTransform.Forward(rawX);
            var y = yTransform.Forward(rawY);
            var isOutside = x < xDomain.Minimum || x > xDomain.Maximum ||
                y < yDomain.Minimum || y > yDomain.Maximum;
            if (isOutside && request.OutlierMode == DensityOutlierMode.Exclude)
            {
                outOfRangePairCount++;
                continue;
            }
            if (isOutside)
            {
                clampedPairCount++;
                x = Math.Clamp(x, xDomain.Minimum, xDomain.Maximum);
                y = Math.Clamp(y, yDomain.Minimum, yDomain.Maximum);
            }

            var xIndex = ResolveBinIndex(
                x,
                xDomain,
                destination.XEdges.Span);
            var yIndex = ResolveBinIndex(
                y,
                yDomain,
                destination.YEdges.Span);
            var flatIndex = checked((yIndex * destination.Width) + xIndex);
            counts[flatIndex]++;
            maximumCount = Math.Max(maximumCount, counts[flatIndex]);
            binnedEventCount++;
        }

        destination.SetAccounting(
            nonFinitePairCount,
            outOfRangePairCount,
            clampedPairCount,
            binnedEventCount,
            maximumCount);
    }

    private static int ResolveBinIndex(
        double value,
        DensityRange domain,
        ReadOnlySpan<double> edges)
    {
        var binCount = edges.Length - 1;
        if (value == domain.Maximum)
        {
            return binCount - 1;
        }

        var lower = 0;
        var upper = edges.Length;
        while (lower < upper)
        {
            var midpoint = lower + ((upper - lower) / 2);
            if (edges[midpoint] <= value)
            {
                lower = midpoint + 1;
            }
            else
            {
                upper = midpoint;
            }
        }

        return Math.Clamp(lower - 1, 0, binCount - 1);
    }

    private static void ValidateRequest(
        AggregateFrame frame,
        DensityBinningRequest request)
    {
        ValidateAxis(request.XAxis, "X");
        ValidateAxis(request.YAxis, "Y");
        if (request.OutlierMode is < DensityOutlierMode.Exclude or > DensityOutlierMode.ClampToEdge)
        {
            throw new DensityBinningValidationException(
                $"Unsupported density outlier mode: {(int)request.OutlierMode}.");
        }
        if (frame.PulseFeatureValues.Length !=
            frame.EventCount * PulseFeatureSchema.ColumnCount)
        {
            throw new DensityBinningValidationException(
                "Aggregate frame pulse feature shape is incompatible with the canonical schema.");
        }
    }

    private static void ValidateAxis(DensityAxisOptions axis, string axisName)
    {
        if (axis.FeatureIndex < 0 || axis.FeatureIndex >= PulseFeatureSchema.ColumnCount)
        {
            throw new DensityBinningValidationException(
                $"{axisName} feature index must be between 0 and {PulseFeatureSchema.ColumnCount - 1} inclusive; actual: {axis.FeatureIndex}.");
        }
        if (axis.Transform is < DisplayTransformKind.Linear or > DisplayTransformKind.Asinh)
        {
            throw new DensityBinningValidationException(
                $"Unsupported {axisName} display transform kind: {(int)axis.Transform}.");
        }
        if (axis.RawRange is not { } range)
        {
            return;
        }
        if (!double.IsFinite(range.Minimum) || !double.IsFinite(range.Maximum))
        {
            throw new DensityBinningValidationException(
                $"{axisName} range bounds must contain only finite values.");
        }
        if (range.Minimum > range.Maximum)
        {
            throw new DensityBinningValidationException(
                $"{axisName} range minimum must be less than or equal to maximum; actual: {range.Minimum:R} > {range.Maximum:R}.");
        }
    }
}
