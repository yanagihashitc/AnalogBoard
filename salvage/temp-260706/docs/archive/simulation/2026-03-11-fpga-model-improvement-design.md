# FPGA Model Improvement Design (Level 1-3)

作成日: 2026-03-11
対象ブランチ: `feature/acquisition-preflight-simulation`

## 背景

- 現在の `FakeUsbSession` は「host が期待する値」を直接書き込んでいる
- FPGA の実際の register エンコーディング経路を再現していないため、host 側 `FpgaRegisterLogic` のデコードバグを検出できない
- FPGA ソースコード (`FPGA_FW/`) の RTL 解析により、以下の改善が実現可能と判明した

## FPGA RTL 解析結果

### WAVE_WR_CNT のエンコーディング経路

```
DDR3_WCTRL                    REG_CTRL                      Host
r_ddr_wr_addr (33bit)         s_WAVE_WR_CNT (37bit)         RegGet_DDRWaveCnt()
  |                             |                              |
  +- -1 -> r_ddr_lim_ddr       +- WAVE_WR_CNT & "00000"      +- H:L -> 32bit ULONG
  |       (DDR_LIM_ADDR)       |   (left shift 5 = x32)       |
  +- 32byte unit address       +- bits[31:0] split to H/L     +- byte unit value
```

Host が読む `WAVE_WR_CNT` = `(r_ddr_wr_addr - 1) x 32` = **total written bytes - 32**

### WAVE_RD_CNT のエンコーディング経路

```
DDR3_RCTRL                    REG_CTRL                      Host
r_ddr_addr (32bit)            s_WAVE_RD_CNT (37bit)         RegGet_DDRReadCnt()
  |                             |                              |
  +- [26:0] & "00000"          +- "00000" & WAVE_RD_CNT      +- H:L -> 32bit ULONG
  |  (AV_MM_ADDR, in bytes)    |   (pass through)             |
  +- byte unit address          +- bits[31:0] split to H/L    +- byte unit value
```

### FPGA_ST bit field (REG_CTRL.vhd lines 987-992)

| Bit | Signal | Meaning |
|-----|--------|---------|
| 0 | `r_DDR_LINK_2FF` | DDR3 link status |
| 1 | `ADC_SET_END` | ADC setup complete |
| 2 | `DDR_WR_END` (= `DDR_WSTOP`) | DDR write complete |
| 3 | `DDR_RD_END` | DDR read complete |
| 4 | `r_MEAS_TRG_2FF` | Measurement trigger status |

### Burst size

- `C_MAX_BURST = 512` (in 32-byte units) = **16,384 bytes = 0x4000**
- This matches `kEp6ReadAlignmentBytes` exactly

### MEAS_CTRL state machine (MEAS_CTRL.vhd)

```
IDLE --(trigger)--> INIT --(10us)--> MEAS --(DDR_FULL or stop)--> WAIT --(500us)--> RD_WAIT --(DDR_RD_END)--> IDLE
```

EP4 register visibility per state:

| State | WAVE_WR_CNT | DDR_WR_END | DDR_RD_END | MEAS_TRG |
|---|---|---|---|---|
| IDLE | 0 | 0 | 0 | 0 |
| INIT | 0 | 0 | 0 | 1 |
| MEAS | increasing | 0 | 0 | 1 |
| WAIT | final | 1 | 0 | 0 |
| RD_WAIT | final | 1 | 0->1 | 0 |

---

## Level 1: Register Encoding Fidelity

### Purpose

Make `FakeUsbSession` encode EP4 registers the same way the FPGA does, rather than reverse-engineering from host expectations. This detects bugs in `FpgaRegisterLogic` decode functions.

### Current problem

```cpp
// Current FakeUsbSession::EP4_GetData (SimulationRunnerCore.cpp:158-184)
ULONG registerWaveWrCnt = 0;
if (producedLogicalBytes_ > 0)
{
    registerWaveWrCnt = producedLogicalBytes_ - kDdrCompletionPaddingBytes;
}
// -> Writes byte value directly via Reg_Write
```

This is "encoding from host's perspective" - if host decode has a bug, FakeUsbSession compensates and the bug is invisible.

### Design

#### 1-1. New file: `Sysmex_AnalogBoard_TestApp/FpgaRegisterEncoding.h`

