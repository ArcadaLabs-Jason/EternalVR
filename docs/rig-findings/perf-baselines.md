# Performance baselines

Stereo (Route S) cost per stereo tick on the development machines, measured with the layer's GPU timing
(`ETERNALVR_GPU_TIMING=1`, docs/VR_STEREO.md, GPU timing). A stereo tick renders eye L and then eye R, so
90 Hz needs both the GPU busy time and the CPU time per tick under 11.1 ms.

## Method

- `tools\rig\launch-ht.ps1 -Stereo` into `game/sp/e1m1_intro/e1m1_intro` with the layer's cinematic skip,
  OpenXR-Simulator 1.5.0 at 90 Hz on the virtual display, the player standing at the start (no input),
  per-eye TAA (the default), the UI layer on, the game's default graphics settings for that machine.
- Render size as the launcher sets it: `ETERNALVR_RENDER_SIZE` (fixed `WxH` or `auto`), the game started at
  that size (`+r_windowWidth/+r_windowHeight`), the window a 1280x720 mirror.
- Numbers are the means of the 10 s `gpu:` and `rates:` lines after a 50 s warm-up (8 windows).
  `GPU` is the stereo tick's GPU busy time (eye L frame plus eye R frame, the union of the submit batches'
  intervals), `CPU` the time from one eye L present to the next.

## A second test PC: RTX 3080 Ti (12 GB), i7-9700K, 32 GB (2026-09-27, main 772b90d + gpu-timing)

