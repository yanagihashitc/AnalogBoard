// Dialog2_Debug.cpp : implementation file
//

#include "pch.h"
#include "AnalogBoard_TestApp.h"
#include "afxdialogex.h"
#include "Dialog2_Debug.h"
#include "AnalogBoard_TestAppDlg.h"
#include "locale.h"

//#include "../AnalogBoard_Dll/AnalogBoard_Dll.h"
// Dialog2_Debug dialog

IMPLEMENT_DYNAMIC(Dialog2_Debug, CDialogEx)

Dialog2_Debug::Dialog2_Debug(CAnalogBoardTestAppDlg* pParent /*=nullptr*/)
	: CDialogEx(IDD_DIALOG2_DEBUG, pParent), m_pMainDlg(pParent)
	, m_SaveBinFile_EP6(_T(""))
{
	g_iSelectedRow = 0;
	g_iSelectedColumn = 0;
}

//Dialog2_Debug::~Dialog2_Debug()
//{
//}

void Dialog2_Debug::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST_EP2EP4_DATA, m_EP2EP4DataList);
	DDX_Control(pDX, IDC_EDIT_DATAVIEWER_LIST, m_EditDataViewerList);
	DDX_Text(pDX, IDC_EDIT_IMPORT_EP2, m_ImportFile_EP2);
	DDX_Text(pDX, IDC_EDIT_EXPORT_EP4, m_ExportFile_EP4);
	DDX_Text(pDX, IDC_EDIT_SAVEFILE_EP6RX, m_SaveBinFile_EP6);
	DDX_Control(pDX, IDC_EDIT_EP6_READSIZE, m_edit_ReadSize);
}


// Windows notification macros use negative constants by design.
#pragma warning(push)
#pragma warning(disable : 26454)
BEGIN_MESSAGE_MAP(Dialog2_Debug, CDialogEx)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_LIST_EP2EP4_DATA, &Dialog2_Debug::OnNMCustomdrawListEp2ep4Data)
	ON_NOTIFY(NM_DBLCLK, IDC_LIST_EP2EP4_DATA, &Dialog2_Debug::OnNMDblclkListEp2ep4Data)
	ON_EN_KILLFOCUS(IDC_EDIT_DATAVIEWER_LIST, &Dialog2_Debug::OnEnKillfocusEditDataviewerList)
	ON_BN_CLICKED(IDC_BUTTON_IMPORT_EP2, &Dialog2_Debug::OnBnClickedButtonImportEp2)
	ON_BN_CLICKED(IDC_BUTTON_EXPORT_EP4, &Dialog2_Debug::OnBnClickedButtonExportEp4)
	ON_BN_CLICKED(IDC_BUTTON_EP4RX, &Dialog2_Debug::OnBnClickedButtonEp4rx)
	ON_BN_CLICKED(IDC_BUTTON_IMPORTEP2_SELECT, &Dialog2_Debug::OnBnClickedButtonImportep2Select)
	ON_BN_CLICKED(IDC_BUTTON_EXPORTEP4_SELECT, &Dialog2_Debug::OnBnClickedButtonExportep4Select)
	ON_BN_CLICKED(IDC_BUTTON_EP2TX, &Dialog2_Debug::OnBnClickedButtonEp2tx)
	ON_BN_CLICKED(IDC_BUTTON_EXPORTEP6_SELECT, &Dialog2_Debug::OnBnClickedButtonExportep6Select)
	ON_BN_CLICKED(IDC_BUTTON_EP6RX, &Dialog2_Debug::OnBnClickedButtonEp6rx)
END_MESSAGE_MAP()
#pragma warning(pop)


// Dialog2_Debug message handlers


BOOL Dialog2_Debug::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// TODO:  Add extra initialization here
	/* EP2/4 data list set */
	CRect rectTemp;
	GetDlgItem(IDC_LIST_EP2EP4_DATA)->GetWindowRect(rectTemp);
	int iWidth = (rectTemp.Width()) / 3;
	m_EP2EP4DataList.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
	m_EP2EP4DataList.InsertColumn(E_ADDRESS, _T("Address"), LVCFMT_LEFT, iWidth);
	m_EP2EP4DataList.InsertColumn(E_EP2DATA, _T("EP2"), LVCFMT_LEFT, iWidth);
	m_EP2EP4DataList.InsertColumn(E_EP4DATA, _T("EP4"), LVCFMT_LEFT, iWidth);

	CString strFpgaAddr;
	UINT	uiStartAddr = 0x0000;
	INT		iRegIndex = 0;

	for (iRegIndex = 0; iRegIndex < REG_MAX_COUNT; iRegIndex++)
	{
		strFpgaAddr.Format(_T("0x%06X"), uiStartAddr + (iRegIndex * 2));
		m_EP2EP4DataList.InsertItem(iRegIndex, strFpgaAddr);
		m_EP2EP4DataList.SetItemText(iRegIndex, 1, _T("--"));
	}

	//m_EditDataViewerList is is hide
	m_EditDataViewerList.ShowWindow(SW_HIDE);

	return TRUE;  // return TRUE unless you set the focus to a control
	// EXCEPTION: OCX Property Pages should return FALSE
}


