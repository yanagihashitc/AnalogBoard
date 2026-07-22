using System.Diagnostics;
using System.Security.Cryptography;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;
using AnalogBoard.ScatterRendering.Core;

namespace AnalogBoard.ScatterRendering.Tests;

internal enum PerformanceExecutionMode
{
    Official = 0,
    DryRun = 1,
}

internal enum PerformanceScenario
{
    HardScatter = 0,
    HardCombined = 1,
    Soak = 2,
    Headroom = 3,
}

internal enum PerformanceVerdictStatus
{
    Pass = 0,
    Fail = 1,
    Incomplete = 2,
    Observed = 3,
}

internal static class PerformanceRunEligibility
{
    public static bool IsEligible(
        PerformanceExecutionMode mode,
        PerformanceScenario scenario,
        bool profileEligible) =>
        mode == PerformanceExecutionMode.Official &&
        scenario != PerformanceScenario.Headroom &&
        profileEligible;
}

internal sealed record PerformanceRunSchedule(
    TimeSpan Warmup,
    TimeSpan Measurement,
    int HardRunCount,
    TimeSpan Soak,
    bool AllowsOverrides,
    string RunnerContractId)
{
    public static PerformanceRunSchedule Official { get; } = new(
        TimeSpan.FromSeconds(30),
        TimeSpan.FromSeconds(60),
        3,
        TimeSpan.FromMinutes(10),
        AllowsOverrides: false,
        "AB-PERF-RUNNER-v1");

    public static PerformanceRunSchedule CreateDryRun(
        int warmupMilliseconds,
        int measurementMilliseconds,
        int soakMilliseconds)
    {
        ValidateDryRunDuration(warmupMilliseconds, "warmup");
        ValidateDryRunDuration(measurementMilliseconds, "measurement");
        ValidateDryRunDuration(soakMilliseconds, "soak");
        return new PerformanceRunSchedule(
            TimeSpan.FromMilliseconds(warmupMilliseconds),
            TimeSpan.FromMilliseconds(measurementMilliseconds),
            HardRunCount: 3,
            TimeSpan.FromMilliseconds(soakMilliseconds),
            AllowsOverrides: true,
            "AB-PERF-RUNNER-v1");
    }

    private static void ValidateDryRunDuration(int milliseconds, string name)
    {
        if (milliseconds < 1 || milliseconds > 10_000)
        {
            throw new PerformanceCommandLineException(
                $"Dry-run {name} duration must be between 1 and 10000 milliseconds; actual: {milliseconds}.");
        }
    }
}

internal sealed record PerformanceRunCommand(
    PerformanceExecutionMode Mode,
    PerformanceScenario Scenario,
    int RunIndex,
    string RepositoryRoot,
    string OutputRoot,
    string OutputPath,
    string? ReferenceProfilePath,
    string ObservedProfilePath,
    string ProvenancePath,
    PerformanceRunSchedule Schedule)
{
    public bool OfficialEligible => Mode == PerformanceExecutionMode.Official;
}

internal static class PerformanceCommandLine
{
    private static readonly HashSet<string> DurationOptions =
    [
        "--warmup-ms",
        "--measurement-ms",
        "--soak-ms",
    ];

    public static PerformanceRunCommand Parse(string[] args)
    {
        ArgumentNullException.ThrowIfNull(args);
        if (args.Length < 2 || !StringComparer.Ordinal.Equals(args[0], "perf"))
        {
            throw new PerformanceCommandLineException(
                "Performance command must start with 'perf official' or 'perf dry-run'.");
        }

        var mode = args[1] switch
        {
            "official" => PerformanceExecutionMode.Official,
            "dry-run" => PerformanceExecutionMode.DryRun,
            _ => throw new PerformanceCommandLineException(
                $"Unknown performance mode: '{args[1]}'."),
        };
        var options = ParseOptions(args.AsSpan(2));
        if (mode == PerformanceExecutionMode.Official &&
            DurationOptions.Any(options.ContainsKey))
        {
            throw new PerformanceCommandLineException(
                "Official performance schedule does not accept duration overrides.");
        }

        var scenario = Required(options, "--scenario") switch
        {
            "hard-scatter" => PerformanceScenario.HardScatter,
            "hard-combined" => PerformanceScenario.HardCombined,
            "soak" => PerformanceScenario.Soak,
            "headroom" => PerformanceScenario.Headroom,
            var value => throw new PerformanceCommandLineException(
                $"Unknown performance scenario: '{value}'."),
        };
        if (!int.TryParse(Required(options, "--run-index"), out var runIndex))
        {
            throw new PerformanceCommandLineException(
                "Performance run index must be an integer.");
        }

        ValidateRunIndex(scenario, runIndex);
        var schedule = mode == PerformanceExecutionMode.Official
            ? PerformanceRunSchedule.Official
            : PerformanceRunSchedule.CreateDryRun(
                RequiredInteger(options, "--warmup-ms"),
                RequiredInteger(options, "--measurement-ms"),
                RequiredInteger(options, "--soak-ms"));
        var allowed = new HashSet<string>(
            [
                "--scenario",
                "--run-index",
                "--repository-root",
                "--output-root",
                "--output",
                "--reference-profile",
                "--observed-profile",
                "--provenance",
                .. mode == PerformanceExecutionMode.DryRun
                    ? DurationOptions
                    : [],
            ],
            StringComparer.Ordinal);
        var unknown = options.Keys.FirstOrDefault(key => !allowed.Contains(key));
        if (unknown is not null)
        {
            throw new PerformanceCommandLineException(
                $"Unknown performance option: '{unknown}'.");
        }

        var referenceProfile = Optional(options, "--reference-profile");
        if (mode == PerformanceExecutionMode.Official && referenceProfile is null)
        {
            throw new PerformanceCommandLineException(
                "Official performance mode requires --reference-profile.");
        }

        return new PerformanceRunCommand(
            mode,
            scenario,
            runIndex,
            Required(options, "--repository-root"),
            Required(options, "--output-root"),
            Required(options, "--output"),
            referenceProfile,
            Required(options, "--observed-profile"),
            Required(options, "--provenance"),
            schedule);
    }

    private static Dictionary<string, string> ParseOptions(ReadOnlySpan<string> args)
    {
        if (args.Length % 2 != 0)
        {
            throw new PerformanceCommandLineException(
                "Every performance option requires one value.");
        }

        var options = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var index = 0; index < args.Length; index += 2)
        {
            var name = args[index];
            if (!name.StartsWith("--", StringComparison.Ordinal))
            {
                throw new PerformanceCommandLineException(
                    $"Performance option name must start with '--': '{name}'.");
            }

            if (!options.TryAdd(name, args[index + 1]))
            {
                throw new PerformanceCommandLineException(
                    $"Performance option must not be repeated: '{name}'.");
            }
        }

