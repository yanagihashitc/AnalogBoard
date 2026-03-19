# FpgaRegisterLogic Test Perspectives

Source: `AnalogBoard_UnitTest/FpgaRegisterLogic_test.cpp`

Total tests: 86

## Register Read / Write Basics

| Case ID / Source Test | Input / Precondition | Perspective (Equivalence / Boundary) | Expected Result | Notes |
|---|---|---|---|---|
| `Test_Reg_Write_Basic` | normal register address and value `0x1234` | Equivalence – normal | low/high bytes are written to adjacent buffer positions | basic register serialization |
| `Test_Reg_Write_Zero` | register value `0x0000` | Boundary – zero | both output bytes become `0x00` | lower boundary |
| `Test_Reg_Write_MaxValue` | register value `0xFFFF` | Boundary – max | both output bytes become `0xFF` | upper boundary |
| `Test_Reg_Write_DoesNotCorruptAdjacent` | target register surrounded by sentinel bytes | Equivalence – no side effect | bytes before/after target remain unchanged | adjacency safety |
| `Test_Reg_Read_Basic` | buffer contains low/high bytes for `0x1234` | Equivalence – normal | register read reconstructs original value | inverse of `Reg_Write` |
| `Test_Reg_Read_Zero` | buffer contains all zero | Boundary – zero | read result is `0x0000` | lower boundary |
| `Test_Reg_Read_MaxValue` | buffer contains all ones at target | Boundary – max | read result is `0xFFFF` | upper boundary |
| `Test_Reg_WriteRead_Roundtrip` | same buffer used for write then read | Equivalence – roundtrip | read value matches written value | read/write consistency |

## Offset / External Control / Update Trigger Logic

| Case ID / Source Test | Input / Precondition | Perspective (Equivalence / Boundary) | Expected Result | Notes |
|---|---|---|---|---|
| `Test_CalcOffsetRegValue_Min` | minimum supported offset input | Boundary – min | converted register value matches minimum mapping | analog offset lower bound |
| `Test_CalcOffsetRegValue_Max` | maximum supported offset input | Boundary – max | converted register value matches maximum mapping | analog offset upper bound |
| `Test_CalcOffsetRegValue_Mid` | mid-range offset input | Equivalence – nominal | converted register value matches expected midpoint mapping | representative normal case |
| `Test_RegSet_SetOffsetValue` | channel config contains offset settings | Equivalence – register set | offset register bytes are updated in EP2 buffer | write-through to packet buffer |
| `Test_CalcExtCtrlVolRegValue_Zero` | external control voltage `0` | Boundary – zero | register value encodes zero correctly | lower boundary |
| `Test_CalcExtCtrlVolRegValue_1100` | representative nominal voltage `1100` | Equivalence – nominal | register value matches expected scaling | typical operating point |
| `Test_CalcExtCtrlVolRegValue_5000` | high-end voltage `5000` | Boundary – upper range | register value matches expected scaling | upper operating boundary |
| `Test_RegSet_SetExtCtrlVol_1` | first external control voltage path selected | Equivalence – register set | corresponding EP2 buffer bytes are updated | path 1 write path |
| `Test_RegSet_SetExtCtrlVol_2` | second external control voltage path selected | Equivalence – register set | corresponding EP2 buffer bytes are updated | path 2 write path |
| `Test_RegSet_UpdateGainValue` | gain setting changes inside config | Equivalence – update trigger | gain-related register bytes are refreshed | update path coverage |
| `Test_RegSet_UpdateOffsetValue` | offset setting changes inside config | Equivalence – update trigger | offset-related register bytes are refreshed | update path coverage |
| `Test_RegSet_UpdateExtCtrlVol` | ext control voltage changes inside config | Equivalence – update trigger | ext-control register bytes are refreshed | update path coverage |
| `Test_RegSet_SelectFirFilterFC_15MHz` | filter selection set to `15MHz` | Equivalence – nominal selection | FIR filter register reflects 15MHz selection | discrete option |
| `Test_RegSet_SelectFirFilterFC_25MHz` | filter selection set to `25MHz` | Equivalence – nominal selection | FIR filter register reflects 25MHz selection | discrete option |
| `Test_RegSet_SelectFirFilterFC_NonZero` | filter selection uses non-zero alternative input | Boundary – non-default branch | non-default filter branch is selected consistently | branch coverage for selector |

