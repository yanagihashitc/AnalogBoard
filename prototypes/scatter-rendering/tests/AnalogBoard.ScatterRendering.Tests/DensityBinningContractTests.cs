using AnalogBoard.ScatterRendering.Core;

namespace AnalogBoard.ScatterRendering.Tests;

internal static class DensityBinningContractTests
{
    public static IReadOnlyList<ContractTestCase> Cases { get; } =
    [
        new(nameof(GivenExactEdges_WhenBinned_ThenInternalAndUpperEdgesMatchAuthority),
            GivenExactEdges_WhenBinned_ThenInternalAndUpperEdgesMatchAuthority),
        new(nameof(GivenAsymmetricAxes_WhenBinned_ThenCountsAreYMajorRowMajor),
            GivenAsymmetricAxes_WhenBinned_ThenCountsAreYMajorRowMajor),
        new(nameof(GivenNonFinitePairs_WhenBinned_ThenExcludedBeforeTransformAndRawBitsRemain),
            GivenNonFinitePairs_WhenBinned_ThenExcludedBeforeTransformAndRawBitsRemain),
        new(nameof(GivenInterleavedNegativeValues_WhenBinned_ThenOwnedFitMatchesPublicAuthority),
            GivenInterleavedNegativeValues_WhenBinned_ThenOwnedFitMatchesPublicAuthority),
        new(nameof(GivenOutliers_WhenExcludedOrClamped_ThenAccountingAndCountsConservePairs),
            GivenOutliers_WhenExcludedOrClamped_ThenAccountingAndCountsConservePairs),
        new(nameof(GivenEmptyExcludedAndDegenerateAxes_WhenBinned_ThenEdgesRemainDeterministic),
            GivenEmptyExcludedAndDegenerateAxes_WhenBinned_ThenEdgesRemainDeterministic),
        new(nameof(GivenInvalidRequest_WhenBinned_ThenTypedFailure),
            GivenInvalidRequest_WhenBinned_ThenTypedFailure),
        new(nameof(GivenHardFixture_WhenBinnedTwice_ThenCountsEdgesAndRawValuesAreIdentical),
            GivenHardFixture_WhenBinnedTwice_ThenCountsEdgesAndRawValuesAreIdentical),
    ];

    private static void GivenExactEdges_WhenBinned_ThenInternalAndUpperEdgesMatchAuthority()
    {
        // Given: Values on the lower edge, internal edge, and final inclusive upper edge.
        var frame = CreateFrame(
            (0.0, 0.0),
            (0.999, 0.999),
            (1.0, 0.0),
            (1.0, 1.0),
            (2.0, 2.0));
        var destination = new DensityGridBuffer(2, 2);

        // When: Binning into a fixed linear 0..2 domain.
        CreateBinner(frame).Bin(frame, CreateRequest(0.0, 2.0), destination);

        // Then: Internal edges select the upper bin and the final upper edge is included.
        ContractAssert.SequenceEqual(new[] { 0.0, 1.0, 2.0 }, destination.XEdges.ToArray());
        ContractAssert.SequenceEqual(new[] { 0.0, 1.0, 2.0 }, destination.YEdges.ToArray());
        ContractAssert.SequenceEqual(new[] { 2, 1, 0, 2 }, destination.Counts.ToArray());
        ContractAssert.Equal(5, destination.BinnedEventCount);
        ContractAssert.Equal(0, destination.NonFinitePairCount);
        ContractAssert.Equal(0, destination.OutOfRangePairCount);

        // Given: A nontrivial internal edge whose ratio rounds just below one.
        const double minimum = 0.1;
        const double maximum = 0.2;
        const int bins = 3;
        var publishedInternalEdge = minimum + ((maximum - minimum) / bins);
        var roundingFrame = CreateFrame((publishedInternalEdge, minimum));
        var roundingDestination = new DensityGridBuffer(bins, bins);

        // When: Binning the exact value published as X edge one.
        CreateBinner(roundingFrame).Bin(
            roundingFrame,
            CreateRequest(minimum, maximum),
            roundingDestination);

        // Then: The published internal edge enters the upper X bin.
        ContractAssert.Equal(
            BitConverter.DoubleToInt64Bits(publishedInternalEdge),
            BitConverter.DoubleToInt64Bits(roundingDestination.XEdges.Span[1]));
        ContractAssert.SequenceEqual(
            new[] { 0, 1, 0, 0, 0, 0, 0, 0, 0 },
            roundingDestination.Counts.ToArray());

        // Given: Finite symmetric bounds whose direct subtraction overflows binary64.
        var hugeFrame = CreateFrame((0.0, 0.0));
        var hugeDestination = new DensityGridBuffer(2, 2);

        // When: Binning the exact mathematical midpoint of the huge range.
        CreateBinner(hugeFrame).Bin(
            hugeFrame,
            CreateRequest(-1e308, 1e308),
            hugeDestination);

        // Then: Edges stay finite/monotonic and midpoint zero enters the upper bins.
        ContractAssert.SequenceEqual(
            new[] { -1e308, 0.0, 1e308 },
            hugeDestination.XEdges.ToArray());
        ContractAssert.SequenceEqual(
            new[] { -1e308, 0.0, 1e308 },
            hugeDestination.YEdges.ToArray());
        ContractAssert.SequenceEqual(new[] { 0, 0, 0, 1 }, hugeDestination.Counts.ToArray());
    }

