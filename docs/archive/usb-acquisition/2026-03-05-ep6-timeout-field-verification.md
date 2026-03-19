# EP6 Timeout 実機検証手順（pending pair 未publish確認）

作成日: 2026-03-05  
対象: `Dialog1_Main.cpp` の EP6 失敗時 `ErrExit = TRUE` 修正

## 1. 目的

EP6 転送失敗（timeout/通信断）時に、取得途中の最終ペア（`*.bin.tmp`）が `.bin` に publish されないことを実機で確認する。

## 2. 事前準備

1. Debug x64 をビルドする。
2. 保存先（`SavePath`）に書き込み権限があることを確認する。
3. 保存先を空ディレクトリにしておく（判定を容易にするため）。
4. 可能ならログ取得手段を準備する。
   - アプリのログ表示内容をテキスト保存
   - 任意: DebugView 等で `OutputDebugString` も採取

### ビルドコマンド

```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_TestApp:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
```

## 3. 試験シナリオ

## 3.1 正常開始

1. アプリ起動。
2. `SavePath` を検証用ディレクトリに設定。
3. 測定条件を設定（`WaveNum` は小さすぎない値を推奨）。
4. `Data Get Start` を押下して取得開始。

## 3.2 EP6失敗の注入

以下のいずれかで EP6 失敗を発生させる。

- 推奨: 取得中に USB ケーブルを一時的に抜く（またはボード電源断）
- 代替: 実験系で EP6 timeout を再現できる既知手順

## 3.3 停止後の確認

取得スレッド終了後、ログと出力ファイルを確認する。

## 4. 期待ログ

以下が確認できること。

1. EP6 エラー発生ログ
   - `EP6 Read <N> byte NG.`
   - 直後に USB エラー文字列（timeout/transfer failed 系）
2. スレッド終了ログ
   - `Exit EP6 get data thread.`
3. エラー発生後に、最終未完了ペアの rename 成功ログが出ない
   - `api=MoveFileEx ... rename success ...` が最終 index に対して出ない

注: エラー前に完了済みの index に対する `rename success` は存在してよい。

## 5. 出力ファイルの期待状態

保存先で以下を満たすこと。

1. エラー時点の最終 index は `*.bin.tmp` が残る
   - 例: `*_fl_<k>.bin.tmp`, `*_fh_<k>.bin.tmp`
2. 同じ `<k>` の `.bin` は存在しない
   - `*_fl_<k>.bin`, `*_fh_<k>.bin` が未生成
3. `<k` の完了済み index は `.bin` が存在してよい

## 6. 確認コマンド例（PowerShell）

```powershell
# 直近の low/high ファイルを確認
Get-ChildItem <SavePath> -File | Where-Object { $_.Name -match "_f[lh]_" } | Sort-Object Name

# tmp と bin の件数確認
@{
  tmp = (Get-ChildItem <SavePath> -Filter *.bin.tmp -File).Count
  bin = (Get-ChildItem <SavePath> -Filter *.bin -File).Count
}
```

## 7. 合否判定

- Pass:
  - EP6 エラーログあり
  - 最終未完了 index は `.tmp` のみ残る
  - 同 index の `.bin` が存在しない
  - エラー後の最終 index に対する `MoveFileEx rename success` がない
- Fail:
  - EP6 エラー発生後に最終未完了 index の `.bin` が生成される
  - もしくは同 index に対する `rename success` が記録される

## 8. 証跡として残すもの

1. 実行日時（JST）
2. 使用バイナリ（Debug/Release、コミットID）
3. アプリログ（EP6 NG 行と終了行を含む）
4. 保存先ディレクトリ一覧（`*.bin`/`*.bin.tmp`）
5. 判定（Pass/Fail）

