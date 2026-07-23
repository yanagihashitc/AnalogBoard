using System.IO;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;
using AnalogBoard.ScatterRendering.Core;

namespace AnalogBoard.ScatterRendering.Tests;

internal static class DisplayTransformContractTests
{
    private const string FixtureSha256 =
        "5955f5b3ec649c490cd1bc752b1515fb33f5b7170212f0e221ca7552acb4adaf";

    public static IReadOnlyList<ContractTestCase> Cases { get; } =
    [
        new(nameof(GivenIndependentAsinhFixture_WhenApplied_ThenForwardInverseAndBitsMatch),
            GivenIndependentAsinhFixture_WhenApplied_ThenForwardInverseAndBitsMatch),
        new(nameof(GivenPinnedLinearAndLogVectors_WhenApplied_ThenRawInputsRemainUnchanged),
            GivenPinnedLinearAndLogVectors_WhenApplied_ThenRawInputsRemainUnchanged),
        new(nameof(GivenPinnedBiexponentialVectors_WhenApplied_ThenReferencePositionsAndInverseMatch),
            GivenPinnedBiexponentialVectors_WhenApplied_ThenReferencePositionsAndInverseMatch),
        new(nameof(GivenMixedNegativeValues_WhenBiexponentialFitted_ThenAutoWidthMatchesAuthority),
            GivenMixedNegativeValues_WhenBiexponentialFitted_ThenAutoWidthMatchesAuthority),
        new(nameof(GivenNonFiniteValue_WhenAnyTransformBoundaryUsed_ThenTypedFailure),
            GivenNonFiniteValue_WhenAnyTransformBoundaryUsed_ThenTypedFailure),
        new(nameof(GivenOverlappingBuffers_WhenTransformUsed_ThenOnlyExactInPlaceForwardIsAllowed),
            GivenOverlappingBuffers_WhenTransformUsed_ThenOnlyExactInPlaceForwardIsAllowed),
        new(nameof(GivenExtremeFiniteBiexponentialValue_WhenApplied_ThenOverflowFailsLoud),
            GivenExtremeFiniteBiexponentialValue_WhenApplied_ThenOverflowFailsLoud),
        new(nameof(GivenInvalidTransformOrBufferShape_WhenApplied_ThenTypedFailure),
            GivenInvalidTransformOrBufferShape_WhenApplied_ThenTypedFailure),
    ];

    private static void GivenIndependentAsinhFixture_WhenApplied_ThenForwardInverseAndBitsMatch()
    {
        // Given: Owner-pinned asinh vectors generated independently by CPython 3.12.2.
        var path = Path.Combine(
            AppContext.BaseDirectory,
            "Fixtures",
            "display-transform-contract-v1.json");
        var bytes = File.ReadAllBytes(path);
        var fixture = JsonSerializer.Deserialize<AsinhFixture>(bytes)
            ?? throw new InvalidOperationException("Asinh fixture must deserialize.");
        var transform = FittedDisplayTransform.Fit(
            DisplayTransformKind.Asinh,
            ReadOnlySpan<double>.Empty,
            Span<double>.Empty);

        // When: Applying forward and inverse to every independent fixture case.
        var hash = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();

        // Then: Identity, provenance, values, tolerances, and negative-zero bits match.
        ContractAssert.Equal(FixtureSha256, hash);
        ContractAssert.Equal("AB-DISPLAY-TRANSFORM-v1", fixture.ContractId);
        ContractAssert.Equal(150.0, fixture.Cofactor);
        ContractAssert.Equal("independent_standard_library", fixture.Provenance.AuthorityKind);
        ContractAssert.Equal("CPython", fixture.Provenance.Implementation);
        ContractAssert.Equal("3.12.2", fixture.Provenance.PythonVersion);
        ContractAssert.SequenceEqual(
            new[] { "math.asinh", "math.sinh" },
            fixture.Provenance.Functions);

        foreach (var fixtureCase in fixture.Cases)
        {
            ContractAssert.Equal(
                fixtureCase.RawIeee754BigEndianHex,
                ToBigEndianHex(fixtureCase.Raw));

            var display = transform.Forward(fixtureCase.Raw);
            var inverse = transform.Inverse(fixtureCase.ForwardExpected);

            Approximately(
                fixtureCase.ForwardExpected,
                display,
                fixture.Tolerances.ForwardAbsolute);
            Approximately(
                fixtureCase.InverseReference,
                inverse,
                fixtureCase.InverseTolerance);
            ContractAssert.Equal(
                fixtureCase.ForwardIeee754BigEndianHex,
                ToBigEndianHex(fixtureCase.ForwardExpected));
            ContractAssert.Equal(
                fixtureCase.InverseIeee754BigEndianHex,
                ToBigEndianHex(fixtureCase.InverseReference));

            if (fixtureCase.Id == "negative_zero")
            {
                ContractAssert.Equal(long.MinValue, BitConverter.DoubleToInt64Bits(display));
                ContractAssert.Equal(long.MinValue, BitConverter.DoubleToInt64Bits(inverse));
            }
        }
    }

