# EP6 Local Buffer Fix Design

作成日: 2026-03-11
対象ブランチ: `investigation/0.1.4-regression`

## 背景

- `0.1.4` では実機ログ上 2 run が正常完走していた。
- `0.1.5` と生の `76b2b2a` では、取得開始後にログが `FPGA start sampling` 付近で途切れ、アプリが再起動したように次のログファイルが始まっていた。
- `76b2b2a` ベースの comparison build (`cmp_76b2b2a`) で、`EP6_GetData()` を reusable buffer から per-call local heap buffer に戻すと、異常終了が消えて実機でデータ取得できた。
- その結果を移植した `0.1.6` は一度安定化したが、follow-up で `ScopedHeapBuffer` を `new[]/delete[]` に変更した後の実機ログ (`2603111710.log`) では EP6 timeout が再発した。

## 目的

- comparison build で有効だった修正を、現行 `0.1.5` に最小差分で移植する。
- 現行の shared mutex 制御と診断ログは維持しつつ、`EP6` の scratch buffer 所有と allocator backend を comparison build と一致させる。

## 検討した案

### 1. 推奨案: `EP6_GetData()` だけを per-call local heap buffer に戻す

- comparison build の成功条件をそのまま現行へ持ち込める。
- shared mutex、timeout、診断ログなど `0.1.5` で追加された他の経路を崩さない。
- 変更範囲は `UsbTransferHelpers` と `EP6_GetData()` の hot path に限定される。

### 1b. follow-up regression 対応: allocator backend を CRT `malloc/free` に固定する

- `cmp_76b2b2a` 成功版との差分を詰めると、`0.1.6` failure 時点では `ScopedHeapBuffer` backend の `new[]/delete[]` 化が主差分だった。
- helper に backend contract を持たせることで、将来の review follow-up で `new[]/delete[]` に戻ることを防げる。

### 2. comparison build を現行へ全面移植する

- 実装は単純だが、`0.1.5` の他の修正を巻き戻す。
- shared mutex や計測ログの変更点まで混ざり、原因切り分け済みの範囲を超える。

## 採用設計

- 案1を採用する。
- `UsbTransferHelpers` に `ScopedHeapBuffer` を追加し、`EP6_GetData()` は呼び出しごとに 4MB buffer を確保して使い捨てる。
- `ScopedHeapBuffer` の allocator backend は comparison build と同じ CRT `malloc/free` に固定し、helper 上で contract を公開する。
- `USB_Lib_Info::m_ep6TransferBuffer` は ABI 互換性のため残し、現行 build では未使用にする。
- `USBBoard_Connect()` の reusable buffer 事前確保は削除する。
- `USBBoard_Disconnect()` / デストラクタの mutex 制御や診断カウンタ処理は維持する。

## テスト方針

- `UsbTransferHelpers_test.cpp` に `ScopedHeapBuffer` の正常系 2 件、境界/異常系 2 件に加え、allocator backend contract 2 件を追加する。
- 既存の reusable buffer test は helper 自体の既存契約として残す。
- 実行確認は `UsbTransferHelpers_test` と `Release x64` build を必須にする。
- `build_test.bat` 全件実行は試みるが、既知の `WaveDataFileIO_test` fixture 依存があれば別途記録する。

## 成功条件

- 現行 `0.1.5` の source に fix が入り、`Release x64` build が成功する。
- `ScopedHeapBuffer` test が pass する。
- allocator backend contract test が pass する。
- 実機で comparison build と同等の改善を確認できる状態までソースが揃う。