```cpp
#pragma once
#include <windows.h>
#include "FpgaRegisterAddress.h"
#include "FpgaRegisterLogic.h"

namespace FpgaRegEncoding
{
    // DDR burst address unit = 32 bytes (256-bit)
    constexpr ULONG kDdrAddressUnitBytes = 32u;

    // FPGA_ST bit definitions (from REG_CTRL.vhd lines 987-992)
    constexpr USHORT kFpgaStBitDdrLink   = 0x0001; // bit[0]
    constexpr USHORT kFpgaStBitAdcSetEnd = 0x0002; // bit[1]
    constexpr USHORT kFpgaStBitDdrWrEnd  = 0x0004; // bit[2]
    constexpr USHORT kFpgaStBitDdrRdEnd  = 0x0008; // bit[3]
    constexpr USHORT kFpgaStBitMeasTrg   = 0x0010; // bit[4]

    // Encode WAVE_WR_CNT as FPGA does:
    //   DDR3_WCTRL: DDR_LIM_ADDR = r_ddr_wr_addr - 1
    //   REG_CTRL: s_WAVE_WR_CNT = DDR_LIM_ADDR & "00000" (<<5)
    //   Host reads bits[31:0]
    inline void EncodeWaveWrCnt(ULONG writtenBytes, BYTE* ep4Buffer)
    {
        const ULONG addrUnits = writtenBytes / kDdrAddressUnitBytes;
        const ULONG ddrLimAddr = (addrUnits > 0) ? (addrUnits - 1) : 0;
        const ULONG regValue = ddrLimAddr * kDdrAddressUnitBytes;

        FpgaRegLogic::Reg_Write(FPGAREG_WAVE_WR_CNT_H,
            static_cast<USHORT>((regValue >> 16) & 0xFFFF), ep4Buffer);
        FpgaRegLogic::Reg_Write(FPGAREG_WAVE_WR_CNT_L,
            static_cast<USHORT>(regValue & 0xFFFF), ep4Buffer);
    }

    // Encode WAVE_RD_CNT as FPGA does:
    //   DDR3_RCTRL: AV_MM_ADDR = r_ddr_addr[26:0] & "00000" (already in bytes)
    //   REG_CTRL: passes through directly
    inline void EncodeWaveRdCnt(ULONG readBytes, BYTE* ep4Buffer)
    {
        FpgaRegLogic::Reg_Write(FPGAREG_WAVE_RD_CNT_H,
            static_cast<USHORT>((readBytes >> 16) & 0xFFFF), ep4Buffer);
        FpgaRegLogic::Reg_Write(FPGAREG_WAVE_RD_CNT_L,
            static_cast<USHORT>(readBytes & 0xFFFF), ep4Buffer);
    }

    // Encode FPGA_ST as FPGA does (REG_CTRL.vhd lines 987-992)
    inline void EncodeFpgaSt(
        bool ddrLink, bool adcSetEnd, bool ddrWrEnd,
        bool ddrRdEnd, bool measTrg, BYTE* ep4Buffer)
    {
        USHORT st = 0;
        if (ddrLink)   st |= kFpgaStBitDdrLink;
        if (adcSetEnd) st |= kFpgaStBitAdcSetEnd;
        if (ddrWrEnd)  st |= kFpgaStBitDdrWrEnd;
        if (ddrRdEnd)  st |= kFpgaStBitDdrRdEnd;
        if (measTrg)   st |= kFpgaStBitMeasTrg;
        FpgaRegLogic::Reg_Write(FPGAREG_FPGA_ST, st, ep4Buffer);
    }
}
```

#### 1-2. FakeUsbSession EP4_GetData rewrite

Replace direct register value calculation with:

```cpp
INT EP4_GetData(BYTE* buffer, size_t bufferSize) override
{
    // ... (existing producedLogicalBytes_ step logic) ...

    std::memset(buffer, 0, bufferSize);

    FpgaRegEncoding::EncodeWaveWrCnt(producedLogicalBytes_, buffer);
    FpgaRegEncoding::EncodeWaveRdCnt(readLogicalBytes_, buffer);

    const bool ddrWrEnd = (producedLogicalBytes_ >= totalLogicalBytes_
                           && totalLogicalBytes_ != 0);
    const bool ddrRdEnd = (readLogicalBytes_ >= totalLogicalBytes_
                           && totalLogicalBytes_ != 0);
    FpgaRegEncoding::EncodeFpgaSt(
        /*ddrLink=*/true,
        /*adcSetEnd=*/true,
        ddrWrEnd, ddrRdEnd,
        /*measTrg=*/false,
        buffer);

    return WaveAcquisition::kUsbSuccess;
}
```

