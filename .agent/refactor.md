# AnalogBoard Refactor Pass

Use this lightweight pass before checkpoint commits. Keep it local, scoped, and deterministic.

## Scope

- Review only changed source code in the current checkpoint scope.
- Preserve public APIs (`EP2_SendData` / `EP4_GetData` / `EP6_GetData` and the `.def` export list), binary/parser contracts (fl/fh .bin layout, `_cfg.txt`), and documented behavior.
- Do not expand into unrelated modules, archived docs, sample data cleanup, or roadmap refactors.
- Move broad cleanup, module splits, performance rewrites, or API redesigns to follow-up work.

## AnalogBoard-Specific Checks

- **Acquisition hot path is off-limits for instrumentation**: never add logging, `OutputDebugStringA`, file I/O, or timing-sensitive work inside the EP6 read loop (3 past field regressions came from this; see `.claude/skills/acquisition-hotpath-guard` and `docs/troubleshooting/usb.md`).
- **EP6 scratch buffer allocator contract**: `ScopedHeapBuffer` must stay on CRT `malloc/free` (`AnalogBoard_Dll/UsbTransferHelpers.h`); do not "modernize" it to `new[]/delete[]` — that exact change regressed in the field while unit tests stayed green.
- Header-only policy files (`*Policy.h`, `*Logic.h`) keep their paired unit test suites in `AnalogBoard_UnitTest/` behaviorally aligned.
- `Dialog1_Main.cpp` and acquisition semantics are out of bounds unless the scope explicitly includes them.
- `CyLib/header` (SDK 1.3 generation) and `CyLib/x64|x86/CyAPI.lib` stay as-is; do not change linker settings (`IgnoreSpecificDefaultLibraries=LIBCMT`, `legacy_stdio_definitions.lib`).
- Comments sparse and in English; commit messages in English (Conventional Commits).

## Refactor Actions

- Prefer small readability improvements, duplicate removal, clearer helper boundaries.
- Re-run only focused checks needed for files changed by this pass (targeted unit test suites, `git diff --check`).
- On Windows: rebuild via `cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild ... /p:Configuration=Release /p:Platform=x64"`. On non-Windows environments, record the pending build verification in `tasks/todo.md` instead of skipping silently.
- Inspect the resulting diff before moving to review.

## Prohibited By Default

- Do not call AI automation scripts under `scripts/`, `claude -p`, CodeRabbit CLI, or other external review CLIs unless the user explicitly asks.
- Do not perform broad formatting churn, generated artifact churn, or unrelated doc rewrites during this pass.
