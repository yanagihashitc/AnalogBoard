/*******************************************************************************
* Copyright (C) PHINE Design, Ltd. 2024
* All rights reserved.
*
* File name		:	AnalogBoard_Dll.c
* File summary	:	USB3.0 function
*******************************************************************************/

/*******************************************************************************
* include file
*******************************************************************************/
#include "pch.h"
#include <windef.h>
#include <windows.h>
#include <stdio.h>
#include <winioctl.h>
#include <tchar.h>
#include <new>
#include <cstdint>
#include "Ep6TransferRetryPolicy.h"
#include "UsbEndpointDiscoveryPolicy.h"
#include "AnalogBoard_Dll.h"
#include "..\CyLib\header\CyAPI.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define CHECK_DATAHEADER 1
using namespace std;

/* Global variable */
const char* g_DLLVersion = "1.0.0";

/* {A123DFB8-6F1E-49F4-93F4-C4E4B48FD5BD} */
static GUID USBDRV_GUID = { 0xA123DFB8, 0x6F1E, 0x49F4, 0x93, 0xF4, 0xC4, 0xE4, 0xB4, 0x8F, 0xD5, 0xBD };

/* The USB Device Class */
static CCyUSBDevice* m_pUSBDevice;

/* Endpoint */
static CCyUSBEndPoint* m_pOutEndpt2;
static CCyUSBEndPoint* m_pInEndpt4;
static CCyUSBEndPoint* m_pInEndpt6;
static Ep4FailureDiagnostic::SingleRecordBuffer g_ep4FailureDiagnostic;

/* EP2/4 Mutec */
HANDLE m_hEP2EP4Mutex;
// Diagnostic counters for EP6 debug logging. They are reset on connect/disconnect.
static LONG g_ep6CallCount = 0;
static LONG g_ep6TimeoutCount = 0;
static LONG g_ep6RetryCount = 0;

static void ResetEp6DiagnosticCounters()
{
	::InterlockedExchange(&g_ep6CallCount, 0);
	::InterlockedExchange(&g_ep6TimeoutCount, 0);
	::InterlockedExchange(&g_ep6RetryCount, 0);
}

static void LogUsbConnectDiagnostic(
	const char* event,
	INT deviceIndex,
	INT altIndex,
	INT endpointIndex,
	UCHAR address,
	UCHAR attributes,
	const UsbEndpointDiscoveryPolicy::EndpointDiscoveryState* endpoints,
	INT result,
	DWORD win32LastError)
{
	UsbEndpointDiscoveryPolicy::ConnectDiagnostic diagnostic;
	diagnostic.event = event;
	diagnostic.deviceIndex = deviceIndex;
	diagnostic.altIndex = altIndex;
	diagnostic.endpointIndex = endpointIndex;
	diagnostic.address = address;
	diagnostic.attributes = attributes;
	diagnostic.hasEp2 = endpoints != nullptr && endpoints->HasEp2();
	diagnostic.hasEp4 = endpoints != nullptr && endpoints->HasEp4();
	diagnostic.hasEp6 = endpoints != nullptr && endpoints->HasEp6();
	diagnostic.result = result;
	if (m_pUSBDevice != nullptr)
	{
		diagnostic.usbdStatus = static_cast<std::uint32_t>(m_pUSBDevice->UsbdStatus);
		diagnostic.ntStatus = static_cast<std::uint32_t>(m_pUSBDevice->NtStatus);
		diagnostic.lastError = static_cast<std::uint32_t>(m_pUSBDevice->LastError);
	}
	diagnostic.win32LastError = static_cast<std::uint32_t>(win32LastError);

	char line[512] = { 0 };
	if (UsbEndpointDiscoveryPolicy::FormatConnectDiagnostic(diagnostic, line, sizeof(line)))
	{
		::OutputDebugStringA(line);
	}
}

