# First-load compass presentation

The supplied b99 arrow slice orders usable-world runtime start, renderer attach,
then the first coordinate-mask installation. The first sampled compass status is
already at 4,693 geometry updates. The core log says compass initialization
finished and first tick finished before any timed first-mask record. It also
records later Off/On delivery. There is no timestamp for the first geometry write
or screenshot of that first radar, so its exact duration cannot be reconstructed.
Do not claim these logs prove a particular cached game field was corrupted.

The source nevertheless wrote -10000 into the native radar layout AND live
component each script tick. Those are game-owned layout inputs, not merely our
render vertices. Initialization/copying can consume displaced positions; a pause
refresh later reconstructs native HUD state. PR #25's release delivery does not
repair initial masking while Compass stays On. Eliminate that class of problem:
this implementation never writes HUD coordinates, alpha or native visibility
flags. It filters only the mapped HUD_RADAR component's display answer during
gameplay; all other component answers, native content callbacks and frontend
answers are forwarded unchanged. Original queries still run. Off unhooks this
filter synchronously through the existing pause-setting export.

Prepare presentation during the usable-world initializer, before the compass
initialization-finished stage and first gameplay housekeeping/camera query. No
new work runs before the existing safe-world gate. Missing signatures/mapping
retain a clean native radar; bounded discovery retries require no pause cycle.
The global display function is hooked but the override applies ONLY to the exact
component mapped by HUD_RADAR, never every HUD query. This is a narrower contract
than global HUD suppression; no help timers or native content callbacks change.

New records share GetTickCount: world-load stages; phase=world-ready;
phase=mask-ready, mapped-component; phase=first-radar-query, native/masked.
All record geometry-writes=0. The Windows production executable test verifies
the first native radar query is masked without a pause, all 67 components keep
native coordinates/alpha, only radar's answer changes, replacement mapping is
recognized, frontend passes through and repeated Off/On preserves preferences.
Deck visual acceptance and GPS generation while hidden remain explicit retests,
not facts inferred from this executable test. Cold-load, check compass/radar and
set a road route BEFORE opening pause, then check Off/On and a mission prompt.
