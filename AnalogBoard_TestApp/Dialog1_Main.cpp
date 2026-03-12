// Dialog1_Main.cpp : implementation file
//

#include "pch.h"
#include "AnalogBoard_TestApp.h"
#include "afxdialogex.h"
#include "Dialog1_Main.h"
#include "AnalogBoard_TestAppDlg.h"
#include "locale.h"
#include "afxwin.h"
#include "../AnalogBoard_Dll/AnalogBoard_Dll.h"
#include "FpgaRegisterLogic.h"
#include "SavePathValidation.h"
#include "WaveDataFileIO.h"
#include "WaveAcquisitionEngine.h"

#define LOG_SWITCH		0

/************Global variables**************/
Dialog1_Main* pMainDlg;
FPGAConfigI_REGMAP	packetConfig;
PBYTE pEp2DataBuf = NULL;//Ep2 data buffer
PBYTE pEp4DataBuf = NULL;//Ep4 data buffer
CWinThread* pLpTestThread_EP2_EP4;
CWinThread* pLpTestThread_EP6_GetData;
INT g_bEP24LoopFlag = 0;
INT g_bEP6ThreadFlag = 0;
INT g_bStartSampling = 0;

void LoopTestProcessThread_EP2_EP4(LPVOID lpParam);
void LoopTestProcessThread_EP6_GetData(LPVOID lpParam);
INT SaveWaveDataToFile(CFile* fp_l, CFile* fp_h, const BYTE* WaveData, ULONG FrameSize_L, ULONG FrameSize_H, INT WaveCnt);
INT SaveWaveDataToCHFile(CFile fp[12], PBYTE WaveData, ULONG FrameSize_L, ULONG FrameSize_H, INT WaveCnt, ULONG OneHighSize, ULONG OneLowSize);
INT CreateWaveDataFile(
	CFile* fp_h,
	CFile* fp_l,
	const CString& TimeStamp,
	INT Index,
	CString* outFinalPath_l,
	CString* outFinalPath_h,
	CString* outTmpPath_l,
	CString* outTmpPath_h,
	DWORD* outLastError);

namespace
{
	// Timing-isolation switch for investigating the current-version EP6 issue.
	// Keep high-frequency EP6 logs off to reduce UI/file I/O interference.
	constexpr bool kEnableEp6HotPathLogs = false;
	constexpr bool kEnablePhase0CycleSummaryLog = true;

	enum CreateWaveDataFileResult
	{
		CreateWaveDataFileOk = 0,
		CreateWaveDataFileOpenLowFailed = -1001,
		CreateWaveDataFileOpenHighFailed = -1002,
	};

	constexpr INT kUsbErrFileIo = -10020;
	constexpr INT kUsbErrFileRename = -10021;
	constexpr ULONG kDefaultMaxReadChunkBytes = 1024u * 1024u * 256u;
	constexpr DWORD kRenameRetryWaitMs = 100;
	// Total attempts = 1 initial attempt + kRenameMaxRetries.
	constexpr int kRenameMaxRetries = 3;

	class CFileWriterAdapter
	{
	public:
		explicit CFileWriterAdapter(CFile* file)
			: file_(file)
		{
		}

		bool Write(const BYTE* data, ULONG size)
		{
			if (file_ == nullptr || file_->m_hFile == CFile::hFileNull)
			{
				::SetLastError(ERROR_INVALID_HANDLE);
				return false;
			}

			try
			{
				file_->Write(data, size);
			}
			catch (CFileException* ex)
			{
				const DWORD osError = static_cast<DWORD>(ex->m_lOsError);
				::SetLastError(osError != ERROR_SUCCESS ? osError : ERROR_WRITE_FAULT);
				ex->Delete();
				return false;
			}

			return true;
		}

	private:
		CFile* file_ = nullptr;
	};

	bool FlushAndCloseFile(CFile* file, DWORD* outLastError)
	{
		if (outLastError != nullptr)
		{
			*outLastError = ERROR_SUCCESS;
		}

		if (file == nullptr || file->m_hFile == CFile::hFileNull)
		{
			return true;
		}

		try
		{
			file->Flush();
		}
		catch (CFileException* ex)
		{
			const DWORD osError = static_cast<DWORD>(ex->m_lOsError);
			if (outLastError != nullptr)
			{
				*outLastError = osError != ERROR_SUCCESS ? osError : ERROR_WRITE_FAULT;
			}
			ex->Delete();

			try
			{
				file->Close();
			}
			catch (CFileException* closeEx)
			{
				closeEx->Delete();
			}

			return false;
		}

		try
		{
			file->Close();
		}
		catch (CFileException* ex)
		{
			const DWORD osError = static_cast<DWORD>(ex->m_lOsError);
			if (outLastError != nullptr)
			{
				*outLastError = osError != ERROR_SUCCESS ? osError : ERROR_WRITE_FAULT;
			}
			ex->Delete();
			return false;
		}

		return true;
	}

	void LogFileIoEvent(
		Dialog1_Main* curObject,
		LPCTSTR api,
		INT index,
		const CString& tmpPath,
		const CString& finalPath,
		DWORD lastError,
		const CString& detail)
	{
		if (curObject == nullptr || curObject->m_pMainDlg == nullptr)
		{
			return;
		}

		CString line;
		line.Format(
			_T("[FileIO] api=%s index=%d tmpPath=%s finalPath=%s GetLastError=%lu %s"),
			api,
			index,
			tmpPath.GetString(),
			finalPath.GetString(),
			lastError,
			detail.GetString());
		curObject->m_pMainDlg->PrintLog(line);
	}

	SavePathValidation::Result ValidateSavePathInternal(const CString& rawSavePath)
	{
		return SavePathValidation::ValidateSavePath(std::wstring(rawSavePath.GetString()));
	}

	void CleanupResidualTmpFiles(Dialog1_Main* curObject, const CString& normalizedSavePath)
	{
		const WaveDataFileIO::CleanupTmpResult cleanupResult =
			WaveDataFileIO::CleanupResidualBinTmpFiles(normalizedSavePath.GetString());

		for (const WaveDataFileIO::CleanupFailureInfo& fail : cleanupResult.failures)
		{
			LogFileIoEvent(curObject, _T("DeleteFile"), -1, fail.path.c_str(), _T(""), fail.lastError, _T("startup tmp cleanup warning"));
		}

		CString detail;
		detail.Format(_T("startup tmp cleanup count=%d failed=%d"), cleanupResult.deletedCount, cleanupResult.failedCount);
		LogFileIoEvent(curObject, _T("startup_cleanup"), -1, normalizedSavePath, _T(""), ERROR_SUCCESS, detail);
	}

	INT FlushCloseAndPublishWavePair(
		Dialog1_Main* curObject,
		CFile* fileLow,
		CFile* fileHigh,
		const CString& tmpPathLow,
		const CString& finalPathLow,
		const CString& tmpPathHigh,
		const CString& finalPathHigh,
		INT index)
	{
		DWORD closeErrorLow = ERROR_SUCCESS;
		DWORD closeErrorHigh = ERROR_SUCCESS;
		if (!FlushAndCloseFile(fileLow, &closeErrorLow))
		{
			LogFileIoEvent(curObject, _T("Close"), index, tmpPathLow, finalPathLow, closeErrorLow, _T("close low failed"));
			return kUsbErrFileIo;
		}
		LogFileIoEvent(curObject, _T("Close"), index, tmpPathLow, finalPathLow, ERROR_SUCCESS, _T("close low success"));

		if (!FlushAndCloseFile(fileHigh, &closeErrorHigh))
		{
			LogFileIoEvent(curObject, _T("Close"), index, tmpPathHigh, finalPathHigh, closeErrorHigh, _T("close high failed"));
			return kUsbErrFileIo;
		}
		LogFileIoEvent(curObject, _T("Close"), index, tmpPathHigh, finalPathHigh, ERROR_SUCCESS, _T("close high success"));

		const WaveDataFileIO::PublishPairResult publishResult = WaveDataFileIO::PublishWavePairAtomic(
			tmpPathLow.GetString(),
			finalPathLow.GetString(),
			tmpPathHigh.GetString(),
			finalPathHigh.GetString(),
			kRenameRetryWaitMs,
			kRenameMaxRetries);

		if (publishResult.low.success)
		{
			CString detail;
			detail.Format(_T("rename success retried=%d"), publishResult.low.retried ? 1 : 0);
			LogFileIoEvent(curObject, _T("MoveFileEx"), index, tmpPathLow, finalPathLow, ERROR_SUCCESS, detail);
		}
		else
		{
			CString detail;
			detail.Format(_T("rename fail retried=%d"), publishResult.low.retried ? 1 : 0);
			LogFileIoEvent(curObject, _T("MoveFileEx"), index, tmpPathLow, finalPathLow, publishResult.low.lastError, detail);
			return kUsbErrFileRename;
		}

		if (publishResult.high.success)
		{
			CString detail;
			detail.Format(_T("rename success retried=%d"), publishResult.high.retried ? 1 : 0);
			LogFileIoEvent(curObject, _T("MoveFileEx"), index, tmpPathHigh, finalPathHigh, ERROR_SUCCESS, detail);
			return USB_SUCCESS;
		}

		CString highDetail;
		highDetail.Format(_T("rename fail retried=%d"), publishResult.high.retried ? 1 : 0);
		LogFileIoEvent(curObject, _T("MoveFileEx"), index, tmpPathHigh, finalPathHigh, publishResult.high.lastError, highDetail);

		CString rollbackDetail;
		rollbackDetail.Format(_T("rename rollback low deleted=%d"), publishResult.rollbackSucceeded ? 1 : 0);
		LogFileIoEvent(
			curObject,
			_T("DeleteFile"),
			index,
			finalPathLow,
			finalPathHigh,
			publishResult.rollbackSucceeded ? ERROR_SUCCESS : publishResult.rollbackLastError,
			rollbackDetail);
		return kUsbErrFileRename;
	}

	class RealUsbSessionAdapter : public WaveAcquisition::IUsbSession
	{
	public:
		explicit RealUsbSessionAdapter(USB_Lib_Info* usbLibInfo)
			: usbLibInfo_(usbLibInfo)
		{
		}

		INT Connect() override
		{
			return USB_SUCCESS;
		}

		void Disconnect() override
		{
		}

		INT EP2_SendData(BYTE* buffer, size_t bufferSize) override
		{
			(void)bufferSize;
			return usbLibInfo_ != nullptr ? usbLibInfo_->EP2_SendData(buffer) : WaveAcquisition::kAcquisitionErrInvalidConfig;
		}

		INT EP4_GetData(BYTE* buffer, size_t bufferSize) override
		{
			(void)bufferSize;
			return usbLibInfo_ != nullptr ? usbLibInfo_->EP4_GetData(buffer) : WaveAcquisition::kAcquisitionErrInvalidConfig;
		}

		INT EP6_GetData(BYTE* buffer, ULONG size) override
		{
			return usbLibInfo_ != nullptr ? usbLibInfo_->EP6_GetData(buffer, size) : WaveAcquisition::kAcquisitionErrInvalidConfig;
		}

	private:
		USB_Lib_Info* usbLibInfo_ = nullptr;
	};

	class DialogWavePairSink : public WaveAcquisition::IWavePairSink
	{
	public:
		DialogWavePairSink(Dialog1_Main* curObject, const CString& timeStampBase)
			: curObject_(curObject)
			, timeStampBase_(timeStampBase)
		{
		}

		INT OpenPair(INT index) override
		{
			DWORD openLastError = ERROR_SUCCESS;
			const INT result = CreateWaveDataFile(
				&fileHigh_,
				&fileLow_,
				timeStampBase_,
				index,
				&currentFinalPathLow_,
				&currentFinalPathHigh_,
				&currentTmpPathLow_,
				&currentTmpPathHigh_,
				&openLastError);
			if (result != CreateWaveDataFileOk)
			{
				LogFileIoEvent(curObject_, _T("Open"), index, currentTmpPathLow_, currentFinalPathLow_, openLastError, _T("tmp open fail"));
				return WaveAcquisition::kAcquisitionErrOpenPair;
			}

			LogFileIoEvent(curObject_, _T("Open"), index, currentTmpPathLow_, currentFinalPathLow_, ERROR_SUCCESS, _T("tmp open success"));
			LogFileIoEvent(curObject_, _T("Open"), index, currentTmpPathHigh_, currentFinalPathHigh_, ERROR_SUCCESS, _T("tmp open success"));
			currentIndex_ = index;
			hasOpenPair_ = true;
			return USB_SUCCESS;
		}

		INT Write(const BYTE* waveData, ULONG frameSizeLow, ULONG frameSizeHigh, INT waveCnt) override
		{
			const INT saveResult = SaveWaveDataToFile(
				&fileLow_,
				&fileHigh_,
				waveData,
				frameSizeLow,
				frameSizeHigh,
				waveCnt);
			if (saveResult != USB_SUCCESS)
			{
				LogFileIoEvent(curObject_, _T("Write"), currentIndex_, currentTmpPathLow_, currentFinalPathLow_, ::GetLastError(), _T("write low/high failed"));
				return WaveAcquisition::kAcquisitionErrWritePair;
			}

			CString writeDetail;
			writeDetail.Format(_T("write bytes=%zu"), static_cast<size_t>(waveCnt) * static_cast<size_t>(frameSizeLow + frameSizeHigh));
			LogFileIoEvent(curObject_, _T("Write"), currentIndex_, currentTmpPathLow_, currentFinalPathLow_, ERROR_SUCCESS, writeDetail);
			return USB_SUCCESS;
		}

		INT PublishPair() override
		{
			const INT publishResult = FlushCloseAndPublishWavePair(
				curObject_,
				&fileLow_,
				&fileHigh_,
				currentTmpPathLow_,
				currentFinalPathLow_,
				currentTmpPathHigh_,
				currentFinalPathHigh_,
				currentIndex_);
			if (publishResult != USB_SUCCESS)
			{
				return WaveAcquisition::kAcquisitionErrPublishPair;
			}

			currentFinalPathLow_.Empty();
			currentFinalPathHigh_.Empty();
			currentTmpPathLow_.Empty();
			currentTmpPathHigh_.Empty();
			currentIndex_ = 0;
			hasOpenPair_ = false;
			return USB_SUCCESS;
		}

		void AbortPair() override
		{
			DWORD closeLastErrorLow = ERROR_SUCCESS;
			DWORD closeLastErrorHigh = ERROR_SUCCESS;
			if (!FlushAndCloseFile(&fileLow_, &closeLastErrorLow))
			{
				LogFileIoEvent(curObject_, _T("Close"), currentIndex_, currentTmpPathLow_, currentFinalPathLow_, closeLastErrorLow, _T("abort low close failed"));
			}
			if (!FlushAndCloseFile(&fileHigh_, &closeLastErrorHigh))
			{
				LogFileIoEvent(curObject_, _T("Close"), currentIndex_, currentTmpPathHigh_, currentFinalPathHigh_, closeLastErrorHigh, _T("abort high close failed"));
			}
			if (!currentTmpPathLow_.IsEmpty())
			{
				::DeleteFile(currentTmpPathLow_);
			}
			if (!currentTmpPathHigh_.IsEmpty())
			{
				::DeleteFile(currentTmpPathHigh_);
			}

			currentFinalPathLow_.Empty();
			currentFinalPathHigh_.Empty();
			currentTmpPathLow_.Empty();
			currentTmpPathHigh_.Empty();
			currentIndex_ = 0;
			hasOpenPair_ = false;
		}

		bool HasOpenPair() const override
		{
			return hasOpenPair_;
		}

	private:
		Dialog1_Main* curObject_ = nullptr;
		CString timeStampBase_;
		CFile fileLow_;
		CFile fileHigh_;
		CString currentFinalPathLow_;
		CString currentFinalPathHigh_;
		CString currentTmpPathLow_;
		CString currentTmpPathHigh_;
		INT currentIndex_ = 0;
		bool hasOpenPair_ = false;
	};

	class DialogAcquisitionObserver : public WaveAcquisition::IAcquisitionObserver
	{
	public:
		explicit DialogAcquisitionObserver(Dialog1_Main* curObject)
			: curObject_(curObject)
		{
		}

		void OnLog(const std::wstring& message) override
		{
			if (curObject_ != nullptr && curObject_->m_pMainDlg != nullptr)
			{
				curObject_->m_pMainDlg->PrintLog(message.c_str());
			}
		}

		void OnCollectedWaveCount(ULONG collectedWaveCount) override
		{
			if (curObject_ != nullptr)
			{
				CString countText;
				countText.Format(_T("%lu"), collectedWaveCount);
				curObject_->m_CtrlEditCollectedCnt.SetWindowText(countText);
			}
		}

		void OnCycleSummary(const WaveAcquisition::AcquisitionSummary& summary) override
		{
			if (curObject_ == nullptr || curObject_->m_pMainDlg == nullptr)
			{
				return;
			}

			CString line;
			line.Format(
				_T("[PR01][CYCLE] status=%s error=%d ep6Calls=%I64u ep6Bytes=%I64u ep6Timeouts=%lu ep6AvgMs=%I64u ep6MaxMs=%I64u saveCalls=%I64u saveBytes=%I64u saveAvgMs=%I64u saveMaxMs=%I64u ddrPolls=%lu ddrWaitPolls=%lu settlingPolls=%d sawWrEndClear=%d maxBacklogBytes=%lu WAVE_WR_CNT=%lu WAVE_RD_CNT=%lu DDR_WR_END=%d DDR_RD_END=%d ignoredTail=%lu"),
				WaveAcquisition::WaveAcquisitionEngine::ToString(summary.terminalStatus),
				summary.errorCode,
				summary.metrics.ep6.callCount,
				summary.metrics.ep6.totalBytes,
				summary.metrics.ep6TimeoutCount,
				summary.metrics.GetEp6AverageElapsedMs(),
				summary.metrics.ep6.maxElapsedMs,
				summary.metrics.save.callCount,
				summary.metrics.save.totalBytes,
				summary.metrics.GetSaveAverageElapsedMs(),
				summary.metrics.save.maxElapsedMs,
				summary.metrics.ddrStatusPollCount,
				summary.metrics.ddrWriteWaitPollCount,
				summary.settlingPollCount,
				summary.sawDdrWrEndClear ? 1 : 0,
				summary.metrics.maxWaveBacklogBytes,
				summary.metrics.latestWaveWrCnt,
				summary.metrics.latestWaveRdCnt,
				summary.metrics.latestDdrWrEnd,
				summary.metrics.latestDdrRdEnd,
				summary.ignoredTailBytes);
			if (kEnablePhase0CycleSummaryLog)
			{
				curObject_->m_pMainDlg->PrintLog(line);
			}
		}

	private:
		Dialog1_Main* curObject_ = nullptr;
	};

	class GlobalFlagStopToken : public WaveAcquisition::IStopToken
	{
	public:
		bool IsStopRequested() const override
		{
			return g_bEP6ThreadFlag == 0;
		}
	};

	WaveAcquisition::AcquisitionSummary RunAcquisitionCycleWithEngine(
		Dialog1_Main* curObject,
		const CString& timeStampBase,
		ULONG oneWaveSizeLow,
		ULONG oneWaveSizeHigh)
	{
		RealUsbSessionAdapter usbSession(
			curObject != nullptr && curObject->m_pMainDlg != nullptr ? &curObject->m_pMainDlg->UsbLibInfo : nullptr);
		DialogWavePairSink sink(curObject, timeStampBase);
		DialogAcquisitionObserver observer(curObject);
		GlobalFlagStopToken stopToken;

		WaveAcquisition::RunConfig config = {};
		config.waveSizeLow = oneWaveSizeLow;
		config.waveSizeHigh = oneWaveSizeHigh;
		config.wavesPerFile = packetConfig.WaveNum;
		config.maxReadChunkBytes = kDefaultMaxReadChunkBytes;
		config.ep6TimeoutRetryLimit = 1;
		config.ep4PollSleepMs = 0;

		WaveAcquisition::WaveAcquisitionEngine engine(&usbSession, &sink, &observer, &stopToken);
		return engine.RunCycle(config);
	}
}

