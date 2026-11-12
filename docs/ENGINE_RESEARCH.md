# Experimental map initialization research

Evidence inspected 2026-10-30:

- FusionFix commit `5a961b66960c0fe33f66f9e61c66f769bf029d66`, [source/fixes.ixx](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/blob/5a961b66960c0fe33f66f9e61c66f769bf029d66/source/fixes.ixx): its map crosshair fix identifies `CFrontEnd__GetWidgetValue` as `Vector2* (__cdecl*)(Vector2*, int)`. Known prologues are `8B 44 24 ? 56 8B 35 ? ? ? ? 85 F6` and `8B 0D ? ? ? ? 85 C9 8B 44 24 ? 74`. This experiment scans both and requires exactly one match inside executable PE sections. These are upstream signatures, not signatures extracted from a local 1.2.0.59 executable.
- Same commit, [frontend_pc.dat](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/blob/5a961b66960c0fe33f66f9e61c66f769bf029d66/data/update/common/data/frontend_pc.dat): design row 63 is `MAP_map_border_init 800 0`, row 64 is border length 1600, row 65 is scroll/zoom speed 2/4. Counting rows is corroborated by row 166 being FE_MAP_CURSOR_THICKNESS, the exact ID used by the upstream crosshair hook. HD and lower-resolution tables both define init as 800.
- [IV-SDK CFrontEnd](https://github.com/Zolika1351/iv-sdk/blob/main/include/CFrontEnd.h) exposes cursor and waypoint operations but no verified CE initial zoom operation. Its legacy addresses were not imported. GTA SA CMenuManager offsets are irrelevant.
- Road Nav's local repository build.bat and Windows Actions workflow use MSVC x86. The same compiler environment is reused; ScriptHook is unnecessary for this hook.

## Hypothesis implemented

`MAP_map_border_init` is an initial visible-world range. A range of 800/1.5 = 533.333 should produce 150% scale. A getter detour delegates to the original and modifies only this one return value, and only when it is exactly `(800,0)`. It never writes center coordinates. Repeated reads start from the game's own design value, so there is no cumulative multiplier. Normal scroll and bounds settings remain untouched.

## Unverified assumptions and acceptance criteria

No real GTAIV.exe or running game was available for disassembly/runtime verification. The name/data support this hypothesis but do not prove reciprocal scaling, interactive-only use, reset timing, or compatibility with FusionFix/Proton. A call may occur for preview rendering, initialization caching, or a different path. A successful build or log is not enough to assert the feature works. User testing must confirm interactive-only 1.5x zoom, unchanged centering/preview/minimap, manual controls, waypoints, and repeatable reopen behavior. If any fails, revise the hook based on evidence rather than treating this as a completed gameplay feature.

Exact file version 1.2.0.59 is required. MinHook handles instruction relocation and installation thread coordination. All initialization occurs through the ASI loader export, outside DllMain. Logging is opt-in and bounded to 32 widget reads, with no automatic uploads.

## User runtime evidence and data route (0.3.0)

The 0.2.3 probe at https://github.com/CryKitty/GTAIV-Road-Nav/issues/2#issuecomment-6539548342 captured getter VA 0x008B9020. Its body loads the table pointer through absolute slot 0x011609C8 and copies `[table + id*8]` and `[table + id*8 + 4]` to the supplied output pair. This confirms table stride/indexing on the user's build. The full 48-byte function is checked in the new implementation with only that absolute operand masked. The operand is decoded; neither captured address is hard-coded.

0.2.2's pass-through detour activated then reproduced the early startup crash, while its disabled mode and the 0.2.3 read-only probe reached gameplay. The detour failure's exact cause remains unknown (code-integrity interactions, patch timing, and hook compatibility have not been distinguished). Do not label a specific cause proven.

0.3.0 reads the active table pointer and validates map design rows before changing the initial-range float in writable data, with no executable code changes. Getter indexing is now observed evidence; reciprocal scaling, initialization timing, preview exclusivity and resetting remain gameplay hypotheses. The worker rechecks pointer stability but cannot eliminate a concurrent table-free/reload race without engine-thread integration.

## 0.3.0 result and 0.3.1 investigation

https://github.com/CryKitty/GTAIV-Road-Nav/issues/2#issuecomment-6539617430 contains the verified 0.3.0 update: table pointer 0x117D4660, rows `(800,0)`, `(1600,0)`, `(2,4)`, then range readback 533.333. The user reported no visible zoom change and no crashes. This proves successful data modification at that time, not persistent use or correct choice of setting.

0.3.1 monitors pointer/value changes and reapplies to fully validated stock rows for ten minutes (at most sixteen writes). It additionally captures candidate direct callers near a push of ID63, rather than the first sixteen unrelated getter callers from 0.2.3. Offline disassembly is needed before treating those byte matches as map initialization sites. Table reset/replacement and ineffective setting interpretation remain competing explanations.

## 0.3.1 result and live-state investigation

Direct REST comments returned https://github.com/CryKitty/GTAIV-Road-Nav/issues/2#issuecomment-6539797865 while the normalized comments connector was still returning an older snapshot. Prefer a direct comments collection read when checking fresh sessions.

The log recorded table resets at about 15.0s and 26.3s, with verified reapplication each time; the user still saw no change. The sole captured push-63 caller is RVA 0x4BD330. Disassembly from the confirmed function start RVA 0x4BD300 shows it reads widget 63, adds a constant, reads widget 64, then loads a float from VA 0x011609CC and compares it against those bounds. It branches on byte VA 0x011609F7. This does not establish initialization semantics. Both addresses are decoded from that instruction sequence in 0.3.2, not hard-coded into the trace.

0.3.2 removes game writes and gathers candidate stores plus input-correlated runtime samples. It starts a fresh bounded log. The test intentionally includes manual zoom and close/reopen to establish the meaning of the candidate state before further functional changes.

## 0.3.2 runtime evidence and 0.4.0 entry latch

https://github.com/CryKitty/GTAIV-Road-Nav/issues/2#issuecomment-6544308437 correlates the candidate range with the requested input: current range fell from 6540 to 1175 while zooming in, rose to about 3986 while zooming out, and switched to 190 with state byte 0 on closure. Reopening restored 6540 with state byte 1. Earlier transitions show other frontend initialization activity, so state-byte semantics remain an inference to verify visually.

Captured initializer bytes set both VA 0x011609D0 and 0x011609CC to float 6540 (0x45CC6000). Captured runtime writers read the companion range and copy it back to the observed current range. 0.4.0 therefore decodes that unique paired initializer and updates both adjacent floats once per observed active entry, only from the stock pair and only within a short entry window. Reader/flag addresses remain derived from the previously captured reader, not hard-coded. Manual range changes consume/skip the opening; a latch prevents persistent forcing. The companion's full semantics and centering/preview compatibility are not yet proven.

## 0.4.0 feedback: preview trigger is incorrect

https://github.com/CryKitty/GTAIV-Road-Nav/issues/2#issuecomment-6544772205 confirms runtime pair writes 6540→4360. The user observed those writes manifested during the pause preview, followed by automatic zoom-out before interactive-map acceptance. Thus the byte at observed VA 0x011609F7 must not be labeled interactive-entry state. The manual controls were reported normal. Correlation with manual zoom establishes range relevance but not trigger semantics.

0.4.1 is read-only: it captures writers of that byte and nearby frontend control state during a timed preview/accept/back/accept sequence. The desired event remains the same acceptance/auto-center-on-player transition requested by the user. No inversion of the old byte is assumed correct, and no centering coordinates are written.

## 0.4.1 outcome and acceptance candidate

https://github.com/CryKitty/GTAIV-Road-Nav/issues/2#issuecomment-6544939806 recorded the timed preview/interactive sequence, but the original byte did not distinguish the requested transitions. The prior sampled neighborhood even overlaps the FE_CANCEL string, so it must not be described as a verified control-state structure.

Disassembling the captured writer branch at VA 0x005A3EEE reveals writes to adjacent bytes 0x01160C35/0x01160C36, calls to 0x0059F940, and a write of 0x32 to 0x01160C40. The routine/flags remain candidates, not confirmed centering/interaction semantics. 0.4.2 uniquely recognizes the branch opcode layout, decodes those addresses, reads the preparation routine, and samples a small neighborhood around those candidate flags. It performs no writes. Do not invert the old flag or treat generic frontend activation as map acceptance without confirming the new sequence.

## 0.4.2 outcome

https://github.com/CryKitty/GTAIV-Road-Nav/issues/2#issuecomment-6545050900 captured the candidate branch/preparation routine. The first flag (0x1160C35) did not clearly distinguish the timed interactive acceptances. The preparation routine initializes general frontend/menu state and contains additional branches beyond the earlier 512-byte capture. Do not label it proven auto-centering. The live range neighborhood was not yet sampled far enough back to include possible focus state near 0x11609D4–0x11609F7.

0.4.3 remains read-only and expands coverage once: 4096 bytes of the observed generic preparation routine, 3072 bytes around the unique paired-range initializer, plus change samples near the runtime ranges and candidate menu flags. Code is losslessly packed (documented LZSS 12-bit-distance/4-bit-length format) and base64 encoded to fit the existing bounded uploader. Offline round-trip tests cover random/repeated data. This enables disassembly of the actual map setup/acceptance code instead of assuming another isolated flag's meaning.

## 0.4.3 evidence and 0.5.0 implementation

https://github.com/CryKitty/GTAIV-Road-Nav/issues/2#issuecomment-6545228328 supplied lossless code blobs and full-state samples. Offline decoding/disassembly showed native zoom code at observed VA 0x008B9880 initializes six stack constants (6540, 5263, 3986, 2709, 1175, 650), loads an index through observed slot 0x011609D8 and rewrites target 0x011609D0 from that array each update. Thus direct target/current writes in 0.4.0 could not persist.

The state samples identify the DWORD at observed 0x011609E8 (map neighborhood byte offset 20) transitioning 3→5 at 42.05s, 51.75s and 63.3s, and 5→3 at 47.85s, 56.65s and 65.8s, correlated with the supplied preview/accept/back sequence. Pause-active byte 0x01160C34 was 1 during these cycles. The previously used 0x011609F7 was generic frontend activity, not interactive acceptance.

0.5.0 decodes the native stop initializer and its index/target instructions. Relative state/index layouts are locked to this CE capture and guarded by the unique instruction sequence and trace-derived adjacency. It patches one interior stop's four-byte immediate after interactive focus 5, retaining 100% and the closest endpoint, then selects that step after a 100ms settling interval. This uses the game's native animation and leaves centering untouched. No function detour or direct call of game routines is used. Selection is latched per interactive entry; manual controls are not continuously forced. Code-integrity compatibility remains unverified for the new post-startup constant patch. Gameplay tests must confirm the candidate pause/focus interpretation and requested visible behavior.

The user explicitly requested functional changes rather than further probe-only passes. All additional diagnostics in 0.5.0 accompany normal feature use; no repeated timed capture is required.
