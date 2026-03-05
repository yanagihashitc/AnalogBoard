#include <windows.h>

#include <cstdio>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <vector>

#include "../AnalogBoard_TestApp/FileLogger.h"

namespace fs = std::filesystem;

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

static std::string ReadTextFile(const fs::path& path)
{
    std::ifstream ifs(path);
    if (!ifs) return {};
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

static fs::path CreateTempDir(const char* prefix)
{
    wchar_t tmpBuf[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpBuf);
    fs::path base = fs::path(tmpBuf) / (std::string(prefix) + "_" + std::to_string(GetTickCount64()));
    fs::create_directories(base);
    return base;
}

// -------------------------------------------------------
// Test: Init creates logs folder and YYMMDDHHMM.log file
// -------------------------------------------------------
void Test_Init_CreatesLogsDirAndFile()
{
    fs::path tmpDir = CreateTempDir("fl_init");

    FileLogger logger;
    bool ok = logger.Init(tmpDir.wstring());

    fs::path logsDir = tmpDir / L"logs";
    TEST_ASSERT(ok, "Init should return true");
    TEST_ASSERT(fs::exists(logsDir), "logs directory should be created");

    std::wstring logPath = logger.GetLogFilePath();
    TEST_ASSERT(!logPath.empty(), "log file path should not be empty");
    TEST_ASSERT(fs::exists(logPath), "log file should exist");

    // Filename format: YYMMDDHHMM.log
    fs::path logFile(logPath);
    std::string stem = logFile.stem().string();
    TEST_ASSERT(stem.size() == 10, "stem should be 10 chars (YYMMDDHHMM)");
    std::string ext = logFile.extension().string();
    TEST_ASSERT(ext == ".log", "extension should be .log");

    // File should be inside logs dir
    TEST_ASSERT(logFile.parent_path() == logsDir, "log file should be in logs dir");

    logger.Close();
    fs::remove_all(tmpDir);
}

// -------------------------------------------------------
// Test: Append adds a line to buffer (not yet flushed)
// -------------------------------------------------------
void Test_Append_BuffersMessages()
{
    fs::path tmpDir = CreateTempDir("fl_buf");

    FileLogger logger;
    logger.Init(tmpDir.wstring());

    logger.Append(L"20260306 12:00:00 000>> Hello world");

    // File should still be empty or contain only the header (no flush yet)
    std::string content = ReadTextFile(logger.GetLogFilePath());
    // The append only buffers, not writes
    TEST_ASSERT(content.find("Hello world") == std::string::npos,
        "message should not be in file before flush");

    logger.Close();
    fs::remove_all(tmpDir);
}

// -------------------------------------------------------
// Test: Flush writes buffered messages to file
// -------------------------------------------------------
void Test_Flush_WritesBufferToFile()
{
    fs::path tmpDir = CreateTempDir("fl_flush");

    FileLogger logger;
    logger.Init(tmpDir.wstring());

    logger.Append(L"20260306 12:00:00 000>> Line1");
    logger.Append(L"20260306 12:00:01 000>> Line2");
    logger.Flush();

    std::string content = ReadTextFile(logger.GetLogFilePath());
    TEST_ASSERT(content.find("Line1") != std::string::npos,
        "Line1 should be in file after flush");
    TEST_ASSERT(content.find("Line2") != std::string::npos,
        "Line2 should be in file after flush");

    logger.Close();
    fs::remove_all(tmpDir);
}

// -------------------------------------------------------
// Test: Flush clears buffer so next flush doesn't duplicate
// -------------------------------------------------------
void Test_Flush_ClearsBuffer()
{
    fs::path tmpDir = CreateTempDir("fl_clrbuf");

    FileLogger logger;
    logger.Init(tmpDir.wstring());

    logger.Append(L"20260306 12:00:00 000>> AAA");
    logger.Flush();

    logger.Append(L"20260306 12:00:01 000>> BBB");
    logger.Flush();

    std::string content = ReadTextFile(logger.GetLogFilePath());

    // Count occurrences of AAA - should be exactly 1
    size_t pos = 0;
    int count = 0;
    while ((pos = content.find("AAA", pos)) != std::string::npos)
    {
        count++;
        pos += 3;
    }
    TEST_ASSERT(count == 1, "AAA should appear exactly once after two flushes");
    TEST_ASSERT(content.find("BBB") != std::string::npos,
        "BBB should be in file after second flush");

    logger.Close();
    fs::remove_all(tmpDir);
}

// -------------------------------------------------------
// Test: Multiple flush cycles append correctly
// -------------------------------------------------------
void Test_MultipleFlushCycles()
{
    fs::path tmpDir = CreateTempDir("fl_multi");

    FileLogger logger;
    logger.Init(tmpDir.wstring());

    logger.Append(L"20260306 12:00:00 000>> Measurement1");
    logger.Flush();

    logger.Append(L"20260306 12:01:00 000>> Measurement2");
    logger.Append(L"20260306 12:01:01 000>> Measurement2-detail");
    logger.Flush();

    std::string content = ReadTextFile(logger.GetLogFilePath());
    TEST_ASSERT(content.find("Measurement1") != std::string::npos, "Measurement1 present");
    TEST_ASSERT(content.find("Measurement2") != std::string::npos, "Measurement2 present");
    TEST_ASSERT(content.find("Measurement2-detail") != std::string::npos, "Measurement2-detail present");

    // Order check: Measurement1 should appear before Measurement2
    size_t pos1 = content.find("Measurement1");
    size_t pos2 = content.find("Measurement2");
    TEST_ASSERT(pos1 < pos2, "Measurement1 should appear before Measurement2");

    logger.Close();
    fs::remove_all(tmpDir);
}

// -------------------------------------------------------
// Test: Close flushes remaining buffer
// -------------------------------------------------------
void Test_Close_FlushesRemaining()
{
    fs::path tmpDir = CreateTempDir("fl_close");

    FileLogger logger;
    logger.Init(tmpDir.wstring());

    logger.Append(L"20260306 12:00:00 000>> Unflushed");
    logger.Close();

    std::string content = ReadTextFile(logger.GetLogFilePath());
    TEST_ASSERT(content.find("Unflushed") != std::string::npos,
        "Close should flush remaining buffered messages");

    fs::remove_all(tmpDir);
}

// -------------------------------------------------------
// Test: Init with non-existent nested path
// -------------------------------------------------------
void Test_Init_CreatesNestedPath()
{
    fs::path tmpDir = CreateTempDir("fl_nested");
    fs::path nested = tmpDir / L"a" / L"b";

    FileLogger logger;
    bool ok = logger.Init(nested.wstring());

    TEST_ASSERT(ok, "Init should succeed with nested path");
    TEST_ASSERT(fs::exists(nested / L"logs"), "nested logs dir should be created");

    logger.Close();
    fs::remove_all(tmpDir);
}

// -------------------------------------------------------
// Test: Append/Flush without Init is safe (no crash)
// -------------------------------------------------------
void Test_AppendWithoutInit_NoCrash()
{
    FileLogger logger;
    // Should not crash
    logger.Append(L"20260306 12:00:00 000>> Orphan");
    logger.Flush();
    logger.Close();

    TEST_ASSERT(true, "No crash on Append/Flush/Close without Init");
}

// -------------------------------------------------------
int main()
{
    std::printf("=== FileLogger Tests ===\n\n");

    RUN_TEST(Test_Init_CreatesLogsDirAndFile);
    RUN_TEST(Test_Append_BuffersMessages);
    RUN_TEST(Test_Flush_WritesBufferToFile);
    RUN_TEST(Test_Flush_ClearsBuffer);
    RUN_TEST(Test_MultipleFlushCycles);
    RUN_TEST(Test_Close_FlushesRemaining);
    RUN_TEST(Test_Init_CreatesNestedPath);
    RUN_TEST(Test_AppendWithoutInit_NoCrash);

    std::printf("\n=== Results: %d tests, %d passed, %d failed ===\n",
        g_TestCount, g_PassCount, g_FailCount);

    return (g_FailCount > 0) ? 1 : 0;
}
