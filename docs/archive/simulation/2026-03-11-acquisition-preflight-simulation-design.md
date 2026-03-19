# Acquisition Preflight Simulation Design

作成日: 2026-03-11
対象ブランチ: `feature/acquisition-preflight-simulation`

## 背景

- 実機確認は時間がかかるため、host 側の不具合を実機前にできるだけ潰したい。
- 現状の取得ループは `Dialog1_Main.cpp` の実機依存コードに強く結合しており、USB timeout、disconnect、writer 遅延、publish failure を実機なしで再現しにくい。
- 一方で、register 解釈と file publish の一部は既に純粋ロジックとして切り出されており、simulation で再利用できる土台がある。
- 実機用 binary には simulation 用コードや分岐を入れたくない。

## 目的

- 実機用 `AnalogBoard_TestApp.exe` を汚さずに、取得前のシミュレーション試験を繰り返し実行できるようにする。
- 実機前に timeout、disconnect、queue pressure、publish failure を自動再現できるようにする。
- 日常運用は preset 名だけでサクッと回せる形にする。

## 採用方針

### 1. 推奨案: 実機用 EXE と simulation 実行系を分離する

- 共通ロジックは `WaveAcquisitionEngine` として切り出す。
- 実機側は `RealUsbSession`、simulation 側は `FakeUsbSession` を差し替える。
- simulation は別実行系 `AnalogBoard_SimRunner.exe` から実行する。
- 実機用 project には fake、scenario parser、preset 定義を追加しない。

### 2. 採用しない案: 実機用 EXE に simulation 切替を入れる

- 配布物は減るが、実機用 binary に不要な分岐と依存が残る。
- 今回の「実機用には余計なものを入れたくない」という要求に合わない。

## 採用設計

### 共通取得エンジン

- `LoopTestProcessThread_EP6_GetData` の USB 読み取り、DDR 状態判定、retry、writer/publish 連携を `WaveAcquisitionEngine` に移す。
- `Dialog1_Main.cpp` は UI 入力、開始停止、画面表示、ログ表示の glue に縮小する。

### 抽象インターフェース

- `IUsbSession`
  - `Connect`
  - `Disconnect`
  - `EP2_SendData`
  - `EP4_GetData`
  - `EP6_GetData`
- `IWavePairSink`
  - pair open / write / publish / abort
- `IAcquisitionObserver`
  - ログ、進捗、cycle summary 出力

### 実機側

- `RealUsbSession` は既存 DLL 呼び出しを包む薄い adapter にする。
- `AnalogBoard_TestApp` は `RealUsbSession` のみを使用する。
- 実機用 build は現行の DLL 先行ビルドを維持する。

### Simulation 側

- `AnalogBoard_SimRunner.exe` を console app として追加する。
- `FakeUsbSession` は scenario に従って以下を返す。
  - EP4 status 遷移
  - EP6 success / timeout / disconnect
  - 遅延注入
- `ScriptedWavePairSink` は以下を再現する。
  - write delay
  - write failure
  - publish failure

### Scenario と日常運用

- 保存形式は `data/sim_scenarios/<preset>.json` とする。
- 日常利用の入口は preset 名に固定する。
- v1 で用意する preset:
  - `normal_complete`
  - `ep6_timeout_once_then_recover`
  - `ep6_timeout_persistent`
  - `usb_disconnect_midstream`
  - `writer_slow_queue_pressure`
  - `publish_fail`
- 実行は `scripts\run_simulation.bat <preset>` を正本にする。

### 出力

- simulation 結果は `logs/sim/<preset>/<timestamp>/` に出す。
- 最低限以下を残す。
  - `runner.log`
  - `summary.json`
  - 生成された wave file 群
- `summary.json` には以下を含める。
  - terminal status
  - error code
  - EP6 call count
  - timeout count
  - `WAVE_WR_CNT`
  - `WAVE_RD_CNT`
  - `DDR_WR_END`
  - `DDR_RD_END`

## Build / Run ドキュメント方針

- この手のコマンドの正本は新規 `.md` ではなく `docs/BUILD.md` に集約する。
- `docs/BUILD.md` に以下を分けて記載する。
  - 実機用だけ build
  - simulation 用だけ build
  - unit test だけ build / run
  - simulation preset 実行
- 実機用だけ build コマンドは現行どおり以下を使う。
  - `cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_Dll:Rebuild /p:Configuration=Release /p:Platform=x64 /m:1"`
  - `cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_TestApp:Rebuild /p:Configuration=Release /p:Platform=x64 /m:1"`
- unit test の正本は current repo では solution target ではなく `AnalogBoard_UnitTest\build_test.bat` とする。

## テスト方針

- Red-Green-Refactor を必須にする。
- `WaveAcquisitionEngine` の contract test を先に追加し、以下を未実装状態で失敗させる。
  - 正常完走
  - timeout recover
  - persistent timeout
  - disconnect
  - queue pressure
  - publish failure
- simulation integration test では各 preset の終了コード、`summary.json`、生成物を確認する。
- 既存の `WaveDataFileIO`、`FpgaRegisterLogic`、`UsbTransferHelpers` の test は維持する。
- 実機最終確認は以下の 3 本に絞る。
  - 正常 1 cycle
  - timeout または disconnect 系 1 cycle
  - 長時間または連続取得 1 cycle

## 成功条件

- 実機用 `AnalogBoard_TestApp.exe` に simulation 用コードが入らない。
- `AnalogBoard_SimRunner.exe` から preset 名だけで simulation を実行できる。
- timeout、disconnect、writer 遅延、publish failure を実機前に自動再現できる。
- `docs/BUILD.md` に実機用 / simulation 用 / unit test 用の build・run コマンドが整理される。
