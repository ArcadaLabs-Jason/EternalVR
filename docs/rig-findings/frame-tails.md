# Frame tails on the default path: missed display periods, hitches and late poses (2026-11-15)

## Question

The default path since 0.1.96 (Frame pacing matched to the headset, the held wait spinning once a hold falls
behind, the auto size 1280 x 1400 per eye, the launcher's defaults) still had three tails on the benchmark routes
(`bench-routes.md`): display periods without a new image (combat about 2%, outdoor and hub under 1%) although the
game's own tick is well under the 11.1 ms period; single hitches of 40 to 140 ms; and a pose age whose p99 is
far above its mean. Where does each tail sample come from, and which of it can the layer fix?

Tags: **[rig]** measured on the test PC, **[log]** read from the layer's logs of earlier runs, **[code]** read in
the layer's code, **[inferred]** a reading the rig has not confirmed.

## Method

- Test PC: RTX 3080 Ti, i7-9700K, Windows' Balanced plan, the OpenXR simulator at 90 Hz; the three benchmark
  routes (`bench-combat`, `bench-outdoor`, `bench-hub`, 60 s of scripted input measured over 70 s), the game at
  normal priority (the bench scenarios raise it), CPU and GPU timing on as in every bench run.
- Two rig tables, both off by default, written beside the frames table:
  - `ETERNALVR_TEST_LATENCY_CSV=1` (release-to-present timing, `present-latency.md`), now with `pose_located`:
    per image the release, eye L's present, the hand-over, the headset frame that showed it (its xrWaitFrame,
    choice, slot taken after the GPU fence, copy, xrEndFrame call) and when the camera hook located the head pose
    that frame submitted.
  - `ETERNALVR_TEST_CPU_CSV=1` (new): every CPU tick's split (`docs/VR_STEREO.md` "CPU timing"): the time outside
    the frame-end jobs (the game frame and eye L's views), eye R's render, the fence and semaphore waits, the
    presents, the process's CPU time.
  - `ETERNALVR_TEST_SLOW_CALLS=1` (new): a line for every camera hook call over 4 ms and every log line that took
    over 2 ms to write.
- The frames table's rows join the latency table by the headset frame's choice count; the CPU ticks by time. A
  script puts every route frame without a new image, every frame at or over the route's pose-age p99 and every
  CPU tick over 40 ms on one cause (analysis scripts kept on the test PC).
- Runs: three suites of the three routes on the default path (`base`), then interleaved A/B pairs (below).

## Results

### Earlier logs first [log]

- The pose age reported before (p99 44 to 54 ms, the perf-pre runs of 2026-11-13) was measured before the held
  wait spun (T-232): the hold let the game go 6 to 8 times a route, and let go the game runs ahead of the headset
  and some poses wait. On 0.1.96's default path (the pace-hold runs r5, r6, r8 and the second-fall latch run,
  2026-11-13 and 11-14, five runs a route) the pose age's p99 is 25 to 33 ms around a median of 20 to 21 ms, and
  75 to 85% of the frames at or over the p99 were a repeat (the image before shown again, one period older) or
  the late image shown a period after its own frame.
- In those runs the stall watch logged 0 to 2 gaps of 50 to 117 ms between presents in play per route, with no
  pipeline created, no allocation and no checkpoint save in the gap (the route's only checkpoint saves are at its
  start); the 10 s lines could not say more.

### The default path, three suites of the three routes (2026-11-15) [rig]

`base-1` to `base-3`: the default path with the two tables on; ticks 89.6 to 90.6 a second in every run.

