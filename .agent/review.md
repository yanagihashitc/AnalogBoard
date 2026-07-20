# Review checkpoint: Phase 0 USBPcap analysis

Review the current task diff as evidence-processing code. Findings come first,
ordered by severity, with exact file and line references. A clean result must say
that no Blocker/Major findings remain and list the verification actually run.

## Scope and authority

- Compare the implementation with `goal.md`, the Phase 0 recording/replay and
  2026-07-17 USBPcap sections of
  `docs/plans/260710-analogboard-rebuild-plan.html`, and
  `.claude/skills/acquisition-hotpath-guard/SKILL.md`.
- Review scripts, tests, schemas, manifests, reports, tracking updates, and Git
  staging state created by this task.
- Do not request acquisition-core, driver, firmware, gcsa, or sys_app changes as
  part of this review. Report such needs as out-of-scope follow-ups.

## Blocker checks

- A source capture can be modified, overwritten, renamed, or staged for commit.
- Raw USB/EP6 payload, protected measurement data, credentials, or an unbounded
  generated trace can enter tracked output.
- The analyzer loads an entire multi-gigabyte capture into memory or requires a
  duplicate full-capture copy.
- Results claim to root-cause the pre-trigger EP4 failure or Type B stall even
  though the available captures contain successful Type C runs only.
- The output changes a D1-D23 decision, freezes a new public corpus format, or
  edits the read-only task_management mirror/roadmap from this workspace.
- A command can execute injected capture paths or other untrusted values through
  shell interpolation.

## Major correctness checks

- Every source capture is identified by exact file size and SHA-256 before its
  result is accepted.
- Tool name and exact Wireshark/TShark/Capinfos version are recorded.
- Required dissector fields are checked before extraction; missing fields fail
  clearly instead of yielding empty success.
- USB device/interface selection is discovered and evidenced per capture rather
  than assumed from a single hard-coded device address.
- Endpoint and direction semantics are correct for EP2 OUT (`0x02`), EP4 IN
  (`0x84`), and EP6 IN (`0x86`).
- URB submission/completion events are correlated without double counting.
- Requested, captured, and reported data lengths remain distinguishable.
- USBD status, NT status, truncation, short transfer, timeout, cancellation, and
  unknown status remain distinguishable.
- Relative time, epoch time, capture start/end, and app/telemetry timestamps are
  not compared without an explicit clock basis and uncertainty.
- `low_mid`, `idle_180_1700`, and the selected sustained-high capture are mapped
  to the correct run labels and analysis purpose.
- Aggregate counts and rates are reproducible and protect against divide-by-zero,
  missing packets, malformed rows, integer overflow, and locale-dependent parsing.
- Reported lifecycle transitions are supported by packet or log evidence; inferred
  transitions are labelled as inference with their confidence/limits.
- The corpus index is explicitly provisional and does not masquerade as the
  production Recorder or Frozen v1 output contract.

## Test and operability checks

- Tests cover malformed rows, missing fields, duplicate URB IDs, unmatched
  completions, non-success status, zero-duration input, and deterministic ordering.
- The live smoke test covers all six captures with Capinfos and full extraction of
  `low_mid`, `idle_180_1700`, and one high capture.
- Running the same extraction twice produces byte-identical bounded summaries,
  excluding an explicitly separated run log if needed.
- Errors include the capture name, stage, command exit status, and useful stderr.
- Commands work from the repository root in the documented WSL/Windows setup and
  tolerate spaces in `C:\Program Files\Wireshark`.
- The tracked report links to the source manifest and generated evidence location,
  while ignored outputs can be regenerated from documented commands.

## Repository checks

- `git diff --check` passes.
- Local Markdown links resolve and JSON files parse.
- No placeholder markers remain in final artifacts.
- No unrelated edits, OS installation, driver/registry mutation, new capture,
  branch deletion, or central task_management write is included.
- `git status --short` and staged-file size review prove that raw captures and
  large generated files are not committed.

After fixes, rerun the focused tests and evidence checks. Record unresolved Minor
items and residual limitations in the task Review; do not describe a skipped check
as passed.
