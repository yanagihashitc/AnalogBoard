namespace AnalogBoard.ScatterRendering.Core;

public readonly struct FittedDisplayTransform
{
    public const double AsinhCofactor = 150.0;
    public const double LogFloor = 1e-6;

    private const double BiexponentialTopScale = 262_144.0;
    private const double BiexponentialDecades = 4.5;
    private const double DefaultBiexponentialWidth = 0.5;
    private const double DisplayLowerBound = -0.5;
    private const double DisplayUpperBound = 1.5;
    private const double RootTolerance = 1e-12;
    private const int RootMaximumIterations = 96;

    private readonly BiexponentialCoefficients _coefficients;

    private FittedDisplayTransform(
        DisplayTransformKind kind,
        double biexponentialWidth,
        BiexponentialCoefficients coefficients)
    {
        Kind = kind;
        BiexponentialWidth = biexponentialWidth;
        _coefficients = coefficients;
    }

    public DisplayTransformKind Kind { get; }

    public double BiexponentialWidth { get; }

    public static FittedDisplayTransform Fit(
        DisplayTransformKind kind,
        ReadOnlySpan<double> values,
        Span<double> scratch)
    {
        ValidateKind(kind);
        ValidateFinite(values);

        if (kind != DisplayTransformKind.Biexponential)
        {
            return new FittedDisplayTransform(kind, 0.0, default);
        }
        if (scratch.Length < values.Length)
        {
            throw new DisplayTransformValidationException(
                $"Biexponential fit scratch length must be at least the input length; input: {values.Length}, scratch: {scratch.Length}.");
        }
        if (values.Overlaps((ReadOnlySpan<double>)scratch))
        {
            throw new DisplayTransformValidationException(
                "Biexponential fit scratch must not overlap the input values.");
        }

        var width = ResolveBiexponentialWidth(values, scratch);
        return new FittedDisplayTransform(
            kind,
            width,
            ComputeBiexponentialCoefficients(width));
    }

    internal static FittedDisplayTransform FitFromOwnedFiniteBuffer(
        DisplayTransformKind kind,
        Span<double> ownedValues)
    {
        ValidateKind(kind);
        ValidateFinite(ownedValues);
        if (kind != DisplayTransformKind.Biexponential)
        {
            return new FittedDisplayTransform(kind, 0.0, default);
        }

        var width = ResolveBiexponentialWidth(ownedValues, ownedValues);
        return new FittedDisplayTransform(
            kind,
            width,
            ComputeBiexponentialCoefficients(width));
    }

    public double Forward(double rawValue)
    {
        ValidateFinite(rawValue);
        return ForwardFinite(rawValue);
    }

    public void Forward(ReadOnlySpan<double> source, Span<double> destination)
    {
        if (destination.Length != source.Length)
        {
            throw new DisplayTransformValidationException(
                $"Destination length must equal source length; source: {source.Length}, destination: {destination.Length}.");
        }
        if (source.Overlaps((ReadOnlySpan<double>)destination, out var elementOffset) &&
            elementOffset != 0)
        {
            throw new DisplayTransformValidationException(
                $"Source and destination may be identical but must not partially overlap; element offset: {elementOffset}.");
        }

        ValidateFinite(source);
        for (var index = 0; index < source.Length; index++)
        {
            destination[index] = ForwardFinite(source[index]);
        }
    }

    public double Inverse(double displayValue)
    {
        ValidateFinite(displayValue);

        var result = Kind switch
        {
            DisplayTransformKind.Linear => displayValue,
            DisplayTransformKind.Log => Math.Pow(10.0, displayValue),
            DisplayTransformKind.Biexponential => ValueFromDisplay(displayValue),
            DisplayTransformKind.Asinh => AsinhCofactor * Math.Sinh(displayValue),
            _ => throw UnsupportedKind(Kind),
        };
        if (!double.IsFinite(result))
        {
            throw new DisplayTransformValidationException(
                "Transform result must remain within the finite binary64 range.");
        }
        return result;
    }

    private static void ValidateKind(DisplayTransformKind kind)
    {
        if (kind is < DisplayTransformKind.Linear or > DisplayTransformKind.Asinh)
        {
            throw UnsupportedKind(kind);
        }
    }

    private static DisplayTransformValidationException UnsupportedKind(
        DisplayTransformKind kind)
    {
        return new DisplayTransformValidationException(
            $"Unsupported display transform kind: {(int)kind}.");
    }

    private static void ValidateFinite(ReadOnlySpan<double> values)
    {
        foreach (var value in values)
        {
            ValidateFinite(value);
        }
    }

    private static void ValidateFinite(double value)
    {
        if (!double.IsFinite(value))
        {
            throw new DisplayTransformValidationException(
                "Transform input must contain only finite values.");
        }
    }

    private double ForwardFinite(double rawValue)
    {
        return Kind switch
        {
            DisplayTransformKind.Linear => rawValue,
            DisplayTransformKind.Log => Math.Log10(Math.Max(rawValue, LogFloor)),
            DisplayTransformKind.Biexponential => SolveDisplayPosition(rawValue),
            DisplayTransformKind.Asinh => Math.Asinh(rawValue / AsinhCofactor),
            _ => throw UnsupportedKind(Kind),
        };
    }

    private static double ResolveBiexponentialWidth(
        ReadOnlySpan<double> values,
        Span<double> scratch)
    {
        var negativeCount = 0;
        foreach (var value in values)
        {
            if (value < 0.0)
            {
                scratch[negativeCount] = value;
                negativeCount++;
            }
        }
        if (negativeCount == 0)
        {
            return DefaultBiexponentialWidth;
        }

        var negatives = scratch[..negativeCount];
        negatives.Sort();
        var representative = Math.Abs(Percentile(negatives, 5.0));
        if (representative <= 0.0)
        {
            return DefaultBiexponentialWidth;
        }

        var estimate = (
            BiexponentialDecades - Math.Log10(BiexponentialTopScale / representative)) / 2.0;
        return Math.Clamp(
            estimate,
            DefaultBiexponentialWidth,
            BiexponentialDecades / 2.0);
    }

    private static double Percentile(ReadOnlySpan<double> sortedValues, double percentile)
    {
        if (sortedValues.Length == 1)
        {
            return sortedValues[0];
        }

        var rank = (sortedValues.Length - 1) * (percentile / 100.0);
        var lowerIndex = (int)Math.Floor(rank);
        var upperIndex = (int)Math.Ceiling(rank);
        if (lowerIndex == upperIndex)
        {
            return sortedValues[lowerIndex];
        }

        var fraction = rank - lowerIndex;
        return sortedValues[lowerIndex] +
            ((sortedValues[upperIndex] - sortedValues[lowerIndex]) * fraction);
    }

    private static BiexponentialCoefficients ComputeBiexponentialCoefficients(
        double width)
    {
        const double additionalNegativeDecades = 0.0;
        var totalDecades = BiexponentialDecades + additionalNegativeDecades;
        var normalizedWidth = width / totalDecades;
        var x2 = additionalNegativeDecades / totalDecades;
        var x1 = x2 + normalizedWidth;
        var x0 = x2 + (2.0 * normalizedWidth);
        var b = totalDecades * Math.Log(10.0);
        var d = SolveBiexponentialD(b, normalizedWidth);
        var cOverA = Math.Exp(x0 * (b + d));
        var fOverA = Math.Exp(b * x1) - (cOverA * Math.Exp(-d * x1));
        var a = BiexponentialTopScale /
            (Math.Exp(b) - fOverA - (cOverA * Math.Exp(-d)));
        var c = cOverA * a;
        var f = fOverA * a;
        return new BiexponentialCoefficients(a, b, c, d, f);
    }

    private static double SolveBiexponentialD(double b, double normalizedWidth)
    {
        static double Equation(double dValue, double bValue, double widthValue)
        {
            return (2.0 * (Math.Log(dValue) - Math.Log(bValue))) +
                (widthValue * (dValue + bValue));
        }

        var lower = 1e-12;
        var upper = b;
        var lowerValue = Equation(lower, b, normalizedWidth);

        for (var iteration = 0; iteration < RootMaximumIterations; iteration++)
        {
            var midpoint = (lower + upper) / 2.0;
            var midpointValue = Equation(midpoint, b, normalizedWidth);
            if (Math.Abs(midpointValue) <= RootTolerance ||
                (upper - lower) <= RootTolerance)
            {
                return midpoint;
            }

            if (HasOppositeSign(lowerValue, midpointValue))
            {
                upper = midpoint;
            }
            else
            {
                lower = midpoint;
                lowerValue = midpointValue;
            }
        }
        return (lower + upper) / 2.0;
    }

    private double SolveDisplayPosition(double rawValue)
    {
        var lower = DisplayLowerBound;
        var upper = DisplayUpperBound;
        var lowerValue = ValueFromDisplay(lower) - rawValue;
        var upperValue = ValueFromDisplay(upper) - rawValue;
        var step = 0.5;

        while (lowerValue > 0.0)
        {
            upper = lower;
            upperValue = lowerValue;
            lower -= step;
            lowerValue = ValueFromDisplay(lower) - rawValue;
            step *= 2.0;
        }

        step = 0.5;
        while (upperValue < 0.0)
        {
            lower = upper;
            lowerValue = upperValue;
            upper += step;
            upperValue = ValueFromDisplay(upper) - rawValue;
            step *= 2.0;
        }

        if (lowerValue == 0.0)
        {
            return lower;
        }
        if (upperValue == 0.0)
        {
            return upper;
        }

        for (var iteration = 0; iteration < RootMaximumIterations; iteration++)
        {
            var midpoint = (lower + upper) / 2.0;
            var midpointValue = ValueFromDisplay(midpoint) - rawValue;
            if (Math.Abs(midpointValue) <= RootTolerance ||
                (upper - lower) <= RootTolerance)
            {
                return midpoint;
            }

            if (HasOppositeSign(lowerValue, midpointValue))
            {
                upper = midpoint;
            }
            else
            {
                lower = midpoint;
                lowerValue = midpointValue;
            }
        }
        return (lower + upper) / 2.0;
    }

    private double ValueFromDisplay(double position)
    {
        var positive = _coefficients.A * Math.Exp(_coefficients.B * position);
        var negative = _coefficients.C * Math.Exp(-_coefficients.D * position);
        var result = positive - negative - _coefficients.F;
        if (!double.IsFinite(positive) ||
            !double.IsFinite(negative) ||
            !double.IsFinite(result))
        {
            throw new DisplayTransformValidationException(
                "Biexponential transform exceeded the finite binary64 range.");
        }
        return result;
    }

    private static bool HasOppositeSign(double left, double right)
    {
        return BitConverter.DoubleToInt64Bits(left) < 0 !=
            BitConverter.DoubleToInt64Bits(right) < 0;
    }

    private readonly record struct BiexponentialCoefficients(
        double A,
        double B,
        double C,
        double D,
        double F);
}
