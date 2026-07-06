# Git 履歴移行手順（Linux）：Sysmex 除去・AnalogBoard へ GitHub ミラー push

**前提**

- 作業環境は **Linux**（bash）。Windows の `findstr` や PowerShell の行継続は使わない。
- 以下のパスは **`/workspace` を作業のルート**として記載する（コンテナ等でマウント先が違う場合は読み替える）。
- **旧リポジトリ**＝これまで使っていたリモート（通常 `origin` の URL）。
- **新リポジトリ**＝GitHub に新規作成した `AnalogBoard` の URL。
- 履歴を書き換えるため **全コミットの SHA は変わる**。旧リポジトリは移行完了まで GitHub 上に残す。

---

## 用語

| 用語 | 意味 |
|------|------|
| 旧 URL | 既存リポジトリの `git remote` の URL（`git remote -v` の `origin` など） |
| 新 URL | 新しい GitHub リポジトリ AnalogBoard の clone URL（HTTPS または SSH） |
| ミラークローン | `git clone --mirror` で作った、履歴書き換え専用の bare リポジトリ |

---

## 事前準備

### 旧 URL の確認

通常の作業用クローンで:

```bash
git remote -v
```

`fetch` に表示される URL が **旧 URL**。

### git-filter-repo

```bash
pip install git-filter-repo
# またはディストリビューションのパッケージ
git filter-repo -h   # 動作確認
```

### dubious ownership（コンテナ・別ユーザーでよく出る）

ミラーで `git` が拒否する場合:

```bash
git config --global --add safe.directory /workspace/AnalogBoard-history-mirror.git
```

---

## フェーズ A: ミラークローン（旧 URL）

作業用の親ディレクトリ **`/workspace`** で:

```bash
cd /workspace
git clone --mirror <OLD_URL> AnalogBoard-history-mirror.git
cd AnalogBoard-history-mirror.git
```

- 作業ツリーはない（bare）のままでよい。
- 以降の `git filter-repo` は **このディレクトリ内**で実行する。

---

## フェーズ B: scrub.txt（blob とコミットメッセージ共通）

**ミラーの親ディレクトリ**（`ls` すると `AnalogBoard-history-mirror.git` と並ぶ場所）に `scrub.txt` を置く。

例:

```text
/workspace/
  AnalogBoard-history-mirror.git/
  scrub.txt
```

`scrub.txt` の内容（**3 段**にすると、`CSysmexAnalogBoardTestAppApp` や `SYSMEXUSBDRV_GUID`、`RootNamespace` 内の `SysmexAnalogBoardDll` など **識別子の途中に埋まった sysmex** も落とせる）:

```text
# Sysmex + following spaces/underscores (case-insensitive)
regex:(?i)sysmex[\s_]+==>

# Remaining standalone token (case-insensitive)
regex:(?<![A-Za-z0-9])sysmex(?![A-Za-z0-9])==>

# Any remaining substring "sysmex" (CamelCase / SYSMEXUSBDRV_GUID / vcxproj 内の旧 .rc 名など)
regex:(?i)sysmex==>
```

- 上から順に評価される想定で書いてある。最終行は **英単語に偶然含まれる `sysmex`** にもマッチしうるが、このリポジトリでは実害はほぼ無い想定。

### 詳細: `SYSMEXUSBDRV_GUID` と INF／ドライバ、置換後の確認

`regex:(?i)sysmex==>` は識別子の **部分一致**でも `sysmex` を削除する。例:

- `SYSMEXUSBDRV_GUID` → **`USBDRV_GUID`**（先頭の `SYSMEX` が大文字小文字無視で `sysmex` とみなされ、空に置換される）

**GUID の「値」は変わらない。** ソース上はいまも次のような形のはずである。

```cpp
static GUID USBDRV_GUID = { 0xA123DFB8, 0x6F1E, 0x49F4, { 0x93, 0xF4, ... } };
```

デバイス列挙やドライババインドで実際に比較されるのは **16 バイトの GUID** である。**C の変数名を変えても、その初期化子が同じなら OS から見た GUID は同じ**。

**INF との関係（ざっくり）**

