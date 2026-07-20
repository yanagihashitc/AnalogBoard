# Refactor checkpoint: Phase 0 USBPcap analysis

This checkpoint applies only to files changed by the current USBPcap analysis and
initial recording-corpus task. Preserve behavior and evidence while improving
clarity, determinism, and maintainability.

## Scope guard

- Review only the current task diff and its directly related tests.
- Do not modify the acquisition hot path, CyAPI transfer code, FPGA firmware,
  driver configuration, gcsa, sys_app, or the canonical output contract.
- Do not edit, rewrite, truncate, or rename source `*.pcapng` captures.
- Do not add raw USB payloads, EP6 bytes, large generated CSV/JSON files, or
  machine-specific absolute paths to Git.
- Keep the analysis result factual: the available captures are successful Type C
  characterization runs and cannot establish the cause of an uncaptured failure.

## Refactor checks

1. Make the capture root and output root explicit command-line inputs with safe
   repository-relative defaults. Reject missing inputs and output paths that
   would overwrite a source capture.
2. Keep multi-gigabyte processing streaming. Remove any full-capture read into
   memory and any unnecessary intermediate copy.
3. Separate concerns where it improves testability:
   - tool discovery and version capture;
   - TShark/Capinfos command construction;
   - normalized row parsing;
   - aggregation and anomaly classification;
   - manifest/report serialization.
4. Preserve deterministic output:
   - stable field and key order;
   - stable capture/scenario ordering;
   - locale-independent numeric formatting;
   - explicit UTC/JST handling;
   - no generated wall-clock time in content that is compared for reproducibility.
5. Validate TShark field availability with `tshark -G fields`; do not silently
   assume a field name across Wireshark versions.
6. Keep request/completion correlation, endpoint direction, captured length,
   requested length, and status semantics distinct. Do not collapse unknown,
   missing, truncated, and successful values into the same state.
7. Prefer small typed data structures and named status values over positional
   arrays, magic indexes, or duplicated string literals.
8. Ensure subprocess arguments are passed without shell interpolation. Quote
   Windows/WSL paths safely and surface stderr plus exit status on failure.
9. Keep tracked corpus files to metadata, hashes, scenario definitions, schemas,
   and bounded summaries. Derived event traces remain under the ignored artifact
   analysis directory unless the task explicitly proves they are bounded and
   payload-free.
10. Update or add focused tests for every behavior changed by refactoring. Use
    synthetic normalized rows or deliberately tiny fixtures; do not duplicate
    production captures into the test tree.

## Verification

- Run the focused analyzer tests.
- Run the deterministic extraction comparison defined in `goal.md`.
- Run `git diff --check`.
- Confirm `git status --short` contains no `*.pcapng`, raw USB payload, large
  generated analysis file, driver/registry change, or unrelated source edit.
- Record the checkpoint result and any intentionally retained complexity in the
  active process log.

If a refactor would change the interpretation of evidence, expand the task scope,
or require a canonical-contract decision, stop and report it instead of applying
the refactor.
