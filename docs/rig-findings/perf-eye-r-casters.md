# Eye R's shadow setup and caster gathering: what it costs per eye, and the ceiling of sharing eye L's casters

`perf-stereo-cpu.md` (lever 3, section 7) proposed giving eye R eye L's shadow caster lists: GatherShadowCasters
(0x1CA3EE0) takes 0.478 samples per stereo tick against 0.199 per mono frame, and `r_skipShadows 1` takes it to zero
for about 5% more ticks. The shadow reuse spike (`perf-eye-r-reuse.md` section 9, result fail) found that eye R
already lists almost none of the spot and point slices eye L renders, and guessed that eye R's remaining shadow work
is "the per-light setup, caster culling and gathering, which run whether a slice is listed or not" [inferred there].
This page reads where the setup and the gathering run (section 1), counts and times them per eye on the bench
routes (section 2) and bounds what eye R could save by taking eye L's casters (section 3). Steam build 25216728,
RVAs in that build.

Tags: **[code]** read in this build's code, **[rig]** measured on the test PC, **[inferred]** a reading the rig
has not confirmed.

## 1. Where the setup and the gathering run

Each render's shadow work for its view is a chain of jobs that 0x1CFA640 builds (the render-view job graph; a
serial twin, 0x1D03460, runs the same steps in one function when the graph is off) [code]:

1. **The setup job** 0x1CF0240 (rcx = the view's shadow context) walks the view's shadowed lights (an array at
   context + 0x184E98, 0x90 bytes each, count at + 0x184E90). It skips a spot or point light whose bit is clear in
   the view's visible-light set (the binning readback, `shadow-pop.md` section 1) and calls the per-light record
   function 0x1CFE2B0 for each other one (at 0x1CF03EA), which lists the slices to render (the stale check and the
   listing of `perf-eye-r-reuse.md` and `perf-sun-cascades.md`). It then allocates atlas space, sorts and cuts the
   list at the triangle budget, and makes **one gather item per listed slice** (`test byte [entry + 0x29], 6` at
   0x1CF0980; 0x40 bytes at context + 0x3E50, count at + 0x184E60).
2. **The gather job** 0x1CEECB0, a parallel-for over those items (its count read by 0x1D013A0), calls
   GatherShadowCasters with each item. That builds the slice's projection (0x1CA6080), then culls the world's models
   against it and writes the casters to the item's output (context + 0x1050 + entry * 0xB8; the casters themselves
   go to one array per context, taken with a `lock xadd`).
3. **The atlas pass** 0x1CA2610 (the render list: rcx = the same context) walks the setup's list and records each
   listed slice's draws from its gathered casters, sets the entry's counter back to 0 and keeps the slice's
   projection in the entry.

So **GatherShadowCasters runs only for listed slices**: a light whose slices eye R does not list costs eye R its
record call in the setup, but no gathering and no atlas work. The spike's guess that the gathering "runs whether a
slice is listed or not" is wrong [code]. The item keeps the light (+0x10), the face (+0x18, the slice >> 1), the
player flag (+0x1E: the entry's flag 0x10, set for the odd slices) and its output (+0x38); the slice itself is the
list entry's byte + 0x28 [code; rig: the probe's cross-check of face and player flag against the list found no
mismatch in any run].

## 2. What each eye's setup, gathering and atlas pass cost (`ETERNALVR_EYE_R_CASTERS=count`)

A rig probe (`ETERNALVR_EYE_R_CASTERS=count|ceiling`, off unless the variable is set; it stays on the
`eye-r-casters` branch with its hooks and tests, not merged, since the result is a fail) times on the job's own
thread, with the render's eye from its tag: the
setup job (entry to epilogue), each record call, each gather item by kind (and the span from a render's first item
to its last), and the atlas pass. CPU time of the jobs, not the tick's critical path: the gathers run in parallel,
the setup and the atlas pass each on one thread.

Rig: the test PC (RTX 3080 Ti, i7-9700K), OpenXR-Simulator at 90 Hz, the Release layer of the `eye-r-casters`
branch (main 5a14018d plus the probe), the bench routes (`bench-routes.md`) at the launcher's defaults, Auto 1280 x 1400,
pacing off (`ETERNALVR_PACE=off`), CPU and GPU timing on. Per stereo tick, one `count` run per route; in the nine
`ceiling` runs of section 3 eye L's setup and gathers are within 0.03 ms of these and its atlas pass within 0.12
ms. The raw runs stay on the test PC. [rig]