- 一般的な **`.inf`** では、クラス GUID やデバイスインターフェース GUID は **`{XXXXXXXX-XXXX-...}` 形式の文字列**として書かれることが多い。これは **値**であり、**C のシンボル名 `SYSMEXUSBDRV_GUID` とは別物**。
- そのため、**変数名だけ** `USBDRV_GUID` に変わった場合、**INF を必ず直す必要は多くの構成ではない**（INF にシンボル名が文字列として埋め込まれていない限り）。

**名前を揃える必要が出るケース**

- **カーネル側とユーザモード側で同じヘッダ**を共有し、マクロ名で GUID を参照している。
- **別リポジトリのドライバ／文書／スクリプト**が、まだ `SYSMEXUSBDRV_GUID` という **文字列**で検索・置換している。
- ベンダー提供のサンプルや WDK の流儀で、INF のコメントやドキュメントに旧識別子が残している（ビルドには影響しないことが多い）。

こうした **名前の整合**が必要かどうかはプロジェクト外の成果物次第なので、置換後に **「他ツール・INF・別チームのソースで旧名を参照していないか」**だけ意識して確認すればよい。

**`git grep -i sysmex HEAD` の意味（本文スクラブの「一通り完了」）**

- **`HEAD`** は、ミラー（または通常 clone）の **現在チェックアウトされているコミット**（多くの場合は既定ブランチの先端）のツリーだけを検索する。
- ここで **何も出なければ**、「その先端スナップショットのファイル内容に、`git grep` が見える形で `sysmex` が残っていない」＝**既定ブランチ先端の「本文」スクラブは一通りできている**、という意味。
- **履歴全体**や **別ブランチ・タグ**まで含めてゼロにしたい場合は、フェーズ E の **全コミットループ**（`git rev-list --all | while read ...`）も **`wc -l` が 0** になるまで確認する。HEAD だけでは足りない。

**実機・ビルドでの確認（任意だが安心）**

- アプリ／DLL を **リビルド**し、従来どおり USB デバイスに接続できるか（GUID **値**を変えていなければ、挙動は変わらないことが多い）。
- カスタム INF を自分で配布している場合は、その中に **旧ブランド名や旧シンボル名の文字列**がないか目視する。

**すでにフェーズ D を一度実行済み**でパスは済んでいる場合でも、`scrub.txt` を上記のように直したうえで **内容だけ再スクラブ**できる（ミラーで）:

```bash
cd /workspace/AnalogBoard-history-mirror.git
git filter-repo --force \
  --replace-text ../scrub.txt \
  --replace-message ../scrub.txt
```

`--path-rename` は不要（既に履歴上のパスは新名）。**ミラーがまだ「書き換え前の素の clone」なら**、最初からこの `scrub.txt` でフェーズ D をやり直す方が一発。

### コピペの罠（`No such file or directory`）

ターミナルから **`root@...#` や `cd ...` を同じ行にまとめてコピー**すると、シェルがプロンプト文字列をコマンドだと解釈して

`bash: root@91d7c7de124f:/workspace/...#: No such file or directory`

のようになる。**プロンプトはコピーせず**、`cd` と `git grep` など **コマンド本体だけ**を貼る。

ミラー内からは相対パス `../scrub.txt`（＝ **`/workspace/scrub.txt`**）を指定する。

---

## フェーズ C: 履歴上のパスに Sysmex が残っていないか洗い出す

```bash
cd /workspace/AnalogBoard-history-mirror.git
git ls-tree -r --name-only HEAD | grep -i sysmex
```

ヒットしたパスは次のどちらかに分かれる。

