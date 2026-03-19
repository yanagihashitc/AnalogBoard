#include <cstdio>
#include <string>

#include "../AnalogBoard_TestApp/AcquisitionLogMessageFormatter.h"

static int g_TestCount = 0;
static int g_PassCount = 0;
static int g_FailCount = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_TestCount++; \
    if (cond) { g_PassCount++; } \
    else { g_FailCount++; std::printf("  FAIL: %s (line %d)\n", msg, __LINE__); } \
} while(0)

#define RUN_TEST(func) do { \
    std::printf("[TEST] %s\n", #func); \
    func(); \
} while(0)

namespace
{
    WaveAcquisition::AcquisitionSummary MakeSummary()
    {
        WaveAcquisition::AcquisitionSummary summary = {};
        summary.terminalStatus = WaveAcquisition::TerminalStatus::EmptyCapture;
        summary.errorCode = -10030;
        summary.savedWaveCount = 0u;
        summary.publishedPairCount = 0;
        summary.ignoredTailBytes = 128u;
        summary.metrics.ep6.callCount = 7u;
        summary.metrics.ep6.totalBytes = 8192u;
        summary.metrics.ep6.maxElapsedMs = 15u;
        summary.metrics.ep6TimeoutCount = 1u;
        summary.metrics.save.callCount = 2u;
        summary.metrics.save.totalBytes = 4096u;
        summary.metrics.save.maxElapsedMs = 6u;
        summary.metrics.ddrStatusPollCount = 9u;
        summary.metrics.ddrWriteWaitPollCount = 3u;
        summary.metrics.maxWaveBacklogBytes = 2048u;
        summary.metrics.latestWaveWrCnt = 64u;
        summary.metrics.latestWaveRdCnt = 32u;
        summary.metrics.latestDdrWrEnd = 1;
        summary.metrics.latestDdrRdEnd = 0;
        summary.metrics.timeout.requestedReadSizeBytes = 65536u;
        summary.metrics.timeout.unreadBytes = 1024u;
        summary.metrics.timeout.readableUpperBoundBytes = 2048u;
        summary.metrics.timeout.backlogBytes = 512u;
        summary.metrics.timeout.waitTimeoutObserved = true;
        summary.metrics.timeout.ep4ReadFailureObserved = false;
        return summary;
    }
}

void Test_TC_N_01_BuildEngineEnterLog_ReturnsFixedMarker()
{
    // Given: The engine call is about to start.
    // When: The direct engine entry marker is formatted.
    const std::wstring line = AcquisitionLogMessageFormatter::BuildEngineEnterLog();

    // Then: The fixed entry marker is returned.
    TEST_ASSERT(line == L"[PR04][ENGINE_ENTER] run_cycle", "TC-N-01 engine enter marker should be fixed");
}

void Test_TC_N_02_BuildEngineExitLog_ContainsTerminalFields()
{
    // Given: An empty-capture summary from the engine.
    const WaveAcquisition::AcquisitionSummary summary = MakeSummary();

    // When: The direct engine exit marker is formatted.
    const std::wstring line = AcquisitionLogMessageFormatter::BuildEngineExitLog(summary);

    // Then: The exit marker contains the terminal status, error, and counters.
    TEST_ASSERT(line.find(L"[PR04][ENGINE_EXIT]") == 0u, "TC-N-02 exit marker prefix should exist");
    TEST_ASSERT(line.find(L"status=empty_capture") != std::wstring::npos, "TC-N-02 status should be present");
    TEST_ASSERT(line.find(L"error=-10030") != std::wstring::npos, "TC-N-02 error should be present");
    TEST_ASSERT(line.find(L"savedWaveCount=0") != std::wstring::npos, "TC-N-02 saved wave count should be present");
    TEST_ASSERT(line.find(L"publishedPairs=0") != std::wstring::npos, "TC-N-02 published pair count should be present");
}

void Test_TC_N_03_BuildCycleSummaryLog_ContainsKeyCounters()
{
    // Given: A representative cycle summary with transport and DDR counters.
    const WaveAcquisition::AcquisitionSummary summary = MakeSummary();

    // When: The cycle summary log line is formatted.
    const std::wstring line = AcquisitionLogMessageFormatter::BuildCycleSummaryLog(summary);

    // Then: The formatted line contains the key counters and terminal fields.
    TEST_ASSERT(line.find(L"[PR01][CYCLE]") == 0u, "TC-N-03 cycle marker prefix should exist");
    TEST_ASSERT(line.find(L"ep6Calls=7") != std::wstring::npos, "TC-N-03 ep6Calls should be present");
    TEST_ASSERT(line.find(L"WAVE_WR_CNT=64") != std::wstring::npos, "TC-N-03 WAVE_WR_CNT should be present");
    TEST_ASSERT(line.find(L"timeoutReadSize=65536") != std::wstring::npos, "TC-N-03 timeoutReadSize should be present");
    TEST_ASSERT(line.find(L"status=empty_capture") != std::wstring::npos, "TC-N-03 status should be present");
}