        return options;
    }

    private static string Required(
        IReadOnlyDictionary<string, string> options,
        string name)
    {
        if (!options.TryGetValue(name, out var value) || string.IsNullOrWhiteSpace(value))
        {
            throw new PerformanceCommandLineException(
                $"Performance option is required: '{name}'.");
        }

        return value;
    }

    private static string? Optional(
        IReadOnlyDictionary<string, string> options,
        string name) =>
        options.TryGetValue(name, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : null;

    private static int RequiredInteger(
        IReadOnlyDictionary<string, string> options,
        string name)
    {
        var value = Required(options, name);
        if (!int.TryParse(value, out var parsed))
        {
            throw new PerformanceCommandLineException(
                $"Performance option must be an integer: '{name}'.");
        }

        return parsed;
    }

    private static void ValidateRunIndex(PerformanceScenario scenario, int runIndex)
    {
        var valid = scenario is PerformanceScenario.HardScatter or PerformanceScenario.HardCombined
            ? runIndex is >= 1 and <= 3
            : runIndex == 1;
        if (!valid)
        {
            throw new PerformanceCommandLineException(
                $"Performance run index is invalid for {scenario}: {runIndex}.");
        }
    }
}

internal static class PerformancePathAuthority
{
    private const string CanonicalOutputRelativePath = "artifacts/phase0-scatter-rendering";
    private const string CanonicalReferenceRelativePath =
        "docs/reference/scatter-rendering/phase0/performance-reference-profile-v1.json";

    public static void ValidateRun(PerformanceRunCommand command)
    {
        ArgumentNullException.ThrowIfNull(command);
        var repositoryRoot = ValidateRepositoryRoot(command.RepositoryRoot);
        var outputRoot = ValidateOutputRoot(
            repositoryRoot,
            command.OutputRoot,
            command.Mode);
        var outputPath = Path.GetFullPath(command.OutputPath);
        var runsDirectory = Path.GetDirectoryName(outputPath)
            ?? throw new PerformanceArtifactException(
                "Performance raw output path has no parent directory.");
        var sessionDirectory = Path.GetDirectoryName(runsDirectory)
            ?? throw new PerformanceArtifactException(
                "Performance raw output path has no session directory.");
        ValidateSessionDirectory(outputRoot, sessionDirectory);
        RequireSamePath(runsDirectory, Path.Combine(sessionDirectory, "runs"), "runs directory");
        RequireSamePath(
            outputPath,
            Path.Combine(runsDirectory, ExpectedRawFileName(command.Scenario, command.RunIndex)),
            "raw output");
        RequireSamePath(
            command.ObservedProfilePath,
            Path.Combine(sessionDirectory, "profile.actual.json"),
            "observed profile");
        RequireSamePath(
            command.ProvenancePath,
            Path.Combine(sessionDirectory, "provenance.json"),
            "provenance");
        if (command.Mode == PerformanceExecutionMode.Official)
        {
            RequireSamePath(
                command.ReferenceProfilePath!,
                Path.Combine(repositoryRoot, CanonicalReferenceRelativePath),
                "official reference profile");
        }
    }

    public static string ValidateFinalize(PerformanceFinalizeCommand command)
    {
        ArgumentNullException.ThrowIfNull(command);
        var repositoryRoot = ValidateRepositoryRoot(command.RepositoryRoot);
        var outputRoot = ValidateOutputRoot(
            repositoryRoot,
            command.OutputRoot,
            command.Mode);
        var sessionDirectory = Path.GetFullPath(command.SessionDirectory);
        ValidateSessionDirectory(outputRoot, sessionDirectory);
        RequireSamePath(
            command.ObservedProfilePath,
            Path.Combine(sessionDirectory, "profile.actual.json"),
            "observed profile");
        RequireSamePath(
            command.FinalObservedProfilePath,
            Path.Combine(sessionDirectory, "profile.final.json"),
            "final observed profile");
        RequireSamePath(
            command.ProvenancePath,
            Path.Combine(sessionDirectory, "provenance.json"),
            "provenance");
        RequireSamePath(
            command.ProcessExitsPath,
            Path.Combine(sessionDirectory, "process-exits.json"),
            "process exits");
        if (command.Mode == PerformanceExecutionMode.Official)
        {
            RequireSamePath(
                command.ReferenceProfilePath!,
                Path.Combine(repositoryRoot, CanonicalReferenceRelativePath),
                "official reference profile");
        }

        return sessionDirectory;
    }

    private static string ValidateRepositoryRoot(string path)
    {
        var repositoryRoot = Path.GetFullPath(path);
        var gitEntry = Path.Combine(repositoryRoot, ".git");
        if (!Directory.Exists(repositoryRoot) ||
            (!Directory.Exists(gitEntry) && !File.Exists(gitEntry)))
        {
            throw new PerformanceArtifactException(
                "Performance repository root must contain a Git administrative entry.");
        }

        return repositoryRoot;
    }

    private static string ValidateOutputRoot(
        string repositoryRoot,
        string path,
        PerformanceExecutionMode mode)
    {
        var outputRoot = Path.GetFullPath(path);
        if (mode == PerformanceExecutionMode.Official)
        {
            RequireSamePath(
                outputRoot,
                Path.Combine(repositoryRoot, CanonicalOutputRelativePath),
                "official output root");
        }

        return outputRoot;
    }

    private static void ValidateSessionDirectory(string outputRoot, string sessionDirectory)
    {
        if (!sessionDirectory.EndsWith(".inprogress", StringComparison.Ordinal) ||
            !SamePath(Path.GetDirectoryName(sessionDirectory), outputRoot))
        {
            throw new PerformanceArtifactException(
                "Performance session must be one direct '.inprogress' child of the authorized output root.");
        }
    }

    private static string ExpectedRawFileName(
        PerformanceScenario scenario,
        int runIndex) => scenario switch
    {
        PerformanceScenario.HardScatter => $"hard-scatter-{runIndex:D2}.raw.json",
        PerformanceScenario.HardCombined => $"hard-combined-{runIndex:D2}.raw.json",
        PerformanceScenario.Soak => "soak-01.raw.json",
        PerformanceScenario.Headroom => "headroom-01.raw.json",
        _ => throw new PerformanceArtifactException(
            $"Unsupported performance scenario path: {scenario}."),
    };

