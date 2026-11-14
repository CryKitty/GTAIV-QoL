> Current load hotfix: native QoL footer extension is held back; the exact b56 FusionFix host is restored and build identity remains on the QoL update row. See [load investigation](B64_WORLD_LOAD_HOTFIX.md). The text below records the earlier implementation.

# Camera presets, radar-range ribbons and native version footer

## Camera

Pause → QoL → Driving Camera Height is a three-position named control using the
existing native menu callback/renderer: Low (Default), Medium, High. The former
0–200 cm / 5 cm fine slider and Custom are not offered. The positions are 0, 1,
2 in the UI, mapped to relative follow-vehicle offsets 0, 45, 120 cm. High is
75 cm above Medium. Low preserves the previous zero behaviour: no camera native
query/write unless releasing an offset this mod already owns. Script-owned
camera offsets, cutscenes, pause and driver/control gates are unchanged.

The existing DrivingHeightCm preference and public ABI remain in centimetres.
Load normalises a valid old 0–200 cm preference to its nearest preset: 0–22 → Low,
23–82 → Medium, 83–200 → High. Choosing a preset writes its effective value through
the existing settings path. Legacy saved values need no manual edit; the updater
continues preserving them, and each load resolves them consistently. No new key.

## Road line

The former visible horizon was 35 + 5v + .025v² metres, clamped 35–300, or a
special 1200 m tracked-target limit. Those visible cutoffs are removed. Both
primary and escape ribbons now call CompassHud::radarRange(), the same bounded
QoLCompassRadarRange export used by compass visibility. If unavailable, they use
the compass fallback (300 m by default). Valid live radius is 10–5000 m.

The route is clipped in XY against the player-centred circle, at the first exit
from its connected visible section. A winding road can have more arc length than
the radius. A 1.5 m native-road guard compensates for the existing 1.25 m spline
bound; the smoothed result is then clipped exactly. Retained routes and tracked
endpoints are clipped again before submission so live range changes cannot leave
an old tail or destination symbol beyond the boundary. Edge heights/bank values
are interpolated; no lateral offset, new damping or recovery delays are added.
The existing 12 m end fade remains. Radius itself can change with native driving
state; this is distinct from using vehicle speed as our own line-length formula.

readGpsRoute already copies every available native point (up to 214 per buffer),
without speed-dependent planning truncation. No new primary planner request is
necessary. A quantized road-station spacing budgets at most 600 samples plus
native corners, with a 1000-point safety cap, rather than starving a large radius
at the former 500-point cap. Tests feed the complete native buffer through this
path at rest and speed, at 50–5000 m radii. Escape candidate planningLead and all
primary wrong-way/borrowed-window ownership/timing remain unchanged.

The guarantee is no additional speed or render-budget starvation within a valid
native route. Native GPS failures/short buffers, the deliberate short wrong-way
lead-in leg, a nearby destination and missing streamed surface/collision data can
still end a real line inside the circle. No fabricated continuation is added.
This is a radar-radius boundary, not a claim to reproduce the minimap's rotating
shape or final per-pixel clipping.

## Version footer

Verified upstream source: FusionFix 5.1.2 [extrainfo.ixx](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/blob/cad0970e2c82712d86c849974b62d078331ddd88/source/extrainfo.ixx).
ExtraInfo reads executable, FusionFix and ASI Loader version resources and extends
one normal menu help/footer string. Its Game-tab branch (pMenuTab == 0) adds the
purple version list. Graphics and Audio have different text in this same hook.
There is no exported AddVersion API in that path.

The verified bundled host now calls the optional core UI API Footer tail directly
from ExtraInfo's final-string assignment. The previous second hook on the game's
renderer argument pushes was unreliable: it changed bytes included in
FusionFix's own startup signature. The native string extension no longer needs
that game signature or a second MinHook hook. The checked seed adaptation patches
only the two EAX/EDX context stores, preserving sections and relocations. Old API
versions safely keep the original string. Matching, purple styling and warning
placement are retained; no overlay is used.

Only after an actual native Game-footer extension does the core set footerSeen,
which removes the duplicate identity from the QoL update row. Until then (or with
ExtraInfo disabled/old host/old API), PR #14's identity remains. Automatic logs
report callback marker flags, length and success, never the input text. VERSION
still names releases and the CI build number still identifies each update.

See [repair evidence and lifecycle](CAMERA_FOOTER_ENEMY_REPAIRS.md) for camera
apply gating, enemy cleanup, diagnostics limits and the regression coverage.

## Acceptance

Use the updater-only steps in the newest CHANGELOG entry after merge/main CI.
Native Game-footer positioning and all live driving/Deck acceptance are still
outstanding; deterministic tests do not claim a screenshot/gameplay result.
