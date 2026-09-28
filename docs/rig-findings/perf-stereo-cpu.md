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

## 5. Shared culling, tried: no gain

Umbra's per-view query was found and shared between the eyes on a test branch (not merged):

- The kick wrapper (RVA 0x1D19280, `kick(context, idRenderView, origin, axis)`, returns at once for a null
  view) hands the query setup (0x1D18BC0) the view's latched origin (+0x28A64), axis (+0x28A70) and
  projection (+0x29340). The setup builds the Umbra camera, submits the query as up to 16 job slices (the
  job at 0x1D15C90) and clears the context's result arrays; the finish (0x1D1A5A0) waits for the job and
  rebuilds the results from the slices. The context comes from the world by view index, so both eyes use the
  same one. With the default `r_umbraJobKickoff` 2 the kick runs in the screen-views loop (0x1C758F9).
- The test ran eye L's query with one frustum enclosing both eyes (the outermost tangent on each side, the
  apex set back behind the eyes' midpoint just far enough that every side plane passes outside both eyes,
  plus 2% headroom for float rounding) and let eye R skip its query and take eye L's results. Eye R reused
  them on every tick.
- Measured over interleaved pairs (the Release layer, 1280 x 1400 per eye): off 152.8 ticks/s, on 143.1.
  Eye R's render did not get shorter, and the CPU and GPU per tick went up (both eyes draw the wider set).

With the default kickoff the query runs as parallel jobs on otherwise idle workers, off the tick's critical
path. The per-function profile above counts CPU samples, not time on the critical path, so it overstated
this lever. Umbra's cvars (kickoff 0, 1 and 3, the adaptive threshold off, the job split) did not beat the
defaults either, nor did the job system's (`jobs_parkAfterUs` 1000 and 5000, `jobs_numThreads` 6) or
`r_threadedRenderGui` 1 (median 151.5 against 150.8 ticks/s over eight pairs).

## 6. The critical path

The tick is latency-bound: eye R's render takes about 3 ms of wall time and 0.1 ms of CPU on the thread that
starts it, with about 4.8 of 8 cores busy. A second sampling mode finds where the time goes: in each round it
samples every busy thread and counts it idle (the job workers' spin loop, or a thread inside a kernel wait)
or working. Rounds with exactly one working thread are the serial stretches, and that thread's function is on
the critical path.

- 21 to 33% of the rounds have one working thread.
- The serial stretches are mostly the recording and submission of each eye's command buffers: the Vulkan
  driver 6 to 13% of all rounds, called from the render graph's passes (0x1C65AF0, 0x1C32860, 0x1C33320) and
  the per-object draw path (0x1C2C470); win32u 4 to 5% (the kernel's queue submits and sync-object waits and
  signals); and 0x1C497A0 3 to 6.5%, which walks a global resource list for every pass's pipeline barriers
  (0x1C65AF0 is between the "Pre pipeline" and "Post pipeline" markers).
- The layer's own share of the serial stretches is about 1% with a Release build (the present's copy
  submission). A Debug build of the layer adds several percent here, and is also about 5% slower overall:
  measure with the Release layer.
- The serial work runs on whichever job worker is free; no single thread owns it.

## 7. What is left

- **Both eyes in one render.** The engine can process several render views at once
  (`r_allowParallelViewProcessing`), but its per-view storage is sized for one view (`stereo-reentry.md`, E4),
  so this needs the device context's per-view slots enlarged: about 30 renderer sites index them.
- **Overlap eye R with the next tick's game frame.** The game frame outside the renders is small (a stereo
  render costs about what a whole mono frame costs), so this gains little.
- **Share eye L's shadow casters with eye R** (lever 3): worth measuring the same way before building, since
  shadow gathering also runs as parallel jobs.
- **The layer's per-present copy**: record the copy command buffers once per ring slot and swapchain image
  instead of for every present (about 1% of the serial stretches).
- Simulator runs of the same build land in one of two groups (about 134 and 150 ticks/s, with more CPU and
  GPU work per tick in the slow group, from the same view path). Compare interleaved pairs, at least four of
  each, by median.
