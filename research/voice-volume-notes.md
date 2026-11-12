# Vocal slider investigation

Stable baseline: branch feature/cutscene-pause, build #53, commit 4bf7082777c06c45a42f54b293ff95dea5aa2773. User confirmed full cutscene audio/visual pause, overlay, and updater. Keep new features in CryKitty/GTAIV-QoL and deliver through the existing updater; do not replace the working pause backend with global volume changes.

Confirmed scope: two independent sliders in the existing game Audio settings: Gameplay Voices and Cutscene Voices. Gameplay volume is already acceptable; the cutscene control must support amplification above the stock level. Keep gameplay default at 100%. Do not label a full cutscene-mix control as voice-only. Referenced ChatGPT thread exposes only five recent user turns and placeholder assistant content; no original vocal-slider specification is available there.

Primary source findings:
- Parik27/IV.EFLC.Rainbomizer lib/audEngine.hh/.cc defines audSpeechSound type 4, audStreamingSound type 8, category-hash metadata flag bit 13 and variable-field category lookup. src/audio.cc distinguishes the cutscenes category for streamed sounds, and hooks audSpeechSound::SetSpeechSound separately from scripted audio metadata. This is evidence of identifiable sound types/categories, not a verified volume setter or proof that voice is isolated in cutscene assets.
- Its lib/audEngine.cc locates the engine instance and metadata manager using CE-compatible signature 53 55 8B E9 68 ? ? ? ? B9 ? ? ? ? E8. Do not reuse guessed addresses or infer a gain field from unknown metadata members.
- Zolika1351/iv-sdk CMenuManager lacks documented CE vocal gain fields; old addresses are not usable for the current CE executable without independent discovery.
- FusionFix source/settings.ixx has a current ASI settings/menu host and native Audio menu integration. external/FusionFixUtils is a submodule; a stable plugin API/version and the user's installed FusionFix capabilities must be established before relying on it. Avoid patching the same native menu code as FusionFix independently.
- Existing native API lists show speech selection/playback controls but no general separate dialogue-volume native.
- Low-level DirectSound buffer/master XAudio2 controls operate on already-mixed output in the proven pause setup. Applying gain there cannot automatically be represented as vocals-only.

The subsequent bundle implementation adds native settings and an experimental runtime metadata gain path; see docs/BUNDLE.md for current behavior and limitations.


Implementation prerequisite: the game-side dialogue gain ABI and direct Audio-menu registration are not established. FusionFix current source exposes other plugin settings through its Plugins page, not directly in Audio; its C++ AddSlider is internal, not a published external C ABI. ASI settings marker detection is only a compatibility hint. Do not promise native Audio placement based on that marker alone.

A read-only updater inventory now reports category trees, category/type histograms, up to 64 speech/cutscene metadata prefixes per file, and FusionFix identity/settings marker. It handles base/EP1/EP2 config paths case-insensitively and rejects links, ambiguous paths, malformed indexes and oversized inputs. The existing upload queue carries the resulting GTAIV-QoL-audio-layout.log. No executable bytes or audio samples are uploaded; no game launch is required. This helper update does not implement sliders or alter runtime sound/pause behavior.

Additional primary source: Funkapppeee/liberty crates/rage/src/audio.rs documents DAT15 named-object indexes, optional sound-header volume at bit 0 in signed hundredths of dB, category hash at bit 13, and category child hashes. Treat these as source-derived metadata interpretation to check against the installed config, not as a verified runtime setter. ReIV's audio headers are mostly unverified RTTI inventories for 1.0.7.0; they supply no CE gain implementation. DK22Pac/plugin-sdk documents audSoundInitParams.Volume at 0x3c, but it does not establish the CE initialization ABI or independent streamed cutscene voice control.


v55 upload received: base categories parsed (220 objects); base sounds 20,940, TLAD sounds 2,290, TBoGT sounds 3,557; no invalid sound headers. Cutscene sounds include separate *_CENTRE, *_LEFT and *_RIGHT leaves across campaigns. CUTSCENES category 7F01B626 contains streamed roots, with uncategorized simple children. Gameplay speech templates are sound type 4. This supports a centre-track gain experiment; it does not establish that every centre track contains dialogue exclusively. Installed FusionFix lacks AsiSettings_GetApi and hashes to 14f6ac879bcc7df5fb18ce7b410bc436e16675b0dad640378dcb1e6dd3be98b3.

Native menu solution uses the existing internal AddToggle/AddSlider implementation from pinned official FusionFix 5.1.2 source, through a minimal source patch. No IVMenuAPI is installed; its author documents incompatibility with stock FusionFix. The modified FF binary and source/notice are delivered through the same authenticated updater, with exact prior FF identity checks and rollback. Runtime gains return bounded immutable metadata copies only when full body bytes match installed DAT metadata. Disable/100%/unknown records return the original pointer. Gain changes take effect for newly created sounds.
