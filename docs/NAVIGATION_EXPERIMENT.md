> Historical investigation carried over from the private development archive. Current source, builds and updater selection use this public repository’s main branch. Private session identifiers and activity times are omitted.

# Navigation continuity and camera-facing ribbon experiment

The initial changes were based on `e94bb0cb`. The whole-leg/shared-surface
follow-up is based on merged `feature/cutscene-pause` at `5597b51a`.

## Controls

The existing action is **hold D-pad Left for 900 ms** to set a waypoint at the
focused compass marker, or clear the owned waypoint when selected again. It now
uses the same eligibility policy on foot and in a vehicle. Phone, aiming/shooting,
pause, cutscene, control and focus gates remain. A cancelled hold requires release;
one continued press cannot create multiple waypoints. The existing raw-input
reader and Left-only filter are unchanged; Right, steering, throttle, triggers
and packet IDs are unaffected. Vehicle radio/mod conflicts still need game testing.
Entering or exiting a vehicle now explicitly cancels a hold from the previous
control context until Left is released. This cancels only the mod's waypoint
hold; it does not suppress genuine trigger presses or write game weapon state.

## A/B the road line

Run the updater with GTA IV closed, then use **Pause → QoL → Road Line**.
On shows the twisted manual/mission and police escape ribbons; Off hides them.
Changes apply live. Flat was removed: legacy Flat and Twisted both migrate to On;
Off stays Off. RoadNav turn cues remain an independent toggle. No file edits.

### Both lines invisible despite the menu being On

Fetched `feature/cutscene-pause` at c38dc7e and verified PR #3's 9d6217d before
editing. The user confirmed the toggle was On and an Off/On cycle did not recover
either line. Therefore an Off default does **not** explain that session.

Inspection found a renderer failure state: scene-depth discovery latched its
first attempt forever, even on failure. Missing scene depth deliberately suppresses
both ribbons rather than drawing through walls. Clearing the line or toggling it
did not reset that discovery latch. Camera discovery had the same one-shot policy,
though it can fall back to the script camera and does not itself prevent drawing.
Both providers now retry missing discovery at most once per two seconds. Geometry
clear, menu Off/On, and device reset invalidate discovery on the render thread.
Valid cached resources do not trigger repeated scans. The depth safety gate remains.

This failure mechanism predates PR #3; no PR #3-specific settings/renderer skip was
found. PR #3's strength zero only disables cross-section roll, not ribbon drawing;
its preference merge preserves Enabled=1, and its vehicle guard owns only waypoint
holds. Startup logging is independent of frame submission. PR #2's surface path
still requires validated road heights and can reuse a validated primary surface
for escape geometry. The affected game's actual resource/discovery status has not
been captured, so attribution of that session to the discovery latch remains
unconfirmed. Do not report a verified PR #3 root cause from code inspection alone.

The new D3D9 regression exercises the production renderer and production depth
cache/texture validation, with controlled discovery and clock inputs. It checks
manual and police routes with shipped settings, live On/Off modes, disabled
logging, unavailable depth, timed discovery recovery, and Off/On recovery. Pixel
assertions run on HAL/reference devices; NULLREF still requires successful draw
submission but explicitly skips pixel checks. Surface/route tests remain in place.
The menu tests emulate the exact x86 helper bytes, and native settings tests cover
live mode selection and persisted values. No in-game validation is claimed.

### Vehicle shooting investigation and test

PR #2 was verified before this investigation. Available older diagnostics were
incomplete and predated PR #1; logs for the reported auto-shoot session were
unavailable. Those older logs cannot establish the reported session's inputs,
weapon state, or whether the symptom predates PR #1. Private capture identifiers
and gameplay/upload times are omitted from this public copy.

PR #1 changed waypoint eligibility from on-foot-only to on-foot-or-vehicle. Its
action sets/clears a waypoint; the XInput filter itself predates PR #1 and only
clears D-pad Left. Both trigger bytes, other buttons, axes, results and packet IDs
are forwarded from the original API. There is no saved fire/aim latch or weapon
write in this path. Code inspection has not reproduced endless shooting or
proved its root cause, including any interaction with other installed mods.

