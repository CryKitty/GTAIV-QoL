# Destination fades and world-anchored bend profiles

## Scope and baseline

Separate branch from main f5cd7f1; no PR #18 commits are included. This addresses destination glyphs and road presentation only. The b64 loading repair must be reviewed/deployed separately; this PR is not a replacement for it. VERSION stays unchanged. Existing updater packaging, saved choices, pause controls, GPS ownership and recovery timings are unchanged. No new native discovery/hooks or installation files are introduced.

## Vanilla comparison: what is verified

The public [GTA IV mission scripts](https://github.com/0x52617665/GTAIVMissionScripts/tree/9d836ff2d955ba2b19048c496f81a0f9eb6c84cb) create blips at coordinate anchors and leave the engine to render their arrows. For example [faustin3.txt, lines 275–278](https://github.com/0x52617665/GTAIVMissionScripts/blob/9d836ff2d955ba2b19048c496f81a0f9eb6c84cb/IV/faustin3.txt#L275) creates a coordinate blip, changes its colour/display and enables its route. The common [main.txt helper](https://github.com/0x52617665/GTAIVMissionScripts/blob/9d836ff2d955ba2b19048c496f81a0f9eb6c84cb/IV/main.txt#L3236) also creates coordinate/contact blips once when absent. The native enum identifies arrow-and-map display as 4. Checkpoint APIs likewise take world coordinates and optional alpha; no player-relative mesh reconstruction is needed for those markers.

This verifies the anchoring/API pattern, **not** proprietary engine smoothing, internal render cadence or its exact fade curve. No engine disassembly or Deck video comparison is claimed. The mod uses the same anchoring principle for its own ribbon presentation; it does not replace mission markers or call checkpoint natives to fake a road.

## Destination fade

The road-line endpoint previously faded only from 5 m to 2 m. Navigation-arrow destinations used a different near fade (about 8 m to 4.572 m at rest). Arrival notification and native mission/GPS removal can happen around 12 m, leaving those glyphs visibly opaque just before removal. This mismatch is reproducible from the code; it is not an uploaded session measurement.

Both now use a cubic smoothstep from `12 + max(16, speed * 0.65)` metres down to 12 m. At rest/low speed that is 28 → 12 m; at 50 m/s it is 44.5 → 12 m. Speed does not change the road-line length. The existing far-distance gates remain. An unexpectedly removed Road Line endpoint gets a 300 ms smooth retirement at its last world position, capped by its current approach opacity so approaching cannot brighten it. Only the glyph retires, never an invented/retained route tail. New destinations/targets replace immediately. Toggle Off, stale snapshots and device reset clear the glyph. Navigation arrows keep their existing removed-cue retirement in addition to the shared approach fade.

## Road stability investigation and fix

Checked paths:

- **Surface probes and GPS noise:** PR #12/#13 spatial XY deadband, height/edge stabilizer and validated surface reuse remain. No frame-time queue or additional damping is added.
- **Sampling/refresh:** whole-segment Hermite tangents could bow a long leg as its native endpoints changed. Tangent corrections now stay within 12 m of corners; straight legs outside those windows remain exactly on the native segment, independent of changing endpoint distances. Stations were already quantized, but banking was calculated on the cropped visible points. `roadLineLift` then found each contiguous bend's centre, radius and strength in that slice on every render. Moving the near cutoff/radar edge changed that profile even at the same world road corner.
- **World geometry:** sampling now retains native road-distance station metadata. A hidden, bounded 80 m road context on either side supplies the bend centre/radius/strength (60 m maximum radius plus the 10 m curvature window). Only the original visible/fitted path is rendered. Extra context uses already supplied native route points, not new planning requests or fabricated roads.
- **GPS head cropping:** the existing spatial matching pass also supplies station correspondences. A prior bend is reused only if its entire visible footprint matches the same forward tangent, position and elevation, with consistent station correspondence. A changed corner/level bypasses it immediately. This adds a per-bend linear check, not a second quadratic matching pass or a timed replan delay.
- **Terrain:** peak elevation uses the currently fitted road at the bend anchor, with surface clearance removed, rather than treating the ribbon's own 24 cm clearance as a hill. The deliberate smooth uphill visibility boost still responds to approach.
- **Camera and decorative tilt:** the existing validated live gameplay viewport, camera-object cache, authored fallback and small-heading-noise filter remain. Projection is never temporally smoothed: doing so would make world geometry lag the camera. Intentional view-dependent twist still responds to looking around. No sideways road offset is restored.

The deterministic moving-player reproduction observes the old cropped-profile height changing at a fixed corner; the new profile stays within 1 mm in that test. Additional coverage handles native head cropping, opposite-direction replans, distinct floors, repeated surface stabilization, radar clipping and metadata truncation. This establishes the corrected geometry defect, not a claim that every Deck jitter source has been visually eliminated. Missing gameplay matrices still use the existing authored fallback; genuine route/surface changes still update immediately. Native collision streaming can still shorten the tail.

## Validation / acceptance

The PR records complete local and CI results. C++ tests exercise smooth monotonic fade, high-speed distance, early GPS loss, new-goal replacement, explicit clear, clock wrap, cropped bend retention and immediate replan/elevation changes. Windows D3D tests check the actual endpoint's full/half/zero pixel brightness. Existing route retention/recovery and surface tests remain.

After the loading hotfix and this change are approved and main CI succeeds, update normally with the existing updater; no file edits. In Pause → QoL enable Road Line and Turn Arrows. Approach waypoint and mission destinations slowly and at speed; check fading and Off cleanup. Drive/stop/restart through bends, slopes, bridges and S-curves, looking around and comparing with stationary road markings. Check waypoint replacement, wrong-way recovery and enemy/police lines. Deck visual acceptance is outstanding; no new uploaded reproduction evidence is claimed.
