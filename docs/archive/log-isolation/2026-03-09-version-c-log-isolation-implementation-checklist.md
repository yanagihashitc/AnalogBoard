# Version C Log Isolation 実装チェックリスト

対象プラン: [EP6 logging 負荷切り分け実施チェックリスト](./2026-03-09-ep6-log-isolation-test-checklist.md)
プロセスログ: [Process Log](./2026-03-09-version-c-log-isolation-implementation-log.md)
作成日: 2026-03-09

---

## Phase 1: Version C 実装

依存: Version B 実装済み

- [x] `AnalogBoard_Dll.cpp` の EP6 DLL perf log 箇所を特定する
- [x] Version C 用の DLL perf log 切り分けスイッチを追加する
- [x] `OutputDebugStringA(...)` ブロックを条件付きにする
- [x] TestApp の version を `0.1.103` に更新する

---

## Phase 2: 検証引き継ぎ

依存: Phase 1

- [x] build 前提と未実施事項を docs に整理する
- [ ] `x64 Release` build を実施する
- [ ] 実機で Version C のデータ取得を実施する

---

## 全 Phase 共通チェック

各 Phase 完了前に以下を確認:

- [ ] UnitTest 全件 pass
- [ ] Debug x64 Rebuild 成功
- [x] process_log にエントリ追記