## Channel / Trigger Selection and Value Conversion

| Case ID / Source Test | Input / Precondition | Perspective (Equivalence / Boundary) | Expected Result | Notes |
|---|---|---|---|---|
| `Test_BuildChSelectBitmask_None` | no data channels selected | Boundary – empty selection | bitmask is zero | lower selection boundary |
| `Test_BuildChSelectBitmask_All` | all channels selected | Boundary – full selection | bitmask contains all supported bits | upper selection boundary |
| `Test_BuildChSelectBitmask_Ch1Only` | only channel 1 selected | Equivalence – single channel | bitmask contains channel 1 bit only | representative single selection |
| `Test_BuildChSelectBitmask_Ch13Only` | only channel 13 selected | Equivalence – edge channel | bitmask contains channel 13 bit only | upper channel edge |
| `Test_BuildChSelectBitmask_EvenChannels` | only even channels selected | Equivalence – sparse multi-select | bitmask reflects alternating pattern | multi-channel composition |
| `Test_RegSet_SelectDataCH` | channel selection config ready for packet build | Equivalence – register set | EP2 buffer receives data-channel bitmask | integration with packet writer |
| `Test_BuildTrgChBitmask_Ch1` | trigger channel is 1 | Equivalence – nominal | trigger bitmask selects channel 1 | lower valid trigger channel |
| `Test_BuildTrgChBitmask_Ch13` | trigger channel is 13 | Equivalence – nominal | trigger bitmask selects channel 13 | upper valid trigger channel |
| `Test_BuildTrgChBitmask_Ch7` | trigger channel is 7 | Equivalence – nominal | trigger bitmask selects channel 7 | mid-range valid channel |
| `Test_BuildTrgChBitmask_InvalidZero` | trigger channel is 0 | Boundary – below min | invalid input falls back to safe result | invalid lower bound |
| `Test_BuildTrgChBitmask_InvalidOver13` | trigger channel exceeds 13 | Boundary – above max | invalid input falls back to safe result | invalid upper bound |
| `Test_RegSet_SelectTRGCH` | trigger channel config ready for packet build | Equivalence – register set | EP2 buffer receives trigger-channel bitmask | packet integration |
| `Test_CalcTrgValueRegValue_Zero` | trigger value `0` | Boundary – zero | register value matches zero mapping | lower boundary |
| `Test_CalcTrgValueRegValue_1800` | representative trigger value `1800` | Equivalence – nominal | register value matches expected scaling | typical operating point |
| `Test_CalcTrgValueRegValue_900` | representative trigger value `900` | Equivalence – nominal | register value matches expected scaling | alternate nominal point |
| `Test_RegSet_SetTRGValue` | trigger threshold config ready for packet build | Equivalence – register set | EP2 buffer receives trigger threshold bytes | packet integration |
| `Test_CalcTrgRangeSamples_Zero` | trigger range `0us` | Boundary – zero | sample count converts to zero-range mapping | lower boundary |
| `Test_CalcTrgRangeSamples_10us` | trigger range `10us` | Equivalence – nominal | sample count matches expected conversion | common setting |
| `Test_CalcTrgRangeSamples_55us` | trigger range `55us` | Equivalence – nominal | sample count matches expected conversion | larger nominal setting |
| `Test_CalcTrgRangeSamples_0_5us` | trigger range `0.5us` | Boundary – sub-unit | fractional range converts correctly | precision-sensitive branch |
| `Test_RegSet_SetTRGRange_N` | negative-side trigger range config | Equivalence – register set | negative range bytes are updated | N-side write path |
| `Test_RegSet_SetTRGRange_P` | positive-side trigger range config | Equivalence – register set | positive range bytes are updated | P-side write path |
| `Test_RegSet_SelectGetDataMeas_Auto` | measurement mode is auto | Equivalence – nominal selection | packet buffer selects auto acquisition mode | mode selector |
| `Test_RegSet_SelectGetDataMeas_Manual` | measurement mode is manual | Equivalence – nominal selection | packet buffer selects manual acquisition mode | mode selector |
| `Test_RegSet_GetWaveDataStart_Start` | start flag requested | Equivalence – control command | packet buffer encodes start command | command path |
| `Test_RegSet_GetWaveDataStart_Stop` | stop flag requested | Equivalence – control command | packet buffer encodes stop command | command path |

