# b112 ring regression

The complete supplied b112 slice has 24 status samples: radar query count starts
at 0 but reaches 6,153. The first query record says native=0 masked=0. Thus the
filter IS consulted; the premise that every session sample is zero is refuted
by the later records. It does not establish control of the independently drawn
health/armour ring. The old frame-calls/hidden fields also aliased query counters
rather than measuring a frame hook. Do not claim those were ring interceptions.

PR #28 removed MinimapRing ownership/enable/release from MinimapLayout and
replaced ring status with "native content/borders untouched". Its geometry
change correctly removed persistent coordinate writes, but removing the separate
DrawFrameFront/Back hooks was the concrete ring coverage regression. Those
callbacks are the IV-SDK radar ring frame path; Plugin-SDK identifies their
radarringfront/back sprite objects. A display-answer override of HUD_RADAR is
not equivalent to controlling those deferred draw callbacks.

Reconnect only the existing validated dedicated frame hooks. They resolve by
sprite references plus unique decoded queued callback bodies, not guessed RVAs
or a global sprite primitive. Preserve the radar predicate, zero geometry
writes, native content/fill/lifetime callbacks, and the immediate pause-setting
release. During frontend pause the originals run; Off removes both hooks before
returning. On prepares them at the existing safe world-load / gameplay path.

New real-hook records identify health-armour-front/back, suppression, frontend,
separate calls/hidden counts, first pass-through/suppressed draw and release.
Status reports actual ring counters rather than aliasing radar queries, and the
larger buffer prevents truncation. The earlier supplied b99 capture had 5,578
frame calls on this dedicated callback mechanism; this is historical path
coverage, NOT a new post-fix Deck capture. The current b112 trace proves query
consultation but cannot prove a newly restored hook ran before delivery.

Windows executable test invokes the production x86 callbacks, verifies both
frames suppressed on the first On draw, native fill/content and unrelated HUD
work still run, and three paused Off/On cycles restore the originals. Captured
controlled trace asserts suppressed=1 for both components and release. This is
replacement-mechanism evidence, not an invented real-game after trace. Deck
acceptance must show front/back suppressed counters increasing from first load
and no visible native ring; Off must restore the complete HUD. If coloured fill
still remains, that is failed acceptance requiring a narrower proven visual
path, not permission to skip a whole stateful content callback again.

References: Zolika1351/iv-sdk include/CRadar.h DrawFrameFront/Back and
DK22Pac/plugin-sdk plugin_IV/game_IV/CRadar.cpp RadarRingFront/RadarRingBack.