UINT editChSelect[] = {
	IDC_CHECK_CH1,
	IDC_CHECK_CH2,
	IDC_CHECK_CH3,
	IDC_CHECK_CH4,
	IDC_CHECK_CH5,
	IDC_CHECK_CH6,
	IDC_CHECK_CH7,
	IDC_CHECK_CH8,
	IDC_CHECK_CH9,
	IDC_CHECK_CH10,
	IDC_CHECK_CH11,
	IDC_CHECK_CH12,
	IDC_CHECK_CH13
};

UINT editTriggerRange[] = {
	IDC_EDIT_TRIGGER_VALUE,
	IDC_EDIT_TRIGGER_RANGE_LOW
};
// Dialog1_Main dialog

IMPLEMENT_DYNAMIC(Dialog1_Main, CDialogEx)

Dialog1_Main::Dialog1_Main(CAnalogBoardTestAppDlg* pParent /*=nullptr*/)
	: CDialogEx(IDD_DIALOG1_MAIN, pParent), m_pMainDlg(pParent)
{
	/* Init Global Variables */
#if 1
	strGainMultp[0][0][0] = -0.2;	//Low ch1 Gain1-1
	strGainMultp[0][0][1] = -0.4;	//Low ch1 Gain1-2
	strGainMultp[0][1][0] = -1.0;	//Low ch1 Gain2-1
	strGainMultp[0][1][1] = -4.0;	//Low ch1 Gain2-2
	strGainMultp[0][2][0] = -0.5;	//Low ch1 Gain3-1
	strGainMultp[0][2][1] = -1.0;	//Low ch1 Gain3-2
	strGainMultp[0][3][0] = 2.0;	//Low ch1 Gain4-1
	strGainMultp[0][3][1] = 4.0;	//Low ch1 Gain4-2
	strGainMultp[0][4][0] = -1.35;	//Low ch1 Gain5-1
	strGainMultp[0][4][1] = -1.35;	//Low ch1 Gain5-2

	strGainMultp[1][0][0] = -0.2;	//Low ch2 Gain1-1
	strGainMultp[1][0][1] = -0.4;	//Low ch2 Gain1-2
	strGainMultp[1][1][0] = -1.0;	//Low ch2 Gain2-1
	strGainMultp[1][1][1] = -4.0;	//Low ch2 Gain2-2
	strGainMultp[1][2][0] = -0.5;	//Low ch2 Gain3-1
	strGainMultp[1][2][1] = -1.0;	//Low ch2 Gain3-2
	strGainMultp[1][3][0] = 2.0;	//Low ch2 Gain4-1
	strGainMultp[1][3][1] = 4.0;	//Low ch2 Gain4-2
	strGainMultp[1][4][0] = -1.35;	//Low ch2 Gain5-1
	strGainMultp[1][4][1] = -1.35;	//Low ch2 Gain5-2

	strGainMultp[2][0][0] = -0.2;	//Low ch3 Gain1-1
	strGainMultp[2][0][1] = -0.4;	//Low ch3 Gain1-2
	strGainMultp[2][1][0] = -1.0;	//Low ch3 Gain2-1
	strGainMultp[2][1][1] = -4.0;	//Low ch3 Gain2-2
	strGainMultp[2][2][0] = -0.5;	//Low ch3 Gain3-1
	strGainMultp[2][2][1] = -1.0;	//Low ch3 Gain3-2
	strGainMultp[2][3][0] = 2.0;	//Low ch3 Gain4-1
	strGainMultp[2][3][1] = 4.0;	//Low ch3 Gain4-2
	strGainMultp[2][4][0] = -1.35;	//Low ch3 Gain5-1
	strGainMultp[2][4][1] = -1.35;	//Low ch3 Gain5-2

	strGainMultp[3][0][0] = -0.2;	//Low ch4 Gain1-1
	strGainMultp[3][0][1] = -0.4;	//Low ch4 Gain1-2
	strGainMultp[3][1][0] = -1.0;	//Low ch4 Gain2-1
	strGainMultp[3][1][1] = -4.0;	//Low ch4 Gain2-2
	strGainMultp[3][2][0] = -0.5;	//Low ch4 Gain3-1
	strGainMultp[3][2][1] = -1.0;	//Low ch4 Gain3-2
	strGainMultp[3][3][0] = 2.0;	//Low ch4 Gain4-1
	strGainMultp[3][3][1] = 4.0;	//Low ch4 Gain4-2
	strGainMultp[3][4][0] = -1.35;	//Low ch4 Gain5-1
	strGainMultp[3][4][1] = -1.35;	//Low ch4 Gain5-2

	strGainMultp[4][0][0] = -0.2;	//Low ch5 Gain1-1
	strGainMultp[4][0][1] = -0.4;	//Low ch5 Gain1-2
	strGainMultp[4][1][0] = -1.0;	//Low ch5 Gain2-1
	strGainMultp[4][1][1] = -4.0;	//Low ch5 Gain2-2
	strGainMultp[4][2][0] = -0.5;	//Low ch5 Gain3-1
	strGainMultp[4][2][1] = -1.0;	//Low ch5 Gain3-2
	strGainMultp[4][3][0] = 2.0;	//Low ch5 Gain4-1
	strGainMultp[4][3][1] = 4.0;	//Low ch5 Gain4-2
	strGainMultp[4][4][0] = -1.35;	//Low ch5 Gain5-1
	strGainMultp[4][4][1] = -1.35;	//Low ch5 Gain5-2

	strGainMultp[5][0][0] = -0.2;	//Low ch6 Gain1-1
	strGainMultp[5][0][1] = -0.4;	//Low ch6 Gain1-2
	strGainMultp[5][1][0] = -1.0;	//Low ch6 Gain2-1
	strGainMultp[5][1][1] = -4.0;	//Low ch6 Gain2-2
	strGainMultp[5][2][0] = -0.5;	//Low ch6 Gain3-1
	strGainMultp[5][2][1] = -1.0;	//Low ch6 Gain3-2
	strGainMultp[5][3][0] = 2.0;	//Low ch6 Gain4-1
	strGainMultp[5][3][1] = 4.0;	//Low ch6 Gain4-2
	strGainMultp[5][4][0] = -1.35;	//Low ch6 Gain5-1
	strGainMultp[5][4][1] = -1.35;	//Low ch6 Gain5-2

	strGainMultp[6][0][0] = -0.2;	//Low ch7 Gain1-1
	strGainMultp[6][0][1] = -0.4;	//Low ch7 Gain1-2
	strGainMultp[6][1][0] = -1.0;	//Low ch7 Gain2-1
	strGainMultp[6][1][1] = -4.0;	//Low ch7 Gain2-2
	strGainMultp[6][2][0] = -0.5;	//Low ch7 Gain3-1
	strGainMultp[6][2][1] = -1.0;	//Low ch7 Gain3-2
	strGainMultp[6][3][0] = 2.0;	//Low ch7 Gain4-1
	strGainMultp[6][3][1] = 4.0;	//Low ch7 Gain4-2
	strGainMultp[6][4][0] = -1.35;	//Low ch7 Gain5-1
	strGainMultp[6][4][1] = -1.35;	//Low ch7 Gain5-2

	strGainMultp[7][0][0] = -0.2;	//Low ch8 Gain1-1
	strGainMultp[7][0][1] = -0.4;	//Low ch8 Gain1-2
	strGainMultp[7][1][0] = -1.0;	//Low ch8 Gain2-1
	strGainMultp[7][1][1] = -4.0;	//Low ch8 Gain2-2
	strGainMultp[7][2][0] = -0.5;	//Low ch8 Gain3-1
	strGainMultp[7][2][1] = -1.0;	//Low ch8 Gain3-2
	strGainMultp[7][3][0] = 2.0;	//Low ch8 Gain4-1
	strGainMultp[7][3][1] = 4.0;	//Low ch8 Gain4-2
	strGainMultp[7][4][0] = -1.35;	//Low ch8 Gain5-1
	strGainMultp[7][4][1] = -1.35;	//Low ch8 Gain5-2

	strGainMultp[8][0][0] = -0.5;	//High ch9 Gain1-1
	strGainMultp[8][0][1] = -2.0;	//High ch9 Gain1-2
	strGainMultp[8][1][0] = -1.0;	//High ch9 Gain2-1
	strGainMultp[8][1][1] = -2.0;	//High ch9 Gain2-2
	strGainMultp[8][2][0] = 1.0;	//High ch9 Gain3-1
	strGainMultp[8][2][1] = 1.0;	//High ch9 Gain3-2
	strGainMultp[8][3][0] = 1.0;	//High ch9 Gain4-1
	strGainMultp[8][3][1] = 1.0;	//High ch9 Gain4-2
	strGainMultp[8][4][0] = 1.0;	//High ch9 Gain5-1
	strGainMultp[8][4][1] = 1.0;	//High ch9 Gain5-2

	strGainMultp[9][0][0] = -0.5;	//High ch10 Gain1-1
	strGainMultp[9][0][1] = -2.0;	//High ch10 Gain1-2
	strGainMultp[9][1][0] = -1.0;	//High ch10 Gain2-1
	strGainMultp[9][1][1] = -2.0;	//High ch10 Gain2-2
	strGainMultp[9][2][0] = 1.0;	//High ch10 Gain3-1
	strGainMultp[9][2][1] = 1.0;	//High ch10 Gain3-2
	strGainMultp[9][3][0] = 1.0;	//High ch10 Gain4-1
	strGainMultp[9][3][1] = 1.0;	//High ch10 Gain4-2
	strGainMultp[9][4][0] = 1.0;	//High ch10 Gain5-1
	strGainMultp[9][4][1] = 1.0;	//High ch10 Gain5-2

	strGainMultp[10][0][0] = -0.5;	//High ch11 Gain1-1
	strGainMultp[10][0][1] = -2.0;	//High ch11 Gain1-2
	strGainMultp[10][1][0] = -1.0;	//High ch11 Gain2-1
	strGainMultp[10][1][1] = -2.0;	//High ch11 Gain2-2
	strGainMultp[10][2][0] = 1.0;	//High ch11 Gain3-1
	strGainMultp[10][2][1] = 1.0;	//High ch11 Gain3-2
	strGainMultp[10][3][0] = 1.0;	//High ch11 Gain4-1
	strGainMultp[10][3][1] = 1.0;	//High ch11 Gain4-2
	strGainMultp[10][4][0] = 1.0;	//High ch11 Gain5-1
	strGainMultp[10][4][1] = 1.0;	//High ch11 Gain5-2

	strGainMultp[11][0][0] = -0.5;	//High ch12 Gain1-1
	strGainMultp[11][0][1] = -2.0;	//High ch12 Gain1-2
	strGainMultp[11][1][0] = -1.0;	//High ch12 Gain2-1
	strGainMultp[11][1][1] = -2.0;	//High ch12 Gain2-2
	strGainMultp[11][2][0] = 1.0;	//High ch12 Gain3-1
	strGainMultp[11][2][1] = 1.0;	//High ch12 Gain3-2
	strGainMultp[11][3][0] = 1.0;	//High ch12 Gain4-1
	strGainMultp[11][3][1] = 1.0;	//High ch12 Gain4-2
	strGainMultp[11][4][0] = 1.0;	//High ch12 Gain5-1
	strGainMultp[11][4][1] = 1.0;	//High ch12 Gain5-2

	strGainMultp[12][0][0] = -0.5;	//High ch13 Gain1-1
	strGainMultp[12][0][1] = -2.0;	//High ch13 Gain1-2
	strGainMultp[12][1][0] = -1.0;	//High ch13 Gain2-1
	strGainMultp[12][1][1] = -2.0;	//High ch13 Gain2-2
	strGainMultp[12][2][0] = -1.0;	//High ch13 Gain3-1
	strGainMultp[12][2][1] = -1.0;	//High ch13 Gain3-2
	strGainMultp[12][3][0] = 1.0;	//High ch13 Gain4-1
	strGainMultp[12][3][1] = 1.0;	//High ch13 Gain4-2
	strGainMultp[12][4][0] = 1.0;	//High ch13 Gain5-1
	strGainMultp[12][4][1] = 1.0;	//High ch13 Gain5-2
#endif

	comboxGainCh[0][0] = &m_combox_ch1_gain_multp_1;	comboxGainCh[0][1] = &m_combox_ch1_gain_multp_2;	comboxGainCh[0][2] = NULL;							comboxGainCh[0][3] = &m_combox_ch1_gain_multp_4;	comboxGainCh[0][4] = &m_combox_ch1_gain_multp_5;
	comboxGainCh[1][0] = &m_combox_ch2_gain_multp_1;	comboxGainCh[1][1] = &m_combox_ch2_gain_multp_2;	comboxGainCh[1][2] = NULL;							comboxGainCh[1][3] = &m_combox_ch2_gain_multp_4;	comboxGainCh[1][4] = &m_combox_ch2_gain_multp_5;
	comboxGainCh[2][0] = &m_combox_ch3_gain_multp_1;	comboxGainCh[2][1] = &m_combox_ch3_gain_multp_2;	comboxGainCh[2][2] = NULL;							comboxGainCh[2][3] = &m_combox_ch3_gain_multp_4;	comboxGainCh[2][4] = &m_combox_ch3_gain_multp_5;
	comboxGainCh[3][0] = &m_combox_ch4_gain_multp_1;	comboxGainCh[3][1] = &m_combox_ch4_gain_multp_2;	comboxGainCh[3][2] = NULL;							comboxGainCh[3][3] = &m_combox_ch4_gain_multp_4;	comboxGainCh[3][4] = &m_combox_ch4_gain_multp_5;
	comboxGainCh[4][0] = &m_combox_ch5_gain_multp_1;	comboxGainCh[4][1] = &m_combox_ch5_gain_multp_2;	comboxGainCh[4][2] = NULL;							comboxGainCh[4][3] = &m_combox_ch5_gain_multp_4;	comboxGainCh[4][4] = &m_combox_ch5_gain_multp_5;
	comboxGainCh[5][0] = &m_combox_ch6_gain_multp_1;	comboxGainCh[5][1] = &m_combox_ch6_gain_multp_2;	comboxGainCh[5][2] = NULL;							comboxGainCh[5][3] = &m_combox_ch6_gain_multp_4;	comboxGainCh[5][4] = &m_combox_ch6_gain_multp_5;
	comboxGainCh[6][0] = &m_combox_ch7_gain_multp_1;	comboxGainCh[6][1] = &m_combox_ch7_gain_multp_2;	comboxGainCh[6][2] = NULL;							comboxGainCh[6][3] = &m_combox_ch7_gain_multp_4;	comboxGainCh[6][4] = &m_combox_ch7_gain_multp_5;
	comboxGainCh[7][0] = &m_combox_ch8_gain_multp_1;	comboxGainCh[7][1] = &m_combox_ch8_gain_multp_2;	comboxGainCh[7][2] = NULL;							comboxGainCh[7][3] = &m_combox_ch8_gain_multp_4;	comboxGainCh[7][4] = &m_combox_ch8_gain_multp_5;
	comboxGainCh[8][0] = &m_combox_ch9_gain_multp_1;	comboxGainCh[8][1] = &m_combox_ch9_gain_multp_2;	comboxGainCh[8][2] = &m_combox_ch9_gain_multp_3;	comboxGainCh[8][3] = &m_combox_ch9_gain_multp_4;	comboxGainCh[8][4] = &m_combox_ch9_gain_multp_5;
	comboxGainCh[9][0] = &m_combox_ch10_gain_multp_1;	comboxGainCh[9][1] = &m_combox_ch10_gain_multp_2;	comboxGainCh[9][2] = &m_combox_ch10_gain_multp_3;	comboxGainCh[9][3] = &m_combox_ch10_gain_multp_4;	comboxGainCh[9][4] = &m_combox_ch10_gain_multp_5;
	comboxGainCh[10][0] = &m_combox_ch11_gain_multp_1;	comboxGainCh[10][1] = &m_combox_ch11_gain_multp_2;	comboxGainCh[10][2] = &m_combox_ch11_gain_multp_3;	comboxGainCh[10][3] = &m_combox_ch11_gain_multp_4;	comboxGainCh[10][4] = &m_combox_ch11_gain_multp_5;
	comboxGainCh[11][0] = &m_combox_ch12_gain_multp_1;	comboxGainCh[11][1] = &m_combox_ch12_gain_multp_2;	comboxGainCh[11][2] = &m_combox_ch12_gain_multp_3;	comboxGainCh[11][3] = &m_combox_ch12_gain_multp_4;	comboxGainCh[11][4] = &m_combox_ch12_gain_multp_5;
	comboxGainCh[12][0] = &m_combox_ch13_gain_multp_1;	comboxGainCh[12][1] = &m_combox_ch13_gain_multp_2;	comboxGainCh[12][2] = &m_combox_ch13_gain_multp_3;	comboxGainCh[12][3] = &m_combox_ch13_gain_multp_4;	comboxGainCh[12][4] = &m_combox_ch13_gain_multp_5;

	editMultp3GainCh[0] = &m_edit_ch1_gain_multp_3;
	editMultp3GainCh[1] = &m_edit_ch2_gain_multp_3;
	editMultp3GainCh[2] = &m_edit_ch3_gain_multp_3;
	editMultp3GainCh[3] = &m_edit_ch4_gain_multp_3;
	editMultp3GainCh[4] = &m_edit_ch5_gain_multp_3;
	editMultp3GainCh[5] = &m_edit_ch6_gain_multp_3;
	editMultp3GainCh[6] = &m_edit_ch7_gain_multp_3;
	editMultp3GainCh[7] = &m_edit_ch8_gain_multp_3;

	editOffsetCh[0] = &m_edit_ch1_offset; editOffsetCh[1] = &m_edit_ch2_offset; editOffsetCh[2] = &m_edit_ch3_offset; editOffsetCh[3] = &m_edit_ch4_offset;
	editOffsetCh[4] = &m_edit_ch5_offset; editOffsetCh[5] = &m_edit_ch6_offset; editOffsetCh[6] = &m_edit_ch7_offset; editOffsetCh[7] = &m_edit_ch8_offset;
	editOffsetCh[8] = &m_edit_ch9_offset; editOffsetCh[9] = &m_edit_ch10_offset; editOffsetCh[10] = &m_edit_ch11_offset; editOffsetCh[11] = &m_edit_ch12_offset;
	editOffsetCh[12] = &m_edit_ch13_offset;

	editVol1Ch[0] = &m_edit_ch9_vol1; editVol1Ch[1] = &m_edit_ch10_vol1; editVol1Ch[2] = &m_edit_ch11_vol1; editVol1Ch[3] = &m_edit_ch12_vol1;
	editVol1Ch[4] = &m_edit_ch13_vol1;

	editVol2Ch[0] = &m_edit_ch3_vol2; editVol2Ch[1] = &m_edit_ch4_vol2; editVol2Ch[2] = &m_edit_ch5_vol2; editVol2Ch[3] = &m_edit_ch6_vol2;
	editVol2Ch[4] = &m_edit_ch7_vol2; editVol2Ch[5] = &m_edit_ch8_vol2;

	buttonSelectCh[0] = &m_button_ch1_select; buttonSelectCh[1] = &m_button_ch2_select; buttonSelectCh[2] = &m_button_ch3_select; buttonSelectCh[3] = &m_button_ch4_select;
	buttonSelectCh[4] = &m_button_ch5_select; buttonSelectCh[5] = &m_button_ch6_select; buttonSelectCh[6] = &m_button_ch7_select; buttonSelectCh[7] = &m_button_ch8_select;
	buttonSelectCh[8] = &m_button_ch9_select; buttonSelectCh[9] = &m_button_ch10_select; buttonSelectCh[10] = &m_button_ch11_select; buttonSelectCh[11] = &m_button_ch12_select;
	buttonSelectCh[12] = &m_button_ch13_select;

	GainTextID[0] = IDC_STATIC_GAIN_CH1; GainTextID[1] = IDC_STATIC_GAIN_CH2; GainTextID[2] = IDC_STATIC_GAIN_CH3; GainTextID[3] = IDC_STATIC_GAIN_CH4;
	GainTextID[4] = IDC_STATIC_GAIN_CH5; GainTextID[5] = IDC_STATIC_GAIN_CH6; GainTextID[6] = IDC_STATIC_GAIN_CH7; GainTextID[7] = IDC_STATIC_GAIN_CH8;
	GainTextID[8] = IDC_STATIC_GAIN_CH9; GainTextID[9] = IDC_STATIC_GAIN_CH10; GainTextID[10] = IDC_STATIC_GAIN_CH11; GainTextID[11] = IDC_STATIC_GAIN_CH12;
	GainTextID[12] = IDC_STATIC_GAIN_CH13;
}