The defensible fix cancels the mod's own hold across vehicle context changes.
Tests cover entry/exit cancellation, a fresh vehicle hold, and complete controller
state forwarding (including held-trigger release through named and forwarding
APIs, unchanged/changed packet IDs, disabled filtering and reconnect).
Diagnostics record vehicle transitions and shooting changes, plus a five-second
heartbeat while shooting, with raw triggers/buttons/packet and tick time.
They are local and use the existing diagnostics switch and uploader consent.
They do not forcibly clear the game's fire state; doing so without identifying
the owner could hide the fault and break intentional shooting.

In game, enter/exit with Left held and triggers released, then release Left and
make a fresh vehicle waypoint. Separately aim/fire/release normally; repeat with
the phone, pause menu and compass disabled. If firing sticks, retain the complete
RoadNav-arrow.log and LibertyLoadout.log, build ID, approximate transition time,
controller/Steam Input setup, and whether it persists with Compass HUD disabled.
Confirm the new capture actually uploaded; issue #10 is at its comment limit.

## Escape continuity

Previously the outside-search-circle early return bypassed refresh/risk checks.
Other pursuit changes had no explicit invalidation state. Failed requests could
leave the helper unavailable, while surface retention stopped as soon as the
player left the old path or reached its end. These mechanisms can leave stale or
absent guidance, though a particular in-game failure cannot be confirmed here.

The planner now queues recalculation on wanted-level changes, flashing-wanted
search transitions, accumulated search-center movement of 25 m, or radius changes
of 15 m. It also retries invalid, nearly exhausted, off-path or missing-helper
routes. Changed pursuit information invalidates in-flight candidates without
clearing the displayed snapshot. The normal 2.5 s debounce remains; failures in
the same search area back off to 5 s and then 10 s, capped at 10 s. New pursuit
information restores the normal debounce. Existing valid-route scoring/hysteresis
and the 12 s refresh policy remain.

Failed candidates, missing collision surfaces and GPS helper failures retain the
last good route/surface. A retained line may be behind the player or outside the
camera; retention does not fabricate a reachable road ahead. Leaving the vehicle
suspends drawing but keeps escape state while wanted. Clearing wanted releases
it; disabling both navigation features or stopping the script also releases it.

### Coexisting primary and escape routes

There is no one-ribbon-at-a-time rule: primary and escape use separate renderer
instances, with the escape submitted after the primary as a narrower coloured
ribbon. The `!routeAvailable` rule applies to fallback **turn chevrons**, not
Road GPS Line; that separate presentation is unchanged by this follow-up.

A reproduced suppression gate was `fitRoadSurface` truncating a new escape to
fewer than two points on collision-probe misses. It used only its own previous
surface, ignoring validated heights available from a primary route on the same
approach. Escape fitting now happens after primary fitting and can consult that
surface cache as well. Matching distance and elevation constraints are unchanged;
an unrelated bridge/floor cannot supply a surface. Display eligibility uses the
wanted lifecycle and committed snapshot, independently of GPS helper ownership or
whether a primary route is available. In-flight candidates never become a display
selection merely because another GPS buffer has been populated.

A temporary probe miss still keeps the last good escape surface. If neither an
old escape surface nor matching primary samples nor collision probes validate the
road, drawing it would require inventing heights; it remains suppressed with a
reason rather than drawing through buildings or other floors. Existing scene-depth
occlusion, camera clipping, paused/control gates and disabled-feature gates remain.

The opt-in log adds `Police escape suppressed: reason=...` for missing committed
snapshots/surfaces or disabled Road GPS Line, plus `Police escape suppressed
(renderer): reason=...` for stale snapshots, missing depth, viewport/device/shader/
state failures, geometry outside the camera, or draw failures. Repeated reasons
are throttled to five seconds; surface submission resumption is also logged.
These are code-level reproduced cases and diagnostics, not a claim that a specific
unlogged in-game occurrence has been identified.

The existing updater-installed diagnostic setup controls local `RoadNav-arrow.log`
recalc/drop reasons and `RoadNav.log` diagnostics. With diagnostics off, these
paths do not open logs. No manual file editing is part of this test flow.
This change adds no new upload destination or consent requirement.

## Validation in this Linux environment

- Existing plus expanded `tests/compass.cpp`: passed with GCC 14, C++17, O2,
  Wall/Wextra/Werror. Covers shared hold gates, one-shot holds, pursuit changes,
  bounded failure backoff and clock wrap, alongside existing GPS handoff tests.