/*******************************************************************************
* function define
*******************************************************************************/
USB_Lib_Info::USB_Lib_Info(void)
{
	m_pUSBDevice = NULL;
	m_pOutEndpt2 = NULL;
	m_pInEndpt4 = NULL;
	m_pInEndpt6 = NULL;
	m_hEP2EP4Mutex = CreateMutex(NULL, FALSE, NULL);
	isConnected = FALSE;
}

USB_Lib_Info::~USB_Lib_Info(void)
{
	if (m_pUSBDevice)
	{
		if (m_pUSBDevice->DeviceCount() != 0)
		{
			/* Close device */
			m_pUSBDevice->Close();

			if (m_pUSBDevice->bSuperSpeed && m_pUSBDevice->UsbBos)
			{
				free(m_pUSBDevice->UsbBos->pContainer_ID);
				free(m_pUSBDevice->UsbBos->pUSB20_DeviceExt);
				free(m_pUSBDevice->UsbBos->pSS_DeviceCap);
				free(m_pUSBDevice->UsbBos);
			}
		}

		delete m_pUSBDevice;
		m_pOutEndpt2 = NULL;
		m_pInEndpt4 = NULL;
		m_pInEndpt6 = NULL;
	}

	if (m_hEP2EP4Mutex != NULL)
	{
		CloseHandle(m_hEP2EP4Mutex);
		m_hEP2EP4Mutex = NULL;
	}
}