void Dialog2_Debug::OnNMCustomdrawListEp2ep4Data(NMHDR* pNMHDR, LRESULT* pResult)
{
	//LPNMCUSTOMDRAW pNMCD = reinterpret_cast<LPNMCUSTOMDRAW>(pNMHDR);
	NMLVCUSTOMDRAW* pLVCD = reinterpret_cast<NMLVCUSTOMDRAW*>(pNMHDR);
	// TODO: Add your control notification handler code here

	switch (pLVCD->nmcd.dwDrawStage)
	{
	case CDDS_PREPAINT:
		*pResult = CDRF_NOTIFYITEMDRAW;
		return;
	case CDDS_ITEMPREPAINT:
		*pResult = CDRF_NOTIFYSUBITEMDRAW;
		return;
	case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
	{
		COLORREF crText, crBkgnd;
		int row = (int)pLVCD->nmcd.dwItemSpec;
		int col = pLVCD->iSubItem;
		if (1 == row % 2)
		{
			crText = RGB(0, 0, 0);
			crBkgnd = RGB(198, 216, 240);
		}
		else
		{
			crText = RGB(0, 0, 0);
			crBkgnd = RGB(255, 255, 255);
		}
		pLVCD->clrText = crText;
		pLVCD->clrTextBk = crBkgnd;
	}
	break;
	}

	*pResult = 0;
}


void Dialog2_Debug::OnNMDblclkListEp2ep4Data(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMITEMACTIVATE pNMItemActivate = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
	// TODO: Add your control notification handler code here
	*pResult = 0;

	NM_LISTVIEW* pNMListView = (NM_LISTVIEW*)pNMHDR;
	CRect rc;

	g_iSelectedRow = pNMListView->iItem;//Selected row
	g_iSelectedColumn = pNMListView->iSubItem;//Selectd column

	if (pNMListView->iSubItem == 1)
	{
		m_EP2EP4DataList.GetSubItemRect(g_iSelectedRow, g_iSelectedColumn, LVIR_LABEL, rc);
		m_EditDataViewerList.SetParent(&m_EP2EP4DataList);
		m_EditDataViewerList.MoveWindow(rc);
		m_EditDataViewerList.SetWindowText(m_EP2EP4DataList.GetItemText(g_iSelectedRow, g_iSelectedColumn));
		m_EditDataViewerList.ShowWindow(SW_SHOW);
		m_EditDataViewerList.SetFocus();
		m_EditDataViewerList.ShowCaret();
		m_EditDataViewerList.SetSel(-1);
	}
}

void Dialog2_Debug::OnEnKillfocusEditDataviewerList()
{
	// TODO: Add your control notification handler code here
	/* Updata the new value into data list */
	CString str;
	m_EditDataViewerList.GetWindowText(str);
	m_EP2EP4DataList.SetItemText(g_iSelectedRow, g_iSelectedColumn, str);
	m_EditDataViewerList.ShowWindow(SW_HIDE);
}