    private static void RequireSamePath(string actual, string expected, string description)
    {
        if (!SamePath(actual, expected))
        {
            throw new PerformanceArtifactException(
                $"Performance {description} is outside its canonical session contract.");
        }
    }

    private static bool SamePath(string? left, string? right) =>
        left is not null &&
        right is not null &&
        StringComparer.OrdinalIgnoreCase.Equals(
            Path.TrimEndingDirectorySeparator(Path.GetFullPath(left)),
            Path.TrimEndingDirectorySeparator(Path.GetFullPath(right)));
}

internal static class PerformanceGitAuthority
{
    private const string OfficialPreflightMarker = "AB-PERF-RUNNER-v1";
    private const string CanonicalReferenceRelativePath =
        "docs/reference/scatter-rendering/phase0/performance-reference-profile-v1.json";
    private const string CanonicalAssemblyRelativePath =
        "prototypes/scatter-rendering/tests/AnalogBoard.ScatterRendering.Tests/" +
        "bin/x64/Release/net10.0-windows/AnalogBoard.ScatterRendering.Tests.dll";

    public static void RequireHead(string repositoryRoot, string expectedRevision)
    {
        var actual = ReadHead(repositoryRoot);
        if (!StringComparer.OrdinalIgnoreCase.Equals(actual, expectedRevision))
        {
            throw new PerformanceMeasurementException(
                $"Performance provenance source revision does not match repository HEAD: expected {expectedRevision}, actual {actual}.");
        }
    }

    public static void RequireOfficialState(
        string repositoryRoot,
        string expectedRevision,
        bool declaredSourceDirty,
        string referenceProfilePath)
    {
        if (!StringComparer.Ordinal.Equals(
                Environment.GetEnvironmentVariable("P0R1_OFFICIAL_PREFLIGHT"),
                OfficialPreflightMarker))
        {
            throw new PerformanceMeasurementException(
                "Official performance C# entry requires the repository preflight wrapper.");
        }

        var gitExecutable = Environment.GetEnvironmentVariable("P0R1_GIT_EXECUTABLE");
        if (string.IsNullOrWhiteSpace(gitExecutable) ||
            !Path.IsPathFullyQualified(gitExecutable) ||
            !File.Exists(gitExecutable))
        {
            throw new PerformanceMeasurementException(
                "Official performance requires an absolute Git executable from the preflight wrapper.");
        }

        var executingAssemblyPath = typeof(PerformanceGitAuthority).Assembly.Location;
        PerformanceBuildIdentity.RequireCurrentSource(
            repositoryRoot,
            executingAssemblyPath,
            gitExecutable);
        RequireOfficialState(
            repositoryRoot,
            expectedRevision,
            declaredSourceDirty,
            referenceProfilePath,
            arguments => RunGit(gitExecutable, repositoryRoot, arguments),
            executingAssemblyPath);
    }

    internal static void RequireOfficialState(
        string repositoryRoot,
        string expectedRevision,
        bool declaredSourceDirty,
        string referenceProfilePath,
        Func<IReadOnlyList<string>, PerformanceGitCommandResult> invokeGit,
        string? executingAssemblyPath = null)
    {
        ArgumentNullException.ThrowIfNull(invokeGit);
        var root = Path.GetFullPath(repositoryRoot);
        var assemblyPath = Path.GetFullPath(
            executingAssemblyPath ?? typeof(PerformanceGitAuthority).Assembly.Location);
        RequireSamePath(
            assemblyPath,
            Path.Combine(root, CanonicalAssemblyRelativePath),
            "Official performance executable is outside the canonical Release x64 output.");
        RequireSamePath(
            referenceProfilePath,
            Path.Combine(root, CanonicalReferenceRelativePath),
            "Official performance reference profile is outside its canonical path.");
        if (declaredSourceDirty)
        {
            throw new PerformanceMeasurementException(
                "Official performance provenance requires a clean source revision.");
        }

        var topLevel = RequireSuccessfulGit(
            invokeGit,
            CreateAuthorityGitArguments("rev-parse", "--show-toplevel"),
            "resolve the repository root");
        if (topLevel.Count != 1 || !SamePath(topLevel[0], root))
        {
            throw new PerformanceMeasurementException(
                "Official performance repository root does not match Git show-toplevel.");
        }

        var revision = RequireSuccessfulGit(
            invokeGit,
            CreateAuthorityGitArguments("rev-parse", "HEAD"),
            "resolve HEAD");
        if (revision.Count != 1 ||
            !StringComparer.OrdinalIgnoreCase.Equals(revision[0], expectedRevision))
        {
            throw new PerformanceMeasurementException(
                "Official performance provenance source revision does not match Git HEAD.");
        }

        var status = RequireSuccessfulGit(
            invokeGit,
            CreateAuthorityGitArguments(
                "status",
                "--porcelain=v1",
                "--untracked-files=normal"),
            "derive source cleanliness");
        if (status.Count != 0)
        {
            throw new PerformanceMeasurementException(
                "Official performance requires an empty Git status.");
        }

        var measuredSourcePaths = PerformanceBuildIdentity.GetMeasuredSourcePaths(root);
        var headSourceLines = RequireSuccessfulGit(
                invokeGit,
                CreateAuthorityGitArguments(
                [
                    "ls-tree",
                    "-r",
                    "HEAD",
                    "--",
                    .. PerformanceBuildIdentity.GetMeasuredSourceAuthorityPathspecs(),
                ]),
                "resolve the measured source blobs at HEAD");
        var headSourceBlobs = ParseHeadSourceBlobs(headSourceLines);
        var headMeasuredSources = headSourceBlobs.Keys.ToArray();
        if (!measuredSourcePaths.SequenceEqual(
                headMeasuredSources,
                StringComparer.Ordinal))
        {
            throw new PerformanceMeasurementException(
                "Measured performance source paths must exactly match HEAD.");
        }

        var workingSourceOids = RequireSuccessfulGit(
            invokeGit,
                CreateAuthorityGitArguments(
                    ["hash-object", "--no-filters", "--", .. measuredSourcePaths]),
            "hash the measured working source inputs");
        if (workingSourceOids.Count != measuredSourcePaths.Count)
        {
            throw new PerformanceMeasurementException(
                "Measured performance working source hash count is inconsistent.");
        }
        for (var index = 0; index < measuredSourcePaths.Count; index++)
        {
            if (!StringComparer.OrdinalIgnoreCase.Equals(
                    workingSourceOids[index],
                    headSourceBlobs[measuredSourcePaths[index]]))
            {
                throw new PerformanceMeasurementException(
                    "Measured performance working source bytes must match HEAD blobs.");
            }
        }

        var expectedIndexStates = measuredSourcePaths
            .Select(path => $"H {path}")
            .ToArray();
        var actualIndexStates = RequireSuccessfulGit(
                invokeGit,
                CreateAuthorityGitArguments(
                [
                    "ls-files",
                    "-v",
                    "--full-name",
                    "--",
                    .. measuredSourcePaths,
                ]),
                "resolve the measured source index states")
            .Select(line => line.Replace('\\', '/'))
            .Order(StringComparer.Ordinal)
            .ToArray();
        if (!expectedIndexStates.SequenceEqual(actualIndexStates, StringComparer.Ordinal))
        {
            throw new PerformanceMeasurementException(
                "Measured performance source inputs must not use assume-unchanged or skip-worktree index flags.");
        }

        var tracked = invokeGit(CreateAuthorityGitArguments(
            "ls-files",
            "--error-unmatch",
            "--full-name",
            "--",
            CanonicalReferenceRelativePath));
        if (tracked.ExitCode != 0 ||
            tracked.StandardOutput.Count != 1 ||
            !StringComparer.Ordinal.Equals(
                tracked.StandardOutput[0].Replace('\\', '/'),
                CanonicalReferenceRelativePath))
        {
            throw new PerformanceMeasurementException(
                "Official performance reference profile must be tracked at HEAD.");
        }
    }

