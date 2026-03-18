#include <chrono>
#include <cstdio>
#include <thread>
#include <utility>
#include <vector>

#include "../AnalogBoard_TestApp/WaveAcquisitionEngine.h"

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

using WaveAcquisition::AcquisitionConfig;
using WaveAcquisition::BlockingQueue;
using WaveAcquisition::WaveChunk;

namespace
{
    WaveChunk MakeChunk(const BYTE seed, const ULONG frameSizeLow, const ULONG frameSizeHigh, const INT waveCount)
    {
        WaveChunk chunk;
        chunk.payload = { seed, static_cast<BYTE>(seed + 1) };
        chunk.frameSizeLow = frameSizeLow;
        chunk.frameSizeHigh = frameSizeHigh;
        chunk.waveCount = waveCount;
        return chunk;
    }
}

void Test_TC_N_01_EnqueueThenDequeue_RoundTripsWaveChunk()
{
    // Given: A queue with room for two chunks and one wave chunk to transfer.
    BlockingQueue<WaveChunk> queue(2u);
    WaveChunk expected = MakeChunk(0x10u, 64u, 128u, 3);

    // When: The chunk is enqueued and then dequeued.
    const bool enqueueResult = queue.Enqueue(std::move(expected), 0u);
    WaveChunk actual;
    const bool dequeueResult = queue.Dequeue(actual, 0u);

    // Then: The transfer succeeds and preserves the chunk payload and metadata.
    TEST_ASSERT(enqueueResult, "TC-N-01 enqueue should succeed");
    TEST_ASSERT(dequeueResult, "TC-N-01 dequeue should succeed");
    TEST_ASSERT(actual.payload.size() == 2u, "TC-N-01 payload should round-trip");
    TEST_ASSERT(actual.payload[0] == 0x10u && actual.payload[1] == 0x11u, "TC-N-01 payload bytes should match");
    TEST_ASSERT(actual.frameSizeLow == 64u, "TC-N-01 low frame size should match");
    TEST_ASSERT(actual.frameSizeHigh == 128u, "TC-N-01 high frame size should match");
    TEST_ASSERT(actual.waveCount == 3, "TC-N-01 wave count should match");
}

void Test_TC_N_02_Dequeue_PreservesFifoOrder()
{
    // Given: Two chunks enqueued in sequence.
    BlockingQueue<WaveChunk> queue(2u);
    queue.Enqueue(MakeChunk(0x20u, 32u, 48u, 1), 0u);
    queue.Enqueue(MakeChunk(0x30u, 96u, 144u, 2), 0u);

    // When: Both chunks are dequeued.
    WaveChunk first;
    WaveChunk second;
    const bool firstResult = queue.Dequeue(first, 0u);
    const bool secondResult = queue.Dequeue(second, 0u);

    // Then: FIFO order is preserved.
    TEST_ASSERT(firstResult, "TC-N-02 first dequeue should succeed");
    TEST_ASSERT(secondResult, "TC-N-02 second dequeue should succeed");
    TEST_ASSERT(first.payload[0] == 0x20u, "TC-N-02 first chunk should come out first");
    TEST_ASSERT(second.payload[0] == 0x30u, "TC-N-02 second chunk should come out second");
}

void Test_TC_N_03_DequeueWaitsUntilProducerEnqueuesWithinTimeout()
{
    // Given: A waiting consumer and a producer that will enqueue shortly.
    BlockingQueue<WaveChunk> queue(1u);
    WaveChunk actual;
    std::thread producer([&queue]()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        queue.Enqueue(MakeChunk(0x40u, 16u, 32u, 4), 100u);
    });

    // When: The consumer waits longer than the producer delay.
    const bool dequeueResult = queue.Dequeue(actual, 100u);
    producer.join();

    // Then: The dequeue succeeds before timing out.
    TEST_ASSERT(dequeueResult, "TC-N-03 dequeue should succeed after producer wake-up");
    TEST_ASSERT(actual.payload[0] == 0x40u, "TC-N-03 queued payload should be received");
}

void Test_TC_B_01_DequeueOnEmptyQueue_WithZeroTimeoutFailsImmediately()
{
    // Given: An empty queue.
    BlockingQueue<WaveChunk> queue(1u);

    // When: Dequeue is attempted with a zero timeout.
    WaveChunk chunk;
    const bool dequeueResult = queue.Dequeue(chunk, 0u);

    // Then: The call fails immediately instead of blocking forever.
    TEST_ASSERT(!dequeueResult, "TC-B-01 empty dequeue should fail immediately");
}

void Test_TC_B_02_EnqueueOnFullQueue_WithZeroTimeoutFailsImmediately()
{
    // Given: A full queue with capacity one.
    BlockingQueue<WaveChunk> queue(1u);
    const bool firstResult = queue.Enqueue(MakeChunk(0x50u, 8u, 8u, 1), 0u);

    // When: Another chunk is enqueued with a zero timeout.
    const bool secondResult = queue.Enqueue(MakeChunk(0x60u, 8u, 8u, 1), 0u);

    // Then: The second enqueue fails immediately.
    TEST_ASSERT(firstResult, "TC-B-02 first enqueue should fill the queue");
    TEST_ASSERT(!secondResult, "TC-B-02 second enqueue should fail immediately");
}