| Route (tick p50, GPU p50 ms) | Eye | Setup ms (record calls sun / spot / point; their ms) | Slices listed | Gathers sun / spot / point / player | Gather CPU ms sun / spot / point / player (sum) | Gather span ms | Atlas pass ms |
|---|---|---|---|---|---|---|---|
| Combat (8.66, 5.39) | L | 0.096 (1 / 16.0 / 23.6; 0.022) | 4.49 | 1.46 / 1.35 / 0.45 / 1.22 | 0.558 / 0.009 / 0.005 / 0.243 (0.815) | 0.453 | 0.856 |
| | R | 0.107 (1 / 17.5 / 24.6; 0.031) | 3.02 | 1.46 / 0.34 / 0.00 / 1.22 | 0.632 / 0.002 / 0.000 / 0.287 (0.921) | 0.509 | 0.993 |
| Outdoor (8.93, 7.55) | L | 0.135 (1 / 20.0 / 15.6; 0.025) | 5.97 | 2.00 / 1.96 / 0.00 / 2.00 | 0.388 / 0.004 / 0.000 / 0.188 (0.580) | 0.274 | 0.603 |
| | R | 0.151 (1 / 19.6 / 16.5; 0.028) | 4.43 | 2.00 / 0.43 / 0.00 / 2.00 | 0.415 / 0.001 / 0.000 / 0.202 (0.619) | 0.284 | 0.633 |
| Hub (7.12, 5.71) | L | 0.146 (1 / 26.3 / 20.5; 0.031) | 8.52 | 2.00 / 3.72 / 1.80 / 1.00 | 0.325 / 0.025 / 0.005 / 0.195 (0.550) | 0.226 | 0.529 |
| | R | 0.153 (1 / 28.1 / 21.3; 0.037) | 4.91 | 2.00 / 1.60 / 0.31 / 1.00 | 0.386 / 0.025 / 0.001 / 0.228 (0.640) | 0.260 | 0.562 |

What eye R gathers of eye L's slices of the same tick, per tick (what `ceiling` skips): in combat the player's 1.22
slices only; outdoors 0.38 spot and 2.00 player; in the hub 0.99 spot and 1.00 player. The rest of eye R's gathers
are the sun's world slices (1.46 to 2.00, Shared sun shadows' work) and a few spot or point slices eye L did not
gather in that tick (combat 0.34, hub 0.92, outdoor 0.05) [rig].

- **The setup is small** in both eyes: 0.10 to 0.16 ms a render, the per-light record calls (16 to 28 spot and 16
  to 25 point lights, and the sun) 0.02 to 0.04 ms of it; eye R's is 0.01 to 0.02 ms longer. Nothing to share there.
- **Eye R gathers what eye L gathers, less the spot and point slices eye L rendered**: the sun's world slices (each
  a whole cascade, static casters too: 0.39 to 0.63 ms of CPU a render, by far the largest gathers), the player's
  slices (0.20 to 0.29 ms) and next to nothing for spot and point lights (0.001 to 0.025 ms). Beyond the sun, eye
  R's share of GatherShadowCasters is the player's slices.
- **The gathers run in parallel**: a render's span (first item's start to last item's end) is 0.23 to 0.51 ms
  against 0.55 to 0.92 ms of CPU, set by the sun cascade's gather, the longest item.
- **The atlas pass is each eye's largest single-thread shadow cost**: 0.53 to 0.99 ms a render, eye R's 0.03 to
  0.14 ms longer than eye L's.
- Eye R's sun gathers take 0.03 to 0.07 ms more CPU than eye L's for the same cascades [rig; why not known].
- The item checks (each gather item's face and player flag against the setup's list entry) found no mismatch in
  any run.

## 3. The ceiling (`ETERNALVR_EYE_R_CASTERS=ceiling`)