void Dialog2_Debug::DataViewerQuickFocus(INT RowIndex)
{
	m_EP2EP4DataList.SetItemState(-1, 0, LVIS_SELECTED);//Cancle all selected state
	m_EP2EP4DataList.SetItemState(RowIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	m_EP2EP4DataList.EnsureVisible(RowIndex, FALSE);
	m_EP2EP4DataList.SetFocus();
}

void Dialog2_Debug::OnBnClickedButtonImportEp2()
{
	CStdioFile	File;
	CString		LineData;
	CString		strAddress;
	CString		strData;
	CString		strFpgaAddr;
	UINT		uiStartAddr = 0;
	INT			iRegIndex = 0;

	if (m_ImportFile_EP2.IsEmpty())
	{
		MessageBox(_T("Please select import file!"));
		return;
	}

	if (!File.Open(m_ImportFile_EP2, CFile::modeRead))
	{
		MessageBox(_T("Failed to open EP2 data file!"));
		return;
	}

	File.ReadString(LineData);

	if (LineData != "Reg Address,Value")
	{
		MessageBox(_T("First line error!"));
		File.Close();
		return;
	}

	uiStartAddr = 0x000000;
	for (iRegIndex = 0; iRegIndex < REG_MAX_COUNT; iRegIndex++)
	{
		File.ReadString(LineData);
		strAddress = LineData.Mid(2, 6);
		strData = LineData.Right(LineData.GetLength() - 11);

		strFpgaAddr.Format(_T("%06X"), uiStartAddr + iRegIndex * 2);
		if (strAddress != strFpgaAddr)
		{
			DataViewerQuickFocus(iRegIndex);//Focus the error line
			MessageBox(_T("Import file format error!"));
			File.Close();
			return;
		}
		m_EP2EP4DataList.SetItemText(iRegIndex, 0, _T("0x") + strAddress.MakeUpper());
		m_EP2EP4DataList.SetItemText(iRegIndex, 1, strData.MakeUpper());
	}

	m_pMainDlg->PrintLog(_T("EP2 data import success!"));
	File.Close();
}


void Dialog2_Debug::OnBnClickedButtonExportEp4()
{
	CStdioFile	File;
	CString		strFpgaAddr;
	CString		strData;
	INT			iRegIndex = 0;

	if (m_ExportFile_EP4.IsEmpty())
	{
		MessageBox(_T("Please select save file path!"));
		return;
	}

	if (!File.Open(m_ExportFile_EP4, CFile::modeWrite | CFile::modeCreate))
	{
		MessageBox(_T("Failed to open file!"));
		return;
	}

	File.WriteString(_T("Reg Address,Value\n"));

	for (iRegIndex = 0; iRegIndex < REG_MAX_COUNT; iRegIndex++)
	{
		strFpgaAddr = m_EP2EP4DataList.GetItemText(iRegIndex, 0);
		strData = m_EP2EP4DataList.GetItemText(iRegIndex, 2);

		File.WriteString(strFpgaAddr + _T(",0x") + strData + _T("\n"));
	}

	File.Close();
	m_pMainDlg->PrintLog(_T("EP4 data save success!"));
}

void Dialog2_Debug::OnBnClickedButtonImportep2Select()
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
		m_ImportFile_EP2 = fileDlg.GetPathName();
	}

	UpdateData(FALSE);
}

void Dialog2_Debug::OnBnClickedButtonExportep4Select()
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
		m_ExportFile_EP4 = fileDlg.GetPathName();
	}

	UpdateData(FALSE);
}

void Dialog2_Debug::OnBnClickedButtonEp2tx()
{
	PBYTE	pWriteBufSrc = NULL;//Ep2 send buffer
	CString strRegData = 0;
	BYTE	ucRegData_8H = 0;
	USHORT	usRegData_16H = 0;
	INT		iRegIndex = 0;
	INT		iRet = USB_SUCCESS;
	double Interval = 0.0;
	CString msg;
	LARGE_INTEGER tFreq, tStart, tEnd;
	QueryPerformanceFrequency(&tFreq);

	/* Check Ep2 data import */
	if (m_EP2EP4DataList.GetItemText(0, 1) == _T("--"))
	{
		MessageBox(_T("Please import EP2 data!"));
		return;
	}

	/* Generate Ep2 send data buffer */
	pWriteBufSrc = (PBYTE)malloc(EP2_DATA_BUFF_SIZE);
	if (!pWriteBufSrc)
	{
		m_pMainDlg->PrintLog(_T("Buf alloc failed!"));
		return;
	}
	
	/* Get register data into buffer */
	for (iRegIndex = 0; iRegIndex < REG_MAX_COUNT; iRegIndex++)
	{
		strRegData = m_EP2EP4DataList.GetItemText(iRegIndex, 1);
		if (strRegData == _T("--"))
		{
			*(pWriteBufSrc + ((size_t)iRegIndex * 2)) = 0x00;
			*(pWriteBufSrc + ((size_t)iRegIndex * 2) + 1) = 0x00;
			continue;
		}

		usRegData_16H = (USHORT)_tcstoul(strRegData, NULL, 16);
		*(pWriteBufSrc + ((size_t)iRegIndex * 2)) = usRegData_16H & 0xFF;
		*(pWriteBufSrc + ((size_t)iRegIndex * 2) + 1) = usRegData_16H >> 8;		
	}

	QueryPerformanceCounter(&tStart);

	/* Send data buffer by Ep2 */
	iRet = m_pMainDlg->UsbLibInfo.EP2_SendData(pWriteBufSrc);

	QueryPerformanceCounter(&tEnd);

	if (iRet == USB_SUCCESS)
	{
		m_pMainDlg->PrintLog(_T("EP2 Send success!"));
		Interval = (1.0 / tFreq.QuadPart)*(tEnd.QuadPart - tStart.QuadPart);
		msg.Format(_T("Spend time: %f s"), Interval);
		m_pMainDlg->PrintLog(msg);
	}
	else
	{
		m_pMainDlg->PrintLog(_T("EP2 Send failed!"));
		m_pMainDlg->PrintLog(m_pMainDlg->strUSBLibError(iRet));
	}

	free(pWriteBufSrc);
	pWriteBufSrc = NULL;
}