1. **ディレクトリ接頭辞だけ**が `Sysmex_*`（ファイル名に Sysmex は無い）→ 親ディレクトリの `--path-rename` だけでよい。
2. **ファイル名にも** `Sysmex` が含まれる → **フルパス指定の `--path-rename` が必須**。かつ [git-filter-repo の仕様](https://github.com/newren/git-filter-repo/blob/master/Documentation/git-filter-repo.txt) により、**ディレクトリ一括リネームより先**に書く（個別 → 親ディレクトリの順）。

件数だけ見る場合:

```bash
cd /workspace/AnalogBoard-history-mirror.git
git ls-tree -r --name-only HEAD | grep -i sysmex | wc -l
```

---

## フェーズ D: git filter-repo

**カレントがミラー**であることを確認して実行。`scrub.txt` の位置に合わせて `--replace-text` / `--replace-message` のパスを変える。

フェーズ C の結果（`grep -i sysmex` の一覧）に基づく **最適化済み**コマンド。ポイントは次のとおり。

- **ルートの `.sln`** と **docs 下のファイル名**は、ディレクトリリネームとは独立した 1 本指定。
- **`Sysmex_AnalogBoard_Dll/` 配下**でファイル名に Sysmex が付くものは、**`Sysmex_AnalogBoard_Dll:AnalogBoard_Dll` より前**に列挙する。
- **`Sysmex_AnalogBoard_TestApp/` 配下**も同様（`SysmexAnalogBoardTestApp.rc` など）。
- **`Sysmex_AnalogBoard_UnitTest/`** は一覧上ファイル名に Sysmex が無いため、**ディレクトリ 1 本**でよい。

```bash
cd /workspace/AnalogBoard-history-mirror.git

git filter-repo --force \
  --path-rename docs/archive/plans/Sysmex_AnalogBoard_improvement_plan.md:docs/archive/plans/AnalogBoard_improvement_plan.md \
  --path-rename docs/archive/plans/Sysmex_AnalogBoard_release_plan.md:docs/archive/plans/AnalogBoard_release_plan.md \
  --path-rename Sysmex_AnalogBoard_TestApp.sln:AnalogBoard_TestApp.sln \
  --path-rename Sysmex_AnalogBoard_Dll/Sysmex_AnalogBoard_Dll.cpp:AnalogBoard_Dll/AnalogBoard_Dll.cpp \
  --path-rename Sysmex_AnalogBoard_Dll/Sysmex_AnalogBoard_Dll.def:AnalogBoard_Dll/AnalogBoard_Dll.def \
  --path-rename Sysmex_AnalogBoard_Dll/Sysmex_AnalogBoard_Dll.h:AnalogBoard_Dll/AnalogBoard_Dll.h \
  --path-rename Sysmex_AnalogBoard_Dll/Sysmex_AnalogBoard_Dll.rc:AnalogBoard_Dll/AnalogBoard_Dll.rc \
  --path-rename Sysmex_AnalogBoard_Dll/Sysmex_AnalogBoard_Dll.vcxproj:AnalogBoard_Dll/AnalogBoard_Dll.vcxproj \
  --path-rename Sysmex_AnalogBoard_Dll/Sysmex_AnalogBoard_Dll.vcxproj.filters:AnalogBoard_Dll/AnalogBoard_Dll.vcxproj.filters \
  --path-rename Sysmex_AnalogBoard_Dll/Sysmex_AnalogBoard_Dll.vcxproj.xml:AnalogBoard_Dll/AnalogBoard_Dll.vcxproj.xml \
  --path-rename Sysmex_AnalogBoard_Dll/res/Sysmex_AnalogBoard_Dll.rc2:AnalogBoard_Dll/res/AnalogBoard_Dll.rc2 \
  --path-rename Sysmex_AnalogBoard_Dll:AnalogBoard_Dll \
  --path-rename Sysmex_AnalogBoard_TestApp/SysmexAnalogBoardTestApp.rc:AnalogBoard_TestApp/AnalogBoardTestApp.rc \
  --path-rename Sysmex_AnalogBoard_TestApp/Sysmex_AnalogBoard_TestApp.cpp:AnalogBoard_TestApp/AnalogBoard_TestApp.cpp \
  --path-rename Sysmex_AnalogBoard_TestApp/Sysmex_AnalogBoard_TestApp.h:AnalogBoard_TestApp/AnalogBoard_TestApp.h \
  --path-rename Sysmex_AnalogBoard_TestApp/Sysmex_AnalogBoard_TestApp.vcxproj:AnalogBoard_TestApp/AnalogBoard_TestApp.vcxproj \
  --path-rename Sysmex_AnalogBoard_TestApp/Sysmex_AnalogBoard_TestApp.vcxproj.filters:AnalogBoard_TestApp/AnalogBoard_TestApp.vcxproj.filters \
  --path-rename Sysmex_AnalogBoard_TestApp/Sysmex_AnalogBoard_TestApp.vcxproj.xml:AnalogBoard_TestApp/AnalogBoard_TestApp.vcxproj.xml \
  --path-rename Sysmex_AnalogBoard_TestApp/Sysmex_AnalogBoard_TestAppDlg.cpp:AnalogBoard_TestApp/AnalogBoard_TestAppDlg.cpp \
  --path-rename Sysmex_AnalogBoard_TestApp/Sysmex_AnalogBoard_TestAppDlg.h:AnalogBoard_TestApp/AnalogBoard_TestAppDlg.h \
  --path-rename Sysmex_AnalogBoard_TestApp/res/SysmexAnalogBoardTestApp.rc2:AnalogBoard_TestApp/res/AnalogBoardTestApp.rc2 \
  --path-rename Sysmex_AnalogBoard_TestApp/res/Sysmex_AnalogBoard_TestApp.ico:AnalogBoard_TestApp/res/AnalogBoard_TestApp.ico \
  --path-rename Sysmex_AnalogBoard_TestApp:AnalogBoard_TestApp \
  --path-rename Sysmex_AnalogBoard_UnitTest:AnalogBoard_UnitTest \
  --replace-text ../scrub.txt \
  --replace-message ../scrub.txt
```

フェーズ C で **新たにヒットしたパス**（別ブランチ・古いコミットにだけ存在するファイルなど）があれば、同じルールで `--path-rename` を追記する。`git filter-repo` は **指定順に適用**されるため、**常に「フルパスの個別リネーム → 親ディレクトリの一括リネーム」**の順を守る。

---

## フェーズ E: 検証

### 全履歴の blob に文字列が残っていないか

`git grep -i sysmex $(git rev-list --all)` のように **全コミット SHA を一度に引数に渡す**のは避ける。理由は次のとおり。

- **ヒット行がコミットの数だけ繰り返し**出るため、履歴が長いと出力が膨大になる。
- コミット数が多いと **`Argument list too long`**（引数長制限）に当たることがある。

**おすすめの進め方**

1. **まず HEAD だけ**（マッチ行があれば内容も少し見える）

```bash
cd /workspace/AnalogBoard-history-mirror.git
git grep -i sysmex HEAD
```

何も出なければ、少なくとも先端ツリーはクリーン。

2. **全コミットを走査し、「まだ汚れているコミットの SHA だけ」出す**（マッチ行は出さない）

```bash
cd /workspace/AnalogBoard-history-mirror.git
git rev-list --all | while read -r rev; do
  git grep -i -q sysmex "$rev" 2>/dev/null && echo "$rev"
done
```

1 行も出なければ、**どのコミットのツリーにも該当文字列は無い**（`git grep` の対象範囲内で）。

3. **汚れコミットが出たときだけ**、その 1 コミットに対して行を表示（デバッグ用・先頭だけ切る）

```bash
cd /workspace/AnalogBoard-history-mirror.git
bad=$(git rev-list --all | while read -r rev; do
  git grep -i -q sysmex "$rev" 2>/dev/null && echo "$rev" && break
done)
if [ -n "$bad" ]; then
  echo "example commit: $bad"
  git grep -i sysmex "$bad" | head -n 40
fi
```

4. **件数だけ知りたい**（汚れコミットの個数）

```bash
cd /workspace/AnalogBoard-history-mirror.git
git rev-list --all | while read -r rev; do
  git grep -i -q sysmex "$rev" 2>/dev/null && echo x
done | wc -l
```

`0` なら、上記の走査ではヒットなし。

※ コミット数が非常に多い場合、手順 2 は **時間がかかる**。その場合は先にフェーズ E の「パス名」「コミットメッセージ」を確認し、問題なければ push 後に別途フルスキャンしてもよい。

### 汚れコミットが 1 件など少数のとき（HEAD はクリーンなのに `wc -l` が 0 でない）

`HEAD` では `git grep` が空でも、**別ブランチ・タグ・古い先端**のツリーにだけ `sysmex` が残っていることがある。

手順 2 で得た **汚れコミットのフル SHA** をシェル変数に入れてから調べる（例: `DIRTY=f050b0ffab8f...`）。

```bash
cd /workspace/AnalogBoard-history-mirror.git
DIRTY=<paste-full-sha-here>

# どの参照から辿れるか
git branch -a --contains "$DIRTY"
git tag --contains "$DIRTY"

# メタ情報
git log -1 --format=fuller "$DIRTY"

# どのファイルのどの行に残っているか（ここが次の手の材料）
git grep -i sysmex "$DIRTY" | head -n 50
```

**対処の目安**

- **内容にまだ sysmex が残っている** → `scrub.txt` を強化するか、該当パターンを追記し、フェーズ B 補足の **`--replace-text` / `--replace-message` だけの再 `filter-repo`** をもう一度かける（全履歴が再処理される）。
- **参照だけが古い履歴を指している**（不要なブランチ／タグ）→ 移行方針次第でその参照を削除してから push してもよいが、**チーム合意**が必要。
- **バイナリやエンコーディング**で置換が効いていない → 該当拡張子を特定し、手動で blob を直すか、専用の置換ルールを検討する。

#### 典型: `Binary file ... matches`（コミット済みの `.exe` など）

`git grep -i sysmex "$DIRTY"` の出力が次の形なら、**テキストではなくバイナリ**にパターンが含まれている。

```text
Binary file <commit>:AnalogBoard_UnitTest/FpgaRegisterLogic_test.exe matches
```

ビルド成果物を誤ってコミットすると、PE 内のパス・シンボル・リソースに旧名が残り、`sysmex` が **文字列として**検出される。`--replace-text` でバイナリを書き換えると **実行ファイルが壊れる**ため、**履歴からそのパスを削除**するのが正攻法である。

```bash
cd /workspace/AnalogBoard-history-mirror.git
git filter-repo --force \
  --path AnalogBoard_UnitTest/FpgaRegisterLogic_test.exe \
  --invert-paths
```

- `--invert-paths` は「指定したパスだけ **全コミットから除外**」する（他のファイルは残る）。
- パスは **書き換え後のツリー上の名前**（例: `AnalogBoard_UnitTest/...`）に合わせる。古いコミットではまだ `Sysmex_AnalogBoard_UnitTest/...` のままかもしれない場合は、**フェーズ D の `path-rename` 済みのミラー**では通常、新パスだけが残る。疑わしければ  
  `git log --all --oneline -- "**/FpgaRegisterLogic_test.exe"`  
  で実際のパスを確認する。
- 同様の `.exe` が複数コミット・複数パスにあるなら、`--path-glob` や複数回の `--path ... --invert-paths`、あるいは「`UnitTest` 以下の `*.exe` を履歴から落とす」方針を検討する（範囲はチームで決める）。

その後、もう一度フェーズ E の **全コミットループ**で `wc -l` が 0 になるか確認する。通常 clone 側では **`.gitignore` に `*.exe` を足して再発防止**する。

### 現在のツリー上のパス名

```bash
cd /workspace/AnalogBoard-history-mirror.git
git ls-tree -r --name-only HEAD | grep -i sysmex
```

空であることが望ましい。

### コミットメッセージのサンプル

```bash
cd /workspace/AnalogBoard-history-mirror.git
git log --oneline -20
git log --grep=sysmex -i --all
```

`--grep` でヒットしないことが望ましい。タグを多用している場合はタグメッセージも目視する。

---

## フェーズ F: 新 GitHub（AnalogBoard）へ push

**新 URL を再確認**したうえで:

```bash
cd /workspace/AnalogBoard-history-mirror.git
git remote add new-origin <NEW_URL>
git push --mirror new-origin
```

- `git push --mirror` は参照を広く送る。**URL が新リポジトリであること**を必ず確認する。
- 旧リポジトリにはこの手順では push しない（GitHub 上は従来どおり残る）。

### `remote rejected ... refs/pull/N/head (deny updating a hidden ref)` について

旧 GitHub リポジトリから **`refs/pull/*` を fetch する設定**（`remote.origin.fetch +refs/pull/*/head:refs/remotes/origin/pull/*` 等）でミラーを取っていると、ミラー内に **PR 用の参照**が残る。`git push --mirror` はそれらも送ろうとするが、**GitHub 側では `refs/pull/*` は隠し参照**であり、クライアントからの更新は拒否される。

**実害はほぼない。** ログに `* [new branch] main -> main` や `* [new tag] ...` が出ていれば、**ブランチとタグは新リポジトリに載っている**。新リポジトリで新しく PR を作れば、GitHub が別途 `refs/pull/*` を管理する。

最後に `error: failed to push some refs` と出ても、**上記が成功していれば移行の本体は完了**とみなしてよい。GitHub の **Branches / Tags** 画面で確認する。

**次回以降・再 push 時にエラーを消したい場合**（ミラー内で PR 参照を捨てる）:

```bash
cd /workspace/AnalogBoard-history-mirror.git
git for-each-ref --format='%(refname)' refs/pull | while read -r ref; do
  git update-ref -d "$ref"
done
```

そのあと `git push --mirror new-origin` をやり直す（通常は不要）。

---

## フェーズ G: 通常 clone で開発再開

```bash
cd /workspace
git clone <NEW_URL> AnalogBoard
cd /workspace/AnalogBoard
```

古い作業コピーや worktree は、未コミット変更を退避したうえで **新リポジトリ基準で作り直す**のが安全。

---

## フェーズ H: 移行完了までの旧リポジトリ

- 旧リポジトリは削除しない。
- チームには「clone / PR は新 URL のみ」「旧 SHA は新リポでは無効」と伝える。

---

## トラブルシューティング

| 現象 | 対処 |
|------|------|
| `fatal: detected dubious ownership` | `git config --global --add safe.directory /workspace/AnalogBoard-history-mirror.git` |
| `findstr: command not found` | Linux では `grep -i` を使う（本ドキュメントのコマンド参照） |
| `bash: root@...#: No such file or directory` | プロンプトをコマンドに含めてコピーした。**コマンド行だけ**貼る |
| `git grep HEAD` で `CSysmex*` や `SYSMEXUSBDRV` が残る | `scrub.txt` に **`regex:(?i)sysmex==>`** を足すか、フェーズ B の 3 段版を使い、`--replace-text` だけ再実行 |
| `git filter-repo` が force を要求 | ミラーで実行しているか、`--force` を付ける |
| `git push --mirror` が拒否される | 新リポが空か。README だけの初期コミットがあると競合することがある → 空リポを使うか方針を決める |
| `refs/pull/N/head` が `deny updating a hidden ref` | **正常。** GitHub は PR 用参照をクライアント push 不可。ブランチ・タグが既に `new` と出ていれば成功。不要ならミラーから `refs/pull` を削除してから再 push（フェーズ F 節） |
| `git grep` でまだ sysmex が出る | `scrub.txt` の不足、`--path-rename` の漏れ、バイナリ内の文字列 → **ミラーを取り直して**ルールを足して再実行。全履歴チェックは `$(git rev-list --all)` 一括ではなくフェーズ E の **コミット単位ループ**を使う |
| `Binary file ... FpgaRegisterLogic_test.exe matches` など | コミット済みバイナリ。**`--replace-text` は使わない**。`git filter-repo --path AnalogBoard_UnitTest/FpgaRegisterLogic_test.exe --invert-paths` で履歴から除去（フェーズ E 節の「Binary file」の項） |

---

## 参考: scrub.txt を別の場所に置く場合

`--replace-text` / `--replace-message` に **絶対パス**を指定すれば、ミラーと同じ親に置く必要はない。

```bash
cd /workspace/AnalogBoard-history-mirror.git
git filter-repo --force \
  ... \
  --replace-text /workspace/scrub.txt \
  --replace-message /workspace/scrub.txt
```
