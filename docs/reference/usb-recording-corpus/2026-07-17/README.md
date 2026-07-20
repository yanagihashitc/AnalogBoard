# 2026-07-17 USBPcap protocol baseline

このディレクトリは、2026-07-17 characterization で取得した6本の USBPcap capture を、成功 Type C の protocol baseline として索引化した Phase 0 証跡である。tracked なのはメタデータと payload-free な要約だけで、capture、EP6 生バイト、再生成した詳細 JSON は Git 管理外に置く。

本 corpus は将来の Tier 1 replay 候補だが、現時点では replay fixture ではない。pre-trigger EP4 failure、Type B drain stall、timeout、切断、short read の failure trace は含まれず、それらの原因も証明しない。

## 正規ファイルと配置

- [manifest.json](manifest.json): 6 source の identity、Capinfos metadata、tool/field contract、ignored analysis の digest
- [scenarios.json](scenarios.json): owner-provided run mapping、主要 endpoint 集計、根拠 frame、観測限界
- [解析器](../../../../scripts/pcap-analysis/README.md): streaming analyzer、再生成手順、focused test
- Source root（ignored/local-only）: `artifacts/field-session/2026-07-17-characterization/`
- Generated analysis root（ignored/regenerable）: `artifacts/field-session/2026-07-17-characterization/analysis/`
- 正規の受け入れ基準: [AnalogBoard 再構築プラン Draft 4.0](../../../plans/260710-analogboard-rebuild-plan.html#p0-corpus-steps)

## Source inventory

全 capture は `pcapng`、encapsulation は `usb-usbpcap`、interface は `USBPcap1` 1本である。時刻は Capinfos 表示値で、report 自体は timezone を宣言しない。

| Capture | Scenario | Size (bytes) | Packets | Capture range | Generated summary SHA-256 |
|---|---|---:|---:|---|---|
| `low_mid.pcapng` | low 15:29 + mid 15:32 | 1,263,775,212 | 530,929 | 15:28:48.619238–15:33:59.837398 | `aef06d4e23596ee7f60b9c5a2ed121dde6433d6b063223f8b14259b4bcf8f725` |
| `mid.pcapng` | mid 15:35 | 935,453,828 | 245,658 | 15:34:34.610678–15:37:08.829616 | `b7a5dab2a3b914c29a4a784225c91a2ca25f4d763fd7bb0eb2c91809e75bb972` |
| `low.pcapng` | low 15:39 | 408,817,408 | 263,819 | 15:38:39.394438–15:41:07.832560 | `d7648fe43582fe292d72985336ecbc3f3d5d4d557f32953b0f1d3f683b77058a` |
| `high1.pcapng` | high 15:42、代表 high | 4,317,352,764 | 146,730 | 15:41:25.076067–15:43:54.907978 | `831eec6dd7731b4cca14db042155d24a9a552e6e1f152a80b04ece68535f6c03` |
| `high2.pcapng` | high 15:45、corroboration | 4,316,179,348 | 138,248 | 15:44:30.057300–15:47:00.831843 | `b0af5e3deaf49e34feb105abb81dd81d6fd4acc41f111c542052eaddd1da89bc` |
| `idle_180_1700.pcapng` | app start/end idle | 112,370,484 | 942,478 | 13:06:42.937619–13:40:03.900738 | `746bfd5ce338d81eb884c93ff290456f0cd239027622cbbb4a355d426c207d36` |

Source SHA-256、duration、interface 詳細は [manifest.json](manifest.json) が正。生成物の aggregate `bounded_summary.json` は 85,314 bytes、SHA-256 は `07c31d74a51dfe43fbb823ceccb04c6915154c1ccd8aaa0b7dabada7e4eab4e7` である。

## 観測結果

- 各 capture は独立に VID `0x04b4` / PID `0xfff2` と endpoint coverage から target を発見し、結果はいずれも bus 1/device 50 だった。固定 address の使い回しではない。
- `low_mid` では frame `31 → 3506 → 3526 → 34730 → 496930 → 496932 → 528366` の順に、first target observation、EP2 Set candidate completion、Set後EP4 completion、Set後EP6 completion、final EP6 completion、その後のsuccessful EP4 completion、last target observationを保持した。
- `mid`、`low`、`high1`、`high2` は capture window 内にEP2を含まない。これはその window での不在であり、アプリケーション全体でSetが無かったという意味ではない。
- `high1` と `high2` は EP6 completion data length の合計がそれぞれ正確に `4,294,967,296` bytes。source pcapng が4 GiB付近で終わる事実を device/acquisition failure と分離し、両 capture とも観測USBD failureは0だった。
- `low` は frame 37 に capture先頭境界の unmatched completion が1件ある。他の unmatched、duplicate、cancelled、non-success、unknown status、truncated は0。
- `idle_180_1700` は target のcontrol/descriptor 36 rowsだけを含み、EP2/EP4/EP6は0。target のlast observationは relative `180.850791000` s（frame 89346）で、capture全体は `2000.963119` s続く。last observation は物理disconnectやprocess exitの証明ではない。
- 5本のacquisition captureでは final EP6 completion 後にsuccessful EP4 completionを観測した。ただし payload-free USB orderingだけでは DDR drain bit、host cleanup、graceful process closeを証明できない。

## スキーマと限界

| Schema | Version | 用途 |
|---|---:|---|
| `analogboard.phase0.usbpcap-source-manifest` | 1 | source identity、Capinfos、required dissector fields |
| `analogboard.phase0.usbpcap-bounded-summary` | 1 | capture単位のdevice/endpoint/correlation/lifecycle集計 |
| `analogboard.phase0.usbpcap-extraction-bundle` | 1 | 6 summaryとtool/field contractのaggregate |
| `analogboard.phase0.usbpcap-corpus-index` | 1 | tracked source/evidence index |
| `analogboard.phase0.usbpcap-scenarios` | 1 | tracked scenario mapping と主要根拠 |

USBPcap 4.6.7 の対象行では request のrequested lengthを取得できないため、全 correlated pair は `short_transfer_unknown` であり、short transferが無かったとは判定しない。NT statusも利用可能fieldに無く、USBD statusと混同しない。EP2/EP4のpayloadは出力していないため、labelはpacket orderingと既存アプリ文脈に基づくcandidate/phase分類であり、command/register bytesのdecode結果ではない。

この corpus は成功 Type C のみである。failure path の Tier 1/2 回帰には、将来の実failure recordingまたは明示的なsynthetic fault injectionが別途必要である。

## 再生成と決定論確認

実行はrepository rootから行う。Windows側の同一Wireshark install rootにある TShark/Capinfos 4.6.7を使い、analyzerが実tool versionと19 required fieldsを再検証する。

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s scripts/pcap-analysis -p 'test_*.py' -v
python3 scripts/pcap-analysis/analyze.py manifest
python3 scripts/pcap-analysis/analyze.py extract --captures low_mid.pcapng mid.pcapng low.pcapng high1.pcapng high2.pcapng idle_180_1700.pcapng
sha256sum artifacts/field-session/2026-07-17-characterization/analysis/*.json
```

`manifest` と `extract` はsourceを変更せず、captureごとに処理前後のsize/SHA-256一致とTShark row count＝Capinfos packet countを要求する。全6件の `extract` を同一input/tool versionで2回実行し、8 JSONのSHA-256が全件一致することを2026-07-20に確認済み。

## D19・保持・責務境界

6 captureはD19確定前に取得された平文のpre-D19 local-only evidenceである。`/artifacts/*` のignoreだけをat-rest保護とみなさず、開発環境・CI・外部へ持ち出さない。保護済みcanonical storageへの移設、再取得、production Recorder schema、暗号化方式の凍結は本scope外である。

tracked JSON/Markdownはsource identity、frame番号、時刻、長さ、count、status分類だけを含み、raw payloadを含まない。P0-C4（bin/cfg/telemetry index）はplannedの別scopeであり、「初期コーパス取得」gateとFrozen v1はopenのまま。

## Central synchronization handoff

- Source repository/branch: `AnalogBoard` / `analysis/phase0-usbpcap-corpus`
- Checkpoint evidence: bootstrap `8e1f034`、P0-C1 `51ba91b`、P0-C2 `80e06f5`、P0-C3はこのindexを追加するcheckpoint commit
- Verification: focused tests 36/36、全6 source identity、全6 full summary、8 JSONの2-run byte identity
- Source-plan update: Draft 4.0 のPhase 0 evidence/statusと `#p0-corpus-steps` status cellsだけ。P0-C1〜C3はPR merge前なので `gate_ready`、P0-C4/Frozen v1/Phase 0は未完
- Central action after PR merge: `task_management` workspaceからregistry syncとroadmap evidence/statusを更新し、P0-C1〜C3をmerge証跡付きで`completed`へ遷移する。AnalogBoard workspaceからcentral mirror/roadmapは編集しない
