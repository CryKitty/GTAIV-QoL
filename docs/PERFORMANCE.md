# Navigation CPU profiling for the beta pass

No in-game FPS, full game-tick, GPU, Steam Deck hardware or native-call timings were measured in this environment. These are **synthetic CPU stage benchmarks**, not end-to-end gameplay claims.

## Reproduce

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/navigation_profile.cpp -o /tmp/navigation-profile
/tmp/navigation-profile
```

The same fixture runs in Windows CI with the normal MSVC x86 toolchain. Tests first compare section coordinates and escape scores against frozen pre-change implementations. No timing threshold is used to decide pass/fail. Inputs include dense/duplicate/elevated/empty paths, arbitrary starts/backwards sampler queries, fitted/unfitted surfaces, both ribbon modes/layer widths and opposite camera headings. The existing D3D9 pixel/recovery test also runs on the changed renderer.

Local measurements: Debian 13 x86_64, GCC 14.2.0, -O2; three successive invocations of the same binary, warmed stages, median microseconds per call below. Timings are noisy and order is not randomized; reproduce on target hardware before drawing broader conclusions.

| CPU stage / workload | Before | After | Change |
| --- | ---: | ---: | --- |
| Route construction: roadLine on a 1,000-node route | 40.609 | 40.609 | Measured, unchanged |
| Ribbon cross-sections: 304 output points, two layers, Twisted, fitted edges | 146.544 | 71.373 | Shared adjacent sections computed once per layer; retained scratch storage |
| Compass layout: sort/overlap placement of 1,000 markers | 30.410 | 30.410 | Measured, unchanged |
| Escape pathScore: 1,000 nodes, 48 visible police locations, 700m horizon | 415.289 | 31.279 | Monotonic sample cursor replaces repeated prefix rescans |

Before/after range across the three runs: sections 144.445–181.205 / 70.284–72.170; escape score 398.560–417.011 / 30.986–34.224 us/call. Checksums and equivalence checks passed each time.

One Windows/MSVC x86 /O2 CI run of the same fixture measured sections
688.031 -> 331.758 us/call and escape score 392.665 -> 167.654; unchanged route
construction was 279.941 and compass layout 15.340. This is a different machine,
compiler and architecture, not a cross-platform comparison or FPS result. The
smaller Windows escape speedup is why the Linux ratio must not be advertised as
a universal improvement. CI prints each run's measurements without a timing gate.

## What changed and what did not

The renderer formerly constructed the same endpoint twice in adjacent quads for each layer. It now builds shared world-space sections once per point/layer and reuses them. Camera yaw is evaluated once per frame. Valid normal fitted/no-edge inputs use the cache; partial edge arrays keep the previous segment-specific fallback. Widths, surface offsets, roll, projection/depth safety, colour/alpha, clipping and draw/state restoration are unchanged. Sections are recomputed for every frame/layer, so camera/player changes cannot use stale geometry.

Escape scoring samples increasing road distances at 12m intervals. Each old sample restarted at node zero, including already-passed road behind the car. The new cursor preserves sampleAt's interpolation, degenerate-segment and endpoint behavior while traversing forward only; backwards queries reset. Scoring weights, elevations, police proximity, route selection/hysteresis and native GPS scheduling are unchanged. Existing planner requests remain sliced over game ticks; this benchmark measures scoring a candidate, not an entire tick.

Compass layout was measured but not rewritten. It was materially cheaper than the section/scoring fixtures, and its native marker queries, text/icon rendering and real-world marker mix are not represented by this CPU fixture. Route smoothing/construction was likewise left unchanged. These measurements do not justify an FPS claim or a broad compass rewrite.

The earlier road-bank benchmark remains available separately in tests/roadnav_bank_benchmark.cpp. Its results are not presented as new optimisations in this pass.
