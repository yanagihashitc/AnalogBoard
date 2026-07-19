#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../AnalogBoard_Dll/UsbEndpointDiscoveryPolicy.h"

static int g_TestCount = 0;
static int g_PassCount = 0;
static int g_FailCount = 0;

#define TEST_ASSERT(condition, message) do { \
    ++g_TestCount; \
    if (condition) { \
        ++g_PassCount; \
    } else { \
        ++g_FailCount; \
        std::printf("  FAIL: %s (line %d)\n", message, __LINE__); \
    } \
} while (0)

#define RUN_TEST(testFunction) do { \
    std::printf("[TEST] %s\n", #testFunction); \
    testFunction(); \
} while (0)

static void AddCompleteEndpointSet(
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState& state,
    std::uintptr_t tokenBase)
{
    state.VisitEndpoint(tokenBase + 0u, 0x02u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    state.VisitEndpoint(tokenBase + 1u, 0x84u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    state.VisitEndpoint(tokenBase + 2u, 0x86u, UsbEndpointDiscoveryPolicy::kBulkAttribute);
}

static void Test_TC_N_01_ResolvesRequiredEndpointsByAddress()
{
    // Given: One alternate interface exposes the required endpoint set.
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState state;

    // When: The descriptors are visited with their expected addresses and attributes.
    state.VisitEndpoint(101u, 0x02u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    state.VisitEndpoint(102u, 0x84u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    state.VisitEndpoint(103u, 0x86u, UsbEndpointDiscoveryPolicy::kBulkAttribute);

    // Then: The complete set is resolved by address and attribute.
    TEST_ASSERT(state.IsComplete(), "TC-N-01 required endpoint set should be complete");
    TEST_ASSERT(state.Ep2Token() == 101u, "TC-N-01 EP2 token should match");
    TEST_ASSERT(state.Ep4Token() == 102u, "TC-N-01 EP4 token should match");
    TEST_ASSERT(state.Ep6Token() == 103u, "TC-N-01 EP6 token should match");
}

static void Test_TC_N_02_IgnoresEnumerationOrder()
{
    // Given: The required endpoints are enumerated in reverse order.
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState state;

    // When: Discovery visits EP6, EP4, and EP2 without relying on array positions.
    state.VisitEndpoint(201u, 0x86u, UsbEndpointDiscoveryPolicy::kBulkAttribute);
    state.VisitEndpoint(202u, 0x84u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    state.VisitEndpoint(203u, 0x02u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);

    // Then: The same complete endpoint set is resolved.
    TEST_ASSERT(state.IsComplete(), "TC-N-02 reversed endpoint order should be accepted");
    TEST_ASSERT(state.Ep2Token() == 203u, "TC-N-02 EP2 should be resolved by address");
    TEST_ASSERT(state.Ep4Token() == 202u, "TC-N-02 EP4 should be resolved by address");
    TEST_ASSERT(state.Ep6Token() == 201u, "TC-N-02 EP6 should be resolved by address");
}

static void Test_TC_N_03_IgnoresUnknownEndpoints()
{
    // Given: Unknown addresses, including expected-address neighbours, surround the required set.
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState state;

    // When: Discovery visits unknown and required endpoint descriptors together.
    state.VisitEndpoint(301u, 0x01u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    state.VisitEndpoint(302u, 0x03u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    state.VisitEndpoint(303u, 0x83u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    state.VisitEndpoint(304u, 0x85u, UsbEndpointDiscoveryPolicy::kBulkAttribute);
    AddCompleteEndpointSet(state, 310u);
    state.VisitEndpoint(305u, 0x87u, UsbEndpointDiscoveryPolicy::kBulkAttribute);

    // Then: Unknown descriptors are ignored without invalidating the required set.
    TEST_ASSERT(state.IsComplete(), "TC-N-03 unknown endpoints should not invalidate discovery");
    TEST_ASSERT(state.Ep2Token() == 310u, "TC-N-03 EP2 token should come from the required set");
    TEST_ASSERT(state.Ep4Token() == 311u, "TC-N-03 EP4 token should come from the required set");
    TEST_ASSERT(state.Ep6Token() == 312u, "TC-N-03 EP6 token should come from the required set");
}

static void Test_TC_N_04_SelectsOnlyACompleteAlternateInterface()
{
    // Given: An incomplete alt precedes a complete alt and another incomplete alt follows it.
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState alt0;
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState alt1;
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState alt2;
    UsbEndpointDiscoveryPolicy::AltEndpointSelectionState selection;
    alt0.VisitEndpoint(401u, 0x02u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    AddCompleteEndpointSet(alt1, 410u);
    alt2.VisitEndpoint(420u, 0x86u, UsbEndpointDiscoveryPolicy::kBulkAttribute);

    // When: Each alternate interface is evaluated independently in enumeration order.
    selection.ConsiderAlt(0, alt0);
    selection.ConsiderAlt(1, alt1);
    selection.ConsiderAlt(2, alt2);

    // Then: The complete alternate interface remains selected.
    TEST_ASSERT(selection.HasSelection(), "TC-N-04 a complete alternate interface should be selected");
    TEST_ASSERT(selection.SelectedAltIndex() == 1, "TC-N-04 alt 1 should be selected");
}

static void Test_TC_N_06_MultipleCompleteAlternates_PreserveLegacyLastWins()
{
    // Given: Two alternate interfaces both expose complete endpoint sets.
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState alt0;
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState alt2;
    UsbEndpointDiscoveryPolicy::AltEndpointSelectionState selection;
    AddCompleteEndpointSet(alt0, 1000u);
    AddCompleteEndpointSet(alt2, 2000u);

    // When: Both are considered in enumeration order.
    selection.ConsiderAlt(0, alt0);
    selection.ConsiderAlt(2, alt2);

    // Then: The established last-complete-alt tie-break remains explicit.
    TEST_ASSERT(selection.HasSelection(), "TC-N-06 a complete alternate must remain selected");
    TEST_ASSERT(selection.SelectedAltIndex() == 2, "TC-N-06 last complete alternate must win");
}

static void Test_TC_A_01_IgnoresNullEndpointTokens()
{
    // Given: Null endpoint slots report required addresses and attributes with token zero.
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState state;

    // When: Discovery visits the null slots.
    state.VisitEndpoint(0u, 0x02u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    state.VisitEndpoint(0u, 0x84u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    state.VisitEndpoint(0u, 0x86u, UsbEndpointDiscoveryPolicy::kBulkAttribute);

    // Then: Null slots are not accepted as resolved endpoints.
    TEST_ASSERT(!state.HasEp2(), "TC-A-01 null EP2 slot should be ignored");
    TEST_ASSERT(!state.HasEp4(), "TC-A-01 null EP4 slot should be ignored");
    TEST_ASSERT(!state.HasEp6(), "TC-A-01 null EP6 slot should be ignored");
    TEST_ASSERT(!state.IsComplete(), "TC-A-01 null slots should not complete discovery");
}

static void Test_TC_A_02_RejectsMissingEp2()
{
    // Given: EP4 and EP6 are present but EP2 is absent.
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState state;

    // When: Discovery scans the incomplete set.
    state.VisitEndpoint(501u, 0x84u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    state.VisitEndpoint(502u, 0x86u, UsbEndpointDiscoveryPolicy::kBulkAttribute);

    // Then: The set remains incomplete with EP2 explicitly missing.
    TEST_ASSERT(!state.HasEp2(), "TC-A-02 EP2 should be missing");
    TEST_ASSERT(!state.IsComplete(), "TC-A-02 missing EP2 should reject the set");
}

static void Test_TC_A_03_RejectsMissingEp4()
{
    // Given: EP2 and EP6 are present but EP4 is absent.
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState state;

    // When: Discovery scans the incomplete set.
    state.VisitEndpoint(601u, 0x02u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    state.VisitEndpoint(602u, 0x86u, UsbEndpointDiscoveryPolicy::kBulkAttribute);

    // Then: The set remains incomplete with EP4 explicitly missing.
    TEST_ASSERT(!state.HasEp4(), "TC-A-03 EP4 should be missing");
    TEST_ASSERT(!state.IsComplete(), "TC-A-03 missing EP4 should reject the set");
}

static void Test_TC_A_04_RejectsMissingEp6()
{
    // Given: EP2 and EP4 are present but EP6 is absent.
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState state;

    // When: Discovery scans the incomplete set.
    state.VisitEndpoint(701u, 0x02u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    state.VisitEndpoint(702u, 0x84u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);

    // Then: The set remains incomplete with EP6 explicitly missing.
    TEST_ASSERT(!state.HasEp6(), "TC-A-04 EP6 should be missing");
    TEST_ASSERT(!state.IsComplete(), "TC-A-04 missing EP6 should reject the set");
}

static void Test_TC_A_05_RejectsWrongTransferAttributes()
{
    // Given: Every required address is paired with the wrong transfer attribute.
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState state;

    // When: Discovery evaluates both address and transfer attribute.
    state.VisitEndpoint(801u, 0x02u, UsbEndpointDiscoveryPolicy::kBulkAttribute);
    state.VisitEndpoint(802u, 0x84u, UsbEndpointDiscoveryPolicy::kBulkAttribute);
    state.VisitEndpoint(803u, 0x86u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);

    // Then: None of the descriptors is accepted.
    TEST_ASSERT(!state.HasEp2(), "TC-A-05 EP2 requires the interrupt attribute");
    TEST_ASSERT(!state.HasEp4(), "TC-A-05 EP4 requires the interrupt attribute");
    TEST_ASSERT(!state.HasEp6(), "TC-A-05 EP6 requires the bulk attribute");
    TEST_ASSERT(!state.IsComplete(), "TC-A-05 wrong attributes should reject the set");
}

static void Test_TC_A_06_RejectsEmptyEndpointSet()
{
    // Given: An alternate interface exposes no endpoint descriptors.
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState state;

    // When: Discovery is inspected without visiting an endpoint.
    // Then: The empty set is incomplete.
    TEST_ASSERT(!state.HasEp2(), "TC-A-06 EP2 should be absent");
    TEST_ASSERT(!state.HasEp4(), "TC-A-06 EP4 should be absent");
    TEST_ASSERT(!state.HasEp6(), "TC-A-06 EP6 should be absent");
    TEST_ASSERT(!state.IsComplete(), "TC-A-06 empty set should be incomplete");
}

static void Test_TC_A_07_RejectsIncompleteAlternateInterfaces()
{
    // Given: Every alternate interface is missing at least one required endpoint.
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState alt0;
    UsbEndpointDiscoveryPolicy::EndpointDiscoveryState alt1;
    UsbEndpointDiscoveryPolicy::AltEndpointSelectionState selection;
    alt0.VisitEndpoint(901u, 0x02u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    alt0.VisitEndpoint(902u, 0x84u, UsbEndpointDiscoveryPolicy::kInterruptAttribute);
    alt1.VisitEndpoint(903u, 0x86u, UsbEndpointDiscoveryPolicy::kBulkAttribute);

    // When: Both incomplete alternate interfaces are considered.
    selection.ConsiderAlt(0, alt0);
    selection.ConsiderAlt(1, alt1);

    // Then: No alternate interface is selected.
    TEST_ASSERT(!selection.HasSelection(), "TC-A-07 incomplete alternates should not be selected");
    TEST_ASSERT(selection.SelectedAltIndex() == -1, "TC-A-07 selected index should stay at the sentinel");
}

static void Test_TC_N_05_ConnectDiagnostic_ContainsEndpointAndDriverStatus()
{
    // Given: A failed endpoint scan with one endpoint present and driver status values.
    UsbEndpointDiscoveryPolicy::ConnectDiagnostic diagnostic;
    diagnostic.event = "alt_rollup";
    diagnostic.deviceIndex = 2;
    diagnostic.altIndex = 1;
    diagnostic.endpointIndex = 4;
    diagnostic.address = 0x84u;
    diagnostic.attributes = UsbEndpointDiscoveryPolicy::kInterruptAttribute;
    diagnostic.hasEp2 = true;
    diagnostic.hasEp4 = false;
    diagnostic.hasEp6 = false;
    diagnostic.result = -7;
    diagnostic.usbdStatus = 0xC0000001u;
    diagnostic.ntStatus = 0xC0000002u;
    diagnostic.lastError = 31u;
    diagnostic.win32LastError = 87u;

    // When: The fixed connect diagnostic line is formatted.
    char line[512] = { 0 };
    const bool formatted = UsbEndpointDiscoveryPolicy::FormatConnectDiagnostic(
        diagnostic,
        line,
        sizeof(line));

    // Then: Field diagnosis can recover descriptor, presence, and all status values.
    TEST_ASSERT(formatted, "TC-N-05 diagnostic must fit the fixed buffer");
    TEST_ASSERT(std::strstr(line, "[PR01][DLL][CONNECT]") != nullptr, "TC-N-05 prefix must be stable");
    TEST_ASSERT(std::strstr(line, "event=alt_rollup") != nullptr, "TC-N-05 event must be present");
    TEST_ASSERT(std::strstr(line, "addr=0x84 attr=3") != nullptr, "TC-N-05 endpoint descriptor must be present");
    TEST_ASSERT(std::strstr(line, "ep2=1 ep4=0 ep6=0") != nullptr, "TC-N-05 endpoint presence must be present");
    TEST_ASSERT(std::strstr(line, "result=-7") != nullptr, "TC-N-05 result must be present");
    TEST_ASSERT(std::strstr(line, "usbdStatus=0xC0000001") != nullptr, "TC-N-05 USBD status must be present");
    TEST_ASSERT(std::strstr(line, "ntStatus=0xC0000002") != nullptr, "TC-N-05 NT status must be present");
    TEST_ASSERT(std::strstr(line, "lastError=31") != nullptr, "TC-N-05 LastError must be present");
    TEST_ASSERT(std::strstr(line, "win32LastError=87") != nullptr, "TC-N-05 Win32 LastError must be present");
}

static void Test_TC_B_01_ConnectDiagnostic_RejectsMissingOrTruncatedBuffer()
{
    // Given: A valid diagnostic payload.
    UsbEndpointDiscoveryPolicy::ConnectDiagnostic diagnostic;
    diagnostic.event = "endpoint";

    // When/Then: Null, zero-length, and truncated outputs fail closed.
    char tiny[8] = { 0 };
    TEST_ASSERT(!UsbEndpointDiscoveryPolicy::FormatConnectDiagnostic(diagnostic, nullptr, 512u),
        "TC-B-01 null output must fail");
    TEST_ASSERT(!UsbEndpointDiscoveryPolicy::FormatConnectDiagnostic(diagnostic, tiny, 0u),
        "TC-B-01 zero-length output must fail");
    TEST_ASSERT(!UsbEndpointDiscoveryPolicy::FormatConnectDiagnostic(diagnostic, tiny, sizeof(tiny)),
        "TC-B-01 truncated output must fail");
}

int main()
{
    std::printf("=== UsbEndpointDiscoveryPolicy Unit Tests ===\n\n");

    RUN_TEST(Test_TC_N_01_ResolvesRequiredEndpointsByAddress);
    RUN_TEST(Test_TC_N_02_IgnoresEnumerationOrder);
    RUN_TEST(Test_TC_N_03_IgnoresUnknownEndpoints);
    RUN_TEST(Test_TC_N_04_SelectsOnlyACompleteAlternateInterface);
    RUN_TEST(Test_TC_N_05_ConnectDiagnostic_ContainsEndpointAndDriverStatus);
    RUN_TEST(Test_TC_N_06_MultipleCompleteAlternates_PreserveLegacyLastWins);
    RUN_TEST(Test_TC_A_01_IgnoresNullEndpointTokens);
    RUN_TEST(Test_TC_A_02_RejectsMissingEp2);
    RUN_TEST(Test_TC_A_03_RejectsMissingEp4);
    RUN_TEST(Test_TC_A_04_RejectsMissingEp6);
    RUN_TEST(Test_TC_A_05_RejectsWrongTransferAttributes);
    RUN_TEST(Test_TC_A_06_RejectsEmptyEndpointSet);
    RUN_TEST(Test_TC_A_07_RejectsIncompleteAlternateInterfaces);
    RUN_TEST(Test_TC_B_01_ConnectDiagnostic_RejectsMissingOrTruncatedBuffer);

    std::printf(
        "\n=== Results: %d assertions, %d passed, %d failed ===\n",
        g_TestCount,
        g_PassCount,
        g_FailCount);
    return g_FailCount == 0 ? 0 : 1;
}