/*******************************************************************************
*Function prototype :   INT USBBoard_Connect(HWND Hwd)
*Function summary   :   Connect the USB board
*arguments          :   HWND Hwd		//GUI handle
*return value       :   USB_SUCCESS					//Board connect success
*						USB_ERR_ALLOCMEM_FAILED		//The USB Device Class pointer alloct failed
*						USS_ERR_NULLPOINTER			//The USB Device Class pointer is null
*						USB_ERR_NODEV				//Can not find device
*						USB_ERR_OPENDEV_FAILED		//Open device failed
*						USS_ERR_SETINTERFACE_FAILED	//Set interface failed
*						USS_ERR_INVALID_ENDPOINTER	//The endpoint is invalid
*						USB_ERR_VENDOR_ID_ERR		//Vendor error
*						USB_ERR_PRODUCT_ID_ERR		//Product error
*						USB_DEV_USB20				//The device is USB2.0
*******************************************************************************/
INT USB_Lib_Info::USBBoard_Connect(HWND Hwd)
{
	INT iIntfcCount = 0;	//The interface number
	INT iEptCount = 0;		//The EndPoint number
	INT iDevIndex = 0;		//The device index 
	INT iIntfcIndex = 0;	//The interface index 
	INT iEndPointIndex = 0;	//The EndPoint index
	INT iDevNum = 0;		//The device num
	CCyUSBEndPoint* pEpt = NULL;
	bool endpointResolved = false;

	if (!Hwd)
	{
		/* NULL pointer */
		return USB_ERR_NULLPOINTER;
	}

	try
	{
		/* CCyUSBDevice *m_pUSBDevice */
		m_pUSBDevice = new CCyUSBDevice(Hwd, CYUSBDRV_GUID, false);
	}
	catch (const bad_alloc&)
	{
		/* Memory alloct failed */
		return USB_ERR_ALLOCMEM_FAILED;
	}

	if (!m_pUSBDevice)
	{
		/* NULL pointer */
		return USB_ERR_NULLPOINTER;
	}

	/* get num of usb device */
	iDevNum = m_pUSBDevice->DeviceCount();

	if (iDevNum <= 0)
	{
		return USB_ERR_NODEV;
	}

	for (INT i = 0; i < iDevNum; i++)
	{
		if (!m_pUSBDevice->Open(i))		//open device
		{
			m_pUSBDevice->Reset();
			if (!m_pUSBDevice->Open(i))//Retry if failed to open
			{
				if (i + 1 == iDevNum)
				{
					return USB_ERR_OPENDEV_FAILED;
				}
				continue;//check next device
			}
		}

		if (m_pUSBDevice->VendorID != CYPRESS_USBDEVICE_VID)
		{
			return USB_ERR_VENDOR_ID_ERR;//VID error
		}
		else if ((m_pUSBDevice->ProductID != CYPRESS_USBDEVICE_PID_30) && (m_pUSBDevice->ProductID != CYPRESS_USBDEVICE_PID_20))
		{
			return USB_ERR_PRODUCT_ID_ERR;//PID error
		}
		else if ((m_pUSBDevice->ProductID == CYPRESS_USBDEVICE_PID_30) && (m_pUSBDevice->BcdUSB != 0x300))//PID 1010 -> Not USB3.0
		{
			return USB_ERR_PRODUCT_ID_ERR;//PID error
		}
		else if ((m_pUSBDevice->ProductID == CYPRESS_USBDEVICE_PID_20) && (m_pUSBDevice->BcdUSB != 0x200))//PID 1012 -> Not USB2.0
		{
			return USB_ERR_PRODUCT_ID_ERR;//PID error
		}

		/* Returns the number of alternate interfaces exposed by the device */
		iIntfcCount = m_pUSBDevice->AltIntfcCount() + 1;
		UsbEndpointDiscoveryPolicy::AltEndpointSelectionState endpointSelection;

		for (iIntfcIndex = 0; iIntfcIndex < iIntfcCount; iIntfcIndex++)
		{
			/* Set the active interface of the device to alt */
			if (m_pUSBDevice->SetAltIntfc(iIntfcIndex) != TRUE)
			{
				const DWORD setAltLastError = ::GetLastError();
				LogUsbConnectDiagnostic(
					"set_alt_failed",
					i,
					iIntfcIndex,
					-1,
					0u,
					0u,
					nullptr,
					USB_ERR_SETINTERFACE_FAILED,
					setAltLastError);
				if (iIntfcIndex + 1 == iIntfcCount)
				{
					return USB_ERR_SETINTERFACE_FAILED;
				}
				continue;
			}

			/* Returns the number of endpoints exposed by the currently selected interface (or Alternate Interface) plus 1 */
			iEptCount = m_pUSBDevice->EndPointCount();
			UsbEndpointDiscoveryPolicy::EndpointDiscoveryState endpointState;

			/* Fill the EndPointsBox */
			for (iEndPointIndex = 1; iEndPointIndex < iEptCount; iEndPointIndex++)
			{
				//CCyUSBEndPoint *pEpt = m_pUSBDevice->EndPoints[iEndPointIndex];
				pEpt = m_pUSBDevice->EndPoints[iEndPointIndex];

				if (!pEpt)
				{
					continue;
				}

				endpointState.VisitEndpoint(
					reinterpret_cast<std::uintptr_t>(pEpt),
					static_cast<std::uint8_t>(pEpt->Address),
					static_cast<std::uint8_t>(pEpt->Attributes));
				LogUsbConnectDiagnostic(
					"endpoint",
					i,
					iIntfcIndex,
					iEndPointIndex,
					pEpt->Address,
					pEpt->Attributes,
					&endpointState,
					USB_SUCCESS,
					ERROR_SUCCESS);
			}

			LogUsbConnectDiagnostic(
				"alt_rollup",
				i,
				iIntfcIndex,
				-1,
				0u,
				0u,
				&endpointState,
				endpointState.IsComplete() ? USB_SUCCESS : USB_ERR_INVALID_ENDPOINTER,
				ERROR_SUCCESS);

			if (endpointState.IsComplete())
			{
				endpointSelection.ConsiderAlt(iIntfcIndex, endpointState);
			}
		}

		if (endpointSelection.HasSelection())
		{
			if (m_pUSBDevice->SetAltIntfc(endpointSelection.SelectedAltIndex()) != TRUE)
			{
				const DWORD setSelectedAltLastError = ::GetLastError();
				LogUsbConnectDiagnostic(
					"set_selected_alt_failed",
					i,
					endpointSelection.SelectedAltIndex(),
					-1,
					0u,
					0u,
					nullptr,
					USB_ERR_SETINTERFACE_FAILED,
					setSelectedAltLastError);
				return USB_ERR_SETINTERFACE_FAILED;
			}

			UsbEndpointDiscoveryPolicy::EndpointDiscoveryState selectedEndpointState;
			iEptCount = m_pUSBDevice->EndPointCount();
			for (iEndPointIndex = 1; iEndPointIndex < iEptCount; iEndPointIndex++)
			{
				pEpt = m_pUSBDevice->EndPoints[iEndPointIndex];

				if (!pEpt)
				{
					continue;
				}

				selectedEndpointState.VisitEndpoint(
					reinterpret_cast<std::uintptr_t>(pEpt),
					static_cast<std::uint8_t>(pEpt->Address),
					static_cast<std::uint8_t>(pEpt->Attributes));
				LogUsbConnectDiagnostic(
					"selected_endpoint",
					i,
					endpointSelection.SelectedAltIndex(),
					iEndPointIndex,
					pEpt->Address,
					pEpt->Attributes,
					&selectedEndpointState,
					USB_SUCCESS,
					ERROR_SUCCESS);
			}

			if (!selectedEndpointState.IsComplete())
			{
				LogUsbConnectDiagnostic(
					"selected_alt_incomplete",
					i,
					endpointSelection.SelectedAltIndex(),
					-1,
					0u,
					0u,
					&selectedEndpointState,
					USB_ERR_INVALID_ENDPOINTER,
					ERROR_SUCCESS);
				return USB_ERR_INVALID_ENDPOINTER;
			}

			m_pOutEndpt2 = reinterpret_cast<CCyUSBEndPoint*>(selectedEndpointState.Ep2Token());
			m_pInEndpt4 = reinterpret_cast<CCyUSBEndPoint*>(selectedEndpointState.Ep4Token());
			m_pInEndpt6 = reinterpret_cast<CCyUSBEndPoint*>(selectedEndpointState.Ep6Token());
			LogUsbConnectDiagnostic(
				"connect_success",
				i,
				endpointSelection.SelectedAltIndex(),
				-1,
				0u,
				0u,
				&selectedEndpointState,
				USB_SUCCESS,
				ERROR_SUCCESS);
			endpointResolved = true;
			break;
		}
	}

	if (!endpointResolved)
	{
		LogUsbConnectDiagnostic(
			"no_complete_alt",
			iDevNum - 1,
			-1,
			-1,
			0u,
			0u,
			nullptr,
			USB_ERR_INVALID_ENDPOINTER,
			ERROR_SUCCESS);
		m_pOutEndpt2 = NULL;
		m_pInEndpt4 = NULL;
		m_pInEndpt6 = NULL;
		return USB_ERR_INVALID_ENDPOINTER;
	}

	g_ep4FailureDiagnostic.Reset();
	isConnected = TRUE;
	ResetEp6DiagnosticCounters();

	if (m_pUSBDevice->BcdUSB == 0x200)
	{
		return USB_DEV_USB20;
	}

	return USB_SUCCESS;
}