Dialog1_Main::~Dialog1_Main()
{
	free(pEp2DataBuf);
	pEp2DataBuf = NULL;
	free(pEp4DataBuf);
	pEp4DataBuf = NULL;
	//free(pEp6DataBuf);
	//pEp6DataBuf = NULL;
}


void Dialog1_Main::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_COMBO_TRIG_CH, m_combox_trigch);
	DDX_Control(pDX, IDC_COMBO_FIR_FILTER_FC, m_combox_fir_filter_fc);
	DDX_Control(pDX, IDC_BUTTON_GETSTART, m_CtrlBtnDataGetStart);
	DDX_Control(pDX, IDC_COMBO_CH1_GAIN_MULTIP_1, m_combox_ch1_gain_multp_1);
	DDX_Control(pDX, IDC_COMBO_CH1_GAIN_MULTIP_2, m_combox_ch1_gain_multp_2);
	//DDX_Control(pDX, IDC_COMBO_CH1_GAIN_MULTIP_3, m_combox_ch1_gain_multp_3);
	DDX_Control(pDX, IDC_COMBO_CH1_GAIN_MULTIP_4, m_combox_ch1_gain_multp_4);
	DDX_Control(pDX, IDC_COMBO_CH1_GAIN_MULTIP_5, m_combox_ch1_gain_multp_5);
	DDX_Control(pDX, IDC_COMBO_CH2_GAIN_MULTIP_1, m_combox_ch2_gain_multp_1);
	DDX_Control(pDX, IDC_COMBO_CH2_GAIN_MULTIP_2, m_combox_ch2_gain_multp_2);
	//DDX_Control(pDX, IDC_COMBO_CH2_GAIN_MULTIP_3, m_combox_ch2_gain_multp_3);
	DDX_Control(pDX, IDC_COMBO_CH2_GAIN_MULTIP_4, m_combox_ch2_gain_multp_4);
	DDX_Control(pDX, IDC_COMBO_CH2_GAIN_MULTIP_5, m_combox_ch2_gain_multp_5);
	DDX_Control(pDX, IDC_COMBO_CH3_GAIN_MULTIP_1, m_combox_ch3_gain_multp_1);
	DDX_Control(pDX, IDC_COMBO_CH3_GAIN_MULTIP_2, m_combox_ch3_gain_multp_2);
	//DDX_Control(pDX, IDC_COMBO_CH3_GAIN_MULTIP_3, m_combox_ch3_gain_multp_3);
	DDX_Control(pDX, IDC_COMBO_CH3_GAIN_MULTIP_4, m_combox_ch3_gain_multp_4);
	DDX_Control(pDX, IDC_COMBO_CH3_GAIN_MULTIP_5, m_combox_ch3_gain_multp_5);
	DDX_Control(pDX, IDC_COMBO_CH4_GAIN_MULTIP_1, m_combox_ch4_gain_multp_1);
	DDX_Control(pDX, IDC_COMBO_CH4_GAIN_MULTIP_2, m_combox_ch4_gain_multp_2);
	//DDX_Control(pDX, IDC_COMBO_CH4_GAIN_MULTIP_3, m_combox_ch4_gain_multp_3);
	DDX_Control(pDX, IDC_COMBO_CH4_GAIN_MULTIP_4, m_combox_ch4_gain_multp_4);
	DDX_Control(pDX, IDC_COMBO_CH4_GAIN_MULTIP_5, m_combox_ch4_gain_multp_5);
	DDX_Control(pDX, IDC_COMBO_CH5_GAIN_MULTIP_1, m_combox_ch5_gain_multp_1);
	DDX_Control(pDX, IDC_COMBO_CH5_GAIN_MULTIP_2, m_combox_ch5_gain_multp_2);
	//DDX_Control(pDX, IDC_COMBO_CH5_GAIN_MULTIP_3, m_combox_ch5_gain_multp_3);
	DDX_Control(pDX, IDC_COMBO_CH5_GAIN_MULTIP_4, m_combox_ch5_gain_multp_4);
	DDX_Control(pDX, IDC_COMBO_CH5_GAIN_MULTIP_5, m_combox_ch5_gain_multp_5);
	DDX_Control(pDX, IDC_COMBO_CH6_GAIN_MULTIP_1, m_combox_ch6_gain_multp_1);
	DDX_Control(pDX, IDC_COMBO_CH6_GAIN_MULTIP_2, m_combox_ch6_gain_multp_2);
	//DDX_Control(pDX, IDC_COMBO_CH6_GAIN_MULTIP_3, m_combox_ch6_gain_multp_3);
	DDX_Control(pDX, IDC_COMBO_CH6_GAIN_MULTIP_4, m_combox_ch6_gain_multp_4);
	DDX_Control(pDX, IDC_COMBO_CH6_GAIN_MULTIP_5, m_combox_ch6_gain_multp_5);
	DDX_Control(pDX, IDC_COMBO_CH7_GAIN_MULTIP_1, m_combox_ch7_gain_multp_1);
	DDX_Control(pDX, IDC_COMBO_CH7_GAIN_MULTIP_2, m_combox_ch7_gain_multp_2);
	//DDX_Control(pDX, IDC_COMBO_CH7_GAIN_MULTIP_3, m_combox_ch7_gain_multp_3);
	DDX_Control(pDX, IDC_COMBO_CH7_GAIN_MULTIP_4, m_combox_ch7_gain_multp_4);
	DDX_Control(pDX, IDC_COMBO_CH7_GAIN_MULTIP_5, m_combox_ch7_gain_multp_5);
	DDX_Control(pDX, IDC_COMBO_CH8_GAIN_MULTIP_1, m_combox_ch8_gain_multp_1);
	DDX_Control(pDX, IDC_COMBO_CH8_GAIN_MULTIP_2, m_combox_ch8_gain_multp_2);
	//DDX_Control(pDX, IDC_COMBO_CH8_GAIN_MULTIP_3, m_combox_ch8_gain_multp_3);
	DDX_Control(pDX, IDC_COMBO_CH8_GAIN_MULTIP_4, m_combox_ch8_gain_multp_4);
	DDX_Control(pDX, IDC_COMBO_CH8_GAIN_MULTIP_5, m_combox_ch8_gain_multp_5);
	DDX_Control(pDX, IDC_COMBO_CH9_GAIN_MULTIP_1, m_combox_ch9_gain_multp_1);
	DDX_Control(pDX, IDC_COMBO_CH9_GAIN_MULTIP_2, m_combox_ch9_gain_multp_2);
	DDX_Control(pDX, IDC_COMBO_CH9_GAIN_MULTIP_3, m_combox_ch9_gain_multp_3);
	DDX_Control(pDX, IDC_COMBO_CH9_GAIN_MULTIP_4, m_combox_ch9_gain_multp_4);
	DDX_Control(pDX, IDC_COMBO_CH9_GAIN_MULTIP_5, m_combox_ch9_gain_multp_5);
	DDX_Control(pDX, IDC_COMBO_CH10_GAIN_MULTIP_1, m_combox_ch10_gain_multp_1);
	DDX_Control(pDX, IDC_COMBO_CH10_GAIN_MULTIP_2, m_combox_ch10_gain_multp_2);
	DDX_Control(pDX, IDC_COMBO_CH10_GAIN_MULTIP_3, m_combox_ch10_gain_multp_3);
	DDX_Control(pDX, IDC_COMBO_CH10_GAIN_MULTIP_4, m_combox_ch10_gain_multp_4);
	DDX_Control(pDX, IDC_COMBO_CH10_GAIN_MULTIP_5, m_combox_ch10_gain_multp_5);
	DDX_Control(pDX, IDC_COMBO_CH11_GAIN_MULTIP_1, m_combox_ch11_gain_multp_1);
	DDX_Control(pDX, IDC_COMBO_CH11_GAIN_MULTIP_2, m_combox_ch11_gain_multp_2);
	DDX_Control(pDX, IDC_COMBO_CH11_GAIN_MULTIP_3, m_combox_ch11_gain_multp_3);
	DDX_Control(pDX, IDC_COMBO_CH11_GAIN_MULTIP_4, m_combox_ch11_gain_multp_4);
	DDX_Control(pDX, IDC_COMBO_CH11_GAIN_MULTIP_5, m_combox_ch11_gain_multp_5);
	DDX_Control(pDX, IDC_COMBO_CH12_GAIN_MULTIP_1, m_combox_ch12_gain_multp_1);
	DDX_Control(pDX, IDC_COMBO_CH12_GAIN_MULTIP_2, m_combox_ch12_gain_multp_2);
	DDX_Control(pDX, IDC_COMBO_CH12_GAIN_MULTIP_3, m_combox_ch12_gain_multp_3);
	DDX_Control(pDX, IDC_COMBO_CH12_GAIN_MULTIP_4, m_combox_ch12_gain_multp_4);
	DDX_Control(pDX, IDC_COMBO_CH12_GAIN_MULTIP_5, m_combox_ch12_gain_multp_5);
	DDX_Control(pDX, IDC_EDIT_OFFSET_1, m_edit_ch1_offset);
	DDX_Control(pDX, IDC_EDIT_OFFSET_2, m_edit_ch2_offset);
	DDX_Control(pDX, IDC_EDIT_OFFSET_3, m_edit_ch3_offset);
	DDX_Control(pDX, IDC_EDIT_OFFSET_4, m_edit_ch4_offset);
	DDX_Control(pDX, IDC_EDIT_OFFSET_5, m_edit_ch5_offset);
	DDX_Control(pDX, IDC_EDIT_OFFSET_6, m_edit_ch6_offset);
	DDX_Control(pDX, IDC_EDIT_OFFSET_7, m_edit_ch7_offset);
	DDX_Control(pDX, IDC_EDIT_OFFSET_8, m_edit_ch8_offset);
	DDX_Control(pDX, IDC_EDIT_OFFSET_9, m_edit_ch9_offset);
	DDX_Control(pDX, IDC_EDIT_OFFSET_10, m_edit_ch10_offset);
	DDX_Control(pDX, IDC_EDIT_OFFSET_11, m_edit_ch11_offset);
	DDX_Control(pDX, IDC_EDIT_OFFSET_12, m_edit_ch12_offset);
	DDX_Control(pDX, IDC_EDIT_VOL1_9, m_edit_ch9_vol1);
	DDX_Control(pDX, IDC_EDIT_VOL1_10, m_edit_ch10_vol1);
	DDX_Control(pDX, IDC_EDIT_VOL1_11, m_edit_ch11_vol1);
	DDX_Control(pDX, IDC_EDIT_VOL1_12, m_edit_ch12_vol1);
	DDX_Control(pDX, IDC_EDIT_VOL2_3, m_edit_ch3_vol2);
	DDX_Control(pDX, IDC_EDIT_VOL2_4, m_edit_ch4_vol2);
	DDX_Control(pDX, IDC_EDIT_VOL2_5, m_edit_ch5_vol2);
	DDX_Control(pDX, IDC_EDIT_VOL2_6, m_edit_ch6_vol2);
	DDX_Control(pDX, IDC_EDIT_VOL2_7, m_edit_ch7_vol2);
	DDX_Control(pDX, IDC_EDIT_VOL2_8, m_edit_ch8_vol2);
	DDX_Control(pDX, IDC_CHECK_CH1, m_button_ch1_select);
	DDX_Control(pDX, IDC_CHECK_CH2, m_button_ch2_select);
	DDX_Control(pDX, IDC_CHECK_CH3, m_button_ch3_select);
	DDX_Control(pDX, IDC_CHECK_CH4, m_button_ch4_select);
	DDX_Control(pDX, IDC_CHECK_CH5, m_button_ch5_select);
	DDX_Control(pDX, IDC_CHECK_CH6, m_button_ch6_select);
	DDX_Control(pDX, IDC_CHECK_CH7, m_button_ch7_select);
	DDX_Control(pDX, IDC_CHECK_CH8, m_button_ch8_select);
	DDX_Control(pDX, IDC_CHECK_CH9, m_button_ch9_select);
	DDX_Control(pDX, IDC_CHECK_CH10, m_button_ch10_select);
	DDX_Control(pDX, IDC_CHECK_CH11, m_button_ch11_select);
	DDX_Control(pDX, IDC_CHECK_CH12, m_button_ch12_select);
	DDX_Control(pDX, IDC_CHECK_CHALL, m_button_all_select);
	DDX_Control(pDX, IDC_EDIT_COLLECTED_CNT, m_CtrlEditCollectedCnt);
	DDX_Control(pDX, IDC_EDIT_TRIGGER_VALUE, m_edit_trigger_value);
	DDX_Control(pDX, IDC_EDIT_TRIGGER_RANGE_LOW, m_edit_trigger_low);
	DDX_Control(pDX, IDC_EDIT_TRIGGER_RANGE_HIGH, m_edit_trigger_high);
	DDX_Control(pDX, IDC_RADIO_MDOE_MANUAL, m_button_mode_manual);
	DDX_Control(pDX, IDC_RADIO_MDOE_AUTO, m_button_mode_auto);
	DDX_Control(pDX, IDC_EDIT_EXPORT, m_edit_export);
	DDX_Control(pDX, IDC_EDIT_IMPORT, m_edit_import);
	DDX_Control(pDX, IDC_EDIT_SAVEPATH, m_edit_savepath);
	DDX_Text(pDX, IDC_EDIT_SAVEPATH, m_OutFile);
	DDX_Text(pDX, IDC_EDIT_IMPORT, m_ImportFile);
	DDX_Text(pDX, IDC_EDIT_EXPORT, m_ExportFile);
	DDX_Control(pDX, IDC_EDIT_WAVENUM, m_edit_wave_num);
	DDX_Control(pDX, IDC_BUTTON_PARSET, m_CtrlBtnParSet);
	DDX_Control(pDX, IDC_STATIC_ACTUAL_RANGE, m_static_ActualRange);
	DDX_Control(pDX, IDC_STATIC_SAMPLE_INFO, m_static_SampleInfo);
	DDX_Control(pDX, IDC_STATIC_SAMPLE_INFO2, m_static_SampleInfo2);
	DDX_Control(pDX, IDC_STATIC_SAMPLE_INFO3, m_static_SampleInfo3);
	DDX_Control(pDX, IDC_BUTTON_SAVEPATH_SELECT, m_button_select_savepath);
	DDX_Control(pDX, IDC_EDIT_CH1_GAIN_MULTIP_3, m_edit_ch1_gain_multp_3);
	DDX_Control(pDX, IDC_EDIT_CH2_GAIN_MULTIP_3, m_edit_ch2_gain_multp_3);
	DDX_Control(pDX, IDC_EDIT_CH3_GAIN_MULTIP_3, m_edit_ch3_gain_multp_3);
	DDX_Control(pDX, IDC_EDIT_CH4_GAIN_MULTIP_3, m_edit_ch4_gain_multp_3);
	DDX_Control(pDX, IDC_EDIT_CH5_GAIN_MULTIP_3, m_edit_ch5_gain_multp_3);
	DDX_Control(pDX, IDC_EDIT_CH6_GAIN_MULTIP_3, m_edit_ch6_gain_multp_3);
	DDX_Control(pDX, IDC_EDIT_CH7_GAIN_MULTIP_3, m_edit_ch7_gain_multp_3);
	DDX_Control(pDX, IDC_EDIT_CH8_GAIN_MULTIP_3, m_edit_ch8_gain_multp_3);
	DDX_Control(pDX, IDC_COMBO_CH13_GAIN_MULTIP_1, m_combox_ch13_gain_multp_1);
	DDX_Control(pDX, IDC_COMBO_CH13_GAIN_MULTIP_2, m_combox_ch13_gain_multp_2);
	DDX_Control(pDX, IDC_COMBO_CH13_GAIN_MULTIP_3, m_combox_ch13_gain_multp_3);
	DDX_Control(pDX, IDC_COMBO_CH13_GAIN_MULTIP_4, m_combox_ch13_gain_multp_4);
	DDX_Control(pDX, IDC_COMBO_CH13_GAIN_MULTIP_5, m_combox_ch13_gain_multp_5);
	DDX_Control(pDX, IDC_EDIT_OFFSET_13, m_edit_ch13_offset);
	DDX_Control(pDX, IDC_EDIT_VOL1_13, m_edit_ch13_vol1);
	DDX_Control(pDX, IDC_CHECK_CH13, m_button_ch13_select);
}


BEGIN_MESSAGE_MAP(Dialog1_Main, CDialogEx)
	//ON_WM_CLOSE()
	ON_BN_CLICKED(IDC_RADIO_MDOE_MANUAL, &Dialog1_Main::OnBnClickedRadioMdoeManual)
	ON_BN_CLICKED(IDC_RADIO_MDOE_AUTO, &Dialog1_Main::OnBnClickedRadioMdoeAuto)
	ON_CBN_SELCHANGE(IDC_COMBO_CH1_GAIN_MULTIP_1, &Dialog1_Main::OnCbnSelchangeComboCh1)
	ON_CBN_SELCHANGE(IDC_COMBO_CH1_GAIN_MULTIP_2, &Dialog1_Main::OnCbnSelchangeComboCh1)
//	ON_CBN_SELCHANGE(IDC_COMBO_CH1_GAIN_MULTIP_3, &Dialog1_Main::OnCbnSelchangeComboCh1)
	ON_CBN_SELCHANGE(IDC_COMBO_CH1_GAIN_MULTIP_4, &Dialog1_Main::OnCbnSelchangeComboCh1)
	ON_CBN_SELCHANGE(IDC_COMBO_CH1_GAIN_MULTIP_5, &Dialog1_Main::OnCbnSelchangeComboCh1)
	ON_CBN_SELCHANGE(IDC_COMBO_CH2_GAIN_MULTIP_1, &Dialog1_Main::OnCbnSelchangeComboCh2)
	ON_CBN_SELCHANGE(IDC_COMBO_CH2_GAIN_MULTIP_2, &Dialog1_Main::OnCbnSelchangeComboCh2)
//	ON_CBN_SELCHANGE(IDC_COMBO_CH2_GAIN_MULTIP_3, &Dialog1_Main::OnCbnSelchangeComboCh2)
	ON_CBN_SELCHANGE(IDC_COMBO_CH2_GAIN_MULTIP_4, &Dialog1_Main::OnCbnSelchangeComboCh2)
	ON_CBN_SELCHANGE(IDC_COMBO_CH2_GAIN_MULTIP_5, &Dialog1_Main::OnCbnSelchangeComboCh2)
	ON_CBN_SELCHANGE(IDC_COMBO_CH3_GAIN_MULTIP_1, &Dialog1_Main::OnCbnSelchangeComboCh3)
	ON_CBN_SELCHANGE(IDC_COMBO_CH3_GAIN_MULTIP_2, &Dialog1_Main::OnCbnSelchangeComboCh3)
//	ON_CBN_SELCHANGE(IDC_COMBO_CH3_GAIN_MULTIP_3, &Dialog1_Main::OnCbnSelchangeComboCh3)
	ON_CBN_SELCHANGE(IDC_COMBO_CH3_GAIN_MULTIP_4, &Dialog1_Main::OnCbnSelchangeComboCh3)
	ON_CBN_SELCHANGE(IDC_COMBO_CH3_GAIN_MULTIP_5, &Dialog1_Main::OnCbnSelchangeComboCh3)
	ON_CBN_SELCHANGE(IDC_COMBO_CH4_GAIN_MULTIP_1, &Dialog1_Main::OnCbnSelchangeComboCh4)
	ON_CBN_SELCHANGE(IDC_COMBO_CH4_GAIN_MULTIP_2, &Dialog1_Main::OnCbnSelchangeComboCh4)
