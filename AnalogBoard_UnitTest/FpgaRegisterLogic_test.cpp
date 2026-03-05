/*******************************************************************************
* FpgaRegisterLogic Unit Tests
*
* FPGA レジスタ操作の純粋ロジック関数のテスト。
* 外部依存なしでコンパイル・実行可能。
*
* ビルド方法 (Visual Studio Developer Command Prompt):
*   cl /EHsc /W4 /I.. FpgaRegisterLogic_test.cpp /Fe:FpgaRegisterLogic_test.exe
*
* 実行:
*   FpgaRegisterLogic_test.exe
*******************************************************************************/

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cassert>

// Windows types for standalone test build
#include <windows.h>

#include "../AnalogBoard_TestApp/FpgaRegisterLogic.h"

/*******************************************************************************
* Simple Test Framework
*******************************************************************************/
static int g_TestCount = 0;
static int g_PassCount = 0;
static int g_FailCount = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_TestCount++; \
    if (cond) { g_PassCount++; } \
    else { g_FailCount++; printf("  FAIL: %s (line %d)\n", msg, __LINE__); } \
} while(0)

#define TEST_ASSERT_EQ(expected, actual, msg) do { \
    g_TestCount++; \
    if ((expected) == (actual)) { g_PassCount++; } \
    else { g_FailCount++; printf("  FAIL: %s - expected %d, got %d (line %d)\n", msg, (int)(expected), (int)(actual), __LINE__); } \
} while(0)

#define TEST_ASSERT_FLOAT_EQ(expected, actual, eps, msg) do { \
    g_TestCount++; \
    if (fabs((double)(expected) - (double)(actual)) < (eps)) { g_PassCount++; } \
    else { g_FailCount++; printf("  FAIL: %s - expected %f, got %f (line %d)\n", msg, (double)(expected), (double)(actual), __LINE__); } \
} while(0)

#define RUN_TEST(func) do { \
    printf("[TEST] %s\n", #func); \
    func(); \
} while(0)

/*******************************************************************************
* Test: Reg_Write
*******************************************************************************/
void Test_Reg_Write_Basic()
{
    BYTE buffer[256] = { 0 };

    // 0x1234 をアドレス 0x20 に書き込み
    FpgaRegLogic::Reg_Write(0x20, 0x1234, buffer);

    TEST_ASSERT_EQ(0x34, buffer[0x20], "Reg_Write low byte");
    TEST_ASSERT_EQ(0x12, buffer[0x21], "Reg_Write high byte");
}

void Test_Reg_Write_Zero()
{
    BYTE buffer[256] = { 0xFF, 0xFF, 0xFF, 0xFF };

    FpgaRegLogic::Reg_Write(0, 0x0000, buffer);

    TEST_ASSERT_EQ(0x00, buffer[0], "Reg_Write zero low byte");
    TEST_ASSERT_EQ(0x00, buffer[1], "Reg_Write zero high byte");
}

void Test_Reg_Write_MaxValue()
{
    BYTE buffer[256] = { 0 };

    FpgaRegLogic::Reg_Write(0x10, 0xFFFF, buffer);

    TEST_ASSERT_EQ(0xFF, buffer[0x10], "Reg_Write 0xFFFF low byte");
    TEST_ASSERT_EQ(0xFF, buffer[0x11], "Reg_Write 0xFFFF high byte");
}

void Test_Reg_Write_DoesNotCorruptAdjacent()
{
    BYTE buffer[256];
    memset(buffer, 0xAA, sizeof(buffer));

    FpgaRegLogic::Reg_Write(0x10, 0x1234, buffer);

    TEST_ASSERT_EQ(0xAA, buffer[0x0F], "Reg_Write no corrupt before");
    TEST_ASSERT_EQ(0x34, buffer[0x10], "Reg_Write target low");
    TEST_ASSERT_EQ(0x12, buffer[0x11], "Reg_Write target high");
    TEST_ASSERT_EQ(0xAA, buffer[0x12], "Reg_Write no corrupt after");
}

/*******************************************************************************
* Test: Reg_Read
*******************************************************************************/
void Test_Reg_Read_Basic()
{
    BYTE buffer[256] = { 0 };
    buffer[0x20] = 0x34;
    buffer[0x21] = 0x12;

    USHORT result = FpgaRegLogic::Reg_Read(0x20, buffer);

    TEST_ASSERT_EQ(0x1234, result, "Reg_Read basic");
}

void Test_Reg_Read_Zero()
{
    BYTE buffer[256] = { 0 };

    USHORT result = FpgaRegLogic::Reg_Read(0x00, buffer);

    TEST_ASSERT_EQ(0x0000, result, "Reg_Read zero");
}

void Test_Reg_Read_MaxValue()
{
    BYTE buffer[256];
    memset(buffer, 0xFF, sizeof(buffer));

    USHORT result = FpgaRegLogic::Reg_Read(0x10, buffer);

    TEST_ASSERT_EQ(0xFFFF, result, "Reg_Read max value");
}

/*******************************************************************************
* Test: Reg_Write / Reg_Read roundtrip
*******************************************************************************/
void Test_Reg_WriteRead_Roundtrip()
{
    BYTE buffer[256] = { 0 };
    USHORT testValues[] = { 0x0000, 0x0001, 0x00FF, 0x0100, 0x1234, 0x5678, 0xABCD, 0xFFFF };

    for (int i = 0; i < sizeof(testValues) / sizeof(testValues[0]); i++)
    {
        FpgaRegLogic::Reg_Write(0x40, testValues[i], buffer);
        USHORT readBack = FpgaRegLogic::Reg_Read(0x40, buffer);
        TEST_ASSERT_EQ(testValues[i], readBack, "Reg roundtrip");
    }
}

/*******************************************************************************
* Test: CalcOffsetRegValue
*******************************************************************************/
void Test_CalcOffsetRegValue_Min()
{
    // OffsetValue = 1414.0 -> dData = 0.0/accuracy + 0.5 = 0.5 -> (USHORT)0 = 0 -> 255 - 0 = 255
    USHORT result = FpgaRegLogic::CalcOffsetRegValue(1414.0f);
    TEST_ASSERT_EQ(255, result, "CalcOffset min (1414)");
}

void Test_CalcOffsetRegValue_Max()
{
    // OffsetValue = 1494.0 -> dData = 80.0/accuracy + 0.5 = 255.5 -> (USHORT)255 -> 255-255 = 0
    USHORT result = FpgaRegLogic::CalcOffsetRegValue(1494.0f);
    TEST_ASSERT_EQ(0, result, "CalcOffset max (1494)");
}

void Test_CalcOffsetRegValue_Mid()
{
    // OffsetValue = 1454.0 -> dData = 40.0/(80/255) + 0.5 = 40*255/80 + 0.5 = 127.5 + 0.5 = 128.0
    // (USHORT)128 -> 255 - 128 = 127
    USHORT result = FpgaRegLogic::CalcOffsetRegValue(1454.0f);
    TEST_ASSERT_EQ(127, result, "CalcOffset mid (1454)");
}

/*******************************************************************************
* Test: RegSet_SetOffsetValue (buffer integration)
*******************************************************************************/
void Test_RegSet_SetOffsetValue()
{
    BYTE buffer[256] = { 0 };

    FpgaRegLogic::RegSet_SetOffsetValue(0, 1414.0f, buffer);

    USHORT regValue = FpgaRegLogic::Reg_Read(FPGAREG_OFFSET_DAT_CH1, buffer);
    TEST_ASSERT_EQ(255, regValue, "RegSet_SetOffsetValue CH0 min");

    FpgaRegLogic::RegSet_SetOffsetValue(5, 1494.0f, buffer);
    regValue = FpgaRegLogic::Reg_Read(FPGAREG_OFFSET_DAT_CH1 + 10, buffer);
    TEST_ASSERT_EQ(0, regValue, "RegSet_SetOffsetValue CH5 max");
}

/*******************************************************************************
* Test: CalcExtCtrlVolRegValue
*******************************************************************************/
void Test_CalcExtCtrlVolRegValue_Zero()
{
    USHORT result = FpgaRegLogic::CalcExtCtrlVolRegValue(0);
    // 0 / (5000/65535) + 0.5 = 0.5 -> (USHORT)0
    TEST_ASSERT_EQ(0, result, "CalcExtCtrlVol zero");
}

void Test_CalcExtCtrlVolRegValue_1100()
{
    // 1100 / (5000/65535) + 0.5 = 1100 * 65535 / 5000 + 0.5 = 14417.7 + 0.5 = 14418.2
    USHORT result = FpgaRegLogic::CalcExtCtrlVolRegValue(1100);
    USHORT expected = (USHORT)(1100.0 / (5000.0 / 65535.0) + 0.5);
    TEST_ASSERT_EQ(expected, result, "CalcExtCtrlVol 1100mV");
}

void Test_CalcExtCtrlVolRegValue_5000()
{
    // 5000 / (5000/65535) + 0.5 = 65535.5 -> (USHORT)65535
    USHORT result = FpgaRegLogic::CalcExtCtrlVolRegValue(5000);
    USHORT expected = (USHORT)(5000.0 / (5000.0 / 65535.0) + 0.5);
    TEST_ASSERT_EQ(expected, result, "CalcExtCtrlVol 5000mV");
}

/*******************************************************************************
* Test: RegSet_SetExtCtrlVol_1 / _2 (buffer integration)
*******************************************************************************/
void Test_RegSet_SetExtCtrlVol_1()
{
    BYTE buffer[256] = { 0 };

    FpgaRegLogic::RegSet_SetExtCtrlVol_1(0, 1100, buffer);

    USHORT regValue = FpgaRegLogic::Reg_Read(FPGAREG_DAC_DAT_CH9, buffer);
    USHORT expected = FpgaRegLogic::CalcExtCtrlVolRegValue(1100);
    TEST_ASSERT_EQ(expected, regValue, "RegSet_SetExtCtrlVol_1 CH0 1100mV");
}

