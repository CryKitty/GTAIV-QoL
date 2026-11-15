# GTAIV-QoL — Windows and Steam Deck Bundle — 1.0.0

A guided-install bundle of seven quality-of-life components for GTA IV Complete Edition. Clearer navigation, a compact compass, usable cutscene pause and a handful of focused controls—managed from the pause menu, with no INI edits.

**1.0.0 — the first official release.** User-confirmed Steam Deck play includes synchronized voiced-cutscene pause/resume and vehicle compass waypoints; broader acceptance and mod-combination testing are still needed.

## Included mods

- **Road Navigation Line:** a road-fitted native-GPS ribbon with Off, Flat and Twisted modes. Police escape guidance uses a narrower flashing red/blue ribbon alongside the normal route.
- **Turn Arrows (Beta):** original Sleeping Dogs-inspired guidance, superseded by Road Line and Off by default; directional chevrons, linked turns, destination cues and wrong-way/U-turn guidance. Includes automatic eligible-enemy guidance when no explicit route is selected and destination colour styling.
- **Map Autozoom:** opens the interactive pause map at 300%, leaving subsequent manual pan/zoom under your control.
- **Pause Cutscenes:** Esc or controller Start pauses supported voiced cutscenes, including dialogue, actors and camera, over a blurred Paused frame. Press again to resume; no time limit.
- **Cutscene Audio:** a pause-menu slider for the whole cutscene output mix, restoring gameplay gain afterward. It is not an isolated speech/dialogue control.
- **Compass HUD:** camera-relative map markers, centered distance, health/armour feedback and unread-message indication. Hold D-pad Left to select a focused waypoint on foot or in a vehicle.
- **Taxi FOV Slider:** set the rear-seat taxi interior view from 40 to 120 degrees.

## Requirements

GTA IV Complete Edition **1.2.0.59**, compatible ASI loader and ScriptHook, Microsoft X86 runtime, and supported **FusionFix 5.1.2 assets/menu host**. The guided installer checks dependencies and supplies the verified modified FusionFix menu host. Unsupported/custom hosts are not blindly replaced. Steam Deck/Linux Desktop Mode needs Python 3 and KDE or Zenity dialogs; missing dependencies require author-hosted downloads. On Windows the verified Microsoft X86 installer runs natively. On Deck, setup runs it inside GTA IV’s existing Proton prefix, detects Steam’s configured compatibility tool automatically, and requires a first Steam launch if that prefix is missing.

Recommended optional companion: [Liberty Loadout – Weapon and Radio Wheels](https://www.nexusmods.com/gta4/mods/1226/) by Delusional94. It is not required by GTAIV-QoL and is not downloaded by its installer.

## Windows first install

Use GTAIV-QoL-1.0.0-Windows.zip: extract fully, close GTA IV and run Install-GTAIV-QoL.exe, keeping the update ZIP beside it. Python/Tk is bundled; no Python installation or Proton setup is required. The same file-based dependency checks/imports and final checklist apply. The Microsoft X86 dependency may request UAC; QoL itself runs per-user and needs a writable game folder. Use the real Update GTA IV QoL Desktop shortcut afterward; installer re-runs hand off to it. The executable is unsigned and may prompt SmartScreen. Native Windows compilation/loader tests pass; equivalent Windows gameplay coverage is not claimed.

## Steam Deck install and update

1. Close GTA IV. Extract the entire Deck ZIP into one folder, then run **Install-GTAIV-QoL.desktop** in Desktop Mode (Execute/Trust if prompted). Keep the supplied update ZIP beside it.
2. The GUI checks for a newer published public version, or uses the verified bundled copy if unavailable, then finds GTA IV and guides one missing requirement at a time, with the checklist only at final review.
3. Use automatic setup, or open the dependency download pages and save the supported ZIP/EXE files to Downloads. Click **Retry / check downloaded file**. Setup places the checked files and runs the verified Microsoft X86 installer in the selected GTA IV Proton prefix. You do not place mod/dependency files or edit settings files by hand.
4. Once all checks pass, click **Install GTA IV QoL**. It installs all seven components and the integrated **Update GTA IV QoL** shortcut, with backups and retained pause-menu settings. No private-repository or log-upload credentials are needed.
5. Enable Pause → QoL → Check for QoL Updates for future Deck checks/staging (Off by default); apply happens after exiting the game. The installed updater shortcut remains a manual fallback; no separate updater download is needed. Pause → QoL and Audio control the components; Road Line **On (twisted) / Off** applies live, with separate Police Escape, Mission, Friend, Enemy and Custom Line On/Off switches (Enemy starts Off and is labelled Beta; the other line types start On).

Maintainer note: this is the 1.0.0 page draft. The full 1.0.0 GitHub release is published; its additional Windows installer asset requires independent verification and merge of the installer PR; the existing 0.1.0-beta.1 prerelease stays unchanged. Nexus publication also requires separate approval. The verified bundled archive remains usable without private credentials.

## Known issues

Rare uncontrolled shooting from a vehicle has been reported until game relaunch. Attribution is unconfirmed and other mods may be involved; the existing vehicle-hold guard is included, but this release does not claim an auto-shoot fix.

Police avoidance is a proximity heuristic, not sight-line prediction. It does not model building occlusion, future/unmarked police or custom shortcuts. Compass search areas and mission-zone shapes are omitted. Road ribbons wait for validated depth/surface data; the recent recovery fix has renderer regression coverage, with the previously failing in-game scenario still awaiting confirmation. Audio-less pause and independent dialogue/gameplay voice volume are not implemented. Large audio boosts may distort.

Performance changes were measured in synthetic CPU stage benchmarks, not as in-game FPS gains. Native Windows compilation is tested; Steam Deck gameplay coverage does not prove all features/mod combinations on Windows.

GTAIV-QoL project code: **MIT**, copyright 2026 Avary Cooney. Included third-party components retain their existing notices/licences. See the archive's README, inventory and known-issues checklist for full details.
