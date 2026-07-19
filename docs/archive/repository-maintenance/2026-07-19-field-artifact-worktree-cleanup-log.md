# 実機資材・worktree整理 Process Log

## 対象

- [AnalogBoard再構築プラン](../../plans/260710-analogboard-rebuild-plan.html)
- [チェックリスト](2026-07-19-field-artifact-worktree-cleanup-checklist.md)
- [Process Log INDEX](../../process_log/INDEX.md)

## Process Log Entries

| DateTime (JST) | Phase / Task | Activity | Result | Evidence | Risks / Issues | Next Action |
|---|---|---|---|---|---|---|
| 2026-07-19 09:36 | Phase 0 / Inventory | root未追跡資材、main差分、2 worktree、branch/remote、重複package、checksumを読み取り棚卸し | `260717/` 21GB、portable package 1.6GB、root Gate package 570KB、worktree合計約973MBを分類 | `git status`; `git worktree list`; `diff -qr`; `sha256sum -c` | portable package基準manifestの11差異は記録済みsource overlayであり、D23/READY/field/build manifestはPass | 承認済みの保全移動と回収を開始 |
| 2026-07-19 09:36 | Phase 0 / Safety gate | 削除・上書き対象、保全先、worktree回収方針をdry-run提示 | ユーザーが「すすめて」と明示承認 | conversation approval | remote branch削除は対象外。raw dataはGitへ追加しない | Phase 1へ |
| 2026-07-19 09:39 | Phase 1 / Artifact relocation | raw characterization、portable package、固有validation build 2件を`artifacts/field-session/`へ移動 | 21GB、1.6GB、408KB、316KBを保全 | `du -sh --apparent-size artifacts/field-session/...` | payloadは`.gitignore`対象。Gitには`artifacts/README.md`のみ収録 | root重複を再照合 |
| 2026-07-19 09:39 | Phase 1 / Duplicate removal | root Gate packageとportable package内`field_package/`を再比較後、rootコピーを削除 | `diff -qr`差分0、48ファイル削除、保持コピーあり | `artifacts/field-session/packages/r7-driver-ep4-polling-20260715T1618JST-source-package/field_package/` | 削除済みrootコピーは保持コピーから復元可能 | validation source回収へ |
| 2026-07-19 09:41 | Phase 2 / Source overlay | 初回rsyncの相対pathがworktree基準で解決されsource/destination不在となったため、絶対pathへ修正 | 初回はcode 11/23で変更なし。再実行後、resolved `host_source`と4対象treeがbyte一致 | `rsync -anic --checksum`差分0 | package基準manifestの11差異はattestation記録済みoverlay | native verificationへ |
| 2026-07-19 09:43 | Phase 2 / Native tests | final diagnostic sourceのnative unit suiteをMSVC helper経由でbuild/run | 全suite 0 failed。Rearm 601、replay 35、completion 21、shutdown 36、polling 18、EP4 diagnostic 28、endpoint 49、WaveDataFileIO 9,420等が全Pass | `AnalogBoard_UnitTest/build_test.bat`; terminal output | 既存`FpgaRegisterLogic_test.cpp`にC4996/C4189 warningあり、失敗なし | Release rebuildへ |
| 2026-07-19 09:44 | Phase 2 / Release build | `Release|x64`をclean rebuild | DLL/Appともbuild成功、exit 0 | MSVC helper `rebuild Release`; `x64/Release/` | 実機受け入れの再実施はしない（使用済みbuild sourceの回収） | source-only stage/commitへ |
| 2026-07-19 09:46 | Phase 2 / Validation publish | 回収したfinal diagnostic source、focused tests、再現build資材をvalidation branchへcommit/push | commit `d760e90`を`origin/validation/win11-driver-r7-acq`へ新規upstream push、local/remote一致 | native全suite 0 failed; Release x64 rebuild; `git rev-parse` | r7移植例外のためmainへmergeしない | worktree固有差分を保全 |
| 2026-07-19 09:47 | Phase 2 / Superseded patch | feature worktreeの未commit差分をbinary patchとしてローカル保全 | 80,887 bytes、SHA-256 `3b7ca7aad5c52810775f983e6a4ee9405b4253735c2660f710c7cb5b47336721` | `artifacts/field-session/superseded-worktree-patches/feature-win11-driver-compat-uncommitted-20260719.patch`; `git apply --check --reverse --ignore-space-change` | 改行混在のため適用確認には`--ignore-space-change`が必要 | worktreeを撤去 |
| 2026-07-19 09:48 | Phase 2 / Worktree cleanup | feature／validationのHEADが各remote branchと一致することを確認後、両worktreeを撤去してprune | main worktreeのみ残存。remote/local branchは保持 | `git worktree list --porcelain`; feature `3ca4ea4`; validation `d760e90` | worktree内の生成物は保全済み固有package以外を削除 | main文書整理へ |
| 2026-07-19 09:54 | Phase 3 / Documentation | artifact配置規約を追加し、plan／runbook／AGENTSの証跡pathを更新。完了済みfield-session文書とD23 logをarchive | root直下の実機用folder参照を解消し、runbookを完了済み履歴として明示 | `artifacts/README.md`; `docs/INDEX.md`; `docs/process_log/INDEX.md` | payloadはローカルのみ。共有時は承認済みevidence storeが別途必要 | script／document検証へ |
| 2026-07-19 09:56 | Phase 3 / Script verification | inventory／DFX stop契約testと全PowerShell構文解析を実行 | contract test Pass、6 files parse Pass | `scripts/field-session/tests/collect_gate_inventory_contract_test.ps1`; PowerShell parser | 実機操作は行っていない | artifact integrityへ |
| 2026-07-19 09:57 | Phase 3 / Artifact integrity | 移動後のD23、READY、field、build、validation package manifestを再検証 | D23 3、READY 51、field 16、build 21、validation 19＋14 entriesが全Pass | 各`manifest/checksums.sha256`; `D23_SESSION_BUNDLE_CHECKSUMS.sha256` | top-level baseline manifestの既知11 overlay差異はattestation済みのためraw Pass条件にしない | docs整合へ |
| 2026-07-19 09:58 | Phase 3 / Docs validation | 変更／追加文書のHTML parse、duplicate ID、fragment、local linkとGit whitespaceを検証 | 5 HTML、32 Markdown、114 local links Pass。差分にconflict marker／EOF異常なし | read-only validator; `cmp AGENTS.md CLAUDE.md` | archive Markdownの行末2spaceと`.gitignore`のCRLFは意図した形式。中央sync checkはAnalogBoard mirror DRIFT、sys_app mirrorは既存の無関係DRIFT | 中央syncを引き渡してmain commitへ |
| 2026-07-19 09:58 | Phase 3 / Central read-only check | task_managementのregistry workflowを`--check`で検証 | downstream planほか対象はOK。AnalogBoard親planはartifact path更新によりDRIFT | `(cd ../task_management && scripts/sync-active-plans.sh --check)` | AnalogBoard workspaceから中央mirrorを直接編集しない規約のためpending sync。sys_app DRIFTは本作業外 | main差分reviewへ |
| 2026-07-19 10:02 | Phase 4 / Final review | origin/mainをfetchし、staged scope、worktree、branch追跡、raw payload非混入を再確認 | mainとorigin/mainは0/0、main worktreeのみ、validation/feature branchはremote一致。Git対象46 filesに`artifacts/field-session/` payloadなし | `git rev-list --left-right --count`; `git worktree list`; `git diff --cached --name-status` | ローカルartifactはGit外のため、このPCまたは承認済みshared evidence storeで保全が必要 | trackingをarchiveしてcommit/push |
| 2026-07-19 10:03 | Phase 4 / Publish | 整理済みmain差分をConventional Commitでcommitし`origin/main`へpush | 完了（push後にlocal/remote一致をreadback確認） | `git commit`; `git push origin main`; `git rev-parse` | 中央AnalogBoard mirror syncのみtask_management workflowへ引き渡し | 完了 |
