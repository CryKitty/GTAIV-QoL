# Driving and compass follow-ups after PRs #10/#11

All five changes use the normal updater. Existing Road Line and Compass HUD
pause-menu switches control their respective fixes. The new Driving Camera
Height slider is also in Pause → QoL; no manual configuration is required.

## Lane data and stable offset

The existing ribbon `widthScale` controls drawn thickness (including the narrow
simultaneous escape ribbon); it is **not projected road width**. No width signal
was present in PR #10's fixed 2 m offset, so it cannot supply a meaningful width
threshold.

GTA IV's `GET_CLOSEST_ROAD` has documented per-direction lane-count outputs:
[HappinessMP native declaration](https://github.com/HappinessMP/natives/blob/c1fda03a879181cc2167362bd9c478bcb150424a/PATH/GetClosestRoad.md).
The bundled SDK supplies its legacy hash. IV's shipped taxi script also calls
this native with two vector and three word-sized outputs, using minimum-lane
selection: [public script reference](https://github.com/0x52617665/GTAIVMissionScripts/blob/9d836ff2d955ba2b19048c496f81a0f9eb6c84cb/IV/taxi.txt).
Some older SDK wrappers leave the output words typed as unknown floats; the
adapter follows the documented integer counts and rejects float-looking or
out-of-range values instead of interpreting them as lanes.

Use **forward + backward >= 2**, counting total lanes on that queried road
segment, to enable the unchanged 2 m right normal. A two-way 1+1 road qualifies,
as does a two-lane one-way road; a one-lane one-way road does not. There is no
metre-width threshold or guessed width conversion. Accept counts only in 0..8
per direction with at least one total lane, a finite bounded median value, and
finite endpoints defining a nearby segment aligned to the route tangent and
on the same level. Invalid, missing, crossing-road or other-deck data centres
the line. The adapter does not infer paint markings or legal lane occupancy.

The right normal remains the PR #10 symmetric 8 m route tangent. Lane factors
are sampled at fixed 12 m route stations and smoothstep-interpolated between
them. Cached nearby/aligned samples avoid per-vertex native queries; changes
need two observations one second apart and then blend over about 1.5 seconds.
Initial valid samples use their classification immediately. This smooths the
lateral transition, not GPS/replan publication. A short transition region may
have an intermediate offset on either side of a lane-count boundary. Cached
geometry and native metadata can still misdescribe unusual narrow streets,
ramps, medians or divided highways. No live lane telemetry from the user's
Deck proves this adapter's outputs yet; invalid output deliberately stays centred.

## People and current outing venues

The existing date-heart-first comparator is generalised to people-first in the
same layout sorter. Named person sprites and live character blips precede POIs;
existing distance/priority/bearing tie-breaks remain within each group. Named
contacts, including coordinate/contact Dwayne markers, are also exempt from the
ambient mission-place filter and local short-range cap, as hearts already were.
Native hidden/transparent markers remain hidden. People can still overlap each
other; this does not invent duplicate slots at one bearing.

Sprite enumeration already covers all 128 supported sprites; strip club 66 is
already in the social venue set. The code gap is earlier: the HUD rejected display
modes 3/5 before checking the outing exemption. The public `dwaynestrip` script
creates native coordinate/contact venue markers, uses display 3/5 for relevant
map-marker categories, and creates short-range outing markers. See the
[public Dwayne strip script](https://github.com/0x52617665/GTAIVMissionScripts/blob/9d836ff2d955ba2b19048c496f81a0f9eb6c84cb/IV/dwaynestrip.txt),
particularly its venue creation and activity setup functions. This is a code
finding, not a reproduced uploaded Dwayne session.

Recognised activity scripts now choose a fresh sprite set each one-second scan:
strip 66; food 21/22/57; drink 47; pool 46; bowling 49; darts 52; shows 70/71;
helicopter outing 56. Only that set may pass display 3/5 and bypass short-range
and mission-place filters. Display 0/1 and alpha zero remain excluded. No
native marker is created or changed. When the activity script ends, the set is
rebuilt empty and its exceptions disappear within the scan interval. Ordinary
nearby venue markers can still remain under the normal rules. Active date
scripts retain the existing date venue set and neutral colour fallback.

Generic/resident hangout managers do not identify an activity, so they do not
promote every venue. Unknown or modded activity scripts and generic EFLC hangout
stages without a recognised activity script do not get guessed venue sets.
This intentionally avoids displaying all venues indiscriminately; some activity
scripts may begin before pickup or remain during drop-off, so exact pickup/end
phase timing follows native marker visibility and script lifetime, not unverified
global-memory state. New bounded counters distinguish candidate venues from
map-display promotions without recording private text or coordinates.

## Driving camera height

The additive setting `Camera.DrivingHeightCm` defaults to 0 and is preserved by
the existing updater preference merge. The slider range is **0..200 cm**, step
**5 cm**, shown numerically next to Taxi FOV. Zero performs no camera query/write
unless releasing this feature's previously owned offset. It is a relative
`SET_FOLLOW_VEHICLE_CAM_OFFSET(true, 0, 0, centimetres/100)` request, not an
absolute world-space camera altitude or a write to the root camera.

The native is documented [here](https://github.com/HappinessMP/natives/blob/c1fda03a879181cc2167362bd9c478bcb150424a/CAMERA/SetFollowVehicleCamOffset.md)
and used by IV's heli-tour script with an explicit release call. Apply only for
a live player driving the vehicle, normal propagating game-camera child, control
and fade gates, no pause/cutscene/interpolation. It runs independently of Compass
HUD being enabled. Never steal an already active native offset; release this
feature's own offset on neutral, exit, pause or a gate change. Changed values are
sent directly, without adding to last frame's Z. Vehicle/camera-mode behaviour
and engine clamping require Deck acceptance; no measured 2 m rendered-camera
movement or guarantee across every follow-camera mode is claimed.

Menu changes append IDs without moving existing settings; the verified host's
bounded row/range/display helpers and the numeric value callback are extended.
Emulator tests exercise the real PE at preferred and rebased addresses, including
0..200/5, numeric display, callback stack/registers and retained Taxi/Audio rows.

## Shaking: evidence and targeted stabilisation

The available 19:52 capture parts include extensive authored-camera fallback
counters even though discovery reports locating a viewport. They do not expose
matrix values or prove exactly why the sampled viewport fails validation, and
appended logs include historical sessions. The source code provides several
independent movement paths:

- Native GPS refreshes can perturb XY; fitted Z and independent edge probes can
  vary at the same road position. An old height filter still moved on every tiny
  fluctuation. A matching-tangent, same-level **12 cm XY deadband** projects small
  noise onto the prior road while preserving forward progress. A world-position
  **8 cm height/edge deadband** holds small surface fluctuations. Existing bounded
  smoothing handles larger nearby corrections; new roads/turns/levels commit
  immediately. There is no temporal buffer delaying navigation/state handoffs.
- Decorative bend height depended on the car body's Z each frame. It now uses
  the fitted road height under the same player XY, so suspension bounce does not
  raise/lower the bank. The existing viewing-dependent bend remains responsive.
- When rendered projection was available, deformation still used the authored
  pose. Geometry now uses that rendered pose as well. A documented inverse-view
  slot is an additional strictly validated candidate when the primary view slot
  is rejected; perspective/position/direction gates remain. See the pinned
  [FusionFix viewport layout](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/blob/cad0970e2c82712d86c849974b62d078331ddd88/source/comvars.ixx).
  If neither is valid, the existing authored-camera fallback remains. No stale
  matrix is frozen and no projection/camera smoothing adds lag.
- The PR #10 right normal already uses the full route's symmetric tangent;
  the lane transition adds spatial/temporal classification smoothing rather
  than alternating left/right or scaling by the moving visible endpoint.

Tests cover fixed-position noise, immediate replacement roads, surface/floor
rules, unchanged wide-road geometry, lane transitions, inverse-view validation
and suspension-independent bank anchoring. Native/GPU/Deck visual testing is
still needed to establish remaining camera fallback and total jitter reduction.

## Diagnostics evidence and acceptance

All available issue #11 comments were fetched across all pages, then refreshed.
At the 2026-11-13 20:34:11 UTC snapshot there were **240 comments**. No Dwayne,
strip-club or explicit friend-activity identifier was found. The 19:52 capture
has positive social/outing-state records, but does not identify this as the
reported Dwayne outing. Only 32/89 parts of the named 19:47 capture and 30/62 of
the 19:52 capture were present at that snapshot. The Dwayne-specific session
was **not identifiable in the available issue parts**. The issue is receiving
captures, as the user confirmed after PR #11. No private log contents are copied
into this public repo and no uploaded Dwayne reproduction is claimed.

After approved merge and a successful main build, use the installed updater;
no file copying/editing. Enable Road Line and Compass HUD in Pause → QoL.

1. Drive one-lane, 1+1 two-way and multilane one-way roads, both directions,
   then bends/bridges/medians; check centring, 2 m offset and a smooth transition.
2. Call Dwayne and put a nearer shop at his bearing, including behind the car;
   people should win. Repeat with a date heart and another mission person.
3. Pick him up for a strip-club outing; look around and turn the camera toward
   both clubs without opening the map. End/cancel it and check extra visibility
   clears. Repeat eating/drinking/show outings for the correct venue set.
4. Set Driving Camera Height to 0, 50, 100 and 200 cm. Return to normal driving
   and test near/medium/far views and a downslope. Check on-foot, passenger,
   cutscene, pause/map, exit/re-entry, 0 reset and saved choice after update.
5. Watch the line while stopped, driving over bumps/slopes and around bends;
   turn the camera and trigger a wrong-way replan or change destination. Check
   less small movement, accurate slopes/corners, immediate fresh geometry and
   retained old line while native planning is pending.

All five Deck acceptance items remain outstanding. Do not merge or publish a
release as part of this work.
