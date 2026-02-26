#include "pch.h"
#include "CColorEdit.h"

CColorEdit::CColorEdit()
{
	m_ForeColor = RGB(0, 0, 0);
	m_BackColor = RGB(255, 255, 255);
	m_BkBrush.CreateSolidBrush(m_BackColor);
	p_Font = NULL;
}

CColorEdit::~CColorEdit()
{
	if (p_Font) {
		delete p_Font;
	}
}

BEGIN_MESSAGE_MAP(CColorEdit, CEdit)
	ON_WM_CTLCOLOR_REFLECT()
END_MESSAGE_MAP()

//HBRUSH CColorEdit::CtlColor(CDC* /*pDC*/, UINT /*nCtlColor*/)
HBRUSH CColorEdit::CtlColor(CDC* pDC, UINT nCtlColor)
{
	// TODO:  Change any attributes of the DC here
	pDC->SetTextColor(m_ForeColor);
	pDC->SetBkColor(m_BackColor);
	m_BkBrush.DeleteObject();
	m_BkBrush.CreateSolidBrush(m_BackColor);

	// TODO:  Return a non-NULL brush if the parent's handler should not be called
	return (HBRUSH)m_BkBrush.GetSafeHandle();
}

void CColorEdit::SetForeColor(COLORREF color) {
	m_ForeColor = color;
}

void CColorEdit::SetBkColor(COLORREF color) {
	m_BackColor = color;
}

void CColorEdit::SetTextFont(int FontHight, LPCTSTR FontName) {
	if (p_Font) {
		delete p_Font;
	}
	p_Font = new CFont;
	p_Font->CreatePointFont(FontHight, FontName);
	SetFont(p_Font);
}