void Test_RegSet_SetExtCtrlVol_2()
{
    BYTE buffer[256] = { 0 };

    FpgaRegLogic::RegSet_SetExtCtrlVol_2(0, 4096, buffer);

    USHORT regValue = FpgaRegLogic::Reg_Read(FPGAREG_DAC_DAT_CH3, buffer);
    USHORT expected = FpgaRegLogic::CalcExtCtrlVolRegValue(4096);
    TEST_ASSERT_EQ(expected, regValue, "RegSet_SetExtCtrlVol_2 CH0 4096mV");
}

/*******************************************************************************
* Test: RegSet_UpdateGainValue / OffsetValue / ExtCtrlVol (trigger registers)
*******************************************************************************/
void Test_RegSet_UpdateGainValue()
{
    BYTE buffer[256] = { 0 };

    FpgaRegLogic::RegSet_UpdateGainValue(buffer);

    USHORT regValue = FpgaRegLogic::Reg_Read(FPGAREG_GAIN_TRG, buffer);
    TEST_ASSERT_EQ(1, regValue, "RegSet_UpdateGainValue writes 1");
}

void Test_RegSet_UpdateOffsetValue()
{
    BYTE buffer[256] = { 0 };

    FpgaRegLogic::RegSet_UpdateOffsetValue(buffer);

    USHORT regValue = FpgaRegLogic::Reg_Read(FPGAREG_OFFSET_TRG, buffer);
    TEST_ASSERT_EQ(1, regValue, "RegSet_UpdateOffsetValue writes 1");
}

void Test_RegSet_UpdateExtCtrlVol()
{
    BYTE buffer[256] = { 0 };

    FpgaRegLogic::RegSet_UpdateExtCtrlVol(buffer);

    USHORT regValue = FpgaRegLogic::Reg_Read(FPGAREG_DAC_TRG, buffer);
    TEST_ASSERT_EQ(1, regValue, "RegSet_UpdateExtCtrlVol writes 1");
}

/*******************************************************************************
* Test: RegSet_SelectFirFilterFC
*******************************************************************************/
void Test_RegSet_SelectFirFilterFC_15MHz()
{
    BYTE buffer[256] = { 0 };

    FpgaRegLogic::RegSet_SelectFirFilterFC(0, buffer);

    USHORT regValue = FpgaRegLogic::Reg_Read(FPGAREG_FILTER_SEL, buffer);
    TEST_ASSERT_EQ(0, regValue, "FirFilterFC 15MHz -> 0");
}

void Test_RegSet_SelectFirFilterFC_25MHz()
{
    BYTE buffer[256] = { 0 };

    FpgaRegLogic::RegSet_SelectFirFilterFC(1, buffer);

    USHORT regValue = FpgaRegLogic::Reg_Read(FPGAREG_FILTER_SEL, buffer);
    TEST_ASSERT_EQ(1, regValue, "FirFilterFC 25MHz -> 1");
}

void Test_RegSet_SelectFirFilterFC_NonZero()
{
    BYTE buffer[256] = { 0 };

    // 0以外は全て1になることを確認
    FpgaRegLogic::RegSet_SelectFirFilterFC(5, buffer);

    USHORT regValue = FpgaRegLogic::Reg_Read(FPGAREG_FILTER_SEL, buffer);
    TEST_ASSERT_EQ(1, regValue, "FirFilterFC nonzero -> 1");
}

/*******************************************************************************
* Test: BuildChSelectBitmask
*******************************************************************************/
void Test_BuildChSelectBitmask_None()
{
    UCHAR chSelect[13] = { 0 };

    USHORT result = FpgaRegLogic::BuildChSelectBitmask(chSelect);

    TEST_ASSERT_EQ(0x0000, result, "ChSelect none -> 0");
}

void Test_BuildChSelectBitmask_All()
{
    UCHAR chSelect[13] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };

    USHORT result = FpgaRegLogic::BuildChSelectBitmask(chSelect);

    TEST_ASSERT_EQ(0x1FFF, result, "ChSelect all -> 0x1FFF");
}

void Test_BuildChSelectBitmask_Ch1Only()
{
    UCHAR chSelect[13] = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    USHORT result = FpgaRegLogic::BuildChSelectBitmask(chSelect);

    TEST_ASSERT_EQ(0x0001, result, "ChSelect CH1 only -> 0x0001");
}

void Test_BuildChSelectBitmask_Ch13Only()
{
    UCHAR chSelect[13] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };

    USHORT result = FpgaRegLogic::BuildChSelectBitmask(chSelect);

    TEST_ASSERT_EQ(0x1000, result, "ChSelect CH13 only -> 0x1000");
}

void Test_BuildChSelectBitmask_EvenChannels()
{
    UCHAR chSelect[13] = { 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0 };

    USHORT result = FpgaRegLogic::BuildChSelectBitmask(chSelect);

    // bits 1,3,5,7,9,11 = 0x0AAA
    TEST_ASSERT_EQ(0x0AAA, result, "ChSelect even channels -> 0x0AAA");
}

/*******************************************************************************
* Test: RegSet_SelectDataCH (buffer integration)
*******************************************************************************/
void Test_RegSet_SelectDataCH()
{
    BYTE buffer[256] = { 0 };
    UCHAR chSelect[13] = { 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };

    FpgaRegLogic::RegSet_SelectDataCH(chSelect, buffer);

    USHORT regValue = FpgaRegLogic::Reg_Read(FPGAREG_DAT_CH_SEL, buffer);
    // bit0, bit2, bit12 = 0x1005
    TEST_ASSERT_EQ(0x1005, regValue, "RegSet_SelectDataCH CH1,3,13");
}

/*******************************************************************************
* Test: BuildTrgChBitmask
*******************************************************************************/
void Test_BuildTrgChBitmask_Ch1()
{
    USHORT result = FpgaRegLogic::BuildTrgChBitmask(1);
    TEST_ASSERT_EQ(0x0001, result, "TrgCh 1 -> 0x0001");
}

void Test_BuildTrgChBitmask_Ch13()
{
    USHORT result = FpgaRegLogic::BuildTrgChBitmask(13);
    TEST_ASSERT_EQ(0x1000, result, "TrgCh 13 -> 0x1000");
}

void Test_BuildTrgChBitmask_Ch7()
{
    USHORT result = FpgaRegLogic::BuildTrgChBitmask(7);
    TEST_ASSERT_EQ(0x0040, result, "TrgCh 7 -> 0x0040");
}

void Test_BuildTrgChBitmask_InvalidZero()
{
    // TRGCH=0 は無効入力 -> 0を返す（ガード条件）
    USHORT result = FpgaRegLogic::BuildTrgChBitmask(0);
    TEST_ASSERT_EQ(0x0000, result, "TrgCh 0 (invalid) -> 0x0000");
}

void Test_BuildTrgChBitmask_InvalidOver13()
{
    // TRGCH=14 は無効入力 -> 0を返す（ガード条件）
    USHORT result = FpgaRegLogic::BuildTrgChBitmask(14);
    TEST_ASSERT_EQ(0x0000, result, "TrgCh 14 (invalid) -> 0x0000");
}

/*******************************************************************************
* Test: RegSet_SelectTRGCH (buffer integration)
*******************************************************************************/
void Test_RegSet_SelectTRGCH()
{
    BYTE buffer[256] = { 0 };

    FpgaRegLogic::RegSet_SelectTRGCH(5, buffer);

    USHORT regValue = FpgaRegLogic::Reg_Read(FPGAREG_TRG_SEL, buffer);
    TEST_ASSERT_EQ(0x0010, regValue, "RegSet_SelectTRGCH CH5 -> 0x0010");
}

/*******************************************************************************
* Test: CalcTrgValueRegValue
*******************************************************************************/
void Test_CalcTrgValueRegValue_Zero()
{
    USHORT result = FpgaRegLogic::CalcTrgValueRegValue(0);
    // 0 / (2000/16383) + 0.5 = 0.5 -> (USHORT)0
    TEST_ASSERT_EQ(0, result, "CalcTrgValue 0mV -> 0");
}

void Test_CalcTrgValueRegValue_1800()
{
    USHORT result = FpgaRegLogic::CalcTrgValueRegValue(1800);
    USHORT expected = (USHORT)(1800.0 / (2000.0 / 16383.0) + 0.5);
    TEST_ASSERT_EQ(expected, result, "CalcTrgValue 1800mV");
}

void Test_CalcTrgValueRegValue_900()
{
    USHORT result = FpgaRegLogic::CalcTrgValueRegValue(900);
    USHORT expected = (USHORT)(900.0 / (2000.0 / 16383.0) + 0.5);
    TEST_ASSERT_EQ(expected, result, "CalcTrgValue 900mV");
}

/*******************************************************************************
* Test: RegSet_SetTRGValue (buffer integration)
*******************************************************************************/
void Test_RegSet_SetTRGValue()
{
    BYTE buffer[256] = { 0 };

    FpgaRegLogic::RegSet_SetTRGValue(1000, buffer);

    USHORT regValue = FpgaRegLogic::Reg_Read(FPGAREG_TRG_THR, buffer);
    USHORT expected = FpgaRegLogic::CalcTrgValueRegValue(1000);
    TEST_ASSERT_EQ(expected, regValue, "RegSet_SetTRGValue 1000mV");
}

/*******************************************************************************
* Test: CalcTrgRangeSamples
*******************************************************************************/
void Test_CalcTrgRangeSamples_Zero()
{
    USHORT result = FpgaRegLogic::CalcTrgRangeSamples(0.0f);
    TEST_ASSERT_EQ(0, result, "TrgRange 0us -> 0 samples");
}

void Test_CalcTrgRangeSamples_10us()
{
    USHORT result = FpgaRegLogic::CalcTrgRangeSamples(10.0f);
    TEST_ASSERT_EQ(400, result, "TrgRange 10us -> 400 samples");
}

