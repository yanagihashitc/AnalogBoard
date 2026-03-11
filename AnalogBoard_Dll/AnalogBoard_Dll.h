/*******************************************************************************
* Copyright (C) PHINE Design, Ltd. 2024
* All rights reserved.
*
* File name		:	AnalogBoard_Dll.h
* File summary	:	USB_Lib_Info class define
*******************************************************************************/
#ifndef _ANALOGBOARD_DLL_H_
#define _ANALOGBOARD_DLL_H_

#pragma once

#include "UsbTransferHelpers.h"

#ifdef __cplusplus
extern "C" {
#endif

/**********************************************************************************
* Global Define
**********************************************************************************/
#define CYPRESS_USBDEVICE_VID			(0x04B4)
#define CYPRESS_USBDEVICE_PID_30		(0xFFF2)
#define CYPRESS_USBDEVICE_PID_20		(0xFFF3)
#define EP2_DATA_BUFF_SIZE				128*2
#define EP4_DATA_BUFF_SIZE				128*4
#define EP6_ONETIME_MAX_SIZE			1024*1024*4
#define EP4_DATA_NODUMMY_SIZE			128*2

/* Error Code */
#define USB_SUCCESS					(0)//success
#define USB_DEV_USB20				(1)//USB speed is 2.0
#define USB_ERR_NODEV				(-1)//No USB device
#define USB_ERR_PARAM				(-2)//Invalid input parameter
#define USB_ERR_OPENDEV_FAILED		(-3)//USB device open failed
#define USB_ERR_SETINTERFACE_FAILED	(-4)//Set interface failed
#define USB_ERR_ALLOCMEM_FAILED		(-5)//Memory alloct failed
#define USB_ERR_NULLPOINTER			(-6)//NULL pointer
#define USB_ERR_INVALID_ENDPOINTER	(-7)//Invalid endpointer
#define USB_ERR_VENDOR_ID_ERR		(-8)//Invalid Vendor ID
#define USB_ERR_PRODUCT_ID_ERR		(-9)//Invalid Product ID
#define USB_ERR_TRANSFER_TIMEOUT	(-10)//USB transfer timeout
#define USB_ERR_UNAVAILABLE			(-11)//USB transfer block

/**********************************************************************************
* Class Define
**********************************************************************************/
class AFX_EXT_CLASS  USB_Lib_Info
{
public:
	USB_Lib_Info(void);
	~USB_Lib_Info(void);

	BOOL isConnected;

	/* public API */
	INT USBBoard_Connect(HWND Hwd);
	void USBBoard_Disconnect(void);
	INT EP2_SendData(BYTE* pSendData);
	INT EP4_GetData(BYTE* pRevData);
	INT EP6_GetData(BYTE* pRevData, UINT  DataSizeCount);
	const char* DllVersion_Get(void);

private:
	// Retained for binary layout compatibility with current EXE/DLL pairings.
	UsbTransferHelpers::ReusableTransferBuffer m_ep6TransferBuffer;
};

#ifdef __cplusplus
}
#endif

#endif // !_ANALOGBOARD_DLL_H_
