# Local Artifacts

This directory stores large or machine-specific payloads that must not be
committed to Git. Keep the top-level repository free of ad-hoc field-session
folders.

## Layout

- `field-session/YYYY-MM-DD-<purpose>/`: raw measurement data and captures
- `field-session/packages/`: immutable portable source or operator packages
- `field-session/validation-builds/`: historical validation build packages
- `field-session/superseded-worktree-patches/`: recovery patches captured before worktree removal

Canonical decisions and compact evidence summaries belong under `docs/`.
Binary payloads, PC inventories, packet captures, and measurement arrays stay
local here or in the approved shared evidence store.

Before removing a worktree or duplicate package, verify its retained copy by
checksum or byte-for-byte comparison and record the result in the applicable
process log.
