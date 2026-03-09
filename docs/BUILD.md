# AnalogBoard Build Guide (Windows / VS2022)

## 1. 前提環境

- OS: Windows
- IDE: Visual Studio 2022 (Community で可)
- 必須コンポーネント:
  - `C++ によるデスクトップ開発`
  - `MSVC v143 (x64)`
  - `Windows 10 SDK` または `Windows 11 SDK`
  - `MFC for v143 (x64)`

## 2. ビルド端末を開く

- `x64 Native Tools Command Prompt for VS 2022` を起動する
- リポジトリへ移動する

```bat
cd /d D:\ubuntu\jupyter\sys_analyzer\AnalogBoard
```

## 3. 初回準備（.vcxproj を用意）

このリポジトリは `*.vcxproj.xml` を保持しているため、ビルド前に `*.vcxproj` を作る。

```bat
copy /Y AnalogBoard_TestApp\AnalogBoard_TestApp.vcxproj.xml AnalogBoard_TestApp\AnalogBoard_TestApp.vcxproj
copy /Y AnalogBoard_Dll\AnalogBoard_Dll.vcxproj.xml AnalogBoard_Dll\AnalogBoard_Dll.vcxproj
```

## 4. ビルド（Release / x64）

このリポジトリは `x64` のみサポート。依存関係の都合で `Dll` を先にビルドする。

```bat
msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_Dll:Rebuild /p:Configuration=Release /p:Platform=x64 /m:1
msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_TestApp:Rebuild /p:Configuration=Release /p:Platform=x64 /m:1
```

## 5. 生成物

- `x64\Release\AnalogBoard_Dll.dll`
- `x64\Release\AnalogBoard_TestApp.exe`

## 6. 既存配布物との比較

```bat
certutil -hashfile x64\Release\AnalogBoard_TestApp.exe SHA256
certutil -hashfile D:\ubuntu\jupyter\sys_analyzer\AnalogBoard_TestApp_ver2.0.2.exe SHA256
```

参考: `AnalogBoard_TestApp_ver2.0.2.exe` の既知SHA256

```text
d8393310f279c5fda6568b01d1d0577a4d50863d9ef4f13a757166d719c7da80
```

## 7. よくあるエラー

### MSB8041 (MFCがない)

- 症状: `このプロジェクトには、MFC のライブラリが必要です`
- 対応: Visual Studio Installer で `MFC for v143 (x64)` を追加

### LNK1181 (`AnalogBoard_Dll.lib` を開けない)

- 症状: `..\x64\Release\AnalogBoard_Dll.lib を開けません`
- 原因: `TestApp` が先にリンクされ、`Dll.lib` が未生成
- 対応: 本書の通り `Dll` を先にビルドする

## 8. 補足

- ソリューション構成は `Debug|x64` / `Release|x64` のみ（`Win32/x86` は非対応）
- ハッシュが一致しない場合でも、タイムスタンプやビルド環境差分でバイナリ差分が出ることがある
- 完全一致を狙う場合は、ソースリビジョン / VSバージョン / SDK / ビルドオプションを固定する