Apply to both `SimulationRunnerCore.cpp` and `WaveAcquisitionEngine_test.cpp`.

#### 1-3. Tests to add

```
Test_L1_01_EncodeWaveWrCnt_MatchesFpgaSpec
  - Input: 16384 bytes -> addrUnits=512, limAddr=511, regValue=511*32=16352
  - Verify host RegGet_DDRWaveCnt returns 16352
  - Verify WaveAcquisitionEngine adds +32 to get 16384

Test_L1_02_EncodeWaveWrCnt_ZeroBytes_ReturnsZero
  - Input: 0 bytes -> regValue=0

Test_L1_03_FpgaSt_AllBits_RoundTrip
  - Set all 5 bits individually
  - Verify RegGet_DDRWriteEnd / RegGet_DDRReadEnd read correctly
  - Verify bit[0] DDR_LINK, bit[1] ADC_SET_END do not interfere

Test_L1_04_FpgaSt_Bit4MeasTrg_DoesNotAffectWrEnd
  - bit[4] set -> DDR_WR_END must not be affected
```

#### 1-4. Bugs detectable after Level 1

| Bug pattern | Current | After Level 1 |
|---|---|---|
| RegGet_DDRWaveCnt H/L merge error (shift direction) | Undetectable (FakeUsbSession writes correct value directly) | Detectable |
| FPGA_ST unused bits pollute WrEnd/RdEnd check | Undetectable (only bit[2]/[3] set) | Detectable (bit[0],[1],[4] also set) |
| kDdrCompletionPaddingBytes value mismatch with FPGA spec | Undetectable (same constant used for reverse calculation) | Detectable (FPGA path computes independently) |

---

## Level 2: MEAS_CTRL State Machine Model

### Purpose

Current `FakeUsbSession` provides all data instantly. Real FPGA produces data progressively via MEAS_CTRL state machine. Model this to test host EP4 polling robustness.

### Design

#### 2-1. New file: `Sysmex_AnalogBoard_SimRunner/FpgaDdrModel.h`

```cpp
#pragma once
#include <windows.h>
#include <algorithm>

namespace SimRunner
{
    enum class MeasState
    {
        Idle,
        Init,       // 10us initialization
        Measuring,  // ADC -> DDR writing in progress
        Wait,       // 500us post-measurement wait
        RdWait,     // Waiting for DDR read to catch up
    };

    struct FpgaDdrModelConfig
    {
        ULONG totalWaveBytes = 0;        // Total bytes to produce
        ULONG burstSizeBytes = 0x4000;   // 16384 bytes (C_MAX_BURST=512 x 32)
        ULONG producerBurstsPerPoll = 1; // Bursts completed per EP4 poll
        INT   initPollCount = 1;         // EP4 polls during INIT phase
        INT   waitPollCount = 1;         // EP4 polls during WAIT phase
    };

    class FpgaDdrModel
    {
    public:
        explicit FpgaDdrModel(const FpgaDdrModelConfig& config)
            : config_(config) {}

        FpgaDdrModel() = default;

        // Called once per EP4_GetData invocation
        void AdvanceOnePoll()
        {
            switch (state_)
            {
            case MeasState::Idle:
                state_ = MeasState::Init;
                stateCounter_ = 0;
                break;

            case MeasState::Init:
                ++stateCounter_;
                if (stateCounter_ >= config_.initPollCount)
                {
                    state_ = MeasState::Measuring;
                    stateCounter_ = 0;
                }
                break;

            case MeasState::Measuring:
            {
                ULONG step = config_.burstSizeBytes
                             * config_.producerBurstsPerPoll;
                writtenBytes_ = (std::min)(
                    config_.totalWaveBytes, writtenBytes_ + step);
                if (writtenBytes_ >= config_.totalWaveBytes)
                {
                    state_ = MeasState::Wait;
                    stateCounter_ = 0;
                }
                break;
            }

            case MeasState::Wait:
                ++stateCounter_;
                if (stateCounter_ >= config_.waitPollCount)
                {
                    state_ = MeasState::RdWait;
                }
                break;

            case MeasState::RdWait:
                break;
            }
        }

        void OnEp6ReadCompleted(ULONG bytesRead)
        {
            readBytes_ += bytesRead;
        }

        MeasState GetState() const { return state_; }
        ULONG GetWrittenBytes() const { return writtenBytes_; }
        ULONG GetReadBytes() const { return readBytes_; }

        bool IsDdrWrEnd() const
        {
            return state_ == MeasState::Wait
                || state_ == MeasState::RdWait;
        }

        bool IsDdrRdEnd() const
        {
            return IsDdrWrEnd()
                && readBytes_ >= writtenBytes_;
        }

        bool IsMeasTrg() const
        {
            return state_ == MeasState::Init
                || state_ == MeasState::Measuring;
        }

    private:
        FpgaDdrModelConfig config_;
        MeasState state_ = MeasState::Idle;
        INT stateCounter_ = 0;
        ULONG writtenBytes_ = 0;
        ULONG readBytes_ = 0;
    };
}
```

