# Internal branch workflow

Use the Customer Sample directory for both branches. Commit changes before switching.
Run `git switch master` for customer development and
`git switch internal/audio-diagnostics` for internal diagnostics.

## Common customer functionality

1. Implement and commit the change on `master`.
2. Switch to `internal/audio-diagnostics` and run `git merge master`.
3. Resolve conflicts by preserving the internal diagnostics around the updated
   customer behavior.

## Internal-only diagnostics

Commit internal logs, driver counters, timing measurements, and debug API usage
only on `internal/audio-diagnostics`.

Never merge this branch back into `master` or a customer
remote. This branch is intended for local or company-internal storage only.
