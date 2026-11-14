# Repository rules

- Every PR must update `CHANGELOG.md`, the changelog shown by the updater.
  Describe each change in plain user-facing language, including new or changed
  INI settings and what to test. For internal-only work, explicitly state that
  it has no user-visible effect. Do not silently omit internal changes.
- Keep review changes small and preserve existing structure and naming.
- Deliver every gameplay change through the updater. Users control settings
  only through the game's pause menu; never require manual INI/file edits.
  A setting without a pause-menu control must ship with a working default.
  Preserve pause-menu choices across updates and document menu controls and
  updater-only test steps in `CHANGELOG.md`.
- Stop when the PR is open with CI green on its exact head; report its number,
  head SHA and checks. Do not merge until the user independently verifies it and
  sends a follow-up merge instruction. Codex then merges using a merge commit
  (not squash), verifies the resulting main CI and reports the merge SHA and
  delivery build/run number. Do not publish a release or the cloud environment.
- `VERSION` identifies named releases, not individual PRs. Keep it unchanged
  between named releases. CI generates the pause-menu build identity from the
  Actions run number and source commit; preserve that per-build identity and the
  updater's successful-install timestamp when changing installation/menu code.
  Same-build checks and staging must not pretend a new build has been installed.