#### 2-2. FakeUsbSession integration with FpgaDdrModel

Replace direct `producedLogicalBytes_` stepping with `FpgaDdrModel`:

```cpp
class FakeUsbSession : public WaveAcquisition::IUsbSession
{
public:
    explicit FakeUsbSession(const SimulationScenario& scenario)
        : scenario_(scenario)
    {
        FpgaDdrModelConfig modelConfig = {};
        modelConfig.totalWaveBytes =
            (scenario.waveSizeLow + scenario.waveSizeHigh)
            * scenario.totalWaveCount;
        modelConfig.burstSizeBytes = 0x4000;
        modelConfig.producerBurstsPerPoll =
            scenario.producerBurstsPerPoll;
        modelConfig.initPollCount = scenario.initPollCount;
        modelConfig.waitPollCount = scenario.waitPollCount;
        ddrModel_ = FpgaDdrModel(modelConfig);
    }

    INT EP4_GetData(BYTE* buffer, size_t bufferSize) override
    {
        ddrModel_.AdvanceOnePoll();

        std::memset(buffer, 0, bufferSize);

        FpgaRegEncoding::EncodeWaveWrCnt(
            ddrModel_.GetWrittenBytes(), buffer);
        FpgaRegEncoding::EncodeWaveRdCnt(
            ddrModel_.GetReadBytes(), buffer);
        FpgaRegEncoding::EncodeFpgaSt(
            /*ddrLink=*/true,
            /*adcSetEnd=*/(ddrModel_.GetState() != MeasState::Idle),
            ddrModel_.IsDdrWrEnd(),
            ddrModel_.IsDdrRdEnd(),
            ddrModel_.IsMeasTrg(),
            buffer);

        return WaveAcquisition::kUsbSuccess;
    }

    INT EP6_GetData(BYTE* buffer, ULONG size) override
    {
        // ... (existing fault injection logic) ...

        ddrModel_.OnEp6ReadCompleted(size);
        return WaveAcquisition::kUsbSuccess;
    }

private:
    FpgaDdrModel ddrModel_;
    // ...
};
```

#### 2-3. Scenario JSON extension

New fields (backward compatible, default to legacy behavior when omitted):

| Field | Default | Meaning |
|---|---|---|
| `producer_bursts_per_poll` | `0` (= all at once) | DDR write bursts per EP4 poll |
| `init_poll_count` | `1` | EP4 polls during INIT phase |
| `wait_poll_count` | `1` | EP4 polls during WAIT phase |

#### 2-4. Tests to add

```
Test_L2_01_InitPhase_WaveWrCntStaysZero
  - During INIT state, WAVE_WR_CNT=0 and DDR_WR_END=0
  - Host EP4 polling correctly handles "no data yet"

Test_L2_02_MeasPhase_WaveWrCntIncreasesInBurstSteps
  - producerBurstsPerPoll=1, burst=0x4000
  - Each EP4 poll: WAVE_WR_CNT increases by 0x4000
  - Host correctly starts EP6 reads at intermediate values

Test_L2_03_WaitPhase_DdrWrEndAsserted
  - In WAIT state: DDR_WR_END=1, DDR_RD_END=0
  - Host correctly reads remaining data

Test_L2_04_RdWaitPhase_DdrRdEndAssertedWhenCaughtUp
  - DDR_RD_END=1 when host reads all data
```

New preset: `data/sim_scenarios/slow_producer.json`

```json
{
  "wave_size_low": 2048,
  "wave_size_high": 2048,
  "waves_per_file": 10,
  "total_wave_count": 100,
  "producer_bursts_per_poll": 1,
  "init_poll_count": 2,
  "wait_poll_count": 3,
  "max_read_chunk_bytes": 16384,
  "timeout_retry_limit": 1,
  "write_delay_ms": 0,
  "write_fail_at": 0,
  "publish_fail_at": 0,
  "ep6_results": ["success"]
}
```

