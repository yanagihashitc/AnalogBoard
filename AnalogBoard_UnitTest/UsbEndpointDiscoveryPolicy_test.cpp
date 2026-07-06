#include <cstdio>
#include <cstdint>

#include "../AnalogBoard_Dll/UsbEndpointDiscoveryPolicy.h"

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

static void Test_TC_N_01_ResolvesRequiredEndpointsByAddress()
{
    // Given: The required endpoints are exposed in a non-positional order.
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState state;

    // When: Endpoint descriptors are visited by address and transfer attribute.
    state.VisitEndpoint(101u, 0x86u, UsbEndpointDiscoveryPolicy::kBulkAttribute);
    state.VisitEndpoint(102u, 0x02u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    state.VisitEndpoint(103u, 0x84u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);

    // Then: The required endpoint tokens are resolved by address.
    TEST_ASSERT(state.IsComplete(), "TC-N-01 required endpoints should be complete");
    TEST_ASSERT(state.Ep2Token() == 102u, "TC-N-01 EP2 should be selected by address");
    TEST_ASSERT(state.Ep4Token() == 103u, "TC-N-01 EP4 should be selected by address");
    TEST_ASSERT(state.Ep6Token() == 101u, "TC-N-01 EP6 should be selected by address");
}

static void Test_TC_N_02_IgnoresEndpointOrderAndUnknownEndpoints()
{
    // Given: Unknown endpoints are present before and between the required endpoints.
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState state;

    // When: Discovery scans every endpoint without relying on index position.
    state.VisitEndpoint(201u, 0x81u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    state.VisitEndpoint(202u, 0x02u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    state.VisitEndpoint(203u, 0x03u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    state.VisitEndpoint(204u, 0x84u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    state.VisitEndpoint(205u, 0x86u, UsbEndpointDiscoveryPolicy::kBulkAttribute);

    // Then: Unknown endpoints are ignored and the required endpoints are selected.
    TEST_ASSERT(state.IsComplete(), "TC-N-02 unknown endpoints should not invalidate discovery");
    TEST_ASSERT(state.Ep2Token() == 202u, "TC-N-02 EP2 should ignore position");
    TEST_ASSERT(state.Ep4Token() == 204u, "TC-N-02 EP4 should ignore position");
    TEST_ASSERT(state.Ep6Token() == 205u, "TC-N-02 EP6 should ignore position");
}

static void Test_TC_N_03_AllowsAltSettingsToBeEvaluatedIndependently()
{
    // Given: One alt setting is incomplete and a later alt setting has all required endpoints.
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState alt0;
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState alt1;

    // When: Each alt setting is evaluated with a separate discovery state.
    alt0.VisitEndpoint(301u, 0x02u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    alt0.VisitEndpoint(302u, 0x84u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    alt1.VisitEndpoint(401u, 0x86u, UsbEndpointDiscoveryPolicy::kBulkAttribute);
    alt1.VisitEndpoint(402u, 0x84u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    alt1.VisitEndpoint(403u, 0x02u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);

    // Then: The incomplete alt is rejected and the complete alt is accepted.
    TEST_ASSERT(!alt0.IsComplete(), "TC-N-03 incomplete alt should not be accepted");
    TEST_ASSERT(alt1.IsComplete(), "TC-N-03 complete alt should be accepted");
    TEST_ASSERT(alt1.Ep2Token() == 403u, "TC-N-03 complete alt should provide EP2");
    TEST_ASSERT(alt1.Ep4Token() == 402u, "TC-N-03 complete alt should provide EP4");
    TEST_ASSERT(alt1.Ep6Token() == 401u, "TC-N-03 complete alt should provide EP6");
}

static void Test_TC_N_04_SelectsLastCompleteAltSetting()
{
    // Given: Two alt settings are complete and a later alt is incomplete.
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState alt0;
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState alt1;
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState alt2;
    UsbEndpointDiscoveryPolicy::AltEndpointSelectionState selection;

    alt0.VisitEndpoint(101u, 0x02u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    alt0.VisitEndpoint(102u, 0x84u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    alt0.VisitEndpoint(103u, 0x86u, UsbEndpointDiscoveryPolicy::kBulkAttribute);
    alt1.VisitEndpoint(201u, 0x02u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    alt1.VisitEndpoint(202u, 0x84u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    alt1.VisitEndpoint(203u, 0x86u, UsbEndpointDiscoveryPolicy::kBulkAttribute);
    alt2.VisitEndpoint(301u, 0x02u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);

    // When: Alt settings are considered in enumeration order.
    selection.ConsiderAlt(0, alt0);
    selection.ConsiderAlt(1, alt1);
    selection.ConsiderAlt(2, alt2);

    // Then: The last complete alt is selected and the later incomplete alt is ignored.
    TEST_ASSERT(selection.HasSelection(), "TC-N-04 selection should exist");
    TEST_ASSERT(selection.SelectedAltIndex() == 1, "TC-N-04 should select the last complete alt");
}

static void Test_TC_B_01_ReportsMissingRequiredEndpoint()
{
    // Given: Only EP2 and EP4 are present.
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState state;

    // When: Discovery scans an incomplete endpoint set.
    state.VisitEndpoint(301u, 0x02u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    state.VisitEndpoint(302u, 0x84u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);

    // Then: Discovery remains incomplete and reports the missing EP6 state.
    TEST_ASSERT(!state.IsComplete(), "TC-B-01 discovery should fail when EP6 is missing");
    TEST_ASSERT(!state.HasEp6(), "TC-B-01 missing EP6 should be explicit");
    TEST_ASSERT(state.HasEp2(), "TC-B-01 EP2 should still be recorded");
    TEST_ASSERT(state.HasEp4(), "TC-B-01 EP4 should still be recorded");
}

static void Test_TC_B_02_DoesNotTreatWrongTransferTypeAsMatch()
{
    // Given: The expected endpoint addresses are exposed with the wrong transfer attributes.
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState state;

    // When: Discovery evaluates both address and transfer attribute.
    state.VisitEndpoint(401u, 0x02u, UsbEndpointDiscoveryPolicy::kBulkAttribute);
    state.VisitEndpoint(402u, 0x84u, UsbEndpointDiscoveryPolicy::kBulkAttribute);
    state.VisitEndpoint(403u, 0x86u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);

    // Then: Wrong transfer types are rejected and discovery remains incomplete.
    TEST_ASSERT(!state.HasEp2(), "TC-B-02 EP2 requires interrupt attribute");
    TEST_ASSERT(!state.HasEp4(), "TC-B-02 EP4 requires interrupt attribute");
    TEST_ASSERT(!state.HasEp6(), "TC-B-02 EP6 requires bulk attribute");
    TEST_ASSERT(!state.IsComplete(), "TC-B-02 wrong attributes should not complete discovery");
}

static void Test_TC_B_03_EmptyEndpointSetIsIncomplete()
{
    // Given: No endpoints are exposed for an alt setting.
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState state;

    // When: Discovery is inspected without any visited endpoints.
    // Then: Every required endpoint remains missing and discovery is incomplete.
    TEST_ASSERT(!state.HasEp2(), "TC-B-03 EP2 should be missing");
    TEST_ASSERT(!state.HasEp4(), "TC-B-03 EP4 should be missing");
    TEST_ASSERT(!state.HasEp6(), "TC-B-03 EP6 should be missing");
    TEST_ASSERT(!state.IsComplete(), "TC-B-03 empty endpoint set should be incomplete");
}

int main()
{
    std::printf("=== UsbEndpointDiscoveryPolicy Unit Tests ===\n\n");

    RUN_TEST(Test_TC_N_01_ResolvesRequiredEndpointsByAddress);
    RUN_TEST(Test_TC_N_02_IgnoresEndpointOrderAndUnknownEndpoints);
    RUN_TEST(Test_TC_N_03_AllowsAltSettingsToBeEvaluatedIndependently);
    RUN_TEST(Test_TC_N_04_SelectsLastCompleteAltSetting);
    RUN_TEST(Test_TC_B_01_ReportsMissingRequiredEndpoint);
    RUN_TEST(Test_TC_B_02_DoesNotTreatWrongTransferTypeAsMatch);
    RUN_TEST(Test_TC_B_03_EmptyEndpointSetIsIncomplete);

    std::printf("\n=== Results: %d tests, %d passed, %d failed ===\n", g_TestCount, g_PassCount, g_FailCount);
    return g_FailCount > 0 ? 1 : 0;
}
