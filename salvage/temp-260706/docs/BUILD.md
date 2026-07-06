# Sysmex AnalogBoard Build Guide (Windows / VS2022)

## 1. 前提環境

- OS: Windows
- IDE: Visual Studio 2022
- 必須コンポーネント:
  - `C++ によるデスクトップ開発`
  - `MSVC v143 (x64)`
  - `Windows 10 SDK` または `Windows 11 SDK`
  - `MFC for v143 (x64)` (`Sysmex_AnalogBoard_TestApp` / `Sysmex_AnalogBoard_Dll` 用)

## 2. 実行場所

```bat
cd /d D:\ubuntu\jupyter\sys_analyzer\Sysmex_AnalogBoard
```

## 3. `.vcxproj` を再生成する場合

通常は不要。`*.vcxproj` が欠けている場合だけ `*.vcxproj.xml` から復元する。

```bat
copy /Y Sysmex_AnalogBoard_TestApp\Sysmex_AnalogBoard_TestApp.vcxproj.xml Sysmex_AnalogBoard_TestApp\Sysmex_AnalogBoard_TestApp.vcxproj
copy /Y Sysmex_AnalogBoard_Dll\Sysmex_AnalogBoard_Dll.vcxproj.xml Sysmex_AnalogBoard_Dll\Sysmex_AnalogBoard_Dll.vcxproj
copy /Y Sysmex_AnalogBoard_SimRunner\Sysmex_AnalogBoard_SimRunner.vcxproj.xml Sysmex_AnalogBoard_SimRunner\Sysmex_AnalogBoard_SimRunner.vcxproj
```

## 4. 実機用だけ build

依存関係の都合で `Dll` を先に build する。

```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild Sysmex_AnalogBoard_TestApp.sln /t:Sysmex_AnalogBoard_Dll:Rebuild /p:Configuration=Release /p:Platform=x64 /m:1"
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild Sysmex_AnalogBoard_TestApp.sln /t:Sysmex_AnalogBoard_TestApp:Rebuild /p:Configuration=Release /p:Platform=x64 /m:1"
```

生成物:

- `x64\Release\Sysmex_AnalogBoard_Dll.dll`
- `x64\Release\Sysmex_AnalogBoard_TestApp.exe`

## 5. simulation 用だけ build

```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild Sysmex_AnalogBoard_TestApp.sln /t:Sysmex_AnalogBoard_SimRunner:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
```

生成物:

- `x64\Debug\Sysmex_AnalogBoard_SimRunner.exe`

## 6. UnitTest だけ build / run

この repository の UnitTest 正本は solution target ではなく `build_test.bat`。

```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat Sysmex_AnalogBoard_UnitTest\build_test.bat"
```

## 7. simulation preset 実行

`scripts\run_simulation.bat <preset>` が正本。script 内で `Sysmex_AnalogBoard_SimRunner` を rebuild してから実行する。

```bat
cmd /d /c "scripts\run_simulation.bat normal_complete"
cmd /d /c "scripts\run_simulation.bat ep6_timeout_once_then_recover"
cmd /d /c "scripts\run_simulation.bat ep6_timeout_persistent"
cmd /d /c "scripts\run_simulation.bat usb_disconnect_midstream"
cmd /d /c "scripts\run_simulation.bat writer_slow_queue_pressure"
cmd /d /c "scripts\run_simulation.bat write_fail"
cmd /d /c "scripts\run_simulation.bat publish_fail"
```

出力先:

- `logs\sim\<preset>\<timestamp>\runner.log`
- `logs\sim\<preset>\<timestamp>\summary.json`
- `logs\sim\<preset>\<timestamp>\*_fl_*.bin`
- `logs\sim\<preset>\<timestamp>\*_fh_*.bin`

## 8. 補足

- solution 構成は `Debug|x64` / `Release|x64` のみ
- `Sysmex_AnalogBoard_TestApp.exe` には simulation preset / fake USB / scenario parser は含めない
- `scripts\run_simulation.bat` の戻り値は terminal status に応じて変わる
- `0`: success
- `2`: EP6 timeout
- `3`: USB disconnect
- `5`: write failure
- `4`: publish failure