void Test_CalcTrgRangeSamples_55us()
{
    USHORT result = FpgaRegLogic::CalcTrgRangeSamples(55.0f);
    TEST_ASSERT_EQ(2200, result, "TrgRange 55us -> 2200 samples");
}

void Test_CalcTrgRangeSamples_0_5us()
{
    USHORT result = FpgaRegLogic::CalcTrgRangeSamples(0.5f);
    TEST_ASSERT_EQ(20, result, "TrgRange 0.5us -> 20 samples");
}

/*******************************************************************************
* Test: RegSet_SetTRGRange (buffer integration)
*******************************************************************************/
void Test_RegSet_SetTRGRange_N()
{
    BYTE buffer[256] = { 0 };

    FpgaRegLogic::RegSet_SetTRGRange(0, 25.0f, buffer);

    USHORT regValue = FpgaRegLogic::Reg_Read(FPGAREG_TRG_RANGE_N, buffer);
    TEST_ASSERT_EQ(1000, regValue, "TRGRange N 25us -> 1000");
}

void Test_RegSet_SetTRGRange_P()
{
    BYTE buffer[256] = { 0 };

    FpgaRegLogic::RegSet_SetTRGRange(1, 30.0f, buffer);

    USHORT regValue = FpgaRegLogic::Reg_Read(FPGAREG_TRG_RANGE_P, buffer);
    TEST_ASSERT_EQ(1200, regValue, "TRGRange P 30us -> 1200");
}

/*******************************************************************************
* Test: RegSet_SelectGetDataMeas
*******************************************************************************/
void Test_RegSet_SelectGetDataMeas_Auto()
{
    BYTE buffer[256] = { 0xFF };

    FpgaRegLogic::RegSet_SelectGetDataMeas(0, buffer);

    USHORT regValue = FpgaRegLogic::Reg_Read(FPGAREG_MEAS_MODE, buffer);
    TEST_ASSERT_EQ(0, regValue, "MeasMode Auto -> 0");
}

void Test_RegSet_SelectGetDataMeas_Manual()
{
    BYTE buffer[256] = { 0 };

    FpgaRegLogic::RegSet_SelectGetDataMeas(1, buffer);

    USHORT regValue = FpgaRegLogic::Reg_Read(FPGAREG_MEAS_MODE, buffer);
    TEST_ASSERT_EQ(1, regValue, "MeasMode Manual -> 1");
}

/*******************************************************************************
* Test: RegSet_GetWaveDataStart
*******************************************************************************/
void Test_RegSet_GetWaveDataStart_Start()
{
    BYTE buffer[256] = { 0 };

    FpgaRegLogic::RegSet_GetWaveDataStart(TRUE, buffer);

    USHORT regValue = FpgaRegLogic::Reg_Read(FPGAREG_MANUAL_MEAS_ON, buffer);
    TEST_ASSERT_EQ(1, regValue, "WaveDataStart Start -> 1");
}

void Test_RegSet_GetWaveDataStart_Stop()
{
    BYTE buffer[256] = { 0xFF };

    FpgaRegLogic::RegSet_GetWaveDataStart(FALSE, buffer);

    USHORT regValue = FpgaRegLogic::Reg_Read(FPGAREG_MANUAL_MEAS_ON, buffer);
    TEST_ASSERT_EQ(0, regValue, "WaveDataStart Stop -> 0");
}

/*******************************************************************************
* Test: RegGet_DDRWaveCnt
*******************************************************************************/
void Test_RegGet_DDRWaveCnt_Zero()
{
    BYTE buffer[256] = { 0 };

    ULONG result = FpgaRegLogic::RegGet_DDRWaveCnt(buffer);

    TEST_ASSERT_EQ(0, (INT)result, "DDRWaveCnt zero");
}

void Test_RegGet_DDRWaveCnt_KnownValue()
{
    BYTE buffer[256] = { 0 };

    // Set WAVE_WR_CNT_H = 0x0001, WAVE_WR_CNT_L = 0x0200
    // Expected: 0x00010200 = 66048
    FpgaRegLogic::Reg_Write(FPGAREG_WAVE_WR_CNT_H, 0x0001, buffer);
    FpgaRegLogic::Reg_Write(FPGAREG_WAVE_WR_CNT_L, 0x0200, buffer);

    ULONG result = FpgaRegLogic::RegGet_DDRWaveCnt(buffer);

    TEST_ASSERT_EQ(66048, (INT)result, "DDRWaveCnt 0x00010200");
}

void Test_RegGet_DDRWaveCnt_HighOnly()
{
    BYTE buffer[256] = { 0 };

    FpgaRegLogic::Reg_Write(FPGAREG_WAVE_WR_CNT_H, 0x00FF, buffer);
    FpgaRegLogic::Reg_Write(FPGAREG_WAVE_WR_CNT_L, 0x0000, buffer);

    ULONG result = FpgaRegLogic::RegGet_DDRWaveCnt(buffer);

    TEST_ASSERT_EQ(0x00FF0000, (INT)result, "DDRWaveCnt high only");
}

/*******************************************************************************
* Test: RegGet_DDRReadCnt
*******************************************************************************/
void Test_RegGet_DDRReadCnt_Zero()
{
    BYTE buffer[256] = { 0 };

    ULONG result = FpgaRegLogic::RegGet_DDRReadCnt(buffer);

    TEST_ASSERT_EQ(0, (INT)result, "DDRReadCnt zero");
}

void Test_RegGet_DDRReadCnt_KnownValue()
{
    BYTE buffer[256] = { 0 };

    FpgaRegLogic::Reg_Write(FPGAREG_WAVE_RD_CNT_H, 0x0002, buffer);
    FpgaRegLogic::Reg_Write(FPGAREG_WAVE_RD_CNT_L, 0x0304, buffer);

    ULONG result = FpgaRegLogic::RegGet_DDRReadCnt(buffer);

    TEST_ASSERT_EQ(0x00020304, (INT)result, "DDRReadCnt 0x00020304");
}

/*******************************************************************************
* Test: RegGet_DDRWriteEnd
*******************************************************************************/
void Test_RegGet_DDRWriteEnd_NotDone()
{
    BYTE buffer[256] = { 0 };

    // FPGA_ST に bit2 が立っていない
    FpgaRegLogic::Reg_Write(FPGAREG_FPGA_ST, 0x0000, buffer);

    INT result = FpgaRegLogic::RegGet_DDRWriteEnd(buffer);

    TEST_ASSERT_EQ(0, result, "DDRWriteEnd not done");
}

void Test_RegGet_DDRWriteEnd_Done()
{
    BYTE buffer[256] = { 0 };

    // FPGA_ST に bit2 が立っている (0x04)
    FpgaRegLogic::Reg_Write(FPGAREG_FPGA_ST, 0x0004, buffer);

    INT result = FpgaRegLogic::RegGet_DDRWriteEnd(buffer);

    TEST_ASSERT_EQ(1, result, "DDRWriteEnd done");
}

void Test_RegGet_DDRWriteEnd_OtherBitsSet()
{
    BYTE buffer[256] = { 0 };

    // bit2以外も立っている
    FpgaRegLogic::Reg_Write(FPGAREG_FPGA_ST, 0x00FF, buffer);

    INT result = FpgaRegLogic::RegGet_DDRWriteEnd(buffer);

    TEST_ASSERT_EQ(1, result, "DDRWriteEnd with other bits");
}

void Test_RegGet_DDRWriteEnd_OnlyBit3()
{
    BYTE buffer[256] = { 0 };

    // bit3だけ立っている (bit2は立っていない)
    FpgaRegLogic::Reg_Write(FPGAREG_FPGA_ST, 0x0008, buffer);

    INT result = FpgaRegLogic::RegGet_DDRWriteEnd(buffer);

    TEST_ASSERT_EQ(0, result, "DDRWriteEnd bit3 only -> not done");
}

/*******************************************************************************
* Test: RegGet_DDRReadEnd
*******************************************************************************/
void Test_RegGet_DDRReadEnd_NotDone()
{
    BYTE buffer[256] = { 0 };

    FpgaRegLogic::Reg_Write(FPGAREG_FPGA_ST, 0x0000, buffer);

    INT result = FpgaRegLogic::RegGet_DDRReadEnd(buffer);

    TEST_ASSERT_EQ(0, result, "DDRReadEnd not done");
}

void Test_RegGet_DDRReadEnd_Done()
{
    BYTE buffer[256] = { 0 };

    FpgaRegLogic::Reg_Write(FPGAREG_FPGA_ST, 0x0008, buffer);

    INT result = FpgaRegLogic::RegGet_DDRReadEnd(buffer);

    TEST_ASSERT_EQ(1, result, "DDRReadEnd done");
}

/*******************************************************************************
* Test: RegGet_SampleStartSt
*******************************************************************************/
void Test_RegGet_SampleStartSt_NotStarted()
{
    BYTE buffer[256] = { 0 };

    FpgaRegLogic::Reg_Write(FPGAREG_FPGA_ST, 0x0000, buffer);

    int result = FpgaRegLogic::RegGet_SampleStartSt(buffer);

    TEST_ASSERT_EQ(FALSE, result, "SampleStartSt not started");
}

void Test_RegGet_SampleStartSt_Started()
{
    BYTE buffer[256] = { 0 };

    // bit4 (0x10) が立っている
    FpgaRegLogic::Reg_Write(FPGAREG_FPGA_ST, 0x0010, buffer);

    int result = FpgaRegLogic::RegGet_SampleStartSt(buffer);

    TEST_ASSERT_EQ(TRUE, result, "SampleStartSt started");
}

void Test_RegGet_SampleStartSt_OtherBits()
{
    BYTE buffer[256] = { 0 };

    // bit4以外のビット
    FpgaRegLogic::Reg_Write(FPGAREG_FPGA_ST, 0x000F, buffer);

    int result = FpgaRegLogic::RegGet_SampleStartSt(buffer);

    TEST_ASSERT_EQ(FALSE, result, "SampleStartSt other bits only");
}

