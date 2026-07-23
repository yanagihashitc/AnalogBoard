using System.Windows.Threading;
using AnalogBoard.ScatterRendering.Core;

namespace AnalogBoard.ScatterRendering.Wpf;

public sealed class DispatcherUiWorkPoster : IUiWorkPoster
{
    private readonly Dispatcher _dispatcher;

    public DispatcherUiWorkPoster(Dispatcher dispatcher)
    {
        ArgumentNullException.ThrowIfNull(dispatcher);
        _dispatcher = dispatcher;
    }

    public bool CheckAccess() => _dispatcher.CheckAccess();

    public bool TryPost(Action callback)
    {
        ArgumentNullException.ThrowIfNull(callback);
        if (_dispatcher.HasShutdownStarted || _dispatcher.HasShutdownFinished)
        {
            return false;
        }

        var operation = _dispatcher.BeginInvoke(
            DispatcherPriority.Background,
            callback);
        return operation.Status != DispatcherOperationStatus.Aborted;
    }
}
