namespace AnalogBoard.ScatterRendering.Tests;

internal static class Program
{
    public static int Main()
    {
        return TestRunner.Run(
            AggregateFrameContractTests.Cases.Concat(
                PerformanceMetricSchemaContractTests.Cases).Concat(
                TestRunnerContractTests.Cases));
    }
}
