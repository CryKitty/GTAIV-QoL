# Beta known issues and acceptance

- Rare, unconfirmed vehicle auto-shoot: occasionally Niko fires indefinitely from a car until relaunch. Other installed mods may be involved. PR #3's hold guard/diagnostics remain; no root cause or fix is claimed without the affected session's logs.
- Road-line recovery after PR #4 is covered by real D3D9 pixel tests, but the previously affected game session has not been revalidated here. Missing/unsupported scene-depth or surface data deliberately suppresses ribbons. On/Off modes cannot bypass that safety gate.
- Police escape is a visible-marker proximity heuristic, not police sight-line prediction. Building occlusion, future movement and unmarked police are not modeled. Search areas and mission-zone shapes are omitted from the compass.
- Native GPS geometry does not guarantee exact lane placement or shortcuts outside its road graph. Mission-specific compass marker behavior needs broader testing.
- Voiced cutscene pause/resume and vehicle compass waypoints have user-confirmed Steam Deck gameplay coverage. Audio-less pause and independent gameplay/dialogue volume are not implemented. Cutscene Audio changes the whole output mix, with possible distortion at large boosts.
- The guided installer targets Steam Deck/Linux. Native Windows build/loader tests do not substitute for broad Windows gameplay testing. Existing unknown/custom FusionFix hosts are rejected safely.

## Beta acceptance using only the installer/updater and pause menu

1. With GTA IV closed, run the extracted Nexus guided installer. Check a first install without QoL GitHub credentials and an upgrade retaining all pause-menu settings/backups. Downloaded missing dependencies must be placed by Retry/automatic setup, with runtime and Proton-loader re-checks; failed installation must not report success.
2. In a car, set a normal map waypoint. Compare Road Line modes 1 Flat and 2 Twisted, then 0 Off and back On; verify the whole leg past a bend. Check roads/crests and foreground occlusion, pause/resume and save reload.
3. Test independent Turn Arrows, shallow/linked turns, wrong-way cues and manual/mission routes. During a police escape check the narrower red/blue ribbon alongside a primary route. This is an acceptance check, not a claim that those scenarios have all passed in-game.
4. Switch Compass HUD off/on. Check minimap restoration, wanted stars, phone/unread messages, activity markers and a fresh D-pad Left waypoint hold on foot/in a car. Enter/exit a car during a hold; it must cancel until release.
5. Open the interactive map and verify autozoom, then manual pan/zoom. Pause/resume a voiced cutscene with Esc/Start; compare Cutscene Audio at 100% and a modest increase, then normal gameplay gain. Check the taxi rear-seat FOV slider.
6. Upgrade using the next archive (or existing development updater). Confirm the selected menu settings survive. Report rare auto-shoot separately with mod list, event time and a complete session capture; do not assume its owner.
