# Benchmark routes

The three 60-second routes the performance gates and targets are measured on (ROADMAP M1 and the performance
gates of M4 and M9; T-075, measured as T-105 and T-111 define). Each starts from a named start the game loads the
same way every time, runs scripted input, and is a by-name QA scenario (`tools/rig/qa/qa-scenarios-bench.ps1`) that
writes one summary line per run. Results: `perf-baselines.md`.

## 1. The routes

| Route | Scenario | Start | The 60 s | Why this one |
|---|---|---|---|---|
| Combat arena | `bench-combat` | Cultist Base, `game/sp/e1m3_cult/e1m3_cult -checkpoint cp_03_shoot_gate`; the debug schedule turns god mode on and teleports the player to the Slayer Gate's golden gore nest (`teleport interact_slayer_gate_nest_1`), the scripted hand aims at it and presses Use (aims tried in turn until the nest's sync starts: on the test PC the first, 90 degrees left and 25 down, in 8 of 8 runs), and 2.5 s after the sync the game itself teleports the player into the gate's arena, whose encounter starts. The route begins 10 s after the sync | Standing still: bursts of fire straight ahead (0.6 s on, 0.4 s off) and a quarter turn (0.4 s of the turn stick, about 90 degrees at the default 230 degrees a second) twice in every 10 s, so the view sweeps the arena three and a half times | A heavy scene the rig can start by script: an outdoor arena full of demons (imps, soldiers and larger demons), their projectiles, the gun's muzzle flashes, fire and gore particles, all GPU- and CPU-heavy at once. A `+map` load spawns only the map's world layer, not the checkpoint's own layers, so most encounters never start (rig, 2026-09-30); this one the game starts itself, so the demons are the game's own |
| Outdoor | `bench-outdoor` | Exultia, `game/sp/e1m2_battle/e1m2_battle` (the map's own start, the start the QA suite uses for its baseline); god mode | 1.2 s of walking forward out of the start room's opening onto the balcony before the route; then the view held on the vista: ahead, right, ahead, left, each a 0.3 s push of the turn stick (equal pushes each way, so the view does not drift), in every 10 s | Long view distance: across the valley to the citadel, its walls and the mountains behind, in fog, many distant buildings; no combat |
| Hub | `bench-hub` | The Fortress of Doom, `game/hub/hub`, loaded the way a player gets there: the title screen, Campaign, Game Slot 1, Continue Game (the 100% save in slot 1, so the late Fortress) | Standing still on the bridge where the save starts, a quarter turn twice in every 10 s (as the combat route, without firing) | The hub the player returns to between missions: a large interior with the view of Earth through the bridge's window, many lights, no demons |

Each route also has a `-q3` scenario (`bench-combat-q3`, `bench-outdoor-q3`, `bench-hub-q3`) that asks for Quest 3
size (`ETERNALVR_RENDER_SIZE=2496x2688`); the plain ones use the launcher's `auto` size.

What the scripts do not do: walk through the level, take damage (god mode) or follow the encounter's later waves on
purpose. The input is the same in every run, but the demons' behaviour is not, so the combat route has the widest
run-to-run spread of the three. The checkpoint names come from what the QA suite already loads (a name the map does
not have crashes the game at load); the outdoor route uses the map's own start because no named checkpoint of
Exultia's is checked on the test PC yet (`cp_09_first_signal` is named in `bhaptics-launch.md` but was never run).

## 2. Settings

- **The launcher's defaults**: the environment `EternalVR.Launcher.exe --dry-run` prints for a fresh data folder
  (0.1.94, on the test PC): stereo, per-eye TAA, render size `auto` at scale 1.00, frame pacing `headset` (the hold
  switch: one image per headset frame while the game keeps up, new frames only when it falls behind), the UI layer
  with the HUD panel, motion controllers with hand aim, arms shown, the CPU Saver's default item
  (`is_cacheGreedily=0`), no foveation, no DLSS, no depth layer. The suite's own differences: the cinematic skip on
  (the launcher's default is off; no cutscene plays on these routes), and the window and the mirror at 1280 x 720 on
  the virtual display.
- **The game's own settings**: the test PC's profile (the game's highest preset with ray-traced reflections),
  with ray tracing held off for the run (`ETERNALVR_DEBUG_CVARS=r_useRayTracing=0`: the gates are RT off). The game
  saves that into its profile; the suite puts the Steam Cloud files back after each run.
- **Measurement switches**: `ETERNALVR_GPU_TIMING=1` (every frame timed, the GPU table), `ETERNALVR_CPU_TIMING=1`
  (the CPU split), `ETERNALVR_TEST_UNFOCUSED_CAP=0` (the simulator's session is VISIBLE whenever its window is not in
  front, where a headset's would be FOCUSED; without it the layer holds the game to one image per display period).
  The game runs at Normal priority (the scenario sets it and notes what it was: Normal already in every run on the
  test PC).
- **Reprojection**: the OpenXR simulator has none. On a headset the gate runs switch off Meta's ASW and Virtual
  Desktop's SSW and record it (T-111).

## 3. Runtime and size

ARCHITECTURE section 11 asks each benchmark run to record the runtime setting and the size the runtime reports.

| Runtime | Setting | Recommended per eye | Asked | Rendered per eye |
|---|---|---|---|---|
| OpenXR-Simulator (test PC, 90 Hz, no refresh setting) | none | 1280 x 1400 (maximum swapchain 4096: 2048 per eye side by side) | `auto` | 1280 x 1400 |
| OpenXR-Simulator | none | 1280 x 1400 | `2496x2688` (Quest 3 size) | 2048 x 2208 (fitted into the simulator's limit, the aspect kept) |
| Virtual Desktop's runtime with a Quest 3 (probe, `render-size.md`) | VDXR's quality setting as the tester's | 2496 x 2688 | `auto` | 2056 x 2216 (the launcher's 2064 x 2208 pixel budget) |

The simulator cannot render Quest 3 size itself: its swapchain is at most 4096 pixels wide and Route S keeps both
eyes side by side. Its 2496 x 2688 run renders 4.52 Mpix per eye against Quest 3's 6.71 (67%), close to the 4.56 Mpix a real
Quest 3 gets at the launcher's default (`auto`, the budget); the `auto` run renders 1.79 Mpix. The simulator has no refresh-rate setting
(`preview_fps` in its settings.json is only its preview window), so every rig run is at 90 Hz; the 72 Hz gate is
read from the same tick times (section 4).

## 4. What a run records

Every run's script lasts 70 s; the measurement is the six whole 10 s windows of the layer's lines inside it (60 s),
the per-frame rows of the GPU table (`eternalvr-gpu-<pid>.csv`) and the frames table (`eternalvr-frames-<pid>.csv`) in
those windows, found by the counts the 10 s lines carry. The summary line (a note in the report, `bench.json` in the
scenario's folder):

- **Ticks/s**: the game's stereo ticks a second (the `rates:` lines), and the lowest 10 s window.
- **Tick CPU**: present to present of one eye L frame plus its eye R frame (the time a stereo tick takes, waits for
  the headset included), p50/p95/p99/mean over every tick. This is the game's frame time.
- **GPU busy per tick**: eye L plus eye R GPU busy time from the timestamps, p50/p95/p99/mean, and its share of the
  tick's CPU time: 85% or more is called GPU-bound, below that CPU-bound.
- **Shown image**: how long each new image stayed in the headset (the predicted display times of the XR frames that
  showed a new image, frame to frame), p50/p95/p99/mean. The gate "p99 frame time within two display periods" reads
  here: at 90 Hz 22.2 ms.
- **Display periods without a new image** (the missed headset frames at the runtime's rate): the display periods
  from the first to the last XR frame of the windows, less the XR frames that showed a new image (the frames
  table's `repeat` column `new`), as a share of the periods. It counts both an XR frame that repeated the last
  image and a period the runtime skipped (its next display time more than 1.5 periods on; the simulator does this
  when the GPU is full, as a headset's compositor would reproject). The two parts are in the line too.
- **Pose age** (T-111): from the pose's `xrLocateViews` to the `xrEndFrame` that submitted the frame.
- **VRAM**: the process's peak local video memory and the budget (`vram:` lines).
- **CPU split**: the whole process's CPU per tick and cores busy, the wall time of the game frame with eye L's views
  and of eye R's render (`cpu:` lines).
- **Frames over 250 ms** (the hard gate's): XR frame intervals and shown-image times over 250 ms.

- **Over a 72 Hz period**: the share of ticks, and of ticks' GPU busy time, over 13.9 ms.

The 72 Hz reading of a 90 Hz run is an inference: a tick longer than 13.9 ms would miss a 72 Hz frame, and the
p99 tick against 27.8 ms is the 72 Hz two-period gate. At 72 Hz the headset hold waits less per tick, so the
share is an upper bound for the same work.

## 5. How to run

On the rig (simulator), three suites so a change in the PC's background load spreads over every route:

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools\rig\qa\qa-suite.ps1 -LayerSrc <release build>\src\vkcore `
    -Only bench-combat,bench-outdoor,bench-hub,bench-combat-q3,bench-outdoor-q3,bench-hub-q3 -Out <dir>\QA-bench1
(the same with QA-bench2, QA-bench3)
powershell -NoProfile -File tools\rig\qa\qa-bench-table.ps1 <dir>\QA-bench1 <dir>\QA-bench2 <dir>\QA-bench3
```

Each run takes about three minutes (the Steam Cloud files go back between runs, about 30 s more): a suite of six
about 18 minutes. Use the release build (`windows-msvc-release`), the PC idle (no build, no other game run).

**On the RTX 4080 machine with the headset** (the gate runs, [owner]): the suite starts the game on the simulator's
runtime (`openxr_simulator_rig.json`); a run on the headset's runtime needs the suite to take another runtime
manifest, which it does not yet. Until then the routes are played by hand from the same starts with the same
settings, and the layer's tables give the same numbers.