/*******************************************************************************
* Test: CalcGain3RegValue
*******************************************************************************/
void Test_CalcGain3RegValue_Minus0_5()
{
    // Gain3 = -0.5 -> dData = (-0.5 - (-0.5)) / accuracy + 0.5 = 0 + 0.5 = 0.5
    // (USHORT)0.5 = 0, + 0x200 = 0x200 = 512
    USHORT result = FpgaRegLogic::CalcGain3RegValue(-0.5);
    TEST_ASSERT_EQ(0x200, result, "CalcGain3 -0.5 -> 0x200");
}

void Test_CalcGain3RegValue_Minus1_0()
{
    // Gain3 = -1.0 -> dData = (-0.5 - (-1.0)) / (0.5/511) + 0.5 = 0.5 / (0.5/511) + 0.5
    //       = 0.5 * 511 / 0.5 + 0.5 = 511 + 0.5 = 511.5
    // (USHORT)511.5 = 511, + 0x200 = 511 + 512 = 1023 = 0x3FF
    USHORT result = FpgaRegLogic::CalcGain3RegValue(-1.0);
    TEST_ASSERT_EQ(0x3FF, result, "CalcGain3 -1.0 -> 0x3FF");
}

void Test_CalcGain3RegValue_Minus0_75()
{
    // Gain3 = -0.75 -> dData = (-0.5 - (-0.75)) / (0.5/511) + 0.5 = 0.25/(0.5/511) + 0.5
    //       = 0.25 * 511 / 0.5 + 0.5 = 255.5 + 0.5 = 256.0
    // (USHORT)256 + 0x200 = 256 + 512 = 768 = 0x300
    USHORT result = FpgaRegLogic::CalcGain3RegValue(-0.75);
    TEST_ASSERT_EQ(0x300, result, "CalcGain3 -0.75 -> 0x300");
}

/*******************************************************************************
* strGainMultp lookup table (Dialog1_Main のコンストラクタの初期値と同一)
*******************************************************************************/
static double g_strGainMultp[13][5][2];

void InitGainMultpTable()
{
    // CH1~8 (Low freq channels): 同じパターン
    for (int ch = 0; ch < 8; ch++)
    {
        g_strGainMultp[ch][0][0] = -0.2;  // Gain1-1
        g_strGainMultp[ch][0][1] = -0.4;  // Gain1-2
        g_strGainMultp[ch][1][0] = -1.0;  // Gain2-1
        g_strGainMultp[ch][1][1] = -4.0;  // Gain2-2
        g_strGainMultp[ch][2][0] = -0.5;  // Gain3-1
        g_strGainMultp[ch][2][1] = -1.0;  // Gain3-2
        g_strGainMultp[ch][3][0] = 2.0;   // Gain4-1
        g_strGainMultp[ch][3][1] = 4.0;   // Gain4-2
        g_strGainMultp[ch][4][0] = -1.35; // Gain5-1
        g_strGainMultp[ch][4][1] = -1.35; // Gain5-2
    }

    // CH9~12 (High freq channels)
    for (int ch = 8; ch < 12; ch++)
    {
        g_strGainMultp[ch][0][0] = -0.5;  // Gain1-1
        g_strGainMultp[ch][0][1] = -2.0;  // Gain1-2
        g_strGainMultp[ch][1][0] = -1.0;  // Gain2-1
        g_strGainMultp[ch][1][1] = -2.0;  // Gain2-2
        g_strGainMultp[ch][2][0] = 1.0;   // Gain3-1
        g_strGainMultp[ch][2][1] = 1.0;   // Gain3-2
        g_strGainMultp[ch][3][0] = 1.0;   // Gain4-1
        g_strGainMultp[ch][3][1] = 1.0;   // Gain4-2
        g_strGainMultp[ch][4][0] = 1.0;   // Gain5-1
        g_strGainMultp[ch][4][1] = 1.0;   // Gain5-2
    }

    // CH13
    g_strGainMultp[12][0][0] = -0.5;  // Gain1-1
    g_strGainMultp[12][0][1] = -2.0;  // Gain1-2
    g_strGainMultp[12][1][0] = -1.0;  // Gain2-1
    g_strGainMultp[12][1][1] = -2.0;  // Gain2-2
    g_strGainMultp[12][2][0] = -1.0;  // Gain3-1
    g_strGainMultp[12][2][1] = -1.0;  // Gain3-2
    g_strGainMultp[12][3][0] = 1.0;   // Gain4-1
    g_strGainMultp[12][3][1] = 1.0;   // Gain4-2
    g_strGainMultp[12][4][0] = 1.0;   // Gain5-1
    g_strGainMultp[12][4][1] = 1.0;   // Gain5-2
}

/*******************************************************************************
* Test: BuildGainSwitchGroup - Gain1/2 ビットエンコーディング
*******************************************************************************/
void Test_BuildGainSwitchGroup_AllOption0()
{
    // CH1~4 全て Gain1=option0(-0.2), Gain2=option0(-1.0) -> 全bit=0
    double gainValues[13][5] = { 0 };
    for (int ch = 0; ch < 4; ch++)
    {
        gainValues[ch][0] = g_strGainMultp[ch][0][0]; // -0.2
        gainValues[ch][1] = g_strGainMultp[ch][1][0]; // -1.0
    }

    USHORT result = FpgaRegLogic::BuildGainSwitchGroup(gainValues, g_strGainMultp, 0, 4);

    // 全bit0 -> 0x0000
    TEST_ASSERT_EQ(0x0000, result, "GainSwitch group0 all option0");
}

void Test_BuildGainSwitchGroup_AllOption1()
{
    // CH1~4 全て Gain1=option1(-0.4), Gain2=option1(-4.0) -> 全bit=1
    double gainValues[13][5] = { 0 };
    for (int ch = 0; ch < 4; ch++)
    {
        gainValues[ch][0] = g_strGainMultp[ch][0][1]; // -0.4
        gainValues[ch][1] = g_strGainMultp[ch][1][1]; // -4.0
    }

    USHORT result = FpgaRegLogic::BuildGainSwitchGroup(gainValues, g_strGainMultp, 0, 4);

    // j=1(Gain2) -> bit1, j=0(Gain1) -> bit0
    // 各CH: usTemp = (1<<1)|1 = 0x3
    // i=3->0: usData = 0x3333
    TEST_ASSERT_EQ(0x3333, result, "GainSwitch group0 all option1");
}

void Test_BuildGainSwitchGroup_Ch1OnlySwitched()
{
    // CH1: Gain1=option1(-0.4), Gain2=option1(-4.0)
    // CH2~4: option0
    double gainValues[13][5] = { 0 };
    gainValues[0][0] = g_strGainMultp[0][0][1]; // -0.4
    gainValues[0][1] = g_strGainMultp[0][1][1]; // -4.0
    for (int ch = 1; ch < 4; ch++)
    {
        gainValues[ch][0] = g_strGainMultp[ch][0][0];
        gainValues[ch][1] = g_strGainMultp[ch][1][0];
    }

    USHORT result = FpgaRegLogic::BuildGainSwitchGroup(gainValues, g_strGainMultp, 0, 4);

    // CH1(i=0): usTemp=0x3, CH2~4(i=1,2,3): usTemp=0x0
    // i=3: usData = 0x0, i=2: 0x00, i=1: 0x00, i=0: 0x03
    // => 0x0003
    TEST_ASSERT_EQ(0x0003, result, "GainSwitch CH1 only switched");
}

void Test_BuildGainSwitchGroup_Gain1Only()
{
    // CH1: Gain1=option1(-0.4), Gain2=option0(-1.0)
    double gainValues[13][5] = { 0 };
    gainValues[0][0] = g_strGainMultp[0][0][1]; // -0.4 (option1)
    gainValues[0][1] = g_strGainMultp[0][1][0]; // -1.0 (option0)
    for (int ch = 1; ch < 4; ch++)
    {
        gainValues[ch][0] = g_strGainMultp[ch][0][0];
        gainValues[ch][1] = g_strGainMultp[ch][1][0];
    }

    USHORT result = FpgaRegLogic::BuildGainSwitchGroup(gainValues, g_strGainMultp, 0, 4);

    // j=1(Gain2)=0, j=0(Gain1)=1 -> usTemp = (0<<1)|1 = 0x01
    TEST_ASSERT_EQ(0x0001, result, "GainSwitch CH1 Gain1 only");
}

void Test_BuildGainSwitchGroup_Gain2Only()
{
    // CH1: Gain1=option0(-0.2), Gain2=option1(-4.0)
    double gainValues[13][5] = { 0 };
    gainValues[0][0] = g_strGainMultp[0][0][0]; // -0.2 (option0)
    gainValues[0][1] = g_strGainMultp[0][1][1]; // -4.0 (option1)
    for (int ch = 1; ch < 4; ch++)
    {
        gainValues[ch][0] = g_strGainMultp[ch][0][0];
        gainValues[ch][1] = g_strGainMultp[ch][1][0];
    }

    USHORT result = FpgaRegLogic::BuildGainSwitchGroup(gainValues, g_strGainMultp, 0, 4);

    // j=1(Gain2)=1, j=0(Gain1)=0 -> usTemp = (1<<1)|0 = 0x02
    TEST_ASSERT_EQ(0x0002, result, "GainSwitch CH1 Gain2 only");
}

void Test_BuildGainSwitchGroup_HighFreqCh9to12()
{
    // CH9~12: Gain1=option1(-2.0), Gain2=option1(-2.0)
    double gainValues[13][5] = { 0 };
    for (int ch = 8; ch < 12; ch++)
    {
        gainValues[ch][0] = g_strGainMultp[ch][0][1]; // -2.0
        gainValues[ch][1] = g_strGainMultp[ch][1][1]; // -2.0
    }

    USHORT result = FpgaRegLogic::BuildGainSwitchGroup(gainValues, g_strGainMultp, 8, 4);

    TEST_ASSERT_EQ(0x3333, result, "GainSwitch group2 (CH9-12) all option1");
}

