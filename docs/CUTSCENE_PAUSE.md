# Synchronized cutscene pause

Esc or controller Start toggles pause during an active voiced cutscene. F9 and L3+R3 are not bound. Held buttons do not repeat or trigger later when a cutscene begins. Normal gameplay pause-menu controls remain unchanged.

Pause has no time limit and no automatic recovery countdown. Explicit resume, focus/menu change, scene exit and script shutdown clean up an owned pause. The old timed recovery worker has been removed. Audio-less cutscenes remain outside the supported path.

## Audio and timeline

The plugin captures the game's XAudio2 engines through legacy COM or modern XAudio2Create exports. Modern export detours include middleware callers. It also captures DirectSound devices and secondary buffers through DirectSoundCreate/DirectSoundCreate8. The user confirmed that this combination stops dialogue, camera and actors, and resumes them in sync on Steam Deck.

XAudio2 StopEngine preserves voice buffers and cursors; captured interfaces defer game StartEngine calls while the mod owns their stopped state. DirectSound Stop/Play preserves secondary-buffer playback cursors and loop settings. Primary buffers are excluded because stopping them can reset their position. Play/status guards prevent premature restarts or false stream completion; a game Stop cancels a pending mod resume. Captured references are retained for process lifetime, bounded to four XAudio2 engines, eight DirectSound devices and 128 secondary buffers. Overflow refuses new pause attempts. Do not hot-unload the ASIs.

A unique source-derived audCutsceneAudioEntity::GetPlayTimeMs hook holds the exact cutscene entity's audio timeline while paused. Unrelated entities pass through unchanged. Resume compensates only for the measured bookkeeping-clock movement, avoiding a visual jump; a new audio section resets that correction. ScriptHook invokes the game/conversation pause natives on the game thread. Missing handlers, ambiguous signatures or unsupported executables refuse pause. Cinematic/audio clock checks still request cleanup if the coordinated hold fails.

## Overlay

ScriptHook's SDK D3DHook service is rebound at each script start. The overlay captures the frame once, applies two horizontal/vertical Gaussian blur passes at quarter resolution, dims it slightly and draws the fixed **Paused** artwork at the bottom right. It uses the supplied Pricedown Black font by Typodermic, rendered once into a complete static word; the font software is not bundled. No font installation or runtime shader compiler is required.

Render state, render targets and depth surfaces are restored. Device loss/reset releases and rebuilds resources. A thread-local re-entry guard prevents ScriptHook callback recursion from locking the render thread. Resume, focus/menu cleanup and script stop dismiss the overlay. CutscenePause Overlay=0 disables only presentation; its default is 1.

## Testing and references

CI checks toggle/transition behavior without a timer, repeated timeline holds and resume compensation, both audio backends, real x86 detours, render-state restoration, recursive render callbacks, overlay removal and device reset. With HAL/reference graphics it also checks the bottom-right label pixels and publishes GTAIV-QoL-overlay-preview. In-game behavior still requires gameplay testing after changes.

- Microsoft XAudio2 StopEngine: https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2-stopengine
- Microsoft XAudio2 versions: https://learn.microsoft.com/en-us/windows/win32/xaudio2/xaudio2-versions
- Microsoft DirectSound secondary-buffer Stop: https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee418151(v=vs.85)
- Microsoft DirectSound Play: https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee418074(v=vs.85)
- SDK callbacks: vendor/scripthook/Services/D3DHook.h, Aru's SDK preserved by violetfrost/gtaiv-scripthook-sdk.
- Source-derived state/timeline signatures: FusionFix comvars.ixx and cutscenecam.ixx at 87db4f6f43c97386e8cefa0cadd92d1933e04fc4.