    private static PerformanceGitCommandResult RunGit(
        string gitExecutable,
        string repositoryRoot,
        IReadOnlyList<string> arguments)
    {
        var startInfo = new ProcessStartInfo
        {
            FileName = gitExecutable,
            WorkingDirectory = Path.GetFullPath(repositoryRoot),
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        foreach (var argument in arguments)
        {
            startInfo.ArgumentList.Add(argument);
        }

        using var process = Process.Start(startInfo)
            ?? throw new PerformanceMeasurementException(
                "Official performance could not start the pinned Git executable.");
        var standardOutput = ReadNonEmptyLines(process.StandardOutput.ReadToEnd());
        var standardError = ReadNonEmptyLines(process.StandardError.ReadToEnd());
        process.WaitForExit();
        return new PerformanceGitCommandResult(
            process.ExitCode,
            standardOutput,
            standardError);
    }

    private static string[] CreateAuthorityGitArguments(params string[] commandArguments) =>
    [
        "-c",
        "core.fsmonitor=false",
        "-c",
        "core.untrackedCache=false",
        "--no-replace-objects",
        .. commandArguments,
    ];

    private static SortedDictionary<string, string> ParseHeadSourceBlobs(
        IReadOnlyList<string> lines)
    {
        var result = new SortedDictionary<string, string>(StringComparer.Ordinal);
        foreach (var line in lines)
        {
            var tab = line.IndexOf('\t');
            if (tab <= 0)
            {
                throw new PerformanceMeasurementException(
                    "Measured performance HEAD tree output is invalid.");
            }

            var identity = line[..tab].Split(' ', StringSplitOptions.RemoveEmptyEntries);
            var path = line[(tab + 1)..].Replace('\\', '/');
            if (identity.Length != 3 ||
                !StringComparer.Ordinal.Equals(identity[1], "blob") ||
                identity[2].Length is not (40 or 64) ||
                !identity[2].All(Uri.IsHexDigit) ||
                !result.TryAdd(path, identity[2].ToLowerInvariant()))
            {
                throw new PerformanceMeasurementException(
                    "Measured performance HEAD tree output is invalid.");
            }
        }

        return result;
    }

    private static IReadOnlyList<string> RequireSuccessfulGit(
        Func<IReadOnlyList<string>, PerformanceGitCommandResult> invokeGit,
        IReadOnlyList<string> arguments,
        string operation)
    {
        var result = invokeGit(arguments);
        if (result.ExitCode != 0)
        {
            throw new PerformanceMeasurementException(
                $"Official performance Git failed to {operation}: " +
                string.Join(" | ", result.StandardError));
        }

        return result.StandardOutput;
    }

    private static string[] ReadNonEmptyLines(string value) =>
        value.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);

    private static void RequireSamePath(string actual, string expected, string message)
    {
        if (!SamePath(actual, expected))
        {
            throw new PerformanceMeasurementException(message);
        }
    }

    private static bool SamePath(string left, string right) =>
        StringComparer.OrdinalIgnoreCase.Equals(
            Path.TrimEndingDirectorySeparator(Path.GetFullPath(left)),
            Path.TrimEndingDirectorySeparator(Path.GetFullPath(right)));

    private static string ReadHead(string repositoryRoot)
    {
        var gitEntry = Path.Combine(Path.GetFullPath(repositoryRoot), ".git");
        var gitDirectory = Directory.Exists(gitEntry)
            ? gitEntry
            : ResolveGitDirectoryFile(gitEntry);
        var head = File.ReadAllText(Path.Combine(gitDirectory, "HEAD")).Trim();
        if (!head.StartsWith("ref: ", StringComparison.Ordinal))
        {
            return ValidateRevision(head);
        }

        var reference = head[5..].Trim();
        var looseReferencePath = Path.Combine(
            gitDirectory,
            reference.Replace('/', Path.DirectorySeparatorChar));
        if (File.Exists(looseReferencePath))
        {
            return ValidateRevision(File.ReadAllText(looseReferencePath).Trim());
        }

        var packedReferences = Path.Combine(gitDirectory, "packed-refs");
        if (File.Exists(packedReferences))
        {
            foreach (var line in File.ReadLines(packedReferences))
            {
                if (line.Length > 41 &&
                    line[40] == ' ' &&
                    StringComparer.Ordinal.Equals(line[41..], reference))
                {
                    return ValidateRevision(line[..40]);
                }
            }
        }

        throw new PerformanceMeasurementException(
            $"Performance repository HEAD reference cannot be resolved: {reference}.");
    }

