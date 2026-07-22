namespace AnalogBoard.ScatterRendering.Core;

public static class FrameGenerationGuard
{
    public static void EnsureNewer(long previousGeneration, long actualGeneration)
    {
        if (actualGeneration <= previousGeneration)
        {
            throw new AggregateFrameValidationException(
                $"Generation must increase monotonically; previous: {previousGeneration}, actual: {actualGeneration}.");
        }
    }
}
