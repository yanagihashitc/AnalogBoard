using System.IO;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using AnalogBoard.ScatterRendering.Core;
using AnalogBoard.ScatterRendering.Wpf;

namespace AnalogBoard.ScatterRendering.Tests;

internal sealed record PerformanceBuildIdentity(
    string MeasuredSourceTreeSha256,
    string GitExecutablePath,
    string GitExecutableSha256,
    string Configuration,
    string TargetFramework,
    string Platform,
    string PlatformTarget,
    string SdkVersion)
{
    private const string Absent = "<absent>";
    private const string PrototypeRelativePath = "prototypes/scatter-rendering";
    private const string CoreAssemblyRelativePath =
        "prototypes/scatter-rendering/tests/AnalogBoard.ScatterRendering.Tests/" +
        "bin/x64/Release/net10.0-windows/AnalogBoard.ScatterRendering.Core.dll";
    private const string WpfAssemblyRelativePath =
        "prototypes/scatter-rendering/tests/AnalogBoard.ScatterRendering.Tests/" +
        "bin/x64/Release/net10.0-windows/AnalogBoard.ScatterRendering.Wpf.dll";
    private static readonly string[] RequiredExternalPaths =
    [
        ".editorconfig",
        ".gitattributes",
        "global.json",
        "docs/reference/scatter-rendering/phase0/display-transform-contract-v1.json",
        "docs/reference/scatter-rendering/phase0/density-raster-contract-v1.json",
        "docs/reference/scatter-rendering/phase0/gmi-raster-contract-v1.json",
    ];
    private static readonly string[] OptionalExternalPaths =
    [
        "docs/reference/scatter-rendering/phase0/performance-reference-profile-v1.json",
        "Directory.Build.props",
        "Directory.Build.targets",
        "Directory.Packages.props",
        "prototypes/Directory.Build.props",
        "prototypes/Directory.Build.targets",
        "prototypes/Directory.Packages.props",
    ];

    public static PerformanceBuildIdentity Capture() =>
        Capture(typeof(PerformanceBuildIdentity).Assembly);

    internal static PerformanceBuildIdentity Capture(Assembly assembly)
    {
        ArgumentNullException.ThrowIfNull(assembly);
        var metadata = assembly
            .GetCustomAttributes<AssemblyMetadataAttribute>()
            .Where(attribute => attribute.Key is not null && attribute.Value is not null)
            .ToDictionary(
                attribute => attribute.Key!,
                attribute => attribute.Value!,
                StringComparer.Ordinal);
        return new PerformanceBuildIdentity(
            RequiredSha256(metadata, "P0R1MeasuredSourceTreeSha256"),
            RequiredAbsoluteFile(metadata, "P0R1GitExecutablePath"),
            RequiredSha256(metadata, "P0R1GitExecutableSha256"),
            RequiredExact(metadata, "P0R1Configuration", "Release"),
            RequiredExact(metadata, "P0R1TargetFramework", "net10.0-windows"),
            RequiredExact(metadata, "P0R1Platform", "x64"),
            RequiredExact(metadata, "P0R1PlatformTarget", "x64"),
            RequiredExact(metadata, "P0R1SdkVersion", "10.0.302"));
    }

    public static void RequireCurrentSource(
        string repositoryRoot,
        string executingAssemblyPath,
        string gitExecutablePath)
    {
        RequireCurrentSource(
            repositoryRoot,
            executingAssemblyPath,
            gitExecutablePath,
            Capture());
    }

    internal static void RequireCurrentSource(
        string repositoryRoot,
        string executingAssemblyPath,
        string gitExecutablePath,
        PerformanceBuildIdentity identity)
    {
        ArgumentNullException.ThrowIfNull(identity);
        var currentSource = ComputeMeasuredSourceTree(repositoryRoot);
        if (!StringComparer.Ordinal.Equals(
                identity.MeasuredSourceTreeSha256,
                currentSource.Sha256))
        {
            throw new PerformanceMeasurementException(
                "Official performance executable was not built from the current measured source tree.");
        }

        RequireSamePath(
            executingAssemblyPath,
            typeof(PerformanceBuildIdentity).Assembly.Location,
            "Official performance executing assembly identity is inconsistent.");
        RequireSamePath(
            gitExecutablePath,
            identity.GitExecutablePath,
            "Official performance Git executable differs from the build identity.");
        var actualGitHash = Sha256File(gitExecutablePath);
        if (!StringComparer.Ordinal.Equals(actualGitHash, identity.GitExecutableSha256))
        {
            throw new PerformanceMeasurementException(
                "Official performance Git executable hash differs from the build identity.");
        }

        RequireDependencyAssembly(
            identity,
            typeof(AggregateFrame).Assembly,
            Path.Combine(
                Path.GetFullPath(repositoryRoot),
                CoreAssemblyRelativePath.Replace('/', Path.DirectorySeparatorChar)),
            "Core");
        RequireDependencyAssembly(
            identity,
            typeof(WriteableBitmapDensitySurface).Assembly,
            Path.Combine(
                Path.GetFullPath(repositoryRoot),
                WpfAssemblyRelativePath.Replace('/', Path.DirectorySeparatorChar)),
            "WPF");
    }

    private static void RequireDependencyAssembly(
        PerformanceBuildIdentity expectedIdentity,
        Assembly dependencyAssembly,
        string expectedPath,
        string label) =>
        RequireDependencyIdentity(
            expectedIdentity,
            Capture(dependencyAssembly),
            dependencyAssembly.Location,
            expectedPath,
            label);

    internal static void RequireDependencyIdentity(
        PerformanceBuildIdentity expectedIdentity,
        PerformanceBuildIdentity actualIdentity,
        string actualPath,
        string expectedPath,
        string label)
    {
        RequireSamePath(
            actualPath,
            expectedPath,
            $"Official performance {label} dependency is outside the canonical Release x64 output.");
        if (actualIdentity != expectedIdentity)
        {
            throw new PerformanceMeasurementException(
                $"Official performance {label} dependency build identity differs from the executing assembly.");
        }
    }

    public static PerformanceMeasuredSourceTree ComputeMeasuredSourceTree(string repositoryRoot)
    {
        var entries = GetMeasuredSourceEntries(repositoryRoot);
        var material = new StringBuilder();
        foreach (var entry in entries)
        {
            material.Append(entry.Key).Append('=').Append(entry.Value).Append('\n');
        }

        var hash = Convert.ToHexString(
            SHA256.HashData(new UTF8Encoding(encoderShouldEmitUTF8Identifier: false)
                .GetBytes(material.ToString()))).ToLowerInvariant();
        return new PerformanceMeasuredSourceTree(
            entries.Values.Count(value => !StringComparer.Ordinal.Equals(value, Absent)),
            hash);
    }

    internal static IReadOnlyList<string> GetMeasuredSourcePaths(string repositoryRoot) =>
        GetMeasuredSourceEntries(repositoryRoot)
            .Where(entry => !StringComparer.Ordinal.Equals(entry.Value, Absent))
            .Select(entry => entry.Key)
            .ToArray();

    internal static IReadOnlyList<string> GetMeasuredSourceAuthorityPathspecs() =>
        [PrototypeRelativePath, .. RequiredExternalPaths, .. OptionalExternalPaths];

    private static SortedDictionary<string, string> GetMeasuredSourceEntries(
        string repositoryRoot)
    {
        var root = Path.GetFullPath(repositoryRoot);
        var prototypeRoot = Path.Combine(
            root,
            PrototypeRelativePath.Replace('/', Path.DirectorySeparatorChar));
        if (!Directory.Exists(prototypeRoot))
        {
            throw new PerformanceMeasurementException(
                "P0-R1 measured source root is absent.");
        }

        var entries = new SortedDictionary<string, string>(StringComparer.Ordinal);
        foreach (var file in Directory.EnumerateFiles(
                     prototypeRoot,
                     "*",
                     SearchOption.AllDirectories))
        {
            var relative = NormalizeRelative(root, file);
            if (relative.Contains("/bin/", StringComparison.Ordinal) ||
                relative.Contains("/obj/", StringComparison.Ordinal))
            {
                continue;
            }

            entries.Add(relative, Sha256File(file));
        }

        foreach (var relative in RequiredExternalPaths)
        {
            var path = Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar));
            if (!File.Exists(path))
            {
                throw new PerformanceMeasurementException(
                    $"P0-R1 measured external input is absent: {relative}.");
            }

            entries.Add(relative, Sha256File(path));
        }

        foreach (var relative in OptionalExternalPaths)
        {
            AddOptional(entries, root, relative);
        }

        return entries;
    }

    private static void AddOptional(
        IDictionary<string, string> entries,
        string root,
        string relative)
    {
        var path = Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar));
        entries.Add(relative, File.Exists(path) ? Sha256File(path) : Absent);
    }

    private static string NormalizeRelative(string root, string path) =>
        Path.GetRelativePath(root, path).Replace('\\', '/');

    private static string RequiredSha256(
        IReadOnlyDictionary<string, string> metadata,
        string key)
    {
        if (!metadata.TryGetValue(key, out var value) ||
            value.Length != 64 ||
            !value.All(character => Uri.IsHexDigit(character)))
        {
            throw new PerformanceMeasurementException(
                $"Performance build metadata is missing a SHA-256 identity: {key}.");
        }

        return value.ToLowerInvariant();
    }

    private static string RequiredAbsoluteFile(
        IReadOnlyDictionary<string, string> metadata,
        string key)
    {
        if (!metadata.TryGetValue(key, out var value) ||
            !Path.IsPathFullyQualified(value) ||
            !File.Exists(value))
        {
            throw new PerformanceMeasurementException(
                $"Performance build metadata is missing an absolute file identity: {key}.");
        }

        return Path.GetFullPath(value);
    }

    private static string RequiredExact(
        IReadOnlyDictionary<string, string> metadata,
        string key,
        string expected)
    {
        if (!metadata.TryGetValue(key, out var value) ||
            !StringComparer.Ordinal.Equals(value, expected))
        {
            throw new PerformanceMeasurementException(
                $"Performance build metadata has an invalid exact identity: {key}.");
        }

        return value;
    }

    private static string Sha256File(string path) =>
        Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();

    private static void RequireSamePath(string actual, string expected, string message)
    {
        if (!StringComparer.OrdinalIgnoreCase.Equals(
                Path.TrimEndingDirectorySeparator(Path.GetFullPath(actual)),
                Path.TrimEndingDirectorySeparator(Path.GetFullPath(expected))))
        {
            throw new PerformanceMeasurementException(message);
        }
    }
}

internal sealed record PerformanceMeasuredSourceTree(int FileCount, string Sha256);