    private static string ResolveGitDirectoryFile(string gitEntry)
    {
        if (!File.Exists(gitEntry))
        {
            throw new PerformanceMeasurementException(
                "Performance repository has no Git administrative entry.");
        }

        var value = File.ReadAllText(gitEntry).Trim();
        const string prefix = "gitdir:";
        if (!value.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
        {
            throw new PerformanceMeasurementException(
                "Performance Git administrative file is invalid.");
        }

        var path = value[prefix.Length..].Trim();
        return Path.GetFullPath(path, Path.GetDirectoryName(gitEntry)!);
    }

    private static string ValidateRevision(string revision)
    {
        if (revision.Length != 40 || !revision.All(Uri.IsHexDigit))
        {
            throw new PerformanceMeasurementException(
                "Performance repository HEAD is not a 40-character Git object id.");
        }

        return revision.ToLowerInvariant();
    }
}

internal sealed record PerformanceGitCommandResult(
    int ExitCode,
    IReadOnlyList<string> StandardOutput,
    IReadOnlyList<string> StandardError);

internal sealed class PerformanceCommandLineException : Exception
{
    public PerformanceCommandLineException(string message)
        : base(message)
    {
    }
}

internal sealed record PerformanceMachineProfile(
    string ProfileId,
    string ProfileStatus,
    string OwnerApprovalId,
    IReadOnlyDictionary<string, object> Identity);

internal sealed record PerformanceProfilePreflightResult(
    bool OfficialEligible,
    string Status,
    IReadOnlyList<string> Mismatches);

internal static class PerformanceProfilePreflight
{
    private static readonly string[] StringFields =
    [
        "manufacturer",
        "model",
        "machine_name",
        "os_product",
        "os_version",
        "os_build",
        "cpu",
        "gpu_name",
        "gpu_driver_version",
        "power_scheme_guid",
        "storage_model",
        "storage_serial",
        "storage_bus_type",
        "monotonic_clock",
        "sdk_version",
        "desktop_runtime_version",
        "target_framework",
        "configuration",
        "architecture",
    ];

    private static readonly string[] IntegerFields =
    [
        "ram_bytes",
        "display_width",
        "display_height",
        "display_refresh_hz",
        "display_dpi_x",
        "display_dpi_y",
        "stopwatch_frequency",
    ];

    private static readonly HashSet<string> ExactFields = new(
        [
            "schema_id",
            "profile_id",
            "profile_status",
            "owner_approval_id",
            .. StringFields,
            .. IntegerFields,
            "remote_session",
        ],
        StringComparer.Ordinal);

    public static PerformanceMachineProfile ParseReference(string json) =>
        Parse(json, "owner_pinned", "Official reference");

    public static PerformanceMachineProfile ParseObserved(string json) =>
        Parse(json, "live_observation", "Observed");

    public static PerformanceProfilePreflightResult Compare(
        PerformanceMachineProfile reference,
        PerformanceMachineProfile observed)
    {
        ArgumentNullException.ThrowIfNull(reference);
        ArgumentNullException.ThrowIfNull(observed);
        var mismatches = new List<string>();
        foreach (var expected in reference.Identity)
        {
            if (!observed.Identity.TryGetValue(expected.Key, out var actual) ||
                !Equals(expected.Value, actual))
            {
                mismatches.Add(expected.Key);
            }
        }

        if (!StringComparer.Ordinal.Equals(reference.ProfileId, observed.ProfileId))
        {
            mismatches.Add("profile_id");
        }

        if (observed.Identity.TryGetValue("remote_session", out var remote) &&
            remote is true)
        {
            mismatches.Add("remote_session");
        }

        return mismatches.Count == 0
            ? new PerformanceProfilePreflightResult(true, "eligible", [])
            : new PerformanceProfilePreflightResult(false, "incomplete", mismatches);
    }

    private static PerformanceMachineProfile Parse(
        string json,
        string expectedStatus,
        string description)
    {
        ArgumentNullException.ThrowIfNull(json);
        using var document = JsonDocument.Parse(json);
        if (document.RootElement.ValueKind != JsonValueKind.Object)
        {
            throw new PerformanceProfileException(
                "Performance profile root must be a JSON object.");
        }

        var properties = new Dictionary<string, JsonElement>(StringComparer.Ordinal);
        foreach (var property in document.RootElement.EnumerateObject())
        {
            if (!properties.TryAdd(property.Name, property.Value))
            {
                throw new PerformanceProfileException(
                    $"Performance profile must not contain duplicate JSON property names: {property.Name}.");
            }
        }

        var missing = ExactFields.FirstOrDefault(field => !properties.ContainsKey(field));
        var extra = properties.Keys.FirstOrDefault(field => !ExactFields.Contains(field));
        if (missing is not null)
        {
            throw new PerformanceProfileException(
                $"Performance profile field is required: {missing}.");
        }

        if (extra is not null)
        {
            throw new PerformanceProfileException(
                $"Performance profile field is not allowed: {extra}.");
        }

        RequireExactString(properties, "schema_id", "analogboard.scatter-rendering.reference-profile.v1");
        var profileId = RequireString(properties, "profile_id", description);
        if (!StringComparer.Ordinal.Equals(profileId, "AB-PERF-REF-v1"))
        {
            throw new PerformanceProfileException(
                $"Performance profile id must be AB-PERF-REF-v1; actual: {profileId}.");
        }

        var status = RequireString(properties, "profile_status", description);
        if (!StringComparer.Ordinal.Equals(status, expectedStatus))
        {
            throw new PerformanceProfileException(
                $"Performance profile status must be {expectedStatus}; actual: {status}.");
        }

        var approvalId = RequireString(properties, "owner_approval_id", description);
        var identity = new Dictionary<string, object>(StringComparer.Ordinal);
        foreach (var field in StringFields)
        {
            identity[field] = RequireString(properties, field, description);
        }

        foreach (var field in IntegerFields)
        {
            identity[field] = RequirePositiveInteger(properties, field, description);
        }

        if (properties["remote_session"].ValueKind is not JsonValueKind.True and not JsonValueKind.False)
        {
            throw new PerformanceProfileException(
                $"{description} profile field must be a JSON boolean: remote_session.");
        }

        identity["remote_session"] = properties["remote_session"].GetBoolean();
        RequirePinnedToolchain(identity);
        return new PerformanceMachineProfile(profileId, status, approvalId, identity);
    }

    private static string RequireString(
        IReadOnlyDictionary<string, JsonElement> properties,
        string field,
        string description)
    {
        var value = properties[field];
        if (value.ValueKind != JsonValueKind.String ||
            string.IsNullOrWhiteSpace(value.GetString()))
        {
            throw new PerformanceProfileException(
                $"{description} profile field must be a non-empty string: {field}.");
        }

        return value.GetString()!;
    }