//	ON_CBN_SELCHANGE(IDC_COMBO_CH4_GAIN_MULTIP_3, &Dialog1_Main::OnCbnSelchangeComboCh4)
	ON_CBN_SELCHANGE(IDC_COMBO_CH4_GAIN_MULTIP_4, &Dialog1_Main::OnCbnSelchangeComboCh4)
	ON_CBN_SELCHANGE(IDC_COMBO_CH4_GAIN_MULTIP_5, &Dialog1_Main::OnCbnSelchangeComboCh4)
	ON_CBN_SELCHANGE(IDC_COMBO_CH5_GAIN_MULTIP_1, &Dialog1_Main::OnCbnSelchangeComboCh5)
	ON_CBN_SELCHANGE(IDC_COMBO_CH5_GAIN_MULTIP_2, &Dialog1_Main::OnCbnSelchangeComboCh5)
//	ON_CBN_SELCHANGE(IDC_COMBO_CH5_GAIN_MULTIP_3, &Dialog1_Main::OnCbnSelchangeComboCh5)
	ON_CBN_SELCHANGE(IDC_COMBO_CH5_GAIN_MULTIP_4, &Dialog1_Main::OnCbnSelchangeComboCh5)
	ON_CBN_SELCHANGE(IDC_COMBO_CH5_GAIN_MULTIP_5, &Dialog1_Main::OnCbnSelchangeComboCh5)
	ON_CBN_SELCHANGE(IDC_COMBO_CH6_GAIN_MULTIP_1, &Dialog1_Main::OnCbnSelchangeComboCh6)
	ON_CBN_SELCHANGE(IDC_COMBO_CH6_GAIN_MULTIP_2, &Dialog1_Main::OnCbnSelchangeComboCh6)
//	ON_CBN_SELCHANGE(IDC_COMBO_CH6_GAIN_MULTIP_3, &Dialog1_Main::OnCbnSelchangeComboCh6)
	ON_CBN_SELCHANGE(IDC_COMBO_CH6_GAIN_MULTIP_4, &Dialog1_Main::OnCbnSelchangeComboCh6)
	ON_CBN_SELCHANGE(IDC_COMBO_CH6_GAIN_MULTIP_5, &Dialog1_Main::OnCbnSelchangeComboCh6)
	ON_CBN_SELCHANGE(IDC_COMBO_CH7_GAIN_MULTIP_1, &Dialog1_Main::OnCbnSelchangeComboCh7)
	ON_CBN_SELCHANGE(IDC_COMBO_CH7_GAIN_MULTIP_2, &Dialog1_Main::OnCbnSelchangeComboCh7)
//	ON_CBN_SELCHANGE(IDC_COMBO_CH7_GAIN_MULTIP_3, &Dialog1_Main::OnCbnSelchangeComboCh7)
	ON_CBN_SELCHANGE(IDC_COMBO_CH7_GAIN_MULTIP_4, &Dialog1_Main::OnCbnSelchangeComboCh7)
	ON_CBN_SELCHANGE(IDC_COMBO_CH7_GAIN_MULTIP_5, &Dialog1_Main::OnCbnSelchangeComboCh7)
	ON_CBN_SELCHANGE(IDC_COMBO_CH8_GAIN_MULTIP_1, &Dialog1_Main::OnCbnSelchangeComboCh8)
	ON_CBN_SELCHANGE(IDC_COMBO_CH8_GAIN_MULTIP_2, &Dialog1_Main::OnCbnSelchangeComboCh8)
//	ON_CBN_SELCHANGE(IDC_COMBO_CH8_GAIN_MULTIP_3, &Dialog1_Main::OnCbnSelchangeComboCh8)
	ON_CBN_SELCHANGE(IDC_COMBO_CH8_GAIN_MULTIP_4, &Dialog1_Main::OnCbnSelchangeComboCh8)
	ON_CBN_SELCHANGE(IDC_COMBO_CH8_GAIN_MULTIP_5, &Dialog1_Main::OnCbnSelchangeComboCh8)
	ON_CBN_SELCHANGE(IDC_COMBO_CH9_GAIN_MULTIP_1, &Dialog1_Main::OnCbnSelchangeComboCh9)
	ON_CBN_SELCHANGE(IDC_COMBO_CH9_GAIN_MULTIP_2, &Dialog1_Main::OnCbnSelchangeComboCh9)
	ON_CBN_SELCHANGE(IDC_COMBO_CH9_GAIN_MULTIP_3, &Dialog1_Main::OnCbnSelchangeComboCh9)
//	ON_CBN_SELCHANGE(IDC_COMBO_CH9_GAIN_MULTIP_4, &Dialog1_Main::OnCbnSelchangeComboCh9)
	ON_CBN_SELCHANGE(IDC_COMBO_CH9_GAIN_MULTIP_5, &Dialog1_Main::OnCbnSelchangeComboCh9)
	ON_CBN_SELCHANGE(IDC_COMBO_CH10_GAIN_MULTIP_1, &Dialog1_Main::OnCbnSelchangeComboCh10)
	ON_CBN_SELCHANGE(IDC_COMBO_CH10_GAIN_MULTIP_2, &Dialog1_Main::OnCbnSelchangeComboCh10)
	ON_CBN_SELCHANGE(IDC_COMBO_CH10_GAIN_MULTIP_3, &Dialog1_Main::OnCbnSelchangeComboCh10)
//	ON_CBN_SELCHANGE(IDC_COMBO_CH10_GAIN_MULTIP_4, &Dialog1_Main::OnCbnSelchangeComboCh10)
	ON_CBN_SELCHANGE(IDC_COMBO_CH10_GAIN_MULTIP_5, &Dialog1_Main::OnCbnSelchangeComboCh10)
	ON_CBN_SELCHANGE(IDC_COMBO_CH11_GAIN_MULTIP_1, &Dialog1_Main::OnCbnSelchangeComboCh11)
	ON_CBN_SELCHANGE(IDC_COMBO_CH11_GAIN_MULTIP_2, &Dialog1_Main::OnCbnSelchangeComboCh11)
	ON_CBN_SELCHANGE(IDC_COMBO_CH11_GAIN_MULTIP_3, &Dialog1_Main::OnCbnSelchangeComboCh11)
//	ON_CBN_SELCHANGE(IDC_COMBO_CH11_GAIN_MULTIP_4, &Dialog1_Main::OnCbnSelchangeComboCh11)
	ON_CBN_SELCHANGE(IDC_COMBO_CH11_GAIN_MULTIP_5, &Dialog1_Main::OnCbnSelchangeComboCh11)
	ON_CBN_SELCHANGE(IDC_COMBO_CH12_GAIN_MULTIP_1, &Dialog1_Main::OnCbnSelchangeComboCh12)
	ON_CBN_SELCHANGE(IDC_COMBO_CH12_GAIN_MULTIP_2, &Dialog1_Main::OnCbnSelchangeComboCh12)
	ON_CBN_SELCHANGE(IDC_COMBO_CH12_GAIN_MULTIP_3, &Dialog1_Main::OnCbnSelchangeComboCh12)
//	ON_CBN_SELCHANGE(IDC_COMBO_CH12_GAIN_MULTIP_4, &Dialog1_Main::OnCbnSelchangeComboCh12)
	ON_CBN_SELCHANGE(IDC_COMBO_CH12_GAIN_MULTIP_5, &Dialog1_Main::OnCbnSelchangeComboCh12)
	ON_CBN_SELCHANGE(IDC_COMBO_CH13_GAIN_MULTIP_1, &Dialog1_Main::OnCbnSelchangeComboCh13)
	ON_CBN_SELCHANGE(IDC_COMBO_CH13_GAIN_MULTIP_2, &Dialog1_Main::OnCbnSelchangeComboCh13)
	ON_CBN_SELCHANGE(IDC_COMBO_CH13_GAIN_MULTIP_3, &Dialog1_Main::OnCbnSelchangeComboCh13)
	ON_CBN_SELCHANGE(IDC_COMBO_CH13_GAIN_MULTIP_5, &Dialog1_Main::OnCbnSelchangeComboCh13)
	ON_BN_CLICKED(IDC_BUTTON_PARSET, &Dialog1_Main::OnBnClickedButtonParset)
	ON_BN_CLICKED(IDC_BUTTON_GETSTART, &Dialog1_Main::OnBnClickedButtonGetstart)
	ON_BN_CLICKED(IDC_CHECK_CHALL, &Dialog1_Main::OnBnClickedCheckChall)
	ON_BN_CLICKED(IDC_BUTTON_EXPORT, &Dialog1_Main::OnBnClickedButtonExport)
	ON_BN_CLICKED(IDC_BUTTON_IMPORT, &Dialog1_Main::OnBnClickedButtonImport)
	ON_BN_CLICKED(IDC_CHECK_CH1, &Dialog1_Main::OnBnClickedCheckCh1)
	ON_BN_CLICKED(IDC_CHECK_CH2, &Dialog1_Main::OnBnClickedCheckCh2)
	ON_BN_CLICKED(IDC_CHECK_CH3, &Dialog1_Main::OnBnClickedCheckCh3)
	ON_BN_CLICKED(IDC_CHECK_CH4, &Dialog1_Main::OnBnClickedCheckCh4)
	ON_BN_CLICKED(IDC_CHECK_CH5, &Dialog1_Main::OnBnClickedCheckCh5)
	ON_BN_CLICKED(IDC_CHECK_CH6, &Dialog1_Main::OnBnClickedCheckCh6)
	ON_BN_CLICKED(IDC_CHECK_CH7, &Dialog1_Main::OnBnClickedCheckCh7)
	ON_BN_CLICKED(IDC_CHECK_CH8, &Dialog1_Main::OnBnClickedCheckCh8)
	ON_BN_CLICKED(IDC_CHECK_CH9, &Dialog1_Main::OnBnClickedCheckCh9)
	ON_BN_CLICKED(IDC_CHECK_CH10, &Dialog1_Main::OnBnClickedCheckCh10)
	ON_BN_CLICKED(IDC_CHECK_CH11, &Dialog1_Main::OnBnClickedCheckCh11)
	ON_BN_CLICKED(IDC_CHECK_CH12, &Dialog1_Main::OnBnClickedCheckCh12)
//	ON_WM_CLOSE()
ON_BN_CLICKED(IDC_BUTTON_IMPORT_SELECT, &Dialog1_Main::OnBnClickedButtonImportSelect)
ON_BN_CLICKED(IDC_BUTTON_EXPORT_SELECT, &Dialog1_Main::OnBnClickedButtonExportSelect)
ON_BN_CLICKED(IDC_BUTTON_SAVEPATH_SELECT, &Dialog1_Main::OnBnClickedButtonSavepathSelect)
//ON_WM_CLOSE()
ON_WM_CLOSE()
ON_EN_CHANGE(IDC_EDIT_TRIGGER_RANGE_LOW, &Dialog1_Main::OnEnChangeEditTriggerRangeLow)
ON_EN_CHANGE(IDC_EDIT_TRIGGER_RANGE_HIGH, &Dialog1_Main::OnEnChangeEditTriggerRangeHigh)
ON_CBN_SELCHANGE(IDC_COMBO_FIR_FILTER_FC, &Dialog1_Main::OnCbnSelchangeComboFirFilterFc)
ON_EN_CHANGE(IDC_EDIT_CH1_GAIN_MULTIP_3, &Dialog1_Main::OnEnChangeEditCh1GainMultip3)
ON_EN_CHANGE(IDC_EDIT_CH2_GAIN_MULTIP_3, &Dialog1_Main::OnEnChangeEditCh2GainMultip3)
ON_EN_CHANGE(IDC_EDIT_CH3_GAIN_MULTIP_3, &Dialog1_Main::OnEnChangeEditCh3GainMultip3)
ON_EN_CHANGE(IDC_EDIT_CH4_GAIN_MULTIP_3, &Dialog1_Main::OnEnChangeEditCh4GainMultip3)
ON_EN_CHANGE(IDC_EDIT_CH5_GAIN_MULTIP_3, &Dialog1_Main::OnEnChangeEditCh5GainMultip3)
ON_EN_CHANGE(IDC_EDIT_CH6_GAIN_MULTIP_3, &Dialog1_Main::OnEnChangeEditCh6GainMultip3)
ON_EN_CHANGE(IDC_EDIT_CH7_GAIN_MULTIP_3, &Dialog1_Main::OnEnChangeEditCh7GainMultip3)
ON_EN_CHANGE(IDC_EDIT_CH8_GAIN_MULTIP_3, &Dialog1_Main::OnEnChangeEditCh8GainMultip3)
ON_EN_CHANGE(IDC_EDIT_SAVEPATH, &Dialog1_Main::OnEnChangeEditSavepath)
END_MESSAGE_MAP()


// Dialog1_Main message handlers


BOOL Dialog1_Main::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// TODO:  Add extra initialization here
	CString strTriggerChName;
	CString strtemp;
	int iTemp = 0;

	/* Init Gain ch combox */
	for (int i = 0; i < 13; i++)
	{
		for (int j = 0; j < 5; j++)
		{
			if (i < 8 && j == 2)
			{
				continue;//CH1~8 gain3
			}

			strtemp.Format(_T("%.2f"), strGainMultp[i][j][0]);
			comboxGainCh[i][j]->AddString(strtemp);
			strtemp.Format(_T("%.2f"), strGainMultp[i][j][1]);
			comboxGainCh[i][j]->AddString(strtemp);
			comboxGainCh[i][j]->SetCurSel(0);

			if ((i > 7) && (j == 2))
			{
				comboxGainCh[i][j]->EnableWindow(FALSE);
			}
			else if ((i > 7) && (j == 3))
			{
				comboxGainCh[i][j]->EnableWindow(FALSE);
			}
			else if (j == 4)
			{
				comboxGainCh[i][j]->EnableWindow(FALSE);
			}
		}
	}

	/* Init trigger ch combox */
	for (int i = 1; i < 14; i++)
	{
		strTriggerChName.Format(_T("Ch%d"), i);
		m_combox_trigch.AddString(strTriggerChName);
	}

	m_combox_trigch.SetCurSel(0);
	
	/* Init fir filter fc combox */
	strtemp.Format(_T("fc=15Mhz, SamplingRate=40Mbps"));
	m_combox_fir_filter_fc.AddString(strtemp);
	strtemp.Format(_T("fc=25Mhz, SamplingRate=60Mbps"));
	m_combox_fir_filter_fc.AddString(strtemp);
	m_combox_fir_filter_fc.SetCurSel(0);

	/* Initialize radio button */
	CButton* pRadioSingleTest = (CButton*)GetDlgItem(IDC_RADIO_MDOE_MANUAL);
	pRadioSingleTest->SetCheck(TRUE);

	/* Set ReadyOnly collected waveforms count edit ctrl */
	m_CtrlEditCollectedCnt.SetReadOnly(1);
	strtemp.Format(_T("0"));
	m_CtrlEditCollectedCnt.SetWindowTextW(strtemp);

	/* Get Dll version */
	const char* ver = NULL;
	ver = m_pMainDlg->UsbLibInfo.DllVersion_Get();
	strtemp.Format(_T("Dll version is %hs"), ver);
	m_pMainDlg->PrintLog(strtemp);

	/* Import default config */
	const bool defaultConfigImportSucceeded = ImportDefaultConfigFile();

	/* Update Gain result */
	for (int i = 0; i < 13; i++)
	{
		UpdateTotalGain(i);
	}

	if (SavePathValidation::ShouldValidateStartupAfterConfigImport(defaultConfigImportSucceeded))
	{
		ValidateSavePathUI(
			SavePathValidation::ShouldShowDialogForUiTrigger(SavePathValidation::UiValidationTrigger::kStartup));
	}

	/* Create USB buffer */
	pEp2DataBuf = (PBYTE)malloc(EP2_DATA_BUFF_SIZE);
	if (!pEp2DataBuf)
	{
		strtemp.Format(_T("EP2 DATA BUFF alloc failed"));
		m_pMainDlg->PrintLog(strtemp);
	}
	else	
	{
		memset(pEp2DataBuf, 0x00, EP2_DATA_BUFF_SIZE);
	}	
	
	pEp4DataBuf = (PBYTE)malloc(EP4_DATA_NODUMMY_SIZE);
	if (!pEp4DataBuf)
	{
		strtemp.Format(_T("EP4 DATA BUFF alloc failed"));
		m_pMainDlg->PrintLog(strtemp);
	}
	else	
	{
		memset(pEp4DataBuf, 0x00, EP4_DATA_NODUMMY_SIZE);
	}	
	
	//pEp6DataBuf = (PBYTE)malloc(EP6_MAX_ONETIMESIZE);
	//if (!pEp6DataBuf)
	//{
	//	strtemp.Format(_T("EP6 DATA BUFF alloc failed"));
	//	m_pMainDlg->PrintLog(strtemp);
	//}
	//else	
	//{
	//	memset(pEp6DataBuf, 0x00, EP6_MAX_ONETIMESIZE);
	//}

	return TRUE;  // return TRUE unless you set the focus to a control
	// EXCEPTION: OCX Property Pages should return FALSE
}


void Dialog1_Main::OnBnClickedRadioMdoeManual()
{
#if 1
	if (((CButton*)GetDlgItem(IDC_RADIO_MDOE_MANUAL))->GetCheck() == BST_CHECKED)
	{
		m_CtrlBtnDataGetStart.EnableWindow(TRUE);
	}
	else
	{
		m_CtrlBtnDataGetStart.EnableWindow(FALSE);
	}
#endif
}


void Dialog1_Main::OnBnClickedRadioMdoeAuto()
{
#if 1
	if (((CButton*)GetDlgItem(IDC_RADIO_MDOE_AUTO))->GetCheck() == BST_CHECKED)
	{
		m_CtrlBtnDataGetStart.EnableWindow(FALSE);
	}
	else
	{
		m_CtrlBtnDataGetStart.EnableWindow(TRUE);
	}
#endif
}


void Dialog1_Main::UpdateTotalGain(INT CHID)
{
	double dGain1 = 0.0;
	double dGain2 = 0.0;
	double dGain3 = 0.0;
	double dGain4 = 0.0;
	double dGain5 = 0.0;
	CString strtemp;

	comboxGainCh[CHID][0]->GetLBText(comboxGainCh[CHID][0]->GetCurSel(), strtemp);
	dGain1 = _ttof(strtemp);
	comboxGainCh[CHID][1]->GetLBText(comboxGainCh[CHID][1]->GetCurSel(), strtemp);
	dGain2 = _ttof(strtemp);
	if (CHID < 8)
	{
		editMultp3GainCh[CHID]->GetWindowTextW(strtemp);
	}
	else
	{
		comboxGainCh[CHID][2]->GetLBText(comboxGainCh[CHID][2]->GetCurSel(), strtemp);
	}	
	dGain3 = _ttof(strtemp);

	comboxGainCh[CHID][3]->GetLBText(comboxGainCh[CHID][3]->GetCurSel(), strtemp);
	dGain4 = _ttof(strtemp);
	
	comboxGainCh[CHID][4]->GetLBText(comboxGainCh[CHID][4]->GetCurSel(), strtemp);
	dGain5 = _ttof(strtemp);

	strtemp.Format(_T("%.2f倍"), dGain1 * dGain2 * dGain3 * dGain4 * dGain5);
	GetDlgItem(GainTextID[CHID])->SetWindowTextW(strtemp);
}


void Dialog1_Main::OnCbnSelchangeComboCh1()
{
	UpdateTotalGain(0);
}


void Dialog1_Main::OnCbnSelchangeComboCh2()
{
	UpdateTotalGain(1);
}


void Dialog1_Main::OnCbnSelchangeComboCh3()
{
	UpdateTotalGain(2);
}