void Test_TC_B_03_RequestStop_ReleasesWaitingConsumerAndRejectsNewProducer()
{
    // Given: A waiting dequeue and a queue that will be stopped.
    BlockingQueue<WaveChunk> queue(1u);
    bool dequeueResult = true;
    std::thread consumer([&queue, &dequeueResult]()
    {
        WaveChunk chunk;
        dequeueResult = queue.Dequeue(chunk, 1000u);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // When: Stop is requested while the consumer waits.
    queue.RequestStop();
    consumer.join();
    const bool enqueueAfterStop = queue.Enqueue(MakeChunk(0x70u, 4u, 4u, 1), 0u);

    // Then: Waiting dequeue is released and new enqueue requests are rejected.
    TEST_ASSERT(!dequeueResult, "TC-B-03 dequeue should stop waiting when stop is requested");
    TEST_ASSERT(!enqueueAfterStop, "TC-B-03 enqueue should be rejected after stop");
}

void Test_TC_B_04_WaveChunkAndConfig_ExposePlannedContracts()
{
    // Given: The Phase 2 contract types.
    WaveChunk chunk = MakeChunk(0x80u, 123u, 456u, 7);
    AcquisitionConfig config;

    // When: The fields are read directly.
    // Then: The struct values and defaults match the plan contract.
    TEST_ASSERT(chunk.frameSizeLow == 123u, "TC-B-04 WaveChunk low frame size should be stored");
    TEST_ASSERT(chunk.frameSizeHigh == 456u, "TC-B-04 WaveChunk high frame size should be stored");
    TEST_ASSERT(chunk.waveCount == 7, "TC-B-04 WaveChunk wave count should be stored");
    TEST_ASSERT(config.queueCapacity == 8u, "TC-B-04 queue capacity default should be 8");
    TEST_ASSERT(config.queueWaitTimeoutMs == 200u, "TC-B-04 queue wait timeout default should be 200ms");
    TEST_ASSERT(config.ddrPollTimeoutMs == 10000u, "TC-B-04 DDR poll timeout default should be 10000ms");
    TEST_ASSERT(config.stopWaitTimeoutMs == 5000u, "TC-B-04 stop wait timeout default should be 5000ms");
    TEST_ASSERT(config.maxUsbRetryCount == 3, "TC-B-04 retry count default should be 3");
}

void Test_TC_B_05_NewUsbErrorCodes_AreAvailable()
{
    // Given: The Phase 2 error code additions.
    // When: The code values are read.
    // Then: They are available to later engine and UI code.
    const bool invalidStateIsNegative = USB_ERR_INVALID_STATE < 0;
    const bool deviceDisconnectedIsNegative = USB_ERR_DEVICE_DISCONNECTED < 0;
    const bool stopTimeoutIsNegative = USB_ERR_THREAD_STOP_TIMEOUT < 0;
    const bool queueTimeoutIsNegative = USB_ERR_QUEUE_FULL_TIMEOUT < 0;
    const bool invalidOutputPathIsNegative = USB_ERR_INVALID_OUTPUT_PATH < 0;
    const bool outputPathMissingIsNegative = USB_ERR_OUTPUT_PATH_NOT_FOUND < 0;
    const bool outputPathNotWritableIsNegative = USB_ERR_OUTPUT_PATH_NOT_WRITABLE < 0;

    TEST_ASSERT(invalidStateIsNegative, "TC-B-05 invalid state error code should be negative");
    TEST_ASSERT(deviceDisconnectedIsNegative, "TC-B-05 device disconnected error code should be negative");
    TEST_ASSERT(stopTimeoutIsNegative, "TC-B-05 stop timeout error code should be negative");
    TEST_ASSERT(queueTimeoutIsNegative, "TC-B-05 queue full timeout error code should be negative");
    TEST_ASSERT(invalidOutputPathIsNegative, "TC-B-05 invalid output path error code should be negative");
    TEST_ASSERT(outputPathMissingIsNegative, "TC-B-05 output path not found error code should be negative");
    TEST_ASSERT(outputPathNotWritableIsNegative, "TC-B-05 output path not writable error code should be negative");
}

int main()
{
    std::printf("=== BlockingQueue Unit Tests ===\n\n");

    RUN_TEST(Test_TC_N_01_EnqueueThenDequeue_RoundTripsWaveChunk);
    RUN_TEST(Test_TC_N_02_Dequeue_PreservesFifoOrder);
    RUN_TEST(Test_TC_N_03_DequeueWaitsUntilProducerEnqueuesWithinTimeout);
    RUN_TEST(Test_TC_B_01_DequeueOnEmptyQueue_WithZeroTimeoutFailsImmediately);
    RUN_TEST(Test_TC_B_02_EnqueueOnFullQueue_WithZeroTimeoutFailsImmediately);
    RUN_TEST(Test_TC_B_03_RequestStop_ReleasesWaitingConsumerAndRejectsNewProducer);
    RUN_TEST(Test_TC_B_04_WaveChunkAndConfig_ExposePlannedContracts);
    RUN_TEST(Test_TC_B_05_NewUsbErrorCodes_AreAvailable);

    std::printf("\n=== Summary ===\n");
    std::printf("Total: %d\n", g_TestCount);
    std::printf("Passed: %d\n", g_PassCount);
    std::printf("Failed: %d\n", g_FailCount);

    return (g_FailCount == 0) ? 0 : 1;
}
