# GTAIV-QoL — Steam Deck Beta Bundle

A guided-install bundle of seven quality-of-life components for GTA IV Complete Edition. Clearer navigation, a compact compass, usable cutscene pause and a handful of focused controls—managed from the pause menu, with no INI edits.

**Beta 0.1.0-beta.1, not 1.0.** User-confirmed Steam Deck play includes synchronized voiced-cutscene pause/resume and vehicle compass waypoints; broader acceptance and mod-combination testing are still needed.

## Included mods

- **Road Navigation Line:** a road-fitted native-GPS ribbon with Off, Flat and Twisted modes. Police escape guidance uses a narrower flashing red/blue ribbon alongside the normal route.
- **Turn Arrows:** directional chevrons, linked turns, destination cues and wrong-way/U-turn guidance. Includes automatic eligible-enemy guidance when no explicit route is selected and destination colour styling.
- **Map Autozoom:** opens the interactive pause map at 300%, leaving subsequent manual pan/zoom under your control.
- **Pause Cutscenes:** Esc or controller Start pauses supported voiced cutscenes, including dialogue, actors and camera, over a blurred Paused frame. Press again to resume; no time limit.
- **Cutscene Audio:** a pause-menu slider for the whole cutscene output mix, restoring gameplay gain afterward. It is not an isolated speech/dialogue control.
- **Compass HUD:** camera-relative map markers, centered distance, health/armour feedback and unread-message indication. Hold D-pad Left to select a focused waypoint on foot or in a vehicle.
- **Taxi FOV Slider:** set the rear-seat taxi interior view from 40 to 120 degrees.

## Requirements

GTA IV Complete Edition **1.2.0.59**, compatible ASI loader and ScriptHook, Microsoft X86 runtime, and supported **FusionFix 5.1.2 assets/menu host**. The guided installer checks dependencies and supplies the verified modified FusionFix menu host. Unsupported/custom hosts are not blindly replaced. Steam Deck/Linux Desktop Mode needs Python 3 and KDE or Zenity dialogs; missing dependencies require author-hosted downloads. Setup can run the verified Microsoft X86 installer inside GTA IV’s existing Proton prefix; setup detects Steam’s configured compatibility tool automatically and launch the game once from Steam first if its prefix is missing.

## Install and update

1. Close GTA IV. Extract the entire beta Deck ZIP into one folder, then run **Install-GTAIV-QoL.desktop** in Desktop Mode (Execute/Trust if prompted). Keep the supplied update ZIP beside it.
2. The GUI checks for a newer published public beta, or uses the verified bundled copy if unavailable, then finds GTA IV and guides one missing requirement at a time, with the checklist only at final review.
3. Use automatic setup, or open the dependency download pages and save the supported ZIP/EXE files to Downloads. Click **Retry / check downloaded file**. Setup places the checked files and runs the verified Microsoft X86 installer in the selected GTA IV Proton prefix. You do not place mod/dependency files or edit settings files by hand.
4. Once all checks pass, click **Install GTA IV QoL**. It installs all seven components and the integrated **Update GTA IV QoL** shortcut, with backups and retained pause-menu settings. No private-repository or log-upload credentials are needed.
5. Use that installed shortcut for future published beta checks; no separate updater download is needed. Pause → QoL and Audio control the components; Road Line **On (twisted) / Off** applies live, with separate Police Escape, Mission, Friend, Enemy and Custom Line On/Off switches (all start On).

Maintainer note before publication: this is a page draft, not a published beta. Clean public source and CI live on CryKitty/GTAIV-QoL main; the old archive stays private. Beta updates require a separately approved public GitHub beta release with GTAIV-QoL-update.zip attached. Until then the verified bundled archive installs and beta checks report no published update.

## Known issues

Rare uncontrolled shooting from a vehicle has been reported until game relaunch. Attribution is unconfirmed and other mods may be involved; the existing vehicle-hold guard is included, but this beta does not claim an auto-shoot fix.

Police avoidance is a proximity heuristic, not sight-line prediction. It does not model building occlusion, future/unmarked police or custom shortcuts. Compass search areas and mission-zone shapes are omitted. Road ribbons wait for validated depth/surface data; the recent recovery fix has renderer regression coverage, with the previously failing in-game scenario still awaiting confirmation. Audio-less pause and independent dialogue/gameplay voice volume are not implemented. Large audio boosts may distort.

Performance changes were measured in synthetic CPU stage benchmarks, not as in-game FPS gains. Native Windows compilation is tested; Steam Deck gameplay coverage does not prove all features/mod combinations on Windows.

GTAIV-QoL project code: **MIT**, copyright 2026 Avary Cooney. Included third-party components retain their existing notices/licences. See the archive's README, inventory and known-issues checklist for full details.