    private static void GivenPinnedLinearAndLogVectors_WhenApplied_ThenRawInputsRemainUnchanged()
    {
        // Given: Pinned sys_app linear/log values, including negative zero and the log floor.
        var raw = new[] { -0.0, -1.0, 0.0, 1e-6, 1.0, 10.0, 262_144.0 };
        var rawBits = raw.Select(BitConverter.DoubleToInt64Bits).ToArray();
        var linearOutput = new double[raw.Length];
        var logOutput = new double[raw.Length];
        var linear = FittedDisplayTransform.Fit(
            DisplayTransformKind.Linear,
            raw,
            new double[raw.Length]);
        var log = FittedDisplayTransform.Fit(
            DisplayTransformKind.Log,
            raw,
            new double[raw.Length]);

        // When: Applying both display-only transforms into separate destinations.
        linear.Forward(raw, linearOutput);
        log.Forward(raw, logOutput);

        // Then: Source/gate coordinates remain bitwise unchanged and pinned outputs match.
        ContractAssert.SequenceEqual(rawBits, raw.Select(BitConverter.DoubleToInt64Bits));
        ContractAssert.SequenceEqual(rawBits, linearOutput.Select(BitConverter.DoubleToInt64Bits));
        var expectedLog = new[]
        {
            -6.0,
            -6.0,
            -6.0,
            -6.0,
            0.0,
            1.0,
            5.418539921951662,
        };
        for (var index = 0; index < expectedLog.Length; index++)
        {
            Approximately(expectedLog[index], logOutput[index], 1e-12);
        }
    }

    private static void GivenPinnedBiexponentialVectors_WhenApplied_ThenReferencePositionsAndInverseMatch()
    {
        // Given: A non-negative fit that resolves the pinned default W=0.5 coefficients.
        var fitValues = new[] { 0.0, 100.0, 1_000.0, 10_000.0, 262_144.0 };
        var scratch = new double[fitValues.Length];
        var transform = FittedDisplayTransform.Fit(
            DisplayTransformKind.Biexponential,
            fitValues,
            scratch);
        var raw = new[] { -100.0, -0.0, 0.0, 100.0, 1_000.0, 10_000.0, 262_144.0 };
        var expected = new[]
        {
            0.006910398448326305,
            0.11111111111085847,
            0.11111111111085847,
            0.21318108753030174,
            0.4543375761727475,
            0.6838326572265032,
            1.0,
        };

        // When: Projecting and inverting the fixed reference values.
        var display = new double[raw.Length];
        transform.Forward(raw, display);

        // Then: Positions and inverse values match the pinned GatingML authority.
        Approximately(0.5, transform.BiexponentialWidth, 1e-12);
        for (var index = 0; index < raw.Length; index++)
        {
            Approximately(expected[index], display[index], 1e-6);
            Approximately(raw[index], transform.Inverse(expected[index]), 1e-5);
        }
    }

