# Straight road lines, native radar gates and compass text stacking

This follow-up starts from PR #12's merged main. All changes are supplied by the
normal updater and controlled by the existing Pause → QoL switches. No new
setting, file edit or release is required.

## Road geometry: offset reverted, other stability work kept

PR #12's offset factor was calculated independently at fixed 12 m stations. A
straight-path reproduction with alternating one-/two-lane observations produces
0..2 m lateral movement and repeated direction reversals. Smoothstep makes each
change continuous but does not make the resulting road straight. The temporal
hysteresis also operates per sample, so adjacent samples can disagree, and the
small GPS-noise deadband can hold individual small lateral changes differently.
This is a deterministic code reproduction, not a lane trace from the Deck.

The current fix removes both the PR #10 lateral offset and PR #12 lane querying,
caching and blending from primary and escape rendering. The centreline is fed
directly through the existing surface fitter and small-noise stabilisers. The
unused lane helper and offset application function are removed. Straightness
wins; no remaining offset is claimed safe from the reported snaking. Native
GPS can still place the line over road dividers/central structures. There is no
invented side-avoidance geometry or route freeze.

PR #12's camera-height setting, world-position surface/XY noise controls,
ground-referenced banking and decorative tilt damping remain. GPS requests,
wrong-way timing, immediate route handoffs and anti-blink retention are unchanged.
Regression tests cover straight roads in both directions, changing native point
density, origin, speed and frame position, diagonal collinearity, surface fitting
and repeated stabilisation. Production-path checks prevent a lateral offset
stage from silently returning.

## Car-wash flood: classification and visibility are separate

The bundled SDK identifies garage **79**, lawyer **80**, car wash **91**, United
Liberty Paper **92**, and boss **93**. PR #12's hand-written person list incorrectly
included garage 79 and car wash 91, omitted lawyer 80/boss 93, and the HUD used
`person` to bypass short-range filtering. Thus distant short-range car washes
were eligible both for the distance bypass and people-first overlap. The
corrected identifiers are checked against the actual SDK at compile time.

The native gate is now the same for every discovered marker:

- supported native display mode (2/4), positive native alpha and valid position;
- native `IS_BLIP_SHORT_RANGE`, checked against the current native radar radius;
- only sprite 54 (date heart) has the deliberate any-distance exception.

No people/outing/compass-selection exemption bypasses these checks. Native
long-range characters remain long-range, and people-first sorting still applies
**after** eligibility. Native short-range characters respect the game's flag.
All POI classes use the same rule, including shops, venues, stations, garages,
car washes and services; this is not a car-wash-only range cap. Native long-range
POIs can still appear at distance if the game explicitly marks them that way.
Hidden/transparent hearts remain hidden despite the heart distance exception.

PR #12's extra display-mode and range promotions for outing venues are removed
under the user's newer native-radar-only visibility direction. Activity detection
and date-label/colour behaviour remain, but do not grant visibility. The former
ambient/mission POI veto is also removed: game visibility decides eligibility,
not a guessed mission/social filter. Persistent waypoint/mission readouts remain
independent of whether an icon is currently within radar range.

### Does radar processing continue while hidden?

`MinimapLayout` displaces only radar geometry and its live layout position. Its
visibility-query hook returns the original result. `MinimapRing` suppresses
radar-owned border/fill callbacks; it does not disable script blip maintenance,
change native display/alpha/short-range state, or force Radar Off. Existing
native-preservation tests exercise those contracts. The available captures also
show changing live radar-range samples while Compass HUD is active. The public
IV map-maintenance scripts choose long-/short-range blips using
`SET_BLIP_AS_SHORT_RANGE`, independently of compass rendering.

We can therefore consume native metadata and the verified live range export
without inventing separate distance exceptions. The range export reads the
current radius, not its target or animation increment. If unavailable, the
existing bounded fallback radius remains; no manual setting change is required.

**Honest limit:** no verified native in this SDK returns a particular blip's
final per-frame radar draw result or post-render fade alpha. The compass reads
native alpha, not an invented approximation of an unexposed renderer fade. This
is the closest supported native-metadata gate, not a claim of pixel-identical
radar edge/ellipse/fade culling. No unverified memory offsets or new renderer
hooks are added to obtain that missing information.

