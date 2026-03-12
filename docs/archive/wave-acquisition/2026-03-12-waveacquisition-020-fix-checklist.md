# 0.2.0 WaveAcquisitionEngine Fix タスクチェックリスト

対象プラン: [2026-03-12-waveacquisition-020-fix-design](./2026-03-12-waveacquisition-020-fix-design.md)
プロセスログ: [Process Log](./2026-03-12-waveacquisition-020-fix-log.md)
作成日: 2026-03-12

---

## Phase 1: Repro Tests

依存: なし

- [x] アライメント不正で即終了する再現テストを追加する
- [x] 空取得成功扱いを防ぐ再現テストを追加する
- [x] Red を確認する

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"
```

---

## Phase 2: Engine Fix

依存: Phase 1

- [x] `WaveAcquisitionEngine` の中間読み取りアライメント処理を修正する
- [x] `WaveAcquisitionEngine` の空取得成功扱いを防ぐ
- [x] 追加テストを Green にする

---

## Phase 3: Verification

依存: Phase 2

- [x] UnitTest 全件 pass を確認する
- [x] Debug x64 Rebuild 成功を確認する
- [x] checklist / process log / index を完了状態に更新する

---

## 全 Phase 共通チェック

各 Phase 完了前に以下を確認:

- [x] UnitTest 全件 pass
- [x] Debug x64 Rebuild 成功
- [x] process_log にエントリ追記
