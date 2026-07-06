
// Sysmex_AnalogBoard_TestAppDlg.h : header file
//

#pragma once
#include "Dialog1_Main.h"
#include "Dialog2_Debug.h"
#include "../Sysmex_AnalogBoard_Dll/Sysmex_AnalogBoard_Dll.h"
#include "FileLogger.h"

#define REG_MAX_COUNT	89

// CSysmexAnalogBoardTestAppDlg dialog
class CSysmexAnalogBoardTestAppDlg : public CDialogEx
{
// Construction
public:
	CSysmexAnalogBoardTestAppDlg(CWnd* pParent = nullptr);	// standard constructor
	afx_msg void OnClose();

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_SYSMEX_ANALOGBOARD_TESTAPP_DIALOG };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support

// Implementation
protected:
	HICON m_hIcon;
	Dialog1_Main m_tabpage1_DataGet;
	Dialog2_Debug m_tabpage2_FpgaDbg;

	// Generated message map functions
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()

public:
	CTabCtrl m_tab_main;
	USB_Lib_Info UsbLibInfo;

	afx_msg void OnTcnSelchangeTabMain(NMHDR* pNMHDR, LRESULT* pResult);
	
	void PrintLog(LPCTSTR sting);
	void FlushLog();
	WCHAR* CSysmexAnalogBoardTestAppDlg::strUSBLibError(INT nStatus);
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);

//private:
	//bool m_bPnP_DevNodeChange = FALSE;
	//bool m_bPnP_Removal = FALSE;
	//bool m_bPnP_Arrival = FALSE;

public:
	virtual BOOL PreTranslateMessage(MSG* pMsg);

private:
};