void Test_BuildGainSwitchCh13_Option0()
{
    double gainValues[5] = { 0 };
    gainValues[0] = g_strGainMultp[12][0][0]; // -0.5
    gainValues[1] = g_strGainMultp[12][1][0]; // -1.0

    USHORT result = FpgaRegLogic::BuildGainSwitchCh13(gainValues, g_strGainMultp[12]);

    TEST_ASSERT_EQ(0x0000, result, "GainSwitch CH13 option0");
}

void Test_BuildGainSwitchCh13_Option1()
{
    double gainValues[5] = { 0 };
    gainValues[0] = g_strGainMultp[12][0][1]; // -2.0
    gainValues[1] = g_strGainMultp[12][1][1]; // -2.0

    USHORT result = FpgaRegLogic::BuildGainSwitchCh13(gainValues, g_strGainMultp[12]);

    // j=1: 1, j=0: 1 -> usTemp = (1<<1)|1 = 0x03
    TEST_ASSERT_EQ(0x0003, result, "GainSwitch CH13 option1");
}

void Test_BuildGainSwitchGroup_InvalidValueFallback()
{
    // Given: CH1 Gain1 has an invalid value that matches neither option.
    double gainValues[13][5] = { 0 };
    gainValues[0][0] = -9.9;                   // invalid for Gain1
    gainValues[0][1] = g_strGainMultp[0][1][1]; // valid option1 for Gain2
    for (int ch = 1; ch < 4; ch++)
    {
        gainValues[ch][0] = g_strGainMultp[ch][0][0];
        gainValues[ch][1] = g_strGainMultp[ch][1][0];
    }

    // When: Building switch bitmask for group CH1-4.
    USHORT result = FpgaRegLogic::BuildGainSwitchGroup(gainValues, g_strGainMultp, 0, 4);

    // Then: invalid branch falls back to bit 0 for Gain1 (CH1 nibble = 0x2).
    TEST_ASSERT_EQ(0x0002, result, "GainSwitch invalid Gain1 fallback to 0");
}

void Test_BuildGainSwitchCh13_InvalidValueFallback()
{
    // Given: CH13 Gain1 has an invalid value, Gain2 is a valid option1.
    double gainValues[5] = { 0 };
    gainValues[0] = -9.9;               // invalid for Gain1
    gainValues[1] = g_strGainMultp[12][1][1]; // valid option1 for Gain2

    // When: Building CH13 switch bitmask.
    USHORT result = FpgaRegLogic::BuildGainSwitchCh13(gainValues, g_strGainMultp[12]);

    // Then: invalid branch falls back to bit 0 for Gain1 (result = 0x2).
    TEST_ASSERT_EQ(0x0002, result, "GainSwitch CH13 invalid Gain1 fallback to 0");
}

/*******************************************************************************
* Test: Reg_Write バイトレベル検証 (全レジスタアドレスの正確性)
*******************************************************************************/
void Test_ByteLevel_OffsetValue_AllChannels()
{
    BYTE buffer[256] = { 0 };

    // 13チャンネル分のOffsetを設定、各アドレスのバイト列を1つずつ検証
    FLOAT offsets[13] = { 1414.0f, 1420.0f, 1430.0f, 1440.0f, 1450.0f, 1454.0f, 1460.0f,
                          1470.0f, 1480.0f, 1490.0f, 1494.0f, 1454.0f, 1414.0f };

    for (int i = 0; i < 13; i++)
    {
        FpgaRegLogic::RegSet_SetOffsetValue(i, offsets[i], buffer);
    }

    // 各チャンネルのレジスタ値を検証
    for (int i = 0; i < 13; i++)
    {
        UINT addr = FPGAREG_OFFSET_DAT_CH1 + (i * 2);
        USHORT regVal = FpgaRegLogic::Reg_Read(addr, buffer);
        USHORT expected = FpgaRegLogic::CalcOffsetRegValue(offsets[i]);

        // バイト単位で検証
        BYTE expectedLow = expected & 0xFF;
        BYTE expectedHigh = (expected >> 8) & 0xFF;

        char msg[128];
        sprintf(msg, "Offset CH%d addr=0x%X low byte", i + 1, addr);
        TEST_ASSERT_EQ(expectedLow, buffer[addr], msg);
        sprintf(msg, "Offset CH%d addr=0x%X high byte", i + 1, addr);
        TEST_ASSERT_EQ(expectedHigh, buffer[addr + 1], msg);
    }
}

void Test_ByteLevel_ExtCtrlVol1_AllChannels()
{
    BYTE buffer[256] = { 0 };
    USHORT vols[5] = { 0, 500, 1000, 1100, 5000 };

    for (int i = 0; i < 5; i++)
    {
        FpgaRegLogic::RegSet_SetExtCtrlVol_1(i, vols[i], buffer);
    }

    for (int i = 0; i < 5; i++)
    {
        UINT addr = FPGAREG_DAC_DAT_CH9 + (i * 2);
        USHORT regVal = FpgaRegLogic::Reg_Read(addr, buffer);
        USHORT expected = FpgaRegLogic::CalcExtCtrlVolRegValue(vols[i]);

        BYTE expectedLow = expected & 0xFF;
        BYTE expectedHigh = (expected >> 8) & 0xFF;

        char msg[128];
        sprintf(msg, "ExtCtrlVol1 CH%d addr=0x%X low", i, addr);
        TEST_ASSERT_EQ(expectedLow, buffer[addr], msg);
        sprintf(msg, "ExtCtrlVol1 CH%d addr=0x%X high", i, addr);
        TEST_ASSERT_EQ(expectedHigh, buffer[addr + 1], msg);
    }
}

void Test_ByteLevel_ExtCtrlVol2_AllChannels()
{
    BYTE buffer[256] = { 0 };
    USHORT vols[6] = { 0, 1000, 2000, 3000, 4000, 4096 };

    for (int i = 0; i < 6; i++)
    {
        FpgaRegLogic::RegSet_SetExtCtrlVol_2(i, vols[i], buffer);
    }

    for (int i = 0; i < 6; i++)
    {
        UINT addr = FPGAREG_DAC_DAT_CH3 + (i * 2);
        USHORT regVal = FpgaRegLogic::Reg_Read(addr, buffer);
        USHORT expected = FpgaRegLogic::CalcExtCtrlVolRegValue(vols[i]);

        BYTE expectedLow = expected & 0xFF;
        BYTE expectedHigh = (expected >> 8) & 0xFF;

        char msg[128];
        sprintf(msg, "ExtCtrlVol2 CH%d addr=0x%X low", i, addr);
        TEST_ASSERT_EQ(expectedLow, buffer[addr], msg);
        sprintf(msg, "ExtCtrlVol2 CH%d addr=0x%X high", i, addr);
        TEST_ASSERT_EQ(expectedHigh, buffer[addr + 1], msg);
    }
}

void Test_ByteLevel_Gain3_AllChannels()
{
    BYTE buffer[256] = { 0 };
    double gain3Vals[8] = { -0.5, -0.55, -0.6, -0.7, -0.75, -0.8, -0.9, -1.0 };

    for (int i = 0; i < 8; i++)
    {
        USHORT usData = FpgaRegLogic::CalcGain3RegValue(gain3Vals[i]);
        FpgaRegLogic::Reg_Write((UINT)(FPGAREG_GAIN_DAT_CH1 + (i * 2)), usData, buffer);
    }

    for (int i = 0; i < 8; i++)
    {
        UINT addr = FPGAREG_GAIN_DAT_CH1 + (i * 2);
        USHORT expected = FpgaRegLogic::CalcGain3RegValue(gain3Vals[i]);
        USHORT actual = FpgaRegLogic::Reg_Read(addr, buffer);

        char msg[128];
        sprintf(msg, "Gain3 CH%d (%.2f) expected=0x%04X", i + 1, gain3Vals[i], expected);
        TEST_ASSERT_EQ(expected, actual, msg);

        // バイト単位
        sprintf(msg, "Gain3 CH%d low byte", i + 1);
        TEST_ASSERT_EQ(expected & 0xFF, buffer[addr], msg);
        sprintf(msg, "Gain3 CH%d high byte", i + 1);
        TEST_ASSERT_EQ((expected >> 8) & 0xFF, buffer[addr + 1], msg);
    }
}

