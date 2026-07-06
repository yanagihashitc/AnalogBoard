# Sysmex AnalogBoard テストアプリケーション - 初期コミット (aebf296) アーキテクチャ詳細解説

> **対象コミット**: `aebf296` (Initial commit)
> **総ファイル数**: 42 ファイル / 約 7,865 行
> **作成日**: 2026-03-22

---

## 目次

1. [ファイル別役割一覧](#1-ファイル別役割一覧)
2. [システムアーキテクチャ全体像](#2-システムアーキテクチャ全体像)
3. [DLL層 - USB通信ライブラリ](#3-dll層---usb通信ライブラリ)
4. [TestApp層 - MFCアプリケーション](#4-testapp層---mfcアプリケーション)
5. [FPGA レジスタマップと制御プロトコル](#5-fpga-レジスタマップと制御プロトコル)
6. [波形データ取得フロー](#6-波形データ取得フロー)
7. [ビルド構成](#7-ビルド構成)

---

## 1. ファイル別役割一覧

### 1.1 ディレクトリツリー

```
Sysmex_AnalogBoard/
├── Sysmex_AnalogBoard_TestApp.sln          ソリューションファイル
├── .gitignore                               Git除外設定
│
├── CyLib/header/                            Cypress USB SDK ヘッダ群
│   ├── CyAPI.h
│   ├── CyUSB30_def.h
│   ├── cyioctl.h
│   ├── usb100.h
│   ├── usb200.h
│   ├── UsbdStatus.h
│   └── VersionNo.h
│
├── Sysmex_AnalogBoard_Dll/                  USB通信DLLプロジェクト
│   ├── Sysmex_AnalogBoard_Dll.h
│   ├── Sysmex_AnalogBoard_Dll.cpp
│   ├── Sysmex_AnalogBoard_Dll.def
│   ├── Sysmex_AnalogBoard_Dll.rc
│   ├── Sysmex_AnalogBoard_Dll.vcxproj.xml
│   ├── Sysmex_AnalogBoard_Dll.vcxproj.filters
│   ├── Resource.h
│   ├── framework.h
│   ├── pch.h / pch.cpp
│   ├── targetver.h
│   └── res/Sysmex_AnalogBoard_Dll.rc2
│
└── Sysmex_AnalogBoard_TestApp/              MFCテストアプリプロジェクト
    ├── Sysmex_AnalogBoard_TestApp.h / .cpp
    ├── Sysmex_AnalogBoard_TestAppDlg.h / .cpp
    ├── Dialog1_Main.h / .cpp
    ├── Dialog2_Debug.h / .cpp
    ├── CColorEdit.h / .cpp
    ├── SysmexAnalogBoardTestApp.rc
    ├── Sysmex_AnalogBoard_TestApp.vcxproj.xml
    ├── Sysmex_AnalogBoard_TestApp.vcxproj.filters
    ├── resource.h
    ├── default_config.csv
    ├── framework.h
    ├── pch.h / pch.cpp
    ├── targetver.h
    └── res/
        ├── SysmexAnalogBoardTestApp.rc2
        └── Sysmex_AnalogBoard_TestApp.ico
```

### 1.2 全ファイル詳細

#### CyLib/header/ — Cypress FX3 USB SDK ヘッダ（7ファイル）


| ファイル            | 行数  | 役割                                                                                                                                   |
| --------------- | --- | ------------------------------------------------------------------------------------------------------------------------------------ |
| `CyAPI.h`       | 600 | **Cypress USB SDK メインヘッダ**。`CCyUSBDevice`, `CCyUSBEndPoint`, `CCyBulkEndPoint`, `CCyInterruptEndPoint` 等の全クラス定義。デバイス検出・オープン・転送APIを提供 |
| `CyUSB30_def.h` | 90  | USB 3.0 BOS (Binary Object Store) 記述子の構造体定義。SuperSpeed機能キャパビリティ、コンテナID、USB 2.0拡張記述子                                                  |
| `cyioctl.h`     | 213 | Cypress USBドライバとのIOCTLコマンド定義。デバイス列挙、転送サイズ設定、パイプリセット等のWindows DeviceIoControl用コード                                                     |
| `usb100.h`      | 270 | USB 1.0/1.1仕様の構造体・定数。デバイス/設定/インターフェース/エンドポイント記述子、セットアップパケット、転送型定義                                                                    |
| `usb200.h`      | 111 | USB 2.0仕様の追加構造体。設定電力記述子、インターフェースアソシエーション記述子                                                                                          |
| `UsbdStatus.h`  | 46  | USBDステータスコード定義。成功/エラーコード（CRC, バイトスタッフ, タイムアウト, ストール等）                                                                                |
| `VersionNo.h`   | 26  | CyAPIライブラリのバージョン番号（ファイル/プロダクトバージョン）                                                                                                  |


#### Sysmex_AnalogBoard_Dll/ — USB通信DLL（12ファイル）


| ファイル                                     | 行数       | 役割                                                                                                  |
| ---------------------------------------- | -------- | --------------------------------------------------------------------------------------------------- |
| `Sysmex_AnalogBoard_Dll.h`               | 67       | **DLL公開API定義**。`USB_Lib_Info`クラスの宣言。エクスポート関数6個、エラーコード12種、バッファサイズ定数を定義                               |
| `Sysmex_AnalogBoard_Dll.cpp`             | 491      | **DLL実装本体**。USB接続/切断、EP2送信(256B)、EP4受信(512B→256B)、EP6大容量受信(4MB/チャンク)の全処理。Mutex排他制御、OVERLAPPED非同期I/O |
| `Sysmex_AnalogBoard_Dll.def`             | 6        | DLLモジュール定義ファイル。MFC `AFX_EXT_CLASS` によるエクスポート（明示的関数列挙なし）                                             |
| `Sysmex_AnalogBoard_Dll.rc`              | (binary) | DLLリソースファイル（バージョン情報等）。Culture: 0x0804（簡体字中国語）                                                       |
| `Sysmex_AnalogBoard_Dll.vcxproj.xml`     | 218      | **DLLビルド構成**。x64/Win32 × Debug/Release。CyAPI.lib/setupapi.lib依存。v143ツールセット                          |
| `Sysmex_AnalogBoard_Dll.vcxproj.filters` | 55       | ソリューションエクスプローラのフィルタ構成（源文件/头文件/资源文件）                                                                 |
| `Resource.h`                             | 17       | DLLリソースID定義（ダイアログなし、最小限）                                                                            |
| `framework.h`                            | 38       | MFC DLL用プリコンパイルヘッダ設定。`_AFX_NO_MFC_CONTROLS_IN_DIALOGS` 定義、`afxwin.h`インクルード                          |
| `pch.h`                                  | 13       | プリコンパイルヘッダ。`framework.h`をインクルード                                                                     |
| `pch.cpp`                                | 5        | プリコンパイルヘッダのコンパイル用ソース                                                                                |
| `targetver.h`                            | 8        | ターゲットWindowsバージョン設定（`SDKDDKVer.h`インクルード）                                                            |
| `res/Sysmex_AnalogBoard_Dll.rc2`         | (binary) | 手動編集用リソースファイル                                                                                       |


#### Sysmex_AnalogBoard_TestApp/ — MFCテストアプリ（20ファイル）


| ファイル                                         | 行数       | 役割                                                                                                      |
| -------------------------------------------- | -------- | ------------------------------------------------------------------------------------------------------- |
| `Sysmex_AnalogBoard_TestApp.h`               | 32       | **MFCアプリケーションクラス宣言**。`CWinApp`派生の`CSysmexAnalogBoardTestAppApp`。`InitInstance()`でメインダイアログを生成            |
| `Sysmex_AnalogBoard_TestApp.cpp`             | 107      | アプリケーション初期化実装。CommonControls初期化、ビジュアルマネージャ設定、メインダイアログDoModal()                                          |
| `Sysmex_AnalogBoard_TestAppDlg.h`            | 58       | **メインダイアログ宣言**。タブコントロール、Dialog1_Main/Dialog2_Debug子ダイアログ、`USB_Lib_Info`インスタンスを保持。USBホットプラグ対応            |
| `Sysmex_AnalogBoard_TestAppDlg.cpp`          | 387      | メインダイアログ実装。タブ管理、USB接続/切断、ログ出力(`PrintLog()`)、`WM_DEVICECHANGE`ハンドリング                                     |
| `Dialog1_Main.h`                             | 291      | **データ取得タブ宣言**。`FPGAConfigI_REGMAP`構造体、FPGAレジスタアドレス定数、13チャネル分のUI変数、ゲイン選択肢定数を定義                           |
| `Dialog1_Main.cpp`                           | 3,298    | **アプリケーション最大のソースファイル**。FPGAレジスタ操作(Reg_Write/Read)、ゲイン/オフセット/トリガー計算、波形取得スレッド、ファイル保存、CSV設定I/O、全UIイベントハンドラ |
| `Dialog2_Debug.h`                            | 66       | **FPGAデバッグタブ宣言**。EP2/EP4/EP6の直接操作UI。レジスタリスト(89行)、インラインエディット                                             |
| `Dialog2_Debug.cpp`                          | 525      | デバッグタブ実装。CSV形式でEP2レジスタ書込、EP4レジスタ読込、EP6バイナリデータ取得・保存。パフォーマンス計測                                            |
| `CColorEdit.h`                               | 24       | **カスタムエディットコントロール宣言**。`CEdit`派生、テキスト色・背景色をカスタマイズ可能                                                      |
| `CColorEdit.cpp`                             | 51       | CColorEdit実装。`SetForeColor()`, `SetBkColor()`, `SetTextFont()`, `CtlColor()`メッセージリフレクション                |
| `resource.h`                                 | 238      | **リソースID定義**。ダイアログID(IDD_*)、コントロールID(IDC_*)、ゲインコンボボックス・オフセット編集・トリガー設定等の全UIコントロールID                      |
| `default_config.csv`                         | 38       | **デフォルト設定ファイル**。13チャネルのゲイン値(5段階)、オフセット(1414-1494mV)、外部制御電圧、FIRフィルタ、トリガー条件、測定モード、波形数、保存パスを定義             |
| `SysmexAnalogBoardTestApp.rc`                | (binary) | メインリソースファイル（50.9KB）。全ダイアログレイアウト、メニュー、アイコン参照                                                             |
| `Sysmex_AnalogBoard_TestApp.vcxproj.xml`     | 228      | TestAppビルド構成。DLLインポートライブラリ依存。`_CRT_SECURE_NO_WARNINGS`定義                                                |
| `Sysmex_AnalogBoard_TestApp.vcxproj.filters` | 81       | フィルタ構成（Source/Header/Resource Files）                                                                    |
| `framework.h`                                | 49       | MFC App用プリコンパイルヘッダ設定。`afxcontrolbars.h`, `afxwin.h`等インクルード                                              |
| `pch.h`                                      | 13       | プリコンパイルヘッダ                                                                                              |
| `pch.cpp`                                    | 5        | プリコンパイルヘッダソース                                                                                           |
| `targetver.h`                                | 8        | ターゲットWindowsバージョン設定                                                                                     |
| `res/Sysmex_AnalogBoard_TestApp.ico`         | (binary) | アプリケーションアイコン（67.8KB）                                                                                    |


#### ソリューションルート（2ファイル）


| ファイル                             | 役割                                                                                 |
| -------------------------------- | ---------------------------------------------------------------------------------- |
| `Sysmex_AnalogBoard_TestApp.sln` | Visual Studio 2022ソリューション。2プロジェクト（TestApp + Dll）を管理。Debug/Release × x64/Win32 の4構成 |
| `.gitignore`                     | ビルド成果物、中間ファイル、IDE設定等の除外ルール                                                         |


---

## 2. システムアーキテクチャ全体像

### 2.1 システム構成図

```
┌─────────────────────────────────────────────────────────────────┐
│                    Sysmex AnalogBoard テストシステム              │
│                                                                 │
│  ┌──────────────────────────────────────────┐                   │
│  │        Sysmex_AnalogBoard_TestApp.exe     │                   │
│  │  ┌────────────────────────────────────┐  │                   │
│  │  │  CSysmexAnalogBoardTestAppDlg      │  │                   │
│  │  │  (メインダイアログ・タブ管理・ログ)   │  │                   │
│  │  │                                    │  │                   │
│  │  │  ┌──────────┐  ┌──────────────┐   │  │                   │
│  │  │  │Dialog1   │  │Dialog2       │   │  │                   │
│  │  │  │_Main     │  │_Debug        │   │  │                   │
│  │  │  │          │  │              │   │  │                   │
│  │  │  │データ取得 │  │FPGAレジスタ  │   │  │                   │
│  │  │  │・13ch設定│  │直接操作      │   │  │                   │
│  │  │  │・波形取得│  │・EP2/EP4/EP6 │   │  │                   │
│  │  │  │・ファイル│  │  テスト      │   │  │                   │
│  │  │  │  保存   │  │              │   │  │                   │
│  │  │  └──────────┘  └──────────────┘   │  │                   │
│  │  └────────────────────────────────────┘  │                   │
│  │              │ USB_Lib_Info               │                   │
│  └──────────────┼────────────────────────────┘                   │
│                 ▼                                                │
│  ┌──────────────────────────────────────────┐                   │
│  │     Sysmex_AnalogBoard_Dll.dll            │                   │
│  │                                           │                   │
│  │  USB_Lib_Info クラス                       │                   │
│  │  ├─ USBBoard_Connect()                    │                   │
│  │  ├─ USBBoard_Disconnect()                 │                   │
│  │  ├─ EP2_SendData()    [256B, Interrupt]   │                   │
│  │  ├─ EP4_GetData()     [256B, Interrupt]   │                   │
│  │  ├─ EP6_GetData()     [4MB/chunk, Bulk]   │                   │
│  │  └─ DllVersion_Get()                      │                   │
│  │              │ Cypress CyAPI              │                   │
│  └──────────────┼────────────────────────────┘                   │
│                 ▼                                                │
│  ┌──────────────────────────────────┐                            │
│  │  CyAPI.lib (Cypress USB SDK)     │                            │
│  │  CCyUSBDevice / CCyUSBEndPoint   │                            │
│  └──────────────┬───────────────────┘                            │
│                 │ USB 3.0 / 2.0                                  │
└─────────────────┼───────────────────────────────────────────────┘
                  ▼
┌─────────────────────────────────────────────────────────────────┐
│              Cypress FX3 USB Controller                          │
│              VID: 0x04B4                                         │
│              PID: 0xFFF2 (USB3.0) / 0xFFF3 (USB2.0)            │
│                                                                 │
│  EP2 (OUT, Interrupt) ──→ FPGA コマンド受信                      │
│  EP4 (IN, Interrupt)  ←── FPGA ステータス送信                    │
│  EP6 (IN, Bulk)       ←── DDR 波形データ送信                     │
└─────────────────────────────────────────────────────────────────┘
                  ▼
┌─────────────────────────────────────────────────────────────────┐
│                    FPGA (Altera/Intel)                            │
│                                                                 │
│  ・13チャネル アナログ信号取得                                     │
│  ・ゲイン/オフセット/DAC 制御                                     │
│  ・FIR フィルタ (15MHz/25MHz)                                    │
│  ・トリガー検出・波形キャプチャ                                   │
│  ・DDR3 メモリへの波形データ書込                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 レイヤー構成


| レイヤー          | コンポーネント                                 | 責務                              |
| ------------- | --------------------------------------- | ------------------------------- |
| **UI層**       | Dialog1_Main, Dialog2_Debug, CColorEdit | ユーザーインタラクション、パラメータ入力、結果表示       |
| **アプリケーション層** | CSysmexAnalogBoardTestAppDlg            | タブ管理、USB接続管理、ログ出力、ホットプラグ対応      |
| **ビジネスロジック層** | Dialog1_Main (レジスタ操作、波形取得)              | FPGA設定計算、波形取得フロー制御、ファイル保存       |
| **通信抽象層**     | USB_Lib_Info (DLL)                      | USB通信のカプセル化、エンドポイント管理、Mutex排他制御 |
| **デバイスドライバ層** | CyAPI.lib                               | Cypress USB ドライバとのインターフェース      |
| **ハードウェア層**   | FX3 + FPGA                              | アナログ信号取得、DDR制御                  |


---

## 3. DLL層 - USB通信ライブラリ

### 3.1 USB_Lib_Info クラス

```cpp
class AFX_EXT_CLASS USB_Lib_Info
{
public:
    BOOL isConnected;                          // 接続状態フラグ

    USB_Lib_Info(void);                        // Mutex生成、変数初期化
    ~USB_Lib_Info(void);                       // BOS解放、デバイス削除

    INT USBBoard_Connect(HWND Hwd);            // USB接続・EP初期化
    void USBBoard_Disconnect(void);            // 切断・リソース解放
    INT EP2_SendData(BYTE* pSendData);         // 256B コマンド送信
    INT EP4_GetData(BYTE* pRevData);           // 256B ステータス受信
    INT EP6_GetData(BYTE* pRevData, UINT DataSizeCount); // 大容量データ受信
    const char* DllVersion_Get(void);          // "1.0.0"

private:
    CCyUSBDevice*    m_pUSBDevice;             // Cypress デバイス
    CCyUSBEndPoint*  m_pOutEndpt2;             // EP2 OUT (Interrupt)
    CCyUSBEndPoint*  m_pInEndpt4;              // EP4 IN (Interrupt)
    CCyUSBEndPoint*  m_pInEndpt6;              // EP6 IN (Bulk)
    HANDLE           m_hEP2EP4Mutex;           // EP2/EP4/EP6 排他Mutex
};
```

### 3.2 エンドポイント構成


| EP      | アドレス | 転送型                      | 方向  | バッファサイズ       | 用途          |
| ------- | ---- | ------------------------ | --- | ------------- | ----------- |
| **EP2** | 0x02 | Interrupt (Attributes=3) | OUT | 256 B         | FPGAコマンド送信  |
| **EP4** | 0x84 | Interrupt (Attributes=3) | IN  | 512 B（有効256B） | FPGAステータス受信 |
| **EP6** | 0x86 | Bulk (Attributes=2)      | IN  | 最大4MB/チャンク    | 波形データ大容量受信  |


### 3.3 エラーコード体系


| コード                           | 値   | 説明                           |
| ----------------------------- | --- | ---------------------------- |
| `USB_SUCCESS`                 | 0   | 成功                           |
| `USB_DEV_USB20`               | 1   | USB 2.0デバイスとして接続成功           |
| `USB_ERR_NODEV`               | -1  | デバイスが見つからない                  |
| `USB_ERR_PARAM`               | -2  | 無効なパラメータ（NULLポインタ、バッファサイズ不足） |
| `USB_ERR_OPENDEV_FAILED`      | -3  | デバイスオープン失敗                   |
| `USB_ERR_SETINTERFACE_FAILED` | -4  | インターフェース設定失敗                 |
| `USB_ERR_ALLOCMEM_FAILED`     | -5  | メモリ確保失敗（malloc失敗）            |
| `USB_ERR_NULLPOINTER`         | -6  | NULLポインタ参照（主にHWND）           |
| `USB_ERR_INVALID_ENDPOINTER`  | -7  | エンドポイントが無効                   |
| `USB_ERR_VENDOR_ID_ERR`       | -8  | ベンダーID不一致（期待: 0x04B4）        |
| `USB_ERR_PRODUCT_ID_ERR`      | -9  | プロダクトID不一致                   |
| `USB_ERR_TRANSFER_TIMEOUT`    | -10 | USB転送タイムアウト                  |
| `USB_ERR_UNAVAILABLE`         | -11 | Mutex獲得失敗（EP2/EP4使用中）        |


### 3.4 USBBoard_Connect() 処理フロー

```
1. HWNDの有効性チェック
2. CCyUSBDevice インスタンス生成
3. DeviceCount() でデバイス数取得 → 0ならエラー
4. 各デバイスについてループ:
   a. Open(i) → 失敗時 Reset() して再試行
   b. VendorID 検証 (0x04B4 = Cypress)
   c. ProductID 検証 (0xFFF2=USB3.0 / 0xFFF3=USB2.0)
   d. BcdUSB 検証 (0x300 or 0x200)
   e. AltIntfcCount() → SetAltIntfc() で各インターフェース設定
   f. EndPointCount() でエンドポイント列挙
   g. エンドポイントフィルタリング:
      ・Attributes=3, Address=0x02 → m_pOutEndpt2
      ・Attributes=3, Address=0x84 → m_pInEndpt4
      ・Attributes=2, Address=0x86 → m_pInEndpt6
5. isConnected = TRUE
```

### 3.5 EP4_GetData() のダミーデータ処理

EP4は512バイト受信するが、有効データはオフセット256以降の256バイトのみ:

```cpp
// 512バイト受信
m_pInEndpt4->XferData(pOneTimeBuffer, 512, FALSE);
// 後半256バイトのみユーザに返却
memcpy(pRevData, pOneTimeBuffer + 256, 256);
```

### 3.6 EP6_GetData() のチャンク分割転送

```cpp
while (ulRecvDataSize < DataSizeCount) {
    // 1回の転送サイズ: min(残りサイズ, 4MB)
    lOneTimeSize = min(DataSizeCount - ulRecvDataSize, EP6_ONETIME_MAX_SIZE);

    // XferData で転送実行
    if (!m_pInEndpt6->XferData(pOneTimeBuffer, lOneTimeSize, FALSE))
        return USB_ERR_TRANSFER_TIMEOUT;

    // ユーザバッファにコピー
    memcpy(pRevData + ulRecvDataSize, pOneTimeBuffer, lOneTimeSize);
    ulRecvDataSize += lOneTimeSize;
}
```

### 3.7 スレッド安全性（Mutex）

```
m_hEP2EP4Mutex: 全エンドポイント操作を排他制御
├─ EP2_SendData(): WaitForSingleObject(INFINITE) → 転送 → ReleaseMutex
├─ EP4_GetData():  WaitForSingleObject(INFINITE) → 転送 → ReleaseMutex
└─ EP6_GetData():  WaitForSingleObject(INFINITE) → 転送 → ReleaseMutex
    ※EP6もMutex保護下 → 大容量転送中はEP2/EP4がブロックされる
```

---

## 4. TestApp層 - MFCアプリケーション

### 4.1 アプリケーション起動フロー

```
CSysmexAnalogBoardTestAppApp::InitInstance()
  │
  ├─ CommonControlsEx 初期化
  ├─ CMFCVisualManager::SetDefaultManager (Windowsネイティブテーマ)
  │
  └─ CSysmexAnalogBoardTestAppDlg dlg → DoModal()
       │
       └─ OnInitDialog()
            ├─ タブコントロール初期化
            │  ├─ タブ0: "Data Get" → Dialog1_Main
            │  └─ タブ1: "FPGA Debug" → Dialog2_Debug（コメント化）
            │
            ├─ UsbLibInfo.USBBoard_Connect(m_hWnd)
            │  ├─ USB3.0 → "Connect board success(USB3.0)!"
            │  ├─ USB2.0 → "Connect board success(USB2.0)!"
            │  └─ 失敗   → エラーメッセージ
            │
            └─ WM_DEVICECHANGE 処理登録（ホットプラグ対応）
```

### 4.2 CSysmexAnalogBoardTestAppDlg

**責務**: タブダイアログ管理 + USB通信の統括


| メンバ                  | 型               | 役割          |
| -------------------- | --------------- | ----------- |
| `m_tab_main`         | `CTabCtrl`      | メインタブコントロール |
| `m_tabpage1_DataGet` | `Dialog1_Main`  | データ取得タブ     |
| `m_tabpage2_FpgaDbg` | `Dialog2_Debug` | FPGAデバッグタブ  |
| `UsbLibInfo`         | `USB_Lib_Info`  | USB通信インスタンス |


**PrintLog()**: タイムスタンプ付きログをリストボックスに出力

```
フォーマット: YYYYMMDD HH:MM:SS.mmm>> [message]
例: 20260321 14:30:45 123>> EP2 Send success!
```

### 4.3 Dialog1_Main — データ取得タブ（3,298行）

#### 4.3.1 FPGAConfigI_REGMAP 構造体

全FPGA設定パラメータを保持する構造体:

```cpp
struct FPGAConfigI_REGMAP {
    double  GainCh[13][5];      // ゲイン値（CH1～CH13、Gain1～Gain5）
    FLOAT   OffsetValue[13];    // オフセット値（1414～1494 mV）
    USHORT  ExtCtrlVol1[5];     // 外部制御電圧1（CH9～CH13、0～1100/5000 mV）
    USHORT  ExtCtrlVol2[6];     // 外部制御電圧2（CH3～CH8、0～4096 mV）
    INT     FirFilterFC;        // FIRフィルタ（0:15MHz/40Mbps, 1:25MHz/60Mbps）
    INT     CHSelect[13];       // チャネル選択（0/1）
    INT     TriggerCh;          // トリガーチャネル（1～13）
    USHORT  TriggerValue;       // トリガー値（0～1800 mV）
    FLOAT   TriggerRange[2];    // トリガー範囲（[0]:負 -55us, [1]:正 +55us）
    INT     ManualMode;         // 測定モード（0:自動, 1:手動）
    UINT    WaveNum;            // ファイルあたりの波形数
    CString SavePath;           // 保存先パス
};
```

#### 4.3.2 UI要素一覧


| カテゴリ     | コントロール型                | 数量              | 説明                |
| -------- | ---------------------- | --------------- | ----------------- |
| ゲイン選択    | CComboBox              | 65 (13CH × 5段階) | Gain1～Gain5の選択    |
| Gain3手入力 | CColorEdit             | 8 (CH1～8)       | CH1-8のGain3は連続値入力 |
| オフセット    | CColorEdit             | 13              | 1414～1494 mV      |
| 外部制御電圧1  | CColorEdit             | 5               | CH9～CH13          |
| 外部制御電圧2  | CColorEdit             | 6               | CH3～CH8           |
| トリガー     | CComboBox + CColorEdit | 4               | チャネル・値・範囲(±)      |
| チャネル選択   | CButton                | 14              | CH1～CH13 + 全選択    |
| 測定モード    | CButton (Radio)        | 2               | 手動/自動             |
| FIRフィルタ  | CComboBox              | 1               | 15MHz/25MHz       |
| 波形数      | CColorEdit             | 1               | ファイルあたり           |
| 保存パス     | CEdit                  | 1               | フォルダパス            |


#### 4.3.3 レジスタ操作関数

**低レベルアクセス**:

```cpp
Reg_Write(Address, Data, Ep2Buffer)  // 16bit値をLittle Endianで書込
Reg_Read(Address, Ep4Buffer)         // 16bit値をLittle Endianで読込
```

**高レベル設定関数**:


| 関数                           | 操作                           |
| ---------------------------- | ---------------------------- |
| `RegSet_SetGainValue()`      | Gain3値の計算・書込 + Gain1/2スイッチ設定 |
| `RegSet_SetOffsetValue()`    | オフセット DAC値計算・書込              |
| `RegSet_SetExtCtrlVol_1/2()` | 外部制御電圧 DAC値計算・書込             |
| `RegSet_UpdateGainValue()`   | ゲイン更新トリガー発行                  |
| `RegSet_UpdateOffsetValue()` | オフセット更新トリガー発行                |
| `RegSet_UpdateExtCtrlVol()`  | DAC更新トリガー発行                  |
| `RegSet_SelectFirFilterFC()` | FIRフィルタ周波数選択                 |
| `RegSet_SelectDataCH()`      | データ取得チャネル選択                  |
| `RegSet_SelectTRGCH()`       | トリガーチャネル選択                   |
| `RegSet_SetTRGValue()`       | トリガー値設定                      |
| `RegSet_SetTRGRange()`       | トリガー範囲設定                     |
| `RegSet_SelectGetDataMeas()` | 測定モード選択                      |
| `RegSet_GetWaveDataStart()`  | 波形取得開始/停止                    |


**ステータス読出し関数**:


| 関数                       | 読込内容                     |
| ------------------------ | ------------------------ |
| `RegGet_DDRWaveCnt()`    | DDR書込バイト数（32bit、2レジスタ結合） |
| `RegGet_DDRWriteEnd()`   | DDR書込完了フラグ（FPGA_ST[2]）   |
| `RegGet_SampleStartSt()` | FPGA測定開始フラグ（FPGA_ST[4]）  |


#### 4.3.4 ゲイン値計算の詳細

**Gain3（CH1-8、連続値）**:

```
範囲: -1.0 ～ -0.5
量子化精度: 0.5 / 511 ≈ 0.00098
変換式: usData = (-0.5 - GainValue) / accuracy + 0.5 + 0x200
```

**Gain1/2（全チャネル、2値選択）**:

```
各チャネルで2つの固定値から選択
レジスタ: 4チャネルごとに4bitフィールド（CH1-4, CH5-8, CH9-12 + CH13別）
```

**オフセット**:

```
範囲: 1414 ～ 1494 mV
精度: 80.0 / 255.0 ≈ 0.314
変換式: usData = 255 - (value - 1414.0) / accuracy + 0.5
```

**外部制御電圧**:

```
範囲: 0 ～ 5000 mV
精度: 5000.0 / 65535.0 ≈ 0.076
変換式: usData = voltage / accuracy + 0.5
```

**トリガー値**:

```
範囲: 0 ～ 1800 mV
精度: 2000.0 / 16383.0 ≈ 0.122
変換式: usData = value / accuracy + 0.5
```

**トリガー範囲**:

```
範囲: -55 ～ +55 μs
変換式: usData = value × 40 （40Mspsサンプリング → 2200サンプル最大）
```

#### 4.3.5 ファイル保存

**ファイル命名規則**:

```
{SavePath}\YYMMDD_HHMM_N_cfg.txt     設定ファイル（CSV）
{SavePath}\YYMMDD_HHMM_N_fl_1.bin    波形データ（低周波 CH1-8）
{SavePath}\YYMMDD_HHMM_N_fh_1.bin    波形データ（高周波 CH9-13）
{SavePath}\YYMMDD_HHMM_N_fl_2.bin    次のファイル...
```

**波形保存関数**:

```cpp
SaveWaveDataToFile(fp_h, fp_l, WaveData, FrameSize_L, FrameSize_H, WaveCnt)
// 1波形 = FrameSize_L(低周波) + FrameSize_H(高周波) のインターリーブ
for (i = 0; i < WaveCnt; i++) {
    fp_l->Write(低周波データ, FrameSize_L);
    fp_h->Write(高周波データ, FrameSize_H);
}
```

#### 4.3.6 設定ファイルI/O（CSV形式）

`ImportDefaultConfigFile()` / `ExportDefaultConfigFile()` で `default_config.csv` を読み書き:

```
# Gain Value Set
行2～9:    CH1～CH8のゲイン値（Gain1,2,3,4,5）
行11～15:  CH9～CH13のゲイン値
# Offset Value Set
行17:      CH1～CH13のオフセット値
# Ext Ctrl Vol Set
行20:      外部制御電圧1（CH9～13）
行21:      外部制御電圧2（CH3～8）
# Fir filter FC Set
行25:      フィルタ選択（0/1）
# Trigger Set
行29:      チャネル選択ビットマスク
行30:      トリガーチャネル
行31:      トリガー値
行32:      トリガー範囲
# Wave Get Set
行35:      測定モード
行36:      波形数
行37:      保存パス
```

### 4.4 Dialog2_Debug — FPGAデバッグタブ

**責務**: EP2/EP4/EP6の直接操作によるFPGAレジスタのデバッグ

**レジスタリスト**: 89行 (アドレス 0x000000～0x0000B0、2バイトステップ)


| 機能             | 操作フロー                                    |
| -------------- | ---------------------------------------- |
| **EP2送信テスト**   | CSVファイル読込 → リストに表示 → EP2_SendData()      |
| **EP4読込テスト**   | EP4_GetData() → リストに読込値表示                |
| **EP6データ取得**   | サイズ指定（16進数） → EP6_GetData() → バイナリファイル保存 |
| **インラインエディット** | リスト列1ダブルクリック → 値編集 → フォーカス喪失で反映          |


パフォーマンス計測: `QueryPerformanceCounter()` で転送時間を測定

### 4.5 CColorEdit — カスタム入力コントロール

`CEdit` 派生クラス。テキスト色・背景色を動的に変更可能:

```cpp
// パラメータエラー時のハイライト表示
EditCtrl_HighLight(CColorEdit* EditCtrl, BOOL HLFlag)
├─ HLFlag = TRUE:  背景 RGB(255, 96, 96)  赤色（エラー）
└─ HLFlag = FALSE: 背景 RGB(255, 255, 255) 白色（正常）
```

---

## 5. FPGA レジスタマップと制御プロトコル

### 5.1 レジスタマップ

EP2で送信する256バイトバッファ内のアドレス配置:


| アドレス          | 名称                | ビット幅  | R/W | 説明                                           |
| ------------- | ----------------- | ----- | --- | -------------------------------------------- |
| 0x0004        | FPGA_ST           | 16    | R   | FPGAステータス。bit2=DDR_WR_END, bit4=SAMPLE_START |
| 0x0006        | DAT_CH_SEL        | 16    | W   | データチャネル選択（13bitビットマスク）                       |
| 0x0008        | TRG_SEL           | 16    | W   | トリガーチャネル選択                                   |
| 0x000A        | TRG_THR           | 16    | W   | トリガー閾値（0～1800mV → 0～16383）                   |
| 0x000C        | TRG_RANGE_N       | 16    | W   | トリガー範囲 負側（0～55μs × 40 = サンプル数）               |
| 0x000E        | TRG_RANGE_P       | 16    | W   | トリガー範囲 正側                                    |
| 0x0010        | MEAS_MODE         | 16    | W   | 測定モード（0:自動, 1:手動）                            |
| 0x0012        | MANUAL_MEAS_ON    | 16    | W   | 手動測定開始/停止                                    |
| 0x0014        | FILTER_SEL        | 16    | W   | FIRフィルタ選択（0:15MHz, 1:25MHz）                  |
| 0x0018        | WAVE_WR_CNT_L     | 16    | R   | DDR書込カウント Low 16bit                          |
| 0x001A        | WAVE_WR_CNT_H     | 16    | R   | DDR書込カウント High 16bit                         |
| 0x0020-0x002E | GAIN_DAT_CH1-8    | 16×8  | W   | CH1～CH8 Gain3値（10bit DAC）                    |
| 0x0040        | GAIN_SW_CH1_4     | 16    | W   | CH1-4 Gain1/2 スイッチ（4bit×4ch）                 |
| 0x0042        | GAIN_SW_CH5_8     | 16    | W   | CH5-8 Gain1/2 スイッチ                           |
| 0x0044        | GAIN_SW_CH9_12    | 16    | W   | CH9-12 Gain1/2 スイッチ                          |
| 0x0046        | GAIN_SW_CH13      | 16    | W   | CH13 Gain1/2 スイッチ                            |
| 0x0050        | GAIN_TRG          | 16    | W   | ゲイン更新トリガー（書込で反映）                             |
| 0x0060-0x0078 | OFFSET_DAT_CH1-13 | 16×13 | W   | CH1～CH13 オフセットDAC値（8bit）                     |
| 0x0080        | OFFSET_TRG        | 16    | W   | オフセット更新トリガー                                  |
| 0x0090-0x009A | DAC_DAT_CH3-8     | 16×6  | W   | 外部制御電圧Vol2（CH3～CH8、16bit DAC）                |
| 0x009C-0x00A4 | DAC_DAT_CH9-13    | 16×5  | W   | 外部制御電圧Vol1（CH9～CH13、16bit DAC）               |
| 0x00B0        | DAC_TRG           | 16    | W   | DAC更新トリガー                                    |


### 5.2 EP2 コマンド送信シーケンス

```
1. RegSet_SelectDataCH()        チャネル選択ビットマスク → DAT_CH_SEL
2. RegSet_SelectTRGCH()         トリガーチャネル → TRG_SEL
3. RegSet_SetTRGValue()         トリガー値 → TRG_THR
4. RegSet_SetTRGRange(0, val)   負側範囲 → TRG_RANGE_N
5. RegSet_SetTRGRange(1, val)   正側範囲 → TRG_RANGE_P
6. RegSet_SelectFirFilterFC()   フィルタ選択 → FILTER_SEL
7. RegSet_SetGainValue()        Gain3値 → GAIN_DAT_CH1-8
                                Gain1/2 → GAIN_SW_CH1_4/5_8/9_12/13
8. RegSet_UpdateGainValue()     → GAIN_TRG（ゲイン反映）
9. RegSet_SetOffsetValue()      → OFFSET_DAT_CH1-13
10. RegSet_UpdateOffsetValue()  → OFFSET_TRG（オフセット反映）
11. RegSet_SetExtCtrlVol_1/2()  → DAC_DAT_CH3-13
12. RegSet_UpdateExtCtrlVol()   → DAC_TRG（DAC反映）
13. RegSet_SelectGetDataMeas()  → MEAS_MODE
14. EP2_SendData(buffer)        256B送信
```

### 5.3 EP4 ステータス読出し

```
EP4_GetData(buffer)  → 256B受信

RegGet_SampleStartSt():
  → FPGA_ST (0x0004) の bit4 を読出
  → 1: FPGA測定開始済み

RegGet_DDRWriteEnd():
  → FPGA_ST (0x0004) の bit2 を読出
  → 1: DDR書込完了

RegGet_DDRWaveCnt():
  → WAVE_WR_CNT_H (0x001A) << 16 | WAVE_WR_CNT_L (0x0018)
  → DDRに書き込まれた総バイト数（32bit）
```

---

## 6. 波形データ取得フロー

### 6.1 波形サイズ計算

```
FIRフィルタ = 0 (15MHz, 40Mbps):
  低周波 (CH1-8): 80 byte/wave/ch
  高周波 (CH9-13): 40 byte/wave/ch

FIRフィルタ = 1 (25MHz, 60Mbps):
  低周波 (CH1-8): 80 byte/wave/ch
  高周波 (CH9-13): 60 byte/wave/ch

OneWaveSize = Σ(選択CH × CHサイズ) × TriggerRange(サンプル数)
OneFileSize = OneWaveSize × WaveNum
```

### 6.2 取得スレッド処理フロー

```
LoopTestProcessThread_EP6_GetData()  ← AfxBeginThread で起動
│
├─ 1. バッファ確保
│     pEp6DataBuf1, pEp6DataBuf2 = malloc(256MB × 2) ダブルバッファ
│
├─ 2. 手動モード: FPGA測定開始
│     RegSet_GetWaveDataStart(TRUE)
│     → MANUAL_MEAS_ON レジスタに 1 を書込
│     → EP2_SendData() で送信
│
├─ 3. DDR書込完了待機ループ
│  │
│  ├─ EP4_GetData() で DDRステータス読込
│  │   ├─ RegGet_DDRWriteEnd() → DDR_WR_END フラグ確認
│  │   └─ RegGet_DDRWaveCnt() → DDR書込バイト数確認
│  │
│  ├─ DDRサイズからUSB転送量を計算
│  │   ├─ 最大: 256MB まとめ読込
│  │   └─ 4KB (0x1000) 境界アライン
│  │
│  ├─ EP6_GetData(buffer, readSize) で波形データ読込
│  │
│  ├─ 読込データの処理
│  │   ├─ 波形サイズ (OneWaveSize) で分割
│  │   ├─ WaveNum 境界でファイル分割
│  │   │   CreateWaveDataFile() → _fl_N.bin / _fh_N.bin
│  │   └─ SaveWaveDataToFile() で低周波/高周波分離保存
│  │
│  └─ ダブルバッファ切替
│       残余データを次バッファにコピー
│
├─ 4. 手動モード: FPGA測定停止
│     RegSet_GetWaveDataStart(FALSE)
│
├─ 5. 設定ファイル保存
│     SaveCfgParametersToFile() → _cfg.txt
│
└─ 6. クリーンアップ
      free(pEp6DataBuf1, pEp6DataBuf2)
      SamplingUISet() → UI復帰
```

### 6.3 ダブルバッファリング戦略

```
読込1: pEp6DataBuf1 に読込 → 処理 → 残余を pEp6DataBuf2 にコピー
読込2: pEp6DataBuf2 に読込（残余の後ろに追加）→ 処理 → 残余を pEp6DataBuf1 にコピー
読込3: ...

残余サイズ = 読込バイト数 - (処理波形数 × OneWaveSize)
```

---

## 7. ビルド構成

### 7.1 ソリューション構成


| プロジェクト                         | 種類                  | 依存関係                       |
| ------------------------------ | ------------------- | -------------------------- |
| **Sysmex_AnalogBoard_Dll**     | MFC Dynamic Library | CyAPI.lib, setupapi.lib    |
| **Sysmex_AnalogBoard_TestApp** | MFC Application     | Sysmex_AnalogBoard_Dll.lib |


ビルド構成: Debug/Release × x64/Win32（x64がメイン、Win32は不完全）

### 7.2 ビルド設定詳細


| 項目         | 値                  |
| ---------- | ------------------ |
| IDE        | Visual Studio 2022 |
| ツールセット     | v143               |
| C++標準      | C++17              |
| ターゲットOS    | Windows 10+        |
| MFC使用      | Dynamic Link       |
| プリコンパイルヘッダ | pch.h              |
| SDL チェック   | 有効                 |


### 7.3 プリプロセッサ定義

**DLL (x64)**:

- `_WINDOWS`, `_AFXEXT` (MFC Extension DLL)
- Debug: `_DEBUG` / Release: `NDEBUG`

**TestApp (x64)**:

- `_WINDOWS`, `_CRT_SECURE_NO_WARNINGS`
- Debug: `_DEBUG` / Release: `NDEBUG`

### 7.4 リンク依存

```
Sysmex_AnalogBoard_TestApp.exe
  └─ Sysmex_AnalogBoard_Dll.lib (Import Library)
       └─ CyAPI.lib          (Cypress USB SDK, CyLib/x64/)
       └─ setupapi.lib       (Windows Setup API)
       └─ legacy_stdio_definitions.lib
```

### 7.5 出力ディレクトリ

```
x64/Debug/
  ├─ Sysmex_AnalogBoard_Dll.dll
  ├─ Sysmex_AnalogBoard_Dll.lib
  └─ Sysmex_AnalogBoard_TestApp.exe

x64/Release/
  ├─ Sysmex_AnalogBoard_Dll.dll
  ├─ Sysmex_AnalogBoard_Dll.lib
  └─ Sysmex_AnalogBoard_TestApp.exe
```

### 7.6 既知の構成上の注記

1. **Win32構成の不完全性**: CyAPI.libの参照パスがx64のみ設定。Win32ビルドはリンクエラーになる
2. **リソース言語の混在**: DLL=簡体字中国語(0x0804)、TestApp=英語(0x0409)。テンプレート流用の痕跡
3. **初期コミット時点ではテストなし**: ユニットテスト、ビルドスクリプトは後のコミットで追加

---

## 付録A: デフォルト設定値 (default_config.csv)


| パラメータ            | デフォルト値                                                     |
| ---------------- | ---------------------------------------------------------- |
| Gain (低周波CH1-8)  | Gain1=-0.4, Gain2=-4.0, Gain3=-0.5, Gain4=2.0, Gain5=-1.35 |
| Gain (高周波CH9-13) | Gain1=-0.5, Gain2=-1.0, Gain3/4/5=1.0                      |
| Offset           | 基本1450mV（CH9=1420, CH12=1416, CH13=1494）                   |
| ExtCtrlVol1      | CH9-12=1100mV, CH13=5000mV                                 |
| ExtCtrlVol2      | CH3-7=4096mV, CH8=4095mV                                   |
| FIRフィルタ          | 1 (25MHz/60Mbps)                                           |
| チャネル選択           | 全チャネル有効                                                    |
| トリガーチャネル         | CH13                                                       |
| トリガー値            | 1800 mV                                                    |
| トリガー範囲           | -50.0 ～ +50.0 μs                                           |
| 測定モード            | 手動 (1)                                                     |
| 波形数              | 10,000 /ファイル                                               |
| 保存パス             | E:\Projects\KD23026A-Sysmex_ANA\wavedata\test_07           |


## 付録B: 初期コミット時点での設計上の特徴と制約

1. **グローバルバッファ共有**: `pEp2DataBuf`, `pEp4DataBuf`, `pEp6DataBuf1/2` はグローバル変数。スレッド間同期メカニズムなし
2. **単一Mutexによる全EP排他**: EP6大容量転送中はEP2/EP4もブロックされ、ステータスポーリングが停止
3. **EP6リトライ機構なし**: タイムアウト発生時は即座にエラー終了（後のコミットで `Ep6TransferRetryPolicy` 追加）
4. **OVERLAPPEDハンドルリーク**: `CreateEvent()` で生成したイベントハンドルが `CloseHandle()` されていない
5. **Dialog2_Debug のタブ追加がコメント化**: 初期状態ではData Getタブのみ表示
6. `**LoopTestProcessThread_EP2_EP4()` が `#if 0` で無効化**: EP4ポーリング用の別スレッドが実装されているが無効

