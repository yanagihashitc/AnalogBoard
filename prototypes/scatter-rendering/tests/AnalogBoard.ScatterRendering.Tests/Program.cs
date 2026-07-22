namespace AnalogBoard.ScatterRendering.Tests;

internal static class Program
{
    [STAThread]
    public static int Main()
    {
        return TestRunner.Run(
            AggregateFrameContractTests.Cases.Concat(
                DisplayTransformContractTests.Cases).Concat(
                DensityBinningContractTests.Cases).Concat(
                DensityRasterContractTests.Cases).Concat(
                WriteableBitmapSurfaceContractTests.Cases).Concat(
                LatestFrameSchedulerContractTests.Cases).Concat(
                DevelopmentPerformanceObservationTests.Cases).Concat(
                PerformanceMetricSchemaContractTests.Cases).Concat(
                TestRunnerContractTests.Cases));
    }
}