#### 2-5. Bugs detectable after Level 2

| Bug pattern | Current | After Level 2 |
|---|---|---|
| EP6 read started during INIT phase | Undetectable (data available instantly) | Detectable (WAVE_WR_CNT=0 in INIT) |
| Read size finalized while DDR_WR_END=0 | Undetectable | Detectable (availableDdrBytes changes mid-loop) |
| EP4 polling frequency vs data accumulation race | Undetectable | Detectable (gradual increase pattern) |

---

## Level 3: Discrete Burst-Aligned DDR Counter Model

### Purpose

Level 2 models state transitions but uses byte-granularity for WAVE_WR_CNT updates. Real FPGA updates counters in discrete burst-sized jumps (16384 bytes). Model this to test host byte calculation edge cases.

### FPGA actual behavior

```
DDR3_WCTRL (P_WC_END state):
  r_ddr_wr_addr += s_burst    <- jump by burst count
  DDR_LIM_ADDR = r_ddr_wr_addr - 1

DDR3_RCTRL (P_RC_END state):
  r_ddr_addr += r_burstcnt    <- jump by burst count
```

Result: WAVE_WR_CNT increases as 0, 16352, 32736, 49120, ...
(0x4000 - 32 = 16352 byte steps. -32 comes from `r_ddr_wr_addr - 1`)

### Design

#### 3-1. FpgaDdrModel burst precision enhancement

```cpp
void AdvanceOnePoll()
{
    // ... (state machine from Level 2) ...

    case MeasState::Measuring:
    {
        // Burst-aligned increment (matches DDR3_WCTRL P_WC_END)
        for (ULONG i = 0; i < config_.producerBurstsPerPoll; ++i)
        {
            if (writtenBurstUnits_ * config_.burstSizeBytes
                >= config_.totalWaveBytes)
            {
                break;
            }
            ++writtenBurstUnits_;
        }

        if (writtenBurstUnits_ * config_.burstSizeBytes
            >= config_.totalWaveBytes)
        {
            state_ = MeasState::Wait;
            stateCounter_ = 0;
        }
        break;
    }
}

ULONG GetWrittenBytes() const
{
    if (writtenBurstUnits_ == 0) return 0;
    return writtenBurstUnits_ * config_.burstSizeBytes;
}

void OnEp6ReadCompleted(ULONG bytesRead)
{
    // DDR3_RCTRL increments r_ddr_addr by r_burstcnt at P_RC_END
    const ULONG burstsRead =
        (bytesRead + config_.burstSizeBytes - 1) / config_.burstSizeBytes;
    readBurstUnits_ += burstsRead;
    readBytes_ = readBurstUnits_ * config_.burstSizeBytes;
}

bool IsDdrRdEnd() const
{
    // DDR3_RCTRL.vhd line 266:
    // DDR_RD_END = (SAMP_END='1' AND r_ddr_addr >= LIMIT_ADDR)
    return IsDdrWrEnd()
        && readBurstUnits_ >= writtenBurstUnits_;
}
```

#### 3-2. Tests to add

```
Test_L3_01_BurstBoundary_AvailableBytesAlignedToChunk
  - WAVE_WR_CNT=16352 (=16384-32) after first burst
  - Engine adds +32 -> 16384, matches kEp6ReadAlignmentBytes
  - Normal path confirmed

Test_L3_02_PartialWaveAtBurstBoundary
  - wave_size=3000 bytes, burst=16384 bytes
  - 1 burst contains 5.46 waves, fraction accumulates in pendingBytes
  - Next burst resolves the fraction correctly

Test_L3_03_LastBurst_PaddedRead
  - total=50000 bytes (3 bursts + 1216 bytes)
  - Last burst: DDR3_WCTRL issues partial burst (P_WC_LAST)
  - Host alignment padding works correctly

Test_L3_04_DdrRdEnd_ExactBurstMatch
  - readBurstUnits == writtenBurstUnits -> DDR_RD_END=1
  - Host exits loop correctly

Test_L3_05_BacklogCalculation_DiscreteJumps
  - WAVE_WR_CNT jumps 0 -> 16352 -> 32736 -> 49120
  - maxWaveBacklogBytes updates correctly at each jump
```

New preset: `data/sim_scenarios/burst_boundary_stress.json`

