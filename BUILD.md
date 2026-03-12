# BUILD

対象 worktree: `baseline/0.1.4-hw-recovery`

## 前提

- 実行場所は repository root (`D:\ubuntu\jupyter\sys_analyzer\AnalogBoard-baseline`)
- Windows build/test は必ず `scripts\run_with_vsdevcmd.bat` を経由する
- Phase 1 では `Release|x64` の DLL / TestApp を基準に確認する
- `CyLib\x64\CyAPI.lib` と `CyLib\x86\CyAPI.lib` が worktree 内に存在すること

`CyLib\x64` / `CyLib\x86` は `.gitignore` 対象のため、`git worktree add` では自動コピーされない。baseline worktree を新規作成した直後は、必要なら次で補充する。

```bat
mkdir CyLib\x64
mkdir CyLib\x86
copy ..\AnalogBoard\CyLib\x64\CyAPI.lib CyLib\x64\CyAPI.lib
copy ..\AnalogBoard\CyLib\x86\CyAPI.lib CyLib\x86\CyAPI.lib
```

## Release Build

```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_Dll:Rebuild /p:Configuration=Release /p:Platform=x64 /m:1"
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_TestApp:Rebuild /p:Configuration=Release /p:Platform=x64 /m:1"
```

生成物:

- `x64\Release\AnalogBoard_Dll.dll`
- `x64\Release\AnalogBoard_TestApp.exe`

## Unit Test

```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"
```

## Phase 1 Gate 1

最初にやること:

1. 上記 Release build を実行する
2. `0.2.2` と同条件で実機 3-5 サイクル測定する
3. 次を確認する
   - waveform 正常
   - empty capture 0 件
   - `DDR_RD_END=1` 到達
   - EP6 timeout / disconnect なし

この Gate 1 を通すまでは、completion semantics 修正に入らない。
