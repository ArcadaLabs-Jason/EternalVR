# Eye R reuse: the view-independent work eye R repeats, and what it can take from eye L

Static pass for the CPU project (the owner: "It's very important that we support as many fans as possible,
not just those with strong cpus"). Under Route S the engine renders each game tick twice, eye L then eye R,
and eye R repeats every per-render stage, including work whose result does not depend on the eye. This page
lists that work, ranks it, and implements the top candidate behind a flag that is off by default. Retail
`DOOMEternalx64vk.exe`, Steam build 25216728; all addresses are RVAs in that build. The game was not run for
this page: every saving below is an estimate for the rig to confirm (section 6).

Tags: **[static-verified]** read in the code or data of this build, **[inferred]** a reading the rig has to
confirm, **[measured]** from the profiles in `perf-stereo-cpu.md`.

## 1. Where eye R's work runs

The per-render work is in three places, and only one of them is certainly on the tick's critical path:

1. **The frontend chain** (render-frame job, world views, the Umbra kick): eye R's runs nested in eye L's
   frame-end job (`VR_STEREO.md`).
2. **The render DAG** the frame-end job builds (0x1CDAFE0, mode 1 through 0x1CDBAE0): the per-view job
   chains (0x1CDCD90 -> 0x1C5CD00 -> 0x1C5CEA0: lighting and shadow setup 0x1CFA640, culling, the shadow
   gather and render-list jobs) and the render thread's jobs from the table at 0x39AB2B0 (0x1CD6F80,
   0x1CD7020, the model commit 0x1C8A180, 0x1CD8360, 0x1CD70B0, the streamer 0x1CD7FA0, 0x1CD8060,
   0x1CD8330). All of them are predecessors of the render thread body 0x1CD8380 (the job at
   `[renderThread + 0x450]`), which records and submits the command buffers. [static-verified]
3. **The render thread body** (0x1CD8380) and its passes: the serial stretches of `perf-stereo-cpu.md`
   section 6 (driver, queue submits, the barrier walk 0x1C497A0). [measured]

So work in (2) is on the critical path only when it is the longest chain before the body. The Umbra lesson
(`perf-stereo-cpu.md` section 5: shared culling, no gain) applies to everything below: CPU samples are not
critical-path time, and each lever needs its A/B on the rig.

## 2. Ranked candidates

| # | Work eye R repeats | Functions (RVA) | Where | Eye R's share (samples per tick, of 6.48) | Estimated gain per tick | Risk | Hook |
|---|---|---|---|---|---|---|---|
| 1 | **Dynamic shadow slices**: gather, atlas allocation, recording and GPU draw of every slice with a moving caster or light | decision 0x1CFE2B0 (check at 0x1CFF33E), lighting job 0x1CF0240, gather 0x1CA3EE0 (job 0x1CEECB0), shadow render-list job 0x1CA2610 | view chains (2); the draws in the passes (3) | part of GatherShadowCasters' 0.478 plus recording; `r_skipShadows 1` (both eyes, all lights) gave +5.3% ticks | 1 to 3% ticks on `e1m1_intro`, more in arenas with shadowed lights over moving demons; eye R's shadow GPU time too | low for spot and point lights (same tick, same casters); medium for sun cascades (fit to the view) | **implemented**: `ETERNALVR_REUSE_SHADOWS` (section 4) |
| 2 | **Image streamer update** (`idImageStreamerLocal`, is_* cvars) | 0x1D476C0 and callees 0x1D37740, 0x1D383B0, 0x1D48190; vtable slot 0x168 = 0x1D47630; DAG job 0x1CD7FA0 (slot 0x39AB310) | render DAG (2), predecessor of the body | about 0.155 per render (the "0x1D476C0 family", 7 to 9x mono per tick) | up to 2.4% of the exe's CPU; ticks only if it is the longest chain before the body | low: eye L's feedback covers almost all of eye R's view | plan in section 5.1 |
| 3 | **Surface render-prog data rebuild** of entities eye R's world frame cleans up | list 0x669BE60 (0x1C8AA80), job 0x1C8A180 -> 0x1C8CDE0 -> 0x1C8B930 | render DAG (2) and frame-end job | 0.091 with 0x1C86BA0 (the "stereo-only particle-model work", about 100x mono) | about 1.4% of the exe's CPU for anti-aliasing Off; already gone with anti-aliasing on | trades CPU for GPU (motion-vector pre-pass) | exists: the moved-flag hook (section 5.2) |
| 4 | **GPU particle simulation** | per-view flag `renderContext + 0x4D9C93` from `r_skipGPUParticles` (0x1C5E17D in the per-view setup 0x1C5DFD0); simulation bytecode `COMMAND_SIM_*` | view chains and passes | `r_skipGPUParticles 1` (sim and draws, both eyes) gave +3.8% ticks | below 2% (only the simulation, one eye) | medium: emission scales with the view distance (`r_particlesEmissionScale_*`); a time step of 0 in eye R has to be confirmed | not located (section 5.3) |
| 5 | Sun cascades (the shadow item for parallel lights) | as 1, light type 2 | as 1 | in 1 | part of 1 | medium: a cascade fitted to eye L's frustum may not cover eye R's outer edge | in item 1's `all` mode |
| 6 | Umbra occlusion query | kick 0x1D19280, setup 0x1D18BC0, finish 0x1D1A5A0 | parallel jobs | 0.66 (3 queries per render) | none: tried, eye R got slower (`perf-stereo-cpu.md` section 5) | | closed |