Eye R keeps the slice's projection but gathers no casters for any slice eye L gathered in the same tick, the sun's
world slices excepted (they are Shared sun shadows', T-234): the output is left as the engine's own overflow path
leaves it, so eye R renders those slices with no casters (its pictures are wrong there; it is a bound). It skipped
what the count runs predicted: in combat 1.12 to 1.24 player slices a tick (eye R's player gather CPU from 0.287 to
0.005 ms), in the hub 0.80 to 1.24 spot and 0.53 to 1.00 player slices.

**A/B** (bench-combat and bench-hub at 1280 x 1400, pacing off, the switch unset against `ceiling`, four
interleaved pairs per route in the order off/ceiling, ceiling/off, off/ceiling, ceiling/off; each run's GPU busy
p50 for the load) [rig]:

| Route | Pair | Off: ticks/s, tick p50 ms, eye R ms, outside ms, GPU ms, missed % | Ceiling: the same | Tick p50, ceiling - off |
|---|---|---|---|---|
| Combat | 1 | 118.2, 8.32, 3.91, 4.53, 4.57, 0.26 | 106.4, 9.20, 4.26, 5.11, 6.49, 1.09 | +0.88 |
| | 2 | 114.2, 8.66, 4.05, 4.69, 5.17, 0.26 | 109.6, 8.96, 4.15, 4.96, 5.84, 0.46 | +0.30 |
| | 3 | 117.4, 8.42, 3.94, 4.59, 4.79, 0.15 | 108.5, 9.05, 4.19, 5.01, 6.24, 0.57 | +0.63 |
| | 4 | 113.6, 8.68, 4.05, 4.75, 5.40, 0.20 | 111.1, 8.82, 4.11, 4.89, 5.55, 0.41 | +0.14 |
| | median | 115.8, 8.54, 3.99, 4.64, 4.98, 0.23 | 109.0, 9.00, 4.17, 4.99, 6.04, 0.52 | +0.46 |
| Hub | 1 | 136.7, 7.31, 3.40, 3.93, 6.08, 0.02 | 138.1, 7.17, 3.35, 3.94, 5.70, 0.07 | -0.14 |
| | 2 | 139.8, 7.12, 3.33, 3.82, 5.48, 0.02 | 133.3, 7.43, 3.49, 4.06, 5.97, 0.78 | +0.31 |
| | 3 | 137.4, 7.29, 3.39, 3.92, 5.90, 0.06 | 135.8, 7.34, 3.43, 3.97, 5.84, 0.04 | +0.05 |
| | 4 | 134.4, 7.34, 3.45, 4.01, 6.06, 0.02 | 134.3, 7.35, 3.44, 3.98, 5.97, 0.07 | +0.01 |
| | median | 137.0, 7.30, 3.40, 3.93, 5.98, 0.02 | 135.1, 7.34, 3.43, 3.97, 5.90, 0.07 | +0.04 |

The tick p50's range within a mode: combat 8.32 to 8.68 ms off and 8.82 to 9.20 ms ceiling, hub 7.12 to 7.34 and
7.17 to 7.43 ms.

- **Combat: the ceiling is not faster in any pair.** The route is two-state with the encounter (GPU per tick about
  4.6 to 5.4 ms or 5.8 to 6.5 ms); all four ceiling runs drew the heavier state or near it, which is most of the
  medians' +0.46 ms. At matched load it is no faster either: the probe's first ceiling run (before the pairs) drew
  the light state (GPU 4.70 ms) at a tick p50 of 8.46 ms, against 8.32 and 8.42 ms for the off runs at 4.57 and 4.79
  ms; in pair 4 (GPU 5.40 against 5.55 ms) +0.14 ms.
- **Hub: no change** (+0.04 ms on the medians, pairs -0.14 to +0.31 ms, within the spread).
- **Why the skip buys nothing**: the skipped gathers are the player's slices (0.24 to 0.29 ms of CPU in combat),
  which run beside eye R's sun cascade gathers, the longest items, so a render's gathers end no sooner. With the
  ceiling eye R's sun gathers even took longer (combat 0.73 to 0.74 ms of CPU against 0.63 ms, span 0.56 to 0.58
  against 0.51 ms; hub 0.48 to 0.53 against 0.39 ms) [rig; why not known].

**Result: fail.** The pass line was the combat tick 0.4 ms shorter (median, beyond the run spread); the ceiling made
it 0.46 ms longer on the medians (heavier encounter states) and no shorter at matched load, and the hub did not
move. Nothing was built: eye R taking eye L's caster lists for the slices both eyes gather (the player's, a few spot
slices in the hub) cannot win the 0.4 ms even as a bound.

## 4. What this leaves

- The shadow work eye R repeats for casters that do not depend on the eye is the sun's cascades, already shared by
  Shared sun shadows (`perf-sun-cascades.md`, T-234: a kept cascade is neither gathered nor recorded by eye R). The
  rest of eye R's gathering is the player's slices (the player model is placed per render, so they would stay per
  eye anyway) and next to nothing for spot and point lights. Lever 3 of `perf-stereo-cpu.md` is so covered by T-234
  and closed beyond it.
- The per-light setup, 0.10 to 0.16 ms a render, is not worth a lever.
- The atlas pass (0.53 to 0.99 ms a render on one thread, in each eye) is the largest per-eye shadow cost left; what
  eye R records there beyond the sun's cascades is its player slices and a few spot slices. Whether it lies on the
  tick's critical path is not known; a bound for it would be its own spike.
- `r_skipShadows 1` taking GatherShadowCasters to zero and the tick rate 5% up (`perf-stereo-cpu.md`) removes both
  eyes' whole shadow path (every light's gather, both atlas passes, the GPU's shadow draws), not a share eye R could
  drop.
