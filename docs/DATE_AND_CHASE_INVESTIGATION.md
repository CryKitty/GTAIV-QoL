> Current safety status: the PR #5 venue-colour runtime path described below is
> disabled after a date-entry crash report. The unverified GetGlobalAddress call
> and date-start observer are removed; venue icons remain white. No uploaded
> capture proves the exact fault. See [current diagnostics](DIAGNOSTICS.md).
> Heart discovery and chase guidance remain enabled. The preference section below
> records historical research, not a currently enabled memory-access feature.

# Date compass and moving-enemy investigation

Starting point: public main includes PR #4 at `6f9ab5e`. The user confirmed both
Road Line and Enemy Line were On during the reported Bernie chase. This is not
being dismissed as a stale installation or an Off setting.

## Missing initiated-date heart

PR #4 fixed distance and mission-time ambient filtering for sprite 54, but the
native enumeration still queried only 0..9. Its old comment called these entity
categories; that was wrong. The bundled SDK types this parameter as eBlipSprite.
GTA IV's own taxi script `sub_33217` enumerates 0..109 and then separately reads
each returned marker's entity type. Thus sprite 54 could never be discovered
through that path; the optional validated radar-table fallback masked the bug
on some runs. The compass now enumerates all 128 sprites its renderer supports,
with the existing cycle/handle bounds and deduplication. Late date creation is
picked up by the normal half-second scan without relying on memory discovery.

A second display gate was distance-first overlap suppression: a nearer shop at
the same bearing could remove the distant heart from the final strip layout.
Date hearts now take priority over that clutter; other icons keep their existing
ordering. Failed texture/dictionary lookups retry, and a missing heart texture
has a recognizable `<3` fallback rather than the generic dot. The map is not
changed. Native hidden/transparent markers remain hidden.

## Live girlfriend preferences, not a guessed girlfriend list

Read-only research source: [GTAIVMissionScripts at 9d836ff](https://github.com/0x52617665/GTAIVMissionScripts/tree/9d836ff2d955ba2b19048c496f81a0f9eb6c84cb/IV).
This is a public decompilation reference, not proof that every executable/script
variant has the same layout. Runtime validation is mandatory.

- `dating_michelle::sub_38420` and the equivalent four dating scripts copy their
  per-place scores into the `go_on_date` startup arguments. The argument count
  is 64 VM words: girlfriend ID at byte 8, a 29-element array header at byte 56,
  then 29 current scores. No per-girlfriend taste table is shipped in this mod.
- `go_on_date::sub_7322` consumes those scores. `sub_52612` rejects scores below
  35; the dating scripts use 50 for ordinary baseline preferences. The compass
  presents below 35 as red, 35..50 as neutral white, and above 50 as green.
  This three-colour presentation is our interpretation of the live numeric
  scores, not a claim that the game exposes an explicit neutral-colour flag.
- `internet_dating` initializes global array 32995: 29 records of 116 bytes,
  including a 32-byte name, sprite at byte 32, activity type at 36 and entrance
  position at 40. The last vector is at 104; the next global is 33837. ScriptHook's
  exported `Game::GetGlobalAddress(unsigned)` supplies the live address.

The native script-start observer copies the actual startup data and always
forwards the original call. No script arguments, globals, saves, relationships
or map colours are written. Name, argument count, girlfriend index, all scores,
table count, names, types, sprites and finite world coordinates are checked.
Venue matching requires the same sprite, an entrance within 35 metres horizontally
and 12 vertically, and a unique match. Unknown, ambiguous, unsupported or missing
data stays white, with bounded availability diagnostics. Snapshots are discarded
when the outing ends, so a previous girlfriend's preferences cannot leak into the
next date. Friend outings do not receive guessed girlfriend colours.

Actual CE/Deck acceptance must confirm the live argument/table validation logs.
If those layouts differ, the deliberate result is neutral white, not invented
preferences or an unsafe scan for matching memory.

## Moving enemy / Bernie chase

The public Bernie2 script creates a hostile driver and passenger in a car,
declares them enemies and uses character blips for them. Enemy selection already
supports both vehicle blips and character blips riding in vehicles; it is not
limited to on-foot enemies. Police, dead entities, the player's vehicle, hidden
markers and candidates beyond the existing 3 km bound remain excluded.

The generation gap was an unconditional return for **any existing native GPS
buffer** when no moving mission target was selected. A stale/unowned buffer is
not evidence of a live mission destination. That veto is removed: a valid enemy
can request guidance when there is no manual waypoint or live fixed mission goal.
Those real destinations retain priority. New enemy requests cannot adopt the old
buffer as their route; subsequent reads must match the chase helper's endpoint.
Driver/passenger blips resolve their occupied vehicle's live center for the
helper and the per-frame fitted endpoint. The earlier bounded tracking/surface
rules remain; no long off-road connector is invented.

A second possible generation gate was the legacy hidden `RoadNav.ChaseGuidance`
setting. Its shipped default is On; the user's stored legacy value is unknown.
Road Line + Enemy Line On now enables chase generation even if that legacy flag
is Off, so the pause-menu request never requires an INI fix. Existing turn-cue
behaviour when the ribbon is disabled retains the legacy policy.

Enemy Line Off correctly suppresses drawing, as does master Off. Neither was Off
in the user's reported chase. Type-switch changes are now recorded locally to
make future generation-versus-visibility checks explicit. No session log proves
which exact native buffer was present in that run; this fixes a code-verified
veto consistent with the symptom, rather than claiming a recorded reproduction.
Police escape still owns its existing shared-planner priority/backoff while wanted;
this PR does not redesign simultaneous automatic enemy/escape planner ownership.

## Updater / pause-menu checklist

1. After merge and its main build succeed, close GTA IV and run the installed
   updater. Launch, then enable Compass HUD, Road Line and Enemy Line in Pause
   → QoL. No file edits are needed.
2. Initiate a date while far away. Look toward its heart, including where a
   nearer shop shares the bearing and with the date behind the player. It should
   appear after the normal scan; centered distance and map icons stay unchanged.
3. Pick up the date. Check preferred venues green, rejected venues red and
   baseline/unknown venues white. Finish/cancel and start another date: colours
   must not reuse the previous partner's scores. If every venue stays white,
   use the existing configured QoL log-upload shortcut so the local `Compass date:`
   availability messages can be reviewed; no file inspection/editing is required.
4. Replay a moving-car enemy chase (including Bernie), with no manual waypoint.
   Check red guidance appears even after previous GPS use, follows the live car
   whether the marker belongs to driver or passenger, and survives route refresh.
   Enemy Line Off must hide it; On restores it. Manual/fixed mission goals keep
   priority. Check police/wanted transitions separately for unchanged stability.

Tests cover late heart enumeration without the radar table, overlap/range/mission
visibility, captured preference/table bounds and lifecycle, all three colours,
ambiguous/unknown fallbacks, moving vehicle candidates, real-goal priority and
Enemy Off/On. Existing live endpoint, navigation and renderer suites still run.
No measured in-game FPS improvement or completed Deck visual test is claimed.