void Dialog2_Debug::OnBnClickedButtonEp4rx()
{
	INT		iRet = USB_SUCCESS;
	INT		iRegIndex = 0;
	USHORT	usRegData_16H = 0;
	PBYTE	pReadBuf = NULL;//Ep4 data buffer
	CString strRegData;
	double Interval = 0.0;
	CString msg;
	LARGE_INTEGER tFreq, tStart, tEnd;
	QueryPerformanceFrequency(&tFreq);

	pReadBuf = (PBYTE)malloc(EP4_DATA_NODUMMY_SIZE);
	if (!pReadBuf)
	{
		m_pMainDlg->PrintLog(_T("Buf alloc failed!"));
		return;
	}

	QueryPerformanceCounter(&tStart);

	/* Get data from Ep4 */
	iRet = m_pMainDlg->UsbLibInfo.EP4_GetData(pReadBuf);

	QueryPerformanceCounter(&tEnd);

	if (iRet == USB_SUCCESS)
	{
		m_pMainDlg->PrintLog(_T("Get EP4 data success!"));
		Interval = (1.0 / tFreq.QuadPart)*(tEnd.QuadPart - tStart.QuadPart);
		msg.Format(_T("Spend time: %f s"), Interval);
		m_pMainDlg->PrintLog(msg);

		/* Display EP4 data in Data viewer */
		for (iRegIndex = 0; iRegIndex < REG_MAX_COUNT; iRegIndex++)
		{
			usRegData_16H = *(pReadBuf + ((size_t)iRegIndex * 2) + 1);
			usRegData_16H = (usRegData_16H << 8) | *(pReadBuf + ((size_t)iRegIndex * 2));
			strRegData.Format(_T("%04X"), usRegData_16H);
			m_EP2EP4DataList.SetItemText(iRegIndex, 2, strRegData);
		}
	}
	else
	{
		m_pMainDlg->PrintLog(_T("Get EP4 data failed!"));
		m_pMainDlg->PrintLog(m_pMainDlg->strUSBLibError(iRet));
	}

	free(pReadBuf);
	pReadBuf = NULL;
}


BOOL Dialog2_Debug::PreTranslateMessage(MSG* pMsg)
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


void Dialog2_Debug::OnBnClickedButtonExportep6Select()
{
	CString	strDefExt;
	CString	strFilter;

	UpdateData(TRUE);

	strDefExt = _T("bin");
	strFilter = _T("File(*.BIN)|*.bin||");

	UpdateData(TRUE);

	CFileDialog fileDlg(FALSE, strDefExt, NULL, NULL, strFilter);

	if (IDOK == fileDlg.DoModal())
	{
		m_SaveBinFile_EP6 = fileDlg.GetPathName();
	}

	UpdateData(FALSE);
}


void Dialog2_Debug::OnBnClickedButtonEp6rx()
{
	CString StrTemp;
	INT iRet = 0;
	INT iReadSize = 0;
	PBYTE pEp6DataBuf = NULL;
	CFileStatus status;
	CFile SaveFile;

	/* Get read size */
	m_edit_ReadSize.GetWindowTextW(StrTemp);
	iReadSize = (UINT)_tcstoul(StrTemp, NULL, 16);

	/* Initial ep6 read buffer */
	pEp6DataBuf = (PBYTE)malloc(iReadSize);
	if (!pEp6DataBuf)
	{
		m_pMainDlg->PrintLog(_T("EP6 buf alloc failed!"));
		return;
	}
	else
	{
		memset(pEp6DataBuf, 0x00, iReadSize);
	}

	do
	{
		/* Open bin file */
		if (!SaveFile.Open(m_SaveBinFile_EP6, CFile::modeCreate | CFile::modeWrite))
		{
			m_pMainDlg->PrintLog(_T("File open failed!"));
			break;
		}
		else
		{
			/* Get ddr data by EP6 */
			iRet = m_pMainDlg->UsbLibInfo.EP6_GetData(pEp6DataBuf, iReadSize);
			if (iRet == USB_SUCCESS)
			{
				m_pMainDlg->PrintLog(_T("Get EP6 data success!"));

				/* Save data to bin file */
				SaveFile.Write(pEp6DataBuf, iReadSize);
			}
			else
			{
				m_pMainDlg->PrintLog(_T("Get EP6 data failed!"));
				m_pMainDlg->PrintLog(m_pMainDlg->strUSBLibError(iRet));
			}

			SaveFile.Close();
		}
	} while (0);

	if (pEp6DataBuf)
	{
		free(pEp6DataBuf);
		pEp6DataBuf = NULL;
	}
}
