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
- Do not merge PRs; Muse merges after review. Do not publish the cloud environment
  as part of a coding task.