void Test_TC_B_01_BuildCycleSummaryLog_AllowsZeroAndNegativeFields()
{
    // Given: Zero counters and a negative terminal error code.
    WaveAcquisition::AcquisitionSummary summary = {};
    summary.terminalStatus = WaveAcquisition::TerminalStatus::Ep6Timeout;
    summary.errorCode = -10;

    // When: The cycle summary line is formatted.
    const std::wstring line = AcquisitionLogMessageFormatter::BuildCycleSummaryLog(summary);

    // Then: Zero and negative values are preserved without formatting failure.
    TEST_ASSERT(line.find(L"ep6Calls=0") != std::wstring::npos, "TC-B-01 zero ep6Calls should be present");
    TEST_ASSERT(line.find(L"status=ep6_timeout") != std::wstring::npos, "TC-B-01 status should be present");
    TEST_ASSERT(line.find(L"error=-10") != std::wstring::npos, "TC-B-01 error should be present");
}

void Test_TC_N_04_BuildEngineContextLog_FormatsPointerFieldsAsHex()
{
    // Given: Concrete addresses whose hexadecimal and decimal representations differ.
    // When: The context marker is formatted.
    const std::wstring line = AcquisitionLogMessageFormatter::BuildEngineContextLog(0x1au, 0x2bu, 0x3cu);

    // Then: All three pointer fields are rendered in hexadecimal.
    TEST_ASSERT(
        line == L"[PR04][ENGINE_CONTEXT] curObject=0x1a mainDlg=0x2b usbLibInfo=0x3c",
        "TC-N-04 context addresses should be formatted as hexadecimal");
}

void Test_TC_B_02_BuildEngineContextLog_FormatsZeroAddressesAsHexZero()
{
    // Given: Zero addresses in the engine call context.
    // When: The context marker is formatted.
    const std::wstring line = AcquisitionLogMessageFormatter::BuildEngineContextLog(0u, 0u, 0u);

    // Then: Zero is rendered consistently as hexadecimal.
    TEST_ASSERT(
        line == L"[PR04][ENGINE_CONTEXT] curObject=0x0 mainDlg=0x0 usbLibInfo=0x0",
        "TC-B-02 zero addresses should be formatted as hexadecimal");
}

void Test_TC_B_03_BuildUsbSessionNullLog_FallsBackForNullApiName()
{
    // Given: A null API name while reporting a null USB session.
    // When: The null-session marker is formatted.
    const std::wstring line = AcquisitionLogMessageFormatter::BuildUsbSessionNullLog(nullptr);

    // Then: The marker remains well-formed and uses a fallback label.
    TEST_ASSERT(line.find(L"[PR04][ENGINE_USB_SESSION_NULL]") == 0u, "TC-B-03 null-session marker prefix should exist");
    TEST_ASSERT(line.find(L"api=(null)") != std::wstring::npos, "TC-B-03 fallback api label should be present");
}

int main()
{
    std::printf("=== AcquisitionLogMessageFormatter Unit Tests ===\n\n");

    RUN_TEST(Test_TC_N_01_BuildEngineEnterLog_ReturnsFixedMarker);
    RUN_TEST(Test_TC_N_02_BuildEngineExitLog_ContainsTerminalFields);
    RUN_TEST(Test_TC_N_03_BuildCycleSummaryLog_ContainsKeyCounters);
    RUN_TEST(Test_TC_N_04_BuildEngineContextLog_FormatsPointerFieldsAsHex);
    RUN_TEST(Test_TC_B_01_BuildCycleSummaryLog_AllowsZeroAndNegativeFields);
    RUN_TEST(Test_TC_B_02_BuildEngineContextLog_FormatsZeroAddressesAsHexZero);
    RUN_TEST(Test_TC_B_03_BuildUsbSessionNullLog_FallsBackForNullApiName);

    std::printf("\n=== Summary ===\n");
    std::printf("Total: %d\n", g_TestCount);
    std::printf("Passed: %d\n", g_PassCount);
    std::printf("Failed: %d\n", g_FailCount);

    return (g_FailCount == 0) ? 0 : 1;
}
