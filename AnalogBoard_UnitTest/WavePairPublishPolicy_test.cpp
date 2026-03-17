#include <cstdio>

#include "../AnalogBoard_TestApp/WavePairPublishPolicy.h"

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

WaveDataFileIO::PublishPairResult MakePublishResult(bool publishSuccess, bool lowSuccess, bool highSuccess)
{
    WaveDataFileIO::PublishPairResult result = {};
    result.success = publishSuccess;
    result.low.success = lowSuccess;
    result.high.success = highSuccess;
    return result;
}

void Test_T5_CloseLowFailure_RemainsFatal()
{
    // Given: Low file close failed before any publish attempt.
    const WaveDataFileIO::PublishPairResult publishResult = MakePublishResult(false, false, false);

    // When: Finalize outcome is classified.
    const WavePairPublishPolicy::FinalizeOutcome outcome =
        WavePairPublishPolicy::ClassifyFinalizeOutcome(false, true, publishResult);

    // Then: Acquisition must still stop as fatal I/O failure.
    TEST_ASSERT(outcome == WavePairPublishPolicy::FinalizeOutcome::kFatal, "T5 close low failure must stay fatal");
}

void Test_T5_CloseHighFailure_RemainsFatal()
{
    // Given: High file close failed before publish.
    const WaveDataFileIO::PublishPairResult publishResult = MakePublishResult(false, false, false);

    // When: Finalize outcome is classified.
    const WavePairPublishPolicy::FinalizeOutcome outcome =
        WavePairPublishPolicy::ClassifyFinalizeOutcome(true, false, publishResult);

    // Then: Acquisition must still stop as fatal I/O failure.
    TEST_ASSERT(outcome == WavePairPublishPolicy::FinalizeOutcome::kFatal, "T5 close high failure must stay fatal");
}

void Test_T5_LowPublishFailure_BecomesNonFatal()
{
    // Given: Close succeeded, but low publish failed and tmp pair remains.
    const WaveDataFileIO::PublishPairResult publishResult = MakePublishResult(false, false, false);

    // When: Finalize outcome is classified.
    const WavePairPublishPolicy::FinalizeOutcome outcome =
        WavePairPublishPolicy::ClassifyFinalizeOutcome(true, true, publishResult);

    // Then: Acquisition continues and the failed pair is retained as tmp.
    TEST_ASSERT(outcome == WavePairPublishPolicy::FinalizeOutcome::kRetainedTmpPair, "T5 low publish failure must be non-fatal");
}

void Test_T5_HighPublishFailure_BecomesNonFatal()
{
    // Given: Close succeeded, low rename succeeded, but high publish failed.
    const WaveDataFileIO::PublishPairResult publishResult = MakePublishResult(false, true, false);

    // When: Finalize outcome is classified.
    const WavePairPublishPolicy::FinalizeOutcome outcome =
        WavePairPublishPolicy::ClassifyFinalizeOutcome(true, true, publishResult);

    // Then: Acquisition continues and the failed pair is retained as tmp.
    TEST_ASSERT(outcome == WavePairPublishPolicy::FinalizeOutcome::kRetainedTmpPair, "T5 high publish failure must be non-fatal");
}

void Test_T5_PublishSuccess_RemainsPublished()
{
    // Given: Both close and publish succeeded.
    const WaveDataFileIO::PublishPairResult publishResult = MakePublishResult(true, true, true);

    // When: Finalize outcome is classified.
    const WavePairPublishPolicy::FinalizeOutcome outcome =
        WavePairPublishPolicy::ClassifyFinalizeOutcome(true, true, publishResult);

    // Then: Pair is reported as published.
    TEST_ASSERT(outcome == WavePairPublishPolicy::FinalizeOutcome::kPublished, "T5 publish success must stay published");
}

void Test_T5_LastPairPublishFailure_IsStillNonFatal()
{
    // Given: The loop is finishing and the last pair publish failed after close.
    const WaveDataFileIO::PublishPairResult publishResult = MakePublishResult(false, true, false);

    // When: Finalize outcome is classified for the terminal pair.
    const WavePairPublishPolicy::FinalizeOutcome outcome =
        WavePairPublishPolicy::ClassifyFinalizeOutcome(true, true, publishResult);

    // Then: Final pair publish failure must not promote the whole acquisition to fatal.
    TEST_ASSERT(outcome == WavePairPublishPolicy::FinalizeOutcome::kRetainedTmpPair, "T5 last pair publish failure must be non-fatal");
}

int main()
{
    std::printf("=== WavePairPublishPolicy Unit Tests ===\n\n");

    RUN_TEST(Test_T5_CloseLowFailure_RemainsFatal);
    RUN_TEST(Test_T5_CloseHighFailure_RemainsFatal);
    RUN_TEST(Test_T5_LowPublishFailure_BecomesNonFatal);
    RUN_TEST(Test_T5_HighPublishFailure_BecomesNonFatal);
    RUN_TEST(Test_T5_PublishSuccess_RemainsPublished);
    RUN_TEST(Test_T5_LastPairPublishFailure_IsStillNonFatal);

    std::printf("\n=== Results: %d tests, %d passed, %d failed ===\n", g_TestCount, g_PassCount, g_FailCount);
    return g_FailCount > 0 ? 1 : 0;
}