void Dialog1_Main::OnCbnSelchangeComboCh4()
{
	UpdateTotalGain(3);
}


void Dialog1_Main::OnCbnSelchangeComboCh5()
{
	UpdateTotalGain(4);
}


void Dialog1_Main::OnCbnSelchangeComboCh6()
{
	UpdateTotalGain(5);
}


void Dialog1_Main::OnCbnSelchangeComboCh7()
{
	UpdateTotalGain(6);
}


void Dialog1_Main::OnCbnSelchangeComboCh8()
{
	UpdateTotalGain(7);
}


void Dialog1_Main::OnCbnSelchangeComboCh9()
{
	UpdateTotalGain(8);
}


void Dialog1_Main::OnCbnSelchangeComboCh10()
{
	UpdateTotalGain(9);
}


void Dialog1_Main::OnCbnSelchangeComboCh11()
{
	UpdateTotalGain(10);
}


void Dialog1_Main::OnCbnSelchangeComboCh12()
{
	UpdateTotalGain(11);
}

void Dialog1_Main::OnCbnSelchangeComboCh13()
{
	UpdateTotalGain(12);
}


void Dialog1_Main::OnBnClickedCheckChall()
{
	BOOL isChecked = m_button_all_select.GetCheck();

	for (int i = 0; i < 13; i++)
	{
		buttonSelectCh[i]->SetCheck(isChecked);
	}
}


void Dialog1_Main::UpdateChSelect()
{
	BOOL isChecked = TRUE;

	for (int i = 0; i < 13; i++)
	{
		isChecked = isChecked && buttonSelectCh[i]->GetCheck();
	}

	m_button_all_select.SetCheck(isChecked);
}


void Dialog1_Main::OnBnClickedCheckCh1()
{
	UpdateChSelect();
}


void Dialog1_Main::OnBnClickedCheckCh2()
{
	UpdateChSelect();
}


void Dialog1_Main::OnBnClickedCheckCh3()
{
	UpdateChSelect();
}


void Dialog1_Main::OnBnClickedCheckCh4()
{
	UpdateChSelect();
}


void Dialog1_Main::OnBnClickedCheckCh5()
{
	UpdateChSelect();
}


void Dialog1_Main::OnBnClickedCheckCh6()
{
	UpdateChSelect();
}


void Dialog1_Main::OnBnClickedCheckCh7()
{
	UpdateChSelect();
}


void Dialog1_Main::OnBnClickedCheckCh8()
{
	UpdateChSelect();
}


void Dialog1_Main::OnBnClickedCheckCh9()
{
	UpdateChSelect();
}


void Dialog1_Main::OnBnClickedCheckCh10()
{
	UpdateChSelect();
}


void Dialog1_Main::OnBnClickedCheckCh11()
{
	UpdateChSelect();
}


void Dialog1_Main::OnBnClickedCheckCh12()
{
	UpdateChSelect();
}

bool Dialog1_Main::ValidateSavePathUI(bool showDialog, CString* outNormalizedPath)
{
	CString rawPath;
	GetDlgItemText(IDC_EDIT_SAVEPATH, rawPath);
	const SavePathValidation::Result validation = ValidateSavePathInternal(rawPath);

	if (validation.code == SavePathValidation::kSavePathValidationOk)
	{
		if (outNormalizedPath != nullptr)
		{
			*outNormalizedPath = validation.normalizedPath.c_str();
		}

		if (m_hasSavePathWarning)
		{
			m_pMainDlg->PrintLog(_T("[SavePathWarning] cleared"));
		}
		m_hasSavePathWarning = false;
		return true;
	}

	CString warningMessage(validation.message.c_str());
	if (warningMessage.IsEmpty())
	{
		warningMessage = _T("SavePath validation failed.");
	}

	m_hasSavePathWarning = true;
	m_pMainDlg->PrintLog(_T("[SavePathWarning] ") + warningMessage);
	if (showDialog)
	{
		MessageBox(warningMessage, _T("Save Path Error"), MB_OK | MB_ICONERROR);
	}

	return false;
}


void Dialog1_Main::OnBnClickedButtonParset()
{
	INT	iRet = 0;
	INT	iIndex = 0;

	iRet = UpdateConfigStruct(&packetConfig);
	if (iRet != E_OK)
	{
		m_pMainDlg->PrintLog(_T("Set parameters error."));
		return;
	}

	/* Get data from Ep4 */
	iRet = m_pMainDlg->UsbLibInfo.EP4_GetData(pEp2DataBuf);
	if (iRet != USB_SUCCESS)
	{
		m_pMainDlg->PrintLog(_T("Get ep4 register data failed."));
		m_pMainDlg->PrintLog(m_pMainDlg->strUSBLibError(iRet));
		return;
	}

	RegSet_SetGainValue(packetConfig.GainCh, pEp2DataBuf);
	Reg_Write((UINT)(FPGAREG_GAIN_TRG), 0x1, pEp2DataBuf);//Start gain set trigger

	for (iIndex = 0; iIndex < 13; iIndex++)
	{
		RegSet_SetOffsetValue(iIndex, packetConfig.OffsetValue[iIndex], pEp2DataBuf);
	}
	Reg_Write((UINT)(FPGAREG_OFFSET_TRG), 0x1, pEp2DataBuf);//Start offset set trigger

	for (iIndex = 0; iIndex < 5; iIndex++)
	{
		RegSet_SetExtCtrlVol_1(iIndex, packetConfig.ExtCtrlVol1[iIndex], pEp2DataBuf);
	}
	Reg_Write((UINT)(FPGAREG_DAC_TRG), 0x1, pEp2DataBuf);//Start dac set trigger

	for (iIndex = 0; iIndex < 6; iIndex++)
	{
		RegSet_SetExtCtrlVol_2(iIndex, packetConfig.ExtCtrlVol2[iIndex], pEp2DataBuf);
	}

	RegSet_SelectFirFilterFC(packetConfig.FirFilterFC, pEp2DataBuf);

	RegSet_SelectDataCH(packetConfig.CHSelect, pEp2DataBuf);

	RegSet_SelectTRGCH(packetConfig.TriggerCh, pEp2DataBuf);

	RegSet_SetTRGValue(packetConfig.TriggerValue, pEp2DataBuf);

	RegSet_SetTRGRange(0, packetConfig.TriggerRange[0], pEp2DataBuf);

	RegSet_SetTRGRange(1, packetConfig.TriggerRange[1], pEp2DataBuf);

	RegSet_SelectGetDataMeas(packetConfig.ManualMode, pEp2DataBuf);

	RegSet_UpdateGainValue(pEp2DataBuf);

	RegSet_UpdateOffsetValue(pEp2DataBuf);

	RegSet_UpdateExtCtrlVol(pEp2DataBuf);

	/* Send data by Ep2 */
	iRet = m_pMainDlg->UsbLibInfo.EP2_SendData(pEp2DataBuf);
	if (iRet != USB_SUCCESS)
	{
		m_pMainDlg->PrintLog(_T("Send ep2 register data failed."));
		m_pMainDlg->PrintLog(m_pMainDlg->strUSBLibError(iRet));
		return;
	}

	m_pMainDlg->PrintLog(_T("Set parameters success."));

	if (((CButton*)GetDlgItem(IDC_RADIO_MDOE_MANUAL))->GetCheck() == BST_CHECKED)
	{
		m_CtrlBtnDataGetStart.EnableWindow(TRUE);
		m_bManualMode = TRUE;
	}
	else
	{
		m_CtrlBtnDataGetStart.EnableWindow(FALSE);
		m_bManualMode = FALSE;

#if false
		/* Create looptest thread */
		pLpTestThread_EP2_EP4 = AfxBeginThread((AFX_THREADPROC)LoopTestProcessThread_EP2_EP4,
			(LPVOID)this,
			THREAD_PRIORITY_NORMAL,
			0,
			0,
			NULL);
		if (NULL == pLpTestThread_EP2_EP4)
		{
			MessageBox(_T("Failed to create thread1"));
			return;
		}
#endif
		if (g_bEP6ThreadFlag == 0)
		{
			/* Create get waveform data thread */
			pLpTestThread_EP6_GetData = AfxBeginThread((AFX_THREADPROC)LoopTestProcessThread_EP6_GetData,
				(LPVOID)this,
				THREAD_PRIORITY_NORMAL,
				0,
				0,
				NULL);
			if (NULL == pLpTestThread_EP6_GetData)
			{
				MessageBox(_T("Failed to create thread2"));
				return;
			}

			m_pMainDlg->PrintLog(_T("Automatic get waveform data Start."));
		}
		else
		{
			m_pMainDlg->PrintLog(_T("EP6 thread is running."));
		}
	}
}


void Dialog1_Main::OnBnClickedButtonGetstart()
{
	CString strTemp;

	if (((CButton*)GetDlgItem(IDC_RADIO_MDOE_AUTO))->GetCheck() == BST_CHECKED)
	{
		m_pMainDlg->PrintLog(_T("Test mode error."));
		return;
	}

	m_CtrlBtnDataGetStart.GetWindowTextW(strTemp);

	if (strTemp == _T("Data Get Start"))
	{
#if 0
		/* Create looptest thread */
		pLpTestThread_EP2_EP4 = AfxBeginThread((AFX_THREADPROC)LoopTestProcessThread_EP2_EP4,
			(LPVOID)this,
			THREAD_PRIORITY_NORMAL,
			0,
			0,
			NULL);
		if (NULL == pLpTestThread_EP2_EP4)
		{
			MessageBox(_T("Failed to create thread1"));
			return;
		}
#endif

		/* Create get waveform data thread */
		pLpTestThread_EP6_GetData = AfxBeginThread((AFX_THREADPROC)LoopTestProcessThread_EP6_GetData,
			(LPVOID)this,
			THREAD_PRIORITY_NORMAL,
			0,
			0,
			NULL);
		if (NULL == pLpTestThread_EP6_GetData)
		{
			MessageBox(_T("Failed to create thread2"));
			return;
		}

		m_pMainDlg->PrintLog(_T("Manual get waveform data Start."));
	}
	else
	{
		/* Stop FPGA sampling */
		if (m_pMainDlg->UsbLibInfo.EP4_GetData(pEp2DataBuf) != USB_SUCCESS)
		{
			m_pMainDlg->PrintLog(_T("Get ep4 register data failed."));
			return;
		}

		RegSet_GetWaveDataStart(FALSE, pEp2DataBuf);

		if (m_pMainDlg->UsbLibInfo.EP2_SendData(pEp2DataBuf) != USB_SUCCESS)
		{
			m_pMainDlg->PrintLog(_T("Send ep2 register data failed."));
			return;
		}

		/* Disable start button */
		m_CtrlBtnDataGetStart.EnableWindow(FALSE);
		g_bStartSampling = 0;
	}
}


void LoopTestProcessThread_EP2_EP4(LPVOID lpParam)
{
	class Dialog1_Main* CurObject = (class Dialog1_Main*)lpParam;
	INT iRet = 0;

	CurObject->m_pMainDlg->PrintLog(_T("Start EP4 loop thread."));

	/* Initialize EP4 buffer */
	memset(pEp4DataBuf, 0x00, EP4_DATA_NODUMMY_SIZE);

	g_bEP24LoopFlag = 1;

	while (g_bEP24LoopFlag)
	{
		/* Get data from Ep4 */
		iRet = CurObject->m_pMainDlg->UsbLibInfo.EP4_GetData(pEp4DataBuf);
		if (iRet != USB_SUCCESS)
		{
			CurObject->m_pMainDlg->PrintLog(_T("Get ep4 register data failed."));
			CurObject->m_pMainDlg->PrintLog(CurObject->m_pMainDlg->strUSBLibError(iRet));
			break;
		}

		Sleep(10);

#if 0
		/* Send data by Ep2 */
		iRet = CurObject->m_pMainDlg->UsbLibInfo.EP2_SendData(pEp2DataBuf);
		if (iRet != USB_SUCCESS)
		{
			CurObject->m_pMainDlg->PrintLog(_T("Send ep2 register data failed."));
			CurObject->m_pMainDlg->PrintLog(CurObject->m_pMainDlg->strUSBLibError(iRet));
			break;
		}
#endif
	}

	g_bEP24LoopFlag = 0;
	CurObject->m_pMainDlg->PrintLog(_T("Exit EP4 loop thread."));
}


void LoopTestProcessThread_EP6_GetData(LPVOID lpParam)
{
	class Dialog1_Main* CurObject = (class Dialog1_Main*)lpParam;
	//BOOL LastRead = FALSE;
	BOOL DDRWrCompleted = FALSE;
	BOOL PcReadCompleted = FALSE;
	INT iRet = 0;
	INT iUSBIndex = 1;
	INT iIndex = 0;
	INT CHNum_L = 0;
	INT CHNum_H = 0;
	INT iTimeStampIndex = 0;
	FLOAT TrgRange = 0.0;		
	ULONG OneCHSize_H = 0;
	ULONG OneWaveSize_L = 0;
	ULONG OneWaveSize_H = 0;
	ULONG OneFileSize_L = 0;
	ULONG OneFileSize_H = 0;
	ULONG OneFileSize = 0;
	CString strTmp;
	CString strTimeStamp;
	CString strTimeStamp_use;
	CString normalizedSavePath;
	CFileStatus fileStatus;
	CurObject->m_pMainDlg->PrintLog(_T("Start EP6 get data thread."));

	const SavePathValidation::Result savePathValidation =
		ValidateSavePathInternal(packetConfig.SavePath);
	if (savePathValidation.code != SavePathValidation::kSavePathValidationOk)
	{
		LogFileIoEvent(
			CurObject,
			_T("ValidateSavePath"),
			-1,
			packetConfig.SavePath,
			_T(""),
			ERROR_INVALID_PARAMETER,
			savePathValidation.message.c_str());
		CurObject->m_pMainDlg->PrintLog(_T("SavePath validation failed. Stop sampling thread."));
		goto FINALIZE_THREAD;
	}

	normalizedSavePath = savePathValidation.normalizedPath.c_str();
	packetConfig.SavePath = normalizedSavePath;
	CleanupResidualTmpFiles(CurObject, normalizedSavePath);

	/* Calculate waveform data size */
	TrgRange = packetConfig.TriggerRange[0] + packetConfig.TriggerRange[1];

	if (packetConfig.FirFilterFC == 0)
	{
		OneCHSize_H = 80;//40Mbps * 14bit(16bit)
	}
	else
	{
		OneCHSize_H = 120;//60Mbps * 14bit(16bit)
	}

	for (int i = 0; i < 8; i++)
	{
		if (packetConfig.CHSelect[i] == 1)
		{
			CHNum_L++;//Low channel num
		}
	}

	for (int i = 8; i < 13; i++)
	{
		if (packetConfig.CHSelect[i] == 1)
		{
			CHNum_H++;//High channel num
		}
	}

	OneWaveSize_L = (ULONG)((80 * CHNum_L) * TrgRange);
	OneWaveSize_H = (ULONG)((OneCHSize_H * CHNum_H) * TrgRange);
	OneFileSize_L = OneWaveSize_L * packetConfig.WaveNum;
	OneFileSize_H = OneWaveSize_H * packetConfig.WaveNum;
	OneFileSize = OneFileSize_L + OneFileSize_H;

	strTmp.Format(_T("Trigger range = %.1fus"), TrgRange);
	CurObject->m_pMainDlg->PrintLog(strTmp);
	strTmp.Format(_T("High data size = %dbyte, Low data size = %dbyte"), OneWaveSize_H, OneWaveSize_L);
	CurObject->m_pMainDlg->PrintLog(strTmp);
	strTmp.Format(_T("High file size = %dbyte, Low file size = %dbyte"), OneFileSize_H, OneFileSize_L);
	CurObject->m_pMainDlg->PrintLog(strTmp);
	
	strTmp.Format(_T("One time max size = %dbyte"), kDefaultMaxReadChunkBytes);
	CurObject->m_pMainDlg->PrintLog(strTmp);

	do
	{
		/* Set button status */
		if (CurObject->m_bManualMode == TRUE)
		{
			CurObject->m_CtrlBtnDataGetStart.SetWindowText(_T("Data Get Stop"));
		}

#if false
		CFile WavedataFile[12];
		CString FileName;
		CString Temp;

		for (int i = 0; i < 12; i++)
		{
			if (packetConfig.CHSelect[i] == 1)
			{
				Temp.Format(_T("\\wavedata_ch%d.bin"), i + 1);
				FileName = packetConfig.SavePath + Temp;

				if (!WavedataFile[i].Open(FileName, CFile::modeCreate | CFile::modeWrite))
				{
					CurObject->m_pMainDlg->PrintLog(_T("Open wave data bin file failed."));
					break;
				}
			}
		}
#endif
		bool runtime = CurObject->m_bManualMode ? FALSE : TRUE;
		bool LastModeSave = CurObject->m_bManualMode;
		bool ErrExit = FALSE;
		g_bStartSampling = 1;
		g_bEP6ThreadFlag = 1;
		
		do 
		{
			/* Manual Mode: Start FPGA sampling */
			if (CurObject->m_bManualMode == TRUE)
			{
				if (CurObject->m_pMainDlg->UsbLibInfo.EP4_GetData(pEp2DataBuf) != USB_SUCCESS)
				{
					CurObject->m_pMainDlg->PrintLog(_T("Get ep4 register data failed."));
					break;
				}

				CurObject->RegSet_GetWaveDataStart(TRUE, pEp2DataBuf);

				if (CurObject->m_pMainDlg->UsbLibInfo.EP2_SendData(pEp2DataBuf) != USB_SUCCESS)
				{
					CurObject->m_pMainDlg->PrintLog(_T("Send ep2 register data failed."));
					break;
				}

				Sleep(5);
			}	

			/* Check FPGA sampling start */		
			while(TRUE)
			{
				if (CurObject->m_pMainDlg->UsbLibInfo.EP4_GetData(pEp4DataBuf) != USB_SUCCESS)
				{
					CurObject->m_pMainDlg->PrintLog(_T("Get ep4 register data failed."));
					ErrExit = TRUE;
					break;
				}

				if (CurObject->RegGet_SampleStartSt(pEp4DataBuf) == TRUE)
				{
					CurObject->m_pMainDlg->PrintLog(_T("FPGA start sampling."));
					break;
				}
				
				if (CurObject->m_bManualMode != LastModeSave)
				{
					CurObject->m_pMainDlg->PrintLog(_T("Manual get mode on.")); 
					ErrExit = TRUE;
					break;
				}
				else if (g_bStartSampling == 0)
				{
					CurObject->m_pMainDlg->PrintLog(_T("Sampling is not start."));
					ErrExit = TRUE;
					break;
				}
				else
				{
					LastModeSave = CurObject->m_bManualMode;
				}				
			}

			if (ErrExit == TRUE)
			{
				/* Flush log immediately on error */
				CurObject->m_pMainDlg->FlushLog();
				break;
			}

			/* Set sampling UI status */
			CurObject->SamplingUISet(FALSE, CurObject->m_bManualMode);

			/* Get current time stamp string */
			SYSTEMTIME curTime;
			GetLocalTime(&curTime);
			strTimeStamp.Format(_T("\\%02d%02d%02d_%02d%02d"), curTime.wYear % 2000, curTime.wMonth, curTime.wDay, curTime.wHour, curTime.wMinute);
			strTimeStamp = packetConfig.SavePath + strTimeStamp;
			CurObject->m_pMainDlg->PrintLog(strTimeStamp);

			if (CFile::GetStatus(strTimeStamp + _T("_cfg.txt"), fileStatus))
			{
				iTimeStampIndex = 0;

				while (1)
				{
					strTmp.Format(_T("_%d"), ++iTimeStampIndex);

					if (!(CFile::GetStatus(strTimeStamp + strTmp + _T("_cfg.txt"), fileStatus)))
					{
						strTimeStamp_use = strTimeStamp + strTmp;
						break;
					}
				}
			}
			else
			{
				strTimeStamp_use = strTimeStamp;
			}

			/* Generate set info file */
			CurObject->SaveCfgParametersToFile(strTimeStamp_use + _T("_cfg.txt"), &packetConfig, TRUE);

			/* Reset collected wave num -> 0 */
			strTmp.Format(_T("%d"), 0);
			CurObject->m_CtrlEditCollectedCnt.SetWindowText(strTmp);

			const WaveAcquisition::AcquisitionSummary summary = RunAcquisitionCycleWithEngine(
				CurObject,
				strTimeStamp_use,
				OneWaveSize_L,
				OneWaveSize_H);
			ErrExit = summary.terminalStatus != WaveAcquisition::TerminalStatus::Success;
			if (summary.terminalStatus == WaveAcquisition::TerminalStatus::Stopped)
			{
				runtime = false;
			}

			strTmp.Format(_T("Read over, saved wave count %lu."), summary.savedWaveCount);
			CurObject->m_pMainDlg->PrintLog(strTmp);

			/* Disenable start button */
			CurObject->m_CtrlBtnDataGetStart.EnableWindow(FALSE);

			/* Manual Mode: Stop FPGA sampling */
			if (CurObject->m_bManualMode == TRUE)
			{
				if (CurObject->m_pMainDlg->UsbLibInfo.EP4_GetData(pEp2DataBuf) != USB_SUCCESS)
				{
					CurObject->m_pMainDlg->PrintLog(_T("Get ep4 register data failed."));
					break;
				}

				CurObject->RegSet_GetWaveDataStart(FALSE, pEp2DataBuf);

				if (CurObject->m_pMainDlg->UsbLibInfo.EP2_SendData(pEp2DataBuf) != USB_SUCCESS)
				{
					CurObject->m_pMainDlg->PrintLog(_T("Send ep2 register data failed."));
					break;
				}
			}

			/* Set sampling UI status */
			CurObject->SamplingUISet(TRUE, CurObject->m_bManualMode);

			/* Flush log file after each measurement cycle */
			CurObject->m_pMainDlg->FlushLog();
		} while (runtime);
	} while (0);

FINALIZE_THREAD:
	/* Restore button status */
	CurObject->m_CtrlBtnDataGetStart.SetWindowText(_T("Data Get Start"));
	CurObject->m_CtrlBtnDataGetStart.EnableWindow(TRUE);

	/* Reset global variable */
	g_bEP6ThreadFlag = 0;
	g_bEP24LoopFlag = 0;
	g_bStartSampling = 0;

	CurObject->m_pMainDlg->PrintLog(_T("Exit EP6 get data thread."));
	CurObject->m_pMainDlg->FlushLog();
}