Not reusable, or not worth it:

- **Light, decal and probe binning, cluster setup** (`CLUSTER_SETUP_TILES`, `CLUSTER_SETUP_SHADOWS`):
  per-view by construction (each eye's own tile grid, `stereo-bin-tiles.md`).
- **Skinning joints and model matrices**: uploaded per render into the object-transform ring
  (`stereo-object-motion.md`); each render needs its own slot for motion vectors. Eye R could read eye L's
  current slot (the upload is 0x1C00B40), but the ring's slot rules exist because sharing a slot strobed in
  the headset. Not before the ring is rethought.
- **World GUIs**: eye R already draws eye L's per-frame GUI commit (`stereo-world-gui.md`).
- **Ray-traced acceleration structures** (`BUILD_ACCELERATION_STRUCTURES`, the render context phase table
  at 0x39AB150): view-independent, but only with ray tracing on, which is off on the CPUs this project
  targets. Worth a look only if RT in VR becomes a goal.

## 3. Evidence

### 3.1 Shadows

- **The decision.** The lighting job 0x1CF0240 (in the view chain, job slot 0x39AD428, built by 0x1CFA640)
  calls 0x1CFE2B0 once per visible shadowed light, which walks the light's slices (two per cube face or
  cascade, `r12d`; odd slices are the player's) at the mip it wants (`r14d`). Per slice it looks up the
  cache entry (0x1D05A40, keyed by light, slice and mip) and sets flags in `bl`: 2 dynamic (a moving caster,
  a moving light, the entry's "has dynamic casters" byte `+0x85`, or the light's dirty mask `+0x165`), 4
  static part to build. For a dynamic slice with an entry it raises the entry's counter (`[r13 + 0x88]`),
  compares it with the stale threshold (`cmp edx, eax` at 0x1CFF33E) and, above it, releases the entry
  (0x1D05BF0) so the slice is allocated again. Then `test bl, 6` puts the slice on the view's render list
  (stride 0x38). [static-verified]
- **The threshold is a budget rule, not a cadence.** The tables it reads are parsed from
  `r_shadowMaxStaleFrames` ("0,0,1,2,2", per mip, at 0x39AD4D8) and `r_shadowParallelMaxStaleFrames`
  ("0,0,1,2", per cascade, at 0x39AD4F0) by 0x1D04C30. After the decisions, 0x1CF0240 sorts the list and cuts
  it at `r_shadowSoftLimitTriangles` (6144 kilo-triangles) and `r_shadowHardLimitTriangles` (65536); a
  slice past its threshold is marked must-render (bit 7) and survives the cut. Everything left on the list is
  allocated if needed, gathered (the gather jobs at view context + 0x3E50, one per listed slice with `flags &
  6`) and rendered; the render-list job 0x1CA2610 then sets each listed entry's counter back to 0 and copies
  the slice's projection into the entry (0x64 bytes). So below the triangle budget **every dynamic slice is
  rendered in every render**, in both eyes. [static-verified] **Corrected by the rig (section 9):** under
  Route S eye R lists far fewer slices than eye L (combat: 2.7 a tick against 4.0, of them 0.07 spot and no
  point light slice), and `r13` at the check is the face's level-5 entry (the key of 0x1D05F20 with its
  third argument 1), so a slice without one never reaches the check (the `test r13, r13` at 0x1CFF293
  jumps straight to the listing).
