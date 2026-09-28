# Where a stereo tick's CPU goes, per function, and what can be shared between the eyes

The stereo tick is a serial chain: the game frame and eye L's render, then eye R's render inside eye L's
frame-end job (`perf-baselines.md`). This page adds a per-function CPU profile of a stereo run against a mono
run, to find the work that eye R repeats although it does not depend on the eye, and ranks the levers. Second
test PC (RTX 3080 Ti, i7-9700K, 8 cores), OpenXR-Simulator, 1280 x 1400 per eye, e1m1 intro with a head sway,
main fc8ee49, Steam build 25216728. RVAs are in that build.

## 1. Method

An external sampling profiler (a small Python script outside the repo): it takes the game threads that use more
than 5% of a core (11 in both runs, 8 of them the job workers at 35 to 52% each), suspends each one in turn in
a tight loop, reads its RIP, resumes it, and counts samples per function start from the exe's unwind table.
There are 20 s per run after 40 s of warm-up: 18,524 samples in the exe for stereo, 14,213 for mono. Both runs
were CPU-bound and uncapped. Stereo made 143 ticks/s (286 renders/s), mono 275 frames/s (sampled rates, a
little under the unsampled ones).

## 2. Results

Almost all the CPU work is per render: a stereo render costs about what a whole mono frame costs, and the game
logic outside the renders is small. Per tick, stereo takes 2.5 times the mono frame's samples in the exe (6.48
against 2.58 per tick at the sampling rate), so a stereo render also costs about 25% more than a mono one.

| Function (RVA) | What | Stereo, samples per tick | Mono, per frame | Ratio |
|---|---|---|---|---|
| 0x18772C0 | job worker loop (`%s Worker %d`): spinning while it waits | 1.089 | 0.431 | 2.5 |
| 0x227FA40 and its callees (0x227FB20, 0x2283840, 0x2282820, 0x2285DD0, 0x2286FF0, 0x2283150, 0x227DE40) | unnamed float-heavy work under 0x2271C10 / 0x1D1A020 | about 0.66 | about 0.11 | about 6 |
| 0x1CA3EE0 | GatherShadowCasters (per light and slice) | 0.478 | 0.199 | 2.4 |
| 0x1C497A0 | unnamed, under 0x1C65AF0 | 0.315 | 0.231 | 1.4 |
| 0x1D476C0 family (0x1D37740, 0x1D383B0, 0x1D48190) | unnamed, job entries 0x1D46F10 / 0x1D47630 | about 0.31 | about 0.04 | 7 to 9 |
| 0x1C2C470 | unnamed, under 0x1C32750 | 0.155 | 0.038 | 4.0 |
| 0x1C18840 | descriptor set writes | 0.059 | 0.011 | 5.4 |
| 0x1C8B930, 0x1C86BA0 | render-model work next to the particle / flare / beam / ribbon table; 0x1C86BA0 is called from the render graph's passes | 0.091 | 0.001 | about 100 |

Ratios well above 2 are work that a stereo render does more of than a mono frame: each eye's view is wider
than the mono window's (the headset's FOV), so more objects, lights and shadow casters survive the culling,
and some passes may use the symmetric FOV that encloses both eyes rather than the eye's own frustum (the view
latch uses the eye's explicit matrix, but not every pass reads the latched one; `stereo-bin-tiles.md`). The
about-100 ratio is work mono does not do at all.

## 3. Levers, ranked

1. **Overlap the eyes (up to one eye's render per tick).** The workers spin 17% of the exe's samples while they
   wait on the serial chain, and only about 4 of 8 cores are busy. Starting eye R's frontend jobs while eye L's
   backend runs would turn the spin into work. This is the biggest lever and the riskiest one: the renderer's
   per-render state (the render counter, the per-render rings of `stereo-object-motion.md`, the device
   context's ping-pong images of `stereo-scatter.md`) assumes one render at a time.
2. **Check what each eye is culled with.** Every per-view cost above scales with what the culling lets through.
   If any culling or occlusion pass takes the symmetric enclosing FOV instead of the eye's own asymmetric
   frustum, it lets through the inner side of the other eye's view as well. Culling with the eye's own frustum
   (as the binning now does, `stereo-bin-tiles.md`) could cut the 6x to 9x families. First find which passes
   read the idRenderView's FOV (the MVP_CULLING / DEPTH_OCCLUSION passes are in the render graph's pass table
   at 0x39AB158).
3. **Share eye L's shadow casters with eye R.** GatherShadowCasters runs once per render. For lights whose
   shadow maps do not depend on the view (every local light; the sun's cascades only through the frustum, and
   the eyes are 64 mm apart), eye R could take eye L's caster lists and even its shadow maps: a CPU and GPU win.
4. **The stereo-only particle-model work (0x1C86BA0 / 0x1C8B930)**: find which Route S setting starts it (a
   forced cvar or a per-view flag) and whether eye R needs it.

## 4. Naming the families by switching subsystems off

Stereo runs with one cvar each (`ETERNALVR_DEBUG_CVARS`), same view and sampling, samples per tick:

| Family | Stereo | `r_useUmbraCulling 0` | `r_skipShadows 1` | `r_skipGPUParticles 1` | Mono |
|---|---|---|---|---|---|
| ticks/s | 143 | 49.4 | 150.6 | 148.4 | 275 (frames) |
| worker spin 0x18772C0 | 1.089 | 1.941 | 1.344 | 1.253 | 0.431 |
| 0x227FA40 family | 0.662 | **0.000** | 0.835 | 0.781 | 0.110 |
| GatherShadowCasters 0x1CA3EE0 | 0.478 | 0.635 | **0.000** | 0.311 | 0.199 |
| 0x1C497A0 | 0.315 | 0.288 | 0.569 | 0.311 | 0.231 |
| 0x1D476C0 family | 0.317 | 0.411 | 0.376 | 0.320 | 0.044 |
| 0x1C2C470 | 0.155 | 1.407 | 0.131 | 0.176 | 0.038 |
| stereo-only particle-model 0x1C86BA0 / 0x1C8B930 | 0.091 | 0.122 | 0.111 | 0.098 | 0.001 |
| all of the exe | 6.477 | 20.769 | 7.379 | 6.785 | 2.584 |

- **The 0x227FA40 family is Umbra's occlusion culling.** It goes to zero with Umbra off, and it costs three
  times per stereo render what it costs per mono frame. Without Umbra everything is drawn: the tick rate drops
  to a third and 0x1C2C470 (per drawn object) grows ninefold. So culling is essential, but the two eyes, 64 mm
  apart, see almost the same set. **Lever: cull once per tick with a frustum that encloses both eyes and give
  eye R eye L's visible set** (the usual shared VR culling), saving about 5% of the tick's CPU plus the per-view
  follow-on work.
- Skipping shadows gains about 5% of the tick rate (GatherShadowCasters goes to zero); skipping the GPU
  particles, about 4%.
- The 0x1D476C0 family, 7 times hotter per tick than mono, does not change with any of the three and is still
  to be named.

## 5. Next steps

- Name the 0x1D476C0 family (more A/Bs: `r_skipGPUTriangleCulling`, `r_skipModelGPUCulling`, streaming,
  decals), and the stereo-only particle-model work.
- Find Umbra's per-view query entry (under 0x2271C10 / 0x1D1A020) and what it is handed (a frustum or a view),
  for the shared-culling lever.
- Prototype lever 3 behind a switch, measuring the tick with `ETERNALVR_CPU_TIMING=1` and GPU timing, and do
  the ghost and shadow checks for eye R.
- Plan lever 1 on paper against the per-render state list before any code.
