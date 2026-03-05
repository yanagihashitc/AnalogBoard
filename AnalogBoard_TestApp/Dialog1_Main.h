#pragma once
#include "afxdialogex.h"
#include "CColorEdit.h"

#define FPGAREG_FPGA_ST			0x000004
#define FPGAREG_DAT_CH_SEL		0x000006
#define FPGAREG_TRG_SEL			0x000008
#define FPGAREG_TRG_THR			0x00000A
#define FPGAREG_TRG_RANGE_N		0x00000C
#define FPGAREG_TRG_RANGE_P		0x00000E
#define FPGAREG_MEAS_MODE		0x000010
#define FPGAREG_MANUAL_MEAS_ON	0x000012
#define FPGAREG_FILTER_SEL		0x000014
#define FPGAREG_WAVE_WR_CNT_L	0x000018
#define FPGAREG_WAVE_WR_CNT_H	0x00001A
#define FPGAREG_WAVE_RD_CNT_L	0x00001C
#define FPGAREG_WAVE_RD_CNT_H	0x00001E
#define FPGAREG_GAIN_DAT_CH1	0x000020
#define FPGAREG_GAIN_SW_CH1_4	0x000040
#define FPGAREG_GAIN_TRG		0x000050
#define FPGAREG_OFFSET_DAT_CH1	0x000060
#define FPGAREG_OFFSET_TRG		0x000080
#define FPGAREG_DAC_DAT_CH3		0x000090
#define FPGAREG_DAC_DAT_CH9		0x00009C
#define FPGAREG_DAC_TRG			0x0000B0

//#define EP6_MAX_ONETIMESIZE		(1024 * 1024 * 512)
//#define EP6_MAX_ONETIMESIZE		(1024 * 1024 * 1024 * 4)

struct Base { };
typedef struct NamedType : Base
{
	double	GainCh[13][5] = { 0 };		//Gain(CH1~CH13)
	FLOAT	OffsetValue[13] = { 0 };	//Offset Value(1414~1494)
	USHORT  ExtCtrlVol1[5] = { 0 };		//Ext Ctrl Vol1(0~1100mV)
	USHORT  ExtCtrlVol2[6] = { 0 };		//Ext Ctrl Vol2(0~4096mV)
	UCHAR   FirFilterFC = 0;			//Fir filter FC(Hi frq sig)
	UCHAR   CHSelect[13] = { 0 };		//CH Select
	UCHAR   TriggerCh = 0;				//Trigger CH
	USHORT  TriggerValue = 0;			//Trigger Value(0~1800mV)
	FLOAT   TriggerRange[2] = { 0 };	//Trigger Range(-55us~+55us)
	UCHAR   ManualMode = 0;				//Manual Get Mode
	USHORT  WaveNum = 0;				//Waveforms Nums Per File
	CString	SavePath;					//Save Path
}FPGAConfigI_REGMAP;

class CAnalogBoardTestAppDlg;		// Forward declaration
// Dialog1_Main dialog

class Dialog1_Main : public CDialogEx
{
	DECLARE_DYNAMIC(Dialog1_Main)

public:
	Dialog1_Main(CAnalogBoardTestAppDlg* pParent = nullptr);   // standard constructor
	~Dialog1_Main();
	//virtual ~Dialog1_Main();

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG1_MAIN };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	DECLARE_MESSAGE_MAP()