- **Eye R's slices are eye L's.** The casters do not move between eye L's render and eye R's (no game tick in
  between); a spot or point light's shadow projection does not depend on the camera. The mip does (projected
  size), but a different mip is a different cache entry, and the lookup then finds none. [static-verified for
  the keys; inferred for the content]
- **Unlisted slices are sampled from their entries.** A static slice whose entry is valid never goes on the
  list (`test bl, 2` fails, the LOD check keeps it), yet it is sampled: the shading takes the atlas rectangle
  and projection from the cache entry. A dynamic slice left off eye R's list is therefore sampled exactly as
  eye L left it. [static-verified for the list; inferred for the sampling]
- **Cost bound.** `r_skipShadows 1` (all shadows, both eyes) raised the tick rate from 143 to 150.6 and took
  GatherShadowCasters (0.478 samples per tick) to zero (`perf-stereo-cpu.md` section 4). Eye R's dynamic
  slices are a part of that: static slices are cached already, and sun cascades stay per eye by default.
  [measured bound; inferred share]

### 3.2 Image streamer

- 0x1D476C0 reads `is_poolSize`, `is_readAheadQueueLength`, `is_maxAllocationsPerFrame`,
  `is_cacheGreedily` and friends: it is the streamer's update. It runs from vtable slot 0x168 of
  `idImageStreamerLocal` (vtable 0x2EC1DD8, entry 0x1D47630, which skips it when `is_update` is 0), called
  through the global at 0x39B3DB0 by the render DAG job 0x1CD7FA0 (slot 0x39AB310), and inline in the
  non-job paths (0x1CDB236, 0x1CCA7D7). [static-verified]
- In the DAG (0x1CDBAE0, edges through the job manager's slot 0x30, predecessor in `rdx`, successor in
  `r8`): 0x1CD70B0 -> streamer -> render body 0x1CD8380, and streamer -> 0x1CD8060 -> 0x1CD8330 -> body. It
  runs beside the view chains; it delays the body only if it outlasts them. [static-verified edges; inferred
  timing]
- It costs 3.6 to 4.5 times a mono frame's per render: per-eye views are larger and wider than the mono
  window, so more and larger mips are wanted. Eye L's and eye R's wants are nearly the same. [measured;
  inferred cause]

### 3.3 The surface rebuild (the "stereo-only particle-model work")

- 0x1C8B930 is the per-surface work of the model commit job 0x1C8A180
  (`r_commitSurfacesRenderProgDataJobGranularity`), which walks the "post commit surface render prog data
  list" (0x669BE60, filled by 0x1C8AA80, "Too many models in post commit surface render prog data list").
  The world list builder's cleanup of status-1 entities queues them (0x18DF11E), and under Route S eye R's
  world frame cleans up every entity eye L committed (`stereo-moved-flag.md`: about 117,000 per 10 s, and
  185,000 surface rebuilds). That is why it is about 100x a mono frame. [static-verified; measured counts]
- The moved-flag hook (`ETERNALVR_STEREO_MOVED`) makes eye R skip that cleanup, and the rebuilds fell to a
  few hundred per 10 s. It is installed only with per-eye TAA or DLSS, because the kept moved flag makes eye
  R draw moving entities with the motion-vector pre-pass. So with anti-aliasing On (the launcher's default
  again) this work is already gone; with anti-aliasing Off the CPU pays it. [static-verified; measured]
- 0x1C86BA0 (DAG job 0x1CD8360, a predecessor of the body) walks each world's double-buffered lists
  (`world + 0x5580D0 + parity * 0x18`, parity from its own call counter): not named yet.

## 4. Implemented: eye R skips the dynamic shadow slices eye L rendered (`ETERNALVR_REUSE_SHADOWS`)

