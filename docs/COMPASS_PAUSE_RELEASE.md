# Compass Off during pause

The b84 session logs show radar geometry updates continuing. They contain no
saved Compass toggle event, so the user-reported toggle cannot be aligned to a
specific mask transition from those logs. A late release/failed setting delivery
is not conclusively established by b84 alone.

Source: the only release was in CompassHud::tick, after several gameplay native
queries and housekeeping. ScriptHook gameplay ticks need not run while the
frontend is paused. There was no frontend-to-mask notification or policy barrier
against an already-admitted On tick. PR #23 repaired geometry restoration inside
release, but did not fix delivery of release from the paused menu.

The settings owner now invokes the companion's QoLCompass_SetEnabled export on
a successful saved Compass setting. It passes one event tick, logs begin/finish
and the returned mask state. The companion logs the same event, thread ID and
mask before/after, raw layout/live sentinel read-backs and pending restores.
Failed restoration writes retain their baseline for the next Off tick. This path only restores existing HUD geometry/hooks: no
player, camera, entity or initialization natives run. A mutex serializes it with
mask updates; Off also prevents a stale On tick from re-enabling the mask. On
allows activation from the normal safe gameplay path again. The verified
FusionFix host is unchanged.

The Windows production-mask test emits/asserts mask=1->0 directly from a paused
setting dispatch without another script tick, then attempts stale activation and
repeats three cycles. It also retains replacement-component restoration and
native visibility/opacity tests. This is deterministic executable evidence, not
an invented Deck retest. Deck acceptance must show the matching core/renderer
event records and full map restoration after a single unpause. If delivery says
unavailable or the map still fails, these logs expose that failure; acceptance
is not claimed ahead of the retest.
