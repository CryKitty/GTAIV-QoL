# Current beta bundle and controls

GTAIV-QoL is a bundle of **seven components**, not a single navigation mod. The authoritative [inventory](BUNDLE_INVENTORY.md) maps every component to active source and installed binaries. [README](../README.md) lists their behavior and pause-menu controls. Historical design/research files in `research/` and source/tests do not imply additional shipped features.

## Runtime architecture

- **GTAIV-QoL.asi:** shared versioned settings API, pause-menu adapter and 300% interactive Map Autozoom.
- **GTAIV-QoL-Pause.asi:** synchronized voiced-cutscene pause/overlay/audio capture, Cutscene Audio output gain, statically linked RoadNav, road ribbons, compass and taxi view controls.
- **Modified FusionFix 5.1.2 menu host:** the verified existing host with bounded menu adaptations; not an official FusionFix binary. Startup, original sections/relocations and existing public settings IDs are retained. A new position-independent section supplies the five added line-type rows. The UI adapter maps the reserved gameplay-voice row to a Road Line toggle and the former voice-enable row to Taxi FOV. Existing third-party notices remain in the installer.
- **GTAIV-QoL.ini:** managed settings with working defaults. Players use only the pause menu; manual edits are not an installation/test step.

RoadNav is linked from its vendored source into the companion, not loaded as a third ASI. The installer backs up/removes duplicate standalone RoadNav copies from root/plugins/scripts. Both bundled ASIs and the menu host are installed through the same checksum/backup/rollback path; no component is separately dropped from the Nexus archive.

## Current menu

QoL contains Turn Arrows, Map Autozoom, Pause During Cutscenes, Taxi FOV, Road Line, and Compass HUD. Audio contains Cutscene Audio (%). Road Line **On (twisted) / Off** applies live to primary and police ribbons; the five individual Police Escape / Mission / Friend / Enemy / Custom Line switches default On and hide only their own ribbon. The five switches are indented directly beneath Road Line, grey and non-interactive while master Off retains their choices. The independent Turn Arrows toggle controls turn cues. Taxi FOV is 40–120 degrees. Cutscene Audio is 0–1600%, defaults to 100%, affects the whole cutscene output mix, and restores gameplay gain afterward. Reserved GameplayPercent is not a gameplay-volume feature.

D-pad Left selects a focused compass waypoint after a 900ms hold on foot or in a vehicle. Phone, combat, pause, cutscene, focus/control and vehicle-context transitions cancel the hold until release. D-pad Left is reserved during eligible gameplay; other controller values, steering and triggers are forwarded. Arrival clears owned pins near the destination. Compass also includes unread-text indication, health/armour damage feedback, social venues and hostile/friendly/objective styling.

Map Autozoom changes the interactive opening zoom only and preserves later manual map navigation. Esc/controller Start pauses/resumes supported voiced cutscenes without a timeout. Turning a component off restores the state it owns where supported; do not hot-unload ASIs.

## Navigation limits and recovery

Manual/mission and police escape ribbons have separate snapshots/renderers and may coexist. Police planning borrows the native GPS buffer, retains committed geometry during retries and compares actual road paths for proximity to visible police markers. It is not sight-line prediction; it does not model future/unmarked police or shortcuts outside the native vehicle graph. Destination colours and eligible-enemy automatic guidance are included subfeatures.

Surface fitting checks road height/elevation and may reuse validated primary heights for escape geometry on a shared road. Ribbons require validated scene depth and stay hidden rather than drawing through opaque geometry. Missing discovery retries at bounded intervals; toggle/clear/device-reset invalidation is performed on the render thread. The previously failing post-PR-#3 session has not been revalidated in-game; tests are not a claim of that confirmation.

Search areas and mission-zone shapes are omitted from the compass. Radar layout/ring observers preserve unrelated HUD and pause-map behavior where signatures validate. Unknown layouts fail closed. See [navigation investigation](NAVIGATION_EXPERIMENT.md) and [known issues](KNOWN_ISSUES.md).

## Installation and distribution

The Nexus beta ZIP includes the exact build's update payload and a short self-reading guided launcher. It uses the existing dependency guide and verified updater installation path without requiring private-repository access for that first install. Missing dependencies remain author-hosted downloads; the verified Microsoft X86 installer is run and re-checked in the selected GTA IV Proton prefix. Existing development installations migrate once to this public repo/main through the new integrated installer; see UPDATER.md. Nexus installs include the credential-free public beta updater; after separately approved beta release/update asset publication, its shortcut checks for new published betas. The old repository stays private.

Supported menu-host hashes and state are checked before writes; unknown/custom FusionFix hosts are rejected. Existing settings/assets, backups, duplicate handling, session locks and rollback remain. Existing automatic upload configuration is retained; first-time installs do not create upload credentials. [Release procedure](RELEASING.md) describes tags/artifacts; this PR does not publish anything.
