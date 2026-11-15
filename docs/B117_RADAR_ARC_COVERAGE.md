# The ring that survived the b117 frame hooks

## Evidence and correction

The supplied short b117 session records both texture-frame callbacks suppressed
from their first draw, followed by 462 calls / 462 hidden. The radar query runs
(231 later queries), but its first native answer is already false. This refutes
both “the frame hook did not run” and “the query is never consulted.” It does not
prove which pixels were visible: no screenshot or GPU pixel capture is supplied.

The earlier bounded CE native-code capture identifies a further, concrete path.
The radar queue's middle callback reads player health and armour, obtains HUD
colours and calls one five-argument cdecl leaf eight times. The float second
argument is a fill fraction; the first side argument selects health or armour.
Other arguments carry packed colour and flash/extra overlay selectors. The leaf
also reads armour and starts a segmented arc loop (23 segments), independently
of the `radarringfront` and `radarringback` texture callbacks. The captured
parent/leaf VAs were 00903060 / 00903330; these are evidence references ONLY,
not addresses used by the implementation. PR #31's labels “health-armour-front”
and “health-armour-back” overstated what its two texture hooks covered.

Source comparison with b99 explains the difference in coverage: b99 displaced
both game-owned HUD_RADAR layout and live positions each script tick, in
addition to filtering border callbacks. A pause refresh could consume those
positions and leave the arc geometry offscreen, matching the user's accepted
post-pause state. b117 correctly leaves those coordinates unchanged. It therefore
also leaves the independent arc path at its native anchor. The logs cannot
identify the exact cached field consumed by that b99 pause, so that part remains
a source-based explanation, not a measured engine refresh trace. Do not restore
coordinate mutation or skip the entire parent callback to imitate it.

The compass's own circle is the waypoint-hold progress indicator near its
top-center strip. It is not drawn at the bottom-left minimap anchor and is kept.

## Narrow suppression and source inventory

| Circular source | Policy with Compass On in gameplay | Record |
| --- | --- | --- |
| `radarringfront` textured frame (border/disc frame) | Suppress dedicated callback | `component=radar-disc-border-texture`, calls/hidden |
| `radarringback` textured frame (groove/background frame) | Suppress dedicated callback | `component=ring-groove-background-texture`, calls/hidden |
| Native health arc and its flash/extra overlays | Suppress typed five-argument leaf | `element=health-front-fill`, amount/colour/overlay/extra, calls/hidden |
| Native armour arc and its flash/extra overlays | Same leaf, other side | `element=armour-back-fill`, same evidence |
| Another phase calling that same fixed-anchor arc leaf | Suppress too, do not allowlist callers | `caller-known=0`, caller RVA, other-phase count |
| Radar map disc/content | Existing mapped HUD_RADAR display-answer filter only | Original/masked radar query and explicit `map-disc=mapped-display-query-only` coverage |
| Own compass progress ring | Keep; different anchor | `own-circle=top-strip-waypoint-hold-not-minimap` |

“Front/back fill” labels distinguish the health/armour sides for this diagnostic;
they do not assert that an unknown additional texture/pixel pass was captured.
The code has not independently isolated a native map-disc GPU submission, so it
does not claim a disc draw count or pixel proof from the query count. If pixels
remain after all four recorded visual sources are suppressed, their identity
requires the next visual retest / GPU capture; these logs will narrow it instead
of calling the old frame counters complete coverage.

The parent is resolved through the independently verified front/back radar queue
relationship, then inspected, NEVER hooked. Candidate leaf requirements include
both native ped-field references in the parent (captured CE displacements A84
and B84, not SDK offsets assumed across releases), at least six five-argument
caller-cleanup sites, both side constants, the cookie/stack/float-argument ABI,
a completely decoded cdecl body, and the leaf's armour reference. Ambiguous or
missing signatures leave the source unfiltered and explicitly report that.
No version-specific VA or shared triangle/sprite primitive is hooked. No ped or
camera native is called by the filter. The parent lifecycle and message work
runs regardless. This retains the wardrobe-help fix.

Preparation stays behind the existing usable-world gate. Frontend draws pass
through with original arguments. Off uses the existing synchronous pause-setting
release and removes arc plus texture hooks before unpause; native geometry and
preferences are never changed. Repeated cycles install again from restored code.

## Validation and next Deck acceptance

The new x86 executable test runs a synthetic parent and the actual typed MinHook:
CPU/lifetime calls continue, both fill sides and another caller are suppressed,
frontend/Off forward all five arguments exactly, repeated cycles restore draws,
and wrong float ABI / too few matching sites / callee-cleanup bodies are rejected.
The existing frame, layout and immediate pause-release tests remain in CI.
This is controlled executable evidence, not a post-fix Deck rendering trace.

Use the updater after merge and successful main CI; no manual settings/file work.
Cold-load with Compass HUD On, check before pausing, get into a vehicle, then
cycle Pause → QoL → Compass HUD Off/On several times. Off must show the complete
native radar/disc/ring immediately; On must remove the native cluster. Check a
wardrobe prompt clears normally and road guidance still functions. The automatic
coverage/first-draw/release records must identify each resolved visual source,
its actual suppressed state and any second caller. Deck acceptance is outstanding.