| Per eye | Ticks/s | GPU mean (p95) ms | CPU mean (p95) ms | Pose age ms |
|---|---|---|---|---|
| 1415x1415 (render size off, session 2's size) | 82 | 8.7 (14.3) | 12.1 (16.2) | 28.6 |
| 1280x1400 (`auto` on the simulator) | 86 | 9.0 (14.3) | 11.6 (15.1) | 26.9 |
| 2048x2208 (`2056x2216`, the Quest 3 budget) | 79 | 11.7 (15.9) | 12.7 (15.9) | 29.1 |

- **CPU-bound.** The CPU time per tick stays at about 12 ms whatever the size: the game's frame work runs
  twice per tick and the i7-9700K cannot bring it under 11.1 ms. Only at the Quest 3 budget does the GPU
  reach the CPU. On this class of CPU the gain for 90 Hz is on the CPU side of a tick.
- Async compute (queue family 2) takes about 0.3 to 0.4 ms per frame; the transfer family's batches have
  no timestamps and are counted as untimed.
- No frame was lost and the query ring never filled (about 6 to 12 of 512 pairs in use).

## The second test PC: where the CPU time of a stereo tick goes (2026-09-27, cpu-split 72314c8 on main a8b4777)

Measured with the layer's CPU timing (`ETERNALVR_CPU_TIMING=1`, docs/VR_STEREO.md, CPU timing) next to
GPU timing, same method as above (`e1m1_intro` start, simulator 90 Hz, per-eye TAA, UI layer on, means of
the 8 windows of 10 s after the 50 s warm-up). A tick starts when the frontend starts eye L's frame-end
job. Stage times are ms per tick; `wall` is elapsed time, `CPU` the calling thread's CPU time in it.

| Stage (ms per tick) | 1280x1400 (`auto`): wall mean (p95) | CPU | 2048x2208 (Quest 3): wall mean (p95) | CPU |
|---|---|---|---|---|
| Tick period (frontend) | 10.52 (14.37) | | 11.34 (14.77) | |
| Outside the frame-end jobs: game frame + eye L views | 5.47 (9.52) | 2.84 | 6.35 (9.88) | 2.85 |
| Eye L frame-end job (hands eye L to the render thread) | 0.02 (0.03) | 0.02 | 0.02 (0.04) | 0.02 |
| Eye R render (its views and its frame-end job) | 5.04 (8.69) | 0.12 | 4.97 (8.87) | 0.11 |
| Drain for a new tag base | 0 | 0 | 0 | 0 |
| vkQueueSubmit, all threads (13.7 calls) | 0.98 (1.34) | 0.95 | 0.93 (1.21) | 0.90 |
| vkWaitForFences, blocking, all threads (3.9 calls) | 0.76 (3.39) | 0.01 | 0.50 (2.96) | 0.01 |
| vkQueuePresentKHR incl. the XR presenter (2 calls) | 0.30 (0.52) | 0.29 | 0.27 (0.42) | 0.27 |
| Whole process CPU | | 42.4 (p95 51.2) | | 42.6 (p95 50.2) |

| Same runs | 1280x1400 | 2048x2208 |
|---|---|---|
| Ticks/s | 92.9 | 87.0 |
| Cores busy (process CPU / wall) | 4.0 of 8 | 3.7 of 8 |
| Busiest thread | 45 % of a core | 43 % of a core |
| GPU busy per tick, mean (p95) | 7.8 (12.2) | 10.6 (13.9) |
| CPU present interval per tick, mean (p95) | 10.8 (13.8) | 11.5 (14.3) |
| Timer overhead | 0.01 ms per tick | 0.01 ms per tick |

- **The tick is a serial chain of two ~5 ms view renders, not a CPU throughput limit.** Eye R's render
  takes 5.0 ms of wall time but only 0.1 ms of the frontend thread's CPU: that thread waits while the
  engine's job workers render eye R's views. The part outside the frame-end jobs (the game frame and eye
  L's views) takes 5.5 ms, again with only 2.8 ms on the frontend thread. Together they are the tick.
- **Half of the CPU is idle.** The process uses 42 ms of CPU per tick, which is 4 of the i7-9700K's 8
  cores. About nine job workers each run 40 to 45 % of a core; no thread is saturated. More cores or a
  faster core only help in so far as they shorten the chain.
- The Vulkan side is small: about 1 ms of driver CPU for 14 submits per tick, 0.3 ms for the two presents
  through the XR presenter, and blocking fence waits of 0.5 to 0.8 ms (p95 3 ms: the CPU waiting for the
  GPU). At the Quest 3 budget the GPU (10.6 ms busy) gets close to the tick, and the time outside the
  frame-end jobs grows by 0.9 ms while eye R's render does not change.
- The engine runs the frame-end job and the presents on whichever worker is free: in only about 12 % of
  ticks did both frame-end jobs run on one thread. Per-thread "frontend" or "render thread" budgets
  therefore mean little on this engine; the stage wall times and the process total are the measure.
- This run reached 93 ticks/s at `auto` (86 in the table above, on an earlier main without CPU timing
  and the per-eye TAA rebuild fix). Run-to-run spread on this box is a few ticks/s.
- Where to optimise later (near the end): shorten or overlap the chain. Eye R repeats all of eye
  L's frontend work, including what does not depend on the eye (culling for shadow views, particles and
  other view-independent passes); skipping that work, or starting eye R's views while eye L's are still
  on the workers, targets up to ~5 ms per tick with idle cores to spare.

## The second test PC: the benchmark routes (2026-11-13, main cee14b6b, 0.1.94's layer)

The three routes of `bench-routes.md` (combat arena: Cultist Base's Slayer Gate; outdoor: Exultia's opening;
hub: the Fortress of Doom), each measured three times at two sizes on the RTX 3080 Ti (12 GB), i7-9700K, 32 GB,
NVIDIA 610.88, OpenXR-Simulator 1.0.27 at 90 Hz on the virtual display. The launcher's default settings
(`bench-routes.md` section 2: per-eye TAA, frame pacing `headset`, render scale 1.0, no DLSS, no foveation),
the game's own settings the PC's profile with ray tracing held off, the release build (`windows-msvc-release`).
Three suites of the six scenarios, one after the other (10:46 to 11:41), so each route's three runs are 18
minutes apart. Nothing else ran on the PC: no build, no other game run; the game's modules from outside Windows
were the game's own, Steam's overlay and Fossilize layer, the simulator and the layer (no RTSS, OBS or ReShade);
the game's priority read Normal at the start of every route. Each cell is the mean of the three runs with the lowest and highest run in
brackets; each run's numbers are over the 60 s of its six whole 10 s windows (`bench-routes.md` section 4).
`auto` is the simulator's recommendation, 1280 x 1400 per eye (1.79 Mpix); the Quest 3 size asked
(2496 x 2688) was fitted to 2048 x 2208 (4.52 Mpix, the size a Quest 3 gets from the launcher's default budget).

| Route | Size per eye | Ticks/s | Tick CPU p50 / p99 ms | GPU busy per tick p50 / p99 ms | GPU share of the tick % | Shown image p99 ms | Display periods without a new image % | Pose age mean / p99 ms | VRAM peak MB |
|---|---|---|---|---|---|---|---|---|---|
| Combat | 1280 x 1400 | 107.5 (106.7-108.2) | 9.0 (8.9-9.1) / 14.0 (13.8-14.2) | 5.7 (5.5-5.9) / 10.2 (9.9-10.4) | 64 (61-65) | 20.6 (17.7-22.2) | 1.9 (1.7-2.3) | 21.2 (21.1-21.4) / 48.0 (43.6-52.8) | 7022 |
| Combat | 2048 x 2208 | 94.2 (84.6-99.3) | 10.4 (9.8-11.5) / 16.5 (14.8-18.9) | 9.0 (8.1-10.6) / 13.9 (12.3-16.8) | 86 (82-91) | 21.3 (20.6-21.9) | 5.5 (2.9-10.4) | 24.3 (23.2-26.2) / 50.1 (48.7-51.0) | 8053 |
| Outdoor | 1280 x 1400 | 106.2 (105.1-107.3) | 9.2 (9.1-9.4) / 13.6 (13.2-14.1) | 7.6 (7.5-7.7) / 10.4 (10.1-10.6) | 81 (80-81) | 17.2 (14.7-19.0) | 1.7 (1.3-1.9) | 23.2 (23.0-23.4) / 51.8 (49.4-53.7) | 7102 |
| Outdoor | 2048 x 2208 | 85.1 (84.6-85.9) | 11.7 (11.6-11.9) / 14.2 (14.2-14.2) | 11.3 (11.2-11.4) / 13.6 (13.5-13.7) | 96 (96-97) | 15.0 (14.6-15.5) | 5.7 (4.9-6.3) | 27.1 (26.9-27.1) / 31.6 (31.3-31.8) | 8070 |
| Hub | 1280 x 1400 | 130.4 (129.3-131.0) | 7.5 (7.4-7.5) / 14.5 (13.0-16.8) | 6.0 (5.8-6.0) / 9.0 (9.0-9.0) | 78 (77-78) | 19.4 (16.9-22.2) | 1.9 (1.7-2.2) | 19.7 (19.6-19.8) / 51.7 (50.4-52.4) | 6986 |
| Hub | 2048 x 2208 | 97.6 (97.1-97.8) | 10.2 (10.1-10.3) / 21.6 (20.7-23.2) | 9.4 (9.3-9.6) / 12.4 (12.3-12.4) | 91 (89-91) | 22.8 (22.0-24.3) | 2.3 (2.1-2.4) | 24.6 (24.6-24.7) / 52.1 (51.6-53.0) | 7993 |

VRAM varied by under 30 MB between a route's runs.

| Same runs (means of the three) | Combat auto | Combat Q3 | Outdoor auto | Outdoor Q3 | Hub auto | Hub Q3 |
|---|---|---|---|---|---|---|
| Lowest 10 s window, ticks/s | 98 | 70 to 95 | 101 | 84 | 118 | 92 |
| Game frame + eye L views, wall ms | 5.1 | 6.0 | 5.0 | 6.8 | 4.2 | 6.0 |
| Eye R render, wall ms | 4.2 | 4.7 | 4.4 | 4.9 | 3.5 | 4.3 |
| Whole process CPU per tick ms (cores busy) | 41.6 (4.4) | 43.2 (4.1) | 43.7 (4.6) | 50.1 (4.3) | 35.2 (4.6) | 40.5 (3.9) |
| Ticks over 13.9 ms (a 72 Hz period) % | 1.0 | 7.5 (1.8-18.4) | 0.9 | 1.7 | 1.0 | 1.8 |
| GPU busy per tick over 13.9 ms % | 0.0 | 4.4 (0.0-13.1) | 0.0 | 0.5 | 0.0 | 0.0 |
| XR frame interval p99 ms | 13.9 | 16.0 | 14.0 | 14.4 | 15.7 | 17.3 |
| Longest shown image in the 60 s, ms (the worst run) | 137 | 118 | 76 | 59 | 88 | 57 |

- **CPU-bound at `auto`, GPU-bound at the Quest 3 budget.** At 1280 x 1400 the GPU is busy for 64% (combat) to
  81% (outdoor) of the tick, and the tick is the CPU chain of the game frame with eye L's views and eye R's
  render, 7.7 to 9.4 ms of wall time with about 4.5 of the 8 cores busy: as on 2026-09-27, a serial chain, not a
  CPU throughput limit. At 2048 x 2208 the outdoor and hub routes are GPU-bound (96% and 91%), the combat route on
  the edge (82 to 91%). Every route runs above 90 ticks a second at `auto` and the hold lets the game go
  (below), so the headset gets a new image in 98% of its display periods.
- **The combat route at the Quest 3 budget is the one with a spread.** Two runs measured 99 ticks a second with
  8.1 to 8.3 ms of GPU per tick, the third (and an exploration run of the same scenario earlier the same morning,
  82 ticks a second, 11.2 ms) 85 ticks a second with 10.6 ms, 13% of its ticks' GPU time over 13.9 ms and 10.4%
  of the display periods without a new image. The encounter is the game's: which demons are in view, and their
  effects, differ from run to run. The other five cells repeat within a few percent.
- **The simulator skips display periods.** In every run its frame loop skipped 5 to 72 display periods a minute
  (127 in the heavy combat run); at 2048 x 2208 on combat and outdoor it gave 82 to 89 XR frames a second
  instead of 90. Those periods count as missed. A headset's compositor has its own priority on the GPU; this part
  may be smaller there.
- **Gates read on this PC** (90 Hz, RT off, the launcher's defaults; the simulator, not a headset): no frame over
  250 ms in any run (the longest shown image 137 ms, the longest XR frame interval 125 ms, both in one combat
  run); the shown image's p99 within two display periods (22.2 ms, reached exactly in some runs) on every route at
  `auto` and at the Quest 3 budget on combat and outdoor; hub at 2048 x 2208 measured 22.0, 22.1 and 24.3 ms: at
  the limit, over it in one run. The 90 Hz target (p99 within 11.1 ms) is
  met by none: 1.7 to 5.7% of the display periods show no new image.
- **VRAM**: 7.0 GB at `auto` and 8.0 GB at 2048 x 2208 of the 11.3 GB budget, flat through each route.

### The 4080 gate: what this predicts (inference, not measured)

The gate runs are on the RTX 4080 with a Ryzen 9 9950X3D and a Quest 3, at Quest 3 size (2496 x 2688, 6.71
Mpix) or, at the launcher's default, the 2056 x 2216 budget (4.56 Mpix, the size measured here as 2048 x 2208).

- **GPU** [inferred]: public reviews put the RTX 4080 about 1.3 to 1.5 times an RTX 3080 Ti in rasterised games
  at 4K (not measured on this project). At the default budget (the 2048 x 2208 rows: 9.0 to 11.4 ms mean, p99
  12.3 to 16.8 ms on the 3080 Ti) that gives 6 to 9 ms of GPU per tick, p99 8 to 13 ms. At full Quest 3 size,
  with about 80% of the pair's GPU cost scaling with pixels (`VR_STEREO.md`, the stereo cutscenes' measurement),
  the GPU per tick grows about 1.39 times: 8 to 12 ms mean on the 4080, p99 11.5 to 14.5 ms on hub and outdoor
  and 15.5 to 18 ms in the combat route's heavy run.
- **CPU** [inferred]: the tick's CPU chain is 7.7 to 9.4 ms here at `auto` and grows to 10 to 12 ms at the larger
  size (the chain waits on the GPU). The 9950X3D's faster cores shorten it; the 4080 machine already measured 135
  ticks a second at `auto` and 109 at 2048 x 2208 on an older build (`render-size.md` section 9, its profile's
  settings, likely RT on), against 106 to 130 and 85 to 98 here with RT off.
- **72 Hz at Quest 3 size** (M4: p99 within 13.9 ms and under 1% missed frames, a target; the gate there is two
  periods, 27.8 ms): at the default budget every route's GPU p99 (8 to 13 ms) is under 13.9 ms, so 72 Hz should
  hold with margin. At full Quest 3 size the p99 lands at the period's edge on hub and outdoor and over it in the
  combat route's heavy runs: expect the combat route to decide the 72 Hz target at native size, and DLSS or
  foveation to be needed there for under 1% missed frames. The two-period gate (27.8 ms) should pass on every
  route at either size.
- **90 Hz at Quest 3 size** (M9, gate p99 within 22.2 ms): at the default budget on the 4080 the GPU per tick
  (6 to 9 ms) is under the 11.1 ms period, so the 3080 Ti's near-misses (hub's 22.0 to 24.3 ms) should pass. At
  full Quest 3 size the mean GPU per tick reaches 8 to 12 ms on the 4080: near the 11.1 ms period, as R11
  section 5.2 says ("native is at the limit"); the gate (two periods) likely passes, the target (one period) does
  not without DLSS or foveation.
- What could make this wrong: the headset's compositor (VDXR or Meta Link) takes GPU time the simulator's does
  not; the pacing hold behaves differently against a real runtime's frame timing (below); the 4080's factor over
  the 3080 Ti differs in this engine.

### Fix candidates (evidence only; nothing changed)

1. **Frame pacing's hold costs frames on this PC** [rig, log]. Every run held the game 6 times and let it go 7
   times (the hold switch's back-off: 1, 2, 4, 8 s ...). Each time it was held, the game's rate fell to 66 to 89
   images a second and 5.5 to 29% of the headset's frames showed no new image (the `pace: ... let go` lines, 42 of
   them in suite 2), while let go the same scenes ran 91 to 164 images a second. Most of each measured minute was
   let go, so the table's numbers are mostly the let-go state; the first minute of play pays the holds. [inferred] The hold
   releases the game when the headset's next frame begins, and the tick's 7.7 to 9.4 ms CPU chain (p95 about
   12 ms) then often does not fit before the frame after. Worth testing: releasing the game one measured tick time
   before the frame it should land in, or holding only when the tick's p95 fits in the period with a margin.
   `present-latency.md` (2026-10-30) measured 0.07 to 0.36% repeats held on a quiet rig and put its bursts
   of repeats down to the game's Below Normal priority under the rig's scheduled task [inferred there]. Here the
   game's priority read Normal at the start of every route and nothing else ran, and each hold still lost 5.5 to
   29% of the headset's frames while it lasted: on these routes the priority is not the whole cause.
   **Measured 2026-11-13 (`pace-hold.md`, T-232):** the cause on this PC was the CPU's clock falling while
   the render thread slept through the hold (Balanced: 84 to 114% of its rated clock held, 125 to 127% let go),
   not the release timing; held, the render thread now spins the last 5 ms of its wait once a hold has fallen
   behind, and outdoor and hub stay held the whole minute.
2. **Eye R repeats eye L's frontend work** [rig, log]: unchanged from 2026-09-27 above. At `auto` the tick is
   4.2 to 5.1 ms (the game frame with eye L's views) plus 3.5 to 4.4 ms (eye R's render) of serial chain, with
   about 3.5 of the 8 cores idle. Removing or overlapping eye R's
   view-independent work is the lever for 90 Hz on CPUs of this class, and it would also make the hold of item 1
   fit.
3. **GPU at the Quest 3 budget** [rig]: outdoor and hub are 91 to 97% GPU at 2048 x 2208. DLSS per eye and fixed
   foveation are the levers there; their targets (T-105) were not measured here.
4. **Pose age p99 about 50 ms** [rig, log]: the mean is 20 to 27 ms, the p99 44 to 54 ms on every route but one,
   and that one is the most GPU-bound (outdoor at 2048 x 2208: p99 31.6 ms). [inferred] The spikes go with
   the game running ahead of the headset (let go, more images than headset frames, some never shown): the GPU-bound
   run, which never runs ahead, has none. Worth a look against T-111's
   gate (within the pipeline depth plus one period) once PLAN 1.17's depth is known.
5. **Single long frames**: one 125 to 137 ms frame in one combat run, 60 to 90 ms in others, none in loading;
   the layer's stall lines for them (over 50 ms between game presents in play) show no pipeline creation, no
   allocation and no time in our present hook. Below the 250 ms gate; not chased.

Not measured: 72 Hz (the simulator has no refresh setting), the full Quest 3 size (the simulator's swapchain
stops at 2048 per eye), RT on, DLSS or foveation, the mono reference and the stereo overhead, a headset and its
compositor, the RTX 4080. Run folders: `QA-b1` to `QA-b3` on the test PC
(each run's layer log, both tables and `bench.json`, from `QA-b3` on).
