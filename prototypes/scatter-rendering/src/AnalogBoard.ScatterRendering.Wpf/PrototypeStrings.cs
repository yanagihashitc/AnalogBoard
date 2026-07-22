using System.Globalization;
using System.Resources;

namespace AnalogBoard.ScatterRendering.Wpf;

internal static class PrototypeStrings
{
    private static readonly ResourceManager ResourceManager = new(
        "AnalogBoard.ScatterRendering.Wpf.Properties.Resources",
        typeof(PrototypeStrings).Assembly);

    public static string Get(string key) =>
        ResourceManager.GetString(key, CultureInfo.CurrentUICulture)
        ?? throw new MissingManifestResourceException(
            $"Required prototype resource is absent: {key}.");

    public static string Format(string key, params object[] arguments) =>
        string.Format(CultureInfo.CurrentCulture, Get(key), arguments);
}