    private static void GivenAsymmetricAxes_WhenBinned_ThenCountsAreYMajorRowMajor()
    {
        // Given: An asymmetric X/Y range that exposes accidental transpose or X-major flattening.
        var frame = CreateFrame((0.25, 0.25), (1.25, 0.25), (1.25, 2.25));
        var request = new DensityBinningRequest(
            new DensityAxisOptions(0, DisplayTransformKind.Linear, new DensityRange(0.0, 2.0)),
            new DensityAxisOptions(1, DisplayTransformKind.Linear, new DensityRange(0.0, 4.0)),
            DensityOutlierMode.Exclude);
        var destination = new DensityGridBuffer(2, 2);

        // When: Binning into the reusable destination.
        CreateBinner(frame).Bin(frame, request, destination);

        // Then: Flattened counts use index = y * width + x.
        ContractAssert.SequenceEqual(new[] { 1, 1, 0, 1 }, destination.Counts.ToArray());
    }

    private static void GivenNonFinitePairs_WhenBinned_ThenExcludedBeforeTransformAndRawBitsRemain()
    {
        // Given: Pairs where either axis is non-finite and transforms that reject non-finite input.
        var frame = CreateFrame(
            (0.0, 0.0),
            (double.NaN, 1.0),
            (1.0, double.NaN),
            (double.PositiveInfinity, 2.0),
            (2.0, double.NegativeInfinity),
            (1.0, 1.0));
        var rawBits = frame.PulseFeatureValues.Span
            .ToArray()
            .Select(BitConverter.DoubleToInt64Bits)
            .ToArray();
        var request = new DensityBinningRequest(
            new DensityAxisOptions(0, DisplayTransformKind.Asinh, new DensityRange(0.0, 2.0)),
            new DensityAxisOptions(1, DisplayTransformKind.Biexponential, new DensityRange(0.0, 2.0)),
            DensityOutlierMode.Exclude);
        var destination = new DensityGridBuffer(2, 2);

        // When: Binning the frame.
        CreateBinner(frame).Bin(frame, request, destination);

        // Then: Four pairs are excluded before transform and source bits remain unchanged.
        ContractAssert.Equal(6, destination.InputEventCount);
        ContractAssert.Equal(4, destination.NonFinitePairCount);
        ContractAssert.Equal(0, destination.OutOfRangePairCount);
        ContractAssert.Equal(2, destination.BinnedEventCount);
        ContractAssert.Equal(2, destination.Counts.Span.ToArray().Sum());
        ContractAssert.SequenceEqual(
            rawBits,
            frame.PulseFeatureValues.Span.ToArray().Select(BitConverter.DoubleToInt64Bits));
    }