/*******************************************************************************
*Function prototype :   void USBBoard_Disconnect(void)
*Function summary   :   Disonnect the USB board
*arguments          :   void
*return value       :   None
*******************************************************************************/
void USB_Lib_Info::USBBoard_Disconnect(void)
{
	if (m_pUSBDevice)
	{
		if (m_pUSBDevice->DeviceCount() != 0)
		{
			/* Close device */
			m_pUSBDevice->Close();

			if (m_pUSBDevice->bSuperSpeed && m_pUSBDevice->UsbBos)
			{
				free(m_pUSBDevice->UsbBos->pContainer_ID);
				free(m_pUSBDevice->UsbBos->pUSB20_DeviceExt);
				free(m_pUSBDevice->UsbBos->pSS_DeviceCap);
				free(m_pUSBDevice->UsbBos);
			}
		}

		delete m_pUSBDevice;
		m_pUSBDevice = NULL;
		m_pOutEndpt2 = NULL;
		m_pInEndpt4 = NULL;
		m_pInEndpt6 = NULL;
		isConnected = FALSE;
		ResetEp6DiagnosticCounters();
	}
}

/*******************************************************************************
*Function prototype :   INT EP2_SendData(BYTE *pSendData)
*Function summary   :   Send EP2 data
*arguments          :   BYTE *pSendData				//The buffer for sending
*return value       :   USB_SUCCESS					//Send data success
*						USS_ERR_INVALID_ENDPOINTER	//The endpoint is invalid
*						USB_ERR_PARAM				//Send buffer is invalid
*						USB_ERR_TRANSFER_TIMEOUT	//Usb transfer timeout
* *						USB_ERR_UNAVAILABLE			//EP4 is running
*******************************************************************************/
INT USB_Lib_Info::EP2_SendData(BYTE* pSendData)
{
	INT		iRet = USB_SUCCESS;
	LONG	lOneTimeSize = EP2_DATA_BUFF_SIZE;//EP2 send size
	OVERLAPPED outOvLap;//The structure contains information used in asynchronous input and output (I/O)

	/* Endpoint is null or not */
	if (!m_pOutEndpt2)
	{
		return USB_ERR_INVALID_ENDPOINTER;
	}

	if (!pSendData)
	{
		return USB_ERR_PARAM;
	}
	else if (_msize(pSendData) < EP2_DATA_BUFF_SIZE)
	{
		return USB_ERR_PARAM;//Send buffer size is not enough
	}

	/* Wait for Ep4 end */
	if (WAIT_OBJECT_0 != WaitForSingleObject(m_hEP2EP4Mutex, INFINITE))
	{
		return USB_ERR_UNAVAILABLE;
	}

	outOvLap.hEvent = CreateEvent(NULL, false, false, _T("CYUSB_OUT"));

	/* Send Ep2 data packet 128*16 byte */
	if (m_pOutEndpt2->XferData(pSendData, lOneTimeSize, FALSE))
	{
		//do nothing
	}
	else
	{
		iRet = USB_ERR_TRANSFER_TIMEOUT;
	}

	ReleaseMutex(m_hEP2EP4Mutex);

	return iRet;
}

