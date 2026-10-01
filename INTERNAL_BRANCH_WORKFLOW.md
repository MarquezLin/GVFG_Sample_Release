# Internal branch workflow

This worktree is the internal diagnostic overlay for the clean Customer Sample.

## Common customer functionality

1. Implement and commit the change on `master` in the Customer worktree.
2. Merge `master` into `internal/audio-diagnostics` here.
3. Resolve conflicts by preserving the internal diagnostics around the updated
   customer behavior.

## Internal-only diagnostics

Commit internal logs, driver counters, timing measurements, and debug API usage
only on `internal/audio-diagnostics`.

Never merge this branch back into `master`, `vfg100-release`, or a customer
remote. This branch is intended for local or company-internal storage only.