/*******************************************************************************
* Test: Full EP2 Buffer Integration - 典型的なパラメータセットでバッファ全体を検証
*******************************************************************************/
void Test_FullEp2Buffer_TypicalConfig()
{
    BYTE ep2Buf[256] = { 0 };

    FpgaRegLogic::FpgaConfig config = {};

    // CH1~8: Low freq default gains
    for (int ch = 0; ch < 8; ch++)
    {
        config.GainCh[ch][0] = -0.2;  // Gain1 option0
        config.GainCh[ch][1] = -1.0;  // Gain2 option0
        config.GainCh[ch][2] = -0.75; // Gain3 中間値
        config.GainCh[ch][3] = 2.0;   // Gain4 option0
        config.GainCh[ch][4] = -1.35; // Gain5
    }

    // CH9~12: High freq default gains
    for (int ch = 8; ch < 12; ch++)
    {
        config.GainCh[ch][0] = -0.5;  // Gain1 option0
        config.GainCh[ch][1] = -1.0;  // Gain2 option0
        config.GainCh[ch][2] = 1.0;   // Gain3
        config.GainCh[ch][3] = 1.0;   // Gain4
        config.GainCh[ch][4] = 1.0;   // Gain5
    }

    // CH13
    config.GainCh[12][0] = -0.5;
    config.GainCh[12][1] = -1.0;
    config.GainCh[12][2] = -1.0;
    config.GainCh[12][3] = 1.0;
    config.GainCh[12][4] = 1.0;

    // Offset: 全CH 1454.0mV
    for (int i = 0; i < 13; i++) config.OffsetValue[i] = 1454.0f;

    // ExtCtrlVol1 (CH9~13)
    config.ExtCtrlVol1[0] = 500;
    config.ExtCtrlVol1[1] = 600;
    config.ExtCtrlVol1[2] = 700;
    config.ExtCtrlVol1[3] = 800;
    config.ExtCtrlVol1[4] = 3000;

    // ExtCtrlVol2 (CH3~8)
    for (int i = 0; i < 6; i++) config.ExtCtrlVol2[i] = 2000;

    config.FirFilterFC = 0;  // 15MHz/40Msps
    config.CHSelect[0] = 1; config.CHSelect[1] = 1; config.CHSelect[8] = 1; // CH1, CH2, CH9
    for (int i = 2; i < 8; i++) config.CHSelect[i] = 0;
    for (int i = 9; i < 13; i++) config.CHSelect[i] = 0;
    config.TriggerCh = 1;
    config.TriggerValue = 900;
    config.TriggerRange[0] = 10.0f;
    config.TriggerRange[1] = 20.0f;
    config.ManualMode = 1;

    FpgaRegLogic::BuildFullEp2Buffer(config, g_strGainMultp, ep2Buf);

    // === 全レジスタのバイト列を検証 ===

    // 1) Gain3 (CH1~8, addr 0x20~0x2E)
    for (int i = 0; i < 8; i++)
    {
        UINT addr = FPGAREG_GAIN_DAT_CH1 + (i * 2);
        USHORT expected = FpgaRegLogic::CalcGain3RegValue(-0.75);
        USHORT actual = FpgaRegLogic::Reg_Read(addr, ep2Buf);
        char msg[128];
        sprintf(msg, "Full: Gain3 CH%d", i + 1);
        TEST_ASSERT_EQ(expected, actual, msg);
    }

    // 2) Gain switch group0 (CH1~4, addr 0x40)
    //    All option0 -> 0x0000
    USHORT gainSw0 = FpgaRegLogic::Reg_Read(FPGAREG_GAIN_SW_CH1_4, ep2Buf);
    TEST_ASSERT_EQ(0x0000, gainSw0, "Full: GainSW group0 all option0");

    // 3) Gain switch group1 (CH5~8, addr 0x42)
    USHORT gainSw1 = FpgaRegLogic::Reg_Read(FPGAREG_GAIN_SW_CH1_4 + 2, ep2Buf);
    TEST_ASSERT_EQ(0x0000, gainSw1, "Full: GainSW group1 all option0");

    // 4) Gain switch group2 (CH9~12, addr 0x44)
    USHORT gainSw2 = FpgaRegLogic::Reg_Read(FPGAREG_GAIN_SW_CH1_4 + 4, ep2Buf);
    TEST_ASSERT_EQ(0x0000, gainSw2, "Full: GainSW group2 all option0");

    // 5) Gain switch CH13 (addr 0x46)
    USHORT gainSw3 = FpgaRegLogic::Reg_Read(FPGAREG_GAIN_SW_CH1_4 + 6, ep2Buf);
    TEST_ASSERT_EQ(0x0000, gainSw3, "Full: GainSW CH13 option0");

    // 6) Gain trigger (addr 0x50) = 1
    USHORT gainTrg = FpgaRegLogic::Reg_Read(FPGAREG_GAIN_TRG, ep2Buf);
    TEST_ASSERT_EQ(1, gainTrg, "Full: Gain trigger = 1");

    // 7) Offset (addr 0x60~0x78)
    USHORT expectedOffset = FpgaRegLogic::CalcOffsetRegValue(1454.0f);
    for (int i = 0; i < 13; i++)
    {
        UINT addr = FPGAREG_OFFSET_DAT_CH1 + (i * 2);
        USHORT actual = FpgaRegLogic::Reg_Read(addr, ep2Buf);
        char msg[128];
        sprintf(msg, "Full: Offset CH%d", i + 1);
        TEST_ASSERT_EQ(expectedOffset, actual, msg);
    }

    // 8) Offset trigger (addr 0x80) = 1
    USHORT offsetTrg = FpgaRegLogic::Reg_Read(FPGAREG_OFFSET_TRG, ep2Buf);
    TEST_ASSERT_EQ(1, offsetTrg, "Full: Offset trigger = 1");

    // 9) ExtCtrlVol1 (addr 0x9C~0xA4)
    USHORT expectedVol1[5];
    expectedVol1[0] = FpgaRegLogic::CalcExtCtrlVolRegValue(500);
    expectedVol1[1] = FpgaRegLogic::CalcExtCtrlVolRegValue(600);
    expectedVol1[2] = FpgaRegLogic::CalcExtCtrlVolRegValue(700);
    expectedVol1[3] = FpgaRegLogic::CalcExtCtrlVolRegValue(800);
    expectedVol1[4] = FpgaRegLogic::CalcExtCtrlVolRegValue(3000);
    for (int i = 0; i < 5; i++)
    {
        UINT addr = FPGAREG_DAC_DAT_CH9 + (i * 2);
        USHORT actual = FpgaRegLogic::Reg_Read(addr, ep2Buf);
        char msg[128];
        sprintf(msg, "Full: ExtCtrlVol1[%d]", i);
        TEST_ASSERT_EQ(expectedVol1[i], actual, msg);
    }

    // 10) DAC trigger (addr 0xB0) = 1
    USHORT dacTrg = FpgaRegLogic::Reg_Read(FPGAREG_DAC_TRG, ep2Buf);
    TEST_ASSERT_EQ(1, dacTrg, "Full: DAC trigger = 1");

    // 11) ExtCtrlVol2 (addr 0x90~0x9A)
    USHORT expectedVol2 = FpgaRegLogic::CalcExtCtrlVolRegValue(2000);
    for (int i = 0; i < 6; i++)
    {
        UINT addr = FPGAREG_DAC_DAT_CH3 + (i * 2);
        USHORT actual = FpgaRegLogic::Reg_Read(addr, ep2Buf);
        char msg[128];
        sprintf(msg, "Full: ExtCtrlVol2[%d]", i);
        TEST_ASSERT_EQ(expectedVol2, actual, msg);
    }

    // 12) FIR Filter (addr 0x14) = 0
    USHORT fir = FpgaRegLogic::Reg_Read(FPGAREG_FILTER_SEL, ep2Buf);
    TEST_ASSERT_EQ(0, fir, "Full: FIR filter = 0 (15MHz)");

    // 13) Data CH select (addr 0x06) = CH1,CH2,CH9 = bit0|bit1|bit8 = 0x0103
    USHORT chSel = FpgaRegLogic::Reg_Read(FPGAREG_DAT_CH_SEL, ep2Buf);
    TEST_ASSERT_EQ(0x0103, chSel, "Full: CH select = 0x0103");

    // 14) Trigger CH (addr 0x08) = CH1 -> bit0 = 0x0001
    USHORT trgCh = FpgaRegLogic::Reg_Read(FPGAREG_TRG_SEL, ep2Buf);
    TEST_ASSERT_EQ(0x0001, trgCh, "Full: TRG CH = 0x0001");

    // 15) Trigger value (addr 0x0A)
    USHORT trgVal = FpgaRegLogic::Reg_Read(FPGAREG_TRG_THR, ep2Buf);
    USHORT expectedTrgVal = FpgaRegLogic::CalcTrgValueRegValue(900);
    TEST_ASSERT_EQ(expectedTrgVal, trgVal, "Full: TRG value");

    // 16) Trigger range N (addr 0x0C) = 10us * 40 = 400
    USHORT trgN = FpgaRegLogic::Reg_Read(FPGAREG_TRG_RANGE_N, ep2Buf);
    TEST_ASSERT_EQ(400, trgN, "Full: TRG range N = 400");

    // 17) Trigger range P (addr 0x0E) = 20us * 40 = 800
    USHORT trgP = FpgaRegLogic::Reg_Read(FPGAREG_TRG_RANGE_P, ep2Buf);
    TEST_ASSERT_EQ(800, trgP, "Full: TRG range P = 800");

    // 18) Meas mode (addr 0x10) = Manual = 1
    USHORT meas = FpgaRegLogic::Reg_Read(FPGAREG_MEAS_MODE, ep2Buf);
    TEST_ASSERT_EQ(1, meas, "Full: Meas mode = 1 (Manual)");

    // 19) バイト列のゼロでないアドレスが正しいことを確認
    // 未使用領域が0のままであることを確認
    TEST_ASSERT_EQ(0x00, ep2Buf[0x00], "Full: unused addr 0x00 = 0");
    TEST_ASSERT_EQ(0x00, ep2Buf[0x01], "Full: unused addr 0x01 = 0");
    TEST_ASSERT_EQ(0x00, ep2Buf[0x02], "Full: unused addr 0x02 = 0");
    TEST_ASSERT_EQ(0x00, ep2Buf[0x03], "Full: unused addr 0x03 = 0");
    // 0x04~0x05 = FPGA_ST (not written in this flow, should be 0)
    TEST_ASSERT_EQ(0x00, ep2Buf[0x04], "Full: FPGA_ST addr 0x04 = 0");
    TEST_ASSERT_EQ(0x00, ep2Buf[0x05], "Full: FPGA_ST addr 0x05 = 0");
    // 0x16~0x17 = unused between FILTER_SEL and WAVE_WR_CNT_L
    TEST_ASSERT_EQ(0x00, ep2Buf[0x16], "Full: unused addr 0x16 = 0");
    TEST_ASSERT_EQ(0x00, ep2Buf[0x17], "Full: unused addr 0x17 = 0");
}