/*******************************************************************************
*Function prototype :   INT EP4_GetData(BYTE *pRevData)
*Function summary   :   Get EP4 data
*arguments          :   BYTE *pRevData				//The buffer for saving data
*return value       :   USB_SUCCESS					//Get data success
*						USS_ERR_INVALID_ENDPOINTER	//The endpoint is invalid
*						USB_ERR_PARAM				//Receive buffer is invalid
*						USB_ERR_TRANSFER_TIMEOUT	//Usb transfer timeout
* *						USB_ERR_UNAVAILABLE			//EP2 is running
*******************************************************************************/
INT USB_Lib_Info::EP4_GetData(BYTE* pRevData)
{
	INT		iRet = USB_SUCCESS;
	LONG	lOneTimeSize = EP4_DATA_BUFF_SIZE;//EP4 receive size
	const LONG requestedLength = lOneTimeSize;
	PBYTE	pOneTimeBuffer = NULL;//Pointer to the return packet
	OVERLAPPED inOvLap;//The structure contains information used in asynchronous input and output (I/O)

	/* Endpoint is null or not */
	if (!m_pInEndpt4)
	{
		return USB_ERR_INVALID_ENDPOINTER;
	}

	if (!pRevData)
	{
		return USB_ERR_PARAM;
	}
	else if (_msize(pRevData) < EP4_DATA_NODUMMY_SIZE)
	{
		return USB_ERR_PARAM;//Rev buffer size is not enough
	}

	if (WAIT_OBJECT_0 != WaitForSingleObject(m_hEP2EP4Mutex, INFINITE))
	{
		return USB_ERR_UNAVAILABLE;
	}

	inOvLap.hEvent = CreateEvent(NULL, false, false, _T("CYUSB_IN"));

	pOneTimeBuffer = (PBYTE)malloc(lOneTimeSize);
	if (!pOneTimeBuffer)
	{
		ReleaseMutex(m_hEP2EP4Mutex);
		return USB_ERR_ALLOCMEM_FAILED;
	}
	memset(pOneTimeBuffer, 0, lOneTimeSize);

	/* Receive fpga return packet 128*4 byte */
	if (m_pInEndpt4->XferData(pOneTimeBuffer, lOneTimeSize, FALSE))
	{
		//Extract read data into user buffer
		memcpy(pRevData, pOneTimeBuffer + 256, EP4_DATA_NODUMMY_SIZE);
	}
	else
	{
		const DWORD win32LastError = ::GetLastError();
		Ep4FailureDiagnostic::Record diagnostic;
		diagnostic.monotonicMs = static_cast<std::uint64_t>(::GetTickCount64());
		diagnostic.endpoint = static_cast<std::uint8_t>(m_pInEndpt4->Address);
		diagnostic.requestedLength = static_cast<std::int32_t>(requestedLength);
		diagnostic.returnedLength = static_cast<std::int32_t>(lOneTimeSize);
		diagnostic.usbdStatus = static_cast<std::uint32_t>(m_pInEndpt4->UsbdStatus);
		diagnostic.ntStatus = static_cast<std::uint32_t>(m_pInEndpt4->NtStatus);
		diagnostic.cypressLastError = static_cast<std::uint32_t>(m_pInEndpt4->LastError);
		diagnostic.win32LastError = static_cast<std::uint32_t>(win32LastError);
		g_ep4FailureDiagnostic.Capture(diagnostic);
		iRet = USB_ERR_TRANSFER_TIMEOUT;
	}

	free(pOneTimeBuffer);
	pOneTimeBuffer = NULL;

	ReleaseMutex(m_hEP2EP4Mutex);

	return iRet;
}

