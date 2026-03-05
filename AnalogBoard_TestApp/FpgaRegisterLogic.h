/*******************************************************************************
* Copyright (C) PHINE Design, Ltd. 2024
* All rights reserved.
*
* File name		:	FpgaRegisterLogic.h
* File summary	:	FPGA register pure logic functions (no MFC dependency)
*******************************************************************************/
#ifndef FPGA_REGISTER_LOGIC_H
#define FPGA_REGISTER_LOGIC_H

#include <cmath>
#include "FpgaRegisterAddress.h"

#ifdef _WIN32
#include <windows.h>
#else
// Portable type definitions for non-Windows builds (unit test)
typedef unsigned char  BYTE;
typedef BYTE*          PBYTE;
typedef unsigned short USHORT;
typedef unsigned int   UINT;
typedef unsigned long  ULONG;
typedef int            INT;
typedef float          FLOAT;
typedef unsigned char  UCHAR;
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#endif

/**********************************************************************************
* Pure Logic Functions
**********************************************************************************/
namespace FpgaRegLogic {

inline void Reg_Write(UINT Address, USHORT Data, PBYTE Buffer)
{
	*(Buffer + Address) = Data & 0xFF;
	*(Buffer + Address + 1) = (Data >> 8) & 0xFF;
}

inline USHORT Reg_Read(UINT Address, const BYTE* Buffer)
{
	USHORT Data = 0;
	Data = *(Buffer + Address + 1);
	Data = (Data << 8) | *(Buffer + Address);
	return Data;
}

inline USHORT CalcOffsetRegValue(FLOAT OffsetValue)
{
	double accuracy = 80.0 / 255.0;
	double dData = (double)OffsetValue - 1414.0;
	dData = dData / accuracy + 0.5;
	USHORT usData = (USHORT)dData;
	return 255 - usData;
}

inline void RegSet_SetOffsetValue(INT CHID, FLOAT OffsetValue, PBYTE Ep2DataBuffer)
{
	USHORT usData = CalcOffsetRegValue(OffsetValue);
	Reg_Write((UINT)(FPGAREG_OFFSET_DAT_CH1 + (CHID * 2)), usData, Ep2DataBuffer);
}

inline USHORT CalcExtCtrlVolRegValue(USHORT VoltageMv)
{
	double accuracy = 5000.0 / 65535.0;
	double dData = (double)(VoltageMv);
	dData = dData / accuracy + 0.5;
	return (USHORT)dData;
}

inline void RegSet_SetExtCtrlVol_1(INT CHID, USHORT ExtCtrlVolValue, PBYTE Ep2DataBuffer)
{
	USHORT usData = CalcExtCtrlVolRegValue(ExtCtrlVolValue);
	Reg_Write((UINT)(FPGAREG_DAC_DAT_CH9 + (CHID * 2)), usData, Ep2DataBuffer);
}

inline void RegSet_SetExtCtrlVol_2(INT CHID, USHORT ExtCtrlVolValue, PBYTE Ep2DataBuffer)
{
	USHORT usData = CalcExtCtrlVolRegValue(ExtCtrlVolValue);
	Reg_Write((UINT)(FPGAREG_DAC_DAT_CH3 + (CHID * 2)), usData, Ep2DataBuffer);
}

inline void RegSet_UpdateGainValue(PBYTE Ep2DataBuffer)
{
	USHORT usData = 1;
	Reg_Write((UINT)FPGAREG_GAIN_TRG, usData, Ep2DataBuffer);
}

inline void RegSet_UpdateOffsetValue(PBYTE Ep2DataBuffer)
{
	USHORT usData = 1;
	Reg_Write((UINT)FPGAREG_OFFSET_TRG, usData, Ep2DataBuffer);
}

inline void RegSet_UpdateExtCtrlVol(PBYTE Ep2DataBuffer)
{
	USHORT usData = 1;
	Reg_Write((UINT)FPGAREG_DAC_TRG, usData, Ep2DataBuffer);
}

inline void RegSet_SelectFirFilterFC(UCHAR FirFilterFC, PBYTE Ep2DataBuffer)
{
	USHORT usData = 0;
	if (FirFilterFC == 0)
	{
		usData = 0; // Filter 15MHz, Resampling 40Msps
	}
	else
	{
		usData = 1; // Filter 25MHz, Resampling 60Msps
	}
	Reg_Write((UINT)FPGAREG_FILTER_SEL, usData, Ep2DataBuffer);
}

inline USHORT BuildChSelectBitmask(const UCHAR CHSelect[13])
{
	USHORT usData = 0;
	for (int i = 0; i < 13; i++)
	{
		if (CHSelect[i] == 1)
		{
			usData |= 1 << i;
		}
	}
	return usData;
}

inline void RegSet_SelectDataCH(const UCHAR CHSelect[13], PBYTE Ep2DataBuffer)
{
	USHORT usData = BuildChSelectBitmask(CHSelect);
	Reg_Write((UINT)FPGAREG_DAT_CH_SEL, usData, Ep2DataBuffer);
}

inline USHORT BuildTrgChBitmask(UCHAR TRGCH)
{
	if (TRGCH < 1 || TRGCH > 13) return 0;
	USHORT usData = 0;
	usData |= 1 << (TRGCH - 1);
	return usData;
}

inline void RegSet_SelectTRGCH(UCHAR TRGCH, PBYTE Ep2DataBuffer)
{
	USHORT usData = BuildTrgChBitmask(TRGCH);
	Reg_Write((UINT)FPGAREG_TRG_SEL, usData, Ep2DataBuffer);
}

inline USHORT CalcTrgValueRegValue(USHORT TRGValue)
{
	double accuracy = 2000.0 / 16383.0;
	double dData = (double)TRGValue;
	dData = dData / accuracy + 0.5;
	return (USHORT)dData;
}

inline void RegSet_SetTRGValue(USHORT TRGValue, PBYTE Ep2DataBuffer)
{
	USHORT usData = CalcTrgValueRegValue(TRGValue);
	Reg_Write((UINT)FPGAREG_TRG_THR, usData, Ep2DataBuffer);
}

inline USHORT CalcTrgRangeSamples(FLOAT TRGRangeValue)
{
	return (USHORT)(TRGRangeValue * 40);
}

inline void RegSet_SetTRGRange(INT RangeSel, FLOAT TRGRangeValue, PBYTE Ep2DataBuffer)
{
	UINT iRegIndex = 0;
	USHORT usData = CalcTrgRangeSamples(TRGRangeValue);

	if (RangeSel == 0) // N
	{
		iRegIndex = (UINT)FPGAREG_TRG_RANGE_N;
	}
	else // P
	{
		iRegIndex = (UINT)FPGAREG_TRG_RANGE_P;
	}

	Reg_Write(iRegIndex, usData, Ep2DataBuffer);
}

inline void RegSet_SelectGetDataMeas(UCHAR ManualMode, PBYTE Ep2DataBuffer)
{
	USHORT usData = 0;
	if (ManualMode == 0)
	{
		usData = 0; // Auto Mode
	}
	else
	{
		usData = 1; // Manual Mode
	}
	Reg_Write((UINT)FPGAREG_MEAS_MODE, usData, Ep2DataBuffer);
}

inline void RegSet_GetWaveDataStart(INT StartFlag, PBYTE Ep2DataBuffer)
{
	USHORT usData = StartFlag ? 1 : 0;
	Reg_Write((UINT)FPGAREG_MANUAL_MEAS_ON, usData, Ep2DataBuffer);
}

inline ULONG RegGet_DDRWaveCnt(const BYTE* Ep4DataBuffer)
{
	USHORT usData = 0;
	ULONG DDRSize = 0;

	usData = Reg_Read((UINT)FPGAREG_WAVE_WR_CNT_H, Ep4DataBuffer);
	DDRSize = (ULONG)usData;
	usData = Reg_Read((UINT)FPGAREG_WAVE_WR_CNT_L, Ep4DataBuffer);
	DDRSize = (ULONG)((DDRSize << 16) | usData);

	return DDRSize;
}

inline ULONG RegGet_DDRReadCnt(const BYTE* Ep4DataBuffer)
{
	USHORT usData = 0;
	ULONG DDRSize = 0;

	usData = Reg_Read((UINT)FPGAREG_WAVE_RD_CNT_H, Ep4DataBuffer);
	DDRSize = (ULONG)usData;
	usData = Reg_Read((UINT)FPGAREG_WAVE_RD_CNT_L, Ep4DataBuffer);
	DDRSize = (ULONG)((DDRSize << 16) | usData);

	return DDRSize;
}

inline INT RegGet_DDRWriteEnd(const BYTE* Ep4DataBuffer)
{
	USHORT usData = 0;
	usData = Reg_Read((UINT)FPGAREG_FPGA_ST, Ep4DataBuffer);
	return (usData & 0x4) == 0x4 ? 1 : 0;
}

inline INT RegGet_DDRReadEnd(const BYTE* Ep4DataBuffer)
{
	USHORT usData = 0;
	usData = Reg_Read((UINT)FPGAREG_FPGA_ST, Ep4DataBuffer);
	return (usData & 0x8) == 0x8 ? 1 : 0;
}

inline bool RegGet_SampleStartSt(const BYTE* Ep4DataBuffer)
{
	USHORT usData = 0;
	usData = Reg_Read((UINT)FPGAREG_FPGA_ST, Ep4DataBuffer);
	return (usData & 0x10) == 0x10;
}

inline USHORT CalcGain3RegValue(double Gain3Value)
{
	// Defensive clamp to avoid undefined conversion when caller passes out-of-range value.
	if (Gain3Value > -0.5) Gain3Value = -0.5;
	if (Gain3Value < -1.0) Gain3Value = -1.0;

	double accuracy = 0.5 / 511.0;
	double dData = Gain3Value;
	dData = (-0.5 - dData) / accuracy + 0.5;
	if (dData < 0.0) dData = 0.0;
	if (dData > 511.0) dData = 511.0;
	USHORT usData = static_cast<USHORT>(dData) + 0x200;
	return usData;
}

/**********************************************************************************
* Gain1/2 Switch Bit Encoding
*
* Compare GainValue[ch][gain_index] with strGainMultp[ch][gain_index][0/1]
* and build a bitmask.
* gain_index 0 = Gain1, 1 = Gain2
* Match strGainMultp[ch][j][0] -> bit=0, match [1] -> bit=1
*
* ch1~12: Group channels in sets of 4 and write one 16-bit register.
*   Per channel 4-bit field: [Gain1_bit, Gain2_bit, 0, 0] from MSB to LSB.
*   j is processed as j=1->0:
*     usTemp = (usTemp << 1) | bit_j1, then (usTemp << 1) | bit_j0
*   So bit_j1 is the upper bit and bit_j0 is the lower bit.
*   Channels are processed as i=3->0:
*     usData = (usData << 4) | usTemp
*   So ch[3] becomes the highest 4 bits and ch[0] the lowest 4 bits.
*
* ch13: handled separately, Gain1/2 only
**********************************************************************************/
inline USHORT BuildGainSwitchGroup(
	const double GainValue[][5],
	const double strGainMultp[][5][2],
	int groupBase, int groupSize)
{
	USHORT usData = 0;

	for (int i = groupSize - 1; i >= 0; i--)
	{
		USHORT usTemp = 0;
		int ch = groupBase + i;

		for (int j = 1; j >= 0; j--)
		{
			if (GainValue[ch][j] == strGainMultp[ch][j][0])
			{
				usTemp = (usTemp << 1) | 0x0;
			}
			else if (GainValue[ch][j] == strGainMultp[ch][j][1])
			{
				usTemp = (usTemp << 1) | 0x1;
			}
			else
			{
				// error case: value doesn't match either option
				usTemp = (usTemp << 1) | 0x0;
			}
		}

		usData = (usData << 4) | usTemp;
	}

	return usData;
}

inline USHORT BuildGainSwitchCh13(
	const double GainValue[5],
	const double strGainMultp[5][2])
{
	USHORT usTemp = 0;

	for (int j = 1; j >= 0; j--)
	{
		if (GainValue[j] == strGainMultp[j][0])
		{
			usTemp = (usTemp << 1) | 0x0;
		}
		else if (GainValue[j] == strGainMultp[j][1])
		{
			usTemp = (usTemp << 1) | 0x1;
		}
		else
		{
			usTemp = (usTemp << 1) | 0x0;
		}
	}

	return usTemp;
}

/**********************************************************************************
* Full EP2 Buffer Construction
* Reproduce the register write sequence in OnBnClickedButtonParset.
* Build the EP2 buffer content deterministically before USB transfer.
**********************************************************************************/
struct FpgaConfig {
	double   GainCh[13][5];
	FLOAT    OffsetValue[13];
	USHORT   ExtCtrlVol1[5];
	USHORT   ExtCtrlVol2[6];
	UCHAR    FirFilterFC;
	UCHAR    CHSelect[13];
	UCHAR    TriggerCh;
	USHORT   TriggerValue;
	FLOAT    TriggerRange[2];
	UCHAR    ManualMode;
};

inline void BuildFullEp2Buffer(
	const FpgaConfig& config,
	const double strGainMultp[13][5][2],
	PBYTE ep2Buf)
{
	// Gain3 (ch1~8)
	for (int i = 0; i < 8; i++)
	{
		if (config.GainCh[i][2] >= -1.0 && config.GainCh[i][2] <= -0.5)
		{
			USHORT usData = CalcGain3RegValue(config.GainCh[i][2]);
			Reg_Write((UINT)(FPGAREG_GAIN_DAT_CH1 + (i * 2)), usData, ep2Buf);
		}
	}

	// Gain1/2 switch (ch1~12, 3 groups of 4)
	for (int n = 0; n < 3; n++)
	{
		USHORT usData = BuildGainSwitchGroup(config.GainCh, strGainMultp, n * 4, 4);
		Reg_Write((UINT)(FPGAREG_GAIN_SW_CH1_4 + (n * 2)), usData, ep2Buf);
	}

	// Gain1/2 switch (ch13)
	{
		USHORT usData = BuildGainSwitchCh13(config.GainCh[12], strGainMultp[12]);
		Reg_Write((UINT)(FPGAREG_GAIN_SW_CH1_4 + (3 * 2)), usData, ep2Buf);
	}

	// Gain trigger
	// Preserve legacy sequence in OnBnClickedButtonParset: trigger is asserted here and again at the end.
	Reg_Write((UINT)FPGAREG_GAIN_TRG, 0x1, ep2Buf);

	// Offset (ch0~12)
	for (int i = 0; i < 13; i++)
	{
		RegSet_SetOffsetValue(i, config.OffsetValue[i], ep2Buf);
	}
	// Preserve legacy sequence in OnBnClickedButtonParset: trigger is asserted here and again at the end.
	Reg_Write((UINT)FPGAREG_OFFSET_TRG, 0x1, ep2Buf);

	// ExtCtrlVol1 (5 channels)
	for (int i = 0; i < 5; i++)
	{
		RegSet_SetExtCtrlVol_1(i, config.ExtCtrlVol1[i], ep2Buf);
	}
	// Preserve legacy sequence in OnBnClickedButtonParset: trigger is asserted here and again at the end.
	Reg_Write((UINT)FPGAREG_DAC_TRG, 0x1, ep2Buf);

	// ExtCtrlVol2 (6 channels)
	for (int i = 0; i < 6; i++)
	{
		RegSet_SetExtCtrlVol_2(i, config.ExtCtrlVol2[i], ep2Buf);
	}

	// FIR filter FC
	RegSet_SelectFirFilterFC(config.FirFilterFC, ep2Buf);

	// Data CH select
	RegSet_SelectDataCH(config.CHSelect, ep2Buf);

	// Trigger CH
	RegSet_SelectTRGCH(config.TriggerCh, ep2Buf);

	// Trigger value
	RegSet_SetTRGValue(config.TriggerValue, ep2Buf);

	// Trigger range
	RegSet_SetTRGRange(0, config.TriggerRange[0], ep2Buf);
	RegSet_SetTRGRange(1, config.TriggerRange[1], ep2Buf);

	// Measurement mode
	RegSet_SelectGetDataMeas(config.ManualMode, ep2Buf);

	// Final triggers
	RegSet_UpdateGainValue(ep2Buf);
	RegSet_UpdateOffsetValue(ep2Buf);
	RegSet_UpdateExtCtrlVol(ep2Buf);
}

} // namespace FpgaRegLogic

#endif // FPGA_REGISTER_LOGIC_H
