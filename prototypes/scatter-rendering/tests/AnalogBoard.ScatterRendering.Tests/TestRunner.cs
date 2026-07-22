namespace AnalogBoard.ScatterRendering.Tests;

internal sealed record ContractTestCase(string Name, Action Execute);

internal static class TestRunner
{
    public static int Run(IEnumerable<ContractTestCase> cases)
    {
        var total = 0;
        var passed = 0;
        var failed = 0;

        foreach (var testCase in cases)
        {
            total++;
            try
            {
                testCase.Execute();
                passed++;
                Console.WriteLine($"PASS {testCase.Name}");
            }
            catch (Exception exception)
            {
                failed++;
                Console.Error.WriteLine($"FAIL {testCase.Name}: {exception}");
            }
        }

        Console.WriteLine($"SUMMARY total={total} passed={passed} failed={failed}");

        return failed == 0 ? 0 : 1;
    }
}

internal static class ContractAssert
{
    public static void Equal<T>(T expected, T actual)
        where T : notnull
    {
        if (!EqualityComparer<T>.Default.Equals(expected, actual))
        {
            throw new InvalidOperationException(
                $"Expected '{expected}', actual '{actual}'.");
        }
    }

    public static void SequenceEqual<T>(
        IEnumerable<T> expected,
        IEnumerable<T> actual)
    {
        if (!expected.SequenceEqual(actual))
        {
            throw new InvalidOperationException("Expected sequences to be equal.");
        }
    }

    public static void SequenceNotEqual<T>(
        IEnumerable<T> first,
        IEnumerable<T> second)
    {
        if (first.SequenceEqual(second))
        {
            throw new InvalidOperationException("Expected sequences to differ.");
        }
    }

    public static TException Throws<TException>(
        Action action,
        string expectedMessage)
        where TException : Exception
    {
        try
        {
            action();
        }
        catch (TException exception)
        {
            Equal(expectedMessage, exception.Message);
            return exception;
        }
        catch (Exception exception)
        {
            throw new InvalidOperationException(
                $"Expected {typeof(TException).Name}, actual {exception.GetType().Name}.",
                exception);
        }

        throw new InvalidOperationException(
            $"Expected {typeof(TException).Name}, but no exception was thrown.");
    }
}
