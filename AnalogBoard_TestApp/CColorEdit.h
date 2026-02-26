#pragma once
#include <afxwin.h>
class CColorEdit :
    public CEdit
{
public:
	CColorEdit();
	~CColorEdit();

	void SetForeColor(COLORREF color);
	void SetBkColor(COLORREF color);
	void SetTextFont(int FontHight, LPCTSTR FontName);

private:
	COLORREF m_ForeColor;  // text color
	COLORREF m_BackColor;  // background color
	CBrush	 m_BkBrush;	   // background brush
	CFont* p_Font;

public:
	DECLARE_MESSAGE_MAP()
	afx_msg HBRUSH CtlColor(CDC* /*pDC*/, UINT /*nCtlColor*/);
};