    private static void GivenOutliers_WhenExcludedOrClamped_ThenAccountingAndCountsConservePairs()
    {
        // Given: Four outliers and three in-range points around a 2x2 fixed range.
        var frame = CreateFrame(
            (-1.0, 1.0),
            (3.0, 1.0),
            (1.0, -1.0),
            (1.0, 3.0),
            (0.0, 0.0),
            (2.0, 2.0),
            (1.0, 1.0));
        var excluded = new DensityGridBuffer(2, 2);
        var clamped = new DensityGridBuffer(2, 2);
        var binner = CreateBinner(frame);

        // When: Applying both pinned outlier policies.
        binner.Bin(frame, CreateRequest(0.0, 2.0, DensityOutlierMode.Exclude), excluded);
        binner.Bin(frame, CreateRequest(0.0, 2.0, DensityOutlierMode.ClampToEdge), clamped);

        // Then: Pair-level accounting is non-overlapping and both count matrices match authority.
        ContractAssert.SequenceEqual(new[] { 1, 0, 0, 2 }, excluded.Counts.ToArray());
        ContractAssert.Equal(4, excluded.OutOfRangePairCount);
        ContractAssert.Equal(0, excluded.ClampedPairCount);
        ContractAssert.Equal(3, excluded.BinnedEventCount);
        ContractAssert.Equal(
            excluded.InputEventCount,
            excluded.NonFinitePairCount + excluded.OutOfRangePairCount + excluded.BinnedEventCount);

        ContractAssert.SequenceEqual(new[] { 1, 1, 1, 4 }, clamped.Counts.ToArray());
        ContractAssert.Equal(0, clamped.OutOfRangePairCount);
        ContractAssert.Equal(4, clamped.ClampedPairCount);
        ContractAssert.Equal(7, clamped.BinnedEventCount);
        ContractAssert.Equal(
            clamped.InputEventCount,
            clamped.NonFinitePairCount + clamped.BinnedEventCount);
    }

    private static void GivenInterleavedNegativeValues_WhenBinned_ThenOwnedFitMatchesPublicAuthority()
    {
        // Given: Interleaved mixed-sign X values that exercise owned negative compaction/sort.
        var points = new[]
        {
            (-500.0, 0.0),
            (100.0, 1.0),
            (-100.0, 2.0),
            (0.0, 3.0),
            (-10.0, 4.0),
        };
        var frame = CreateFrame(points);
        var rawBits = frame.PulseFeatureValues.Span.ToArray()
            .Select(BitConverter.DoubleToInt64Bits)
            .ToArray();
        var xValues = points.Select(point => point.Item1).ToArray();
        var publicFit = FittedDisplayTransform.Fit(
            DisplayTransformKind.Biexponential,
            xValues,
            new double[xValues.Length]);
        var request = new DensityBinningRequest(
            new DensityAxisOptions(0, DisplayTransformKind.Biexponential),
            new DensityAxisOptions(1, DisplayTransformKind.Linear),
            DensityOutlierMode.Exclude);
        var destination = new DensityGridBuffer(2, 2);

        // When: Density binning fits the same values through its owned scratch seam.
        CreateBinner(frame).Bin(frame, request, destination);

        // Then: Resolved W matches public authority, all points count, and raw bits remain.
        ContractAssert.Equal(
            BitConverter.DoubleToInt64Bits(publicFit.BiexponentialWidth),
            BitConverter.DoubleToInt64Bits(destination.XBiexponentialWidth));
        ContractAssert.Equal(points.Length, destination.BinnedEventCount);
        ContractAssert.SequenceEqual(
            rawBits,
            frame.PulseFeatureValues.Span.ToArray().Select(BitConverter.DoubleToInt64Bits));
    }

