# /goal execution guide — feature/win11-driver-compat 再作成

## Goal Metadata

- Source plan: `docs/plans/260703-analogboard-rebuild-plan.html`（「資産の移植・破棄」「ドライバ・通信層戦略」節）＋ `docs/driver_next.md`（変更範囲の正）＋ `docs/260706-field-session-runbook.html`（事前準備節・実験3）
- Scope name: `feature/win11-driver-compat 再作成`（オーナー決定 2026-07-06：dev から切り直し・最初から push）
- Checkpoint unit: one commit per completed phase（本 goal は1フェーズ＝最終的に1〜2 commit）
- Push policy: push after the final checkpoint commit（ブランチ作成直後にも一度 push し、消失リスクを塞ぐ）
- Out of scope: `Dialog1_Main.cpp`・取得セマンティクス・エンジン・再アーム・新アプリ（WPF/C++コア）・scatter/Zarr プロトタイプ（別 goal・`proto/` 系ブランチ）・gcsa/sys_app・ドライバ INF・FPGA FW
- Assumptions:
  - 実装差分は `docs/driver_next.md` 記載の範囲：`AnalogBoard_Dll` の **endpoint discovery hardening** のみ（public API `EP2_SendData`/`EP4_GetData`/`EP6_GetData` 維持）。ヘッダ差し替えは不要（`CyLib/header` は SDK 1.3 世代一致確認済み）
  - `CyLib/x64|x86/CyAPI.lib` は main で gitignore 例外化・コミット済みのものを dev 系ブランチにも取り込む
  - **MSVC ビルド・テスト実行は Windows 実機PCのみ可**。Linux サンドボックスで実行する場合、ビルド検証は「Windows 検証待ち」として扱う（下記 Stop Conditions）
  - 旧ブランチの実装は消失しており参照不可。driver_next.md の記述（endpoint discovery hardening・focused unit test・`WaveDataFileIO_test` の既存 failure は別件）だけを仕様とする

## Principle

Do not execute an entire roadmap blindly. Execute `feature/win11-driver-compat 再作成` as small
reviewable batches while keeping repository checkpoints at the phase level.

Each `/goal` run should include:

- The exact source plan file and step IDs.
- The implementation scope.
- Required verification.
- Task tracking cleanup: update `tasks/todo.md`, add a Review section, and archive
  the completed batch to `tasks/todo_archive.md`.
- Phase checkpoint policy: commit and push once per completed phase, not once per small batch.
- Commit/push only the relevant changes. Do not include unrelated user changes.

## Batch Policy

- Batch size: 1 batch ＝ 1 レビュー可能単位（テスト1スイート＋その実装程度）。ホットパス近傍は特に小さく
- Initial batches:
  - **Batch 1 — ブランチ準備**: `origin/dev` から `feature/win11-driver-compat` を作成 → main の `.gitignore` CyLib 例外化と `CyLib/x64|x86/CyAPI.lib` を取り込み → 空実装のまま一度 push（消失防止）。`AnalogBoard_Dll.vcxproj` のリンク設定（`LIBCMT` ignore・`legacy_stdio_definitions.lib`）が dev 側にあることを確認
  - **Batch 2 — TDD: endpoint discovery hardening**: 失敗するテストを先に書く（`AnalogBoard_UnitTest/` に `UsbEndpointDiscovery_test.cpp` 等の focused suite を新設）。仕様：エンドポイントを**位置ではなくアドレスで解決**（EP2=0x02 / EP4=0x84 / EP6=0x86）、endpoint 数・alt setting の差異に耐性、未発見時は明示エラー（クラッシュ・NULL 参照不可）。実装は `AnalogBoard_Dll` に限定し、テスト可能なロジックはヘッダオンリー分離（既存 `*Policy.h` の流儀に従う）
  - **Batch 3 — 検証と締め**: focused unit tests green（Windows: `cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild Sysmex_AnalogBoard_TestApp.sln /t:<UnitTest>:Rebuild /p:Configuration=Release /p:Platform=x64 /m:1"` ※ .sln 名は dev 実体に合わせる）→ `Release|x64` の DLL/TestApp リビルド → 手順書「事前準備」チェック行の消し込み → checkpoint
- After every batch: update `tasks/todo.md`, implement the scoped changes, run
  focused verification, add Review notes, and move the completed batch to `tasks/todo_archive.md`.
- If a batch turns out too large, split it and document the split in `tasks/todo.md`.

## Checkpoint Policy

After the final batch, run this checkpoint:

1. Phase-level focused verification: 新設テストスイート＋`Ep6TransferRetryPolicy`/`Ep6TransferTuningPolicy` 等の既存 focused suites、`git diff --check`。Windows 環境なら `Release|x64` リビルド成功まで（`WaveDataFileIO_test` の既存 failure は別件として記録のみ）
2. Refactor pass on files touched in this scope, following `.agent/refactor.md`.
3. Review pass focused on bugs, regressions, missing tests, and unintended scope
   creep, following `.agent/review.md`.
4. Necessary fixes from the refactor/review passes.
4b. `$claude-review-fixer` による peer review、必要な修正反映。Claude 接続不可、または
    長めの timeout（default 30 分、PR review は 60 分、large PR / ultracode review では必要に応じて延長）後も
    返信がない場合は、理由を `tasks/todo.md` に記録して review を skip し、final verification に進む。
5. Final diff inspection.
6. One commit for this phase（Conventional Commits・English）.
7. Push.

If the refactor or review pass is interrupted, resume only the incomplete pass;
re-run a completed pass only when the target diff changed.

Do not use AI automation scripts under `scripts/`, `claude -p`, CodeRabbit CLI, or
other external review CLIs in the standard flow. Use them only when the user asks.

## Stop Conditions

Stop and ask the user before proceeding if any of these occur:

- Destructive file operations outside ordinary source edits.
- External publication, package release, registry upload, or index publishing.
- Secrets, credentials, `.env`, `.git`, or protected paths are involved.
- Backward compatibility, public API（`.def` export・関数シグネチャ）, persistent format, or
  binary contracts must change beyond the plan.
- **MSVC ビルド検証が実行できない環境で最終 checkpoint に達した場合**：コード＋テスト作成済み・「Windows 検証待ち」を `tasks/todo.md` に記録した状態で停止し、Windows 実機PCでのビルド・テスト実行をユーザーに依頼する（green 確認前に「完了」を宣言しない）
- endpoint discovery の仕様が driver_next.md の記述から確定できず、取得ホットパスの挙動変更が必要になった場合（acquisition-hotpath-guard 領域＝現場退行3回の領域）