Files: `src/stereo_seq/shadow_reuse.*` (pure logic, tests in `tests/stereo_seq/shadow_reuse_tests.cpp`),
`src/vkcore/shadow_reuse_hooks.*` (the hook), installed from `seq_hooks.cpp` after the other Route S hooks.

| Value | Effect |
|---|---|
| unset, `0` | nothing located, nothing hooked, no log line (the default) |
| `count` | the hook counts what it would skip; the game is unchanged |
| `1` | eye R skips spot and point light slices eye L rendered in this tick |
| `all` | also the sun's cascades and the player's own slices (experimental) |

- **The hook** is a mid hook on `cmp edx, eax` (RVA 0x1CFF33E; signature at 0x1CFF2B8, from the counter's
  `mov edx, [r13 + 0x88]` to the release call, unique in `.text`). Cross-checks: the `lea rcx` in it is the
  module base, and both table displacements name small non-negative values inside the image and outside
  `.text` (logged: `stale tables at RVA 0x39AD4D8 (mips 0,0,1,2,2) and 0x39AD4F0 (cascades 0,0,1,2)`). Any
  mismatch logs and installs nothing. The multiplayer guard is asked at install and at every call.
- **Which eye.** The render's tag, found by the backend counter the job sees
  (`seqTagForBackendFrame(seqBackendFrame() + 1)`, kept per job thread until the counter moves; a missing
  tag is not kept).
- **Eye L** stamps each slice it checks (light, slice, mip) with the tick in a 4096-slot table of packed
  (hash, tick) words.
- **Eye R** skips a slice when all of these hold: the entry's counter is 1 at its check (eye L's render
  reset it to 0 and eye R's check raised it; a slice eye L evaluated but did not render, cut by the budget
  or not yet rendered, shows 2 or more); eye L stamped the slice in this tick; the static part is not
  missing (flag 4); and the light is a spot or point light and the slice not the player's (unless `all`).
  Skipping hands `eax` the counter (the entry is not released), clears `bl`'s bits 2 and 4 (the slice does
  not go on eye R's list, so it is neither gathered nor rendered) and sets the entry's counter back to 0, as
  eye R's own render would have. The next eye L sees 1 and renders the slice as before.
- **Fail-safe by construction.** Every doubtful case renders the slice as the engine would: an unknown eye
  or tick, a tag that names the wrong render (the stamp then belongs to another tick, or the counter is not
  1), a slice only eye R sees (eye L never stamped it, so eye R renders it every tick), a race with eye L's
  render-list job (the counter is still 2).
- **Log** every 10 s: `seq-shadow: last 10 s (<mode>): dynamic slice checks L n R n other n; eye R skipped
  n (eye L's of the tick), rendered: eye L did not render it n, did not evaluate it n, static part n, sun or
  player n, untagged n` (`would skip` in `count` mode).
- **Not verified statically**: that nothing else on the shadow path expects each dynamic slice on every
  view's list. One byte of the entry, `+0x86`, is set to 1 by every check and rewritten by the render-list
  job; no reader was found in the shadow code (0x1CA0000 to 0x1CB0000, 0x1CEE000 to 0x1D08000). The rig run
  in section 6 is the check.

## 5. Plans for the next candidates

### 5.1 Streamer update once per tick

1. Bound first, no code: `ETERNALVR_DEBUG_CVARS=is_update=0` A/B (both eyes stop streaming, so a short run
   at a view that is already streamed in). If the tick rate does not move, drop the lever.
2. If it does: a mid hook at the entry of 0x1D47630's update call (the `cmp dword [is_update + 8], 0` at
   0x1D47668, reached once per render) that makes eye R's render take the skip branch, the eye found by the
   render's tag as in section 4. Log per eye. Risk: textures only eye R sees (a few degrees at its outer
   edge) stream in one tick later.

### 5.2 The surface rebuild with anti-aliasing Off

The moved-flag hook already removes it; with anti-aliasing Off it is not installed because the moved flag it
keeps costs GPU time. For CPU-bound players with anti-aliasing Off, an opt-in (`ETERNALVR_STEREO_MOVED=force`)
that installs it anyway trades eye R's rebuild CPU for eye R's motion-vector pre-pass on the GPU. Measure both
sides (CPU timing and GPU timing) before offering it.