    private static long RequirePositiveInteger(
        IReadOnlyDictionary<string, JsonElement> properties,
        string field,
        string description)
    {
        var value = properties[field];
        if (value.ValueKind != JsonValueKind.Number ||
            !value.TryGetInt64(out var integer) ||
            integer <= 0)
        {
            throw new PerformanceProfileException(
                $"{description} profile field must be a positive integer: {field}.");
        }

        return integer;
    }

    private static void RequireExactString(
        IReadOnlyDictionary<string, JsonElement> properties,
        string field,
        string expected)
    {
        var actual = RequireString(properties, field, "Performance");
        if (!StringComparer.Ordinal.Equals(expected, actual))
        {
            throw new PerformanceProfileException(
                $"Performance profile field {field} must be '{expected}'; actual: '{actual}'.");
        }
    }

    private static void RequirePinnedToolchain(IReadOnlyDictionary<string, object> identity)
    {
        var required = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["sdk_version"] = "10.0.302",
            ["desktop_runtime_version"] = "10.0.10",
            ["target_framework"] = "net10.0-windows",
            ["configuration"] = "Release",
            ["architecture"] = "x64",
            ["monotonic_clock"] = "System.Diagnostics.Stopwatch",
        };
        foreach (var item in required)
        {
            if (!Equals(identity[item.Key], item.Value))
            {
                throw new PerformanceProfileException(
                    $"Performance profile field {item.Key} must be '{item.Value}'; actual: '{identity[item.Key]}'.");
            }
        }
    }
}

internal sealed class PerformanceProfileException : Exception
{
    public PerformanceProfileException(string message)
        : base(message)
    {
    }
}

internal sealed record PerformanceRawRun(
    string SchemaId,
    string RunnerContractId,
    PerformanceExecutionMode Mode,
    PerformanceScenario Scenario,
    int RunIndex,
    bool OfficialEligible,
    long StopwatchFrequency,
    long WindowStartTick,
    long WindowEndTick,
    long[] ScatterFrameDurationTicks,
    long[] ScatterCompletionTicks,
    long[] GmiCompletionTicks,
    long[] InputLatencyTicks,
    long[] ScatterPublicationDurationTicks,
    long[] GmiPublicationDurationTicks,
    int PendingFrameMaximum,
    int PendingCallbackMaximum,
    long CoalescedFrameCount,
    long OverwrittenSampleCount,
    long ProcessAllocatedBytes,
    int AllocationProbeFrameCount,
    long OneEventAllocationProbeBytes,
    long HardEventAllocationProbeBytes,
    long RetainedManagedHeapGrowthBytes,
    long StartingPrivateBytes,
    long PrivateBytesGrowth)
{
    [JsonRequired]
    public long PlannedWindowDurationTicks { get; init; }

    [JsonRequired]
    public int InputCadenceHz { get; init; }

    [JsonRequired]
    public long SchedulerFailureCount { get; init; }

    [JsonRequired]
    public long PendingWorkAtEnd { get; init; }

    [JsonRequired]
    public long MetricSampleCountMismatch { get; init; }

    [JsonRequired]
    public long EventAllocationProbeDeltaBytes { get; init; }

    [JsonRequired]
    public int AllocationProbeInvalidPairCount { get; init; }
}

internal sealed record PerformanceRunVerdict(
    PerformanceScenario Scenario,
    int RunIndex,
    PerformanceVerdictStatus Status,
    bool OfficialCandidate,
    IReadOnlyList<string> FailedMetrics,
    IReadOnlyList<string> IncompleteReasons);

internal static class PerformanceRunEvaluator
{
    private const long RetainedHeapLimitBytes = 8L * 1024 * 1024;
    private const long PrivateBytesAbsoluteLimit = 32L * 1024 * 1024;

    public static PerformanceRunVerdict Evaluate(PerformanceRawRun raw)
    {
        ArgumentNullException.ThrowIfNull(raw);
        if (raw.Scenario == PerformanceScenario.Headroom)
        {
            return new PerformanceRunVerdict(
                raw.Scenario,
                raw.RunIndex,
                PerformanceVerdictStatus.Observed,
                OfficialCandidate: false,
                [],
                []);
        }

        var incomplete = Validate(raw);
        if (incomplete.Count != 0)
        {
            return new PerformanceRunVerdict(
                raw.Scenario,
                raw.RunIndex,
                PerformanceVerdictStatus.Incomplete,
                OfficialCandidate: false,
                [],
                incomplete);
        }

        var failed = new List<string>();
        var frequency = raw.StopwatchFrequency;
        var plannedWindowEnd = checked(raw.WindowStartTick + raw.PlannedWindowDurationTicks);
        var elapsedSeconds = raw.PlannedWindowDurationTicks / (double)frequency;
        var scatterRate = raw.ScatterCompletionTicks.Length / elapsedSeconds;
        AddFailure(failed, scatterRate >= 30.0, PerformanceMetricNames.ScatterDeliveredRate);
        AddFailure(
            failed,
            PercentileMilliseconds(raw.ScatterFrameDurationTicks, 0.95, frequency) <= 33.3,
            PerformanceMetricNames.ScatterFrameTimeP95);
        AddFailure(
            failed,
            MaximumMilliseconds(raw.ScatterFrameDurationTicks, frequency) <= 100.0,
            PerformanceMetricNames.ScatterFrameTimeMax);
        AddFailure(
            failed,
            PercentileMilliseconds(raw.InputLatencyTicks, 0.95, frequency) <= 100.0,
            PerformanceMetricNames.UiInputLatencyP95);
        AddFailure(
            failed,
            MaximumMilliseconds(raw.InputLatencyTicks, frequency) <= 250.0,
            PerformanceMetricNames.UiInputLatencyMax);
        AddFailure(
            failed,
            PercentileMilliseconds(raw.ScatterPublicationDurationTicks, 0.99, frequency) <= 1.0,
            PerformanceMetricNames.ProducerPublicationP99);
        if (raw.Scenario is PerformanceScenario.HardCombined or PerformanceScenario.Soak)
        {
            var gmiRate = raw.GmiCompletionTicks.Length / elapsedSeconds;
            AddFailure(failed, gmiRate >= 5.0, PerformanceMetricNames.GmiDeliveredRate);
            AddFailure(
                failed,
                MaximumIntervalMilliseconds(
                    raw.GmiCompletionTicks,
                    raw.WindowStartTick,
                    plannedWindowEnd,
                    frequency) <= 500.0,
                PerformanceMetricNames.GmiMaxInterval);
            AddFailure(
                failed,
                PercentileMilliseconds(raw.GmiPublicationDurationTicks, 0.99, frequency) <= 1.0,
                PerformanceMetricNames.ProducerPublicationP99);
        }

        AddFailure(
            failed,
            raw.PendingFrameMaximum <= 1,
            PerformanceMetricNames.PendingWorkMax);
        AddFailure(
            failed,
            raw.PendingCallbackMaximum <= 1,
            PerformanceMetricNames.PendingWorkMax);
        var allocationPerFrame = raw.ProcessAllocatedBytes / (double)raw.ScatterCompletionTicks.Length;
        AddFailure(
            failed,
            allocationPerFrame <= 64 * 1024,
            PerformanceMetricNames.ManagedAllocationPerFrame);
        var allocationDelta =
            raw.EventAllocationProbeDeltaBytes / (double)raw.AllocationProbeFrameCount;
        AddFailure(
            failed,
            allocationDelta <= 8 * 1024,
            PerformanceMetricNames.ManagedAllocationEventDeltaPerFrame);
        if (raw.Scenario == PerformanceScenario.Soak)
        {
            AddFailure(
                failed,
                raw.RetainedManagedHeapGrowthBytes <= RetainedHeapLimitBytes,
                PerformanceMetricNames.RetainedManagedHeapGrowth);
            var privateLimit = Math.Max(
                PrivateBytesAbsoluteLimit,
                checked((long)Math.Ceiling(raw.StartingPrivateBytes * 0.10)));
            AddFailure(
                failed,
                raw.PrivateBytesGrowth <= privateLimit,
                PerformanceMetricNames.PrivateBytesGrowth);
        }

        var status = failed.Count == 0
            ? PerformanceVerdictStatus.Pass
            : PerformanceVerdictStatus.Fail;
        return new PerformanceRunVerdict(
            raw.Scenario,
            raw.RunIndex,
            status,
            OfficialCandidate:
                raw.Mode == PerformanceExecutionMode.Official &&
                raw.OfficialEligible &&
                status == PerformanceVerdictStatus.Pass,
            failed,
            []);
    }

