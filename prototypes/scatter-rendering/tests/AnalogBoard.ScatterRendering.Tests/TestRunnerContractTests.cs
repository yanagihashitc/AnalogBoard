namespace AnalogBoard.ScatterRendering.Tests;

internal static class TestRunnerContractTests
{
    public static IReadOnlyList<ContractTestCase> Cases { get; } =
    [
        new(nameof(GivenPassingCases_WhenRun_ThenExactBoundedSummaryIsEmitted),
            GivenPassingCases_WhenRun_ThenExactBoundedSummaryIsEmitted),
        new(nameof(GivenFailingCase_WhenRun_ThenExactBoundedSummaryIsEmitted),
            GivenFailingCase_WhenRun_ThenExactBoundedSummaryIsEmitted),
    ];

    private static void GivenPassingCases_WhenRun_ThenExactBoundedSummaryIsEmitted()
    {
        // Given: Two successful bounded contract cases.
        var cases = new[]
        {
            new ContractTestCase("passing-one", () => { }),
            new ContractTestCase("passing-two", () => { }),
        };

        // When: Running them through the self-hosted runner.
        var result = RunAndCapture(cases);

        // Then: Success emits exactly one machine-readable bounded summary line.
        ContractAssert.Equal(0, result.ExitCode);
        ContractAssert.SequenceEqual(
            ["SUMMARY total=2 passed=2 failed=0"],
            SummaryLines(result.StandardOutput));
    }

    private static void GivenFailingCase_WhenRun_ThenExactBoundedSummaryIsEmitted()
    {
        // Given: One successful and one intentionally failing bounded contract case.
        var cases = new[]
        {
            new ContractTestCase("passing", () => { }),
            new ContractTestCase(
                "failing",
                () => throw new InvalidOperationException("intentional failure")),
        };

        // When: Running them through the self-hosted runner.
        var result = RunAndCapture(cases);

        // Then: Failure still emits exactly one summary with matching counts.
        ContractAssert.Equal(1, result.ExitCode);
        ContractAssert.SequenceEqual(
            ["SUMMARY total=2 passed=1 failed=1"],
            SummaryLines(result.StandardOutput));
    }

    private static CapturedRun RunAndCapture(IEnumerable<ContractTestCase> cases)
    {
        var originalOutput = Console.Out;
        var originalError = Console.Error;
        using var standardOutput = new StringWriter();
        using var standardError = new StringWriter();

        try
        {
            Console.SetOut(standardOutput);
            Console.SetError(standardError);
            var exitCode = TestRunner.Run(cases);
            return new CapturedRun(
                exitCode,
                standardOutput.ToString(),
                standardError.ToString());
        }
        finally
        {
            Console.SetOut(originalOutput);
            Console.SetError(originalError);
        }
    }

    private static string[] SummaryLines(string output)
    {
        return output
            .Replace("\r\n", "\n", StringComparison.Ordinal)
            .Split('\n', StringSplitOptions.RemoveEmptyEntries)
            .Where(line => line.StartsWith("SUMMARY ", StringComparison.Ordinal))
            .ToArray();
    }

    private sealed record CapturedRun(
        int ExitCode,
        string StandardOutput,
        string StandardError);
}
