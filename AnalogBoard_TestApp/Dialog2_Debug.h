#pragma once
#include "afxdialogex.h"

/* Script list item ID */
enum EP2EP4_DATALIST_ITEM_ID
{
	E_ADDRESS = 0,
	E_EP2DATA,
	E_EP4DATA,
};

enum RET_STATUS
{
	E_OK = 0,
	E_FALSE,
};

class CAnalogBoardTestAppDlg;		// Forward declaration
// Dialog2_Debug dialog

class Dialog2_Debug : public CDialogEx
{
	DECLARE_DYNAMIC(Dialog2_Debug)

public:
	Dialog2_Debug(CAnalogBoardTestAppDlg* pParent = nullptr);   // standard constructor
	//Dialog2_Debug(CWnd* pParent = nullptr);   // standard constructor
	//virtual ~Dialog2_Debug();

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG2_DEBUG };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()
public:
	virtual BOOL OnInitDialog();	

	CString m_ImportFile_EP2;
	CString m_ExportFile_EP4;
	CString m_SaveBinFile_EP6;
	INT g_iSelectedRow;
	INT g_iSelectedColumn;
	CListCtrl m_EP2EP4DataList;
	CEdit m_EditDataViewerList;
	CAnalogBoardTestAppDlg* m_pMainDlg;

	afx_msg void OnNMCustomdrawListEp2ep4Data(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnNMDblclkListEp2ep4Data(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnBnClickedButtonImportEp2();
	afx_msg void OnBnClickedButtonExportEp4();
	afx_msg void OnBnClickedButtonEp4rx();
	afx_msg void OnBnClickedButtonImportep2Select();
	afx_msg void OnBnClickedButtonExportep4Select();
	afx_msg void OnEnKillfocusEditDataviewerList();
	afx_msg void OnBnClickedButtonEp2tx();

	void DataViewerQuickFocus(INT RowIndex);
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnBnClickedButtonExportep6Select();
	afx_msg void OnBnClickedButtonEp6rx();
	CEdit m_edit_ReadSize;
};