    private static List<string> Validate(PerformanceRawRun raw)
    {
        var reasons = new List<string>();
        if (!StringComparer.Ordinal.Equals(
                raw.SchemaId,
                "analogboard.scatter-rendering.raw-run.v1"))
        {
            reasons.Add("schema_id");
        }

        if (!StringComparer.Ordinal.Equals(raw.RunnerContractId, "AB-PERF-RUNNER-v1"))
        {
            reasons.Add("runner_contract_id");
        }

        if (raw.StopwatchFrequency <= 0 || raw.WindowEndTick <= raw.WindowStartTick)
        {
            reasons.Add("clock_window");
        }

        var officialDurationSeconds = raw.Scenario == PerformanceScenario.Soak ? 600L : 60L;
        var validOfficialFrequency =
            raw.StopwatchFrequency > 0 &&
            raw.StopwatchFrequency <= long.MaxValue / officialDurationSeconds;
        if (!validOfficialFrequency)
        {
            reasons.Add("clock_frequency_range");
        }
        else if (raw.Mode == PerformanceExecutionMode.Official &&
            raw.PlannedWindowDurationTicks != officialDurationSeconds * raw.StopwatchFrequency)
        {
            reasons.Add("official_planned_window");
        }
        var validPlannedEnd =
            raw.PlannedWindowDurationTicks > 0 &&
            raw.WindowStartTick <= long.MaxValue - raw.PlannedWindowDurationTicks;
        if (!validPlannedEnd ||
            raw.WindowEndTick - raw.WindowStartTick < raw.PlannedWindowDurationTicks)
        {
            reasons.Add("actual_window_duration");
        }

        if (raw.Mode == PerformanceExecutionMode.Official && !raw.OfficialEligible)
        {
            reasons.Add("official_eligibility");
        }

        if (raw.OverwrittenSampleCount != 0)
        {
            reasons.Add("overwritten_samples");
        }
        if (raw.SchedulerFailureCount != 0 ||
            raw.PendingWorkAtEnd != 0 ||
            raw.MetricSampleCountMismatch != 0)
        {
            reasons.Add("scheduler_integrity");
        }
        if (raw.AllocationProbeInvalidPairCount != 0 ||
            raw.EventAllocationProbeDeltaBytes < 0)
        {
            reasons.Add("allocation_probe_integrity");
        }

        if (raw.ScatterFrameDurationTicks.Length == 0 ||
            raw.ScatterCompletionTicks.Length == 0 ||
            raw.InputLatencyTicks.Length == 0 ||
            raw.ScatterPublicationDurationTicks.Length == 0)
        {
            reasons.Add("required_samples");
        }

        if (raw.Scenario is PerformanceScenario.HardCombined or PerformanceScenario.Soak &&
            (raw.GmiCompletionTicks.Length == 0 ||
             raw.GmiPublicationDurationTicks.Length == 0))
        {
            reasons.Add("gmi_samples");
        }

        var plannedSeconds = raw.PlannedWindowDurationTicks /
            (double)Math.Max(1L, raw.StopwatchFrequency);
        var expectedInputSamples = Math.Floor(plannedSeconds * raw.InputCadenceHz) - 1.0;
        if (raw.InputCadenceHz != 20 ||
            !double.IsFinite(expectedInputSamples) ||
            expectedInputSamples > int.MaxValue ||
            raw.InputLatencyTicks.Length < Math.Max(1.0, expectedInputSamples))
        {
            reasons.Add("input_probe_cohort");
        }

        if (!Positive(raw.ScatterFrameDurationTicks) ||
            !Positive(raw.InputLatencyTicks) ||
            !Positive(raw.ScatterPublicationDurationTicks) ||
            !Positive(raw.GmiPublicationDurationTicks))
        {
            reasons.Add("positive_durations");
        }

        var completionEnd = validPlannedEnd
            ? raw.WindowStartTick + raw.PlannedWindowDurationTicks
            : raw.WindowStartTick;
        if (!OrderedWithin(raw.ScatterCompletionTicks, raw.WindowStartTick, completionEnd) ||
            !OrderedWithin(raw.GmiCompletionTicks, raw.WindowStartTick, completionEnd))
        {
            reasons.Add("completion_order");
        }

        if (raw.PendingFrameMaximum < 0 ||
            raw.PendingCallbackMaximum < 0 ||
            raw.CoalescedFrameCount < 0 ||
            raw.ProcessAllocatedBytes < 0 ||
            raw.AllocationProbeFrameCount < 1 ||
            raw.OneEventAllocationProbeBytes < 0 ||
            raw.HardEventAllocationProbeBytes < 0)
        {
            reasons.Add("counters");
        }

        if (raw.Scenario == PerformanceScenario.Soak && raw.StartingPrivateBytes <= 0)
        {
            reasons.Add("soak_memory_baseline");
        }

        return reasons;
    }

