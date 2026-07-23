namespace AnalogBoard.ScatterRendering.Core;

public sealed class DisplayTransformValidationException : ArgumentException
{
    public DisplayTransformValidationException(string message)
        : base(message)
    {
    }
}
