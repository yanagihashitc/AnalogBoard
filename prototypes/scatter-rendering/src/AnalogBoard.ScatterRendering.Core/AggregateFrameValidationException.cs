namespace AnalogBoard.ScatterRendering.Core;

public sealed class AggregateFrameValidationException : Exception
{
    public AggregateFrameValidationException(string message)
        : base(message)
    {
    }
}