public:
	virtual BOOL OnInitDialog();
	CString m_ImportFile;
	CString m_OutFile;
	CString m_ExportFile;
	int GainTextID[13];
	double strGainMultp[13][5][2];
	bool m_bManualMode;

	CComboBox m_combox_trigch;
	CComboBox m_combox_fir_filter_fc;
	CComboBox m_combox_ch1_gain_multp_1;
	CComboBox m_combox_ch1_gain_multp_2;
	//CComboBox m_combox_ch1_gain_multp_3;
	CComboBox m_combox_ch1_gain_multp_4;
	CComboBox m_combox_ch1_gain_multp_5;
	CComboBox m_combox_ch2_gain_multp_1;
	CComboBox m_combox_ch2_gain_multp_2;
	//CComboBox m_combox_ch2_gain_multp_3;
	CComboBox m_combox_ch2_gain_multp_4;
	CComboBox m_combox_ch2_gain_multp_5;
	CComboBox m_combox_ch3_gain_multp_1;
	CComboBox m_combox_ch3_gain_multp_2;
	//CComboBox m_combox_ch3_gain_multp_3;
	CComboBox m_combox_ch3_gain_multp_4;
	CComboBox m_combox_ch3_gain_multp_5;
	CComboBox m_combox_ch4_gain_multp_1;
	CComboBox m_combox_ch4_gain_multp_2;
	//CComboBox m_combox_ch4_gain_multp_3;
	CComboBox m_combox_ch4_gain_multp_4;
	CComboBox m_combox_ch4_gain_multp_5;
	CComboBox m_combox_ch5_gain_multp_1;
	CComboBox m_combox_ch5_gain_multp_2;
	//CComboBox m_combox_ch5_gain_multp_3;
	CComboBox m_combox_ch5_gain_multp_4;
	CComboBox m_combox_ch5_gain_multp_5;
	CComboBox m_combox_ch6_gain_multp_1;
	CComboBox m_combox_ch6_gain_multp_2;
	//CComboBox m_combox_ch6_gain_multp_3;
	CComboBox m_combox_ch6_gain_multp_4;
	CComboBox m_combox_ch6_gain_multp_5;
	CComboBox m_combox_ch7_gain_multp_1;
	CComboBox m_combox_ch7_gain_multp_2;
	//CComboBox m_combox_ch7_gain_multp_3;
	CComboBox m_combox_ch7_gain_multp_4;
	CComboBox m_combox_ch7_gain_multp_5;
	CComboBox m_combox_ch8_gain_multp_1;
	CComboBox m_combox_ch8_gain_multp_2;
	//CComboBox m_combox_ch8_gain_multp_3;
	CComboBox m_combox_ch8_gain_multp_4;
	CComboBox m_combox_ch8_gain_multp_5;
	CComboBox m_combox_ch9_gain_multp_1;
	CComboBox m_combox_ch9_gain_multp_2;
	CComboBox m_combox_ch9_gain_multp_3;
	CComboBox m_combox_ch9_gain_multp_4;
	CComboBox m_combox_ch9_gain_multp_5;
	CComboBox m_combox_ch10_gain_multp_1;
	CComboBox m_combox_ch10_gain_multp_2;
	CComboBox m_combox_ch10_gain_multp_3;
	CComboBox m_combox_ch10_gain_multp_4;
	CComboBox m_combox_ch10_gain_multp_5;
	CComboBox m_combox_ch11_gain_multp_1;
	CComboBox m_combox_ch11_gain_multp_2;
	CComboBox m_combox_ch11_gain_multp_3;
	CComboBox m_combox_ch11_gain_multp_4;
	CComboBox m_combox_ch11_gain_multp_5;
	CComboBox m_combox_ch12_gain_multp_1;
	CComboBox m_combox_ch12_gain_multp_2;
	CComboBox m_combox_ch12_gain_multp_3;
	CComboBox m_combox_ch12_gain_multp_4;
	CComboBox m_combox_ch12_gain_multp_5;
	CComboBox m_combox_ch13_gain_multp_1;
	CComboBox m_combox_ch13_gain_multp_2;
	CComboBox m_combox_ch13_gain_multp_3;
	CComboBox m_combox_ch13_gain_multp_4;
	CComboBox m_combox_ch13_gain_multp_5;
	CComboBox* comboxGainCh[13][5];	
	CColorEdit m_edit_ch1_offset;
	CColorEdit m_edit_ch2_offset;
	CColorEdit m_edit_ch3_offset;
	CColorEdit m_edit_ch4_offset;
	CColorEdit m_edit_ch5_offset;
	CColorEdit m_edit_ch6_offset;
	CColorEdit m_edit_ch7_offset;
	CColorEdit m_edit_ch8_offset;
	CColorEdit m_edit_ch9_offset;
	CColorEdit m_edit_ch10_offset;
	CColorEdit m_edit_ch11_offset;
	CColorEdit m_edit_ch12_offset;
	CColorEdit m_edit_ch13_offset;
	CColorEdit* editOffsetCh[13];
	CColorEdit m_edit_ch9_vol1;
	CColorEdit m_edit_ch10_vol1;
	CColorEdit m_edit_ch11_vol1;
	CColorEdit m_edit_ch12_vol1;
	CColorEdit m_edit_ch13_vol1;
	CColorEdit* editVol1Ch[5];
	CColorEdit m_edit_ch3_vol2;
	CColorEdit m_edit_ch4_vol2;
	CColorEdit m_edit_ch5_vol2;
	CColorEdit m_edit_ch6_vol2;
	CColorEdit m_edit_ch7_vol2;
	CColorEdit m_edit_ch8_vol2;
	CColorEdit* editVol2Ch[6];
	CColorEdit m_edit_trigger_value;
	CColorEdit m_edit_trigger_low;
	CColorEdit m_edit_trigger_high;
	CColorEdit m_edit_wave_num;
	CEdit m_CtrlEditCollectedCnt;	
	CEdit m_edit_export;
	CEdit m_edit_import;
	CEdit m_edit_savepath;
	CButton m_CtrlBtnParSet;
	CButton m_CtrlBtnDataGetStart;
	CButton m_button_all_select;
	CButton m_button_ch1_select;
	CButton m_button_ch2_select;
	CButton m_button_ch3_select;
	CButton m_button_ch4_select;
	CButton m_button_ch5_select;
	CButton m_button_ch6_select;
	CButton m_button_ch7_select;
	CButton m_button_ch8_select;
	CButton m_button_ch9_select;
	CButton m_button_ch10_select;
	CButton m_button_ch11_select;
	CButton m_button_ch12_select;
	CButton m_button_ch13_select;
	CButton* buttonSelectCh[13];
	CButton m_button_mode_manual;
	CButton m_button_mode_auto;
	CStatic m_static_ActualRange;
	CStatic m_static_SampleInfo;
	CStatic m_static_SampleInfo2;
	CStatic m_static_SampleInfo3;
	CButton m_button_select_savepath;
	CColorEdit m_edit_ch1_gain_multp_3;
	CColorEdit m_edit_ch2_gain_multp_3;
	CColorEdit m_edit_ch3_gain_multp_3;
	CColorEdit m_edit_ch4_gain_multp_3;
	CColorEdit m_edit_ch5_gain_multp_3;
	CColorEdit m_edit_ch6_gain_multp_3;
	CColorEdit m_edit_ch7_gain_multp_3;
	CColorEdit m_edit_ch8_gain_multp_3;
	CColorEdit* editMultp3GainCh[8];

	CAnalogBoardTestAppDlg* m_pMainDlg;	// Parent window pointer

	void UpdateTotalGain(INT CHID);
	void UpdateChSelect();
	void ImportDefaultConfigFile();
	void ExportDefaultConfigFile();
	INT UpdateConfigStruct(FPGAConfigI_REGMAP* myStructPtr);
	void Reg_Write(UINT Address, USHORT Data, PBYTE Ep2DataBuffer);
	USHORT Reg_Read(UINT Address, PBYTE Ep4RegBuffer);
	void RegSet_SetGainValue(double GainValue[13][5], PBYTE Ep2DataBuffer);
	void RegSet_SetOffsetValue(INT CHID, FLOAT OffsetValue, PBYTE Ep2DataBuffer);
	void RegSet_SetExtCtrlVol_1(INT CHID, USHORT ExtCtrlVolValaue, PBYTE Ep2DataBuffer);
	void RegSet_SetExtCtrlVol_2(INT CHID, USHORT ExtCtrlVolValaue, PBYTE Ep2DataBuffer);
	void RegSet_UpdateGainValue(PBYTE Ep2DataBuffer);
	void RegSet_UpdateOffsetValue(PBYTE Ep2DataBuffer);
	void RegSet_UpdateExtCtrlVol(PBYTE Ep2DataBuffer);
	void RegSet_SelectFirFilterFC(UCHAR FirFilterFC, PBYTE Ep2DataBuffer);
	void RegSet_SelectDataCH(UCHAR CHSelect[13], PBYTE Ep2DataBuffer);
	void RegSet_SelectTRGCH(UCHAR TRGCH, PBYTE Ep2DataBuffer);
	void RegSet_SetTRGValue(USHORT TRGValue, PBYTE Ep2DataBuffer);
	void RegSet_SetTRGRange(INT RangeSel, FLOAT TRGRangeValue, PBYTE Ep2DataBuffer);
	void RegSet_SelectGetDataMeas(UCHAR ManualMode, PBYTE Ep2DataBuffer);
	void RegSet_GetWaveDataStart(BOOL StartFlag, PBYTE Ep2DataBuffer);	
	ULONG RegGet_DDRWaveCnt(PBYTE Ep4DataBuffer);
	ULONG RegGet_DDRReadCnt(PBYTE Ep4DataBuffer);
	INT RegGet_DDRWriteEnd(PBYTE Ep4DataBuffer);
	INT RegGet_DDRReadEnd(PBYTE Ep4DataBuffer);
	bool RegGet_SampleStartSt(PBYTE Ep4DataBuffer);
	void EditCtrl_HighLight(CColorEdit* EditCtrl, BOOL HLFlag);
	INT ValidateSavePathForUi(const CString& savePath, BOOL showMessageBox);
	void SaveCfgParametersToFile(CString FilePath, FPGAConfigI_REGMAP* packetConfig, BOOL totalGainValue);
	void UpdateRangeDisplay(FPGAConfigI_REGMAP* CfgStruct);
	void SamplingUISet(bool OpenFlag, bool samplingmode);
	void CheckGain3andDisply(FPGAConfigI_REGMAP* Config, INT index);

	afx_msg void OnBnClickedRadioMdoeManual();
	afx_msg void OnBnClickedRadioMdoeAuto();
	afx_msg void OnCbnSelchangeComboCh1();
	afx_msg void OnCbnSelchangeComboCh2();
	afx_msg void OnCbnSelchangeComboCh3();
	afx_msg void OnCbnSelchangeComboCh4();
	afx_msg void OnCbnSelchangeComboCh5();
	afx_msg void OnCbnSelchangeComboCh6();
	afx_msg void OnCbnSelchangeComboCh7();
	afx_msg void OnCbnSelchangeComboCh8();
	afx_msg void OnCbnSelchangeComboCh9();
	afx_msg void OnCbnSelchangeComboCh10();
	afx_msg void OnCbnSelchangeComboCh11();
	afx_msg void OnCbnSelchangeComboCh12();
	afx_msg void OnCbnSelchangeComboCh13();
	afx_msg void OnBnClickedButtonParset();
	afx_msg void OnBnClickedButtonGetstart();
	afx_msg void OnBnClickedCheckChall();
	afx_msg void OnBnClickedButtonExport();
	afx_msg void OnBnClickedButtonImport();
	afx_msg void OnBnClickedCheckCh1();
	afx_msg void OnBnClickedCheckCh2();
	afx_msg void OnBnClickedCheckCh3();
	afx_msg void OnBnClickedCheckCh4();
	afx_msg void OnBnClickedCheckCh5();
	afx_msg void OnBnClickedCheckCh6();
	afx_msg void OnBnClickedCheckCh7();
	afx_msg void OnBnClickedCheckCh8();
	afx_msg void OnBnClickedCheckCh9();
	afx_msg void OnBnClickedCheckCh10();
	afx_msg void OnBnClickedCheckCh11();
	afx_msg void OnBnClickedCheckCh12();
	afx_msg void OnBnClickedButtonImportSelect();
	afx_msg void OnBnClickedButtonExportSelect();
	afx_msg void OnBnClickedButtonSavepathSelect();
	afx_msg void OnEnChangeEditTriggerRangeLow();
	afx_msg void OnEnChangeEditTriggerRangeHigh();
	afx_msg void OnCbnSelchangeComboFirFilterFc();	
	afx_msg void OnEnChangeEditCh1GainMultip3();
	afx_msg void OnEnChangeEditCh2GainMultip3();
	afx_msg void OnEnChangeEditCh3GainMultip3();
	afx_msg void OnEnChangeEditCh4GainMultip3();
	afx_msg void OnEnChangeEditCh5GainMultip3();
	afx_msg void OnEnChangeEditCh6GainMultip3();
	afx_msg void OnEnChangeEditCh7GainMultip3();
	afx_msg void OnEnChangeEditCh8GainMultip3();
	afx_msg void OnEnKillfocusEditSavepath();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
};
