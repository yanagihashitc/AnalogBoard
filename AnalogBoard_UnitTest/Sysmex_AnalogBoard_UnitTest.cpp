#include <Windows.h>
#include <climits>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

#ifndef AFX_EXT_CLASS
#define AFX_EXT_CLASS
#endif

#include "..\AnalogBoard_Dll\AnalogBoard_Dll.h"
#include "..\AnalogBoard_TestApp\WaveDataPublishLogic.h"

namespace {

struct TestCase {
	const char* id;
	const char* description;
	std::function<bool(void)> run;
};

bool AssertEqualInt(const char* caseId, INT expected, INT actual)
{
	if (expected != actual)
	{
		std::printf("[%s] FAIL: expected=%d actual=%d\n", caseId, expected, actual);
		return false;
	}
	std::printf("[%s] PASS\n", caseId);
	return true;
}

bool AssertTrue(const char* caseId, bool value, const char* message)
{
	if (!value)
	{
		std::printf("[%s] FAIL: %s\n", caseId, message);
		return false;
	}
	std::printf("[%s] PASS\n", caseId);
	return true;
}

bool AssertStringEqual(const char* caseId, const char* expected, const char* actual)
{
	if ((expected == nullptr) || (actual == nullptr))
	{
		std::printf("[%s] FAIL: null string pointer detected.\n", caseId);
		return false;
	}

	if (std::strcmp(expected, actual) != 0)
	{
		std::printf("[%s] FAIL: expected=\"%s\" actual=\"%s\"\n", caseId, expected, actual);
		return false;
	}

	std::printf("[%s] PASS\n", caseId);
	return true;
}

class FakePublishOps final : public wave_data_publish::IPublishOps
{
public:
	int lowCloseRet = 0;
	int highCloseRet = 0;
	bool rollbackRet = true;

	int lowCloseCalls = 0;
	int highCloseCalls = 0;
	int rollbackCalls = 0;
	int cleanupCalls = 0;

	int CloseAndRenameLow(const std::wstring&, const std::wstring&) override
	{
		++lowCloseCalls;
		return lowCloseRet;
	}

	int CloseAndRenameHigh(const std::wstring&, const std::wstring&) override
	{
		++highCloseCalls;
		return highCloseRet;
	}

	bool RollbackLowFile(const std::wstring&, const std::wstring&) override
	{
		++rollbackCalls;
		return rollbackRet;
	}

	void CleanupOnError(const std::wstring&, const std::wstring&) override
	{
		++cleanupCalls;
	}
};

} // namespace

