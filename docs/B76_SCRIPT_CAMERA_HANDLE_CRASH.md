# b76 scripted-camera handle crash

## Confirmed operation and mechanism

Base main is f8cc37245eb36e2b6314223b51ea2b8c1932c1cf (b76). All four supplied logs were inspected locally. No private log files, paths or capture payloads are committed. The user reproduced with no wanted level and no route, eliminating escape planning as a requirement.

The core per-operation trail shows two successful validations of gameplay child **3842**, completed interpolation (false), then GET_SCRIPT_DRAW_CAM output **4231426544 / 0xFC3671F0**. `camera script exists begin` is the last operation marker; there is no finish. In source that call is **DoesCamExist(scripted)**. The fault recorder's **EDX matches the raw draw value exactly**, at GTAIV.exe RVA **0x5B6A2A**, with a read target of **0x37C**. Together these identify the failing operation and the unsafe promotion of an unusable draw result into a native camera handle. Identical duplicated fault records are not separate crashes. The exact reason the getter supplies that opaque value, and a CE internal symbol for the RVA, are not established; no invented function name or executable disassembly is claimed.

PR #20's validator rejected only zero and all-ones. It therefore admitted this negative-as-int32 output and called the game's existence native on it. Validating a known gameplay child does not validate an unrelated draw result. The fix removes the failing dereference rather than adding another timing delay or treating a heap-address range as a handle format.

## Readiness clarification

The last arrow-log `ready=0 / gameplay-valid=0` is an earlier throttled diagnostic sample, not the admission state of the failed query. PR #20's source sets readiness only after the second child validation; the attached core trail records both validations and then interpolation/draw. The crash happened before the next state summary could print. It is incorrect to claim those queries bypassed PR #20's existing gate solely from that stale line.

One shared `cameraAccessReady` predicate now gates the supplemental interpolation/draw/offset probes and application, with an explicit **camera probes ready** marker before the current batch. Rejected probe batches also invalidate write readiness. The only necessary bootstrap is getting/checking the gameplay child after stable, controllable seating: camera validity cannot be established without obtaining it. This safe bootstrap stays behind PR #20's 500 ms seating/transition guard and two-sample admission; it is not an unguarded scripted-camera probe. No child lookup happens during entry animation, missing control, cutscene or pause. No supplemental probes/writes happen without full readiness. Teardown and untouched Low remain native-free for height control.

## Fix and conservative ownership

- Camera IDs must be positive signed-32-bit candidates. Zero, all-ones and high-bit/negative values, including the exact captured result, classify as no usable script camera. Camera handles are IDs, not object addresses: no arbitrary heap-address cutoff is introduced. Vehicle handle policy is kept separate.
- The raw draw output stays numeric, initialized to zero. It is **never** passed to DoesCamExist, IsCamActive or IsCamPropagating, even if positive. Only the already validated gameplay child receives its existing existence check.
- A draw value equal to that known gameplay child needs no second validation and indicates no different owner. Any other positive candidate conservatively blocks height writes as a possible script owner, without dereferencing it. This can hold off height while ownership is uncertain; it does not pretend an unknown positive candidate is verified active. Classification markers distinguish none/gameplay/other-candidate (0/1/2).
- Keep per-operation markers and append readiness/classification markers without renumbering previous stage IDs. Obsolete script-existence/active/propagating stage names remain readable for older captures but no such native calls execute in this path. Trace volume remains bounded per session.

The fixed path preserves preset mapping (0/+45/+120 cm), pending safe resets, actual script-owner conservatism, interpolation guard, world-load guard, exact b56 FusionFix host and updater plumbing. Police GPS requests, fades, centred geometry and surface/bend stabilizers are unchanged. VERSION remains for named releases.

## Earlier b64/b72 faults

b72 also ended in the broader scripted-camera query bracket at the same RVA, with a suspicious high-bit value in EDX. b76 now establishes the exact bad-draw/existence mechanism. The reported b64 occurrences at the same RVA are consistent with this path, which PR #17 introduced, but b64 lacks the per-operation/raw-result correlation needed to prove that it was the entire cause of its loading hang. The separate host/load rollback remains intact. No reopening of unrelated features or claim that all historical faults are proved identical.

## Validation / acceptance

The PR records exact head and full CI. Deterministic tests run the production probe helper using the captured value, zero/all-ones/negative boundaries, the known child, different positive and large positive candidates. Unknown values cause no existence/active/propagating call because the production path contains none; other-owner candidates prevent offset reads/writes. Every rejected readiness state and invalid gameplay handle yields zero probe callbacks and no write. Interpolation blocks subsequent draw/offset work. Existing entry/clock-wrap, 0/.45/1.2 m mapping, lifetime and bounded-marker tests remain. Production wiring tests allow exactly one camera-existence call, for the validated gameplay child.

After approval/merge and successful main CI, use the updater, cold-load with no route/wanted level, select High in Pause → QoL and simply enter/drive an ordinary car. Then enable Road Line/Police Escape Line and repeat a wanted carjack, render the escape line and drive until wanted clears. Cycle Low/Medium/High/Low while driving; test pause and entry/exit. Exit normally for automatic diagnostics upload. Deck acceptance is outstanding; no manual edits or release.
