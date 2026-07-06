# AnalogBoard Review Pass

Use this lightweight pass before checkpoint commits. Review as a bug-first source reviewer, not as an external automation wrapper.

## Review Stance

- Lead with findings, ordered by severity.
- Focus on changed source code and runtime/config behavior in the current checkpoint scope.
- Check for behavioral bugs, regressions, contract violations, missing tests, and scope creep.
- Fix in-scope actionable findings before commit. Record real out-of-scope follow-up only when it is necessary and concrete.

## AnalogBoard-Specific Risks

- **"Unit tests green" is weak evidence here**: three past in-place changes regressed in the field with green tests (allocator backend swap, engine extraction empty capture, hot-path logging). Flag any change whose real risk only manifests on hardware timing, and require it to be listed for the next field/emulator gate.
- Hot-path regressions: any code on the EP6 read path (read sizing, retry/backoff, buffer handling) must keep policy-header contracts and their tests aligned (`Ep6TransferRetryPolicy.h`, `Ep6TransferTuningPolicy.h`, `ReadRequestBurstPolicy.h`, `AcquisitionCompletionLogic.h`).
- Allocator/CRT contracts: `ScopedHeapBuffer` stays CRT `malloc/free`; no `_msize()` across CRT boundaries; no new file-scope singleton state in the DLL.
- Export/ABI drift: `.def` export list and `EP2_SendData`/`EP4_GetData`/`EP6_GetData` signatures must not change; C++ name-mangling visible changes are a contract break.
- Endpoint discovery: never assume endpoint order/position; resolve by address (EP2=0x02, EP4=0x84, EP6=0x86); handle missing endpoints with useful errors, not crashes.
- Driver/INF: `cyusb3.inf` and the signed `.cat` are never edited; `FX3/` driver packages and `CyLib/*/CyAPI.lib` binaries change only with explicit owner intent.
- Downstream contracts: fl/fh .bin layout (headerless `<u2`, channel-major, FL8/FH5, exact-multiple), `_cfg.txt` required fields, and pulse-feature column order (per-channel A→H→W) are frozen by the rebuild plan.
- Fixture mistakes: committed fixtures must be small and synthetic; do not depend on ignored local `data/` or `logs/` for tests.

## Docs And Scope

- Do not treat docs-only changes as source issues.
- Do flag documentation when it contradicts runtime behavior or the rebuild plan's frozen decisions (D1–D18).
- Reject scope creep into `Dialog1_Main.cpp`, acquisition semantics, or roadmap items not in the checkpoint scope.

## Verification

- Keep deterministic checks in the flow: focused unit test suites, `git diff --check`.
- Windows-only verification (MSVC build, test exe runs) that cannot run in the current environment must be recorded in `tasks/todo.md` as pending, never assumed green.
- If refactor or review is interrupted, resume only the incomplete pass. Re-run completed passes only when the target diff changed.

## Prohibited By Default

- Do not call AI automation scripts under `scripts/`, `claude -p`, CodeRabbit CLI, or other external review CLIs unless the user explicitly asks.