    private static void GivenEmptyExcludedAndDegenerateAxes_WhenBinned_ThenEdgesRemainDeterministic()
    {
        // Given: An empty frame, an all-out-of-range frame, and a degenerate X axis.
        var empty = CreateFrame();
        var outside = CreateFrame((10.0, 30.0), (11.0, 31.0));
        var degenerate = CreateFrame((2.0, 0.0), (2.0, 1.0), (2.0, 2.0));
        var binner = new DensityBinner(maximumEventCount: 3);
        var emptyDestination = new DensityGridBuffer(2, 2);
        var outsideDestination = new DensityGridBuffer(2, 2);
        var degenerateDestination = new DensityGridBuffer(2, 2);

        // When: Binning each boundary case.
        binner.Bin(empty, CreateRequest(0.0, 2.0), emptyDestination);
        binner.Bin(outside, CreateRequest(0.0, 2.0), outsideDestination);
        binner.Bin(
            degenerate,
            new DensityBinningRequest(
                new DensityAxisOptions(0, DisplayTransformKind.Linear),
                new DensityAxisOptions(1, DisplayTransformKind.Linear),
                DensityOutlierMode.Exclude),
            degenerateDestination);

        // Then: Fixed edges survive zero counts and degenerate max values enter the final bin.
        ContractAssert.SequenceEqual(new[] { 0.0, 1.0, 2.0 }, emptyDestination.XEdges.ToArray());
        ContractAssert.SequenceEqual(new[] { 0, 0, 0, 0 }, emptyDestination.Counts.ToArray());
        ContractAssert.SequenceEqual(new[] { 0.0, 1.0, 2.0 }, outsideDestination.XEdges.ToArray());
        ContractAssert.Equal(2, outsideDestination.OutOfRangePairCount);
        ContractAssert.SequenceEqual(new[] { 0, 0, 0, 0 }, outsideDestination.Counts.ToArray());
        ContractAssert.SequenceEqual(new[] { 2.0, 3.0, 4.0 }, degenerateDestination.XEdges.ToArray());
        ContractAssert.SequenceEqual(new[] { 0.0, 1.0, 2.0 }, degenerateDestination.YEdges.ToArray());
        ContractAssert.SequenceEqual(new[] { 0, 1, 0, 2 }, degenerateDestination.Counts.ToArray());
    }

    private static void GivenInvalidRequest_WhenBinned_ThenTypedFailure()
    {
        // Given: One valid frame and every bounded request failure class.
        var frame = CreateFrame((0.0, 0.0));
        var destination = new DensityGridBuffer(2, 2);
        var binner = CreateBinner(frame);

        // When/Then: Invalid dimensions, ranges, columns, modes, and capacity fail exactly.
        ContractAssert.Throws<DensityBinningValidationException>(
            () => new DensityGridBuffer(0, 2),
            "Density grid width must be between 1 and 1024 inclusive; actual: 0.");
        ContractAssert.Throws<DensityBinningValidationException>(
            () => new DensityGridBuffer(2, 1025),
            "Density grid height must be between 1 and 1024 inclusive; actual: 1025.");
        ContractAssert.Throws<DensityBinningValidationException>(
            () => binner.Bin(
                frame,
                new DensityBinningRequest(
                    new DensityAxisOptions(24, DisplayTransformKind.Linear),
                    new DensityAxisOptions(1, DisplayTransformKind.Linear),
                    DensityOutlierMode.Exclude),
                destination),
            "X feature index must be between 0 and 23 inclusive; actual: 24.");
        ContractAssert.Throws<DensityBinningValidationException>(
            () => binner.Bin(
                frame,
                new DensityBinningRequest(
                    new DensityAxisOptions(
                        0,
                        DisplayTransformKind.Linear,
                        new DensityRange(2.0, 1.0)),
                    new DensityAxisOptions(1, DisplayTransformKind.Linear),
                    DensityOutlierMode.Exclude),
                destination),
            "X range minimum must be less than or equal to maximum; actual: 2 > 1.");
        ContractAssert.Throws<DensityBinningValidationException>(
            () => binner.Bin(
                frame,
                new DensityBinningRequest(
                    new DensityAxisOptions(
                        0,
                        DisplayTransformKind.Linear,
                        new DensityRange(double.NaN, 1.0)),
                    new DensityAxisOptions(1, DisplayTransformKind.Linear),
                    DensityOutlierMode.Exclude),
                destination),
            "X range bounds must contain only finite values.");
        ContractAssert.Throws<DensityBinningValidationException>(
            () => binner.Bin(
                frame,
                new DensityBinningRequest(
                    new DensityAxisOptions(0, DisplayTransformKind.Linear),
                    new DensityAxisOptions(1, DisplayTransformKind.Linear),
                    (DensityOutlierMode)999),
                destination),
            "Unsupported density outlier mode: 999.");

        var undersized = new DensityBinner(maximumEventCount: 0);
        ContractAssert.Throws<DensityBinningValidationException>(
            () => undersized.Bin(frame, CreateRequest(0.0, 1.0), destination),
            "Frame event count exceeds binner capacity; frame: 1, capacity: 0.");
    }

