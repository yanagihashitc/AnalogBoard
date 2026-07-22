# Refactor checkpoint router

This file is deliberately scope-neutral. The active `goal.md` selects the
versioned refactor profile; this router validates that binding and delegates the
checkpoint without adding task-specific rules.

## Required binding

Before changing code during the refactor pass:

1. Read the explicit `Refactor checkpoint profile` path and full
   `Refactor checkpoint blob SHA` from the active `goal.md`. Do not infer a
   profile from the branch name, touched paths, previous goal, or newest file.
2. Require a normalized repository-relative regular file named `refactor.md`
   below `.agent/checkpoint-profiles/`. Reject absolute paths, `..`, symlinks,
   untracked files, and paths outside that directory.
3. Compute the working-tree Git blob identity with `git hash-object -- <path>`
   and require exact equality with the full blob SHA pinned by `goal.md`.
4. Read the entire pinned profile and apply its scope guard, refactor checks,
   verification, and stop conditions to files touched by the current batch.

Stop before refactoring if either binding is absent, ambiguous, outside the
allowed directory, unreadable, or hash-mismatched. Never fall back to another
profile, including a historically related profile.

## Common checkpoint behavior

- Preserve behavior and authoritative contract bytes unless the pinned profile
  explicitly defines the behavior change already authorized by `goal.md`.
- Refactor only files touched by the current batch; do not broaden scope to
  adjacent cleanup.
- Keep tests green and add or update focused tests for behavior changed during
  refactoring.
- Record applied profile path, expected and actual blob identities, changes,
  verification, retained complexity, and residual limits in the active process
  log.
