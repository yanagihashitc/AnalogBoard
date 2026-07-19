# D23実機copy-ready bundle checklist

対象: `r7-driver-ep4-polling-20260715T1618JST-source-package/TELEMETRY_CSV_READY_1314`

プロセスログ: [Process log](2026-07-16-d23-copy-ready-bundle-log.md)

## 設計・配置

- [x] 親package直下から実験用READYへ迷わず到達できる
- [x] READY内の番号付きファイルだけでN leg→rollback→B legを進められる
- [x] current runbook Draft 2.6、CSV手順、記録シートをofflineで読める
- [x] superseded `field_package/`と`build_evidence/`を変更しない

## Fail-closed gates

- [x] build ID・EXE/DLL・immutable checksum不一致を拒否する
- [x] NoDfx readbackが0以外、device 0台／複数台、NULL相当を拒否する
- [x] inventory wrapperが空のsample ID／USB portを拒否する
- [x] CSV header、row数、cycle連番、必須duration欠落を拒否する
- [x] DFX restart failure時にUSB抜き差しを指示せず停止する

## 配布・検証

- [x] PowerShell parseとcontract testsをPassする
- [x] positive package verificationをPassする
- [x] 破損・欠落・誤状態のnegative testsをPassする
- [x] HTML構造、local link、番号順、checksumをPassする
- [x] 実機操作未実施、driver rollbackは手動、D4未成立を明記する
