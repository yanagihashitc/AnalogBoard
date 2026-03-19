#include <cstdio>

#include "../AnalogBoard_TestApp/DialogMainBindingPolicy.h"

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

void Test_TC_N_01_ResolveMainDialog_UsesOwnerWhenCurrentIsNull()
{
    // Given: A member dialog that was default-constructed without a bound owner.
    int owner = 1234;

    // When: The main dialog binding is resolved.
    int* const resolved = DialogMainBindingPolicy::ResolveMainDialog<int>(nullptr, &owner);

    // Then: The owner dialog is adopted.
    TEST_ASSERT(resolved == &owner, "TC-N-01 owner should be adopted when current is null");
}

void Test_TC_N_02_ResolveMainDialog_PreservesExistingParent()
{
    // Given: A dialog that already has an explicit parent binding.
    int current = 1111;
    int owner = 2222;

    // When: The main dialog binding is resolved again.
    int* const resolved = DialogMainBindingPolicy::ResolveMainDialog(&current, &owner);

    // Then: The explicit parent is preserved.
    TEST_ASSERT(resolved == &current, "TC-N-02 existing parent should be preserved");
}

void Test_TC_B_01_ResolveMainDialog_AllowsNullOwner()
{
    // Given: Neither the current nor the owner dialog is available.
    // When: The binding is resolved.
    int* const resolved = DialogMainBindingPolicy::ResolveMainDialog<int>(nullptr, nullptr);

    // Then: The result remains null.
    TEST_ASSERT(resolved == nullptr, "TC-B-01 null owner should remain null");
}

int main()
{
    std::printf("=== DialogMainBindingPolicy Unit Tests ===\n\n");

    RUN_TEST(Test_TC_N_01_ResolveMainDialog_UsesOwnerWhenCurrentIsNull);
    RUN_TEST(Test_TC_N_02_ResolveMainDialog_PreservesExistingParent);
    RUN_TEST(Test_TC_B_01_ResolveMainDialog_AllowsNullOwner);

    std::printf("\n=== Summary ===\n");
    std::printf("Total: %d\n", g_TestCount);
    std::printf("Passed: %d\n", g_PassCount);
    std::printf("Failed: %d\n", g_FailCount);

    return (g_FailCount == 0) ? 0 : 1;
}