int main()
{
	std::vector<TestCase> tests;

	tests.push_back({
		"TC-N-01",
		"Constructed state is disconnected",
		[]() {
			// Given: A newly constructed USB_Lib_Info instance.
			USB_Lib_Info usbLib;
			// When: The connection state is checked immediately.
			const bool isDisconnected = (usbLib.isConnected == FALSE);
			// Then: It should be disconnected.
			return AssertTrue("TC-N-01", isDisconnected, "isConnected should be FALSE after construction.");
		}
	});

	tests.push_back({
		"TC-N-02",
		"DllVersion_Get returns current DLL version",
		[]() {
			// Given: A USB_Lib_Info instance.
			USB_Lib_Info usbLib;
			// When: DllVersion_Get is called.
			const char* version = usbLib.DllVersion_Get();
			// Then: It should match the current version string.
			if (!AssertTrue("TC-N-02", version != nullptr, "version pointer should not be null."))
			{
				return false;
			}
			return AssertEqualInt("TC-N-02", 0, std::strcmp(version, "1.0.0"));
		}
	});

	tests.push_back({
		"TC-N-03",
		"USBBoard_Disconnect is safe on a fresh instance",
		[]() {
			// Given: A newly constructed USB_Lib_Info instance.
			USB_Lib_Info usbLib;
			// When: USBBoard_Disconnect is called without prior connection.
			usbLib.USBBoard_Disconnect();
			// Then: The state should remain disconnected.
			return AssertTrue("TC-N-03", usbLib.isConnected == FALSE, "isConnected should remain FALSE after disconnect.");
		}
	});

	tests.push_back({
		"TC-N-04",
		"USBBoard_Disconnect is idempotent",
		[]() {
			// Given: A newly constructed USB_Lib_Info instance.
			USB_Lib_Info usbLib;
			// When: USBBoard_Disconnect is called repeatedly.
			usbLib.USBBoard_Disconnect();
			usbLib.USBBoard_Disconnect();
			// Then: The state should remain disconnected.
			return AssertTrue("TC-N-04", usbLib.isConnected == FALSE, "repeated disconnect should keep isConnected FALSE.");
		}
	});

	tests.push_back({
		"TC-N-05",
		"DllVersion_Get remains consistent across calls and instances",
		[]() {
			// Given: Two independent USB_Lib_Info instances.
			USB_Lib_Info usbLibA;
			USB_Lib_Info usbLibB;
			// When: The version is fetched multiple times.
			const char* v1 = usbLibA.DllVersion_Get();
			const char* v2 = usbLibA.DllVersion_Get();
			const char* v3 = usbLibB.DllVersion_Get();
			// Then: Version strings should be identical and pointer should be stable.
			if (!AssertStringEqual("TC-N-05", "1.0.0", v1))
			{
				return false;
			}
			if (!AssertStringEqual("TC-N-05", v1, v2))
			{
				return false;
			}
			if (!AssertStringEqual("TC-N-05", v1, v3))
			{
				return false;
			}
			return AssertTrue("TC-N-05", (v1 == v2) && (v1 == v3), "version pointer should be stable.");
		}
	});

	tests.push_back({
		"TC-A-01",
		"USBBoard_Connect with NULL HWND",
		[]() {
			// Given: A USB_Lib_Info instance.
			USB_Lib_Info usbLib;
			// When: USBBoard_Connect is called with NULL handle.
			const INT actual = usbLib.USBBoard_Connect(NULL);
			// Then: It should return USB_ERR_NULLPOINTER.
			return AssertEqualInt("TC-A-01", USB_ERR_NULLPOINTER, actual);
		}
	});

	tests.push_back({
		"TC-A-02",
		"EP2_SendData with NULL buffer while endpoint is not initialized",
		[]() {
			// Given: A USB_Lib_Info instance without successful connect.
			USB_Lib_Info usbLib;
			// When: EP2_SendData is called.
			const INT actual = usbLib.EP2_SendData(NULL);
			// Then: It should return USB_ERR_INVALID_ENDPOINTER first.
			return AssertEqualInt("TC-A-02", USB_ERR_INVALID_ENDPOINTER, actual);
		}
	});

	tests.push_back({
		"TC-A-03",
		"EP4_GetData with NULL buffer while endpoint is not initialized",
		[]() {
			// Given: A USB_Lib_Info instance without successful connect.
			USB_Lib_Info usbLib;
			// When: EP4_GetData is called.
			const INT actual = usbLib.EP4_GetData(NULL);
			// Then: It should return USB_ERR_INVALID_ENDPOINTER first.
			return AssertEqualInt("TC-A-03", USB_ERR_INVALID_ENDPOINTER, actual);
		}
	});

	tests.push_back({
		"TC-A-04",
		"USBBoard_Connect(NULL) keeps disconnected state",
		[]() {
			// Given: A newly constructed USB_Lib_Info instance.
			USB_Lib_Info usbLib;
			// When: USBBoard_Connect is called with NULL handle.
			const INT connectResult = usbLib.USBBoard_Connect(NULL);
			// Then: The error code should be NULL pointer and state should remain disconnected.
			if (!AssertEqualInt("TC-A-04", USB_ERR_NULLPOINTER, connectResult))
			{
				return false;
			}
			return AssertTrue("TC-A-04", usbLib.isConnected == FALSE, "isConnected should remain FALSE after failed connect.");
		}
	});

	tests.push_back({
		"TC-A-05",
		"EP6_GetData with NULL buffer while endpoint is not initialized",
		[]() {
			// Given: A USB_Lib_Info instance without successful connect.
			USB_Lib_Info usbLib;
			// When: EP6_GetData is called with NULL buffer.
			const INT actual = usbLib.EP6_GetData(NULL, 0);
			// Then: It should return USB_ERR_INVALID_ENDPOINTER first.
			return AssertEqualInt("TC-A-05", USB_ERR_INVALID_ENDPOINTER, actual);
		}
	});

	tests.push_back({
		"TC-B-01",
		"EP6_GetData with DataSizeCount=0 while endpoint is not initialized",
		[]() {
			// Given: A USB_Lib_Info instance without successful connect and a valid 1-byte buffer.
			USB_Lib_Info usbLib;
			BYTE buffer[1] = { 0 };
			// When: EP6_GetData is called with minimum data size 0.
			const INT actual = usbLib.EP6_GetData(buffer, 0);
			// Then: It should return USB_ERR_INVALID_ENDPOINTER first.
			return AssertEqualInt("TC-B-01", USB_ERR_INVALID_ENDPOINTER, actual);
		}
	});

	tests.push_back({
		"TC-B-02",
		"EP6_GetData with DataSizeCount=1 while endpoint is not initialized",
		[]() {
			// Given: A USB_Lib_Info instance without successful connect and a valid 1-byte buffer.
			USB_Lib_Info usbLib;
			BYTE buffer[1] = { 0 };
			// When: EP6_GetData is called with minimum + 1 data size.
			const INT actual = usbLib.EP6_GetData(buffer, 1);
			// Then: It should return USB_ERR_INVALID_ENDPOINTER first.
			return AssertEqualInt("TC-B-02", USB_ERR_INVALID_ENDPOINTER, actual);
		}
	});

	tests.push_back({
		"TC-B-03",
		"EP6_GetData with DataSizeCount=UINT_MAX while endpoint is not initialized",
		[]() {
			// Given: A USB_Lib_Info instance without successful connect and a valid 1-byte buffer.
			USB_Lib_Info usbLib;
			BYTE buffer[1] = { 0 };
			// When: EP6_GetData is called with maximum unsigned int data size.
			const INT actual = usbLib.EP6_GetData(buffer, UINT_MAX);
			// Then: It should return USB_ERR_INVALID_ENDPOINTER first.
			return AssertEqualInt("TC-B-03", USB_ERR_INVALID_ENDPOINTER, actual);
		}
	});

	tests.push_back({
		"TC-B-04",
		"EP6_GetData keeps returning endpoint error after failed connect",
		[]() {
			// Given: A USB_Lib_Info instance where connect failed by NULL handle.
			USB_Lib_Info usbLib;
			const INT connectResult = usbLib.USBBoard_Connect(NULL);
			BYTE buffer[1] = { 0 };
			// When: EP6_GetData is called after failed connect.
			const INT actual = usbLib.EP6_GetData(buffer, 1);
			// Then: Connect should fail with NULL pointer and endpoint should still be invalid.
			if (!AssertEqualInt("TC-B-04", USB_ERR_NULLPOINTER, connectResult))
			{
				return false;
			}
			return AssertEqualInt("TC-B-04", USB_ERR_INVALID_ENDPOINTER, actual);
		}
	});

	tests.push_back({
		"TC-P-01",
		"PublishFilePair returns invalid-arg when any path is empty",
		[]() {
			// Given: Empty path fields and fake operations.
			wave_data_publish::FilePairNames names;
			FakePublishOps ops;
			names.lowTmpFileName = L"";
			names.highTmpFileName = L"h.tmp";
			names.lowFinalFileName = L"l.bin";
			names.highFinalFileName = L"h.bin";
			// When: PublishFilePair is executed.
			const int actual = wave_data_publish::PublishFilePair(names, ops);
			// Then: Invalid-arg error should be returned and no file operation should run.
			if (!AssertEqualInt("TC-P-01", wave_data_publish::kPublishErrInvalidArg, actual))
			{
				return false;
			}
			return AssertTrue("TC-P-01",
				ops.lowCloseCalls == 0 && ops.highCloseCalls == 0 && ops.rollbackCalls == 0 && ops.cleanupCalls == 0,
				"no operation should be called for invalid argument.");
		}
	});

	tests.push_back({
		"TC-P-02",
		"PublishFilePair returns low-file error when low close/rename fails",
		[]() {
			// Given: Valid names and low close failure.
			wave_data_publish::FilePairNames names;
			FakePublishOps ops;
			names.lowTmpFileName = L"l.tmp";
			names.highTmpFileName = L"h.tmp";
			names.lowFinalFileName = L"l.bin";
			names.highFinalFileName = L"h.bin";
			ops.lowCloseRet = -2;
			// When: PublishFilePair is executed.
			const int actual = wave_data_publish::PublishFilePair(names, ops);
			// Then: Low-file mapped error should be returned and cleanup should run.
			if (!AssertEqualInt("TC-P-02", wave_data_publish::kPublishErrLowFileBase - 2, actual))
			{
				return false;
			}
			return AssertTrue("TC-P-02",
				ops.lowCloseCalls == 1 && ops.highCloseCalls == 0 && ops.rollbackCalls == 0 && ops.cleanupCalls == 1,
				"expected low close once and cleanup once.");
		}
	});

	tests.push_back({
		"TC-P-03",
		"PublishFilePair returns high-file error when high close/rename fails and rollback succeeds",
		[]() {
			// Given: Valid names and high close failure with successful rollback.
			wave_data_publish::FilePairNames names;
			FakePublishOps ops;
			names.lowTmpFileName = L"l.tmp";
			names.highTmpFileName = L"h.tmp";
			names.lowFinalFileName = L"l.bin";
			names.highFinalFileName = L"h.bin";
			ops.highCloseRet = -3;
			ops.rollbackRet = true;
			// When: PublishFilePair is executed.
			const int actual = wave_data_publish::PublishFilePair(names, ops);
			// Then: High-file mapped error should be returned.
			if (!AssertEqualInt("TC-P-03", wave_data_publish::kPublishErrHighFileBase - 3, actual))
			{
				return false;
			}
			return AssertTrue("TC-P-03",
				ops.lowCloseCalls == 1 && ops.highCloseCalls == 1 && ops.rollbackCalls == 1 && ops.cleanupCalls == 1,
				"expected low/high close once, rollback once, and cleanup once.");
		}
	});

	tests.push_back({
		"TC-P-04",
		"PublishFilePair returns rollback error when high close fails and rollback also fails",
		[]() {
			// Given: Valid names and high close failure with rollback failure.
			wave_data_publish::FilePairNames names;
			FakePublishOps ops;
			names.lowTmpFileName = L"l.tmp";
			names.highTmpFileName = L"h.tmp";
			names.lowFinalFileName = L"l.bin";
			names.highFinalFileName = L"h.bin";
			ops.highCloseRet = -3;
			ops.rollbackRet = false;
			// When: PublishFilePair is executed.
			const int actual = wave_data_publish::PublishFilePair(names, ops);
			// Then: Rollback mapped error should be returned.
			if (!AssertEqualInt("TC-P-04", wave_data_publish::kPublishErrRollbackBase - 3, actual))
			{
				return false;
			}
			return AssertTrue("TC-P-04",
				ops.lowCloseCalls == 1 && ops.highCloseCalls == 1 && ops.rollbackCalls == 1 && ops.cleanupCalls == 1,
				"expected low/high close once, rollback once, and cleanup once.");
		}
	});

	tests.push_back({
		"TC-P-05",
		"PublishFilePair succeeds and clears file names",
		[]() {
			// Given: Valid names and all operations succeed.
			wave_data_publish::FilePairNames names;
			FakePublishOps ops;
			names.lowTmpFileName = L"low.tmp";
			names.highTmpFileName = L"high.tmp";
			names.lowFinalFileName = L"low.bin";
			names.highFinalFileName = L"high.bin";
			// When: PublishFilePair is executed.
			const int actual = wave_data_publish::PublishFilePair(names, ops);
			// Then: Success should be returned and names should be cleared.
			if (!AssertEqualInt("TC-P-05", 0, actual))
			{
				return false;
			}
			return AssertTrue("TC-P-05",
				names.lowTmpFileName.empty() && names.highTmpFileName.empty() &&
				names.lowFinalFileName.empty() && names.highFinalFileName.empty() &&
				ops.cleanupCalls == 0,
				"all names should be cleared on success without cleanup.");
		}
	});

	tests.push_back({
		"TC-B-05",
		"PublishFilePair handles minimum non-empty path length",
		[]() {
			// Given: One-character paths and successful operations.
			wave_data_publish::FilePairNames names;
			FakePublishOps ops;
			names.lowTmpFileName = L"a";
			names.highTmpFileName = L"b";
			names.lowFinalFileName = L"c";
			names.highFinalFileName = L"d";
			// When: PublishFilePair is executed.
			const int actual = wave_data_publish::PublishFilePair(names, ops);
			// Then: Success should be returned.
			if (!AssertEqualInt("TC-B-05", 0, actual))
			{
				return false;
			}
			return AssertTrue("TC-B-05",
				ops.lowCloseCalls == 1 && ops.highCloseCalls == 1 && ops.rollbackCalls == 0 && ops.cleanupCalls == 0,
				"expected only low/high close operations.");
		}
	});

	int failed = 0;

	for (const TestCase& test : tests)
	{
		std::printf("Running %s: %s\n", test.id, test.description);
		if (!test.run())
		{
			++failed;
		}
	}

	std::printf("\nTotal: %zu, Failed: %d\n", tests.size(), failed);
	return failed == 0 ? 0 : 1;
}