INT CreateWaveDataFile(
	CFile* fp_h,
	CFile* fp_l,
	const CString& TimeStamp,
	INT Index,
	CString* outFinalPath_l,
	CString* outFinalPath_h,
	CString* outTmpPath_l,
	CString* outTmpPath_h,
	DWORD* outLastError)
{
	CString strIndex;
	if (outLastError != nullptr)
	{
		*outLastError = ERROR_SUCCESS;
	}

	if (fp_h == nullptr || fp_l == nullptr)
	{
		if (outLastError != nullptr)
		{
			*outLastError = ERROR_INVALID_PARAMETER;
		}
		return CreateWaveDataFileOpenLowFailed;
	}

	CString finalPathLow;
	CString finalPathHigh;
	strIndex.Format(_T("_fl_%d.bin"), Index);
	finalPathLow = TimeStamp + strIndex;
	strIndex.Format(_T("_fh_%d.bin"), Index);
	finalPathHigh = TimeStamp + strIndex;
	CString tmpPathLow = finalPathLow + _T(".tmp");
	CString tmpPathHigh = finalPathHigh + _T(".tmp");

	if (outFinalPath_l != nullptr)
	{
		*outFinalPath_l = finalPathLow;
	}
	if (outFinalPath_h != nullptr)
	{
		*outFinalPath_h = finalPathHigh;
	}
	if (outTmpPath_l != nullptr)
	{
		*outTmpPath_l = tmpPathLow;
	}
	if (outTmpPath_h != nullptr)
	{
		*outTmpPath_h = tmpPathHigh;
	}

	if (!fp_l->Open(tmpPathLow, CFile::modeCreate | CFile::modeWrite))
	{
		if (outLastError != nullptr)
		{
			*outLastError = ::GetLastError();
		}
		return CreateWaveDataFileOpenLowFailed;
	}

	if (!fp_h->Open(tmpPathHigh, CFile::modeCreate | CFile::modeWrite))
	{
		const DWORD openError = ::GetLastError();
		DWORD closeError = ERROR_SUCCESS;
		FlushAndCloseFile(fp_l, &closeError);
		::DeleteFile(tmpPathLow);
		if (outLastError != nullptr)
		{
			*outLastError = openError;
		}
		return CreateWaveDataFileOpenHighFailed;
	}

	return CreateWaveDataFileOk;
}

INT SaveWaveDataToFile(CFile* fp_l, CFile* fp_h, const BYTE* WaveData, ULONG FrameSize_L, ULONG FrameSize_H, INT WaveCnt)
{
	if (fp_l == nullptr || fp_h == nullptr)
	{
		::SetLastError(ERROR_INVALID_PARAMETER);
		return kUsbErrFileIo;
	}

	CFileWriterAdapter writerLow(fp_l);
	CFileWriterAdapter writerHigh(fp_h);
	const INT saveResult = WaveDataFileIO::SaveWaveDataToFileImpl(
		writerLow,
		writerHigh,
		WaveData,
		FrameSize_L,
		FrameSize_H,
		WaveCnt);

	if (saveResult != WaveDataFileIO::kSaveWaveDataOk)
	{
		return kUsbErrFileIo;
	}

	return USB_SUCCESS;
}

INT SaveWaveDataToCHFile(CFile fp[12], PBYTE WaveData, ULONG FrameSize_L, ULONG FrameSize_H, INT WaveCnt, ULONG OneHighSize, ULONG OneLowSize)
{
	ULONG FrameSize = FrameSize_L + FrameSize_H;
	INT iRet = 0;
	INT h_chnum = 0;
	INT l_chnum = 0;

	for (int i = 0; i < WaveCnt; i++)
	{
		for (int j = 0; j < 12; j++)
		{
			if (packetConfig.CHSelect[j] == 1)
			{
				if (j < 8)
				{
					l_chnum++;
					fp[j].Write(WaveData + ((size_t)i * (size_t)FrameSize) + ((size_t)l_chnum * (size_t)OneLowSize), OneLowSize);
				}
				else
				{
					h_chnum++;
					fp[j].Write(WaveData + ((size_t)i * (size_t)FrameSize) + ((size_t)l_chnum * (size_t)OneLowSize) + ((size_t)h_chnum * (size_t)OneHighSize), OneHighSize);
				}
			}
		}
	}

	return iRet;
}

INT Dialog1_Main::UpdateConfigStruct(FPGAConfigI_REGMAP* packetConfig)
{
	CString		strValue;
	CString		strTmp;
	CComboBox*	pComboBox;
	CButton*	pCheckBox;
	CButton*	pRadioButton;
	INT			iErrFlag = E_OK;

	UpdateData(TRUE);

	for (int i = 0; i < 13; i++) 
	{
		/* Get channel[i] gain value from UI */
		for (int j = 0; j < 5; j++) 
		{
			if ((i < 8) && (j == 2))//lpw ch 1~8, gain3
			{
				editMultp3GainCh[i]->GetWindowTextW(strValue);

				if ((_tstof(strValue) < -1.0) || (_tstof(strValue) > -0.5))
				{
					strTmp.Format(_T("CH[%d] gain3 = %.1f is not in range(-1.0 ~ -0.5)"), i + 1, _tstof(strValue));
					m_pMainDlg->PrintLog(strTmp);

					EditCtrl_HighLight(editMultp3GainCh[i], TRUE);
					iErrFlag = E_FALSE;
				}
				else
				{
					EditCtrl_HighLight(editMultp3GainCh[i], FALSE);
				}
			}
			else
			{
				comboxGainCh[i][j]->GetLBText(comboxGainCh[i][j]->GetCurSel(), strValue);
			}
			
			packetConfig->GainCh[i][j] = _tstof(strValue);
#if LOG_SWITCH
			strTmp.Format(_T("packetConfig->GainCh[%d][%d] = %f"),i, j, packetConfig->GainCh[i][j]);
			m_pMainDlg->PrintLog(strTmp);
#endif
		}

		/* Get channel[i] offset Value(1414~1494) */
		editOffsetCh[i]->GetWindowTextW(strValue);
		packetConfig->OffsetValue[i] = (FLOAT)_tstof(strValue);

#if LOG_SWITCH
		strTmp.Format(_T("packetConfig->OffsetValue[%d] = %d"), i, packetConfig->OffsetValue[i]);
		m_pMainDlg->PrintLog(strTmp);
#endif
		if (packetConfig->OffsetValue[i] < 1414.0 || packetConfig->OffsetValue[i] > 1494.0)
		{
			//m_edit_ch1_offset.SetHighlight(1, 3);
			strTmp.Format(_T("OffsetValue CH[%d] = %.1f is not in range(1414~1494)"), i + 1, packetConfig->OffsetValue[i]);
			m_pMainDlg->PrintLog(strTmp);

			EditCtrl_HighLight(editOffsetCh[i], TRUE);
			iErrFlag = E_FALSE;
		}
		else
		{
			EditCtrl_HighLight(editOffsetCh[i], FALSE);
		}

		/* Select channel[i] */
		pCheckBox = (CButton*)GetDlgItem(editChSelect[i]);
		packetConfig->CHSelect[i] = pCheckBox->GetCheck();
#if LOG_SWITCH
		strTmp.Format(_T("packetConfig->CHSelect[%d] = %d"), i, packetConfig->CHSelect[i]);
		m_pMainDlg->PrintLog(strTmp);
#endif
		/* Get channel[i](8~12) Extensive Control Volume 1(0~1100mV) */
		if (i >= 8 && i < 12)
		{
			editVol1Ch[i - 8]->GetWindowTextW(strValue);
			packetConfig->ExtCtrlVol1[i - 8] = _tstoi(strValue);
#if LOG_SWITCH
			strTmp.Format(_T("packetConfig->ExtCtrlVol1[%d] = %d"), i - 8, packetConfig->ExtCtrlVol1[i - 8]);
			m_pMainDlg->PrintLog(strTmp);
#endif
			if (packetConfig->ExtCtrlVol1[i - 8] > 1100 || packetConfig->ExtCtrlVol1[i - 8] < 0)
			{
				strTmp.Format(_T("ExtCtrlVol High CH[%d] = %d is not in range(0~1100mV)"), i + 1, packetConfig->ExtCtrlVol1[i - 8]);
				m_pMainDlg->PrintLog(strTmp);
				EditCtrl_HighLight(editVol1Ch[i - 8], TRUE);
				iErrFlag = E_FALSE;
			}
			else
			{
				EditCtrl_HighLight(editVol1Ch[i - 8], FALSE);
			}
		}

		/* Get channel[i](13) Extensive Control Volume 1(0~5000mV) */
		if (i == 12)
		{		
			editVol1Ch[i - 8]->GetWindowTextW(strValue);
			packetConfig->ExtCtrlVol1[i - 8] = _tstoi(strValue);
#if LOG_SWITCH
			strTmp.Format(_T("packetConfig->ExtCtrlVol1[%d] = %d"), i - 8, packetConfig->ExtCtrlVol1[i - 8]);
			m_pMainDlg->PrintLog(strTmp);
#endif
			if (packetConfig->ExtCtrlVol1[i - 8] > 5000 || packetConfig->ExtCtrlVol1[i - 8] < 0)
			{
				strTmp.Format(_T("ExtCtrlVol High CH[%d] = %d is not in range(0~5000mV)"), i + 1, packetConfig->ExtCtrlVol1[i - 8]);
				m_pMainDlg->PrintLog(strTmp);
				EditCtrl_HighLight(editVol1Ch[i - 8], TRUE);
				iErrFlag = E_FALSE;
			}
			else
			{
				EditCtrl_HighLight(editVol1Ch[i - 8], FALSE);
			}
		}

		/* Get channel[i](2~7) Extensive Control Volume 2(0~4096mV) */
		if (i >= 2 && i < 8)
		{
			editVol2Ch[i - 2]->GetWindowTextW(strValue);
			packetConfig->ExtCtrlVol2[i - 2] = _tstoi(strValue);
#if LOG_SWITCH
			strTmp.Format(_T("packetConfig->ExtCtrlVol2[%d] = %d"), i, packetConfig->ExtCtrlVol2[i - 2]);
			m_pMainDlg->PrintLog(strTmp);
#endif
			if (packetConfig->ExtCtrlVol2[i - 2] > 4096 || packetConfig->ExtCtrlVol2[i - 2] < 0)
			{
				strTmp.Format(_T("ExtCtrlVol Low CH[%d] = %d is not in range(0~4096mV)"), i + 1, packetConfig->ExtCtrlVol2[i - 2]);
				m_pMainDlg->PrintLog(strTmp);
				EditCtrl_HighLight(editVol2Ch[i - 2], TRUE);
				iErrFlag = E_FALSE;
			}
			else
			{
				EditCtrl_HighLight(editVol2Ch[i - 2], FALSE);
			}
		}
	}

	/* Select Fir filter FC(Hi frq sig) */
	pComboBox = (CComboBox*)GetDlgItem(IDC_COMBO_FIR_FILTER_FC);
	packetConfig->FirFilterFC = pComboBox->GetCurSel();
#if LOG_SWITCH
	strTmp.Format(_T("packetConfig->FirFilterFC = %d"), packetConfig->FirFilterFC);
	m_pMainDlg->PrintLog(strTmp);
#endif

	/* Select trigger channel(1~13) */
	pComboBox = (CComboBox*)GetDlgItem(IDC_COMBO_TRIG_CH);
	packetConfig->TriggerCh = pComboBox->GetCurSel() + 1;
#if LOG_SWITCH
	strTmp.Format(_T("packetConfig->TriggerCh = %d"), packetConfig->TriggerCh);
	m_pMainDlg->PrintLog(strTmp);
#endif

	/* Get trigger value(0~1800mV) of trigger channel(1~13) */
	m_edit_trigger_value.GetWindowTextW(strValue);
	packetConfig->TriggerValue = _tstoi(strValue);
#if LOG_SWITCH
	strTmp.Format(_T("packetConfig->TriggerValue = %d"), packetConfig->TriggerValue);
	m_pMainDlg->PrintLog(strTmp);
#endif

	if (packetConfig->TriggerValue > 1800 || packetConfig->TriggerValue < 0)
	{
		strTmp.Format(_T("TriggerValue = %d is not in range(0~1800mV)"), packetConfig->TriggerValue);
		m_pMainDlg->PrintLog(strTmp);
		EditCtrl_HighLight(&m_edit_trigger_value, TRUE);
		iErrFlag = E_FALSE;
	}
	else
	{
		EditCtrl_HighLight(&m_edit_trigger_value, FALSE);
	}

	/* Get trigger range(-55us~+55us) of trigger channel(1~13) */
#if true
	UpdateRangeDisplay(packetConfig);
#if LOG_SWITCH
	strTmp.Format(_T("packetConfig->TriggerRange[0] = %.1f"), packetConfig->TriggerRange[0]);
	m_pMainDlg->PrintLog(strTmp);
	strTmp.Format(_T("packetConfig->TriggerRange[1] = %.1f"), packetConfig->TriggerRange[1]);
	m_pMainDlg->PrintLog(strTmp);
#endif
#else
	m_edit_trigger_low.GetWindowTextW(strValue);
	packetConfig->TriggerRange[0] = _tstoi(strValue);
#if LOG_SWITCH
	strTmp.Format(_T("packetConfig->TriggerRange[0] = %d"), packetConfig->TriggerRange[0]);
	m_pMainDlg->PrintLog(strTmp);
#endif

	m_edit_trigger_high.GetWindowTextW(strValue);
	packetConfig->TriggerRange[1] = _tstoi(strValue);
#if LOG_SWITCH
	strTmp.Format(_T("packetConfig->TriggerRange[1] = %d"), packetConfig->TriggerRange[1]);
	m_pMainDlg->PrintLog(strTmp);
#endif
#endif

	if ((packetConfig->TriggerRange[0] > 55.0) || (packetConfig->TriggerRange[0] < 0.0))
	{
		strTmp.Format(_T("TriggerRange[0] = -%.1f is not in range(-55us~+55us)"), packetConfig->TriggerRange[0]);
		m_pMainDlg->PrintLog(strTmp);
		EditCtrl_HighLight(&m_edit_trigger_low, TRUE);
		iErrFlag = E_FALSE;
	}
	else
	{
		EditCtrl_HighLight(&m_edit_trigger_low, FALSE);
	}

	if ((packetConfig->TriggerRange[1] > 55.0) || (packetConfig->TriggerRange[1] < 0.0))
	{
		strTmp.Format(_T("TriggerRange[1] = +%.1f is not in range(-55us~+55us)"), packetConfig->TriggerRange[1]);
		m_pMainDlg->PrintLog(strTmp);
		EditCtrl_HighLight(&m_edit_trigger_high, TRUE);
		iErrFlag = E_FALSE;
	}
	else
	{
		EditCtrl_HighLight(&m_edit_trigger_high, FALSE);
	}

	/* Get the mode of getting manual */
	pRadioButton = (CButton*)GetDlgItem(IDC_RADIO_MDOE_MANUAL);
	packetConfig->ManualMode = pRadioButton->GetCheck();
#if LOG_SWITCH
	strTmp.Format(_T("packetConfig->ManualMode = %d"), packetConfig->ManualMode);
	m_pMainDlg->PrintLog(strTmp);
#endif

	/* Get waveforms number which per file need to save */
	m_edit_wave_num.GetWindowText(strValue);
	packetConfig->WaveNum = _tstoi(strValue);
#if LOG_SWITCH
	strTmp.Format(_T("packetConfig->WaveNum = %d"), packetConfig->WaveNum);
	m_pMainDlg->PrintLog(strTmp);
#endif
	if (packetConfig->WaveNum == 0)
	{
		strTmp.Format(_T("WaveNum cannot be 0"));
		m_pMainDlg->PrintLog(strTmp);
		EditCtrl_HighLight(&m_edit_wave_num, TRUE);
		iErrFlag = E_FALSE;
	}
	else
	{
		EditCtrl_HighLight(&m_edit_wave_num, FALSE);
	}

	/* Get save path to save waveform data */
	GetDlgItemText(IDC_EDIT_SAVEPATH, strValue);
	packetConfig->SavePath = strValue;
#if LOG_SWITCH
	//strTmp.Format(_T("packetConfig->SavePath = %s"), packetConfig->SavePath);
	m_pMainDlg->PrintLog(_T("packetConfig->SavePath = ") + packetConfig->SavePath);
#endif
	const bool shouldValidateSavePathForSetParameters =
		SavePathValidation::ShouldValidateForUiTrigger(SavePathValidation::UiValidationTrigger::kSetParameters);
	CString normalizedSavePath;
	if (shouldValidateSavePathForSetParameters
		&& !ValidateSavePathUI(TRUE, &normalizedSavePath))
	{
		return -1;
	}
	packetConfig->SavePath = SavePathValidation::ResolveSavePathForSetParameters(
		strValue.GetString(),
		normalizedSavePath.GetString(),
		shouldValidateSavePathForSetParameters).c_str();

	if (iErrFlag == E_FALSE)
	{
		strTmp.Format(_T("Input parameters are error!"));
		MessageBox(strTmp, _T("Error"), MB_OK | MB_ICONERROR);
	}

	return iErrFlag;
}


void Dialog1_Main::OnBnClickedButtonExport()
{
	CString strPath;	
	INT iRet = 0;

	iRet = UpdateConfigStruct(&packetConfig);
	if (iRet != E_OK)
	{
		m_pMainDlg->PrintLog(_T("The parameters filled in are incorrect"));
		return;
	}

	GetDlgItemText(IDC_EDIT_EXPORT, strPath);
	SaveCfgParametersToFile(strPath, &packetConfig, FALSE);

	m_pMainDlg->PrintLog(_T("Parameter script export successful"));
}