| Route | Missed periods (bench) | Pose age p50 / p99 ms | XR frames without a new image (route) | The game's image took longer than the period | The frame after such an image | Other |
|---|---|---|---|---|---|---|
| combat | 2.63 / 1.28 / 2.33% | 19.5-19.8 / 31.8-33.1 | 370 | 309 (84%) | 61 (16%) | 0 |
| outdoor | 0.46 / 0.91 / 0.48% | 20.8-21.0 / 31.0-32.7 | 117 | 96 (82%) | 20 (17%) | 1 (XR worker: the image's GPU work not done) |
| hub | 0.30 / 0.37 / 0.46% | 20.6 / 25.4-26.2 | 81 | 54 (67%; 2 of them over 25 ms) | 26 (32%) | 1 (the same) |

- **The late images are barely late.** In combat 308 of 309 were less than 4 ms over the period (median 0.3 to
  0.5 ms, p90 1.3 to 1.8 ms), outdoors 94 of 96, in the hub 41 of 54. The game's own time for them (release to
  hand-over) was 11.6 ms at the median against 8.7 ms for the images on time; the overrun sat in the eye L half
  (release to eye L's present) for 32%, in the eye R half for 21% and in both for 47% (combat), and the CPU ticks
  around them kept 3.3 to 4.4 cores busy as usual: more work in those ticks, not a thread starved or a CPU
  slowed. Outdoors and in the hub both halves grew together in nearly every case.
- **Not the layer's pacing:** the held release woke at most 0.3 ms after the headset frame's choice in every late
  image; the XR worker's frame interval and its parts before the choice show no late frame; the simulator skipped
  1 to 7 display periods a route (counted in the bench's missed periods, not in the frames table).
- **The frame after a late image** has no image of its own: the late image's present returned without a hold
  (the next headset frame had begun), so the next image started at once and was due at the following choice
  with less time.

Pose-age tail: every frame at or over the route's p99 put on one cause (three runs, 189 to 191 frames a route).

| Route | A repeat (the image before, one period older) | The late image, shown a period late | On time: the simulator's xrEndFrame long | On time: other |
|---|---|---|---|---|
| combat | 90 (48%) | 82 (43%) | 0 | 17 (9%: the XR worker's wait for the next choice 13, before the release 4) |
| outdoor | 68 (36%) | 114 (60%) | 0 | 9 (5%) |
| hub | 80 (42%) | 34 (18%) | 50 (26%) | 25 (13%: the game's image 14, the XR worker 7, before the release 3, shown a frame later 1) |

Where the age of an ordinary frame goes (median of every new frame, ms; combat / outdoor / hub): the camera hook
locates the pose 3.8 / 5.3 / 5.9 ms before the render thread is released (the game's frame starts while the
render thread waits in the hold), the game's image 8.9 / 8.7 / 6.9, the hand-over to xrEndFrame 3.6 / 3.9 / 4.8
(the image waits for the next headset frame's choice: the slack before its deadline, plus the copy), and the
simulator's xrEndFrame and what follows 3.0. The tail is the misses: a frame without a new image is one period
older, and so is a late image.

Hitches: CPU ticks over 40 ms inside the route, nine route runs.

| Route | Ticks over 40 ms | Their length ms | Where in the tick | In the tick |
|---|---|---|---|---|
| combat | 7 | 42-66 | outside the frame-end jobs (the game frame and eye L's views); eye R, the fence waits and the presents as usual | the process at about one core (four normally) |
| outdoor | 1 | 42 | the same | the same |
| hub | 2 | 42-43 | the same | the same |

(The hub also had a 169 ms tick at a menu, counted by the stall watch as a menu stall.)

### The hitches were the rig's scripted input [rig]

`ETERNALVR_TEST_SLOW_CALLS=1` (two suites): every camera hook call over 10 ms was the camera hook re-reading the
bench's scripted input file (`ETERNALVR_TEST_INPUT`, which only the rig sets): 14 to 99 ms, the read's own time in
its log line matching the hook's; a read by the XR worker's action sync (the other caller) held the camera hook
on the reader's lock for as long. The bench scripts rewrite that file every 0.4 to 0.6 s; opening a freshly
written file took over 10 ms in 5 to 9% of the reads (up to 99 ms) [inferred: the file scan on open]. Every
in-route hitch of the nine base runs above has a read of 34 to 66 ms ending inside it (10 of 10), and 13 to 21%
of the frames without a new image fell within 80 ms after a read over 10 ms. Apart from those, the camera hook
took over 4 ms only around a log line that took 2 to 13 ms to write (a handful a run, mostly at start-up).

Fixed (a rig-only change, nothing a player runs touches it): the file is read on a thread of its own, every 50 ms
(`vkcore/controller_test.cpp`). The two later suites (`base-4`, `base-5`) with the fix: the reads still take up
to 70 ms on that thread, and no tick in the routes reached 40 ms.

| Route, `base-4` / `base-5` | Missed periods (bench) | Pose age p50 / p99 ms | XR frames without a new image | The game's image over the period | The frame after it | Late images under 4 ms over | Pose-age tail that is a miss |
|---|---|---|---|---|---|---|---|
| combat | 2.54 / 1.15% | 19.6-19.8 / 31.1-31.4 | 232 | 211 (91%) | 21 (9%) | 209 of 211 | 127 of 128 |
| outdoor | 0.89 / 0.39% | 21.0-21.1 / 31.0-31.8 | 79 | 74 (94%) | 5 (6%) | 74 of 74 | 128 of 128 |
| hub | 0.22 / 0.19% | 20.5-20.6 / 25.2-25.4 | 37 | 28 (76%) | 6 (16%) | 26 of 28 | 74 of 128 (the simulator's xrEndFrame 45) |

The same picture without the rig's stalls: the frames without a new image are the game's image running a
little over the period, the frame after it, and nothing of the layer's pacing; the pose-age tail is those
frames (in the hub also the simulator taking long in xrEndFrame).

### What the layer controls

- The late images are the game's own work running a little long. The layer's share of the tick (eye R's render
  under Route S) grows with the rest in those ticks, and nothing in the layer stood out. What the layer controls
  is what happens to a late image: it waits a whole period for the next headset frame, and the frame it missed
  shows the image before again.
- The pose's age at the median is the pipeline: the game's frame running ahead of the render thread's release,
  the image, the slack before the headset's choice, and the runtime. Rendering with a later pose would mean
  holding the game's frame instead of the render thread (its CPU work then in the image's time, so more late
  images), or re-locating the pose for the render and correcting the view the game built (late latching, a large
  change; the runtime's own reprojection already corrects the head's rotation between the submitted pose and the
  display) [inferred]. Neither is worth it while the tail is the missed periods.

### The late-image wait, opt-in (T-235)

The existing late-image wait (`late_image.hpp`, a rig switch since T-201): a headset frame that begins while the
held game is still drawing its image waits for it, until 6 ms before the period ends (the submit margin of 4 ms
and 2 ms for the image's GPU work), and shows it at its own display time. Interleaved pairs, combat at the auto
size, the first build (the scripted input read in the camera hook in both arms, `ETERNALVR_TEST_LATE_WAIT=1`):

| Run | Missed periods % | Stalls in the route | Pose age p50 / p99 ms | Tick p50 ms | Shown image p99 ms | Hold let go | Late waits: the image came / ran out |
|---|---|---|---|---|---|---|---|
| default 1 | 3.17 | 0 | 19.9 / 32.8 | 11.06 | 22.2 | 5 | |
| wait 1 | 0.91 | 1 | 19.6 / 31.2 | 11.09 | 13.5 | 0 | 424 / 42 |
| wait 2 | 0.43 | 0 | 19.8 / 26.3 | 11.10 | 11.6 | 0 | 273 / 23 |
| default 2 | 2.57 | 2 | 19.7 / 32.3 | 11.04 | 22.2 | 3 | |
| default 3 | 1.35 | 1 | 19.8 / 31.7 | 11.09 | 22.2 | 0 | |
| wait 3 | 0.67 | 0 | 19.5 / 27.9 | 11.08 | 12.5 | 0 | 458 / 20 |
| wait 4 | 0.28 | 1 | 19.8 / 25.3 | 11.05 | 11.7 | 0 | 378 / 17 |
| default 4 | 1.94 | 0 | 19.7 / 32.5 | 11.11 | 22.2 | 1 | |
| **default, mean of 4** | **2.26** | 0.8 | 19.8 / **32.3** | 11.07 | **22.2** | 2.2 | |
| **wait, mean of 4** | **0.57** | 0.5 | 19.7 / **27.7** | 11.08 | **12.3** | 0 | 383 / 26 |

("Stalls in the route": gaps over 50 ms between presents in play; in this build they were the scripted input's reads.)

Outdoor, hub and combat at the Quest 3 size (2048 x 2208 per eye, where the graphics card sets the pace), the same
build:

| Route | Default (2 runs) | Wait (2 runs) | Pose age p99 ms, default / wait | Late waits a run |
|---|---|---|---|---|
| outdoor | 0.56, 0.56% | 0.20, 0.31% | 31.3 / 26.4 | 110-229 |
| hub | 0.41, 0.33% | 0.11, 0.19% | 25.9 / 25.0 | 39-48 |
| combat at 2048 x 2208 | 5.63, 8.17% | 3.13, 8.76% | 37.8 / 43.8 | 968-1060 |

- At the auto size every waiting run had fewer frames without a new image than every default run of the same
  route: combat 2.26% to 0.57% (4 pairs), outdoor 0.56% to 0.26%, hub 0.37% to 0.15% (2 pairs each); the pose age
  p99 fell 4.6 / 4.9 / 0.9 ms; the hold never let the game go in a waiting run (2.2 times a run without); the tick
  is unchanged.
- At the Quest 3 size the gain did not hold: 3.13 and 8.76% against 5.63 and 8.17%, a pose age p99 worse in both
  waiting runs although the frames over 25 ms fell (11 and 35% against 42 and 40%). There most images that arrive
  late still need more GPU time than the 2 ms left.
- What stays with the wait: about half the frames without a new image are a late image that arrived in time
  whose GPU work had not finished in the 2 ms left (the slot choice's fence wait), a third the frame after a late
  image, the rest late images the wait did not cover (it ran out, or its frame had no time left to wait).

With the scripted input on its own thread (the final build), combat at the auto size; `ETERNALVR_PACE_LATE_WAIT=1`,
and the same with 3.5 ms left for the GPU (`ETERNALVR_TEST_LATE_WAIT_GPU_MS=3.5`) [rig]:

| Run | Missed periods % | Stalls in the route | Pose age p50 / p99 ms | Tick p50 ms | Shown image p99 ms | Hold let go | Late waits: the image came / ran out |
|---|---|---|---|---|---|---|---|
| default 5 | 1.98 | 0 | 19.8 / 31.9 | 11.05 | 22.2 | 3 | |
| wait 5 | 0.11 | 0 | 19.7 / 25.1 | 11.12 | 11.5 | 0 | 301 / 10 |
| wait, 3.5 ms GPU 1 | 0.31 | 0 | 19.6 / 27.4 | 11.12 | 11.8 | 0 | 298 / 20 |
| default 6 | 1.83 | 0 | 19.7 / 31.5 | 11.12 | 22.2 | 2 | |
| wait, 3.5 ms GPU 2 | 0.44 | 0 | 19.6 / 27.6 | 11.13 | 12.4 | 0 | 347 / 23 |
| wait 6 | 0.46 | 0 | 19.5 / 25.9 | 11.10 | 12.0 | 0 | 558 / 41 |

Outdoors the same build: 0.02% waiting with the default 2 ms, 0.20% with 3.5 ms (one run each; the two
default-path suites `base-4` and `base-5`: 0.89 and 0.39%). With the scripted input no longer stalling the game,
the wait's frames without a new image in combat fall to 0.11 and 0.46% (default 1.98 and 1.83%), two thirds of
what is left being late images whose GPU work had not finished in the 2 ms; leaving 3.5 ms catches fewer late
images and gained nothing over these runs, so 2 ms stays.

## Review round: the gates and the GPU's share (2026-11-15)

- **Gated in the layer**, not only in the launcher: the wait runs only with `ETERNALVR_PACE=headset`, both eyes
  each game frame by the standard renderer (not with Parallel Eye Rendering, not with Alternate eyes on or auto);
  asked for and refused, the log says why once (`pace: ETERNALVR_PACE_LATE_WAIT=1 is not used: ...`;
  `features/pacing/late_wait_gate.*`, the wait itself now in `vkcore/late_wait.*`).
- **The GPU's share follows the GPU.** The slot choice's fence wait (the XR worker waiting after its choice for
  the newest image's GPU work) feeds an estimate: the largest of the last 8 frames, up at once, down only after
  30 frames in a row under 1 ms. The wait leaves the larger of 2 ms and that estimate for the late image's GPU
  work, so where the graphics card sets the pace there is no time left and the frame does not wait. The 10 s line
  counts those frames (`... N not waited for (the GPU's recent fence waits left no time); S ms left for the GPU
  on average`).
- The wait rounds its event waits down to the millisecond and stops when there is no event to wait on (it could
  spin before); the scripted input is read once in the first call, so a script is in force from the first frame,
  before its thread takes over.

Re-measure with this build (interleaved; combat at the auto size and at the Quest 3 size, 2048 x 2208) [rig]:

| Run | Missed periods % | Pose age p50 / p99 ms | Shown image p99 ms | Hold let go | Waited / not waited (GPU) | GPU busy per tick p50 ms |
|---|---|---|---|---|---|---|
| combat, default 7 | 2.65 | 19.7 / 31.1 | 22.2 | 3 | | |
| combat, wait 4 | 0.57 | 19.6 / 29.1 | 12.2 | 0 | 276 / 93 | |
| combat, default 8 | 1.74 | 19.6 / 31.3 | 22.2 | 1 | | |
| combat, wait 5 | 0.33 | 19.6 / 28.4 | 11.8 | 0 | 405 / 1 | |
| Quest 3 size, default 3 | 1.15 | 20.8 / 30.9 | 16.6 | 1 | | 6.7 |
| Quest 3 size, wait 1 | 13.63 | 26.8 / 38.2 | 19.3 | 3 | 50 / 118 | 11.9 |
| Quest 3 size, wait 2 | 8.20 | 25.2 / 36.2 | 22.2 | 6 | 129 / 154 | 10.6 |
| Quest 3 size, default 4 | 6.87 | 23.5 / 39.0 | 22.2 | 5 | | 8.9 |
| Quest 3 size, wait 3 | 9.76 | 25.4 / 40.7 | 22.2 | 5 | 50 / 172 | 11.0 |
| Quest 3 size, default 5 | 1.87 | 21.2 / 31.4 | 22.2 | 2 | | 6.9 |
| Quest 3 size, default 6 | 14.22 | 26.9 / 39.3 | 20.2 | 1 | | 12.0 |
| Quest 3 size, wait 4 | 2.39 | 21.6 / 33.5 | 22.2 | 4 | 119 / 180 | 7.5 |

- **Auto size: the gain holds.** 2.65 and 1.74% to 0.57 and 0.33%, pose age p99 31.1-31.3 to 28.4-29.1 ms (with
  the fixed 2 ms share, 0.11 and 0.46%, p99 25.1-25.9: the estimate takes some of the gain where a fence wait
  spike holds it up, 3.6 to 5.1 ms left for the GPU on average in one run).
- **Quest 3 size: the wait steps aside.** It waited in 50 to 129 frames a run (about 1,000 with the fixed share)
  and not in 118 to 180 where the GPU's recent fence waits left no time. What a run does there is set by how heavy
  its encounter is on the graphics card (GPU busy per tick 6.7 to 12.0 ms between runs of either arm, and the
  missed periods with it): at the same load the waiting runs are no worse (12.0 ms default 14.22%, p99 39.3; 11.9
  ms waiting 13.63%, p99 38.2; 8.9 ms default 6.87%, p99 39.0; 7.5 ms waiting 2.39%, p99 33.5). The four waiting
  runs drew the heavier encounters (10.3 ms on average against 8.6), so their means (8.50%, p99 37.2 against
  6.03%, 35.2) cannot be read as the wait's cost; eight runs cannot separate the two further.

## Built

- `ETERNALVR_PACE_LATE_WAIT=1` (was the rig's `ETERNALVR_TEST_LATE_WAIT`) and the launcher's "Wait for a late
  frame (testing)" on the Play tab under Frame pacing: off by default, offered only with Frame pacing matched to
  the headset, in stereo, without Alternate eyes or Parallel Eye Rendering (neither measured; the layer refuses
  them too). It leaves the late image's GPU work the larger of 2 ms and the slot choice's recent fence waits, so it
  steps aside where the graphics card sets the pace. It changes what a player sees only when ticked; for a
  headset look.
- `ETERNALVR_TEST_LATE_WAIT_GPU_MS` (rig): the GPU's share after the wait, 0.5 to 8 ms (2 by default).
- The scripted input read on its own thread (rig only; started only when `ETERNALVR_TEST_INPUT` is set, after
  one read at the first call): the bench and QA scenarios no longer stall the game's frame and the headset's frame on a file read, so the
  hitch counts of earlier bench runs on this PC included the rig's own stalls.
- The rig tables: `ETERNALVR_TEST_CPU_CSV`, `pose_located` in the latency table, `ETERNALVR_TEST_SLOW_CALLS`; the
  bench copies the latency and CPU tables beside the frames table when a run writes them.

## Limits

- One PC (Intel desktop CPU on Balanced, RTX 3080 Ti) and the OpenXR simulator; no headset. The wait submits a
  frame up to 5 ms later than without it: whether a runtime's compositor takes such a frame in time, and whether
  the image then looks smoother, is for a headset to show.
- Two to four runs per arm; the Quest 3 size's heavy encounter varies a lot between runs (GPU busy per tick 6.7
  to 12.0 ms, missed periods 1.2 to 14.2% in either arm), so its runs say that the wait steps aside there and is
  no worse at the same load, not more.
- The slow file reads are put down to the scan on open of a freshly written file without checking which process
  held the file [inferred]; with the read on its own thread it no longer matters to the measurements.
