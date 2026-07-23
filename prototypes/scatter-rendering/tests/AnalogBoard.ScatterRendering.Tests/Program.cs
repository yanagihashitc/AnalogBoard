using System.IO;
using System.Text.Json;

namespace AnalogBoard.ScatterRendering.Tests;

internal static class Program
{
    [STAThread]
    public static int Main(string[] args)
    {
        if (args.Length != 0)
        {
            try
            {
                if (args.Length >= 2 &&
                    StringComparer.Ordinal.Equals(args[0], "perf") &&
                    StringComparer.Ordinal.Equals(args[1], "finalize"))
                {
                    return PerformanceSuiteFinalizer.Execute(
                        PerformanceFinalizeCommandLine.Parse(args));
                }

                return OfficialPerformanceRunner.Execute(
                    PerformanceCommandLine.Parse(args));
            }
            catch (PerformanceCommandLineException exception)
            {
                Console.Error.WriteLine($"PERFORMANCE_COMMAND_ERROR {exception.Message}");
                return PerformanceExitCodes.CommandLineContract;
            }
            catch (PerformanceProfileException exception)
            {
                Console.Error.WriteLine($"PERFORMANCE_PROFILE_ERROR {exception.Message}");
                return PerformanceExitCodes.ReferenceProfile;
            }
            catch (PerformanceMeasurementException exception)
            {
                Console.Error.WriteLine($"PERFORMANCE_MEASUREMENT_ERROR {exception}");
                return PerformanceExitCodes.MeasurementInvalid;
            }
            catch (PerformanceArtifactException exception)
            {
                Console.Error.WriteLine($"PERFORMANCE_ARTIFACT_ERROR {exception}");
                return PerformanceExitCodes.ArtifactFailure;
            }
            catch (IOException exception)
            {
                Console.Error.WriteLine($"PERFORMANCE_IO_ERROR {exception}");
                return PerformanceExitCodes.ArtifactFailure;
            }
            catch (JsonException exception)
            {
                Console.Error.WriteLine($"PERFORMANCE_JSON_ERROR {exception}");
                return PerformanceExitCodes.MeasurementInvalid;
            }
        }

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
                OfficialPerformanceContractTests.Cases).Concat(
                PerformanceMetricSchemaContractTests.Cases).Concat(
                TestRunnerContractTests.Cases));
    }
}