    private static void GivenHardFixture_WhenBinnedTwice_ThenCountsEdgesAndRawValuesAreIdentical()
    {
        // Given: The hard 100,001-event fixture and two independent 512-square destinations.
        const int eventCount = 100_001;
        var frame = SyntheticAggregateFrameFactory.Create(eventCount, seed: 42, generation: 7);
        var firstRawBits = frame.PulseFeatureValues.Span[..PulseFeatureSchema.ColumnCount]
            .ToArray()
            .Select(BitConverter.DoubleToInt64Bits)
            .ToArray();
        var request = new DensityBinningRequest(
            new DensityAxisOptions(0, DisplayTransformKind.Linear),
            new DensityAxisOptions(1, DisplayTransformKind.Linear),
            DensityOutlierMode.Exclude);
        var first = new DensityGridBuffer(512, 512);
        var second = new DensityGridBuffer(512, 512);
        var binner = new DensityBinner(eventCount);

        // When: Binning the same immutable frame twice.
        binner.Bin(frame, request, first);
        binner.Bin(frame, request, second);

        // Then: Counts, edge bits, conservation, generation, and raw bits are deterministic.
        ContractAssert.Equal(eventCount, first.BinnedEventCount);
        ContractAssert.Equal(eventCount, first.Counts.Span.ToArray().Sum());
        ContractAssert.Equal(262_144, first.Counts.Length);
        ContractAssert.Equal(7L, first.Generation);
        ContractAssert.SequenceEqual(first.Counts.ToArray(), second.Counts.ToArray());
        ContractAssert.SequenceEqual(
            first.XEdges.Span.ToArray().Select(BitConverter.DoubleToInt64Bits),
            second.XEdges.Span.ToArray().Select(BitConverter.DoubleToInt64Bits));
        ContractAssert.SequenceEqual(
            first.YEdges.Span.ToArray().Select(BitConverter.DoubleToInt64Bits),
            second.YEdges.Span.ToArray().Select(BitConverter.DoubleToInt64Bits));
        ContractAssert.SequenceEqual(
            firstRawBits,
            frame.PulseFeatureValues.Span[..PulseFeatureSchema.ColumnCount]
                .ToArray()
                .Select(BitConverter.DoubleToInt64Bits));
    }

    private static DensityBinner CreateBinner(AggregateFrame frame)
    {
        return new DensityBinner(frame.EventCount);
    }

    private static DensityBinningRequest CreateRequest(
        double minimum,
        double maximum,
        DensityOutlierMode outlierMode = DensityOutlierMode.Exclude)
    {
        var range = new DensityRange(minimum, maximum);
        return new DensityBinningRequest(
            new DensityAxisOptions(0, DisplayTransformKind.Linear, range),
            new DensityAxisOptions(1, DisplayTransformKind.Linear, range),
            outlierMode);
    }

    private static AggregateFrame CreateFrame(params (double X, double Y)[] points)
    {
        var values = new double[checked(points.Length * PulseFeatureSchema.ColumnCount)];
        for (var eventIndex = 0; eventIndex < points.Length; eventIndex++)
        {
            var offset = eventIndex * PulseFeatureSchema.ColumnCount;
            values[offset] = points[eventIndex].X;
            values[offset + 1] = points[eventIndex].Y;
        }

        return AggregateFrame.Create(
            PulseFeatureSchema.Version,
            generation: 1,
            points.Length,
            PulseFeatureSchema.Columns,
            values);
    }
}