void Test_FullEp2Buffer_GainSwitched()
{
    BYTE ep2Buf[256] = { 0 };

    FpgaRegLogic::FpgaConfig config = {};

    // CH1~4: Gain1=option1(-0.4), Gain2=option1(-4.0) -> all bits 1
    for (int ch = 0; ch < 4; ch++)
    {
        config.GainCh[ch][0] = -0.4;  // option1
        config.GainCh[ch][1] = -4.0;  // option1
        config.GainCh[ch][2] = -0.5;  // Gain3
        config.GainCh[ch][3] = 4.0;   // Gain4 option1
        config.GainCh[ch][4] = -1.35;
    }
    // CH5~8: Gain1=option0(-0.2), Gain2=option0(-1.0) -> all bits 0
    for (int ch = 4; ch < 8; ch++)
    {
        config.GainCh[ch][0] = -0.2;
        config.GainCh[ch][1] = -1.0;
        config.GainCh[ch][2] = -1.0;
        config.GainCh[ch][3] = 2.0;
        config.GainCh[ch][4] = -1.35;
    }
    // CH9~12: Gain1=option1(-2.0), Gain2=option0(-1.0)
    for (int ch = 8; ch < 12; ch++)
    {
        config.GainCh[ch][0] = -2.0;  // option1
        config.GainCh[ch][1] = -1.0;  // option0
        config.GainCh[ch][2] = 1.0;
        config.GainCh[ch][3] = 1.0;
        config.GainCh[ch][4] = 1.0;
    }
    // CH13: option1
    config.GainCh[12][0] = -2.0;
    config.GainCh[12][1] = -2.0;
    config.GainCh[12][2] = -1.0;
    config.GainCh[12][3] = 1.0;
    config.GainCh[12][4] = 1.0;

    for (int i = 0; i < 13; i++) config.OffsetValue[i] = 1454.0f;
    for (int i = 0; i < 5; i++) config.ExtCtrlVol1[i] = 0;
    for (int i = 0; i < 6; i++) config.ExtCtrlVol2[i] = 0;
    config.FirFilterFC = 1;
    for (int i = 0; i < 13; i++) config.CHSelect[i] = 1;
    config.TriggerCh = 13;
    config.TriggerValue = 1800;
    config.TriggerRange[0] = 55.0f;
    config.TriggerRange[1] = 55.0f;
    config.ManualMode = 0;

    FpgaRegLogic::BuildFullEp2Buffer(config, g_strGainMultp, ep2Buf);

    // Gain switch group0: CH1~4 all option1 = 0x3333
    USHORT sw0 = FpgaRegLogic::Reg_Read(FPGAREG_GAIN_SW_CH1_4, ep2Buf);
    TEST_ASSERT_EQ(0x3333, sw0, "Full2: GainSW group0 = 0x3333");

    // Gain switch group1: CH5~8 all option0 = 0x0000
    USHORT sw1 = FpgaRegLogic::Reg_Read(FPGAREG_GAIN_SW_CH1_4 + 2, ep2Buf);
    TEST_ASSERT_EQ(0x0000, sw1, "Full2: GainSW group1 = 0x0000");

    // Gain switch group2: CH9~12 Gain1=1, Gain2=0 -> each 0x01
    // j=1(Gain2)=0, j=0(Gain1)=1 -> usTemp=0x01
    // 4 channels: 0x1111
    USHORT sw2 = FpgaRegLogic::Reg_Read(FPGAREG_GAIN_SW_CH1_4 + 4, ep2Buf);
    TEST_ASSERT_EQ(0x1111, sw2, "Full2: GainSW group2 = 0x1111");

    // CH13: both option1 = 0x03
    USHORT sw3 = FpgaRegLogic::Reg_Read(FPGAREG_GAIN_SW_CH1_4 + 6, ep2Buf);
    TEST_ASSERT_EQ(0x0003, sw3, "Full2: GainSW CH13 = 0x0003");

    // FIR filter = 1
    USHORT fir = FpgaRegLogic::Reg_Read(FPGAREG_FILTER_SEL, ep2Buf);
    TEST_ASSERT_EQ(1, fir, "Full2: FIR = 1 (25MHz)");

    // CH select all = 0x1FFF
    USHORT chSel = FpgaRegLogic::Reg_Read(FPGAREG_DAT_CH_SEL, ep2Buf);
    TEST_ASSERT_EQ(0x1FFF, chSel, "Full2: CH select all = 0x1FFF");

    // Trigger CH13 = bit12 = 0x1000
    USHORT trgCh = FpgaRegLogic::Reg_Read(FPGAREG_TRG_SEL, ep2Buf);
    TEST_ASSERT_EQ(0x1000, trgCh, "Full2: TRG CH13 = 0x1000");

    // Trigger value 1800mV
    USHORT trgVal = FpgaRegLogic::Reg_Read(FPGAREG_TRG_THR, ep2Buf);
    TEST_ASSERT_EQ(FpgaRegLogic::CalcTrgValueRegValue(1800), trgVal, "Full2: TRG value 1800mV");

    // Trigger range N = 55 * 40 = 2200
    USHORT trgN = FpgaRegLogic::Reg_Read(FPGAREG_TRG_RANGE_N, ep2Buf);
    TEST_ASSERT_EQ(2200, trgN, "Full2: TRG range N = 2200");

    // Trigger range P = 55 * 40 = 2200
    USHORT trgP = FpgaRegLogic::Reg_Read(FPGAREG_TRG_RANGE_P, ep2Buf);
    TEST_ASSERT_EQ(2200, trgP, "Full2: TRG range P = 2200");

    // Meas mode Auto = 0
    USHORT meas = FpgaRegLogic::Reg_Read(FPGAREG_MEAS_MODE, ep2Buf);
    TEST_ASSERT_EQ(0, meas, "Full2: Meas Auto = 0");

    // Gain3 CH1~4 = -0.5 -> 0x200
    for (int i = 0; i < 4; i++)
    {
        USHORT g3 = FpgaRegLogic::Reg_Read(FPGAREG_GAIN_DAT_CH1 + (i * 2), ep2Buf);
        char msg[64];
        sprintf(msg, "Full2: Gain3 CH%d = 0x200", i + 1);
        TEST_ASSERT_EQ(0x200, g3, msg);
    }

    // Gain3 CH5~8 = -1.0 -> 0x3FF
    for (int i = 4; i < 8; i++)
    {
        USHORT g3 = FpgaRegLogic::Reg_Read(FPGAREG_GAIN_DAT_CH1 + (i * 2), ep2Buf);
        char msg[64];
        sprintf(msg, "Full2: Gain3 CH%d = 0x3FF", i + 1);
        TEST_ASSERT_EQ(0x3FF, g3, msg);
    }
}

/*******************************************************************************
* Test: CalcOffsetRegValue 精密境界値テスト
*******************************************************************************/
void Test_CalcOffsetRegValue_BoundaryValues()
{
    // 入力の刻みによるレジスタ値の変化を確認
    // accuracy = 80.0/255.0 ≈ 0.3137
    // 1 step = accuracy ≈ 0.3137mV

    // 1414.0 -> 255
    TEST_ASSERT_EQ(255, FpgaRegLogic::CalcOffsetRegValue(1414.0f), "Offset boundary 1414.0");
    // 1414.0 + accuracy = 1414.3137... -> (0.3137/0.3137 + 0.5) = 1.5 -> (USHORT)1 -> 254
    // ただしFLOAT精度の影響あり

    // 1494.0 -> 0
    TEST_ASSERT_EQ(0, FpgaRegLogic::CalcOffsetRegValue(1494.0f), "Offset boundary 1494.0");

    // 値が同一入力なら常に同じ出力 (冪等性)
    for (FLOAT v = 1414.0f; v <= 1494.0f; v += 1.0f)
    {
        USHORT r1 = FpgaRegLogic::CalcOffsetRegValue(v);
        USHORT r2 = FpgaRegLogic::CalcOffsetRegValue(v);
        char msg[64];
        sprintf(msg, "Offset idempotent %.0f", v);
        TEST_ASSERT_EQ(r1, r2, msg);
    }
}

/*******************************************************************************
* Test: CalcTrgValueRegValue 精密境界値テスト
*******************************************************************************/
void Test_CalcTrgValueRegValue_BoundaryValues()
{
    // 0mV -> 0
    TEST_ASSERT_EQ(0, FpgaRegLogic::CalcTrgValueRegValue(0), "TrgVal boundary 0");

    // 2000mV -> 16383 (full scale)
    TEST_ASSERT_EQ(16383, FpgaRegLogic::CalcTrgValueRegValue(2000), "TrgVal boundary 2000");

    // 1000mV -> 中間値
    USHORT mid = FpgaRegLogic::CalcTrgValueRegValue(1000);
    // 1000 / (2000/16383) + 0.5 = 1000 * 16383 / 2000 + 0.5 = 8191.5 + 0.5 = 8192
    TEST_ASSERT_EQ(8192, mid, "TrgVal boundary 1000");

    // 冪等性
    for (USHORT v = 0; v <= 1800; v += 100)
    {
        USHORT r1 = FpgaRegLogic::CalcTrgValueRegValue(v);
        USHORT r2 = FpgaRegLogic::CalcTrgValueRegValue(v);
        char msg[64];
        sprintf(msg, "TrgVal idempotent %d", v);
        TEST_ASSERT_EQ(r1, r2, msg);
    }
}

/*******************************************************************************
* Test: CalcExtCtrlVolRegValue 精密境界値テスト
*******************************************************************************/
void Test_CalcExtCtrlVolRegValue_BoundaryValues()
{
    // 0mV -> 0
    TEST_ASSERT_EQ(0, FpgaRegLogic::CalcExtCtrlVolRegValue(0), "ExtCtrl boundary 0");

    // 5000mV -> 65535
    USHORT maxVol = FpgaRegLogic::CalcExtCtrlVolRegValue(5000);
    // 5000 / (5000/65535) + 0.5 = 65535 + 0.5 = 65535.5 -> (USHORT)65535
    TEST_ASSERT_EQ(65535, maxVol, "ExtCtrl boundary 5000");

    // 2500mV -> 中間: 2500/(5000/65535) + 0.5 = 32767.5 + 0.5 = 32768.0 -> (USHORT)32768
    // 実際は浮動小数点の丸めで 32767 になる
    USHORT midVol = FpgaRegLogic::CalcExtCtrlVolRegValue(2500);
    USHORT expectedMid = (USHORT)(2500.0 / (5000.0 / 65535.0) + 0.5);
    TEST_ASSERT_EQ(expectedMid, midVol, "ExtCtrl boundary 2500");

    // 冪等性
    for (USHORT v = 0; v <= 5000; v += 500)
    {
        USHORT r1 = FpgaRegLogic::CalcExtCtrlVolRegValue(v);
        USHORT r2 = FpgaRegLogic::CalcExtCtrlVolRegValue(v);
        char msg[64];
        sprintf(msg, "ExtCtrl idempotent %d", v);
        TEST_ASSERT_EQ(r1, r2, msg);
    }
}

