# Performance targets on the test PC (2026-11-16)

The ROADMAP's beta box "Performance targets measured and recorded (T-075, T-105)": separately with DLSS Quality
alone and with Balanced fixed foveation alone, p99 frame time against the 11.1 ms period and missed frames; Route
S's stereo overhead against the mono reference; the interop copies against their budget of 1.0 ms GPU per frame at
Quest 3 size; the longest shader-compile hitch after the first run (target 50 ms); each record with the HAGS state
(T-085). The definitions are T-075, T-105 and T-111; section 4 says where the test PC cannot meet one and how the
measurement differs.

Tags: **[rig]** measured on the test PC, **[log]** read from the layer's logs, **[code]** read in the code,
**[inferred]** a reading the rig has not confirmed.

## 1. Method

**The record (T-105).** Test PC: RTX 3080 Ti (12 GB), i7-9700K, Windows 11 on the Balanced power plan, NVIDIA
driver 610.88, hardware-accelerated GPU scheduling **on** (`HwSchMode` not set in the registry, so the driver's
default; dxdiag: `Hardware Scheduling: DriverSupportState:Stable Enabled:True`); the game's Steam exe (SHA-256
`69dc13e8...6a1c11`); the runtime OpenXR-Simulator ("OpenXR Simulator Runtime" 1.0.27, API 1.1) at 90 Hz on the
virtual display, which recommends 1280 x 1400 per eye (eye FOVs 54 degrees out, 40 in, 44 up, 54 down); the DLSS
DLL 310.9.1.0 (the launcher's newest-DLL route) in the DLSS runs. One layer build for every run (this branch,
release).

**The routes and settings.** The three benchmark routes of `bench-routes.md` (`bench-combat`, `bench-outdoor`,
`bench-hub`: 60 s of scripted input, measured over six whole 10 s windows), at the launcher's defaults as the bench
scenarios pin them (per-eye TAA, frame pacing matched to the headset, the UI layer, no foveation, no DLSS, no depth
layer, ray tracing held off; Shared sun shadows and Wait for a late frame off), each with one change:

| Configuration | What changes |
|---|---|
| default | nothing: per-eye TAA |
| DLSS Quality | `ETERNALVR_STEREO_DLSS=1`, `ETERNALVR_STEREO_DLSS_QUALITY=quality`, the 310.9.1.0 DLL, preset K (what the launcher passes for DLSS Quality with the newest DLL): each eye rendered at 1365 x 1472 into 2048 x 2208 (both eyes' DLSS features made, about 88 evaluations a second per eye) |
| Balanced foveation | `ETERNALVR_FOVEATION=balanced` (the launcher's Balanced): full rate within 24 degrees of head-forward, half rate to 56, quarter outside (12.6% / 81.6% / 5.8% of each eye at 2048 x 2208) |
| mono reference | the launcher's Mono (`ETERNALVR_MODE=mono`, pacing off, no stereo cvar holds): one view at the size with both eyes' pixel count, 2896 x 3120 for 2048 x 2208 per eye (9.04 Mpix), 1808 x 1984 for 1280 x 1400 (3.59 Mpix); ray tracing turned off by the route's debug schedule, since mono holds no cvars at run time and the profile's value wins over the command line |
| default, pacing off | the default with `ETERNALVR_PACE=off`, the mono reference's pacing (for the overhead at the auto size) |
| DLSS Quality, preset E | not in the box: the DLSS run with `ETERNALVR_DLSS_PRESET=E` (the launcher's "older model, faster"), two outdoor runs and one hub run, after the preset K runs showed no gain |

Sizes: 2048 x 2208 per eye (the `-q3` scenarios: Quest 3 size asked, fitted into the simulator's 2048 px per-eye
limit), where the GPU is the limit, and the auto size 1280 x 1400, where the processor is, for the overhead's CPU
side.

**Interleaving and spread.** The GPU load of the combat route swings with its encounter, so the configurations ran
interleaved, in rounds of one run each, in a changed order every round: three runs per configuration on combat, two
on outdoor and hub. Every run's GPU busy time per tick is in the tables, and configurations are compared at matched
GPU load, never on one pair. Two runs were left out: one hub run whose title screen never reached the main menu
(Bethesda.net), skipped by the suite, and one Balanced hub run in which the game's "unable to contact game
services" notice paused the route after 24 s; both were run again.

**What each run records** (the bench summary, `bench-routes.md` section 4, plus two rig switches added for this
job, both off by default):

- the shown image (how long each new image stayed in the headset) p50 / p99 / max, the `xrEndFrame` interval p99
  (T-105's VR frame time), and the display periods without a new image (missed frames, the runtime's skipped
  periods included);
- the CPU tick (eye L plus eye R present to present; mono: one frame) p50 / p99 / max;
- GPU busy per tick: eye L, eye R and the tick (mono: per frame), from the timestamps of every frame
  (`ETERNALVR_GPU_TIMING=1`);
- the interop copies' GPU time (`ETERNALVR_TEST_COPY_GPU=1`, new): three timestamps around the present hook's copy
  of each eye (and the GUI image) into the shared ring, on the game's queue, and around the XR worker's D3D12 copy
  of the ring into the headset's swapchain image (and of the GUI image into the UI layer's), summed per new headset
  image. A part's time runs from the end of the work before it on its queue to its own end, so other GPU work
  running alongside can only lengthen it: an upper bound of the copies' own cost;
- the game's pipeline creations per 10 s (`ETERNALVR_TEST_PIPELINES=1`, new) beside the stall watch's lines for
  gaps over 50 ms between presents (each with the pipelines created in it);
- VRAM peak, and the HAGS state above (the same for every run).

## 2. Results [rig]

### 2048 x 2208 per eye (the simulator's largest, 67% of Quest 3's pixels)

| Route | Configuration | Runs | GPU busy per tick, ms (each run) | eye L / eye R ms | Shown image p50 / p99 ms | Display periods without a new image % | `xrEndFrame` interval p99 ms | CPU tick p50 / p99 ms | Longest tick / shown image ms | VRAM peak MB |
|---|---|---|---|---|---|---|---|---|---|---|
| combat | default | 3 | 7.6, 11.3, 10.6 | 5.5 / 4.3 | 11.1-12.2 / 18.2-22.2 | 1.59-12.61 | 12.2-17.7 | 11.1-12.4 / 14.0-18.4 | 22-41 / 26-46 | 8053-8054 |
| combat | DLSS Quality | 3 | 10.5, 10.4, 9.8 | 5.8 / 4.4 | 11.1-11.5 / 17.2-22.2 | 5.39-8.09 | 14.9-15.9 | 11.6-11.9 / 15.5-16.7 | 18-20 / 23-28 | 8201-8207 |
| combat | Balanced foveation | 3 | 7.5, 8.0, 7.9 | 4.2 / 3.6 | 11.1 / 22.2 | 2.48-3.52 | 13.6-14.4 | 11.2-11.3 / 14.5-15.3 | 18-26 / 25-27 | 8021-8069 |
| combat | mono reference (2896 x 3120) | 3 | 8.0, 7.1, 8.9 | - | 11.7-11.9 / 15.6-23.1 | 4.00-8.46 | 15.5-15.6 | 8.0-9.6 / 11.6-16.1 | 15-19 / 22-29 | 8966-9030 |
| outdoor | default | 2 | 11.4, 11.3 | 6.7 / 4.7 | 11.6 / 20.4-20.8 | 5.98-6.04 | 14.1-14.4 | 11.8 / 14.2-14.3 | 21-22 / 25-26 | 8065-8066 |
| outdoor | DLSS Quality | 3 | 11.5, 10.7, 11.3 | 6.6 / 4.5 | 11.2-11.8 / 17.1-20.9 | 4.11-6.93 | 14.2-14.5 | 11.4-11.9 / 14.1-14.5 | 17-20 / 24-26 | 8230-8263 |
| outdoor | Balanced foveation | 2 | 8.8, 8.8 | 4.8 / 4.0 | 11.1 / 13.3-21.7 | 0.98-1.52 | 12.0-12.4 | 11.1 / 12.9-13.6 | 15-57 / 24-58 | 8051-8079 |
| outdoor | DLSS Quality, preset E | 2 | 7.8, 8.3 | 4.3 / 3.8 | 11.1 / 11.8-12.3 | 0.33-0.78 | 11.6 | 11.1 / 12.8-12.9 | 15 / 22-23 | 8145-8162 |
| outdoor | mono reference (2896 x 3120) | 2 | 8.1, 8.1 | - | 11.7-11.8 / 15.1-15.8 | 5.57-6.55 | 15.0-15.8 | 9.0 / 11.3-11.4 | 14-17 / 20-31 | 8990-9006 |
| hub | default | 2 | 8.8, 8.7 | 4.8 / 3.9 | 11.1 / 20.8-21.7 | 1.31-1.87 | 12.4-13.3 | 11.2 / 13.4-13.6 | 19-23 / 24 | 7961-7989 |
| hub | DLSS Quality | 3 | 9.1, 9.4, 9.3 | 5.3 / 4.0 | 11.1 / 20.4-22.2 | 1.20-2.17 | 12.3-15.0 | 11.1 / 13.3-15.8 | 29-37 / 35-44 | 8141-8143 |
| hub | Balanced foveation | 2 | 6.8, 6.9 | 3.6 / 3.2 | 11.1 / 11.3-11.5 | 0.20-0.50 | 11.3 | 11.1 / 12.7-12.8 | 26-30 / 25-38 | 7952-7978 |
| hub | DLSS Quality, preset E | 1 | 6.3 | 3.3 / 3.0 | 11.1 / 11.2 | 0.17 | 11.2 | 11.1 / 12.7 | 27 / 24 | 8043 |
| hub | mono reference (2896 x 3120) | 2 | 7.5, 7.7 | - | 11.8 / 15.2 | 6.15-6.39 | 15.2 | 8.4-8.6 / 11.3 | 13-17 / 19-31 | 8885-8887 |

### Auto, 1280 x 1400 per eye

| Route | Configuration | Runs | GPU busy per tick, ms (each run) | eye L / eye R ms | Shown image p50 / p99 ms | Display periods without a new image % | `xrEndFrame` interval p99 ms | CPU tick p50 / p99 ms | Longest tick / shown image ms | VRAM peak MB |
|---|---|---|---|---|---|---|---|---|---|---|
| combat | default | 3 | 4.9, 6.2, 5.6 | 2.9 / 2.7 | 11.1 / 22.2 | 1.94-2.70 | 11.7-12.4 | 11.1 / 13.8-14.1 | 16-18 / 25-26 | 7020-7037 |
| combat | default, pacing off | 3 | 5.4, 6.1, 6.0 | 3.0 / 2.8 | 11.1 / 12.5-14.0 | 0.41-0.81 | 12.3-12.7 | 8.6-9.1 / 12.4-13.6 | 17-18 / 23-25 | 7022-7037 |
| combat | mono reference (1808 x 1984) | 3 | 4.7, 4.0, 4.8 | - | 11.1 / 14.8-17.0 | 0.19-1.32 | 14.7-16.9 | 5.0-5.5 / 8.2-9.9 | 12-20 / 23-50 | 7493-7509 |
| outdoor | default | 2 | 6.3, 6.4 | 3.3 / 3.1 | 11.1 / 11.4 | 0.35-0.39 | 11.3-11.4 | 11.1 / 12.7 | 15-24 / 23-26 | 7080-7082 |
| outdoor | default, pacing off | 2 | 7.5, 7.7 | 4.0 / 3.6 | 11.1 / 12.1 | 0.02-0.09 | 12.1 | 9.0-9.2 / 11.6-11.8 | 14-21 / 21-23 | 7101 |
| outdoor | mono reference (1808 x 1984) | 2 | 5.0, 5.0 | - | 11.1 / 14.9-15.6 | 0.07-0.09 | 14.8-15.5 | 5.7 / 8.0 | 12-14 / 23-24 | 7519 |
| hub | default | 2 | 5.0, 4.8 | 2.5 / 2.4 | 11.1 / 11.2-11.3 | 0.09-0.31 | 11.2 | 11.1 / 12.7-12.9 | 23-29 / 22-36 | 6950-7001 |
| hub | default, pacing off | 2 | 5.8, 5.8 | 3.0 / 2.8 | 11.1 / 12.2-12.3 | 0.00-0.04 | 12.2-12.3 | 7.2 / 9.8-9.9 | 16 / 18-20 | 6976-6985 |
| hub | mono reference (1808 x 1984) | 2 | 4.2, 4.2 | - | 11.1 / 15.9-16.0 | 0.20-0.30 | 15.9-16.0 | 4.8 / 7.4-7.5 | 11-15 / 24-25 | 7378 |

How to read them:

- **The shown image's p99 moves in whole periods.** An image shows for one period (11.1 ms) or two (22.2); the
  p99 is 11.1 to 11.5 ms only while fewer than about 1% of the display periods go without a new image, and jumps
  to 17 to 22 ms past that. So the target "p99 within the 11.1 ms period" and the missed frames read the same
  thing; the `xrEndFrame` interval p99 (T-105's frame time) is lower because the simulator keeps calling at its
  rate and skips a period only when the GPU is full.
- **At 2048 x 2208 the GPU sets the pace.** Default: outdoor 11.3 ms of GPU per tick, 6.0% of the periods without
  a new image; hub 8.7 ms, 1.3-1.9%; combat 7.6 to 11.3 ms with its encounter, 1.6 to 12.6% (the run at 7.6 ms
  missed 1.6%, the runs at 10.6 and 11.3 ms 10.0 and 12.6%). Eye L costs more than eye R (6.7 against 4.7 ms
  outdoors): eye R reuses the tick's shadow maps and other per-tick work.
- **The combat route compares only at matched load.** Its GPU load per tick moves by up to 4 ms between runs of
  one configuration (default 7.6 to 11.3), more than any configuration's effect; outdoor and hub repeat within
  0.1 to 0.8 ms and carry the comparisons below.

### DLSS Quality

- **Preset K gives the GPU nothing back at this size on this card.** Outdoor 10.7-11.5 ms against 11.3-11.4 ms
  default, hub 9.1-9.4 against 8.7-8.8 (0.5 ms more), combat 9.8-10.5 against the default's 7.6-11.3; the missed
  periods follow (outdoor 4.1-6.9% against 6.0%, hub 1.2-2.2% against 1.3-1.9%). Each eye renders 44% of the
  pixels, so the transformer model's own pass costs about what the smaller render saves [inferred: its cost was
  not timed apart; eye L 6.6 against 6.7 ms outdoors]. VRAM is 150 to 200 MB higher.
- **Preset E (the older model) does give it back.** Outdoor 7.8-8.3 ms (31% less than default), 0.33-0.78% of the
  periods without a new image, shown p99 11.8-12.3 ms; hub 6.3 ms (27% less), 0.17%, p99 11.2 ms. Two and one runs:
  enough to say the transformer pass is the cost on this card, not a measure of E's image in a headset.

### Balanced foveation

- **The biggest single gain at this size.** Outdoor 8.8 ms (22% less than default), 1.0-1.5% of the periods
  without a new image (p99 13.3-21.7 ms); hub 6.8-6.9 ms (22% less), 0.20-0.50%, p99 11.3-11.5 ms: the hub meets
  the 11.1 ms target with it. Combat 7.5-8.0 ms in three runs and 2.5-3.5% missed; the default's matching run
  (7.6 ms) missed 1.6%, so on combat foveation shows no gain at matched load in these runs.
- What the half and quarter rate cost the picture outside the central 24 degrees is for a headset to judge
  (ROADMAP's fixed foveation default box).

### Stereo overhead against the mono reference (T-075's Route S budget: GPU at most 30% over mono, render-thread CPU at most twice)

| Route, size | Stereo GPU per tick ms | Mono GPU per frame ms | Stereo / mono | Stereo CPU tick p50 ms | Mono CPU frame p50 ms | Stereo / mono |
|---|---|---|---|---|---|---|
| combat, 2048 x 2208 (default) | 9.82 (7.6-11.3) | 7.98 (7.1-8.9) | 1.23 (runs 0.95 to 1.60) | GPU-bound | GPU-bound | - |
| outdoor, 2048 x 2208 (default) | 11.35 | 8.11 | **1.40** | GPU-bound | GPU-bound | - |
| hub, 2048 x 2208 (default) | 8.71 | 7.64 | 1.14 | GPU-bound | GPU-bound | - |
| combat, auto (pacing off) | 5.85 | 4.50 | 1.30 | 8.9 | 5.3 | 1.68 |
| outdoor, auto (pacing off) | 7.58 | 5.02 | **1.51** | 9.1 | 5.7 | 1.59 |
| hub, auto (pacing off) | 5.81 | 4.22 | **1.38** | 7.2 | 4.8 | 1.49 |
| combat, auto (default, paced) | 5.57 | 4.50 | 1.24 | held to 11.1 | 5.3 | - |
| outdoor, auto (default, paced) | 6.37 | 5.02 | 1.27 | held to 11.1 | 5.7 | - |
| hub, auto (default, paced) | 4.92 | 4.22 | 1.17 | held to 11.1 | 4.8 | - |

- **GPU: over the 30% budget outdoors, within it in the hub.** At 2048 x 2208 stereo costs 1.40 times the mono
  reference outdoors (3.2 ms per tick), 1.14 times in the hub; combat's encounter swing leaves 0.95 to 1.60 by
  round. At the auto size with the same pacing as mono (off) it is 1.30 to 1.51 times; with the default pacing
  1.17 to 1.27 times, because ticks held to the headset leave the GPU less to overlap [inferred]. What stereo
  adds over one view of the same pixel count is the per-view work done twice (eye L's 6.7 ms against eye R's
  4.7 outdoors shows eye R already skips part of it).
- **CPU: within the budget.** With pacing off on both sides (both CPU-bound at the auto size), a stereo tick takes
  1.49 to 1.68 times a mono frame present to present (the CPU split: the game frame with eye L's views 3.9 to
  5.0 ms of wall time, eye R's render 3.3 to 4.3 ms). The layer's timers split the stereo tick only; for mono the
  present-to-present frame time is the reference, so the ratio is of frame times, not of the render thread
  alone.
- At 2048 x 2208 both sides are GPU-bound, so the CPU ratio is read at the auto size only.

### Interop copies (T-075: 1.0 ms GPU per frame at Quest 3 size, colour, depth and UI, both directions)

| | 2048 x 2208 per eye (26 stereo runs) | 1280 x 1400 (14 stereo runs) |
|---|---|---|
| Into the ring, per present (an eye and its GUI image) | 0.050-0.059 ms mean (p99 0.07-0.56, rare peaks 1.8-3.6) | 0.022-0.033 ms |
| The ring into the headset's image, per new image | 0.070-0.073 ms (p99 0.10-0.11) | 0.030-0.040 ms |
| The GUI image into the UI layer's, per new image | 0.110-0.121 ms | 0.050-0.054 ms |
| **All of them per new headset image** | **0.281-0.300 ms** | **0.131-0.192 ms** |
| The desktop mirror, per new image | 0.001-0.002 ms | 0.001-0.004 ms |

- **0.3 ms per frame at 2048 x 2208, under a third of the budget.** The copies scale with the pixels; at Quest 3
  size (1.49 times the pixels) that is about 0.42 to 0.45 ms [inferred]. The depth layer (off by default) adds
  its own copies: at 2048 x 2208 about 0.12 ms of GPU a tick for its two shaders and about 0.15 ms on the D3D12
  copy (`presenter.md`), so about 0.85 ms at Quest 3 size with it on, still within the budget [inferred].
- The rare per-copy peaks of 2 to 4 ms are the copy waiting for GPU work running alongside (the timestamps start
  when the work before it on its queue is done), not the copy itself [inferred].

### Shader-compile hitch and the longest hitches

- **The game builds its pipelines at the map load.** About 1,050 pipelines at each load, 260 to 370 ms in all,
  the longest call 2 to 16 ms (62 runs). During the routes it creates 0 or 1 pipelines a run, each call 0.4 ms at
  most. The game keeps no pipeline cache file of its own in its folders on the test PC; the driver's shader
  cache keeps the compiled pipelines between runs, and it was warm for every run of the day (the same routes ran
  on earlier days), so every run here is a run "after the first run" as T-075 defines it.
- **Longest shader-compile hitch after the first run: 0.4 ms (target 50 ms) [rig, log].**
- **The longest hitches of any kind** were a 57.5 ms tick (outdoor, Balanced foveation, one run), 40.9 ms
  (combat, default, one run) and 37.1 ms (hub, DLSS); no pipeline was created in their 10 s windows and no gap
  between presents passed 50 ms (the stall line's threshold) during any route. No frame over 250 ms in any run
  (the hard gate's measure). Their cause is not identified here (`frame-tails.md` traces the known ones).
- A first run with a cold driver cache was not measured (it would need the driver's shader cache emptied);
  T-075's target is for the runs after it.

### HAGS and VRAM

- HAGS on (the driver's default) for every run; each run's record (the rig's run file) carries the registry
  state, `HwSchMode` not set, which on this driver means on. T-021's preflight warning applies to this PC.
- VRAM peak 7.0 GB at the auto size, 8.0-8.3 GB at 2048 x 2208 (DLSS 150-200 MB more), 8.9-9.0 GB for the mono
  reference's 2896 x 3120, against a budget of 11.3 GB on the 12 GB card.

## 3. Against the targets

| Target (T-075, T-105) | On the test PC at 2048 x 2208 | Met? |
|---|---|---|
| p99 frame time within 11.1 ms, DLSS Quality alone | shown image p99 17-22 ms, 1.2-8.1% of the periods without a new image on every route | no |
| p99 frame time within 11.1 ms, Balanced foveation alone | hub 11.3-11.5 ms (0.2-0.5%); outdoor 13.3-21.7 ms (1.0-1.5%); combat 22.2 ms (2.5-3.5%) | hub only |
| (default, for reference) | 18-22 ms, 1.3-12.6% | no |
| Route S overhead: GPU at most 30% over mono | 1.14 (hub) to 1.40 (outdoor); auto size 1.17-1.51 | no outdoors |
| Route S overhead: render-thread CPU at most twice mono | 1.49-1.68 (frame time, auto size, pacing off) | yes |
| Interop copies 1.0 ms GPU per frame at Quest 3 size | 0.28-0.30 ms at 67% of the pixels, about 0.45 ms at Quest 3 size [inferred] | yes at 2048 x 2208; Quest 3 size inferred |
| Shader-compile hitch after the first run, 50 ms | 0.4 ms (the longest pipeline call in play) | yes |
| HAGS state in every record | on (the driver's default), in this record; every run's record carries the registry state | yes |

## 4. How the test PC differs from the definitions

- **Size.** The simulator stops at 2048 px per eye, so 2048 x 2208 (4.52 Mpix, 67% of Quest 3's 2496 x 2688)
  stands for Quest 3 size; the 90 Hz target at Quest 3 size itself is not measured. On a real Quest 3 the
  launcher's auto size renders about 4.56 Mpix per eye (`bench-routes.md` section 3), close to this size.
- **GPU and CPU.** An RTX 3080 Ti and an i7-9700K, not the RTX 4080 the gates name (REQ-20).
- **Runtime.** The OpenXR simulator, not a headset's runtime: no compositor, no reprojection to switch off (T-111's
  "reprojection off" holds by default), 90 Hz only. The frame log's `predictedDisplayPeriod` stayed at the
  refresh period.
- **Runs.** T-105's gate run is one warm-up run and three consecutive runs per route on one build and driver. Here
  the configurations were interleaved, three runs on combat and two on outdoor and hub per configuration (three
  for DLSS Quality), all after the day's warm-up runs.
- **Settings.** T-111 pins the headset profile's default DLSS mode and foveation preset; the box asks for DLSS
  Quality alone and Balanced foveation alone, so each ran on the launcher's defaults with only that change.
- **GPU timing.** GPU busy time covers the queues the layer can time: the game's transfer queue is not timed, in
  every configuration alike.
- **The mono reference.** T-075 asks for one view at the enclosing camera's FOV with both eyes' pixel count. The
  launcher's Mono renders the head-tracked view at its own FOV (108 x 108.5 degrees here, against the eyes'
  enclosing 108 x 98), at both eyes' pixel count, with pacing off, the game's own TAA and ray tracing turned off
  from the debug schedule. The CPU side compares present-to-present frame times, since the layer's CPU split
  covers stereo ticks only.

## 5. What only a headset or a true Quest 3 size can show

- The frame time and missed frames at 2496 x 2688 per eye, and on a headset's compositor (which reprojects
  where the simulator skips a period).
- Whether Balanced foveation's half and quarter rate, and DLSS preset E's softer image, are acceptable in the
  headset; the numbers here only say what they save.
- The copies' time at Quest 3 size (estimated above), with the depth layer on, and with a headset runtime's own
  swapchain formats.
- A first run on a cold driver shader cache.

## 6. Runs

On the test PC, one folder per run named `<configuration>-<route>-<round>` (`def`, `dq`, `dqe`, `fb`, `mono`,
`spo`; `cq`, `oq`, `hq` at 2048 x 2208, `c`, `o`, `h` at auto), each with the suite's report, the layer log, both
tables and `bench.json`, 2026-11-16 09:51 to 12:51.
