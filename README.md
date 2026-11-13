# GTAIV-QoL — beta mod bundle

Seven quality-of-life components for **GTA IV Complete Edition 1.2.0.59**, delivered together by a guided installer and updater. This is **0.1.0-beta.1**, not a 1.0 release. Settings are controlled in the game's pause menu; no INI editing is required.

The canonical source and development-build branch is **[main](https://github.com/CryKitty/GTAIV-QoL/tree/main)** in this fresh public repository. The old history remains in the private GTAIV-QoL-dev archive. See [release procedure](docs/RELEASING.md).

## Included components

| Component | What it does | Pause-menu control |
| --- | --- | --- |
| Road Navigation Line | Draws a surface-fitted native-GPS ribbon, with camera-facing twisted presentation. Includes separate red/blue police escape guidance alongside manual/mission routes. Sustained moving wrong-way driving requests a forward lead-in for manual/fixed primary destinations after 1.5 seconds; see [replanning](docs/PRIMARY_ROUTE_REPLAN.md). | QoL → Road Line: On / Off; Police Escape / Mission / Friend / Enemy / Custom Line |
| Turn Arrows | Directional turn chevrons, linked turns, destination cues and wrong-way/U-turn guidance. Also supports automatic guidance toward eligible moving enemies when no explicit route is selected. | QoL → Turn Arrows |
| Map Autozoom | Opens the interactive pause map at 300% while preserving subsequent manual navigation. | QoL → Map Autozoom |
| Pause Cutscenes | Pauses picture, dialogue, actors and camera together in supported voiced cutscenes, with a blurred frame and Paused label. Press Esc or controller Start again to resume; no time limit. | QoL → Pause During Cutscenes |
| Cutscene Audio | Changes the **whole game output mix during cutscenes**, then restores gameplay gain. It is not a separate dialogue-only slider. Default 100%; range 0–1600%. | Audio → Cutscene Audio (%) |
| Compass HUD | Replaces the minimap with a camera-relative compass, centered-marker distance, health/armour feedback and unread-message indicator. Hold D-pad Left for 0.9 seconds to set/clear a focused waypoint on foot or in a vehicle. A filling ring surrounds the hovered icon, with hold/result/arrival captions below the icon in the objective-label style. Heart/date icons are discovered after date initiation at any distance, with the normal focused distance readout. Date venues currently stay white while the unsafe preference-access path is disabled. An active waypoint’s unchanged objective/name and distance stay below the compass without hover; waypoint distance uses miles. | QoL → Compass HUD |
| Taxi FOV Slider | Adjusts the rear-seat taxi interior camera, 40–120 degrees, default 70. | QoL → Taxi FOV (degrees) |

Destination colours, police escape planning, compass waypoint/notification features and road-line recovery are included within these components, not extra ASIs to install. See the [code-backed inventory and package map](docs/BUNDLE_INVENTORY.md).

## Requirements and installation

- GTA IV Complete Edition **1.2.0.59** and its compatible ScriptHook/ASI loader, plus the Microsoft X86 runtime in the game's Proton prefix.
- The supported **FusionFix 5.1.2 host and assets**. The installer supplies a verified modified menu host; unsupported/custom hosts are rejected rather than overwritten blindly.
- The guided installer targets **Steam Deck / Linux Desktop Mode with Python 3 and KDE or Zenity dialogs**. Internet is needed for missing dependencies downloaded from their authors. A Steam-installed Proton tool and the existing GTA IV prefix are used for verified runtime and loader setup.

**First install from Nexus:** download the beta Deck ZIP, extract the entire archive into one folder, close GTA IV, and run **Install-GTAIV-QoL.desktop** in Desktop Mode (Execute/Trust if prompted). Keep the supplied update ZIP beside the launcher. The GUI checks for a newer published public beta, downloading and verifying it if available; otherwise it uses the verified bundled beta. It then finds GTA IV and walks through one missing requirement at a time, with a final checklist before Install and a clear Done state afterward.

For missing dependencies, use **Set up** for the current requirement, or open their download pages and save the supported ZIP/EXE files to Downloads. Click **Retry / check downloaded file**: setup validates and places the loader, native ScriptHook, CE patch and FusionFix files itself. It keeps your original downloads. The Microsoft X86 installer runs in the selected GTA IV Proton prefix, and setup re-checks the result. Setup reads Steam’s configured compatibility tool automatically; there is no Proton-version chooser. Launch GTA IV once from Steam first if its prefix does not yet exist. No manual copying into the game, registry editing or INI editing is required.

**After installation:** use the installed **Update GTA IV QoL** shortcut. The updater is part of the installer, not a separate required download. Nexus installations use the credential-free **public beta channel**, including published prereleases. Re-running the same installed-version launcher also hands off to the updater; a newer downloaded archive can install/repair its bundled beta. Checks happen when you launch it, not through a new background service. Menu settings and backups are retained.

**Existing development users:** migrate once using the new main build's integrated installer, then use the installed updater. It selects the newest successful non-PR `build.yml` run on this repo's **main**. Existing channel/menu choices remain. GitHub requires read-only authorization for Actions artifact downloads even from a public repo; select the new GTAIV-QoL, not GTAIV-QoL-dev. Public beta release checks/downloads never use that token or the separate log-upload token. Exact [Deck migration and automation steps](docs/UPDATER.md#migrate-an-existing-deck-install).

**Publication status:** the clean public source is available; no beta release or Nexus page has been published by this migration. Until a beta/update asset is separately approved and published, beta checks report no published update; the verified bundled archive still installs. Public CI restores the exact verified FusionFix menu seed from this tree, without private artifact access. See [installer audit](docs/INSTALLER_AUDIT.md).

The installer backs up replaced files, removes duplicate standalone RoadNav ASIs, preserves supported settings/assets and rolls back failed bundle writes. Do not hot-unload either bundled ASI. Existing automatic log-upload configuration is retained; first-time Nexus installs do not create upload credentials.

## Beta status and known issues

Steam Deck gameplay testing has confirmed synchronized voiced-cutscene pause/resume and compass waypoints in vehicles. That does not establish every feature or mod combination as fully verified. The road-line recovery changes from PR #4 have D3D9 pixel/regression coverage; confirmation in the previously failing game session is still outstanding. Native Windows compilation/loader tests pass; equivalent Windows gameplay coverage is not claimed.

- **Rare, unconfirmed vehicle auto-shoot:** Niko may occasionally keep firing from a car until relaunch. It may be a conflict with other installed mods; attribution to this bundle is unconfirmed. PR #3's vehicle-hold guard and diagnostics remain in place. This beta does not claim a fix, and this release pass does not investigate it without session logs.
- Police avoidance is a **proximity heuristic**, not sight-line prediction. It cannot model building occlusion, future police movement or unmarked officers. Roads come from GTA IV's native vehicle graph, not custom shortcuts or lane geometry.
- The compass omits police search areas and mission-zone shapes. The full pause map remains available. Mission-specific marker behavior still needs broader testing.
- Road ribbons and police escape remain experimental beta features. Ribbons require validated scene depth/surface data and can be suppressed while resources are unavailable. Missing scene discovery now retries; unsupported signatures remain fail-closed.
- Audio-less cutscene pause and independent gameplay/dialogue-only volume controls are not implemented. Large cutscene boosts can distort audio; 100% preserves the original level.

See [known issues and acceptance checklist](docs/KNOWN_ISSUES.md). Performance numbers are synthetic CPU measurements, **not measured in-game FPS gains**: [profiling report](docs/PERFORMANCE.md).

## Source, credits and licence

GTAIV-QoL project code is licensed under the **MIT licence**, copyright 2026 Avary Cooney; see [LICENSE](LICENSE). Vendored components and the modified FusionFix host retain their existing licences and notices. The build preserves those notices in the installer. The Paused artwork uses supplied Pricedown Black artwork by Typodermic; no font installation or font software redistribution is required. See [artwork credits](assets/README.md).

Developers: use the canonical branch's existing `build.bat` in an MSVC x86 developer prompt. CI runs the normal build, native and Python tests, updater UI checks and beta packaging. [BUNDLE.md](docs/BUNDLE.md) describes the current architecture; [NEXUS_DESCRIPTION.md](docs/NEXUS_DESCRIPTION.md) is the release-page draft.

Individual line controls, precise destination anchors, moving-target limitations and updater/pause-menu acceptance checks: [Road-line controls](docs/ROAD_LINE_CONTROLS.md).

Date hearts, live venue preferences and automatic vehicle-chase findings/acceptance: [date and chase investigation](docs/DATE_AND_CHASE_INVESTIGATION.md).

Pause → QoL → **Check for QoL Updates** (default Off) enables Deck background checks and staging; installation happens after exit. The first-time installer and manual fallback remain. See [in-game updates](docs/IN_GAME_UPDATES.md).
