# b64 world-load hotfix

## Changes and evidence

The user's verified A/B is b56 (81d1abe) loads and b64 (f5cd7f1) hangs after the mission title, with intact installed hashes. This isolates the regression interval, **not an individual blocking call**. No Deck runtime, stack dump or executable reproduction is available here; this is a conservative rollback/guard, not a claimed experimental bisect.

The five supplied logs show successful core/menu initialization, saved High camera height (120 cm), background route-reader initialization, and a running pause tick. They do not show the blocked game-thread stage. Missing-native messages are pre-existing b56 diagnostic probes, not a new failure. The reported empty renderer frames establish a live render loop, not a usable player/world.

All 1,350 comments retrieved from private dated diagnostics issue #12 (latest retrieved comment 2026-11-14T04:34:53Z) contained no identifiable f5cd7f1 capture or its camera/stage records. The unattached arrow trace was therefore unavailable. No uploaded reproduction evidence beyond the supplied files is claimed; no private log contents are included here.

| Suspect | Source/evidence | Hotfix action |
| --- | --- | --- |
| PR #17 FusionFix ExtraInfo host | Changed binary; supplied pause log records the new host arrangement, but no footer callback entry proves it ran | Restore byte-identical b56 host, remove ExtraInfo patch, make optional footer callback inert; keep build identity on QoL row |
| PR #17 camera discovery | Previously resolves before rejecting loading/non-driver frames; saved 120 cm enables that path | Resolve/query only after normal-driving gates and initial world readiness |
| PR #16 periodic checks | First periodic deadline is 600 seconds, beyond the approximately 72-second attached pause trace | Keep scheduler; do not start session checks until initial world readiness |
| PR #17 enemy pruning | Runs on normal compass ticks without an outer initial-world guard; initially empty set does not demonstrate a stall | Defer whole compass initialization/tick until initial world readiness |

The bundled host regression test verifies SHA-256 **972a01435fed1272c109580eabe70cf0cbf88e300ebc67d2d2039e99f5933134**, the exact b56 host. b64 was **cbe36bcce4abc858d30faf1b478567819f6d566fe56278cb7300bd38534db072**. Matching b56 bytes proves the rollback, not that this host was the sole cause.

## Load safety and retained features

Only existing read-only screen/player bootstrap queries run before readiness. Initialization requires fade-in, a playing/controllable player and an existing player ped. Camera discovery, entity pruning, renderer attachment and scheduler activation wait. Readiness latches through normal pause/fade and resets at script stop/start. A stopped/unready scheduler returns before its lock or request/file work. Short, once-per-session core-log stage records bracket deferred initialization and first camera discovery/query without per-frame logging.

Camera presets, enemy cleanup/road guidance and the 10-minute scheduler remain after readiness. Native QoL footer extension is intentionally held back; no competing footer hook is re-enabled. Release version/build and live status remain on the update row. Existing anti-blink road geometry/stabilizers are untouched.

## Updater compatibility

The installer accepts the two exact verified host hashes as well as its existing supported base. Thus a b56 rollback with a stale b64 bundle receipt can receive this hotfix. Hash validation, transactional installation and backups remain. A deterministic real-host installation test preserves High=120 and checks Off through that rollback scenario. Uploader auth/config, pending and delivered queues are not changed.

## Validation and acceptance

Python discovery, portable C++ tests and full Windows build/updater-ui CI are recorded in the PR. Native Windows-only tests cannot run on this Linux workspace; CI runs those checks. Tests cover 38,000 waiting ticks with no runtime initialization, readiness/lifecycle transitions, scheduler pre-world inactivity, camera gate ordering, exact host bytes, fallback identity and rollback installation.

Deck acceptance is outstanding. Use the normal updater while the game is closed, then cold-launch into the world, inspect Pause → QoL build/status, drive with Low/Medium/High, defeat an enemy without extra corpse shots, and exercise Off/On, a 10-minute recheck and staged-on-exit apply with a genuinely newer build. No manual file edits. Destination fading and further road-jitter investigation are separate follow-up work.
