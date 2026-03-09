# AcquisitionPerfMetrics Test Perspectives

Source: `AnalogBoard_UnitTest/AcquisitionPerfMetrics_test.cpp`

Total tests: 6

## Perspective Table

| Case ID / Source Test | Input / Precondition | Perspective (Equivalence / Boundary) | Expected Result | Notes |
|---|---|---|---|---|
| `Test_TC_N_01_RecordEp6Transfer_AggregatesCountTimeAndBytes` | multiple EP6 samples with elapsed time and byte counts | Equivalence – normal aggregation | call count, total time, max time, bytes are accumulated | EP6 summary metrics |
| `Test_TC_N_02_RecordDdrStatus_TracksLatestAndMaxBacklog` | DDR write/read counters increase over time | Equivalence – normal aggregation | latest counters update and max backlog is tracked | cycle summary input |
| `Test_TC_B_01_EmptyCollector_ReturnsZeroAverages` | collector has no samples | Boundary – empty state | averages return zero | avoid divide-by-zero |
| `Test_TC_B_02_RecordDdrStatus_InvertedCounters_ClampBacklogToZero` | read counter exceeds write counter | Boundary – inverted counters | backlog is clamped to zero | defensive metric handling |
| `Test_TC_B_03_RecordEp6Transfer_TimeoutsIncreaseCounter` | transfer sequence includes timeout flags | Boundary – timeout counting | timeout counter increments while call count still tracks all calls | timeout frequency metric |
| `Test_TC_B_04_RecordSaveTransfer_SingleSampleUsesSameAvgAndMax` | one save transfer sample only | Boundary – single sample | average and max equal the same sample | save metrics base case |
