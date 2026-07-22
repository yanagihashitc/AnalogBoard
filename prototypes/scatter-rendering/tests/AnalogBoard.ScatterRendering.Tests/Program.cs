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
                GmiSnapshotContractTests.Cases).Concat(
                GmiRasterContractTests.Cases).Concat(
                RasterFrameLeaseContractTests.Cases).Concat(
                WriteableBitmapSurfaceContractTests.Cases).Concat(
                LatestFrameSchedulerContractTests.Cases).Concat(
                CombinedVisualizationHarnessContractTests.Cases).Concat(
                DevelopmentPerformanceObservationTests.Cases).Concat(
                Batch4DevelopmentObservationTests.Cases).Concat(
                PerformanceMetricSchemaContractTests.Cases).Concat(
                TestRunnerContractTests.Cases));
    }
}
