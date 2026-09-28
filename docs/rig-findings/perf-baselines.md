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