    private static bool Positive(IReadOnlyList<long> values) =>
        values.All(value => value > 0);

    private static bool OrderedWithin(
        IReadOnlyList<long> values,
        long start,
        long end)
    {
        var previous = start;
        foreach (var value in values)
        {
            if (value < previous || value < start || value >= end)
            {
                return false;
            }

            previous = value;
        }

        return true;
    }

    private static void AddFailure(List<string> failures, bool passed, string metric)
    {
        _ = PerformanceMetricSchema.UnitFor(metric);
        if (!passed)
        {
            failures.Add(metric);
        }
    }

    private static double PercentileMilliseconds(
        IReadOnlyList<long> values,
        double percentile,
        long frequency)
    {
        var sorted = values.ToArray();
        Array.Sort(sorted);
        var index = Math.Max(0, (int)Math.Ceiling(percentile * sorted.Length) - 1);
        return sorted[index] * 1_000.0 / frequency;
    }

    private static double MaximumMilliseconds(IReadOnlyList<long> values, long frequency) =>
        values.Max() * 1_000.0 / frequency;

    private static double MaximumIntervalMilliseconds(
        IReadOnlyList<long> values,
        long start,
        long end,
        long frequency)
    {
        var maximum = values[0] - start;
        for (var index = 1; index < values.Count; index++)
        {
            maximum = Math.Max(maximum, values[index] - values[index - 1]);
        }

        maximum = Math.Max(maximum, end - values[^1]);
        return maximum * 1_000.0 / frequency;
    }
}

internal sealed record PerformanceSuiteVerdict(
    string Status,
    bool OfficialAcceptance,
    IReadOnlyList<string> IncompleteReasons);

internal static class PerformanceSuiteEvaluator
{
    public static PerformanceSuiteVerdict EvaluateOfficial(
        IReadOnlyList<PerformanceRunVerdict> scatterRuns,
        IReadOnlyList<PerformanceRunVerdict> combinedRuns,
        PerformanceRunVerdict soak,
        bool headroomObserved,
        bool profileEligible,
        IReadOnlyList<int> childProcessIds)
    {
        ArgumentNullException.ThrowIfNull(scatterRuns);
        ArgumentNullException.ThrowIfNull(combinedRuns);
        ArgumentNullException.ThrowIfNull(soak);
        ArgumentNullException.ThrowIfNull(childProcessIds);
        var incomplete = new List<string>();
        if (scatterRuns.Count != 3 || combinedRuns.Count != 3)
        {
            incomplete.Add("hard_run_count");
        }

        if (!headroomObserved)
        {
            incomplete.Add("headroom_observation");
        }

        if (!profileEligible)
        {
            incomplete.Add("reference_profile");
        }

        if (childProcessIds.Count != 8 || childProcessIds.Distinct().Count() != 8)
        {
            incomplete.Add("independent_child_processes");
        }

        var allRuns = scatterRuns.Concat(combinedRuns).Append(soak).ToArray();
        if (allRuns.Any(run => run.Status == PerformanceVerdictStatus.Incomplete))
        {
            incomplete.Add("incomplete_run");
        }

        if (incomplete.Count != 0)
        {
            return new PerformanceSuiteVerdict("incomplete", false, incomplete);
        }

        var passed = allRuns.All(
            run => run.Status == PerformanceVerdictStatus.Pass && run.OfficialCandidate);
        return new PerformanceSuiteVerdict(
            passed ? "pass" : "fail",
            passed,
            []);
    }
}

internal sealed record PerformanceArtifactSeal(
    string Path,
    long SizeBytes,
    string Sha256);

internal static class AtomicPerformanceArtifactWriter
{
    public static PerformanceArtifactSeal WriteOrValidate(
        string finalPath,
        byte[] expectedBytes)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(finalPath);
        ArgumentNullException.ThrowIfNull(expectedBytes);
        var fullPath = Path.GetFullPath(finalPath);
        if (File.Exists(fullPath))
        {
            return ValidateExisting(fullPath, expectedBytes);
        }

        try
        {
            return Write(fullPath, stream => stream.Write(expectedBytes));
        }
        catch (IOException) when (File.Exists(fullPath))
        {
            return ValidateExisting(fullPath, expectedBytes);
        }
    }

    public static PerformanceArtifactSeal Write(
        string finalPath,
        Action<Stream> write)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(finalPath);
        ArgumentNullException.ThrowIfNull(write);
        var fullPath = Path.GetFullPath(finalPath);
        var directory = Path.GetDirectoryName(fullPath)
            ?? throw new InvalidOperationException("Artifact path has no parent directory.");
        Directory.CreateDirectory(directory);
        var partialPath = fullPath + $".{Guid.NewGuid():N}.partial";
        if (File.Exists(fullPath))
        {
            throw new IOException($"Performance artifact already exists: '{fullPath}'.");
        }

        try
        {
            using (var stream = new FileStream(
                       partialPath,
                       FileMode.CreateNew,
                       FileAccess.Write,
                       FileShare.None,
                       bufferSize: 64 * 1024,
                       FileOptions.WriteThrough))
            {
                write(stream);
                stream.Flush(flushToDisk: true);
            }

            var bytes = File.ReadAllBytes(partialPath);
            var hash = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
            File.Move(partialPath, fullPath);
            return new PerformanceArtifactSeal(fullPath, bytes.LongLength, hash);
        }
        catch
        {
            if (File.Exists(partialPath))
            {
                File.Delete(partialPath);
            }

            throw;
        }
    }

    private static PerformanceArtifactSeal ValidateExisting(
        string fullPath,
        byte[] expectedBytes)
    {
        var actualBytes = File.ReadAllBytes(fullPath);
        if (!actualBytes.AsSpan().SequenceEqual(expectedBytes))
        {
            throw new PerformanceArtifactException(
                $"Existing performance artifact does not match retry payload: '{fullPath}'.");
        }

        return new PerformanceArtifactSeal(
            fullPath,
            actualBytes.LongLength,
            Convert.ToHexString(SHA256.HashData(actualBytes)).ToLowerInvariant());
    }
}