void ParseCSVLine(const CString& strLine, CStringArray& arrFields)
{
	CString strDelimiter = _T(",");
	int nFieldCount = 0;

	int nPos = 0;
	int nStart = 0;
	CString strToken;

	while ((nPos = strLine.Find(strDelimiter, nStart)) != -1)
	{
		strToken = strLine.Mid(nStart, nPos - nStart);
		arrFields.Add(strToken);

		nStart = nPos + strDelimiter.GetLength();

		nFieldCount++;
	}

	strToken = strLine.Mid(nStart);
	arrFields.Add(strToken);

	return; 
}


void Dialog1_Main::OnBnClickedButtonImport()
{
	CString			strPath;
	CStdioFile		file;
	CString			strLine;
	CString			cellValue;
	CString			cellValue1;
	CString			strTmp;
	CStringArray	arrFields;
	double			dValue;
	int				count;
	int				errorCode = 0;
	int				line = 0;

	m_lastConfigImportSucceeded = false;

	char* old_locale = _strdup(setlocale(LC_CTYPE, NULL));
	setlocale(LC_ALL, "ja_JP");

	GetDlgItemText(IDC_EDIT_IMPORT, strPath);
	if (!file.Open(strPath, CFile::modeRead))
	{
		MessageBox(_T("Error, CSV file is open"), _T("Error"), MB_OK | MB_ICONERROR);
		return;
	}

	while (file.ReadString(strLine))
	{
		/* Parse strLine using ',' as separators and save the parsed strLine in a CStringArray object arrFields */
		ParseCSVLine(strLine, arrFields);

		/* Set selection of channel[i](1~8) gain(1~4) */
		if (line > 1 && line < 10)
		{
			for (int i = 0; i < 5; i++)
			{
				cellValue = arrFields[(size_t)i + 1];
				dValue = _tstof(cellValue);

				if (i == 2)//low ch 1~8, gain3
				{
					if ((dValue < strGainMultp[line - 2][i][1]) || (dValue > strGainMultp[line - 2][i][0]))
					{
						errorCode = 1;
						count = i;
						break;
					}
					else
					{
						cellValue = cellValue.Left(6);
						editMultp3GainCh[line - 2]->SetWindowText(cellValue);
					}
				}
				else
				{			
					if (dValue != strGainMultp[line - 2][i][0] && dValue != strGainMultp[line - 2][i][1])
					{
						errorCode = 1;
						count = i;
						break;
					}
					else if (dValue == strGainMultp[line - 2][i][0])
					{
						comboxGainCh[line - 2][i]->SetCurSel(0);
					}
					else if (dValue == strGainMultp[line - 2][i][1])
					{
						comboxGainCh[line - 2][i]->SetCurSel(1);
					}
				}
			}
		}

		/* Set selection of channel[i](9~13) gain(1~4) */
		if (line > 9 && line < 15)
		{
			for (int i = 0; i < 5; i++)
			{
				cellValue = arrFields[(size_t)i + 1];
				dValue = _tstof(cellValue);
				
				if (dValue != strGainMultp[line - 2][i][0] && dValue != strGainMultp[line - 2][i][1])
				{
					errorCode = 1;
					count = i;
					break;
				}
				else if (dValue == strGainMultp[line - 2][i][0])
				{
					comboxGainCh[line - 2][i]->SetCurSel(0);
				}
				else if (dValue == strGainMultp[line - 2][i][1])
				{
					comboxGainCh[line - 2][i]->SetCurSel(1);
				}
			}
		}

		if (errorCode == 1)
		{
			strTmp.Format(_T("Value of CH[%d] Gain[%d] is not in range"), line - 1, count + 1);
			m_pMainDlg->PrintLog(strTmp);
			MessageBox(strTmp, _T("Error"), MB_OK | MB_ICONERROR);
			break;
		}
		/* Set channel[i] offset Value(1414~1494) */
		if (line == 17)
		{
			for (int i = 0; i < 13; i++)
			{
				cellValue = arrFields[(size_t)i + 1];
				int	value = (int)_tstof(cellValue);
				if (value < 1414.0 || value > 1494.0)
				{
					errorCode = 1;
					strTmp.Format(_T("CH[%d] offset value is not in range(1414~1494)"), line - 1);
					m_pMainDlg->PrintLog(strTmp);
					MessageBox(strTmp, _T("Error"), MB_OK | MB_ICONERROR);
					break;
				}
				else
				{
					editOffsetCh[i]->SetWindowText(cellValue);
				}
			}
			if (errorCode == 1)
			{
				break;
			}
		}

		/* Set channel[i](9~12) Extensive Control Volume 1(0~1100mV) */
		if (line == 20)
		{
			for (int i = 0; i < 4; i++)
			{
				cellValue = arrFields[(size_t)i + 1];
				int	value = _tstoi(cellValue);
				if (value > 1100 || value < 0)
				{
					errorCode = 1;
					strTmp.Format(_T("pExtCtrlVol1 CH[%d] is not in range(0~1100mV)"), line - 1);
					m_pMainDlg->PrintLog(strTmp);
					MessageBox(strTmp, _T("Error"), MB_OK | MB_ICONERROR);
					break;
				}
				else
				{
					editVol1Ch[i]->SetWindowText(cellValue);
				}
			}
			if (errorCode == 1)
			{
				break;
			}

			/* Set channel[i](13) Extensive Control Volume 1(0~5000mV) */
			cellValue = arrFields[(size_t)5];
			int	value = _tstoi(cellValue);
			if (value > 5000 || value < 0)
			{
				errorCode = 1;
				strTmp.Format(_T("pExtCtrlVol1 CH[%d] is not in range(0~5000mV)"), line - 1);
				m_pMainDlg->PrintLog(strTmp);
				MessageBox(strTmp, _T("Error"), MB_OK | MB_ICONERROR);
				break;
			}
			else
			{
				editVol1Ch[4]->SetWindowText(cellValue);
			}			
			if (errorCode == 1)
			{
				break;
			}
		}

		/* Set channel[i](3~8) Extensive Control Volume 2(0~4096mV) */
		if (line == 21)
		{
			for (int i = 0; i < 6; i++)
			{
				cellValue = arrFields[(size_t)i + 1];
				int	value = _tstoi(cellValue);
				if (value > 4096 || value < 0)
				{
					errorCode = 1;
					strTmp.Format(_T("pExtCtrlVol2 CH[%d] is not in range(0~4096mV)"), line - 1);
					m_pMainDlg->PrintLog(strTmp);
					MessageBox(strTmp, _T("Error"), MB_OK | MB_ICONERROR);
					break;
				}
				else
				{
					editVol2Ch[i]->SetWindowText(cellValue);
				}
			}
			if (errorCode == 1)
			{
				break;
			}
		}

		/* Set selection of Fir filter FC(Hi frq sig) */
		if (line == 25)
		{
			cellValue = arrFields[1];
			int	value = _tstoi(cellValue);
			if (value > 1 || value < 0)
			{
				strTmp.Format(_T("This option is not available"));
				m_pMainDlg->PrintLog(strTmp);
				MessageBox(strTmp, _T("Error"), MB_OK | MB_ICONERROR);
				break;
			}
			else
			{
				m_combox_fir_filter_fc.SetCurSel(value);
			}
		}

		/* Set selection of channel[i] */
		if (line == 29)
		{
			for (int i = 0; i < 13; i++)
			{
				cellValue = arrFields[(size_t)i + 1];
				int	value = _tstoi(cellValue);
				if (value > 1 || value < 0)
				{
					errorCode = 1;
					strTmp.Format(_T("This option is not available"));
					m_pMainDlg->PrintLog(strTmp);
					MessageBox(strTmp, _T("Error"), MB_OK | MB_ICONERROR);
					break;
				}
				else
				{
					if (value == 1)
					{
						buttonSelectCh[i]->SetCheck(BST_CHECKED);
					}
					else
					{
						buttonSelectCh[i]->SetCheck(BST_UNCHECKED);
					}
				}
			}
			if (errorCode == 1)
			{
				break;
			}
			UpdateChSelect();
		}

		/* Set selection of trigger channel(1~13) */
		if (line == 30)
		{
			cellValue = arrFields[1];
			int	value = _tstoi(cellValue);
			if (value > 13 || value <= 0)
			{
				strTmp.Format(_T("This trigger ch option is not available"));
				m_pMainDlg->PrintLog(strTmp);
				MessageBox(strTmp, _T("Error"), MB_OK | MB_ICONERROR);
				break;
			}
			else
			{
				m_combox_trigch.SetCurSel(value - 1);
			}
		}

		/* Set trigger value(0~1800mV) of trigger channel(1~13) */
		if (line == 31)
		{
			cellValue = arrFields[1];
			int	value = _tstoi(cellValue);
			if (value > 1800 || value < 0)
			{
				strTmp.Format(_T("TriggerValue = %d is not in range(0~1800mV)"), value);
				m_pMainDlg->PrintLog(strTmp);
				MessageBox(strTmp, _T("Error"), MB_OK | MB_ICONERROR);
				break;
			}
			else
			{
				m_edit_trigger_value.SetWindowText(cellValue);
			}
		}

		/* Set trigger range(-55us~+55us) of trigger channel(1~13) */
		if (line == 32)
		{
			cellValue = arrFields[1];
			cellValue1 = arrFields[2];
			FLOAT	value = (FLOAT)_tstof(cellValue);
			FLOAT value_1 = (FLOAT)_tstof(cellValue1);
			if (value_1 > 55.0 || value_1 < 0.0 || value < -55.0 || value > 0.0)
			{
				strTmp.Format(_T("TriggerRange[0] = %.1f or TriggerRange[1] = %.1f is not in range(-55us~+55us)"), value, value_1);
				m_pMainDlg->PrintLog(strTmp);
				MessageBox(strTmp, _T("Error"), MB_OK | MB_ICONERROR);
				break;
			}
			else
			{
				CString newCellValue = cellValue;
				newCellValue.Replace(L"-", L"");
				m_edit_trigger_low.SetWindowText(newCellValue);
				m_edit_trigger_high.SetWindowText(cellValue1);
			}
		}

		/* Set the mode of getting manual */
		if (line == 35)
		{
			cellValue = arrFields[1];
			int	value = _tstoi(cellValue);
			if (value > 1 || value < 0)
			{
				strTmp.Format(_T("This option of manual mode is not available"));
				m_pMainDlg->PrintLog(strTmp);
				MessageBox(strTmp, _T("Error"), MB_OK | MB_ICONERROR);
				break;
			}
			else
			{
				if (value == 0)
				{
					m_button_mode_manual.SetCheck(BST_UNCHECKED);
					m_button_mode_auto.SetCheck(BST_CHECKED);
					OnBnClickedRadioMdoeAuto();
				}
				else
				{
					m_button_mode_manual.SetCheck(BST_CHECKED);
					m_button_mode_auto.SetCheck(BST_UNCHECKED);
					OnBnClickedRadioMdoeManual();
				}
			}
		}

		/* Set waveforms number which per file need to save */
		if (line == 36)
		{
			cellValue = arrFields[1];
			int	value = _tstoi(cellValue);
			if (value < 0)
			{
				strTmp.Format(_T("Waveforms Nums Per File: %d cannot be less than 0"), value);
				m_pMainDlg->PrintLog(strTmp);
				MessageBox(strTmp, _T("Error"), MB_OK | MB_ICONERROR);
				break;
			}
			else
			{
				m_edit_wave_num.SetWindowText(cellValue);
			}
		}

		/* Set save path to save waveform data */
		if (line == 37)
		{
			cellValue = arrFields[1];
			if (cellValue.GetLength() == 0)
			{
				strTmp.Format(_T("SavePath cannot be NULL"));
				m_pMainDlg->PrintLog(strTmp);
				MessageBox(strTmp, _T("Error"), MB_OK | MB_ICONERROR);
				break;
			}
			else
			{
				m_edit_savepath.SetWindowText(cellValue);
			}
		}
		
		arrFields.RemoveAll();
		line++;
	}

	setlocale(LC_CTYPE, old_locale);
	free(old_locale);

	file.Close();

	/* Update Gain result */
	for (int i = 0; i < 13; i++)
	{
		UpdateTotalGain(i);
	}

	ValidateSavePathUI(FALSE);

	if (line != 38)
	{
		strTmp.Format(_T("The data of default_config.csv is incomplete and the parameter script import fails"));
		m_pMainDlg->PrintLog(strTmp);
		MessageBox(strTmp, _T("Error"), MB_OK | MB_ICONERROR);
	}
	else
	{
		m_lastConfigImportSucceeded = true;
		m_pMainDlg->PrintLog(_T("Parameter script import successful"));
	}
}


bool Dialog1_Main::ImportDefaultConfigFile()
{
	UpdateData(TRUE);

	m_edit_import.SetWindowTextW(_T("default_config.csv"));

	OnBnClickedButtonImport();

	return m_lastConfigImportSucceeded;
}


void  Dialog1_Main::ExportDefaultConfigFile()
{
	UpdateData(TRUE);

	m_edit_export.SetWindowTextW(_T("default_config.csv"));

	OnBnClickedButtonExport();
	return;
}


void Dialog1_Main::OnBnClickedButtonImportSelect()
{
	CString	strDefExt;
	CString	strFilter;

	UpdateData(TRUE);

	strDefExt = _T("csv");
	strFilter = _T("File(*.CSV)|*.CSV||");

	UpdateData(TRUE);

	CFileDialog fileDlg(FALSE, strDefExt, NULL, NULL, strFilter);

	if (IDOK == fileDlg.DoModal())
	{
		m_ImportFile = fileDlg.GetPathName();
	}

	UpdateData(FALSE);
}


void Dialog1_Main::OnBnClickedButtonExportSelect()
{
	CString	strDefExt;
	CString	strFilter;

	UpdateData(TRUE);

	strDefExt = _T("csv");
	strFilter = _T("File(*.CSV)|*.CSV||");

	UpdateData(TRUE);

	CFileDialog fileDlg(FALSE, strDefExt, NULL, NULL, strFilter);

	if (IDOK == fileDlg.DoModal())
	{
		m_ExportFile = fileDlg.GetPathName();
	}

	UpdateData(FALSE);
}


void Dialog1_Main::OnBnClickedButtonSavepathSelect()
{
	TCHAR           szFolderPath[MAX_PATH] = { 0 };
	CString         strFolderPath = TEXT("");
	bool            folderDialogCanceled = false;

	UpdateData(TRUE);
	BROWSEINFO      sInfo;
	::ZeroMemory(&sInfo, sizeof(BROWSEINFO));
	sInfo.pidlRoot = 0;
	sInfo.lpszTitle = _T("Select waveforms test save folder");
	sInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_EDITBOX | BIF_DONTGOBELOWDOMAIN;
	sInfo.lpfn = NULL;

	LPITEMIDLIST lpidlBrowse = ::SHBrowseForFolder(&sInfo);
	if (lpidlBrowse != NULL)
	{ 
		if (::SHGetPathFromIDList(lpidlBrowse, szFolderPath))
		{
			m_OutFile = szFolderPath;
		}
	}
	else
	{
		folderDialogCanceled = true;
	}
	if (lpidlBrowse != NULL)
	{
		::CoTaskMemFree(lpidlBrowse);
	}

	UpdateData(FALSE);
	const SavePathValidation::UiValidationTrigger trigger =
		folderDialogCanceled
		? SavePathValidation::UiValidationTrigger::kFolderDialogCancel
		: SavePathValidation::UiValidationTrigger::kFolderDialogConfirmed;
	if (SavePathValidation::ShouldValidateForUiTrigger(trigger))
	{
		ValidateSavePathUI(SavePathValidation::ShouldShowDialogForUiTrigger(trigger));
	}
}


void Dialog1_Main::Reg_Write(UINT Address, USHORT Data, PBYTE Ep2RegBuffer)
{
	FpgaRegLogic::Reg_Write(Address, Data, Ep2RegBuffer);
}


USHORT Dialog1_Main::Reg_Read(UINT Address, PBYTE Ep4RegBuffer)
{
	return FpgaRegLogic::Reg_Read(Address, Ep4RegBuffer);
}


void Dialog1_Main::RegSet_SetGainValue(double GainValue[13][5], PBYTE Ep2DataBuffer)
{
	USHORT usData = 0;
	USHORT usTemp = 0;
	CString strTmp;
	double accuracy = 0.5 / 511.0;
	double dData = 0.0;

	/* Set ch1~8 gain3 */
	for (int i = 0; i < 8; i++)
	{
		if((GainValue[i][2] > -0.5) || (GainValue[i][2] < -1.0))
		{
			strTmp.Format(_T("CH%d Gain3 Value = %f is error."), i + 1, GainValue[i][2]);
			m_pMainDlg->PrintLog(strTmp);
		}
		else
		{
			usData = FpgaRegLogic::CalcGain3RegValue(GainValue[i][2]);

			Reg_Write((UINT)(FPGAREG_GAIN_DAT_CH1 + (i * 2)), usData, Ep2DataBuffer);
		}
	}

	/* Set ch1~12 gain1,2,*/
	for (int n = 0; n < 3; n++)
	{
		usData = 0;

		for (int i = 3; i >= 0; i--)
		{
			usTemp = 0;

			for (int j = 1; j >= 0; j--)
			{
				if (GainValue[n * 4 + i][j] == strGainMultp[n * 4 + i][j][0])
				{
					usTemp = (usTemp << 1) | 0x0;
				}
				else if (GainValue[n * 4 + i][j] == strGainMultp[n * 4 + i][j][1])
				{
					usTemp = (usTemp << 1) | 0x1;
				}
				else
				{
					strTmp.Format(_T("CH%d Gain%d Value = %f is error."), n * 4 + i + 1, j + 1, GainValue[n * 4 + i][j]);
					m_pMainDlg->PrintLog(strTmp);

					strTmp.Format(_T("Expected Value1 = %f, Value2 = %f."), strGainMultp[n * 4 + i][j][0], strGainMultp[n * 4 + i][j][1]);
					m_pMainDlg->PrintLog(strTmp);
				}
			}

			usData = (usData << 4) | usTemp;
		}

		//strTmp.Format(_T("Value%d = 0x%x."), n + 1, usData);
		//m_pMainDlg->PrintLog(strTmp);
		Reg_Write((UINT)(FPGAREG_GAIN_SW_CH1_4 + (n * 2)), usData, Ep2DataBuffer);		
	}

	/* Set ch13 gain1,2,*/
	usTemp = 0;

	for (int j = 1; j >= 0; j--)
	{
		if (GainValue[12][j] == strGainMultp[12][j][0])
		{
			usTemp = (usTemp << 1) | 0x0;
		}
		else if (GainValue[12][j] == strGainMultp[12][j][1])
		{
			usTemp = (usTemp << 1) | 0x1;
		}
		else
		{
			strTmp.Format(_T("CH%d Gain%d Value = %f is error."), 13, j + 1, GainValue[12][j]);
			m_pMainDlg->PrintLog(strTmp);

			strTmp.Format(_T("Expected Value1 = %f, Value2 = %f."), strGainMultp[12][j][0], strGainMultp[12][j][1]);
			m_pMainDlg->PrintLog(strTmp);
		}
	}

	Reg_Write((UINT)(FPGAREG_GAIN_SW_CH1_4 + (3 * 2)), usTemp, Ep2DataBuffer);
}


void Dialog1_Main::RegSet_SetOffsetValue(INT CHID, FLOAT OffsetValaue, PBYTE Ep2DataBuffer)
{
	FpgaRegLogic::RegSet_SetOffsetValue(CHID, OffsetValaue, Ep2DataBuffer);
}


void Dialog1_Main::RegSet_SetExtCtrlVol_1(INT CHID, USHORT ExtCtrlVolValaue, PBYTE Ep2DataBuffer)
{
	FpgaRegLogic::RegSet_SetExtCtrlVol_1(CHID, ExtCtrlVolValaue, Ep2DataBuffer);
}