## DDR / Status Register Decode

| Case ID / Source Test | Input / Precondition | Perspective (Equivalence / Boundary) | Expected Result | Notes |
|---|---|---|---|---|
| `Test_RegGet_DDRWaveCnt_Zero` | DDR write counter bytes are zero | Boundary – zero | decoded write count is zero | lower boundary |
| `Test_RegGet_DDRWaveCnt_KnownValue` | DDR write counter bytes contain known pattern | Equivalence – nominal decode | decoded write count matches expected value | register decode correctness |
| `Test_RegGet_DDRWaveCnt_HighOnly` | only high-order counter bytes are set | Boundary – high-byte emphasis | decoded write count keeps high-order bits | multi-byte decode edge |
| `Test_RegGet_DDRReadCnt_Zero` | DDR read counter bytes are zero | Boundary – zero | decoded read count is zero | lower boundary |
| `Test_RegGet_DDRReadCnt_KnownValue` | DDR read counter bytes contain known pattern | Equivalence – nominal decode | decoded read count matches expected value | register decode correctness |
| `Test_RegGet_DDRWriteEnd_NotDone` | status bit for DDR write end is clear | Equivalence – flag off | function reports not done | status bit decode |
| `Test_RegGet_DDRWriteEnd_Done` | status bit for DDR write end is set | Equivalence – flag on | function reports done | status bit decode |
| `Test_RegGet_DDRWriteEnd_OtherBitsSet` | unrelated status bits are set but write-end bit state is known | Equivalence – masking | unrelated bits do not affect write-end result | bitmask isolation |
| `Test_RegGet_DDRWriteEnd_OnlyBit3` | only neighboring bit is set | Boundary – adjacent bit | write-end result remains false | adjacent-bit guard |
| `Test_RegGet_DDRReadEnd_NotDone` | DDR read-end bit is clear | Equivalence – flag off | function reports not done | status bit decode |
| `Test_RegGet_DDRReadEnd_Done` | DDR read-end bit is set | Equivalence – flag on | function reports done | status bit decode |
| `Test_RegGet_SampleStartSt_NotStarted` | sample-start bit is clear | Equivalence – flag off | function reports not started | measurement state decode |
| `Test_RegGet_SampleStartSt_Started` | sample-start bit is set | Equivalence – flag on | function reports started | measurement state decode |
| `Test_RegGet_SampleStartSt_OtherBits` | unrelated bits are set around sample-start bit | Equivalence – masking | unrelated bits do not change sample-start result | bitmask isolation |

## Gain Conversion and Gain Switch Bitmaps

