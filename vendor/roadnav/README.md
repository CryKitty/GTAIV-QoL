# Bundled RoadNav source

Pinned from CryKitty/GTAIV-Road-Nav commit 6b9b66f540d8139cb1303087cf282c6b89bf3abd.

Adaptations: src/roadnav.cpp uses the shared QoL settings client, hides cues when disabled, and is initialized by the companion ASI instead of its own DllMain. src/d3d_hook_service.hpp uses the authentic ScriptHook service header already carried by this bundle. Build identity is generated from the QoL commit. Rendering, route reading and cue geometry retain the upstream implementation. SDK notices and upstream third-party notices are retained.

One companion binary contains RoadNav and cutscene pause; the installer disables standalone RoadNav ASIs to prevent duplicate rendering.

Bundle navigation update: camera-facing forward chevrons, GPS-path/ramp sampling, 15-degree shallow bends and motion-confirmed U-turn cues. Portable behavioral tests: tests/roadnav_navigation.cpp. See docs/BUNDLE.md for limits and in-game testing.

Current behavior after build71: upward-arrow conversion removed, 40-degree normal-turn threshold restored, speed-aware preview shared by selection and renderer (70–320m plus outer fade). GPS sampling, distinct linked turns, U-turns and ten-foot approach fade remain.

The companion also runs the optional Compass HUD via game-thread native HUD calls. Its source is compass_hud.hpp with portable policy in src/compass.hpp. Read-only radar filename-table layout reference: https://github.com/ClonkAndre/IV-SDK/blob/main/include/CRadar.h . Health-offset and entity-blip position reference: https://github.com/HazardX/gta4_scripthookdotnet/tree/master/ScriptHookDotNet . No upstream code or game textures are bundled by this addition.
