# Review checkpoint router

This file is deliberately scope-neutral. The active `goal.md` selects the
versioned review profile; this router validates that binding and delegates the
checkpoint without carrying assumptions from an earlier scope.

## Required binding

Before reviewing the current batch:

1. Read the explicit `Review checkpoint profile` path and full
   `Review checkpoint blob SHA` from the active `goal.md`. Do not infer a
   profile from the branch name, touched paths, previous goal, or newest file.
2. Require a normalized repository-relative regular file named `review.md`
   below `.agent/checkpoint-profiles/`. Reject absolute paths, `..`, symlinks,
   untracked files, and paths outside that directory.
3. Compute the working-tree Git blob identity with `git hash-object -- <path>`
   and require exact equality with the full blob SHA pinned by `goal.md`.
4. Read the entire pinned profile and review the current batch against its
   scope guard, review checks, evidence requirements, and stop conditions.

Stop before review if either binding is absent, ambiguous, outside the allowed
directory, unreadable, or hash-mismatched. Never fall back to another profile,
including a historically related profile.

## Common checkpoint behavior

- Report findings first, ordered by severity, with exact file references and
  concrete impact. A pass is incomplete until each finding is fixed, retested,
  or explicitly blocked.
- Review the complete current-batch diff while keeping profile-specific checks
  authoritative; do not expand the product scope.
- Re-run affected focused verification after fixes and inspect the final diff,
  tracked paths, generated artifacts, secrets, and sibling-repository status.
- Record applied profile path, expected and actual blob identities, findings,
  fixes, skipped checks, verification, and residual limits in the active
  process log. Never relabel an unavailable or failed check as passed.
