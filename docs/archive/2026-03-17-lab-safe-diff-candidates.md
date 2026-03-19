# Lab to Baseline Safe Diff Candidates

対象:

- source: `lab/0.2.2-engine-semantics`
- target: `baseline/0.1.4-hw-recovery`

目的:

- lab で code-side / simulation-side に検証済みの差分を、baseline へ戻せる単位に分解する
- `cherry-pick` 向きの差分と、lab に残す verification asset を分離する

---

## Classification

| Bucket | Files | Recommendation | Why |
|---|---|---|---|
| Direct backport candidate | `AnalogBoard_TestApp/AcquisitionCompletionLogic.h`, `AnalogBoard_UnitTest/AcquisitionCompletionLogic_test.cpp` | baseline へ first port | baseline 側にも同名 helper / test があり、lab 差分は startup stale completed snapshot (`DDR_WR_END=1 && DDR_RD_END=1 && WAVE_WR_CNT>0`) を active cycle として latch しない guard とその test 追加にほぼ限定される |
| Optional manual port | `AnalogBoard_TestApp/AcquisitionPerfMetrics.h`, `AnalogBoard_UnitTest/AcquisitionPerfMetrics_test.cpp` | いったん skip、必要時のみ manual port | baseline はすでに timeout telemetry と `[PR01][TIMEOUT] stage=...` を持つ。lab 側の追加は `timeout.drainingHintSeen` を metrics に保持する補助情報なので、field で不足が見えたときだけ小さく移植すればよい |
| Lab-only verification asset | `AnalogBoard_TestApp/WaveAcquisitionEngine.cpp`, `AnalogBoard_UnitTest/WaveAcquisitionEngine_test.cpp` | baseline へは戻さない | baseline の mainline は `Dialog1_Main.cpp` ベースであり、`WaveAcquisitionEngine` を直接戻す段階ではない。semantics の参照実装として保持する |
| Lab-only verification asset | `AnalogBoard_SimRunner/*`, `AnalogBoard_UnitTest/SimulationScenario_test.cpp`, `AnalogBoard_UnitTest/SimulationRunnerIntegration_test.cpp`, `data/sim_scenarios/stale_ddrwrend_rdwait.json`, `data/sim_scenarios/high_density_timeout_active.json` | lab 維持 | baseline には SimRunner がなく、ここは regression asset として lab に残す方が安全 |
| Docs-only | `docs/2026-03-17-lab-semantics-rdwait-wstop-checklist.md`, `docs/process_log/2026-03-17-lab-semantics-rdwait-wstop-log.md` | cherry-pick 不要 | branch-local implementation history。baseline には結果だけ checklist / process log で同期すれば十分 |
| Excluded | `data/sim_scenarios/empty_capture.json`, `data/sim_scenarios/slow_producer.json` | 対象外 | user-owned dirty files のため今回の safe diff 候補に含めない |

---

## Suggested Order

1. `AcquisitionCompletionLogic.h` の startup stale `WAVE_WR_CNT` guard を baseline へ戻す
2. `AcquisitionCompletionLogic_test.cpp` に `TC_R_05` を追加して regression を固定する
3. 必要なら `AcquisitionPerfMetrics.h` に `drainingHintSeen` を保持する小差分を手移植する
4. lab-only asset は backport せず、baseline 実装時の参照先として使う

---

## Current Status

- 2026-03-18 に `AcquisitionCompletionLogic.h` の startup stale `WAVE_WR_CNT` guard を baseline へ backport し、`AcquisitionCompletionLogic_test.cpp` の `TC_R_05` を Red (`Failed: 3`) -> Green (`14/14`) で固定した
- baseline worktree で `build_test.bat`、`Release|x64` DLL rebuild、`Release|x64` TestApp rebuild を実行し、code-side gate を通した
- `AcquisitionPerfMetrics.h` の `timeout.drainingHintSeen` 保存は、baseline がすでに `Dialog1_Main.cpp` で `[PR01][TIMEOUT] stage=...` を timeout 時点に直接出しているため今回は skip とした
- `WaveAcquisitionEngine` / SimRunner / scenario preset は引き続き lab-only verification asset として維持する

## Verification Basis

- lab `build_test.bat` pass
- `stale_ddrwrend_rdwait` simulation pass
- `high_density_timeout_active` simulation pass

2026-03-17 の確認では、helper / metrics のうち baseline に直接価値がある差分は小さく、engine / SimRunner / scenario は verification asset として lab に残すのが自然だった。2026-03-18 の baseline backport 後もこの分類は維持し、実機 gate を閉じるまでは telemetry の追加移植より field validation を優先する。
