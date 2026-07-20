# Phase 0 USBPcap解析・初期録画コーパス化 チェックリスト

対象プラン: [AnalogBoard 再構築プラン](../../plans/260710-analogboard-rebuild-plan.html#p0-corpus-steps)
プロセスログ: [Process Log](../../process_log/2026-07-20-usbpcap-corpus-log.md)
作成日: 2026-07-20

## Batch 1 — P0-C1 analyzer contract and immutable manifest

- [x] Six source captures are regular, readable, non-symlink files with pinned SHA-256 and size
- [x] Exact TShark/Capinfos 4.6.7 versions and required field inventory are recorded
- [x] Focused tests are written Red-first and pass after implementation
- [x] Streaming analyzer rejects unsafe paths, missing fields, malformed rows, and ambiguous status
- [x] Capinfos manifest records format, encapsulation/interface, packet count, start/end/duration
- [x] Batch 1 focused verification, refactor, review, and final-diff checks are complete

## Batch 2 — P0-C2 deterministic extraction

- [ ] Full extraction completes for `low_mid`, `idle_180_1700`, and representative `high1`
- [ ] Endpoint/direction and URB request/completion classification remains distinct
- [ ] Lifecycle evidence frames and bounded EP6 throughput/cadence summaries are generated
- [ ] All six captures have endpoint/status/length summaries
- [ ] Repeated extraction is byte-identical
- [ ] Batch 2 checkpoint, commit, push, and `tasks/todo_archive.md` move are complete

## Batch 3 — P0-C3 corpus index and evidence closeout

- [ ] Tracked README, manifest, scenarios, schemas, limits, and regeneration commands exist
- [ ] Successful Type C baseline and missing failure trace limitations are explicit
- [ ] Parent plan evidence/status cells are updated without changing P0-C1–P0-C4 acceptance text
- [ ] AnalogBoard agent guidance is synchronized and central read-only handoff is recorded
- [ ] Batch 3 checkpoint, commit, push, and `tasks/todo_archive.md` move are complete

## Test perspectives (fixed before test code)

| Case ID | Input / Precondition | Perspective | Expected Result | Notes |
|---|---|---|---|---|
| TC-N-01 | Complete required TShark field inventory | Equivalence — normal | Inventory accepted in stable order | Synthetic inventory |
| TC-A-01 | One required field missing | Boundary — set minus 1 | Clear missing-field error | Assert type and message |
| TC-A-02 | Empty/NULL field inventory | Boundary — empty/NULL | Clear missing-field error | NULL represented by absent iterable/input |
| TC-N-02 | Valid Capinfos long report | Equivalence — normal | Typed metadata parsed | Locale-independent labels |
| TC-A-03 | Missing Capinfos property | Boundary — incomplete | Clear parse error naming property | Assert type and message |
| TC-A-04 | Invalid numeric Capinfos value | Equivalence — malformed | Clear parse error | Assert type and message |
| TC-N-03 | Valid normalized USB row at frame/length minimum 0 or 1 as applicable | Boundary — min | Typed row retains unknown separately | No payload field exists |
| TC-A-05 | Negative frame/length (-1) | Boundary — below min | Row validation error | Assert type and message |
| TC-A-06 | Duplicate/malformed column count | Equivalence — malformed | Row validation error | Batch 2 correlation uses typed rows |
| TC-N-04 | Output is the designated `source_root/analysis` directory or an external sibling | Equivalence — normal | Output accepted | Repository-relative default is `source_root/analysis` |
| TC-A-07 | Output equals source root, a source capture, or a non-analysis source sibling | Boundary — collision | Unsafe-output error | Prevent overwrite/side effects while allowing the required analysis directory |
| TC-A-08 | Missing, symlink, directory, or 0-byte capture | Boundary — filesystem | Source validation error | Maximum size has no imposed cap; streaming is required instead |
| TC-N-05 | Same inputs/tool version serialized twice | Equivalence — determinism | Byte-identical JSON | Stable capture/key ordering |

## Phase checkpoint

- [ ] All focused tests and required live extractions pass
- [ ] Six source hashes/sizes and Capinfos readability are reverified
- [ ] Manifest and bounded summary are byte-identical across two runs
- [ ] JSON, Markdown links, parent-plan HTML/anchors/links, and `git diff --check` pass
- [ ] No capture/raw payload/large generated output is tracked or staged
- [ ] Phase PR is created from `analysis/phase0-usbpcap-corpus` to `main`