- Existing plus expanded `tests/roadnav_navigation.cpp`: passed with the same
  flags. Covers disabled/straight/side-on roll, continuous angle/sign changes,
  bank-output equivalence at multiple densities and existing navigation tests.
- Both focused suites also passed AddressSanitizer and UndefinedBehaviorSanitizer.
- Both focused suites compile to Windows x86 objects using MinGW GCC 14.
- Python suite: 61 tests run, 57 passed, 4 skipped (optional Unicorn emulator).
- Supplementary MinGW syntax check of `roadnav.cpp` passed with local SDK spelling/
  forward-declaration adapters and a shader declaration stand-in. This is not a
  shader compilation, link, MSVC build or runtime check; no stand-ins are shipped.
- Local `build.bat` execution is unavailable (no Windows command processor/MSVC).
  On the review branch, normal Windows CI passed `build.bat` and both changed
  native test suites after fixing a shadowed test variable. See the PR checks for
  the complete workflow status; in-game acceptance remains separate.

The measured bank hot spot repeatedly scanned the route for each of its two
samples per point. Two monotonic sampling cursors now preserve the original
interpolation with linear traversal. Surface fitting reuses banks when XY/count
are unchanged, and the renderer reuses its per-layer vertex buffer rather than
allocating two new buffers every frame. Reproduce the same-process comparison with:

```sh
g++ -std=c++17 -O2 tests/roadnav_bank_benchmark.cpp -o /tmp/roadnav-bank-bench
/tmp/roadnav-bank-bench
```

For 220 points and 4,000 calls, measured original **214.562 microseconds/call**,
optimized **9.694 microseconds/call** (22.13x for this function). Outputs matched
exactly for the benchmark, and equivalence tests cover other densities/elevations.
These are synthetic CPU measurements, not in-game frame time or FPS.

## Required in-game checklist (not run here)

- [ ] On foot and in a vehicle: set/clear the same focused marker with Left;
  verify one action per hold and no driving/radio regressions.
- [ ] Phone, combat, pause, focus loss and vehicle transitions cancel safely.
- [ ] A/B pause-menu modes 1 (Flat) and 2 (Twisted) on straight roads and left/right 90-degree turns,
  at low/high speed; inspect crests, ramps, camera-axis crossings and readability.
- [ ] Full chase: police lose/re-find player, wanted level and search boundary
  change, helper/path calculation fails, destination runs out, then wanted clears.
  Confirm continuity without rapid replanning or direction snapping.
- [ ] Repeat the same game scene/configuration for frame-time/FPS comparison.

## Follow-up regression checks

Before the follow-up, both focused portable suites passed, but the long-leg
reproducer produced only 0.3970 m of cross-section rise at 400 m (one-metre ribbon,
75-degree setting) instead of the expected 0.9659 m. The shared-surface regression
also demonstrates that an escape starting 8 m ahead is entirely discarded by the
old own-cache-only fitting path when collision queries miss.

After the change, tests cover constant full roll up to 1,000 m past an L's corner,
zero curvature on that leg, off/straight-road baselines, continuous heading
transitions, simultaneous visible primary/escape surfaces with collision misses,
unchanged primary geometry, and rejection of a cache on a different floor. Existing
dual-native-GPS-buffer/candidate and chase debounce/backoff tests still apply.
Local follow-up checks passed: both focused C++ suites, the RoadNav ASan/UBSan
run, Windows x86 compilation of the expanded navigation suite, supplementary
RoadNav syntax checking, and Python tests (61 run, 57 passed, 4 optional-emulator
skips). The follow-up's PR checks record the normal Windows build and full suite
results.

In-game follow-up (not runnable in this cloud instance):

- [ ] L-shaped route: pause-menu Flat versus Twisted; entire perpendicular leg stays rolled,
  straight-ahead leg stays flat, left/right turns and joins do not pop at speed.
- [ ] Wanted with a primary waypoint/mission route: both ribbons render together,
  including shared approaches, diverging paths, candidate swaps and probe misses.
  Check suppression diagnostics if depth/surfaces are genuinely unavailable.
- [ ] Full chase with lose/re-find and moving search zone: no blinking/snapping,
  last route retained during retries, clean removal when wanted clears.