BOOL USB_Lib_Info::EP4_TakeFailureDiagnostic(Ep4FailureDiagnostic::Record* pRecord)
{
	if (pRecord == nullptr)
	{
		return FALSE;
	}

	if (WAIT_OBJECT_0 != WaitForSingleObject(m_hEP2EP4Mutex, INFINITE))
	{
		return FALSE;
	}

	const bool consumed = g_ep4FailureDiagnostic.Consume(pRecord);
	ReleaseMutex(m_hEP2EP4Mutex);
	return consumed ? TRUE : FALSE;
}

/*******************************************************************************
*Function prototype :   INT EP6_GetData(BYTE* pRevData, UINT  DataSizeCount)
*Function summary   :   Get EP6 data
*arguments          :   BYTE *pRevData		//The buffer for saving data
*						UINT  DataSizeCount	//USB transfer data size
*return value       :   USB_SUCCESS					//Get data success
*						USS_ERR_INVALID_ENDPOINTER	//The endpoint is invalid
*						USB_ERR_PARAM				//Receive buffer is invalid
*						USB_ERR_TRANSFER_TIMEOUT	//Usb transfer timeout
*******************************************************************************/
INT USB_Lib_Info::EP6_GetData(BYTE* pRevData, UINT  DataSizeCount)
{
	INT		iRet = USB_SUCCESS;
	LONG	lOneTimeSize = 0;
	UINT	ulRecvDataSize = 0;//EP6 receive size
	PBYTE	pOneTimeBuffer = NULL;//Pointer to the return packet
	OVERLAPPED inOvLap;//The structure contains information used in asynchronous input and output (I/O)
	const ULONGLONG callStartMs = ::GetTickCount64();
	int callRetryCount = 0;

	/* Endpoint is null or not */
	if (!m_pInEndpt6)
	{
		return USB_ERR_INVALID_ENDPOINTER;
	}

	if (!pRevData)
	{
		return USB_ERR_PARAM;
	}
	//else if (_msize(pRevData) < DataSizeCount)
	//{
	//	return USB_ERR_PARAM;//Rev buffer size is not enough
	//}

	if (WAIT_OBJECT_0 != WaitForSingleObject(m_hEP2EP4Mutex, INFINITE))
	{
		return USB_ERR_UNAVAILABLE;
	}

	inOvLap.hEvent = CreateEvent(NULL, false, false, _T("CYUSB_IN"));

	pOneTimeBuffer = (PBYTE)malloc(EP6_ONETIME_MAX_SIZE);
	if (!pOneTimeBuffer)
	{
		ReleaseMutex(m_hEP2EP4Mutex);
		return USB_ERR_ALLOCMEM_FAILED;
	}
	memset(pOneTimeBuffer, 0, EP6_ONETIME_MAX_SIZE);

	while (ulRecvDataSize < DataSizeCount)
	{
		if (DataSizeCount - ulRecvDataSize > EP6_ONETIME_MAX_SIZE)
		{
			lOneTimeSize = EP6_ONETIME_MAX_SIZE;
		}
		else
		{
			lOneTimeSize = LONG(DataSizeCount - ulRecvDataSize);
		}

		/* Receive fpga return packet */
		const LONG requestedChunkSize = lOneTimeSize;
		int chunkRetryCount = 0;
		const bool chunkSuccess = Ep6TransferRetryPolicy::ExecuteWithRetry(
			[&]() -> bool
			{
				lOneTimeSize = requestedChunkSize;
				return m_pInEndpt6->XferData(pOneTimeBuffer, lOneTimeSize, FALSE) ? true : false;
			},
			[](DWORD backoffMs)
			{
				::Sleep(backoffMs);
			},
			nullptr,
			&chunkRetryCount);
		callRetryCount += chunkRetryCount;

		if (chunkSuccess)
		{
			memcpy(pRevData + ulRecvDataSize, pOneTimeBuffer, lOneTimeSize);
		}
		else
		{
			iRet = USB_ERR_TRANSFER_TIMEOUT;
			::InterlockedIncrement(&g_ep6TimeoutCount);
			break;
		}

		ulRecvDataSize += lOneTimeSize;
	}

	free(pOneTimeBuffer);
	pOneTimeBuffer = NULL;

	ReleaseMutex(m_hEP2EP4Mutex);

	const LONG currentCallCount = ::InterlockedIncrement(&g_ep6CallCount);
	const LONG currentTimeoutCount = ::InterlockedCompareExchange(&g_ep6TimeoutCount, 0, 0);
	const LONG currentRetryCount = ::InterlockedAdd(&g_ep6RetryCount, callRetryCount);
	const ULONGLONG elapsedMs = ::GetTickCount64() - callStartMs;
	char perfLog[256] = { 0 };
	sprintf_s(
		perfLog,
		"[PR01][DLL][EP6] call=%ld requestBytes=%u recvBytes=%u elapsedMs=%llu result=%d retryCount=%d totalRetryCount=%ld timeoutCount=%ld\n",
		currentCallCount,
		DataSizeCount,
		ulRecvDataSize,
		elapsedMs,
		iRet,
		callRetryCount,
		currentRetryCount,
		currentTimeoutCount);
	::OutputDebugStringA(perfLog);

	return iRet;
}

/*******************************************************************************
*Function prototype :   const char* DllVersion_Get(void)
*Function summary   :   Get the Dll version
*arguments          :   void
*return value       :   g_DLLVersion				//The dll version
*******************************************************************************/
const char* USB_Lib_Info::DllVersion_Get(void)
{
	return g_DLLVersion;
}
