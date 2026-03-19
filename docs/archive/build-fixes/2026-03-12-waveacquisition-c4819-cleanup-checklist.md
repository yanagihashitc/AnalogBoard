# WaveAcquisitionEngine C4819 cleanup checklist

対象プラン: warning cleanup (`WaveAcquisitionEngine.cpp` C4819)
プロセスログ: [Process Log](./2026-03-12-waveacquisition-c4819-cleanup-log.md)
作成日: 2026-03-12

---

## Phase 1: Root Cause Fix

依存: なし

- [x] `WaveAcquisitionEngine.cpp` の非 CP932 文字を特定する
- [x] warning 原因の Unicode punctuation を ASCII に置き換える
- [x] C4819 の原因と対処を `docs/troubleshooting` に記録する

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_TestApp:Rebuild;AnalogBoard_SimRunner:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
```

---

## 全 Phase 共通チェック

- [x] 対象 warning が再現しない
- [x] UnitTest 全件 pass
- [x] Debug x64 Rebuild 成功
- [x] process_log にエントリ追記
