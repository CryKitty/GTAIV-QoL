# Sustained wrong-way road-line replanning

With **Pause → QoL → Road Line On**, a manual waypoint or fixed coordinate mission route can request a new forward lead-in after the car's heading **and actual movement** oppose the expected route direction continuously for **1.5 seconds**. The movement floor is 2.5 m/s (about 9 km/h / 5.6 mph); standing, crawling, backing along the route, a brief swerve, another floor, a missing route, a pause/control interruption, a target change or a sampling gap breaks that observation. No new INI edit or menu setting is required. Road Line Off cancels the feature and restores the normal target.

The initial road tangent can be projected behind an old GPS calculation origin for the debounce. Otherwise driving away from that origin for 1.5 seconds could leave its clamped endpoints before the timer ever fires. Lateral/height checks still reject unrelated roads/floors.

## How the direction preference is implemented

The verified scripting API requests GPS **from the player's current position**; it does not expose a verified start-heading parameter. Merely toggling the same target can return the same backward plan. The implementation therefore asks native GPS for a short drivable node ahead (normally 55–180 m, with shorter-node fallback for bends). It accepts the result only if its endpoint, floor, proximity to the current car and first remaining road direction validate. All line geometry comes from native GPS; no straight shortcut, invented reverse road edge, player teleport or vehicle/control write is used.

The original player/mission destination is never moved or removed. The temporary lead-in is excluded from compass objectives. On reaching/passing its node, or cancellation/timeout, GPS returns to the original destination **from the car's then-current position**, allowing GTA to choose the remaining block/U-turn. This is a staged native route, not a claim that the entire final trip is precomputed in the new direction immediately. Road legality and the final continuation remain GTA's planner's responsibility. If no suitable forward node/native path exists, the old route is retained; there is no fabricated path fallback.

Native GPS has two buffers which can both end at the same goal. After an opt-in primary replan, current-origin proximity breaks that ambiguity so the old-origin plan does not immediately win again. Normal readers and police escape keep their existing selection behavior.

## Stability and scope

- Keep the previous primary snapshot/line while the new leg is pending; publish only a validated forward native path, and retain the previous valid surface if replacement fitting temporarily misses. Pending lead-in GPS has a 3-second timeout; a forward leg is bounded to 15 seconds, and new attempts have a 10-second minimum backoff. Restoration retains the last valid line until a current-origin primary path is available; retries are at most once per 10 seconds.
- Turn Arrows uses the original primary route/cue algorithm, not the temporary line leg. Its existing wrong-way/U-turn cue policy is unchanged.
- Police escape is intentionally unchanged. Primary forward jobs are deferred while wanted/escape owns the shared GPS planner; its wanted/search transitions, retry backoff and last-good escape geometry remain intact. Moving entity/chase targets are not changed.
- Changing/clearing the target, disabling Road Line, pausing, losing control, leaving the car or ending the script removes only the owned helper and restores the original route when it still exists.

## Validation and game acceptance

Portable tests cover sub-1.5-second swerves, continuous 1.5 seconds while driving away past the old route origin, stationary/crawl/reverse motion, target/sample-gap resets, another floor, timer wrap, request backoff, ahead-node selection/rejection, backward native-path rejection even with a matching endpoint, GPS helper/primary ownership and resuming the unchanged original target. The two-buffer regression verifies opt-in new-origin preference and unchanged default selection. Native/rendering, coexistence and installer/updater suites remain.

These are deterministic geometry/state/adapter tests, not an in-game route-finding or legal-U-turn guarantee. After merge/update, test a manual destination in a two-way street and a one-way block: drive opposite the line for under 1.5 seconds (no job), stop/crawl (no job), then maintain the opposite heading while moving for 1.5 seconds. The old line should stay visible until the forward leg is ready, lead ahead, then return to the original destination at the node. Check turn cues independently, a changed/cleared waypoint, Off/On, pause/control loss and wanted/escape transitions. Test a fixed mission coordinate without moving/removing its mission marker. Failed/slow native requests should retain the last valid line and back off instead of blinking/retrying every tick.

## Follow-up timing and two-buffer acceptance

Code trace (no in-game timing capture is available): the normal navigation tick
can delay the first opposite sample by up to 200 ms. Observation remains 1,500 ms.
Observation/pending work now polls every 50 ms; ordinary navigation remains at
200 ms. Previously the next 200 ms tick imposed a minimum request wait, and
endpoint-first buffer ranking could repeatedly select a backward old buffer,
reject it, time out at 3 seconds, wait for restoration and then the 10-second
failed-attempt backoff. Native calculation latency is unknown, not measured.

Forward validation now runs on **each** consistently copied buffer before
endpoint/current-origin ranking. A usable second buffer is published on the next
50 ms navigation service tick and fitted in that same tick. Assuming game ticks
arrive on schedule, the request starts within 1.75 s of continuous opposite
movement (200 ms first sample + 1.5 s observation + up to 50 ms polling).
A result arriving inside the existing 3 s request window is therefore committed
or the request fails by at most **4.80 s**, including a final 50 ms poll. The
successful-result latency is instead detection + native calculation + at most
50 ms service delay; there is no separate 200 ms acceptance floor. Frame stalls
add their own delay. There is **no finite guaranteed fresh-line worst case** if
GTA returns no forward legal route. Failed requests retain the line/backoff.

Reaching/passing the ahead node still restores the original destination;
restoration waits at least 200 ms for a current-origin original-destination
buffer, and retries it every 10 seconds while retaining the last valid ribbon.
The 30-second general borrowed-window recovery does not gate acquisition of a
forward result. Wanted/escape still defers fixed-primary wrong-way jobs.

Road ribbons now stay on the native GPS centreline. PR #10's right-side offset
and PR #12's lane-station blending are withdrawn after a straight-road regression;
no lane-count query or lateral offset participates in rendering. Surface fitting,
small-noise stabilisation and primary/escape ownership are kept. See
[road and compass regressions](ROAD_AND_COMPASS_REGRESSIONS.md) for the current
behaviour, evidence and acceptance checks.
