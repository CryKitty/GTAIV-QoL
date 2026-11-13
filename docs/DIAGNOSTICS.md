# Current crash evidence retrieval

The reported date-entry crash has **no accessible uploaded capture**. The known
private diagnostics issues are full; successful delivery must not be assumed.
The code change removes the unverified ScriptHook global-accessor call and unused
date-start observer. Venue icons deliberately stay white until a safe provider is
verified. Without a crash record the exact faulting instruction is unconfirmed.

Pull these exact files **if present beside GTAIV.exe** (the uploader collection in
`tools/steam-deck/enable-qol-logs.py`):

- `RoadNav-arrow.log` — includes any QoL passive fault record (registers/module/offset).
- `RoadNav.log`
- `GTAIV-QoL.log`
- `GTAIV-QoL-diagnostics.log`
- `GTAIV-QoL-pause.log`
- `GTAIV-QoL-audio-layout.log`
- `GTAIV-QoL-police.log`
- `ScriptHook.log`
- `LibertyLoadout.log`
- `FusionFix.log`

Do not substitute launcher/Social Club logs for a missing QoL fault record. This
mod's passive recorder appends to RoadNav-arrow.log; it does **not** create a .dmp.
If a game/other-mod crash dump exists, preserve it separately. Local queued captures
may remain under `~/.local/share/roadnav-uploader/pending/` and confirmed archived
originals under `~/.local/share/roadnav-uploader/delivered/`; their existence is not
proof that this crash was captured or uploaded. Supply available files privately
rather than posting raw diagnostics to the public source PR. No save files are needed.

Retest after updater delivery: initiate a date, pick her up, let all venue icons
load, pan/hover/set a waypoint and drive between venues. Hearts should remain
visible, venues should be white, and loading them should not crash. Compass HUD
remains the pause-menu control. If it recurs, preserve the files before another
launch overwrites/truncates logs. Do not rely on the full issue #10 receiving them.

Historical capture instructions below describe an earlier diagnostic build.

# Broad cutscene and audio capture

This build is for collecting evidence for cutscene pause and future voice-volume controls. It does not pause cutscenes or implement voice sliders. The existing 300% map zoom remains enabled.

## Steam Deck setup

1. Close GTA IV. Extract the entire diagnostic bundle into the GTAIV.exe folder, replacing GTAIV-QoL.asi and GTAIV-QoL.ini.
2. In Desktop Mode, run the included Upload-QoL-Log.desktop once (choose Execute/Trust if KDE asks). It updates the existing RoadNav uploader to collect both QoL logs in full, split into numbered upload parts. It uses the existing private repository authorization and Steam launch options. No new token or INI editing is needed. If it reports an unfamiliar uploader, stop and report that message.
3. Start GTA IV normally. Logging starts automatically on the supported CE 1.2.0.59 executable. No keyboard shortcut is required.

## One useful test session

- Spend about 30 seconds in ordinary gameplay with ambient voices, then trigger a phone call or scripted conversation if convenient.
- Play a voiced cutscene for 30–60 seconds. Try the game's ordinary Esc/Start once, then return if a menu opens. Do not use F9; the failed minimizing probe is gone.
- Open the normal audio menu. Move the existing music and SFX sliders separately from their starting positions to low/high values, spending about five seconds at each value. Restore your preferred settings. This provides comparison points for audio-control research; separate voice sliders do not exist yet.
- If convenient, include radio audio, another conversation, and a cutscene ending or being skipped. There is no need to replay a whole mission solely for logging.
- Optional: F8 records a timestamped marker only. Deck users can ignore it; automatic state transitions and heartbeat records require no keyboard.
- Exit normally. The established uploader queues both logs automatically, retrying offline deliveries. The included helper can also request a manual retry after the game is closed. Tell Codex the session is uploaded and describe the sequence you tried.

## What is recorded

GTAIV-QoL.log retains the ordinary mod log. GTAIV-QoL-diagnostics.log records a format/version header, loaded module names/base addresses/sizes (no module paths), main image metadata, unique signature match counts and bounded nearby code bytes. Runtime observations include discovered cutscene state, user/script pause flags, time-scale/system-time/audio-offset fields, foreground state, and a bounded 256-byte cutscene audio entity snapshot on change. Audio-related embedded names and direct code references are collected for speech, voice, dialogue, conversations, volume, radio and cutscenes. Up to eight writable global windows directly referenced by those code candidates are sampled as raw, unclassified data. These candidates may be unrelated; they are not asserted to be mixer channels or confirmed voice-volume values.

Reads use ReadProcessMemory, require addresses within the main image, and reject missing/ambiguous signatures independently. The logger does not call native game functions, dereference captured pointer values, install hooks, write game memory, change focus, record audio, or inspect unrelated processes. The separate map zoom feature retains its existing behavior. Missing signatures are reported so the next build can be targeted from evidence rather than guessed addresses.

Capture defaults: 500 ms sampling, change records plus five-second heartbeats, 30-minute maximum, and a 1 MiB byte budget with a small final-record allowance. Capture stops when either limit is reached; it does not wrap away the beginning. Restarting the game begins a fresh capture, so exit and let the uploader snapshot it before another session. BroadCapture=0 disables this capture independently of map zoom. SampleIntervalMs accepts 100–5000; CaptureSeconds accepts 30–3600. The shipped INI already has the intended settings.

The updated uploader preserves complete QoL capture contents within the logger's bound, rather than taking only a head/tail excerpt. It queues safe-size numbered comments with a common session ID and the QoL ASI hash. Original logs remain on the Deck. Existing pending delivery, private destination checks and authentication are retained. Queued parts may arrive in a different order; their part numbers reconstruct the original order.
