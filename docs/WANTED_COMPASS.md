# Wanted compass and route-response changes

Update through the existing updater after merge; no manual file edits. Pause →
QoL → Check for QoL Updates displays the binary's compiled VERSION (currently
0.1.0-beta.1). A changed VERSION is compiled into the next build and appears on
its next launch after apply. It is a version label, not a Git commit identifier.
The build regenerates src/qol_version.hpp before compiling the core menu API.

The primary/manual wrong-way observation is now 1,500 ms. Heading and actual
velocity still both oppose the expected route by more than 120 degrees, at least
2.5 m/s. Brief interruptions, crawling/stationary motion, reversing along the
route and invalid samples still cancel observation. Existing forward lead-in,
last-good geometry retention, restoration and ten-second request backoff remain.

## Police state comes from the game

The background samples STORE_WANTED_LEVEL, PLAYER_HAS_GREYED_OUT_STARS(player)
and PLAYER_HAS_FLASHING_STARS_ABOUT_TO_DROP(player). GTA IV's own wantedhelp script uses the greyed
stars native for lost sight; its GREYSTARS help text explicitly describes that
meaning. The player-specific flashing-about-to-drop signal supplies the escape-countdown state, rather than a HUD blink phase. Both
query handlers must resolve before tinting; unknown state produces no colour.
No position/radius comparison is used to infer police visibility or green state.
No wanted flags, stars, AI state or search-circle values are written.

| Game state | Compass background |
|---|---|
| Wanted, stars not grey (police see player) | Low-alpha alternating dark red/blue |
| Wanted, grey stars, no countdown | Normal |
| Wanted, grey stars and flashing/countdown | Low-alpha green |
| Wanted cleared after green | Fade to normal over 1.8 seconds |
| Unknown signals, Compass HUD Off, hidden/paused/cutscene HUD | No tint |

The panel is confined to the icon strip and draws before ticks/icons. Text stays
outlined; objective, distance and persistent waypoint rows are below the panel.
The existing Compass HUD switch controls all background effects.

## Why escape guidance could miss a reset

The previous planner watched only flashing-stars transitions. Losing/regaining
sight while still inside the zone could leave that value unchanged. If the live
circle probe was unavailable, its fallback stayed anchored at the first wanted
position indefinitely. It could also keep the old route as its hysteresis winner
or choose a cheap candidate behind the player's present direction.

The planner now independently observes native sight transitions. A re-sighting
recentres the fallback on the current player position; a validated live circle
remains preferred. Wanted, countdown, sight and meaningful live-center/radius
changes still queue replans through the existing bounded backoff. The last route
and fitted ribbon are retained while requests run; no forced blank is introduced.
The fallback radius/geometry remain an escape proximity heuristic, not sight-line
prediction or an exact substitute for a missing live circle.

Every new attempt, including retries, captures current travel velocity above
crawling speed or the vehicle body heading at low speed. Candidate endpoints
must lie ahead; returned native GPS geometry must still lead in the current
travel direction, exit the latest circle and have useful road remaining.
Candidates use real road-node travel costs, with 12 seconds of speed-based
planning room (180–900 metres). The first usable candidate commits immediately;
other candidates are fallbacks, rather than delaying publication for a full
comparison cycle. No artificial segment is added.

### Continuing past the first destination

There was no arrival-means-escape-complete flag: zero wanted level clears escape
guidance. The renewal loop could instead cancel an active native request on
every meaningful circle movement, and its fixed 150-metre destination margin
could run out while waiting through four candidates. Retry state also retained
an earlier heading, and continuous sight had no explicit refresh trigger.

Circle observations now leave the native request in flight. Its result is
validated against the latest circle and current travel; obsolete results go
through fallback candidates and bounded failure backoff. Boundary observations
cannot erase that backoff. Reaching/running out of a destination requests another
forward route while wanted; continuous native sight also requests renewal every
five seconds while moving at least 2.5 m/s, without needing a sight transition,
new risk score or circle reset. The committed snapshot/fitted ribbon stay during
calculation and failure. If road data cannot produce a valid replacement, the
old road cannot be extended by inventing a shortcut. Real Deck route selection
and visibility still require acceptance testing.

All compass distance rows now use miles (two decimals below 10 mi, one above),
including hovered icons. Legacy MetricDistances preferences no longer change
this display; no file editing or new menu setting is needed.

## Deck acceptance through updater and pause menu

1. Update with GTA IV closed, launch and inspect the version next to Check for
   QoL Updates. Confirm Compass HUD Off removes both compass and tint.
2. Drive opposite a manual route briefly, then for about 1.5 seconds at speed.
   Stop/crawl/reverse along the route and verify those do not trigger. Check the
   old ribbon remains through a successful/failed forward-leg request.
3. Get wanted: check red/blue while seen. Break sight while still in the search
   zone: normal background. Escape unseen until the game starts the flashing
   countdown: green. Clear wanted and check the fade.
4. While wanted, turn and let cops see you again. Check a replacement escape
   route starts in the observed travel direction, with the old line retained
   during calculation. Repeat inside the zone (no prior flashing countdown),
   through wanted-level changes, pause and cutscenes. Confirm readability with a
   waypoint and nearby clustered icons. Native route choice and actual Deck
   visuals still need this in-game acceptance; unit tests do not prove them.

5. While continuously seen, drive through the first escape endpoint and keep
   driving while wanted. Check forward renewal before/after arrival, no blanking
   during requests and normal cleanup only when wanted clears. Repeat after a
   turn/search reset. If GPS cannot find a valid road, check it retains the last
   line and backs off rather than drawing a shortcut.
6. Hover multiple compass icons and set a waypoint. Every distance must end in
   mi, with no feet/metres readout; existing Compass HUD controls still work.