### 5.3 GPU particles

Find the simulation dispatch (the `COMMAND_SIM_*` bytecode's compute pass, gated by the per-view flag at
render context + 0x4D9C93) and whether eye R's time step is 0. If it is, eye R's dispatch is pure cost and
can be skipped the way section 4 skips shadows. `r_skipGPUParticles 1` bounds the whole system at +3.8%
ticks for both eyes.

## 6. Rig recipe (not run: the rig was in use)

Build: this branch (`perf-reuse`). Runs as in `perf-stereo-cpu.md` (simulator, `e1m1_intro` with the head
sway, Release layer, interleaved pairs, at least four of each, compare medians), each 120 s after a 50 s
warm-up, with `ETERNALVR_CPU_TIMING=1` and `ETERNALVR_GPU_TIMING=1`:

```
tmp-vr\rs\rsrun.ps1 -Name ro-count -Tree <worktree> -Ack -WatchSeconds 120 -Map game/sp/e1m1_intro/e1m1_intro `
    -ExtraEnv 'ETERNALVR_CPU_TIMING=1','ETERNALVR_GPU_TIMING=1','ETERNALVR_REUSE_SHADOWS=count'
tmp-vr\rs\rsrun.ps1 -Name ro-off ... -ExtraEnv 'ETERNALVR_CPU_TIMING=1','ETERNALVR_GPU_TIMING=1'
tmp-vr\rs\rsrun.ps1 -Name ro-on  ... -ExtraEnv 'ETERNALVR_CPU_TIMING=1','ETERNALVR_GPU_TIMING=1','ETERNALVR_REUSE_SHADOWS=1'
```

1. **`count` first**: the install line (`seq-shadow: shadow stale check hooked (RVA 0x1CFF33E) ... stale
   tables ... (mips 0,0,1,2,2) ... (cascades 0,0,1,2)`) and the 10 s lines. `would skip` per 10 s divided by
   the ticks is the number of slices eye R renders for nothing each tick; "did not render it" should be small
   (the budget cut) and "did not evaluate it" small (faces only eye R sees). If `would skip` is near zero on
   `e1m1_intro`, repeat on `e1m2_battle` and in a lit arena with demons.
2. **A/B**, `off` against `1`, interleaved: from `rates:` the ticks per second; from `cpu:` the
   `eye R render` wall mean and p95 and the process CPU per tick; from `gpu:` eye R's busy time. The lever
   works if eye R's render wall time drops and the tick rate rises beyond the run-to-run spread.
3. **Visual**: the e1m2 start view with the head still (`ETERNALVR_HEAD_POSITION=0`,
   `ETERNALVR_TEST_HEAD_SWAY=0,0,1`) and a moving demon under a shadowed light, `ETERNALVR_CAPTURE_EYES=<dir>,45`
   in both runs; eye R's shadows should match eye L's (same caster pose, same softness), with no missing or
   one-tick-late shadows. `tools/stereo/eye_diff.py <dir> --disparity` on both runs should give the same
   disparity and a similar difference level. Then `all`, looking at the sun's shadows at eye R's outer edge
   (a missing near cascade shows as a blurrier band).
4. **Headset** (owner): the chainsaw training room and an arena with the flag on; look for shadow flicker in
   one eye only.

## 7. Risks

- The skip changes what eye R's view lists: anything downstream that counts on seeing every dynamic slice on
  every view's list (statistics, `r_showShadowCache`, the atlas's own bookkeeping) sees only eye L's.
- A slice that eye L rendered but whose content eye R needs differently: none for spot and point lights at
  the same mip; the sun's cascades and the player's weapon shadow (the viewmodel is placed per render) are
  excluded unless `all`.
- The saving may be small where few lights have dynamic shadows; the rig's `count` numbers decide whether the
  hook stays.

## 8. Rig results (2026-09-28): parked

Rig: RTX 4080 + 9950X3D, OpenXR-Simulator, 1280x1400 per eye, Debug layer, e1m2_battle start view, 60 s windows
after the warm-up (`tools/rig/cpu-cvar-ab.ps1` on the cpu-cvars branch with `-Sets base= -ExtraEnv ...`).

- Count run (`ETERNALVR_REUSE_SHADOWS=count`): about 1,165 dynamic slice checks per second in eye L and 830 in eye
  R; eye R would skip about 166 per second, one slice per tick. The rest are sun or player slices (about 333 per
  second) and slices whose static part was missing (166 per second).
- Off against `=1`, three interleaved pairs, ticks/s: 165.0 / 166.0, 167.5 / 167.6, 165.8 / 173.6. The third "on"
  run logged "no game window appeared to place on the virtual display", so it is not comparable. Without it the
  gain is +0.3%, inside the noise.
- The engine redraws one dynamic slice per tick for eye R in this scene, so there is little to save here. The
  hook stays on this branch, not merged, until a lit arena with moving demons (many dynamic shadowed lights) is
  measured, where the count run should show several slices per tick.
- The other candidates of section 2 (image streamer update, GPU particles, the surface rebuild with anti-aliasing
  off) are the next bounds to measure.
- Alternate eyes (eb83331) render one eye per tick, so this hook is not installed with them.

## 9. Rig results (2026-11-15, the bench routes): fail, the hook stays off

Rig: the test PC (RTX 3080 Ti, i7-9700K, OpenXR-Simulator at 90 Hz), Release layer from 0.1.97's code (aa899b47) with this
branch, the bench routes (`bench-routes.md`) at the launcher's defaults with pacing off (`ETERNALVR_PACE=off`), CPU
and GPU timing on. The raw runs (layer logs, CSVs) stay on the test PC.

**What eye R checks and lists, per stereo tick** (`count`, two runs each, the second shown; the first within 0.3 of
it). "Eye L's of the tick" is what `all` would skip and, for spot and point, what `1` skips. "Listed" is the count
mode's second watch on `test bl, 6` (0x1CFF357), which every listed slice passes, with or without a level-5 entry:

| Route | Kind (sun / spot / point / player) | Eye R checks | Eye L's of the tick | Listed by eye L | Listed by eye R | Eye R's also listed by eye L |
|---|---|---|---|---|---|---|
| Combat | | 1.45 / 0.07 / 0.00 / 1.22 | 0 / 0 / 0 / 1.22 | 1.45 / 0.86 / 0.44 / 1.22 | 1.45 / 0.07 / 0.00 / 1.22 | 1.00 / 0 / 0 / 1.22 |
| Hub | | 2.00 / 1.57 / 0.32 / 1.00 | 0 / 0.53 / 0 / 1.00 | 2.00 / 3.85 / 1.82 / 1.00 | 2.00 / 1.57 / 0.32 / 1.00 | 1.00 / 1.05 / 0 / 1.00 |
| Outdoor | | 2.00 / 0.39 / 0.00 / 2.00 | 0 / 0.33 / 0 / 2.00 | 2.00 / 1.95 / 0.00 / 2.00 | 2.00 / 0.39 / 0.00 / 2.00 | 1.00 / 0.33 / 0 / 2.00 |

- Eye R lists far fewer spot and point slices than eye L: in the combat arena 0.07 a tick against 1.3, in the hub
  1.9 against 5.7. The engine already leaves to eye L most of what section 3.1 expected eye R to redo (eye L's
  render clearing the lights' moved-caster bits before eye R's setup is the likely reason, as Parallel Eye's
  `view_shadow_cache.cpp` notes for view 0; [inferred]). What eye R lists of its own is the sun's cascades (1.45
  to 2 a tick, fitted to each eye; none of them passes the check as eye L's) and the player's slices (1 to 2 a
  tick, all eye L's of the tick).
- So `1` skips nothing in the combat arena and about half a spot slice a tick in the hub and outdoors; `all` adds
  the player's 1 to 2 slices.
- `r13` at the check is the face's level-5 entry; the sun's slices without one (1.33 of the 1.45 in combat) go
  straight to the listing and never reach the check, so not even `all` can skip them there.

**A/B** (`off` against `1`, four interleaved rounds, `all` twice; medians; tick p50 is the stereo tick's CPU time,
eye R and outside the CPU split's wall times, GPU the tick's busy p50, missed the display periods without a new
image):

| Route, size | Mode | Ticks/s | Tick p50 ms | Eye R ms | Outside ms | GPU ms | Missed % | Eye R skipped a tick |
|---|---|---|---|---|---|---|---|---|
| Combat 1280x1400 | off | 110.9 | 8.78 | 4.12 | 4.88 | 5.31 | 1.1 | - |
| | 1 | 107.3 | 9.15 | 4.24 | 5.05 | 6.27 | 1.1 | 0 |
| | all | 111.1 | 8.82 | 4.10 | 4.89 | 5.39 | 1.0 | 1.23 (player) |
| Hub 1280x1400 | off | 137.7 | 7.18 | 3.38 | 3.88 | 5.76 | 0.2 | - |
| | 1 | 136.9 | 7.32 | 3.40 | 3.91 | 5.96 | 0.2 | 0.5 (spot) |
| | all | 136.5 | 7.28 | 3.39 | 3.94 | 5.87 | 0.1 | 0.68 spot, 1.0 player |
| Combat 2048x2208 | off | 90.0 | 10.84 | 4.97 | 6.24 | 9.75 | 7.8 | - |
| | 1 | 87.4 | 11.20 | 5.04 | 6.47 | 10.29 | 9.4 | 0 |
| | all | 88.7 | 11.01 | 4.95 | 6.38 | 10.05 | 8.5 | 1.21 (player) |

The combat route is two-state with the encounter (GPU per tick 4.7 to 4.9 ms or 5.7 to 6.6 ms); `1` drew three
heavy runs of four, which is its whole difference, since it skipped nothing there. By state: light runs off 8.55 /
8.56, `1` 8.59, `all` 8.62 ms; heavy runs off 9.04 / 9.01, `1` 9.19 / 9.32 / 9.11, `all` 9.01 ms. No mode is
faster in either state; the hub is 0.10 to 0.14 ms slower with `1` and `all`.

**Eye captures** (`shadow-eyes-*` QA scenarios: the combat arena, the hub's bridge and Exultia's start, the head
held still, every 45th pair; compared with the spike's own script, medians over 130 to 160 pairs a run):

- Both eyes on one view (`ETERNALVR_STEREO_SAME_VIEW=1`), eye L against eye R, mean absolute difference (0 to 255):
  combat off 1.57, `1` 1.55, `all` 1.64; Exultia off 1.82, `1` 1.83, `all` 1.83; hub off 2.32 / 2.36 / 2.40, `1`
  2.52 / 2.46 / 2.32, `all` 2.31 / 2.32. The hub's first two `1` runs have eye R 0.3 to 0.4 darker over the whole
  upper image (not shaped like any shadow), the third and both `all` runs (which skip the same spot slices and more)
  are within the off runs. Difference images show no shadow that differs between the eyes in any mode, the player's
  hand and gun shadows on the demon included. The combat runs' largest pair (8.65 in `all`) is the arena's teleport,
  where the world changes between the two eyes' renders.
- Per-eye views: eye L against eye R level and the run-to-run spread of eye R against that of eye L are the same
  in every mode (hub eye R / eye L across runs 1.13, 1.10, 1.12 for off, `1`, `all`; Exultia 1.04, 1.05, 1.05).

**Result: fail.** The pass line was the combat tick down 0.5 ms with eye R down 0.4 ms; `1` skips no slice in the
combat arena, and neither `1` nor `all` (the player's slices too) moved the tick or eye R's render beyond the run
spread on any route. The captures show no shadow difference, so the fail is the saving, not the picture. The shadow
work eye R pays (`r_skipShadows` bound, `perf-stereo-cpu.md`) is not in the slices this check sees: eye R renders
the sun's cascades and the player's slices, and the rest is the per-light setup, caster culling and gathering,
which run whether a slice is listed or not [inferred]. The hook stays on `shadow-reuse`, off unless `ETERNALVR_REUSE_SHADOWS`
asks for it, and only under Route S (Parallel Eye Rendering claims the same check first when it is on). Next
candidates for eye R's shadow time: the sun's cascades fitted once to a frustum around both eyes, and the setup's
per-light cost, each its own bound first.