    private static void GivenMixedNegativeValues_WhenBiexponentialFitted_ThenAutoWidthMatchesAuthority()
    {
        // Given: Mixed values whose fifth-percentile negative spread widens the linear region.
        var values = new[] { -500.0, -100.0, -10.0, 0.0, 100.0 };
        var transform = FittedDisplayTransform.Fit(
            DisplayTransformKind.Biexponential,
            values,
            new double[values.Length]);

        // When: Reading the resolved width and projecting unsorted duplicates.
        var repeated = new[] { 100.0, -10.0, 100.0, 0.0, -10.0, 10_000.0 };
        var output = new double[repeated.Length];
        transform.Forward(repeated, output);

        // Then: Exact auto-W interpolation, order, and duplicate mapping are retained.
        Approximately(0.8721089548649563, transform.BiexponentialWidth, 1e-12);
        Approximately(output[0], output[2], 1e-12);
        Approximately(output[1], output[4], 1e-12);
        ContractAssert.Equal(true, output[1] < output[3]);
        ContractAssert.Equal(true, output[3] < output[0]);
        ContractAssert.Equal(true, output[0] < output[5]);
    }

    private static void GivenNonFiniteValue_WhenAnyTransformBoundaryUsed_ThenTypedFailure()
    {
        // Given: Every display transform and every IEEE-754 non-finite category.
        var kinds = Enum.GetValues<DisplayTransformKind>();
        var nonFinite = new[] { double.NaN, double.PositiveInfinity, double.NegativeInfinity };

        // When/Then: Fit and scalar forward boundaries fail loud with one typed contract.
        foreach (var kind in kinds)
        {
            foreach (var value in nonFinite)
            {
                ContractAssert.Throws<DisplayTransformValidationException>(
                    () => FittedDisplayTransform.Fit(kind, new[] { value }, new double[1]),
                    "Transform input must contain only finite values.");

                var transform = FittedDisplayTransform.Fit(
                    kind,
                    ReadOnlySpan<double>.Empty,
                    Span<double>.Empty);
                ContractAssert.Throws<DisplayTransformValidationException>(
                    () => transform.Forward(value),
                    "Transform input must contain only finite values.");
            }
        }
    }

    private static void GivenInvalidTransformOrBufferShape_WhenApplied_ThenTypedFailure()
    {
        // Given: An invalid enum, an undersized biexponential scratch, and mismatched output.
        var values = new[] { 1.0, 2.0 };

        // When/Then: Every unsupported shape is rejected deterministically.
        ContractAssert.Throws<DisplayTransformValidationException>(
            () => FittedDisplayTransform.Fit(
                (DisplayTransformKind)999,
                values,
                new double[values.Length]),
            "Unsupported display transform kind: 999.");
        ContractAssert.Throws<DisplayTransformValidationException>(
            () => FittedDisplayTransform.Fit(
                DisplayTransformKind.Biexponential,
                values,
                new double[values.Length - 1]),
            "Biexponential fit scratch length must be at least the input length; input: 2, scratch: 1.");

        var transform = FittedDisplayTransform.Fit(
            DisplayTransformKind.Linear,
            values,
            new double[values.Length]);
        ContractAssert.Throws<DisplayTransformValidationException>(
            () => transform.Forward(values, new double[values.Length - 1]),
            "Destination length must equal source length; source: 2, destination: 1.");
    }

    private static void GivenOverlappingBuffers_WhenTransformUsed_ThenOnlyExactInPlaceForwardIsAllowed()
    {
        // Given: Exact in-place and shifted-overlap transform buffers.
        var inPlace = new[] { 1.0, 10.0 };
        var linear = FittedDisplayTransform.Fit(
            DisplayTransformKind.Linear,
            inPlace,
            new double[inPlace.Length]);

        // When: Applying exact in-place forward transformation.
        linear.Forward(inPlace, inPlace);

        // Then: Exact alias is safe, while shifted forward and fit overlaps fail loud.
        ContractAssert.SequenceEqual(new[] { 1.0, 10.0 }, inPlace);
        ContractAssert.Throws<DisplayTransformValidationException>(
            InvokeShiftedForwardOverlap,
            "Source and destination may be identical but must not partially overlap; element offset: 1.");
        ContractAssert.Throws<DisplayTransformValidationException>(
            InvokeExactBiexponentialFitAlias,
            "Biexponential fit scratch must not overlap the input values.");
        ContractAssert.Throws<DisplayTransformValidationException>(
            InvokeShiftedBiexponentialFitOverlap,
            "Biexponential fit scratch must not overlap the input values.");
    }