```json
{
  "wave_size_low": 1500,
  "wave_size_high": 1500,
  "waves_per_file": 5,
  "total_wave_count": 20,
  "producer_bursts_per_poll": 1,
  "init_poll_count": 1,
  "wait_poll_count": 1,
  "max_read_chunk_bytes": 16384,
  "timeout_retry_limit": 0,
  "write_delay_ms": 0,
  "write_fail_at": 0,
  "publish_fail_at": 0,
  "ep6_results": ["success"]
}
```

Target: wave size (3000 bytes) does not divide evenly into burst size (16384 bytes). Tests fraction handling at 5.46 waves/burst.

#### 3-3. Bugs detectable after Level 3

| Bug pattern | Current | After Level 3 |
|---|---|---|
| availableDdrBytes assumes continuous byte values | Undetectable (FakeUsbSession returns arbitrary values) | Detectable (discrete jumps expose calculation gaps) |
| Last burst with fractional size - alignment padding error | Undetectable (total always burst-aligned) | Detectable (non-aligned total tests fraction path) |
| Backlog calculation misses instantaneous large jumps | Undetectable (continuous increase) | Detectable (0->16352 jump verifies max update) |
| DDR_RD_END off-by-one in byte-level comparison | Undetectable (byte values match exactly) | Detectable (burst-unit comparison tests edge cases) |

---

## Implementation Roadmap

```
Level 1 (register encoding fidelity)
  +- Create FpgaRegisterEncoding.h
  +- Rewrite FakeUsbSession (SimRunner + UnitTest)
  +- Add 4 register round-trip tests
  +- Verify all existing tests PASS
      |
Level 2 (MEAS_CTRL state machine model)
  +- Create FpgaDdrModel.h
  +- Replace FakeUsbSession with FpgaDdrModel-based impl
  +- Add 3 scenario JSON fields (backward compatible)
  +- Add 4 state transition tests + slow_producer preset
  +- Verify all existing tests PASS
      |
Level 3 (burst discretization)
  +- Add burst unit counters to FpgaDdrModel
  +- Add 5 boundary tests + burst_boundary_stress preset
  +- Verify all existing tests PASS
```

Levels are additive. Each level must pass all existing tests before proceeding to the next.

## Investment vs Effect Summary

| Level | Investment | Effect |
|---|---|---|
| Level 1 | Low | Detects host register decode bugs. Highest ROI. |
| Level 2 | Medium | Tests EP4 polling robustness against progressive data arrival |
| Level 3 | Medium | Tests byte calculation edge cases at burst boundaries |

Level 1 alone provides significant improvement in host-side register decode bug detection.

## FPGA Source References

| Module | File |
|---|---|
| DDR3_WCTRL | `FPGA_FW/SYSMEX_ANA_20250129_restored/RTL/DDR3_WCTRL/DDR3_WCTRL.vhd` |
| DDR3_RCTRL | `FPGA_FW/SYSMEX_ANA_20250129_restored/RTL/DDR3_RCTRL/DDR3_RCTRL.vhd` |
| MEAS_CTRL | `FPGA_FW/SYSMEX_ANA_20250129_restored/RTL/MEAS_CTRL/MEAS_CTRL.vhd` |
| REG_CTRL | `FPGA_FW/SYSMEX_ANA_20250129_restored/RTL/REG_CTRL/REG_CTRL.vhd` |
| FPGA_TOP | `FPGA_FW/SYSMEX_ANA_20250129_restored/RTL/FPGA_TOP.vhd` |
| BUFFER_CTRL | `FPGA_FW/SYSMEX_ANA_20250129_restored/RTL/USB30_TOP/BUFFER_CTRL.vhd` |

## Host Source References

| Module | File |
|---|---|
| Register decode | `Sysmex_AnalogBoard_TestApp/FpgaRegisterLogic.h` |
| Register addresses | `Sysmex_AnalogBoard_TestApp/FpgaRegisterAddress.h` |
| Performance metrics | `Sysmex_AnalogBoard_TestApp/AcquisitionPerfMetrics.h` |
| Acquisition engine | `Sysmex_AnalogBoard_TestApp/WaveAcquisitionEngine.h/.cpp` |
| Simulation runner | `Sysmex_AnalogBoard_SimRunner/SimulationRunnerCore.cpp` |
| Unit tests | `Sysmex_AnalogBoard_UnitTest/WaveAcquisitionEngine_test.cpp` |
