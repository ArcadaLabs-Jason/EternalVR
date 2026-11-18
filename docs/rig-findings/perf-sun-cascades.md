# The sun's shadow cascades per eye: what eye R's cost, and eye R on eye L's (`ETERNALVR_SHARED_SUN`)

Under Route S the stereo tick is a serial chain, eye L's render and then eye R's, each about 4 ms at 1280 x 1400
on the test PC. `r_skipShadows 1` shortens the combat tick by 2.2 ms, and the shadow reuse spike (branch
`shadow-reuse`, its `docs/rig-findings/perf-eye-r-reuse.md` section 9) found that eye R lists almost none of the
spot and point light slices eye L renders: what it renders of its own is the sun's cascades and the player's
slices. This page reads where the sun's cascades are placed and rendered per eye (section 1), measures what eye R's
cost (section 2) and what eye R saves by shading with eye L's (section 3). Steam build 25216728, RVAs in that
build.

Tags: **[code]** read in this build's code, **[rig]** measured on the test PC, **[inferred]** a reading the rig
has not confirmed.

## 1. Where the sun's cascades are placed and rendered

- **Rendered from each render's shadow setup.** The lighting job of every view (0x1CF0240) calls the per-light
  record function 0x1CFE2B0 for each shadowed light; for the sun (light type 2) it walks four cascades, two slices
  each (even the world's casters, odd the player's). A slice it redraws goes on the view's render list at
  `test bl, 6` (0x1CFF357) and is then gathered (the gather jobs at view context + 0x3E50) and rendered; the
  render-list job 0x1CA2610 copies the slice's projection into its cache entry. Under Route S both eyes render view
  slot 0, so both eyes' setups use the same shadow cache and the same keys. [code]
- **Placed around the view's origin, not fitted to the frustum.** For a sun cascade with a cache entry the setup
  compares the view's origin (render view + 0x2940C, 0x2941C, 0x2942C) with the centre the entry keeps (+0x40,
  +0x48), across the sun's direction (light + 0xB4..0xBC), against `r_shadowParallelMoveMargin` (0.05, at
  0x1CFEF0B) times the cascade's radius (from a parameter block's +0x8C, +0x90 and +0x94, the cascade index as
  the exponent of a power; 0x1CFEEDE to 0x1CFEF03). Within the margin the cascade keeps its place; past it every
  level of the cascade is released (0x1CFF16A) and its static part marked to build. The eyes are 6.4 cm apart, a
  small share of a margin of 5% of a cascade's radius, so both eyes almost always place a cascade at the same
  centre. Nothing in this placement reads the view's direction or field of view. [code; the radius values not
  read]
- **Which cascades render.** `r_amortizedSunCascades` updates cascade 0 in every render and one of cascades 1 to 3
  in turn (`shadow-pop.md` section 1). The cascades' stale limits (`r_shadowParallelMaxStaleFrames` 0,0,1,2) are
  0 for cascades 0 and 1, so at the stale check (`cmp edx, eax`, 0x1CFF33E) a due cascade's counter is always over
  its limit: the setup releases the entry (0x1CFF34F) and lists the slice again without one, and the whole cascade
  is rendered, its static casters too. [code; rig: every sun slice either eye lists was released at the check,
  section 2]
