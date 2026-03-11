#pragma once

#include <cstdio>
#include <cmath>
#include <cstring>

static int g_TestCount = 0;
static int g_PassCount = 0;
static int g_FailCount = 0;

template <size_t N, typename... Args>
void FormatTestMessage(char (&buffer)[N], const char* format, Args... args)
{
    std::snprintf(buffer, N, format, args...);
}

#define TEST_ASSERT(cond, msg) do { \
    g_TestCount++; \
    if (cond) { g_PassCount++; } \
    else { g_FailCount++; std::printf("  FAIL: %s (line %d)\n", msg, __LINE__); } \
} while(0)

#define TEST_ASSERT_EQ(expected, actual, msg) do { \
    g_TestCount++; \
    if ((expected) == (actual)) { g_PassCount++; } \
    else { g_FailCount++; std::printf("  FAIL: %s - expected %d, got %d (line %d)\n", msg, (int)(expected), (int)(actual), __LINE__); } \
} while(0)

#define TEST_ASSERT_FLOAT_EQ(expected, actual, eps, msg) do { \
    g_TestCount++; \
    if (fabs((double)(expected) - (double)(actual)) < (eps)) { g_PassCount++; } \
    else { g_FailCount++; std::printf("  FAIL: %s - expected %f, got %f (line %d)\n", msg, (double)(expected), (double)(actual), __LINE__); } \
} while(0)

#define RUN_TEST(func) do { \
    std::printf("[TEST] %s\n", #func); \
    func(); \
} while(0)
