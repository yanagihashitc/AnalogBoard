# 新PC high-density 3run データ監査

監査日: 2026-07-14  
状態: 完了済み履歴（2026-07-19 archive）  
対象runbook: [実機確認runbook](../../260706-field-session-runbook.html)  
データ: `260714/`（元保存先 `D:\Analyzer_data\260714\`）  
ログ: `runbook_revision_bundle_260714/evidence/new_pc/2607141608.log`  
代表SHA-256: [manifest](2026-07-14-new-pc-high3-representative-sha256.txt)

## 結論

3runのデータ完全性はPass。全1,997組のFL/FHが1始まりの連続indexで対応し、欠落・片側のみのpair・非最終ファイルのサイズ異常・`.tmp`残置はない。全ファイルはgcsa v1のevent境界（FL 38,400 bytes/event、FH 24,000 bytes/event）に一致し、各runの永続化bytesはログの`saveBytes`と完全一致した。

Gate A全体をPassにするには、driver version、EXE/DLL SHA-256、USB port/controller、sample、NVMe、電源モードの出所確認が残る。データの再取得は現時点では不要。

## Run別集計

| Run | FL/FH pair | Index | Event数 | 永続化bytes | 最終pair | ログ | 判定 |
|---|---:|---:|---:|---:|---|---|---|
| `260714_1611` | 628 | 1–628 | 62,800 | 3,918,720,000 | FL/FHとも100 events | Type C、`saveBytes`一致 | Pass |
| `260714_1613` | 689 | 1–689 | 68,829 | 4,294,929,600 | FL/FHとも29 events | Type C、`saveBytes`一致 | Pass |
| `260714_1616` | 680 | 1–680 | 67,930 | 4,238,832,000 | FL/FHとも30 events | Type C、`saveBytes`一致 | Pass |

`Read over`と永続化bytesの差は順に5,120／37,696／3,712 bytesで、いずれも1 event（62,400 bytes）未満。部分eventをbinへ書かず、FL/FHの完全eventだけを対で永続化した結果と整合する。`260714_1613`の`WAVE_RD_CNT=0`は4GiB counter wrapとして扱い、単独の異常根拠にはしない。

## cfg確認

- 3ファイルのSHA-256は同一: `69d7b0c5b6681080bb761736a32f7e3190ba332a3d5e4cbdaf50dc49f65ea12d`
- 全CH1–CH13が有効
- `Select(0/1)=0`
- FL/FHとも2,400 samples/channel/event
- `Waveformes Nums Per File=100`
- `Manual Get Mode=0`
- Trigger rangeは`-30.0..30.0us`

## 代表decode

各runの先頭・中央・末尾pairから、それぞれ先頭・中央・末尾eventをgcsa v1と同じ`<u2`・event-major・channel-major形状で非破壊decodeした。全13chでshort readなし、全値が14-bit ADC範囲（0–16,383）内、3run間のchannel別min/max/meanに不連続な差はなかった。現行gcsa上の波形とラベル対応はオーナーが目視確認済み。

## 証跡の限界

- 代表pairのみSHA-256を保存し、約12GB全体のcontent hashは算出していない。
- データとログからdriver version、実行EXE/DLL hash、USB controller/port、sample識別、NVMe、電源モードは復元できない。
- ログ冒頭に接続失敗と保存先warningがあるが、16:10:05のUSB3.0接続成功と16:10:07の設定成功後に対象3runが開始され、各runはType Cで完了している。
