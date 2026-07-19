#include <cstdio>
#include <cstring>

#include "../AnalogBoard_Dll/Ep4FailureDiagnostic.h"

static int g_TestCount = 0;
static int g_PassCount = 0;
static int g_FailCount = 0;

#define TEST_ASSERT(condition, message) do { \
    g_TestCount++; \
    if (condition) { \
        g_PassCount++; \
    } else { \
        g_FailCount++; \
        std::printf("FAIL: %s\n", message); \
    } \
} while (0)

#define RUN_TEST(testFunction) do { \
    std::printf("Running %s...\n", #testFunction); \
    testFunction(); \
} while (0)

static Ep4FailureDiagnostic::Record MakeFailureRecord()
{
    Ep4FailureDiagnostic::Record record;
    record.monotonicMs = 123456789u;
    record.endpoint = 0x84u;
    record.requestedLength = 512;
    record.returnedLength = 0;
    record.usbdStatus = 0xC0000001u;
    record.ntStatus = 0xC0000120u;
    record.cypressLastError = 31u;
    record.win32LastError = 121u;
    return record;
}

void Test_EP4D_N_01_CapturePreservesEveryRequiredFailureField()
{
    Ep4FailureDiagnostic::SingleRecordBuffer buffer;
    const Ep4FailureDiagnostic::Record expected = MakeFailureRecord();
    buffer.Capture(expected);

    Ep4FailureDiagnostic::Record actual;
    const bool consumed = buffer.Consume(&actual);

    TEST_ASSERT(consumed, "EP4D-N-01 captured failure should be consumable");
    TEST_ASSERT(actual.monotonicMs == expected.monotonicMs, "EP4D-N-01 monotonic time should be preserved");
    TEST_ASSERT(actual.endpoint == expected.endpoint, "EP4D-N-01 endpoint should be preserved");
    TEST_ASSERT(actual.requestedLength == expected.requestedLength, "EP4D-N-01 requested length should be preserved");
    TEST_ASSERT(actual.returnedLength == expected.returnedLength, "EP4D-N-01 returned length should be preserved");
    TEST_ASSERT(actual.usbdStatus == expected.usbdStatus, "EP4D-N-01 UsbdStatus should be preserved");
    TEST_ASSERT(actual.ntStatus == expected.ntStatus, "EP4D-N-01 NtStatus should be preserved");
    TEST_ASSERT(actual.cypressLastError == expected.cypressLastError, "EP4D-N-01 Cypress LastError should be preserved");
    TEST_ASSERT(actual.win32LastError == expected.win32LastError, "EP4D-N-01 Win32 last error should be preserved");
}

void Test_EP4D_N_02_ConsumeEmptiesTheFixedRecordSlot()
{
    Ep4FailureDiagnostic::SingleRecordBuffer buffer;
    buffer.Capture(MakeFailureRecord());

    Ep4FailureDiagnostic::Record record;
    TEST_ASSERT(buffer.Consume(&record), "EP4D-N-02 first consume should succeed");
    TEST_ASSERT(!buffer.Consume(&record), "EP4D-N-02 record should be emitted only once");
}

void Test_EP4D_N_03_LatestFailureReplacesAnUnconsumedStaleRecord()
{
    Ep4FailureDiagnostic::SingleRecordBuffer buffer;
    Ep4FailureDiagnostic::Record stale = MakeFailureRecord();
    stale.monotonicMs = 1u;
    buffer.Capture(stale);

    Ep4FailureDiagnostic::Record latest = MakeFailureRecord();
    latest.monotonicMs = 2u;
    latest.returnedLength = 128;
    buffer.Capture(latest);

    Ep4FailureDiagnostic::Record actual;
    TEST_ASSERT(buffer.Consume(&actual), "EP4D-N-03 latest failure should be consumable");
    TEST_ASSERT(actual.monotonicMs == 2u, "EP4D-N-03 stale failure should be replaced");
    TEST_ASSERT(actual.returnedLength == 128, "EP4D-N-03 latest returned length should be preserved");
}

void Test_EP4D_N_04_FormatProducesOneCompleteFailureRecord()
{
    const Ep4FailureDiagnostic::Record record = MakeFailureRecord();
    char output[512] = {};

    const bool formatted = Ep4FailureDiagnostic::FormatRecord(record, output, sizeof(output));

    TEST_ASSERT(formatted, "EP4D-N-04 complete record should fit the fixed output buffer");
    TEST_ASSERT(std::strstr(output, "endpoint=0x84") != nullptr, "EP4D-N-04 endpoint should be formatted");
    TEST_ASSERT(std::strstr(output, "requestedLength=512") != nullptr, "EP4D-N-04 requested length should be formatted");
    TEST_ASSERT(std::strstr(output, "returnedLength=0") != nullptr, "EP4D-N-04 returned length should be formatted");
    TEST_ASSERT(std::strstr(output, "usbdStatus=0xC0000001") != nullptr, "EP4D-N-04 UsbdStatus should be formatted");
    TEST_ASSERT(std::strstr(output, "ntStatus=0xC0000120") != nullptr, "EP4D-N-04 NtStatus should be formatted");
    TEST_ASSERT(std::strstr(output, "cypressLastError=31") != nullptr, "EP4D-N-04 Cypress LastError should be formatted");
    TEST_ASSERT(std::strstr(output, "win32LastError=121") != nullptr, "EP4D-N-04 Win32 last error should be formatted");
    TEST_ASSERT(std::strstr(output, "monotonicMs=123456789") != nullptr, "EP4D-N-04 monotonic time should be formatted");
    TEST_ASSERT(std::strchr(output, '\n') == nullptr, "EP4D-N-04 formatter should produce exactly one log record");
}

void Test_EP4D_A_01_NullOrShortOutputDoesNotProducePartialEvidence()
{
    Ep4FailureDiagnostic::SingleRecordBuffer buffer;
    buffer.Capture(MakeFailureRecord());

    TEST_ASSERT(!buffer.Consume(nullptr), "EP4D-A-01 null consume target should be rejected");

    Ep4FailureDiagnostic::Record record;
    TEST_ASSERT(buffer.Consume(&record), "EP4D-A-01 null consume should not discard the record");

    char output[12] = {};
    TEST_ASSERT(
        !Ep4FailureDiagnostic::FormatRecord(record, output, sizeof(output)),
        "EP4D-A-01 short format buffer should report failure");
    TEST_ASSERT(output[sizeof(output) - 1u] == '\0', "EP4D-A-01 short output should remain terminated");
}

int main()
{
    std::printf("=== EP4 Failure Diagnostic Unit Tests ===\n\n");

    RUN_TEST(Test_EP4D_N_01_CapturePreservesEveryRequiredFailureField);
    RUN_TEST(Test_EP4D_N_02_ConsumeEmptiesTheFixedRecordSlot);
    RUN_TEST(Test_EP4D_N_03_LatestFailureReplacesAnUnconsumedStaleRecord);
    RUN_TEST(Test_EP4D_N_04_FormatProducesOneCompleteFailureRecord);
    RUN_TEST(Test_EP4D_A_01_NullOrShortOutputDoesNotProducePartialEvidence);

    std::printf("\nTests: %d, Passed: %d, Failed: %d\n", g_TestCount, g_PassCount, g_FailCount);
    return g_FailCount == 0 ? 0 : 1;
}
