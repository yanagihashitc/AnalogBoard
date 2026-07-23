using System.Globalization;
using System.Resources;

namespace AnalogBoard.ScatterRendering.Wpf;

internal static class PrototypeStrings
{
    private const string MissingResourceMessageKey = "RequiredPrototypeResourceAbsent";

    private static readonly ResourceManager ResourceManager = new(
        "AnalogBoard.ScatterRendering.Wpf.Properties.Resources",
        typeof(PrototypeStrings).Assembly);

    public static string Get(string key)
    {
        var value = ResourceManager.GetString(key, CultureInfo.CurrentUICulture);
        if (value is not null)
        {
            return value;
        }

        var missingResourceFormat = ResourceManager.GetString(
            MissingResourceMessageKey,
            CultureInfo.CurrentUICulture);
        if (missingResourceFormat is null)
        {
            throw new MissingManifestResourceException();
        }

        throw new MissingManifestResourceException(
            string.Format(CultureInfo.CurrentCulture, missingResourceFormat, key));
    }

    public static string Format(string key, params object[] arguments) =>
        string.Format(CultureInfo.CurrentCulture, Get(key), arguments);
}
