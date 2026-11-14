> Later b76 evidence identifies `DoesCamExist(scripted)` on an unusable draw output as the failing operation. See [the confirmed b76 investigation](B76_SCRIPT_CAMERA_HANDLE_CRASH.md). The analysis below records the evidence available for PR #20.

# b72 first-driving-query crash

## Evidence and source-level isolation

Baseline main is 16304a6a6523212c7dad988d1a86f206b18d7adc (b72). The user confirms that the merged PR #18 hotfix loads successfully. Five supplied session files were inspected locally; no private log contents/paths or capture payloads are committed.

The complete supplied arrow session records renderer attachment long before car entry. On entering the vehicle, escape candidate selection and GPS handoff return, followed by background planning. The next suppression record explicitly has **no committed escape snapshot, no primary points and no fitted escape surface**. Only after that does driving-camera resolution complete. The core stage log enters `camera query begin` without a finish, and the passive recorder observes a read AV in GTAIV.exe at RVA 0x5B6A2A, target 0x16C. Duplicate identical recorder entries are not independent crashes. The user reports the same RVA in b64 and none in the tested healthy b56 sessions; that is corroboration, not proof of a specific native.

This rules out a newly committed escape ribbon as the demonstrated trigger: none existed at the last snapshot, and the first camera query batch did not return. It also contradicts a first-renderer-attachment explanation. Handoff had returned earlier; the trace does not demonstrate a GPS/camera race. On the ScriptHook thread the source order is compass/camera tick, escape update, then render-snapshot submission. The D3D renderer consumes retained snapshots separately.

Source comparison: b56 guarded the camera block with a child-camera/propagation condition. PR #17 removed that prerequisite and introduced scripted-camera queries, making the global interpolation and other queries reachable more broadly. PR #18 waits for a usable world and basic driving conditions, but “is driver” still does not establish completed seating or a valid gameplay camera. PR #19 changes ribbon presentation, not this query block. A deterministic state test shows b72's basic driver gate admitting a transition-like state without seating/camera proof, while the new entry gate refuses it.

**Root-cause scope:** an unsafe newly reachable camera-query path, with a concrete missing camera-lifetime/readiness guard. The existing bracket contains global interpolation, scripted-camera lookup/validation and follow-offset read. It cannot identify which of those individual native calls faulted. No runtime executable bisect, crash dump, CE disassembly or native-specific fault reproduction is available here. This PR must not claim that IS_CAM_INTERPOLATING alone is proved guilty, or that Deck acceptance has passed.

## RVA / SDK investigation

The vendored Aru SDK and public [IV-SDK camera layout](https://github.com/Zolika1351/iv-sdk/blob/dddbd3f54b146bdb3a9852809e48e5cb169af3cd/include/CCamera.h) distinguish the game, final, on-foot and vehicle camera objects. [CCam](https://github.com/Zolika1351/iv-sdk/blob/dddbd3f54b146bdb3a9852809e48e5cb169af3cd/include/CCam.h) also exposes a separate follow-vehicle type. Thus an entity's driver flag is not a camera-object validity test. Those public addresses target other game versions and do **not** establish a name for CE RVA 0x5B6A2A. No named function is asserted from a guessed offset/near-null field. Further identification would require a matching executable/symbol map or a crash dump with the exact handler/substep.

The native signatures agree on output-pointer scripted-camera lookup, boolean follow-offset query, and a handle-taking specific interpolation query. Public [mission scripts](https://github.com/0x52617665/GTAIVMissionScripts/tree/9d836ff2d955ba2b19048c496f81a0f9eb6c84cb/IV) use IS_SPECIFIC_CAM_INTERPOLATING with their camera handle. ScriptHook's name-derived SDK hashes remain in use; alternate native IDs or GTA V signatures are not substituted.

## Changes

- No camera reads/resolution/writes during teardown or untouched Low/default.
- Require a real driver, full seating, no get-in animation, existing world/control/cutscene/pause gates, 500 ms stable seating, then two samples of the same validated gameplay camera. Invalid zero/all-ones handles are rejected before camera-existence/active queries. Changing vehicle, losing control, exiting, missing/changing camera restart the appropriate readiness state.
- Replace the global no-handle interpolation query with IS_SPECIFIC_CAM_INTERPOLATING on the validated child. Keep actual scripted-camera ownership and existing offset ownership checks. Read the specific boolean result as SDK b8 because argument and return storage alias in NativeContext.
- Apply/release only on a validated normal-driving frame. Keep owned release pending while the camera is unavailable; an in-vehicle Low reset happens on the next safe frame. Process exit naturally discards the camera, with no native call during shutdown. Camera-only settling does not delay GPS requests, route transfers or line rendering.
- Reject non-finite GPS targets before touching existing routes. Recheck helpers after move/configuration, and only restore a still-existing primary on failure. Existing zero/all-ones helper handles are not passed to native routing. No fabricated route is drawn.
- Correct the recalc message: `validTarget` tests the existing committed route/helper, not the finite candidate chosen afterwards. A first pursuit legitimately has no committed target and proceeds to selection.
- Add fixed-name begin/finish records for each camera API, candidate generation and handoff substeps to the short core log, with tick/thread IDs and numeric results/handles. Eight occurrences per operation/session bound the volume; script lifecycle resets the budget. The legacy broad stage records stay for comparison. No token, API response, private path or coordinates are added to these traces.

PR #18 world-load gating, exact b56 FusionFix host/footer rollback, updater settings and PR #19 fades/surface/XY/bend stabilizers are preserved. VERSION unchanged; all changes ship in the normal updater ASIs and use existing pause controls.

## Validation / Deck acceptance

PR lists the exact head and CI run. Deterministic coverage includes carjacking/driver-without-seating, stable seating, invalid/changing camera, control loss, vehicle changes, clock wrap, unsafe-frame no-write, deferred release, unchanged 0/.45/1.2 m mapping, b8 return aliasing, bounded/reset stage traces, non-finite targets, disappeared helper/move/configure failure and primary restoration. Source-order tests tie the gate and individual stage pairs to the production path. Existing navigation/rendering/retention suites remain.

Deck acceptance remains outstanding. After approval/merge and successful main CI, update normally, cold-load, select High with Road Line and Police Escape Line On in Pause → QoL, carjack in police sight, finish entering and drive the chase out. Confirm escape-line rendering, camera height after settling, wanted clear, Medium/Low, repeated entry/exit, pause/resume, waypoint fades and stable road lines. Exit normally and let diagnostics upload; no manual file edits. If the issue persists, the bounded per-call trace is intended to identify the missing finish rather than assigning blame from simultaneous features.
