
// AnalogBoard_TestAppDlg.cpp : implementation file
//

#include "pch.h"
#include "framework.h"
#include "AnalogBoard_TestApp.h"
#include "AnalogBoard_TestAppDlg.h"
#include "afxdialogex.h"
#include <dbt.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CAnalogBoardTestAppDlg* pMainDlg;
static FileLogger g_fileLogger;
// CAboutDlg dialog used for App About

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg();

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

// Implementation
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()


// CAnalogBoardTestAppDlg dialog



CAnalogBoardTestAppDlg::CAnalogBoardTestAppDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_ANALOGBOARD_TESTAPP_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CAnalogBoardTestAppDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_TAB_MAIN, m_tab_main);
}

// Windows notification macros use negative constants by design.
#pragma warning(push)
#pragma warning(disable : 26454)
BEGIN_MESSAGE_MAP(CAnalogBoardTestAppDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_CLOSE()
	ON_NOTIFY(TCN_SELCHANGE, IDC_TAB_MAIN, &CAnalogBoardTestAppDlg::OnTcnSelchangeTabMain)
END_MESSAGE_MAP()
#pragma warning(pop)


// CAnalogBoardTestAppDlg message handlers

BOOL CAnalogBoardTestAppDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// Add "About..." menu item to system menu.

	// IDM_ABOUTBOX must be in the system command range.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != nullptr)
	{
		BOOL bNameValid;
		CString strAboutMenu;
		bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
		ASSERT(bNameValid);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	pMainDlg = this;
	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon

	// TODO: Add extra initialization here
	m_tab_main.InsertItem(0, _T("Data Get"));
	//m_tab_main.InsertItem(1, _T("FPGA Debug"));

	m_tabpage1_DataGet.BindMainDialog(this);
	m_tabpage1_DataGet.Create(IDD_DIALOG1_MAIN, &m_tab_main);
	//m_tabpage2_FpgaDbg.Create(IDD_DIALOG2_DEBUG, &m_tab_main);

	CRect rTab, rItem;
	m_tab_main.GetItemRect(0, &rItem);
	m_tab_main.GetClientRect(&rTab);
	int x = rItem.left;
	int y = rItem.bottom + 1;
	int cx = rTab.right - rItem.left - 3;
	int cy = rTab.bottom - y - 2;

	m_tabpage1_DataGet.SetWindowPos(NULL, x, y, cx, cy, SWP_HIDEWINDOW);
	//m_tabpage2_FpgaDbg.SetWindowPos(NULL, x, y, cx, cy, SWP_HIDEWINDOW);

	int tab = m_tab_main.GetCurSel();
	switch (tab)
	{
	case 0:
		m_tabpage1_DataGet.SetWindowPos(NULL, x, y, cx, cy, SWP_SHOWWINDOW);
		break;
	case 1:
		m_tabpage2_FpgaDbg.SetWindowPos(NULL, x, y, cx, cy, SWP_SHOWWINDOW);
		break;
	default:
		break;
	}

	// Initialize file logger: create logs/ folder next to the exe
	{
		TCHAR exePath[MAX_PATH];
		GetModuleFileName(NULL, exePath, MAX_PATH);
		CString exeDir(exePath);
		int pos = exeDir.ReverseFind(_T('\\'));
		if (pos >= 0) exeDir = exeDir.Left(pos);
		g_fileLogger.Init(std::wstring(exeDir));
	}

	int iRet = UsbLibInfo.USBBoard_Connect(m_hWnd);

	if (iRet == USB_SUCCESS)
	{
		PrintLog(_T("Connect board success(USB3.0)!"));
	}
	else if (iRet == USB_DEV_USB20)
	{
		PrintLog(_T("Connect board success(USB2.0)!"));
	}
	else
	{
		PrintLog(_T("Connect board failed!"));
		PrintLog(strUSBLibError(iRet));
	}

	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CAnalogBoardTestAppDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialogEx::OnSysCommand(nID, lParam);
	}
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CAnalogBoardTestAppDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

// The system calls this function to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CAnalogBoardTestAppDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}



void CAnalogBoardTestAppDlg::OnTcnSelchangeTabMain(NMHDR* pNMHDR, LRESULT* pResult)
{
	// TODO: Add your control notification handler code here
	*pResult = 0;

	CRect rTab, rItem;
	m_tab_main.GetItemRect(0, &rItem);
	m_tab_main.GetClientRect(&rTab);
	int x = rItem.left;
	int y = rItem.bottom + 1;
	int cx = rTab.right - rItem.left - 3;
	int cy = rTab.bottom - y - 2;
	int tab = m_tab_main.GetCurSel();

	m_tabpage1_DataGet.SetWindowPos(NULL, x, y, cx, cy, SWP_HIDEWINDOW);
	m_tabpage2_FpgaDbg.SetWindowPos(NULL, x, y, cx, cy, SWP_HIDEWINDOW);

	switch (tab)
	{
	case 0:
		m_tabpage1_DataGet.SetWindowPos(NULL, x, y, cx, cy, SWP_SHOWWINDOW);
		break;
	case 1:
		m_tabpage2_FpgaDbg.SetWindowPos(NULL, x, y, cx, cy, SWP_SHOWWINDOW);
		break;
	default:
		break;
	}
}


void CAnalogBoardTestAppDlg::PrintLog(LPCTSTR sting)
{
	SYSTEMTIME curTime;
	GetLocalTime(&curTime);

	CString strBuf;
	strBuf.Format(_T("%04d%02d%02d %02d:%02d:%02d %03d>> %s"), curTime.wYear, curTime.wMonth, curTime.wDay, curTime.wHour, curTime.wMinute, curTime.wSecond, curTime.wMilliseconds, sting);
	if (pMainDlg != NULL)
	{
		pMainDlg->GetDlgItem(IDC_LIST1)->SendMessage(LB_ADDSTRING, 0, (LPARAM)(LPCTSTR)strBuf);
		pMainDlg->GetDlgItem(IDC_LIST1)->SendMessage(WM_VSCROLL, SB_LINEDOWN, 0);
	}
	g_fileLogger.Append(std::wstring((LPCTSTR)strBuf));
}

void CAnalogBoardTestAppDlg::FlushLog()
{
	g_fileLogger.Flush();
}

void CAnalogBoardTestAppDlg::OnClose()
{
	UpdateData(TRUE);

	/* Export default config*/
	m_tabpage1_DataGet.ExportDefaultConfigFile();

	g_fileLogger.Close();

	CDialogEx::OnClose();
}

WCHAR* CAnalogBoardTestAppDlg::strUSBLibError(INT nStatus)
{
	static WCHAR suMessageInf[128];

	memset(suMessageInf, 0, 256);
	switch (nStatus)
	{
	case USB_SUCCESS:
		wsprintf(suMessageInf, _T("Success. "));
		break;
	case USB_ERR_NODEV:
		wsprintf(suMessageInf, _T("No Valid USB Device, Error Code : %d. "), nStatus);
		break;
	case USB_ERR_PARAM:
		wsprintf(suMessageInf, _T("Parameter Error, Error Code : %d. "), nStatus);
		break;
	case USB_ERR_OPENDEV_FAILED:
		wsprintf(suMessageInf, _T("Device Open Error, Error Code : %d. "), nStatus);
		break;
	case USB_ERR_SETINTERFACE_FAILED:
		wsprintf(suMessageInf, _T("Set Interface Error, Error Code : %d. "), nStatus);
		break;
	case USB_ERR_ALLOCMEM_FAILED:
		wsprintf(suMessageInf, _T("Memory Allocate Error, Error Code : %d. "), nStatus);
		break;
	case USB_ERR_NULLPOINTER:
		wsprintf(suMessageInf, _T("NULL Pointer, Error Code : %d. "), nStatus);
		break;
	case USB_ERR_INVALID_ENDPOINTER:
		wsprintf(suMessageInf, _T("Invalid Endpointer, Error Code : %d. "), nStatus);
		break;
	case USB_ERR_VENDOR_ID_ERR:
		wsprintf(suMessageInf, _T("Invalid VID, Error Code : %d. "), nStatus);
		break;
	case USB_ERR_PRODUCT_ID_ERR:
		wsprintf(suMessageInf, _T("Invalid PID, Error Code : %d. "), nStatus);
		break;
	case USB_ERR_TRANSFER_TIMEOUT:
		wsprintf(suMessageInf, _T("USB Timeout, Error Code : %d. "), nStatus);
		break;
	case USB_DEV_USB20:
		wsprintf(suMessageInf, _T("Device is USB2.0, Error Code : %d. "), nStatus);
		break;
	case USB_ERR_UNAVAILABLE:
		wsprintf(suMessageInf, _T("Mutex is running, Error Code : %d. "), nStatus);
		break;
	default:
		break;
	}

	return suMessageInf;
}


LRESULT CAnalogBoardTestAppDlg::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	INT iRet = 0;

	if (message == WM_DEVICECHANGE)
	{
		if (wParam == DBT_DEVICEARRIVAL)
		{
			if (UsbLibInfo.isConnected == FALSE)
			{
				iRet = UsbLibInfo.USBBoard_Connect(m_hWnd);

				if (iRet == USB_SUCCESS)
				{
					PrintLog(_T("Connect board success(USB3.0)!"));
				}
				else if (iRet == USB_DEV_USB20)
				{
					PrintLog(_T("Connect board success(USB2.0)!"));
				}
				else
				{
					PrintLog(_T("USB board is not Found."));
					//PrintLog(strUSBLibError(iRet));
				}
			}
		}
		else if (wParam == DBT_DEVICEREMOVECOMPLETE)
		{
			if (UsbLibInfo.isConnected == TRUE)
			{
				UsbLibInfo.USBBoard_Disconnect();

				iRet = UsbLibInfo.USBBoard_Connect(m_hWnd);

				if (iRet == USB_SUCCESS)
				{
					//PrintLog(_T("Connect board success(USB3.0)!"));
				}
				else if (iRet == USB_DEV_USB20)
				{
					//PrintLog(_T("Connect board success(USB2.0)!"));
				}
				else
				{
					PrintLog(_T("USB board is removed! Disconnect usb."));
					//PrintLog(strUSBLibError(iRet));
				}
			}
		}
	}

	return CDialogEx::WindowProc(message, wParam, lParam);
}


BOOL CAnalogBoardTestAppDlg::PreTranslateMessage(MSG* pMsg)
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