void Dialog1_Main::RegSet_SetExtCtrlVol_2(INT CHID, USHORT ExtCtrlVolValaue, PBYTE Ep2DataBuffer)
{
	FpgaRegLogic::RegSet_SetExtCtrlVol_2(CHID, ExtCtrlVolValaue, Ep2DataBuffer);
}


void Dialog1_Main::RegSet_UpdateGainValue(PBYTE Ep2DataBuffer)
{
	FpgaRegLogic::RegSet_UpdateGainValue(Ep2DataBuffer);
}


void Dialog1_Main::RegSet_UpdateOffsetValue(PBYTE Ep2DataBuffer)
{
	FpgaRegLogic::RegSet_UpdateOffsetValue(Ep2DataBuffer);
}


void Dialog1_Main::RegSet_UpdateExtCtrlVol(PBYTE Ep2DataBuffer)
{
	FpgaRegLogic::RegSet_UpdateExtCtrlVol(Ep2DataBuffer);
}


void Dialog1_Main::RegSet_SelectFirFilterFC(UCHAR FirFilterFC, PBYTE Ep2DataBuffer)
{
	FpgaRegLogic::RegSet_SelectFirFilterFC(FirFilterFC, Ep2DataBuffer);
}


void Dialog1_Main::RegSet_SelectDataCH(UCHAR CHSelect[13], PBYTE Ep2DataBuffer)
{
	FpgaRegLogic::RegSet_SelectDataCH(CHSelect, Ep2DataBuffer);
}


void Dialog1_Main::RegSet_SelectTRGCH(UCHAR TRGCH, PBYTE Ep2DataBuffer)
{
	FpgaRegLogic::RegSet_SelectTRGCH(TRGCH, Ep2DataBuffer);
}


void Dialog1_Main::RegSet_SetTRGValue(USHORT TRGValue, PBYTE Ep2DataBuffer)
{
	FpgaRegLogic::RegSet_SetTRGValue(TRGValue, Ep2DataBuffer);
}


void Dialog1_Main::RegSet_SetTRGRange(INT RangeSel, FLOAT TRGRangeValue, PBYTE Ep2DataBuffer)
{
	FpgaRegLogic::RegSet_SetTRGRange(RangeSel, TRGRangeValue, Ep2DataBuffer);
}


void Dialog1_Main::RegSet_SelectGetDataMeas(UCHAR ManualMode, PBYTE Ep2DataBuffer)
{
	FpgaRegLogic::RegSet_SelectGetDataMeas(ManualMode, Ep2DataBuffer);
}


void Dialog1_Main::RegSet_GetWaveDataStart(BOOL StartFlag, PBYTE Ep2DataBuffer)
{
	FpgaRegLogic::RegSet_GetWaveDataStart(StartFlag, Ep2DataBuffer);
}


ULONG Dialog1_Main::RegGet_DDRWaveCnt(PBYTE Ep4DataBuffer)
{
	return FpgaRegLogic::RegGet_DDRWaveCnt(Ep4DataBuffer);
}

ULONG Dialog1_Main::RegGet_DDRReadCnt(PBYTE Ep4DataBuffer)
{
	return FpgaRegLogic::RegGet_DDRReadCnt(Ep4DataBuffer);
}


INT Dialog1_Main::RegGet_DDRWriteEnd(PBYTE Ep4DataBuffer)
{
	return FpgaRegLogic::RegGet_DDRWriteEnd(Ep4DataBuffer);
}

INT Dialog1_Main::RegGet_DDRReadEnd(PBYTE Ep4DataBuffer)
{
	return FpgaRegLogic::RegGet_DDRReadEnd(Ep4DataBuffer);
}


bool Dialog1_Main::RegGet_SampleStartSt(PBYTE Ep4DataBuffer)
{
	return FpgaRegLogic::RegGet_SampleStartSt(Ep4DataBuffer);
}


void Dialog1_Main::EditCtrl_HighLight(CColorEdit* EditCtrl, BOOL HLFlag)
{
	if (HLFlag)
	{
		EditCtrl->SetBkColor(RGB(255, 96, 96));
		EditCtrl->SetRedraw(TRUE);
		EditCtrl->Invalidate();
		EditCtrl->UpdateWindow();
	}

	else
	{
		EditCtrl->SetBkColor(RGB(255, 255, 255));
		EditCtrl->SetRedraw(TRUE);
		EditCtrl->Invalidate();
		EditCtrl->UpdateWindow();
	}
}


void Dialog1_Main::SaveCfgParametersToFile(CString FilePath, FPGAConfigI_REGMAP* packetConfig, BOOL totalGainValue)
{
	CStdioFile	file;
	char* old_locale = _strdup(setlocale(LC_CTYPE, NULL));
	setlocale(LC_ALL, "ja_JP");

	if (!file.Open(FilePath, CFile::modeCreate | CFile::modeWrite | CFile::typeText))
	{
		MessageBox(_T("Error, CSV file is open"), _T("Error"), MB_OK | MB_ICONERROR);
		return;
	}

	CString line;
	line.Format(_T("# Gain Value Set\n"));
	file.WriteString(line);
	if (totalGainValue)
	{
		line.Format(_T(" ,Gain1,Gain2,Gain3(CH9~13 fixed),Gain4(CH9~13 fixed),Gain5(fixed),TotalGain\n"));
	}
	else
	{
		line.Format(_T(" ,Gain1,Gain2,Gain3(CH9~13 fixed),Gain4(CH9~13 fixed),Gain5(fixed)\n"));
	}
	
	file.WriteString(line);

	/* Write value of channel[i] gain to csv */
	for (int i = 0; i < 13; i++)
	{
		for (int j = 0; j < 6; j++)
		{
			if (j == 0)
			{
				line.Format(_T("CH%d:,"), i + 1);
			}
			else if(i < 8 && j == 3)//Gain3
			{
				line.Format(_T("%.3f,"), packetConfig->GainCh[i][j - 1]);
			}
			else
			{
				line.Format(_T("%f,"), packetConfig->GainCh[i][j - 1]);
			}
			file.WriteString(line);
		}

		if (totalGainValue)
		{
			line.Format(_T("%.2f倍\n"), packetConfig->GainCh[i][0] * packetConfig->GainCh[i][1] *
			packetConfig->GainCh[i][2] * packetConfig->GainCh[i][3] * packetConfig->GainCh[i][4]);
		}
		else
		{
			line.Format(_T("\n"));
		}

		file.WriteString(line);
	}

	/* Write channel[i] offset Value(1414~1494) to csv */
	line.Format(_T("\n"));
	file.WriteString(line);
	line.Format(_T("# Offset Value Set\n"));
	file.WriteString(line);
	line.Format(_T("CH1~CH13(1414~1494mv):,"));
	file.WriteString(line);
	for (int i = 0; i < 13; i++)
	{
		line.Format(_T("%.1f,"), packetConfig->OffsetValue[i]);
		file.WriteString(line);
	}
	line.Format(_T("\n"));
	file.WriteString(line);

	/* Write channel[i](8~13) Extensive Control Volume 1 to csv */
	line.Format(_T("\n"));
	file.WriteString(line);
	line.Format(_T("# Ext Ctrl Vol Set\n"));
	file.WriteString(line);
	line.Format(_T("CH9~CH12(0~1100mv) CH13(0~5000mv):,"));
	file.WriteString(line);
	for (int i = 0; i < 5; i++)
	{
		line.Format(_T("%d,"), packetConfig->ExtCtrlVol1[i]);
		file.WriteString(line);
	}

	/* Write channel[i](2~7) Extensive Control Volume 2(0~4096mV) to csv */
	line.Format(_T("\n"));
	file.WriteString(line);
	line.Format(_T("CH3~CH8(0~4096mv):,"));
	file.WriteString(line);
	for (int i = 0; i < 6; i++)
	{
		line.Format(_T("%d,"), packetConfig->ExtCtrlVol2[i]);
		file.WriteString(line);
	}
	line.Format(_T("\n"));
	file.WriteString(line);

	/* Write selection of Fir filter FC(Hi frq sig) to csv */
	line.Format(_T("\n"));
	file.WriteString(line);
	line.Format(_T("# Fir filter FC Set(Hi Frq sig)\n"));
	file.WriteString(line);
	CString str1 = _T("\"0: fc=15M, SamplingRate=40Mbps\",");
	CString str2 = _T("\"1: fc=25M, SamplingRate=60Mbps\"\n");
	line = str1 + str2;
	file.WriteString(line);
	line.Format(_T("Select(0/1),%d\n"), packetConfig->FirFilterFC);
	file.WriteString(line);

	/* Write selection of channel[i] to csv */
	line.Format(_T("\n"));
	file.WriteString(line);
	line.Format(_T("# Trigger Set\n"));
	file.WriteString(line);
	line.Format(_T("Data CH Select:,"));
	file.WriteString(line);
	for (int i = 0; i < 13; i++)
	{
		line.Format(_T("CH%d,"), i + 1);
		file.WriteString(line);
	}
	line.Format(_T("\n"));
	file.WriteString(line);

	line.Format(_T("(Select:1/Not:0),"));
	file.WriteString(line);
	for (int i = 0; i < 13; i++)
	{
		line.Format(_T("%d,"), packetConfig->CHSelect[i]);
		file.WriteString(line);
	}

	line.Format(_T("\n"));
	file.WriteString(line);
	/* Write selection of trigger channel(1~13) to csv */
	line.Format(_T("Trigger CH(1~13):,%d\n"), packetConfig->TriggerCh);
	file.WriteString(line);
	/* Write Trigger Value(0~1800mV) to csv */
	line.Format(_T("Trigger Value(0~1800mv):,%d\n"), packetConfig->TriggerValue);
	file.WriteString(line);
	/* Write Trigger Range(-55us~+55us) to csv */
	line.Format(_T("Trigger Range(-55~55us):,-%.1f,%.1f\n"), packetConfig->TriggerRange[0], packetConfig->TriggerRange[1]);
	file.WriteString(line);
	if (totalGainValue)
	{	
		line.Format(_T("Total Trigger Range:,%.1fus\n"), (FLOAT)(packetConfig->TriggerRange[0] + packetConfig->TriggerRange[1]));
		file.WriteString(line);
		if (packetConfig->FirFilterFC)//60M
		{
			line.Format(_T("High freq num per sample: %d\n"), (INT)((packetConfig->TriggerRange[0] + packetConfig->TriggerRange[1]) * 60));
		}
		else//40M
		{
			line.Format(_T("High freq num per sample: %d\n"), (INT)((packetConfig->TriggerRange[0] + packetConfig->TriggerRange[1]) * 40));
		}	
		file.WriteString(line);
		line.Format(_T("Low freq num per sample: %d\n"), (INT)((packetConfig->TriggerRange[0] + packetConfig->TriggerRange[1]) * 40));
		file.WriteString(line);
	}

	line.Format(_T("\n"));
	file.WriteString(line);
	line.Format(_T("#Wave Get Set\n"));
	file.WriteString(line);
	/* Write Manual Get Mode to csv */
	line.Format(_T("Manual Get Mode(ON:1/OFF:0):,%d\n"), packetConfig->ManualMode);
	file.WriteString(line);
	/* Write the num of  waveforme to csv */
	line.Format(_T("Waveformes Nums Per File:,%d\n"), packetConfig->WaveNum);
	file.WriteString(line);
	/* Write the save path to csv Save Path*/
	line = _T("Save Path:,") + packetConfig->SavePath + _T("\n");
	file.WriteString(line);

	setlocale(LC_CTYPE, old_locale);
	free(old_locale);

	file.Close();
}


void Dialog1_Main::UpdateRangeDisplay(FPGAConfigI_REGMAP* CfgStruct)
{
	CString	strTmpe;
	FLOAT	TriggerRange[2];
	FLOAT	Multiple = 0.0;
	FLOAT	RangeMinUnit = 0.0;
	FLOAT	RangeComplement = 0.0;
	FLOAT	TotalRange = 0.0;
	INT		TotalSampleNum_HFeq = 0;
	INT		TotalSampleNum_LFeq = 0;
	CComboBox* pComboBox;

	/* Get trigger range(-55us~+55us) of trigger channel(1~13) */
	m_edit_trigger_low.GetWindowTextW(strTmpe);
	TriggerRange[0] = (FLOAT)_tstof(strTmpe);
	m_edit_trigger_high.GetWindowTextW(strTmpe);
	TriggerRange[1] = (FLOAT)_tstof(strTmpe);

	/* Select Fir filter FC(Hi frq sig) */
	pComboBox = (CComboBox*)GetDlgItem(IDC_COMBO_FIR_FILTER_FC);
	CfgStruct->FirFilterFC = pComboBox->GetCurSel();

	/* Calculation of complementary values */
	if (CfgStruct->FirFilterFC)//60M
	{
		RangeMinUnit = (FLOAT)0.8;
	}
	else//40M
	{
		RangeMinUnit = (FLOAT)0.4;
	}

	Multiple = (TriggerRange[0] + TriggerRange[1]) / RangeMinUnit;
	Multiple = (FLOAT)ceil(Multiple);
	RangeComplement = (Multiple * RangeMinUnit) - (TriggerRange[0] + TriggerRange[1]);

	if (RangeComplement)
	{
		TriggerRange[1] += RangeComplement;
	}

	if ((TriggerRange[0] > 55.0) || (TriggerRange[0] < 0.0))
	{
		EditCtrl_HighLight(&m_edit_trigger_low, TRUE);
	}
	else
	{
		EditCtrl_HighLight(&m_edit_trigger_low, FALSE);
	}

	if ((TriggerRange[1] > 55.0) || (TriggerRange[1] < 0.0))
	{
		EditCtrl_HighLight(&m_edit_trigger_high, TRUE);
	}
	else
	{
		EditCtrl_HighLight(&m_edit_trigger_high, FALSE);
	}

	CfgStruct->TriggerRange[0] = TriggerRange[0];
	CfgStruct->TriggerRange[1] = TriggerRange[1];

	TotalRange = Multiple * RangeMinUnit;
	
	if (CfgStruct->FirFilterFC)//60M
	{
		TotalSampleNum_HFeq = (INT)((CfgStruct->TriggerRange[0] + CfgStruct->TriggerRange[1]) * 60);
	}
	else//40M
	{
		TotalSampleNum_HFeq = (INT)((CfgStruct->TriggerRange[0] + CfgStruct->TriggerRange[1]) * 40);
	}

	TotalSampleNum_LFeq = (INT)((CfgStruct->TriggerRange[0] + CfgStruct->TriggerRange[1]) * 40);

	strTmpe.Format(_T("(Actual Range: -%.1f ~ %.1f)"), CfgStruct->TriggerRange[0], CfgStruct->TriggerRange[1]);
	m_static_ActualRange.SetWindowTextW(strTmpe);	
	strTmpe.Format(_T("(Actual Total range: %.1fus)"), TotalRange);
	m_static_SampleInfo.SetWindowTextW(strTmpe);
	strTmpe.Format(_T("(High freq num per sample: %d)"), TotalSampleNum_HFeq);
	m_static_SampleInfo2.SetWindowTextW(strTmpe);
	strTmpe.Format(_T("(Low freq num per sample: %d)"), TotalSampleNum_LFeq);
	m_static_SampleInfo3.SetWindowTextW(strTmpe);
}


void Dialog1_Main::OnEnChangeEditTriggerRangeLow()
{
	UpdateRangeDisplay(&packetConfig);
}


void Dialog1_Main::OnEnChangeEditTriggerRangeHigh()
{
	UpdateRangeDisplay(&packetConfig);
}


void Dialog1_Main::OnCbnSelchangeComboFirFilterFc()
{
	UpdateRangeDisplay(&packetConfig);
}


BOOL Dialog1_Main::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN)
	{
		switch (pMsg->wParam)
		{
		case VK_RETURN:
			return TRUE;
		case VK_ESCAPE:
			return TRUE;
		}
	}

	return CDialogEx::PreTranslateMessage(pMsg);
}

void Dialog1_Main::SamplingUISet(bool OpenFlag, bool samplingmode)
{
	m_combox_fir_filter_fc.EnableWindow(OpenFlag); 
	m_button_ch1_select.EnableWindow(OpenFlag);
	m_button_ch2_select.EnableWindow(OpenFlag);
	m_button_ch3_select.EnableWindow(OpenFlag);
	m_button_ch4_select.EnableWindow(OpenFlag);
	m_button_ch5_select.EnableWindow(OpenFlag);
	m_button_ch6_select.EnableWindow(OpenFlag);
	m_button_ch7_select.EnableWindow(OpenFlag);
	m_button_ch8_select.EnableWindow(OpenFlag);
	m_button_ch9_select.EnableWindow(OpenFlag);
	m_button_ch10_select.EnableWindow(OpenFlag);
	m_button_ch11_select.EnableWindow(OpenFlag);
	m_button_ch12_select.EnableWindow(OpenFlag);
	m_button_ch13_select.EnableWindow(OpenFlag);
	m_button_all_select.EnableWindow(OpenFlag);
	m_edit_trigger_low.EnableWindow(OpenFlag);
	m_edit_trigger_high.EnableWindow(OpenFlag);
	m_edit_wave_num.EnableWindow(OpenFlag);
	m_button_select_savepath.EnableWindow(OpenFlag);

	if (samplingmode == TRUE)//Manual mode
	{
		m_button_mode_manual.EnableWindow(OpenFlag);
		m_button_mode_auto.EnableWindow(OpenFlag);
	}
	else//External trigger
	{
	}
}


void Dialog1_Main::OnEnChangeEditCh1GainMultip3()
{
	CheckGain3andDisply(&packetConfig, 0);
}


void Dialog1_Main::OnEnChangeEditCh2GainMultip3()
{
	CheckGain3andDisply(&packetConfig, 1);
}


void Dialog1_Main::OnEnChangeEditCh3GainMultip3()
{
	CheckGain3andDisply(&packetConfig, 2);
}


void Dialog1_Main::OnEnChangeEditCh4GainMultip3()
{
	CheckGain3andDisply(&packetConfig, 3);
}


void Dialog1_Main::OnEnChangeEditCh5GainMultip3()
{
	CheckGain3andDisply(&packetConfig, 4);
}


void Dialog1_Main::OnEnChangeEditCh6GainMultip3()
{
	CheckGain3andDisply(&packetConfig, 5);
}


void Dialog1_Main::OnEnChangeEditCh7GainMultip3()
{
	CheckGain3andDisply(&packetConfig, 6);
}


void Dialog1_Main::OnEnChangeEditCh8GainMultip3()
{
	CheckGain3andDisply(&packetConfig, 7);
}

void Dialog1_Main::OnEnChangeEditSavepath()
{
	// Heavy path validation (existence/writability probe) is deferred
	// to confirmation actions such as startup, folder selection, and parameter apply.
	// Keep this branch so policy can enable text-changed validation later
	// without changing message-map wiring.
	if (SavePathValidation::ShouldValidateForUiTrigger(SavePathValidation::UiValidationTrigger::kTextChanged))
	{
		ValidateSavePathUI(FALSE);
	}
}

void Dialog1_Main::CheckGain3andDisply(FPGAConfigI_REGMAP* Config, INT index)
{
	CString strValue;
	//CString strTmp;

	editMultp3GainCh[index]->GetWindowTextW(strValue);
	double gain3value = _tstof(strValue);

	if ((gain3value < -1.0) || (gain3value > -0.5))
	{
		//strTmp.Format(_T("CH[%d] gain3 = %.1f is not in range(-1.0 ~ -0.5)"), index + 1, gain3value);
		//m_pMainDlg->PrintLog(strTmp);

		EditCtrl_HighLight(editMultp3GainCh[index], TRUE);
	}
	else
	{
		EditCtrl_HighLight(editMultp3GainCh[index], FALSE);

		packetConfig.GainCh[index][2] = gain3value;

		UpdateTotalGain(index);
	}
}
