# Bundled component inventory

Checked against current initialization paths, settings, native build and updater packaging. There are **seven gameplay components**; diagnostic/research source files are not additional shipped mods.

| Component | Active implementation / wiring | Installed payload |
| --- | --- | --- |
| Road Navigation Line | `vendor/roadnav/src/roadnav.cpp`, `road_line.hpp`, `road_line_renderer.hpp`, `road_surface.hpp`; `QOL_GPS_LINE`, `QOL_GPS_VIEW_BEND` | GTAIV-QoL-Pause.asi |
| Turn Arrows | `vendor/roadnav/src/turn_cue.hpp`, `turn_renderer.hpp`, `roadnav.cpp`; `QOL_ROADNAV` | GTAIV-QoL-Pause.asi |
| Map Autozoom | `src/main.cpp` AutoZoom/Initialize; `QOL_MAP_AUTOZOOM` | GTAIV-QoL.asi |
| Pause Cutscenes | `src/cutscene_pause.cpp`, pause session/timeline/audio capture and overlay code; `QOL_CUTSCENE_PAUSE` | GTAIV-QoL-Pause.asi |
| Cutscene Audio | `src/cutscene_output.hpp`, `cutscene_mix.hpp`, game mastering-output capture from `cutscene_pause.cpp`; `QOL_CUTSCENE_VOICE` | GTAIV-QoL-Pause.asi |
| Compass HUD | `vendor/roadnav/src/compass_hud.hpp`, controller/input, minimap layout/ring, notice renderer; `QOL_COMPASS` | GTAIV-QoL-Pause.asi |
| Taxi FOV Slider | `vendor/roadnav/src/navigation_awareness.hpp` taxiTick/configureTaxi; `QOL_TAXI_FOV` | GTAIV-QoL-Pause.asi |

Road-line/turn-guidance subfeatures include manual/mission routes, automatic eligible-enemy chase routing, police escape planning/retries, destination colours, surface fitting and scene-depth occlusion. The compass includes social/activity markers, hostile/friendly/objective styling, hold-to-waypoint selection, arrival feedback, health/armour damage feedback and unread-message indication. These share the two ASIs; none is a separately omitted plugin.

`GameplayPercent` is reserved, not an implemented gameplay-voice mod. Legacy voice metadata research remains in source/tests but initialization explicitly removes that processing in favor of whole cutscene output gain. No ScriptHookDotNet gameplay component is shipped. RoadNav is linked statically; a duplicate standalone RoadNav.asi is backed up/removed by the installer.

The updater payload has both ASIs, shared settings, the verified installer helper and the persistent updater. The helper embeds the modified FusionFix menu host and its existing notices. The Nexus archive carries that exact payload plus the guided dependency/bootstrap launcher, README, changelog, inventory, known issues and MIT licence. Dependencies such as ASI loader/ScriptHook/FusionFix assets are checked/downloaded from their authors by the existing guide; they are not silently dropped or misrepresented as extra bundle mods.