- **Sampled from the cache entry.** Each view's light parameter build (0x1CF0B80) takes a cascade's shadow rect and
  matrix from the cache entry it looks up itself (`VR_STEREO.md`, view 1's shadow setup), so a cascade eye L
  rendered is sampled by eye R with the projection eye L rendered it with. Parallel Eye Rendering's view 1 already
  shades with view 0's sun cascades this way. [code]

So under Route S eye R renders cascade 0 and a far cascade again in each tick, for the same casters (no game tick
between the eyes), the same sun and, almost always, the same centre.

## 2. What eye R's sun cascades cost (the ceiling)

Rig: the test PC (RTX 3080 Ti, i7-9700K), OpenXR-Simulator at 90 Hz, the Release layer of this branch (main
aa899b47 plus the hooks), the bench routes (`bench-routes.md`) at the launcher's defaults, 1280 x 1400, pacing off
(`ETERNALVR_PACE=off`), CPU and GPU timing on. The raw runs stay on the test PC.

**Count per eye and tick** (`ETERNALVR_SHARED_SUN=count`; the hub's from the shared runs' eye R checks, which see
what eye R would list) [rig]:

| Route | Sun slices eye L lists, cascade 0 / 1 / 2 / 3 | Eye R | Player slices, each eye |
|---|---|---|---|
| Combat | 1.00 / 0.33 / 0 / 0 | 1.00 / 0.33 / 0 / 0 | 1.00 |
| Outdoor | 1.00 / 0.33 / 0.33 / 0.33 | 1.00 / 0.33 / 0.33 / 0.33 | 2.00 |
| Hub | 1.00 / 0.33 / 0.33 / 0.33 | 1.00 / 0.33 / 0.33 / 0.33 | 1.00 |

Every sun slice either eye lists was released at the stale check first (listed without an entry), so each one is
a whole cascade rendered again. The casters per cascade are not counted (no counter reached them without a new
hook in the gather jobs). The layer's GPU timing brackets each eye's submits, not the shadow passes, so eye R's sun
cascade GPU time is read as the drop in eye R's GPU busy time with the ceiling.

**The ceiling** (`ETERNALVR_SHARED_SUN=ceiling`: eye R renders no sun world slice and shades with eye L's). Combat:
the three-route suites interleaved three times plus three interleaved combat-only pairs (n = 6 each); outdoor and
hub: the three suites (n = 3). Medians; tick p50 is the stereo tick's CPU time, eye R and outside the CPU split's
wall times, GPU the tick's busy p50 and each eye's, missed the display periods without a new image [rig]:

| Route | Mode | Ticks/s | Tick p50 ms | Eye R ms | Outside ms | GPU ms | GPU eye L / R ms | Missed % |
|---|---|---|---|---|---|---|---|---|
| Combat | off | 112.8 | 8.72 | 4.08 | 4.78 | 5.39 | 2.61 / 2.52 | 0.7 |
| | ceiling | 123.2 | 7.92 | 4.41 | 3.69 | 4.81 | 2.42 / 2.05 | 0.5 |
| Outdoor | off | 109.6 | 9.11 | 4.30 | 4.81 | 7.65 | 3.98 / 3.31 | 0.2 |
| | ceiling | 115.1 | 8.65 | 4.26 | 4.42 | 7.30 | 3.88 / 2.90 | 0.3 |
| Hub | off | 138.4 | 7.14 | 3.35 | 3.87 | 5.77 | 2.74 / 2.53 | 0.2 |
| | ceiling | 143.0 | 6.98 | 3.35 | 3.64 | 5.68 | 2.85 / 2.34 | 0.1 |

- **Ceiling: 0.80 ms of the combat tick** (pairs 0.17 to 1.49 ms; the combat route is two-state with the encounter:
  GPU per tick about 4.7 or 5.7 ms and more), 0.46 ms outdoors, 0.16 ms in the hub; eye R's GPU 0.2 to 0.5 ms less.
- The saving shows in the CPU split's "outside" (game frame and eye L's views), not in eye R's render wall, which
  rises 0.3 ms in combat: eye R's shadow setup and gather jobs run in the render DAG beside the frame-end jobs, so
  their time is not where the split puts eye R's render [inferred].
- **GATE: passed** (0.80 ms against the 0.4 ms bar).

## 3. Eye R on eye L's cascades (`ETERNALVR_SHARED_SUN=1`)

The cascades are placed around the view's origin (section 1), so a cascade fitted once to a frustum around both
eyes, as first planned, buys nothing here: both eyes already place a cascade at the same centre, and eye R needs
only to sample eye L's. The switch as first measured here (sections 3 and 4) kept, at the stale check, the entry of
each sun world slice whose counter was 1 (eye L rendered it since eye R last checked it: cascade 0 eye L's of the
same tick, a far cascade eye L's of its last turn). Review found that a counter set to 0 by an allocation, or kept at
0 by eye R itself, reads 1 again in a tick eye L did not render the slice, so staleness was not bounded; the switch
now also needs eye L's listing of the slice stamped with the same tick (section 6), and eye R never shades with a
cascade older than eye L's of the same tick. Eye R then neither releases nor renders it; the player's slices and
every other light stay as they are. The design, the hooks and the log are in `VR_STEREO.md`, "Shared sun
cascades". [code]

**What eye R and eye L list** (shared as first measured, per tick) [rig]: eye R lists no sun slice on any route;
every one of its sun checks had the counter at 1 and was kept (combat 1.33 a tick, hub 2.00). Eye L lists what it
did, except in the combat arena, where it renders cascade 1 twice in three ticks instead of once (1.67 sun slices a
tick instead of 1.33): with eye R not rendering cascade 1 on its turn, the light's dirty bit for it (moving casters)
is still set at eye L's next setup, which renders it then [inferred from the counts; the bit at light + 0x165, read
at 0x1CFF24A]. So the far cascades are updated as often as before (0.67 a tick), by eye L alone, and cascade 0 once
a tick instead of twice.

**A/B** (off against `1`, four interleaved rounds of bench-combat, bench-hub and bench-combat at 2048 x 2208,
pacing off; medians; columns as in section 2) [rig]:

| Route, size | Mode | Ticks/s | Tick p50 ms | Eye R ms | Outside ms | GPU ms | GPU eye L / R ms | Missed % |
|---|---|---|---|---|---|---|---|---|
| Combat 1280x1400 | off | 110.2 | 8.92 | 4.17 | 4.91 | 5.85 | 2.85 / 2.76 | 0.7 |
| | 1 | 116.4 | 8.39 | 4.55 | 4.02 | 5.49 | 2.94 / 2.33 | 1.0 |
| Hub 1280x1400 | off | 139.1 | 7.16 | 3.33 | 3.85 | 5.78 | 2.71 / 2.56 | 0.1 |
| | 1 | 143.2 | 6.93 | 3.33 | 3.65 | 5.69 | 2.89 / 2.33 | 0.2 |
| Combat 2048x2208 | off | 94.0 | 10.46 | 4.78 | 6.00 | 9.36 | 5.29 / 4.06 | 6.4 |
| | 1 | 87.2 | 11.31 | 5.10 | 6.42 | 10.25 | 6.19 / 4.12 | 10.2 |

- **Combat 1280 x 1400: the tick p50 0.53 ms shorter** (8.92 to 8.39 ms; rounds -0.89, -0.32, -0.39, -0.43 ms),
  5.6% more ticks a second. **Hub: 0.23 ms shorter** (rounds -0.36, +0.07, -0.10, -0.60 ms), not slower.
- **Combat 2048 x 2208: no gain.** This route is two-state too (GPU per tick about 10.4 ms with 85 to 87 ticks a
  second, or about 8.2 ms with 100); off drew the lighter state twice, `1` once, which is all of the medians'
  difference. Within a state: heavy, off 11.46 / 11.32 ms against 11.38 / 11.52 / 11.24 ms; light, off 9.60 / 9.52
  against 9.83 ms (one run). The GPU sets the tick there (86 to 93% of it), and eye R's GPU time falls about
  0.4 ms while eye L's rises about as much (eye L's extra cascade 1 renders and the frames' GPU split; the
  light-state run's eye L 0.46 ms up is one run).
- Missed display periods at 1280 x 1400 combat: 0.6 to 1.1% with `1` against 0.5 to 0.8% off, while the game
  makes 6% more ticks; not chased (the simulator skipping periods while the GPU is busy is the likely reason, as
  seen with Parallel Eye Rendering's higher tick rates) [inferred].
- Eye R's render wall rises and "outside" falls by more, as with the ceiling (section 2).

## 4. Eye captures

The QA still scenes (`sun-eyes-combat`, `-hub`, `-outdoor`: the combat arena, the hub's bridge, Exultia's start; the
head held still, a capture every 45 pairs, 130 to 160 pairs a run), compared with the means of absolute
differences (0 to 255) over pairs after the first five [rig]:

- **Both eyes on one view** (`ETERNALVR_STEREO_SAME_VIEW=1`), eye L against eye R, median over pairs: combat off 1.57,
  `1` 1.58; outdoor 1.82 and 1.82; hub off 2.29 / 2.47 / 2.40, `1` 2.42 / 2.31 / 2.52 (three runs each). In the
  hub, one run of three each way has eye R even and two have it 0.3 to 0.4 darker over the upper image (off runs 2
  and 3, `1` runs 1 and 3; the window band with the Earth and the hologram globe 2.7 to 3.6): a state of the run,
  not of the switch (the shadow reuse spike saw the same in its hub runs). No difference image shows a shadow, a
  cascade edge or a seam.
- **Per-eye views**, the median image of each eye over a run, run against run: hub eye R off/off 0.48, `1`/`1`
  0.50, off/`1` 0.51 and 0.46 (eye L 0.42, 0.50, 0.57, 0.58); outdoor (the view shifts a little from run to run)
  eye R off/off 5.34, `1`/`1` 8.63, off/`1` 6.64 and 6.72 (eye L 4.67, 8.31, 6.02, 6.29). Eye R's change with the
  switch is within its run-to-run spread, as eye L's is. Crops of eye R's shadowed ground and walls off and on
  look the same; edges no softer. The combat arena's per-eye captures are dominated by the fight and are compared
  only on one view.

## 5. Result and what is left

**PASS** on the rig (with the first rule; the stricter rule of section 6 keeps the gain): the combat tick at 1280 x
1400 0.53 ms shorter (bar 0.4 ms), the hub 0.23 ms shorter, no shadow difference between the eyes and none in eye R
between off and on beyond the runs' own spread. At 2048 x 2208 the GPU sets the tick and the switch gains nothing.
The switch stays off by default; testers turn it on with the launcher's "Shared sun shadows (testing)" on the
Advanced tab (`DECISIONS.md` T-234).

- Not seen on the rig: the simulator has no headset, so a shadow that differs between the eyes by a little (the
  amount the shading could notice and a flat comparison not) needs a headset look, in the sun: Exultia, Arc
  Complex's yards, Cultist Base's arena, with a quick turn and a walk (a cascade re-placed by eye R's origin
  alone keeps eye L's place for that tick, rendered in the same tick: section 6).
- A PC where the GPU sets the tick gains nothing (as at 2048 x 2208 here).

## 6. Eye L's stamp of the tick (the review round)

**Why.** The first rule kept a slice whenever its entry's counter read 1 at eye R's check. The counter at entry +
0x88 is set to 0 by the render-list job 0x1CA2610 (stores at 0x1CA2EF3 and 0x1CA2F96) and by the atlas allocation
0x1D05220 (stores at 0x1D0537A and 0x1D05673) [disasm], and eye R set it to 0 itself after each keep. So a cascade
eye L did not render in a tick (a light that left its list, a skipped render, an entry fresh from an allocation or a
map load) could still read 1 at eye R's check, and eye R kept it: staleness was not bounded. [code]

**Now.** At its listing (`test bl, 6`), eye L stamps each sun world slice it lists with the tick, keyed by the light
and the slice (eye L's check releases the cascade's entry and its listing allocates a new one, so the entry differs
between the eyes: every sun slice either eye lists was released at the check, section 2). Eye R keeps a slice only
when eye L stamped it in the same tick and its counter is 1. **Eye R never shades with a cascade older than eye L's
of the same tick: the worst-case staleness is zero ticks.** The stamps are emptied when the game frame count goes
back (a map load) or the multiplayer guard closes; the eye and tick come from the render's tag looked up on every
call (the per-thread cache is gone). [code; `shared_sun_tests.cpp` runs a tick loop where eye L skips some ticks]

**What that changes** [rig, the shared-sun QA run and two combat rounds]: eye R keeps cascade 0 (1.00 a tick) and
renders its far cascade turns itself again (0.33 a tick in combat); eye L lists what it lists with the switch off
(1.33 a tick in combat). The extra cascade 1 renders eye L made under the first rule (section 3, [inferred]) are
gone: the switch now saves one cascade 0 render a tick and moves nothing to eye L.

**Re-measure** (bench-combat at 1280 x 1400, pacing off, two interleaved pairs; the rig had just finished another
job's runs, and both off runs were slower than section 3's) [rig]:

| Mode | Ticks/s | Tick p50 ms | Eye R ms | Outside ms | GPU ms | GPU eye L / R ms | Missed % |
|---|---|---|---|---|---|---|---|
| off | 108.6 / 107.7 | 9.01 / 9.07 | 4.23 / 4.24 | 4.97 / 5.02 | 4.67 / 5.34 | 2.23 / 2.22, 2.59 / 2.51 | 1.4 / 1.5 |
| 1 | 111.6 / 119.3 | 8.84 / 8.31 | 4.45 / 4.13 | 4.50 / 4.23 | 4.61 / 4.49 | 2.27 / 2.02, 2.24 / 1.97 | 1.0 / 0.4 |

The tick p50 is 0.17 and 0.76 ms shorter in the two rounds (0.47 ms on the medians), against 0.53 ms in section 3:
the gain stays, within the route's spread. Eye captures on one view with the stricter rule, eye L against eye R:
combat 1.70 and 1.64 (two runs) against off 1.57 and 1.58 and the first rule's 1.58; outdoor 1.82 against
1.82 and 1.82. The combat difference maps are even over the whole image (eye R 0.2 to 0.3 brighter or darker overall
in either direction, run by run, in every mode), with no shadow, cascade edge or seam; the 0.1 higher median is
two runs against two and not shaped like a shadow [rig; a larger sample would tell whether it is real]. The
`shared-sun` QA scenario passes with its checks for the stricter rule (eye R keeps cascade 0 in 90% or more of the
ticks of every 10 s line, lists fewer sun slices than eye L, leaves nothing off at the listing).

## 7. Stamps cleared only on a real restart (follow-up)

**Change.** The stamp table was emptied by any stamp with a tick below the highest one, so a worker still listing
tick N while another already stamped N+1 cost the whole table: eye R then rendered its own cascades for that tick.
It is now emptied only when the tick falls back by more than 16 (a map load); a stamp a few ticks behind only
stamps its own tick, and the per-tick match keeps it harmless to the rest. Eye L's stamps are also made only in
`shared` mode (`count` and `ceiling` never read them). Unit tests cover the late stamp (N+1 then N keeps both), the
restart, and two keys in one of the 256 slots: the overwritten one reads as not stamped, so eye R renders it. [code]

**Measured** (bench-combat at 1280 x 1400, pacing off, `ETERNALVR_SHARED_SUN=1`, three interleaved pairs, the build
of the change against the build before it) [rig]:

| Build | Tick p50 ms | GPU p50 ms | Eye R checks kept / tick |
|---|---|---|---|
| before | 8.15 / 8.70 / 8.92 | 4.44 / 5.83 / 6.14 | 1.000 / 1.000 / 1.000 |
| after | 8.05 / 8.04 / 7.98 | 4.52 / 4.50 / 4.45 | 1.000 / 1.000 / 1.000 |

The keeps are the same: eye R keeps cascade 0 once in every tick (69% of its sun checks, the rest being the far
cascade turns it renders itself) in all six runs, so out-of-order stamps did not happen in this scene and there was
no saving to win back here; the change is a guard for a rarer ordering. The tick p50 differences are the rig's
drift between runs (the before runs slowed down from the second pair on), not the change. The `shared-sun` QA
scenario passes on the final build.
