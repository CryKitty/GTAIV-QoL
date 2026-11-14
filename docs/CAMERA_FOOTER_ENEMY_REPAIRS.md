# PR #15 follow-up repairs: camera, footer, enemy lifecycle

This branch is based on main 81d1abe (PR #15), independently of periodic-check
PR #16. It contains no periodic-check scheduling changes.

## Evidence and honest limits

All **1,180 comments** from private diagnostics issue #11 (UTC day 2026-11-13)
were read across all pages. No camera-height/preset or native-footer proof record
was present, and no PR #15 reproduction was identifiable. Camera-related records
were primarily renderer/taxi records, not the driving-height state. The issue has
many attacker-mark records but no death/retirement transitions identifying the
reported corpse-marker episode. This does not prove that damage events perform
cleanup: the existing code already polls, but its predicates and ownership rules
were incomplete.

The uploader rotated at UTC midnight, so issue #12 (2026-11-14 UTC) was also
checked across all **445 available comments**. The newest identifiable build was
PR #14; no PR #15 camera/footer proof was found there either. Counts describe the
investigation snapshot, not a claim that later uploads were read. Private capture
contents are not copied into this public repo. The user's specific failing gate
and exact footer-hook failure are not established by uploaded session evidence.
The fixes below address reproducible source-level failures; live acceptance and
new proof logs remain necessary to confirm this user's installation.

## Camera path

The menu mapping was already correct: UI 0/1/2 → setting ID 17 → 0/45/120 cm,
with nearest-preset migration and persistence. The apply path nevertheless required
GET_GAME_CAM_CHILD to return a valid, propagating camera. A child in the normal
game-camera graph is not evidence of an active scripted camera, and an absent or
non-propagating child vetoed every offset even with a valid driver/preset. The
new gate rejects an actually active scripted draw camera instead; child state is
sampled only for diagnostic comparison, never as an apply prerequisite.

The original generic no-argument native context also leaves return storage
uninitialised if a lookup fails. Offset setters/active queries are now explicitly
resolved in the initialized game loop by SDK hash with name fallback, with a
bounded retry if not yet available; absent handlers prevent a claimed apply,
and active-query return storage is initialised. No alternate camera mechanism is
introduced: the same SET_FOLLOW_VEHICLE_CAM_OFFSET handler receives
(enabled, 0, 0, cm/100). Low invokes no camera query/write unless releasing our own
previous offset. Existing script-owned offsets are not stolen. Pause, cutscene,
driver, fade, control and interpolation gates remain.

Core diagnostics record the stored startup value, effective preset and each saved
selection. RoadNav records native availability and state transitions plus a
five-second heartbeat with apply/release counters. State gates: 0 ready, 1 Low,
2 stopping, 3 no player, 4 not driver, 5 cutscene, 6 pause, 7 fade, 8 control,
9 interpolation, 10 scripted camera, 11 missing natives. Commands: 0 none,
1 apply, 2 release. active=1/owned=0 explains an existing offset we leave alone.
Old child-valid/propagating flags allow the next capture to show whether the old
veto was the user's actual blocker. These logs contain fixed state/values only.
No uploaded capture is claimed to prove which of these gates failed on the Deck.

## Native footer

Pinned FusionFix ExtraInfo source and the exact shipped seed were inspected.
The original PR #15 hook replaced PUSH -1 / PUSH -1 / PUSH EAX inside the full
signature ExtraInfo searches at startup. Disjoint overwritten instruction ranges
do not make these lookups independent: loading QoL's hook first invalidates
FusionFix's signature, and installing against another already modified renderer
can miss it. The user's visible native footer shows ExtraInfo works, but without
a PR #15 pause log the exact order/miss on his install cannot be claimed.

The root dependency is removed. The verified host's final context stores at RVAs
0x7371e and 0x73766 call a position-independent helper that invokes the additive
32-byte UI API Footer tail. The core extends the completed ExtraInfo string and
returns a borrowed thread-local pointer. The host passes that pointer to the same
native renderer. Its initialisation signature is never changed. The former
companion renderer hook is removed, with corresponding source-builder adaptation.
API sizes 20/24/28, missing callbacks and null results retain the original string.
Both original branches preserve flags, stack/registers, FP/SIMD and relocations.

The formatter requires the native purple Fusion Fix version marker, adds the
QoL name/release/build before warnings, and is idempotent. Only successful Game
footer extension sets footerSeen; normal help/Graphics/Audio cannot remove the
row identity. The row then shows its control name and live update status/time,
with no version/build. First-observed marker combinations and success are logged
without the source text. Missing ExtraInfo/API retains the previous row fallback.

## Enemy markers

Source-level causes:

- Attacker cleanup tested only existence/dead/distance/binding, not fatal injury,
  incapacitation or whether the attacker remained aggressive/friendly.
- The remembered combat order could re-add a mark without current aggression.
- The compass used membership in its previous enemy set as new hostility
  evidence, making its red classification self-perpetuating.
- Owned-blip removal required a still-matching ped binding; a despawned ped with
  an unbound surviving blip could lose tracking before its owned blip was removed.

A cheap normal-cycle prune now runs before compass visibility/pause gates. It
samples the tracked set's real existence, health, dead/fatal/injured and current
combat/shooting/melee state, with one per-cycle ped cache shared with classification
and chase queries. Full pool discovery remains on its existing 250 ms cadence;
new expensive state samples are range-limited during acquisition. Historical
damage or orders alone never keep/recreate an inactive attacker.

A single native blip drives both the world chevron (display 4) and compass icon.
Owned handles are removed even when the entity binding becomes zero. Borrowed
markers restore our colour changes, then hide their native display while retired;
they are restored on a live threat's return and preserved/restored safely on
shutdown. Binding checks avoid modifying a different entity after rebinding.
Dead native enemy markers are suppressed even if their game blip outlives the
ped. Current state overrides old hostility declarations, and the previous enemy
set is no longer evidence for hostility. Chase position/target selection uses the
same capability state, retaining live target tracking and existing GPS cadence.
No AI tasks, relationships or damage history are changed.

Retirement/suppression diagnostics are event-based. Tests cover missing, dead,
incapacitated and no-longer-hostile states on the next normal update, without a
damage event, for owned and borrowed presentation; live marks/chase candidates
remain, and detached versus rebound bindings are distinguished.

## Acceptance

Use the newest CHANGELOG updater-only steps after review/merge and green main CI.
Check all three camera presets, cutscene/pause/relaunch, native Game footer and
status-only update row, then attackers dying/going inactive without another shot,
with a live moving enemy/Enemy Line regression check. Let automatic diagnostics
upload on normal exit. No Deck acceptance result is claimed here.