Reference identifier/display definitions:
[IV-SDK .NET enums](https://github.com/ClonkAndre/IV-SDK-DotNet/blob/f5f5d63f705819525fe2f5307648661fb5613124/IVSDKDotNetWrapper/enums.h).
The repository's bundled SDK remains the build-time authority. Public script
reference: [IV main](https://github.com/0x52617665/GTAIVMissionScripts/blob/9d836ff2d955ba2b19048c496f81a0f9eb6c84cb/IV/main.txt).
No private capture text is used as source documentation.

## Text stack below the compass

The no-manual-waypoint mission path updated the hover-name anchor after drawing
the persistent readout, but the hover-distance path still branched on
`havePinned` and could use the old fixed distance position. Presence of a
persistent mission readout and presence of a manual waypoint are different states.

`BelowCompassTextStack` now owns the calculation. Both persistent drawing paths
reserve through their actual distance baseline, after all explicit name lines.
Hover names start 0.036 normalised screen units below that baseline; their
single/multiple name lines and distance are laid out afterward. The existing
0.024 line step is retained. Setting/waypoint-result/arrival captions remain in
the common caption slot above a reserved persistent block; the progress ring
still belongs to the focused icon. No text is moved above the compass.

With no persistent block, the native hover name/distance keep their prior
positions (0.087 / 0.108). Tests cover one through four static name lines,
manual-waypoint and mission paths, all feedback captions, multi-line hover text
and the unchanged no-readout layout. Duplicate objective suppression is kept.

## Diagnostics evidence

The first all-page issue #11 fetch contained **772 comments**, through
**2026-11-13 22:40:30 UTC**. It includes part of a capture timestamped 22:22,
after PR #12's 20:59:58 UTC merge: **30 of 87 parts** were available. These are
capture timestamps, not proof that every appended log record belongs to the
reported drive/build. Historical log content is appended in those parts.

No available capture identifies a wavy line or car-wash flood directly. There
are no screenshots, route-vertex/lane-station traces, or per-blip sprite + range
+ visibility records that establish those symptoms. Available recent-capture
parts include compass counts and changing live radar-radius samples, but no
new PR #12 outing counter was found in the parts initially available. The exact
tested build/session and relevant full tail cannot be certified from those
partial records. User feedback supplies the visual regression report; the
straight-road wave and POI classification errors are code/test findings.

No private log contents, credentials or actual private file paths are copied
into the public repository. Final refreshed issue coverage, if more parts
arrive, is recorded in the PR description without claiming a visual reproduction.

## Updater-only Deck acceptance

After approved merge and a successful main build, close GTA IV and use the
installed updater (or Pause → QoL → Check for QoL Updates, then exit for apply).
No file copying/editing; all existing choices, including camera height, remain.

1. **Pause → QoL → Road Line On.** Drive a long straight street at several speeds,
   stop/start and reverse direction; then test bends, ramps, bridges, a waypoint
   change and sustained wrong-way recovery. Check centred, straight geometry
   without periodic sideways waves. Enable Police Escape Line and repeat while
   wanted. Old geometry should remain during calculation; replans must commit
   promptly. Centreline placement over a divider is an acknowledged trade-off.
2. **Pause → QoL → Compass HUD On.** Approach and leave car washes, garages, shops,
   stations and venues. Compare Compass HUD Off/native radar under the same zoom
   and conditions. Local icons should respect range; native long-range markers
   can remain. Check an outing and a compass-selected POI do not bypass range,
   a character wins overlap against a nearby place, and a far date heart remains.
   Check native hidden/transparent markers are not promoted.
3. **Compass HUD On.** During a mission with a persistent name and miles readout,
   hover another icon with no manual waypoint. Repeat with a manual waypoint,
   long/multi-line objective and no readout. Hold an icon, set/clear its waypoint
   and reach a destination; captions, the full static readout and hover name/
   distance must stay separate and below the compass. Normal no-readout hover
   positions and hold duration/ring are unchanged.

All three Deck acceptance tests remain outstanding. CI does not prove final
native radar fade parity or the rendered in-game appearance.