| Case ID / Source Test | Input / Precondition | Perspective (Equivalence / Boundary) | Expected Result | Notes |
|---|---|---|---|---|
| `Test_CalcGain3RegValue_Minus0_5` | gain3 input `-0.5` | Equivalence – nominal | converted register value matches expected mapping | representative negative gain |
| `Test_CalcGain3RegValue_Minus1_0` | gain3 input `-1.0` | Boundary – lower edge | converted register value matches expected mapping | lower operating edge |
| `Test_CalcGain3RegValue_Minus0_75` | gain3 input `-0.75` | Equivalence – nominal | converted register value matches expected mapping | mid negative gain |
| `Test_BuildGainSwitchGroup_AllOption0` | all group switches set to option 0 | Boundary – all default | group bitmask reflects all-zero/default selection | lower selection boundary |
| `Test_BuildGainSwitchGroup_AllOption1` | all group switches set to option 1 | Boundary – all alternate | group bitmask reflects all-one/alternate selection | upper selection boundary |
| `Test_BuildGainSwitchGroup_Ch1OnlySwitched` | only channel 1 switch differs from default | Equivalence – single deviation | bitmask changes only for channel 1 | localized effect |
| `Test_BuildGainSwitchGroup_Gain1Only` | only gain1 path selected | Equivalence – single group path | bitmask reflects gain1 path only | path isolation |
| `Test_BuildGainSwitchGroup_Gain2Only` | only gain2 path selected | Equivalence – single group path | bitmask reflects gain2 path only | path isolation |
| `Test_BuildGainSwitchGroup_HighFreqCh9to12` | high-frequency channels 9-12 switched | Equivalence – subgroup selection | bitmask reflects high-frequency subgroup | subgroup coverage |
| `Test_BuildGainSwitchCh13_Option0` | channel 13 switch uses option 0 | Equivalence – nominal | channel 13 bitmask matches option 0 | separate CH13 path |
| `Test_BuildGainSwitchCh13_Option1` | channel 13 switch uses option 1 | Equivalence – nominal | channel 13 bitmask matches option 1 | separate CH13 path |
| `Test_BuildGainSwitchGroup_InvalidValueFallback` | group switch input contains invalid value | Boundary – invalid selector | function falls back to safe/default mapping | invalid input handling |
| `Test_BuildGainSwitchCh13_InvalidValueFallback` | channel 13 switch input contains invalid value | Boundary – invalid selector | function falls back to safe/default mapping | invalid input handling |

## Byte-Level Packet Verification / Full Buffer Integration

| Case ID / Source Test | Input / Precondition | Perspective (Equivalence / Boundary) | Expected Result | Notes |
|---|---|---|---|---|
| `Test_ByteLevel_OffsetValue_AllChannels` | all-channel offset settings applied to buffer | Equivalence – byte layout | every channel's offset bytes appear at expected positions | packet layout verification |
| `Test_ByteLevel_ExtCtrlVol1_AllChannels` | all-channel ext control voltage 1 settings applied | Equivalence – byte layout | ext-control-1 bytes match expected layout | packet layout verification |
| `Test_ByteLevel_ExtCtrlVol2_AllChannels` | all-channel ext control voltage 2 settings applied | Equivalence – byte layout | ext-control-2 bytes match expected layout | packet layout verification |
| `Test_ByteLevel_Gain3_AllChannels` | all-channel gain3 settings applied | Equivalence – byte layout | gain3 bytes match expected layout | packet layout verification |
| `Test_FullEp2Buffer_TypicalConfig` | representative full configuration | Equivalence – integration | completed EP2 buffer matches expected composite packet | end-to-end packet composition |
| `Test_FullEp2Buffer_GainSwitched` | representative full configuration with gain switch changes | Equivalence – integration | completed EP2 buffer reflects switched gain settings | integrated variant path |

## Precision Boundary Bundles

| Case ID / Source Test | Input / Precondition | Perspective (Equivalence / Boundary) | Expected Result | Notes |
|---|---|---|---|---|
| `Test_CalcOffsetRegValue_BoundaryValues` | offset conversion tested across boundary set | Boundary – bundled edge cases | boundary conversions remain consistent | compact boundary sweep |
| `Test_CalcTrgValueRegValue_BoundaryValues` | trigger value conversion tested across boundary set | Boundary – bundled edge cases | boundary conversions remain consistent | compact boundary sweep |
| `Test_CalcExtCtrlVolRegValue_BoundaryValues` | ext control voltage conversion tested across boundary set | Boundary – bundled edge cases | boundary conversions remain consistent | compact boundary sweep |
| `Test_CalcGain3RegValue_BoundaryValues` | gain3 conversion tested across boundary set | Boundary – bundled edge cases | boundary conversions remain consistent | compact boundary sweep |