/*******************************************************************************
* Test: CalcGain3RegValue 精密境界値テスト
*******************************************************************************/
void Test_CalcGain3RegValue_BoundaryValues()
{
    // -0.5 -> 0x200 (最小)
    TEST_ASSERT_EQ(0x200, FpgaRegLogic::CalcGain3RegValue(-0.5), "Gain3 boundary -0.5");

    // -1.0 -> 0x3FF (最大)
    TEST_ASSERT_EQ(0x3FF, FpgaRegLogic::CalcGain3RegValue(-1.0), "Gain3 boundary -1.0");

    // -0.75 -> 0x300 (中間)
    TEST_ASSERT_EQ(0x300, FpgaRegLogic::CalcGain3RegValue(-0.75), "Gain3 boundary -0.75");

    // 冪等性: 同じ入力で常に同じ出力
    for (double v = -1.0; v <= -0.5; v += 0.05)
    {
        USHORT r1 = FpgaRegLogic::CalcGain3RegValue(v);
        USHORT r2 = FpgaRegLogic::CalcGain3RegValue(v);
        char msg[64];
        sprintf(msg, "Gain3 idempotent %.2f", v);
        TEST_ASSERT_EQ(r1, r2, msg);
    }

    // 範囲: 0x200 <= result <= 0x3FF
    for (double v = -1.0; v <= -0.5; v += 0.01)
    {
        USHORT r = FpgaRegLogic::CalcGain3RegValue(v);
        char msg[64];
        sprintf(msg, "Gain3 range check %.2f (result=0x%X)", v, r);
        TEST_ASSERT(r >= 0x200 && r <= 0x3FF, msg);
    }
}

/*******************************************************************************
* Main
*******************************************************************************/
int main()
{
    printf("=== FpgaRegisterLogic Unit Tests ===\n\n");

    InitGainMultpTable();

    // Reg_Write
    RUN_TEST(Test_Reg_Write_Basic);
    RUN_TEST(Test_Reg_Write_Zero);
    RUN_TEST(Test_Reg_Write_MaxValue);
    RUN_TEST(Test_Reg_Write_DoesNotCorruptAdjacent);

    // Reg_Read
    RUN_TEST(Test_Reg_Read_Basic);
    RUN_TEST(Test_Reg_Read_Zero);
    RUN_TEST(Test_Reg_Read_MaxValue);

    // Roundtrip
    RUN_TEST(Test_Reg_WriteRead_Roundtrip);

    // CalcOffsetRegValue
    RUN_TEST(Test_CalcOffsetRegValue_Min);
    RUN_TEST(Test_CalcOffsetRegValue_Max);
    RUN_TEST(Test_CalcOffsetRegValue_Mid);

    // RegSet_SetOffsetValue
    RUN_TEST(Test_RegSet_SetOffsetValue);

    // CalcExtCtrlVolRegValue
    RUN_TEST(Test_CalcExtCtrlVolRegValue_Zero);
    RUN_TEST(Test_CalcExtCtrlVolRegValue_1100);
    RUN_TEST(Test_CalcExtCtrlVolRegValue_5000);

    // RegSet_SetExtCtrlVol
    RUN_TEST(Test_RegSet_SetExtCtrlVol_1);
    RUN_TEST(Test_RegSet_SetExtCtrlVol_2);

    // Update triggers
    RUN_TEST(Test_RegSet_UpdateGainValue);
    RUN_TEST(Test_RegSet_UpdateOffsetValue);
    RUN_TEST(Test_RegSet_UpdateExtCtrlVol);

    // FirFilterFC
    RUN_TEST(Test_RegSet_SelectFirFilterFC_15MHz);
    RUN_TEST(Test_RegSet_SelectFirFilterFC_25MHz);
    RUN_TEST(Test_RegSet_SelectFirFilterFC_NonZero);

    // BuildChSelectBitmask
    RUN_TEST(Test_BuildChSelectBitmask_None);
    RUN_TEST(Test_BuildChSelectBitmask_All);
    RUN_TEST(Test_BuildChSelectBitmask_Ch1Only);
    RUN_TEST(Test_BuildChSelectBitmask_Ch13Only);
    RUN_TEST(Test_BuildChSelectBitmask_EvenChannels);

    // RegSet_SelectDataCH
    RUN_TEST(Test_RegSet_SelectDataCH);

    // BuildTrgChBitmask
    RUN_TEST(Test_BuildTrgChBitmask_Ch1);
    RUN_TEST(Test_BuildTrgChBitmask_Ch13);
    RUN_TEST(Test_BuildTrgChBitmask_Ch7);
    RUN_TEST(Test_BuildTrgChBitmask_InvalidZero);
    RUN_TEST(Test_BuildTrgChBitmask_InvalidOver13);

    // RegSet_SelectTRGCH
    RUN_TEST(Test_RegSet_SelectTRGCH);

    // CalcTrgValueRegValue
    RUN_TEST(Test_CalcTrgValueRegValue_Zero);
    RUN_TEST(Test_CalcTrgValueRegValue_1800);
    RUN_TEST(Test_CalcTrgValueRegValue_900);

    // RegSet_SetTRGValue
    RUN_TEST(Test_RegSet_SetTRGValue);

    // CalcTrgRangeSamples
    RUN_TEST(Test_CalcTrgRangeSamples_Zero);
    RUN_TEST(Test_CalcTrgRangeSamples_10us);
    RUN_TEST(Test_CalcTrgRangeSamples_55us);
    RUN_TEST(Test_CalcTrgRangeSamples_0_5us);

    // RegSet_SetTRGRange
    RUN_TEST(Test_RegSet_SetTRGRange_N);
    RUN_TEST(Test_RegSet_SetTRGRange_P);

    // SelectGetDataMeas
    RUN_TEST(Test_RegSet_SelectGetDataMeas_Auto);
    RUN_TEST(Test_RegSet_SelectGetDataMeas_Manual);

    // GetWaveDataStart
    RUN_TEST(Test_RegSet_GetWaveDataStart_Start);
    RUN_TEST(Test_RegSet_GetWaveDataStart_Stop);

    // DDRWaveCnt
    RUN_TEST(Test_RegGet_DDRWaveCnt_Zero);
    RUN_TEST(Test_RegGet_DDRWaveCnt_KnownValue);
    RUN_TEST(Test_RegGet_DDRWaveCnt_HighOnly);
    RUN_TEST(Test_RegGet_DDRReadCnt_Zero);
    RUN_TEST(Test_RegGet_DDRReadCnt_KnownValue);

    // DDRWriteEnd
    RUN_TEST(Test_RegGet_DDRWriteEnd_NotDone);
    RUN_TEST(Test_RegGet_DDRWriteEnd_Done);
    RUN_TEST(Test_RegGet_DDRWriteEnd_OtherBitsSet);
    RUN_TEST(Test_RegGet_DDRWriteEnd_OnlyBit3);
    RUN_TEST(Test_RegGet_DDRReadEnd_NotDone);
    RUN_TEST(Test_RegGet_DDRReadEnd_Done);

    // SampleStartSt
    RUN_TEST(Test_RegGet_SampleStartSt_NotStarted);
    RUN_TEST(Test_RegGet_SampleStartSt_Started);
    RUN_TEST(Test_RegGet_SampleStartSt_OtherBits);

    // CalcGain3RegValue
    RUN_TEST(Test_CalcGain3RegValue_Minus0_5);
    RUN_TEST(Test_CalcGain3RegValue_Minus1_0);
    RUN_TEST(Test_CalcGain3RegValue_Minus0_75);

    // BuildGainSwitchGroup
    RUN_TEST(Test_BuildGainSwitchGroup_AllOption0);
    RUN_TEST(Test_BuildGainSwitchGroup_AllOption1);
    RUN_TEST(Test_BuildGainSwitchGroup_Ch1OnlySwitched);
    RUN_TEST(Test_BuildGainSwitchGroup_Gain1Only);
    RUN_TEST(Test_BuildGainSwitchGroup_Gain2Only);
    RUN_TEST(Test_BuildGainSwitchGroup_HighFreqCh9to12);
    RUN_TEST(Test_BuildGainSwitchCh13_Option0);
    RUN_TEST(Test_BuildGainSwitchCh13_Option1);
    RUN_TEST(Test_BuildGainSwitchGroup_InvalidValueFallback);
    RUN_TEST(Test_BuildGainSwitchCh13_InvalidValueFallback);

    // Byte-level verification
    RUN_TEST(Test_ByteLevel_OffsetValue_AllChannels);
    RUN_TEST(Test_ByteLevel_ExtCtrlVol1_AllChannels);
    RUN_TEST(Test_ByteLevel_ExtCtrlVol2_AllChannels);
    RUN_TEST(Test_ByteLevel_Gain3_AllChannels);

    // Full EP2 buffer integration
    RUN_TEST(Test_FullEp2Buffer_TypicalConfig);
    RUN_TEST(Test_FullEp2Buffer_GainSwitched);

    // Precision boundary tests
    RUN_TEST(Test_CalcOffsetRegValue_BoundaryValues);
    RUN_TEST(Test_CalcTrgValueRegValue_BoundaryValues);
    RUN_TEST(Test_CalcExtCtrlVolRegValue_BoundaryValues);
    RUN_TEST(Test_CalcGain3RegValue_BoundaryValues);

    // Summary
    printf("\n=== Results: %d tests, %d passed, %d failed ===\n", g_TestCount, g_PassCount, g_FailCount);

    return g_FailCount > 0 ? 1 : 0;
}