    private static void GivenExtremeFiniteBiexponentialValue_WhenApplied_ThenOverflowFailsLoud()
    {
        // Given: The pinned default-W biexponential transform and a finite 1e300 value.
        var fitValues = new[] { 0.0, 1.0 };
        var transform = FittedDisplayTransform.Fit(
            DisplayTransformKind.Biexponential,
            fitValues,
            new double[fitValues.Length]);

        // When/Then: Exponential bracket overflow is reported instead of using Infinity.
        ContractAssert.Throws<DisplayTransformValidationException>(
            () => transform.Forward(1e300),
            "Biexponential transform exceeded the finite binary64 range.");
    }

    private static void InvokeShiftedForwardOverlap()
    {
        var buffer = new[] { 1.0, 10.0, 0.0 };
        var transform = FittedDisplayTransform.Fit(
            DisplayTransformKind.Log,
            buffer.AsSpan(0, 2),
            new double[2]);
        transform.Forward(buffer.AsSpan(0, 2), buffer.AsSpan(1, 2));
    }

    private static void InvokeExactBiexponentialFitAlias()
    {
        var buffer = new[] { -100.0, 10.0 };
        FittedDisplayTransform.Fit(
            DisplayTransformKind.Biexponential,
            buffer,
            buffer);
    }

    private static void InvokeShiftedBiexponentialFitOverlap()
    {
        var buffer = new[] { -100.0, 10.0, 100.0 };
        FittedDisplayTransform.Fit(
            DisplayTransformKind.Biexponential,
            buffer.AsSpan(0, 2),
            buffer.AsSpan(1, 2));
    }

    private static void Approximately(double expected, double actual, double tolerance)
    {
        if (double.IsNaN(actual) || Math.Abs(expected - actual) > tolerance)
        {
            throw new InvalidOperationException(
                $"Expected '{expected:R}' within '{tolerance:R}', actual '{actual:R}'.");
        }
    }

    private static string ToBigEndianHex(double value)
    {
        var bytes = BitConverter.GetBytes(value);
        if (BitConverter.IsLittleEndian)
        {
            Array.Reverse(bytes);
        }
        return Convert.ToHexString(bytes).ToLowerInvariant();
    }

    private sealed record AsinhFixture(
        [property: JsonPropertyName("contract_id")] string ContractId,
        [property: JsonPropertyName("cofactor")] double Cofactor,
        [property: JsonPropertyName("tolerances")] AsinhTolerances Tolerances,
        [property: JsonPropertyName("provenance")] AsinhProvenance Provenance,
        [property: JsonPropertyName("cases")] IReadOnlyList<AsinhCase> Cases);

    private sealed record AsinhTolerances(
        [property: JsonPropertyName("forward_absolute")] double ForwardAbsolute);

    private sealed record AsinhProvenance(
        [property: JsonPropertyName("authority_kind")] string AuthorityKind,
        [property: JsonPropertyName("implementation")] string Implementation,
        [property: JsonPropertyName("python_version")] string PythonVersion,
        [property: JsonPropertyName("functions")] IReadOnlyList<string> Functions);

    private sealed record AsinhCase(
        [property: JsonPropertyName("id")] string Id,
        [property: JsonPropertyName("raw")] double Raw,
        [property: JsonPropertyName("raw_ieee754_be_hex")] string RawIeee754BigEndianHex,
        [property: JsonPropertyName("forward_expected")] double ForwardExpected,
        [property: JsonPropertyName("forward_ieee754_be_hex")] string ForwardIeee754BigEndianHex,
        [property: JsonPropertyName("inverse_reference")] double InverseReference,
        [property: JsonPropertyName("inverse_ieee754_be_hex")] string InverseIeee754BigEndianHex,
        [property: JsonPropertyName("inverse_tolerance")] double InverseTolerance);
}
