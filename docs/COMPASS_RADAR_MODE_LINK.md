# Link Compass HUD to the native Radar mode

The b122 Off report was explained by the user's native Display → Radar setting
being Off. Turning that native setting On restored the map. The game's captured
CE radar queue separately gates map/blips on Radar mode while HUD On still draws
the ring. The b122 frame and arc restoration was working. This PR adds the newly
requested link; it does not claim to fix a missing native draw callback.

## Behaviour and ownership

At the existing usable-world initialization, and on Compass setting transitions:

- On: persist the exact current native mode, then set native Radar to Off (0).
- Off: restore the saved mode, verify the read-back and clear the saved record.
- A deliberately saved Off (0), On (1), or Blips Only (2) is restored exactly.
- A repeated setting value / ordinary frame does not reapply a completed action.
  Manual Display → Radar changes are left alone until the next transition.
  The existing independent compass presentation filters remain as shipped.
- At startup with a pending saved record, reuse that baseline rather than saving
  the previous session's temporary Off. Startup with Compass Off restores a
  pending baseline; without a baseline it changes nothing.
- Shutdown leaves native state and the record alone. This preserves manual
  changes until a new startup/transition, and also covers abrupt termination.
  A subsequent On startup reuses the baseline; a subsequent Off restores it.

`[Compass] RadarRestoreMode` in the existing GTAIV-QoL.ini is an INTERNAL journal,
not another user control. -1 means idle, 0/1/2 mean a saved baseline. The helper
preserves it during verified updater installation. The Win32 profile write and
cache flush must both succeed before the first native change. Runtime rejects
malformed records; the updater preserves their fail-closed state instead of
silently converting them to an empty journal. A failed native restore retains
the record for a later transition/startup. No guessed default On is used. No
user copies/edits a file, and the payload file set does not change.

This protects normal relaunches and process interruption, not arbitrary loss or
external deletion of the configuration file. If persistence/discovery fails,
the link logs the failure and does not change an unverified native value; the
existing compass filters still operate. It cannot reconstruct a deleted saved
baseline. VERSION and the verified FusionFix host remain unchanged.

## Supported native field and guards

Use CMenuManager's uint32 Radar mode, independently documented by IV-SDK as 16
bytes after the HUD preference. The captured CE radar queue has two HUD-zero
checks, four Radar-zero checks and one Radar==2 check. Resolve that relationship
from decoded, padded executable procedures; require a unique field and a valid
0..2 value in committed writable image data. No captured game VA, save name,
blip inference, native invocation or native HUD geometry write is used. The
first discovery/write is behind the existing usable-world gate. The synchronous
pause-setting export uses the same resolver/state under a mutex, so toggles do
not wait for a later script tick after initialization. Missing discovery retries
at bounded intervals only until the initial action can run; no successful link
is polled/reasserted against manual menu changes.

Each action records `Compass radar link: event=... enabled=... source=...`, native
mode before/after, saved mode, action and verified status. Toggle event IDs match
the core `Compass setting` record; startup uses its own event ID. This is a
scalar native-mode read-back, not GPU/pixel proof. Existing per-element texture,
arc and release records stay available alongside it.

## Interactions and acceptance

The user confirms b122 guidance/compass/audio worked while native Radar was Off.
Source audit also finds:

- Compass handles come from the live radar table/native blip iterator, not the
  minimap draw or native Radar preference. Sprite display/alpha/range gates stay.
- Primary/escape road-line range reads the native radius via the unchanged
  FusionFix range export, with the existing bounded fallback. It does not read
  Radar mode or require a map draw. Speed-independent range planning remains.
- Map Autozoom changes the existing native zoom path; it neither reads nor writes
  the Radar mode. Its menu setting and zoom data are independent of this link.
- Off removes the existing frame/arc/query filters. With a saved On, the game
  itself draws disc, content, blips and ring; QoL does not synthesize them. A saved
  Off remains Off; scripted/cutscene HUD gates still belong to the game.

Deterministic tests cover 0/1/2 mapping, duplicate calls/manual override, new
transitions, persisted On/Off startup recovery, write failures and malformed
journals. The Windows test resolves synthetic CE-like code, uses the actual
Win32 persistence adapter through relaunch, and checks event-linked records.
Existing frame/arc/layout tests remain. No post-change Deck acceptance is claimed.

After merge and successful main CI, install normally with the updater. Set native
Display → Radar On with Compass Off, then turn Pause → QoL → Compass HUD On:
Radar should read Off and the confirmed clean compass remains. Turn Compass Off:
Radar must read On and the full vanilla radar returns. Repeat with an initial
Radar Off and Blips Only: Off restores those exact values. While Compass On,
manually change Radar; it must stay changed during play until the next compass
transition. Exit/relaunch with Compass On, then Off: the original baseline must
still restore. Repeat Off/On cycles, drive a mission/waypoint and police route,
check Map Autozoom and pause-map operation. Normal automatic captures should
show the paired setting/link events and exact before/after modes. No file edits.
