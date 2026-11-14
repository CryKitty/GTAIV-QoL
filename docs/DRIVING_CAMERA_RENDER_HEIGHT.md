# Driving-camera height: render-path candidate and proof gate

## What b78 establishes

The supplied core and full-session slice show the guarded legacy SET_FOLLOW_VEHICLE_CAM_OFFSET invocation completes, then IS_FOLLOW_VEHICLE_CAM_OFFSET_ACTIVE remains true. No native fault occurs. **This proves a stored native flag, not a changed rendered camera.** No camera-position/view read-back is present, and the user confirms no visible change. The old code wrote a relative-looking vector into that legacy override and never changed/read the final follow-camera transform. Repeating the same flag setter is not evidence of a fix.

The source-level mechanism distinction is supported by vanilla [manhat_heli_tours](https://github.com/0x52617665/GTAIVMissionScripts/blob/9d836ff2d955ba2b19048c496f81a0f9eb6c84cb/IV/manhat_heli_tours.txt): it sets follow submode 0 along with the legacy offset before creating a scripted tour camera. That is not proof that the legacy override affects every normal third-person follow mode. The SDK exposes separate game/final/vehicle/on-foot camera objects; native child validity does not establish which transform consumes that override. Exact internal CE offset fields/branch semantics are not exposed by this SDK and are not invented here. Integration root cause: the former implementation considered legacy override bookkeeping success sufficient without coupling it to the rendered view. **Deck proof of the replacement is still required.**

The apparent preset-zero anomaly is not a read artefact: core records menu selections 45 → 0, then 45 → 120. First apply is at 119721543, release at 119733501 (about 12 seconds later), reapply at 119746193. UI Set is the selection writer; camera probing does not write the preset. No entry-time automatic clobber was found.

## Replacement mechanism / provenance

Independent implementation using the CE vehicle-follow copy call contexts documented by [Centered Vehicle Cam](https://github.com/gennariarmando/iv-centered-vehicle-cam/blob/d52efaed243e34b1e0e18cd5e0b1121db9a0ec78/source/Main.cpp). No upstream source code is vendored. That implementation applies its height at the fresh vehicle-follow matrix copy; its published call signatures/patterns identify front/rear contexts. This candidate intercepts the resolved copy target with MinHook, always calls/chains the original target first (including an existing FusionFix target), and permits changes only for those two exact return sites. Other callers of a shared copy routine pass through untouched. Two unique PE32 x86 executable signatures and executable destinations are mandatory; missing/ambiguous sites fail closed. Nothing is patched during DLL load or before stable driving/world readiness.

The normal game camera computes the baseline every frame. Add only the requested **world Z**, preserving XY, orientation, zoom and the original return value. Do not create/propagate a scripted camera, force submode 0, or call SET_CAM_POS. Low is pass-through; source/destination chaining accounts for an already-owned copied height to avoid adding it twice. A stale state (>200 ms), unsafe frame or an external legacy override prevents positive shifts. No camera-native calls occur in the copy hook or D3D read-back.

## Safety and camera-call inventory

All raw GET_SCRIPT_DRAW_CAM classification code from PR #21 remains unchanged. Negative/zero values are ignored; different positive candidates conservatively block writes and are never passed to existence/active/propagation natives. No propagating-child veto returns.

| Site / phase | Calls or operation | Admission / lifetime |
| --- | --- | --- |
| Basic tick, including Low | Ped/car/driver, seating/entry, cutscene-loaded/finished, pause/control checks; player position | Existing world gate first; no camera handles or camera writes here |
| Entry bootstrap, positive preset | GET_GAME_CAM_CHILD; DOES_CAM_EXIST on that positive child only | 500 ms stable seated/control state; two valid samples required for probes |
| Positive probe batch | IS_SPECIFIC_CAM_INTERPOLATING(validated child) | Shared cameraAccessReady gate and native availability |
| Script-owner check | GET_SCRIPT_DRAW_CAM → numeric PR #21 classification | Same gate; raw output never goes to any dereferencing native |
| Existing override check | IS_FOLLOW_VEHICLE_CAM_OFFSET_ACTIVE | Same gate; another owner's legacy override blocks the matrix shift |
| Apply each fresh copy | Original vehicle-follow matrix copy, checked world-Z write/read-back | Known caller only, fresh driving permission, valid matrix and proximity to player; no natives |
| Low / pause / entry / exit / unload | Disable/publish permission; Low original copy is retained | No legacy setter/release native; next fresh original copy restores baseline. Low clears child-sample admission before the next positive selection |
| Render proof | RoadSceneCamera's read-only live viewport/inverse-view read | Matrix/reference validity and freshness; no game natives, no scene matrix writes |
| Existing road/compass camera projection | GET_ROOT_CAM / GET_GAME_CAM_CHILD fallback, GET_CAM_POS/ROT/FOV and clip planes | Existing normal-driving/cutscene/control path, unchanged by this candidate |
| Existing taxi-FOV feature | Existing validated camera FOV observer/read/restore | Unchanged; separate from driving height |

**Removed from height control:** SET_FOLLOW_VEHICLE_CAM_OFFSET. There is no apply/release setter whose active flag can masquerade as rendered proof. Readiness and scripted-owner gates are retained; Low performs no height-native probes, but its passive matrix/viewport telemetry can establish the neutral baseline. The CE copy hooks delegate without a new offset at Low.

## Required read-back and honest limit

Once per second WHILE DRIVING, log requested cm, driver/allowed, child, copy-before-Z, copy-after-Z, copy-dz, actual rendered eye Z from the validated gameplay viewport, rendered-dz, expected-dz, pair status, same-vehicle Low baseline, verified status, copy counts and age. Do not use the active bit as evidence. A positive verification requires both the copy delta and independent rendered delta to agree with the preset, plus a paired current view and prior matched Low baseline on the same vehicle. High with an unchanged rendered view must remain verified=0. Unavailable/mismatched viewport data likewise cannot pass.

Local synthetic read-back and executable x86 copy tests establish the implementation contract, **not** a live Deck measurement. This PR remains draft until a real same-drive Low/Medium/High capture shows approximately 0/.45/1.20 m with verified=1 and the user sees the change. No authentic driving log can be fabricated from this workspace. The b78 logs do not contain this proof. If the candidate fails, keep it unmerged and use the failed read-back to locate the next stage.

## Validation / acceptance

PR records exact head and CI. Tests cover real x86 shared-target front/rear chaining and return/stack ABI, unrelated caller pass-through, hundreds of non-accumulating copies, untouched fields, invalid/stale policy, 0/.45/1.2 m, source-copy baseline, production viewport read-back, and rejecting a successful memory write whose rendered view stays unchanged. Existing PR #21 junk-handle/probe safety tests remain.

Updater only after independent verification/explicit merge instruction and successful main CI. Cold load, drive Low on a fixed stretch with a roofline/landmark; repeat Medium and High on the same vehicle, cycle while moving, enter/exit and pause/resume, then test ordinary no-route entry and wanted carjacking/escape. Exit for automatic diagnostics upload; no manual files. Compare read-back rows and visuals. VERSION and payload shape stay unchanged; host/load/fades/stabilizers are not replaced. Help-prompt investigation is separate and must not be represented as fixed by this camera change.

The b84 user retest confirms visible height changes and exact copy-level deltas.
Its viewport sample remained unavailable; this is now logged as
`verified=unavailable`, not a measured verification failure. It is not claimed
to be independent rendered-position proof.
