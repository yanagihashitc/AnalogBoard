# Wave Output Format Handover (2026-03-02)

## 1. 目的
- 基準コミット: `aebf296adf72a8ac5a8355f7f539dca87521f724`
- 目的: `fl/fh` の意味（low/high）とデータ記録順（low first, high next）を将来変更で壊さないように固定する。

## 2. 実施済み変更

### 2.1 実装側（low/high の意味を明示）
- `AnalogBoard_TestApp/WaveFilePublish.h`
  - `WaveFrameSplitView` を追加。
  - `BuildWaveFrameSplitView(...)` を追加。
- `AnalogBoard_TestApp/WaveFilePublish.cpp`
  - `BuildWaveFrameSplitView(...)` 実装を追加。
  - `frameSizeLow + frameSizeHigh` のオーバーフロー防止チェックを追加。
- `AnalogBoard_TestApp/Dialog1_Main.cpp`
  - `SaveWaveDataToFile(...)` / `CreateWaveDataFile(...)` の引数順を `low -> high` に統一。
  - 実書き込みを `BuildWaveFrameSplitView(...)` 経由に変更。
  - これにより low/high の切り出し位置をロジックとして固定。

### 2.2 テスト側（回帰固定）
- `tests/wave_file_publish_test.cpp`
  - 既存: パス命名、publish 成功/失敗、rollback。
  - 追加: frame split 正常系・境界・異常系。
  - 追加: `aebf296` 互換（2フレーム）で low/high 出力バイト列を検証。
  - 追加: 実ファイル I/O 統合寄りテスト。
    - `.tmp -> .bin` publish 後に内容一致確認。
    - 既存最終ファイルの上書き publish 確認。

## 3. 変更ファイル一覧
- `AnalogBoard_TestApp/Dialog1_Main.cpp`
- `AnalogBoard_TestApp/WaveFilePublish.h`
- `AnalogBoard_TestApp/WaveFilePublish.cpp`
- `tests/wave_file_publish_test.cpp`

## 4. ローカル実行結果（Linux環境）
- 実行コマンド:
  - `g++ -std=c++17 -Wall -Wextra -pedantic tests/wave_file_publish_test.cpp AnalogBoard_TestApp/WaveFilePublish.cpp -o /tmp/wave_file_publish_test && /tmp/wave_file_publish_test`
- 結果:
  - `All tests passed.`

## 5. Windows での確認手順

### 5.1 ビルド・テスト
1. `x64 Native Tools Command Prompt for VS 2022` を開く。
2. リポジトリルートへ移動。
3. 必要ならプロジェクトファイルを再生成（`.vcxproj.xml -> .vcxproj`）:
   - `copy /Y AnalogBoard_TestApp\AnalogBoard_TestApp.vcxproj.xml AnalogBoard_TestApp\AnalogBoard_TestApp.vcxproj`
   - `copy /Y AnalogBoard_Dll\AnalogBoard_Dll.vcxproj.xml AnalogBoard_Dll\AnalogBoard_Dll.vcxproj`
   - `copy /Y AnalogBoard_UnitTest\AnalogBoard_UnitTest.vcxproj.xml AnalogBoard_UnitTest\AnalogBoard_UnitTest.vcxproj`
4. UnitTest ターゲットをビルド:
   - `msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_UnitTest:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1`
5. テスト実行:
   - `x64\Debug\AnalogBoard_UnitTest.exe`

### 5.2 カバレッジ（任意）
- `scripts\run_coverage.bat`

## 6. 実機/運用での推奨確認
1. サンプリング実行後、同一 index の `*_fl_*.bin` / `*_fh_*.bin` ペアを取得。
2. `fl` 側が low データ、`fh` 側が high データであることを既知サンプルで確認。
3. publish 後に `.tmp` が残っていないことを確認。
4. 既存ファイル上書きケースでも最新データが反映されることを確認。

## 7. 未実施項目（この環境では不可）
- MFC 実行経路（`Dialog1_Main.cpp` を含む実行バイナリ）の Windows 実行確認。
- 実機データでの end-to-end 検証。

## 8. 補足
- 変更意図は「仕様変更」ではなく「`aebf296` 互換を崩さないための明示化とテスト固定」。
