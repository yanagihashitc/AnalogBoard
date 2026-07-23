namespace AnalogBoard.ScatterRendering.Core;

public static class PulseFeatureSchema
{
    private static readonly string[] CanonicalColumns =
    [
        "FSC_A", "FSC_H", "FSC_W",
        "SSC_A", "SSC_H", "SSC_W",
        "FL1_A", "FL1_H", "FL1_W",
        "FL2_A", "FL2_H", "FL2_W",
        "FL3_A", "FL3_H", "FL3_W",
        "FL4_A", "FL4_H", "FL4_W",
        "FL5_A", "FL5_H", "FL5_W",
        "FL6_A", "FL6_H", "FL6_W",
    ];

    public const int Version = 1;

    public const int ColumnCount = 24;

    public static IReadOnlyList<string> Columns { get; } =
        Array.AsReadOnly(CanonicalColumns);

    internal static bool IsCanonicalOrder(IReadOnlyList<string> columns)
    {
        if (columns.Count != CanonicalColumns.Length)
        {
            return false;
        }

        for (var index = 0; index < CanonicalColumns.Length; index++)
        {
            if (!string.Equals(
                    columns[index],
                    CanonicalColumns[index],
                    StringComparison.Ordinal))
            {
                return false;
            }
        }

        return true;
    }
}
