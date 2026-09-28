# 11 -- Performance feasibility and budget

Status: research notes, 2026-09-25. Nothing here has been measured on the target rig yet. Tags:
**[C]** = confirmed from a benchmark, vendor document, source code or local data dump; **[U]** =
estimate, inference or unverified. Every modelled frame time in this document is [U] by construction.
Reference material for this topic is in `reference/performance/` (see its `MANIFEST.part.md`).
Upscaling (doc 07) and foveated rendering (doc 08) are covered separately; this document only uses
their cost figures as inputs.

---

## 1. Summary

- **DOOM Eternal is one of the cheapest AAA renderers per pixel on PC.** An RTX 4080 averages 244.5 fps
  at 4K Ultra Nightmare without ray tracing (TechPowerUp, i9-13900K) [C]. That is about 2,030 Mpix/s.
  The CPU side is even lighter: a 2-core Pentium averaged 180 fps at 1080p low [C: Tom's Hardware].
- **Owner's anchor:** "on 4K I can hold 120 with ray tracing" on the RTX 4080 + 9950X3D rig
  [C, owner-measured; DLSS mode, preset and scene unknown]. 4K x 120 = 995 Mpix/s. Our model predicts
  about 147 fps average at 4K native with RT on a 4080, so "hold 120" reads as a floor under an average
  near 145-150; that is consistent if DLSS was off and inconsistent-but-harmless if DLSS was on.
- **Quest 3 at the VDXR-recommended 2496x2688 per eye is 13.4 Mpix per stereo frame, 1.62x a 4K
  frame.** At 90 Hz that is 1,207 Mpix/s: 1.21x the owner's RT anchor and 0.60x the 4080's measured
  non-RT 4K throughput.
- **Verdict (RTX 4080, Quest 3-class resolution, RT off):** 72 Hz and 90 Hz are feasible at native
  resolution on average scenes; 90 Hz with margin for heavy fights needs DLSS Quality or fixed
  foveation; 120 Hz needs both, or a render scale near 0.8. **RT on:** 72 Hz marginal native, 90 Hz only
  with DLSS Quality, 120 Hz not realistic. PSVR2 and Pimax Crystal at their full recommended sizes
  (16-16.6 Mpix) are 90 Hz-with-DLSS-Q cases on the 4080.
- **Mid-range (RTX 4070, RX 7800 XT, RX 6800 XT):** 72 Hz with Quality upscaling, 90 Hz with upscaling
  plus foveation or a reduced render scale, RT off. **RTX 3070 / RX 6800:** 72 Hz with upscaling; the
  3070's 8 GB is the binding limit before its GPU time is.
- **VRAM on 16 GB is comfortable** (about 10.5-11.5 GB without RT, 12-13 GB with RT, estimated). 12 GB
  cards need one texture-pool step down; 8 GB cards need Medium pool and no RT. The game's own VRAM
  gate cannot see our stereo doubling, so we must do our own check.
- **The game simulates once per rendered frame** (`com_fixedTic` "run a single game frame per render
  frame", `com_adaptiveTick` with a 30-1000 Hz range) [C: cvar descriptions; U: behaviour]. There is no
  60 Hz tick to fight: at 90 Hz the game ticks at 90 Hz and the camera is rebuilt every frame. The
  flip side is that runtime reprojection at half rate also halves the game's tick rate.
- **No PC runtime accepts application motion vectors** (`XR_FB_space_warp` is standalone-Quest only)
  [C: OpenXR-Inventory]. Depth is the only useful input to PC reprojection; submit it.
- **The real risks are hitching, not averages:** pipeline creation for patched SPIR-V (a first-launch
  slowdown to 66 ms/frame has been seen in DOOM 2016 VR), per-command hook cost, and allocation-pool overflow from 2-layer
  images.

---

## 2. Flat performance data

### 2.1 Modern GPUs, Ultra Nightmare, RT off, average FPS [C]

Source: TechPowerUp RX 7900 XTX review (13900K) and RX 7800 XT review (13900K); full tables in
`reference/performance/techpowerup-gpu-reviews-doom-eternal.md`.

| GPU | 1080p | 1440p | 4K | 4K Mpix/s |
|---|---|---|---|---|
| RTX 4090 | 542.6 | 459.6 | 307.8 | 2,553 |
| RX 7900 XTX | 483.3 | 395.1 | 245.8 | 2,039 |
| RTX 4080 | 471.6 | 379.5 | 244.5 | 2,028 |
| RTX 4070 Ti | 414.9 | 324.8 | 201.8 | 1,674 |
| RX 6800 XT | 373.2 | 286.5 | 159.9 | 1,326 |
| RTX 4070 | 340.0 | 263.6 | 158.7 | 1,316 |
| RX 7800 XT | 366.0 | 281.3 | 158.3 | 1,313 |
| RTX 3080 | 303.2 | 248.3 | 151.9 | 1,260 |
| RX 6800 | 326.4 | 250.2 | 139.5 | 1,157 |
| RTX 3070 | 255.2 | 199.8 | 118.3 | 981 |

These are averages in a TPU custom scene, not combat worst cases. Launch-era data (2020, 2080 Ti) and
the 4080 FE review (5800X) are in the same reference folder; the 4080 FE result on the 5800X (243.6 fps
at 4K) matches the 13900K result within 1%, so 4K is GPU-bound on any modern CPU.

### 2.2 Ray tracing and DLSS [C]

| Source | GPU | Setting | Result |
|---|---|---|---|
| DSOGaming (2021) | RTX 3080, 9900K | 4K UN, RT on, native | "more than 90 fps" |
| DSOGaming | RTX 3080 | 4K UN, RT on, DLSS Quality | "more than 120 fps" |
| DSOGaming | RTX 3080 | RT cost | 50-90 fps depending on scene |
| NVIDIA (2021) | RTX range | DLSS at 4K | "up to 60%" faster |
| HotHardware | several | RT VRAM | about +1.5 GB |
| Owner rig | RTX 4080, 9950X3D | 4K, RT on | "hold 120" (DLSS mode unknown) |

RTX 3080 at 4K goes from about 150 fps (TPU, RT off) to about 90 fps (DSOGaming, RT on). Different
scenes, so treat the ratio as rough: RT roughly doubles the resolution-dependent part of the frame [U].
RT reflections default to half-resolution trace with temporal upscale
(`r_raytracedReflectionsTemporalUpscaleQuality 1`) [C: cvar], so the cost does scale with pixels. AMD RT
is relatively slower than NVIDIA in this title [C: HotHardware, qualitative]; the model below applies
the NVIDIA-derived factor to AMD and therefore flatters AMD with RT.

### 2.3 CPU ceiling [C]

- Tom's Hardware (2080 Ti): i5-9600K, i7-8700K and i9-9900K tied at all settings; everything above Core
  i3 cleared 240 fps at 1080p low; top results 329-365 fps; a Pentium G5400 averaged 180 fps.
- TechPowerUp: RTX 4090 reaches 542.6 fps at 1080p on a 13900K, so the CPU frame on a current high-end
  CPU is below 1.85 ms. On the 5800X it was 470.9 fps (partly CPU-bound).
- id's own design explains this: bindless resources and GPU-driven merged indirect draws, where "Opaque/
  PreZ draw calls almost completely disappear" [C: SIGGRAPH 2020 slides, `reference/idtech7/docs/`].

A 9950X3D will not be the limit at 72-144 Hz; our per-command overhead and one-off stalls will
(section 7).

### 2.4 VRAM [C]

| Source | Condition | VRAM |
|---|---|---|
| TechPowerUp (2080 Ti, launch) | UN, 1080p / 1440p / 4K | 7,196 / 7,517 / 8,370 MB |
| Tom's Hardware (menu estimate) | Ultra 4K / UN 1080p | 6,025 / 6,766 MiB |
| GameGPU (16 GB cards, RT on) | 1080p / 1440p / 4K | 8.4 / 8.6 / 9.6 GB |
| HotHardware | RT on vs off | about +1.5 GB |
| Cvar dump | `is_poolSize` / `is_poolCapacity` | 4096 / 8192 MiB |

The resolution-dependent share is small relative to the texture pool: +1.17 GB from 1080p to 4K on the
2080 Ti, about 140-190 bytes per added pixel once streaming residency growth is included [U: derived].

---

## 3. A per-GPU frame-time model

Least-squares fit of frame time against pixel count, `t = a + b * Mpix`, over the three TPU points per
card [U: model; C: inputs]. `a` is resolution-independent GPU and CPU-exposed time; `b` is the marginal
cost per megapixel.

| GPU | a (ms) | b (ms/Mpix) | worst residual |
|---|---|---|---|
| RTX 4090 | 1.36 | 0.228 | 0.02 ms |
| RTX 4080 | 1.47 | 0.316 | 0.00 ms |
| RX 7900 XTX | 1.37 | 0.324 | 0.03 ms |
| RTX 4070 Ti | 1.57 | 0.409 | 0.01 ms |
| RTX 4070 | 1.81 | 0.541 | 0.01 ms |
| RX 7800 XT | 1.48 | 0.581 | 0.07 ms |
| RX 6800 XT | 1.42 | 0.580 | 0.07 ms |
| RX 6800 | 1.62 | 0.666 | 0.08 ms |
| RTX 3070 | 2.36 | 0.733 | 0.06 ms |

The fit is almost exact, which says Eternal's cost is close to linear in pixels in this range.

**VR stereo frame (our multiview design)**, assumptions stated so they can be replaced by rig data:

1. Fixed cost grows by 35%: shadows, light binning, GPU culling, particle simulation and the light
   atlas stay mono; vertex work and per-view passes double [U]. Multiview runs geometry once per view
   in hardware, so vertex shading is about 2x while CPU submission is about 1x [U: doc 02 open question 9].
2. Pixel cost = `b` x stereo pixels x 1.07. The 7% covers patched shaders (per-eye clip transforms,
   cluster-coordinate remap, reconstruction fixes) and multiview inefficiencies [U].
3. Our own GPU work is 0.5 ms: two eye copies into OpenXR swapchain images (about 54 MB each way at
   Quest 3 size, roughly 0.1-0.2 ms on a 4080-class card), depth copy/convert, UI quad, timestamps and
   any cross-API copy [U].
4. RT on multiplies `b` by 2.03 (the 3080 ratio above) [U].
5. Upscaler "Q" renders 1/1.5 per axis (0.444 of the pixels) and adds two evaluations per frame: DLSS
   CNN preset (E/F) cost scaled from NVIDIA's 4K table by per-eye output size, +10% for Quality input;
   FSR2 Quality from AMD's table [C: tables; U: scaling]. Transformer presets (J/K) cost about twice the
   CNN figure: add about 1.2 ms on a 4080 at Quest 3 size.
6. Fixed foveation "FFR" removes 30% of pixel work [U: depends on doc 08's method].

Compositor time and streaming encode are not in these numbers; they come out of the budget in section 5.

---

## 4. How many pixels VR asks for

Runtime-recommended per-eye sizes come from `xrEnumerateViewConfigurationViews` and already include
lens-distortion oversampling. Two were captured in DOOM 2016 VR tester logs [C]; the rest are panel-based
assumptions [U]. Details in `reference/performance/headset-render-targets.md`.

| Headset, per-eye size | Stereo Mpix | 72 Hz | 90 Hz | 120 Hz | x owner anchor at 90 / 120 |
|---|---|---|---|---|---|
| Quest 3, 2496x2688 (VDXR log) | 13.42 | 966 | 1,207 | 1,610 | 1.21 / 1.62 |
| Quest 3, panel x1.3 (2683x2870) | 15.40 | 1,108 | 1,386 | 1,848 | 1.39 / 1.86 |
| Index, 2016x2240 | 9.03 | -- | 812 | 1,083 | 0.82 / 1.09 |
| Pico 4 / Steam Frame, 2592x2592 | 13.44 | 967 | 1,209 | 1,612 | 1.21 / 1.62 |
| Bigscreen Beyond 2, 2560x2560 | 13.11 | 983 (75 Hz) | 1,179 | -- | 1.19 / -- |
| Pimax Crystal, 2880x2880 | 16.59 | 1,194 | 1,492 | 1,990 | 1.50 / 2.00 |
| PSVR2, 2804x2860 (SteamVR log) | 16.04 | -- | 1,443 | 1,924 | 1.45 / 1.93 |

Rates in Mpix/s. The owner anchor is 4K x 120 = 995 Mpix/s with RT. Against the 4080's non-RT 4K
throughput (2,028 Mpix/s) the same headsets need 0.40x (Index 90 Hz) to 0.98x (Pimax 120 Hz).

Pixel rate alone understates VR cost: fixed per-frame cost still applies, and the budget must hold on
the worst frame, not the average.

---

## 5. VR frame budget

### 5.1 Budget split [U]

| | 72 Hz | 90 Hz | 120 Hz |
|---|---|---|---|
| Frame period | 13.9 ms | 11.1 ms | 8.3 ms |
| Compositor, distortion, streaming colour conversion (GPU) | 1.5 | 1.3 | 1.2 |
| Heavy-fight headroom (25% of period) | 3.5 | 2.8 | 2.1 |
| **Available for engine + our work (average frame)** | **8.9** | **7.0** | **5.0** |
| of which our overhead (section 3, item 3) | 0.5 | 0.5 | 0.5 |

The 25% headroom is a judgement (review averages come from non-combat scenes); replace it with the
rig's measured p99/average ratio.

### 5.2 RTX 4080, all target headsets (ms per stereo frame, engine + ours) [U]

| Scenario | Index | Quest 3 | PSVR2 | Pimax Crystal |
|---|---|---|---|---|
| Native | 5.5 | 7.0 | 7.9 | 8.1 |
| Native + FFR 30% | 4.6 | 5.7 | 6.3 | 6.4 |
| DLSS Quality (CNN) | 4.7 | 5.8 | 6.5 | 6.6 |
| DLSS Quality + FFR | 4.3 | 5.2 | 5.8 | 5.9 |
| Native, RT on | 8.7 | 11.7 | 13.5 | 13.9 |
| DLSS Quality, RT on | 6.1 | 7.9 | 9.0 | 9.2 |

Read against 5.1: at 90 Hz the 4080 needs at most 7.0 ms, so Quest 3 native is exactly at the line,
and anything with DLSS or FFR clears it. At 120 Hz (5.0 ms) only Index native-with-FFR and the
DLSS+FFR rows come close; Quest 3 at 120 Hz needs a render scale around 0.85-0.9 on top of DLSS+FFR.
RT on fits 72 Hz with DLSS Q on Quest 3 (7.9 vs 8.9) and nothing at 120 Hz.

### 5.3 Quest 3 at 2496x2688 across GPUs (ms per stereo frame) [U]

| Scenario | 4090 | 4080 | 7900 XTX | 4070 Ti | 4070 | 7800 XT | 6800 XT | 6800 | 3070 |
|---|---|---|---|---|---|---|---|---|---|
| Native | 5.6 | 7.0 | 7.0 | 8.5 | 10.7 | 10.8 | 10.8 | 12.3 | 14.2 |
| Native + FFR | 4.6 | 5.7 | 5.6 | 6.7 | 8.4 | 8.3 | 8.3 | 9.4 | 11.1 |
| Upscaler Q | 4.7 | 5.8 | 5.6 | 7.0 | 8.8 | 8.3 | 8.1 | 9.2 | 10.9 |
| Upscaler Q + FFR | 4.3 | 5.2 | 4.9 | 6.3 | 7.7 | 7.2 | 7.0 | 7.9 | 9.4 |
| Upscaler Q, RT on | 6.2 | 7.9 | 7.7* | 9.7 | 12.3 | 12.1* | 11.9* | 13.6* | 15.7 |

`*` AMD with RT is optimistic (NVIDIA RT factor applied). Upscaler is DLSS CNN on NVIDIA, FSR2 on AMD.

Against 5.1: the 4070 / 7800 XT / 6800 XT reach 72 Hz (8.9 ms) with Quality upscaling and 90 Hz
(7.0 ms) only with upscaling plus FFR, and then only just. The RX 6800 and RTX 3070 reach 72 Hz with
upscaling plus FFR; on Index-class resolution (9 Mpix) the 3070 upscaled is 8.5 ms, so 80 Hz is
realistic there.

### 5.4 Other resolution levers

- Dropping the runtime render scale from 100% to 80% per axis removes 36% of pixels at zero integration
  cost; starting FSR at 80% is common user guidance for VR mods [C].
- Size the eye render targets exactly to the runtime recommendation. A 16:9 carrier of 4800x2700 for
  2496x2688 eyes wastes pixels; per-eye sizing removes 48% of source pixels [C].
- Use `r_lodScale` (3.5 default; `r_lodModeWidth` 1920 "because assets are authored at this
  resolution") only if vertex cost shows up in profiles [C: cvars; U: effect].

---

## 6. VRAM budget

### 6.1 What doubles

Per-eye (becomes a 2-layer image) [U until the RE pass lists Eternal's actual targets]: scene depth,
HDR colour, the thin G-buffer / normal and specular targets, velocity, SSR and SSDO (half-res) targets,
TAA or DLSS history (per eye by requirement), bloom and post chain intermediates, and the
refraction/distortion target. DLSS or FSR contexts are per eye (two contexts).

Stays mono: the texture streaming pool (`is_poolSize`), the 8192x8192 shadow atlas
(`r_shadowAtlasWidth/Height`), light grid and cluster lists (with union-frustum binning), particle
light atlas (2048x2048), geometry and index pools, RT acceleration structures, and the UI target if the
HUD goes to a quad layer.

### 6.2 Estimate for Quest 3 size on an RTX 4080 [U]

| Item | No RT | RT on |
|---|---|---|
| Engine at 1080p UN (pools + fixed targets) | 7.2 GB | 7.2 GB |
| Resolution-dependent growth to 13.4 Mpix stereo (140-190 B/px over 1080p) | +1.6-2.1 GB | +1.6-2.1 GB |
| RT buffers and acceleration structures | -- | +1.5 GB |
| Two DLSS contexts at about 6.7 Mpix output each (CNN / transformer) | +0.3-0.5 GB | +0.3-0.5 GB |
| OpenXR swapchains, 3 images x 2 eyes, colour + depth (runtime-owned, same GPU) | +0.3 GB | +0.3 GB |
| Cross-API copy targets if the XR session runs on D3D12 | +0.1 GB | +0.1 GB |
| Runtime compositor, Link/VD encoder, desktop | +0.5-1.0 GB | +0.5-1.0 GB |
| **Total** | **10.0-11.2 GB** | **11.5-12.7 GB** |

A 16 GB card has 3-6 GB spare. A 12 GB card (RTX 4070, 4070 Ti) fits without RT and is marginal with
it. An 8 GB card (RTX 3070) does not fit at Ultra Nightmare: GameGPU saw 8 GB cards pinned at 8,000 MB
with system RAM rising to 10-11 GB, i.e. spilling [C].

### 6.3 The game's VRAM gate does not know about stereo

The settings menu refuses combinations above the detected VRAM (Tom's Hardware lists the per-preset
estimates; `menu_advanced_ignoreVRAMAmount 1` bypasses it) [C]. The game computes its estimate for the
single render size it believes it uses, so it will accept settings that overflow once we double the
targets. We need our own estimate (section 6.2 formula, fed with actual `vkAllocateMemory` totals) and a
launcher warning, not the game's gate.

### 6.4 Allocation-size lesson from DOOM 2016

A 154,009,616-byte 2-layer image overflowed DOOM 2016's 128 MiB block pool [C]. Eternal's Vulkan
allocator uses 16 MB device-local blocks (`vulkan_VRAMAllocatorBlockSizeDeviceLocalMB 16`) and a 4096 MB
small-allocation heap limit [C: cvar dump], so large render targets presumably take a dedicated path
[U]. What we must check is whether any doubled image crosses a size threshold that changes its
allocation path or exceeds a fixed pool. Sizes of a 2-layer RGBA16F target: Quest 3 at 2496x2688 is
107 MB; Pimax Crystal at 2880x2880 is 133 MB (126.6 MiB, just under 128 MiB); Pimax at a 1.2x render
scale is 191 MB. Log every image whose layer count we change, its size and the allocation it lands in.

### 6.5 Texture pool recommendation [U]

- 16 GB: Ultra Nightmare texture pool without RT; with RT, keep it unless measured total exceeds about
  14.5 GB, then drop one step.
- 12 GB: one step below Ultra Nightmare; RT off by default.
- 8 GB: Medium or High pool, RT off, and a render scale below 100%.

The mapping from menu step to `is_poolSize` MiB is not documented; read it on the rig.

---

## 7. CPU side and hitching

### 7.1 Command-recording hooks

In DOOM 2016 VR, aggregate hook cost measured about 3 ms before a shared-recording rewrite and about
0.7 ms after, with lookup/lock falling from about 3 us to 0.05 us (menu scene, summed across recording
threads) [C]. Eternal records far fewer draw commands than DOOM 2016 because of merged indirect draws [C:
SIGGRAPH], so the same design should cost less. Targets: under 0.5 ms aggregate recording overhead and
no global lock on any per-command path (per-thread dispatch caches, shared/exclusive metadata locks,
lock-free buffer-only barriers).

### 7.2 Pipeline creation for patched SPIR-V

Eternal is reported to use about 500 pipeline states and a dozen descriptor layouts [C: Coenen frame
study, "supposedly"]. With a mono and a multiview twin per graphics pipeline plus per-eye compute
variants, expect 1,000-1,500 pipelines. Each new SPIR-V module misses the driver's shader cache on first
run; typical cold compile times are tens of milliseconds per pipeline [U]. For comparison, a DOOM 2016
corpus was 647 original modules, 722 with profiles, and 1,395 variants in a later audit [C].

Rules:
1. Patch and create the multiview twin **inside the game's own `vkCreate*Pipelines` call** (or on a
   worker thread started from it and joined before the game uses the handle). Never create lazily at
   first draw; that is a mid-combat hitch.
2. Pass the game's `VkPipelineCache` through so the driver caches our variants alongside its own.
3. Keep our own disk cache of patched SPIR-V keyed by input hash, transform options and patcher
   version (doc 02, 7.3), so the transform itself is paid once per install.
4. Offer a "prepare shaders" step on first launch after install or driver update: start the game to
   the main menu, let it create pipelines, exit. Show progress.

### 7.3 First-launch slowness

In DOOM 2016 VR the first run sustained about 66 ms/frame versus 11-15 ms afterwards, "most extra time outside
Present", with similar sampled CPU work; the cause was never confirmed [C]. Focus throttling and a
second XR session were fixed as hypotheses. A third hypothesis worth testing on Eternal first: cold
driver compilation of newly patched modules running in the background, or a driver cache that was
invalidated by our new SPIR-V [U]. Measure pipeline-creation time per call and the driver cache size
before and after the first run.

### 7.4 Other CPU costs

Never call `xrWaitFrame` with our queue lock held, and never activate windows from the render thread
(both cause stalls or deadlocks). The other OpenXR frame calls are well under a millisecond together [U].

---

## 8. Tick rate, pacing, latency and reprojection

### 8.1 The game ticks per rendered frame

Retail cvars: `com_fixedTic 1` "run a single game frame per render frame"; `com_adaptiveTick 1` "adapt
the game hz dynamically" with `com_adaptiveTickMinHz 30`, `MaxHz 1000`, and
`com_adaptiveTickImmediateMode 1` "immediately adjust game hz based on last frame's duration" [C]. The
flat game runs uncapped by default (`r_swapInterval 0`) and the settings menu allows a dynamic-resolution
target of 60 to 1000 fps [C: TechPowerUp]. Speedrunning evidence agrees: meathook momentum, BFG tendril
damage and dash speed all depend on framerate, and runs are capped at 250 fps for that reason [U: search
summaries and a video title, not a primary rules page].

Consequences:
- At 72/90/120 Hz the game simulates at 72/90/120 Hz. There is no 60 Hz tick, so no interpolation layer
  and no pose update that "misses" ticks. The head pose is applied every frame.
- Framerate-dependent mechanics will vary a little between 72 and 120 Hz headsets; accept it.
- When the runtime drops to half rate (ASW, Motion Smoothing, SSW), the game drops to 45 Hz ticks too.
  That is above the 30 Hz floor, but input latency and meathook behaviour change. Prefer lower render
  scale over relying on reprojection.
- Because tick length follows the last frame's duration, irregular blocking in `xrWaitFrame` becomes
  irregular game time steps. Pacing must be even, not just fast.

The cvar list also contains `multiView_60Hz` ("0 = alternate frame rendering, 1 = render both each
frame") beside the dormant `vr_*` / `stereoRender_*` set (doc 03) [C: names]. It hints that id Tech 7's
old stereo path rendered both views per frame; whether any of it survives is doc 03's question.

### 8.2 Where the pose goes in

id Tech 7 simulates frame N+1 while the render thread builds frame N, and async compute for the next
frame overlaps the current one ("about 80 percent of the image", GPU utilisation near 97%) [C: Khan
interview notes]. The pose must therefore be predicted for the display time returned by `xrWaitFrame`
and latched at the point where the render view is built, not at game-logic time, and the view actually
used must be recorded per frame and submitted with that frame. Starting
point: `xrWaitFrame`/`xrBeginFrame`/`xrLocateViews` in the present or acquire hook right after
`xrEndFrame` (doc 02 shape 1), which also throttles the game to display rate.

### 8.3 Latency targets

Keep predicted-pose age at scan-out within two display periods (about 22 ms at 90 Hz, 17 ms at 120 Hz)
[U: common practice]. Rotational error is corrected by the runtime's timewarp regardless; positional
error and hand/weapon lag are not. DOOM 2016 VR measured 12-17 ms from controller publication to
rendered prop at about 120 fps [C]. Log the same span.

### 8.4 Reprojection

| Runtime | Half-rate mode | App inputs used | Tag |
|---|---|---|---|
| Meta PC (Link / Air Link) | ASW, 90 -> 45 | Depth via `XR_KHR_composition_layer_depth`; no app motion vectors | [C] 45 Hz halving (Meta docs), [C] no `XR_FB_space_warp` (inventory), [U] depth use |
| SteamVR (Index, Beyond, Pimax in SteamVR mode, PSVR2, Steam Frame) | Motion Smoothing | None; depth only forwarded to third-party drivers | [C] depth note (doc 01), [U] rest |
| VDXR (Virtual Desktop) | SSW | Depth consumed | [C] doc 01 |

Submit per-eye depth (reversed-Z, `nearZ > farZ`) by default on Meta and VDXR; make it a setting on
SteamVR. Motion vectors from the game's velocity buffer have no PC consumer today.

### 8.5 Dynamic resolution

The game's own dynamic resolution measures frame time against its target
(`profile_dropResolutionPercentage`, `profile_raiseResolutionPercentage`) [C: cvars]. With `xrWaitFrame`
in the loop, the measured time includes waiting, so its controller will misjudge load. DLSS disables it
anyway [C: user reports, `reference/upscaling/doom-eternal-dlss-integration-notes.md`]. Start with it off;
a later option is our own controller driving `rs_forceResolution` from GPU timestamps, if that cvar
takes effect per frame without reallocating targets [U].

---

## 9. Settings recommendations for VR

| Setting (cvar) | VR value | Why |
|---|---|---|
| Motion blur (`r_motionblur`) | 0 | Comfort |
| Depth of field (`r_dof`) | 0 | Eye focus is the user's; saves a half-res pass per eye |
| Chromatic aberration (`r_chromaticAberration`) | 0 | Comfort; lenses already add CA |
| Film grain (`r_filmGrainRatio`) | 0 | Per-eye noise differs, reads as shimmer |
| Lens flares (`r_lensFlaresRatio`) | 0 | Screen-space, breaks in stereo |
| Sharpening (`r_sharpening`) | lower than 4, tune | Over-sharpening aliases in the headset |
| Screen-space reflections (`r_SSR`) | 0 at first | Hard to make correct in stereo; re-enable after per-eye fixes |
| SSDO (`r_SSDO`) | on, temporal history per eye | Only its temporal history needs per-eye handling |
| Ray-traced reflections (`r_raytracedReflections`) | 0 initially | Doubles pixel cost; revisit after stereo RT is verified |
| Dynamic resolution (`r_enableResolutionScale` dynamic mode) | off | Section 8.5 |
| Async compute (`r_asyncPostProcess`) | leave on unless it breaks our frame ownership | Eternal leans on it for performance |
| View kicks, shakes, bob | off | Comfort (doc 06) |
| Shadow quality | High, not Ultra Nightmare | Mono atlas; small visual gain above High (Tom's: UN is 3-4% slower than Ultra) |
| Texture pool | per section 6.5 | VRAM |
| Anti-aliasing | TAA per eye, or DLSS/FSR per eye (doc 07) | History must be per eye |
| Photosensitivity (`r_fullscreenLumDeltaThreshold`) | keep default 0.4 | Limits luminance jumps; helps comfort |
| V-sync (`r_swapInterval`) | 0 | Pacing comes from `xrWaitFrame`, not the desktop |

---

## 10. Feasibility verdict

**RTX 4080 (owner's rig).**
- **72 Hz:** feasible at native resolution on every target headset with RT off; feasible with RT on at
  Quest 3 size with DLSS Quality.
- **90 Hz:** feasible at native resolution for Index and at the line for Quest 3; comfortable with DLSS
  Quality or FFR; PSVR2 and Pimax Crystal at full recommended size need DLSS Quality. RT on: Quest 3 with
  DLSS Q is at the line (7.9 vs 7.0 ms), so RT at 90 Hz is a stretch goal.
- **120 Hz:** Index with FFR or DLSS; Quest 3 / Pico / Steam Frame need DLSS Q + FFR and a render scale
  near 0.85-0.9; PSVR2 120 Hz needs a reduced render scale. No RT at 120 Hz.

**RTX 4090 / RX 7900 XTX:** one refresh step better than the 4080 without RT; the XTX with RT is likely
worse than the table shows.

**RTX 4070 / RX 7800 XT / RX 6800 XT:** 72 Hz with Quality upscaling; 90 Hz with upscaling plus FFR or a
render scale around 0.85; RT off. The 4070's 12 GB needs a texture-pool step down.

**RTX 3070 / RX 6800:** 72 Hz with upscaling plus FFR at Quest 3 size, 80-90 Hz at Index size. The
3070's 8 GB forces Medium/High pool and no RT; VRAM, not shader throughput, is its limit.

Overall: **a DOOM Eternal VR port is performance-feasible on the owner's hardware at 90 Hz**, which is
not true for most AAA titles, and reachable at 72 Hz on mid-range cards. It stays feasible only if our
own overhead stays under about 0.5 ms GPU and 0.5 ms CPU, and if pipeline creation never lands mid-frame.

---

## 11. Implications for our design

1. **Budget our own GPU work at 0.5 ms per stereo frame** and timestamp it from day one.
2. **Size eye targets from `xrEnumerateViewConfigurationViews` times our render scale**, per eye, never
   a carrier (the 48% lesson in section 5.4). Expose render scale; default 100% on 16 GB+ cards, 80-90% below.
3. **Make DLSS/FSR per eye (doc 07) and fixed foveation (doc 08) first-class**, not optional extras: the
   budget tables show 90 Hz on mid-range and 120 Hz on the 4080 depend on them. Prefer the CNN preset at
   120 Hz; the transformer preset costs about 1.2 ms more per stereo frame on a 4080.
4. **Keep shadows, light binning, culling and particle atlases mono.** The 35% fixed-cost growth
   assumption only holds if they do.
5. **Own the VRAM check.** Estimate from real allocation totals plus our doubling and the XR swapchains;
   warn before the game's gate is fooled.
6. **Log image size and allocation path for every image we promote to 2 layers**, and stop cleanly (a
   reserved exit code) if one would land in a fixed pool it overflows.
7. **Create pipeline twins inside the game's creation call**, pass through `VkPipelineCache`, keep a
   disk cache of patched SPIR-V, and offer a first-launch shader preparation step.
8. **Pace from `xrWaitFrame` and keep it even.** The game's tick follows frame duration, so jittery
   waits become jittery game time. Measure the distribution of game-frame deltas, not just averages.
9. **Latch the predicted pose where the render view is built** and record which pose each frame used.
10. **Submit depth by default** on Meta PC and VDXR; do not build a motion-vector export for PC
    runtimes.
11. **Force the section 9 settings from the launcher**, including dynamic resolution off.

---

## 12. Measurements to take on the rig first

### 12.1 Re-measure the owner's anchor properly

Record for every run: game build and executable; preset and every changed setting (texture pool step,
shadow quality, RT reflections on/off, `r_antialiasing` value and `r_dlssQuality` mode, sharpening,
motion blur, DoF); resolution and resolution-scale mode; `r_swapInterval` and any frame cap (in-game,
driver, RTSS); display refresh rate; driver version; DLSS DLL version in the game folder; scene
(mission, checkpoint, combat or walking) and duration; average, 1% low and p99 frame time; GPU busy time
(PresentMon `GPUBusy` or NVIDIA FrameView); CPU frame time; VRAM in use (`com_showFPS 3` or
`menu_advanced_DebugVRAMUsage 1` plus PresentMon/GPU-Z dedicated memory); GPU clock and power, to catch
power limits.

Runs to do, same scene each time: 4K native RT off; 4K native RT on; 4K DLSS Quality RT on; 1440p and
1080p native RT off (to fit `a` and `b` for this rig); 1080p low (CPU ceiling).

### 12.2 Before any stereo code

1. **Pixel-scaling test:** set a custom window of 2496x5376 or render-scale runs to mimic 13.4 Mpix, to
   check the linear model beyond 4K.
2. **Heavy-fight ratio:** p99 / average frame time in two combat arenas; replaces the 25% headroom.
3. **VRAM by texture-pool step:** read `is_poolSize` for each menu step and total VRAM at 4K.
4. **Tick behaviour:** with a frame cap of 72, 90 and 120 fps, confirm game time per frame follows the
   frame (log from `com_adaptiveTick*` or a hook on the game-frame entry) and check the 30 Hz floor.
5. **Pipeline count and creation time:** a pass-through Vulkan layer logging every
   `vkCreateGraphicsPipelines`/`vkCreateComputePipelines` call, its duration, and when in the session it
   happens (startup, level load, mid-play).
6. **Allocation map:** log `vkAllocateMemory` sizes and the image-to-allocation binding for all render
   targets at 4K, to predict which targets change path when doubled.

### 12.3 With the first stereo build

GPU timestamps (engine frame, our passes, upscaler per eye), `xrWaitFrame` blocked time, pose age at
submit, game-frame delta distribution, first- versus second-launch frame time and pipeline-creation
totals, steady-state VRAM per headset profile.

---

## Sources

Benchmarks and articles (local notes in `reference/performance/`):
- TechPowerUp, DOOM Eternal Benchmark Test & Performance Analysis (2020-03-20):
  https://www.techpowerup.com/review/doom-eternal-benchmark-test-performance-analysis/
- TechPowerUp, RTX 4080 Founders Edition review, DOOM Eternal page and test setup:
  https://www.techpowerup.com/review/nvidia-geforce-rtx-4080-founders-edition/15.html
- TechPowerUp, RX 7900 XTX review: https://www.techpowerup.com/review/amd-radeon-rx-7900-xtx/15.html
- TechPowerUp, RX 7800 XT review: https://www.techpowerup.com/review/amd-radeon-rx-7800-xt/13.html
- DSOGaming, Doom Eternal Ray Tracing & DLSS Benchmarks:
  https://www.dsogaming.com/pc-performance-analyses/doom-eternal-ray-tracing-dlss-benchmarks/
- HotHardware, Doom Eternal Ray Tracing Tested: https://hothardware.com/reviews/doom-eternal-ray-tracing-tested
- GameGPU, DOOM Eternal RTX benchmarks: https://en.gamegpu.com/test-gpu/action-fps-tps/doom-eternal-test-rtx
- Tom's Hardware, Doom Eternal Graphics, CPU Testing:
  https://www.tomshardware.com/features/doom_eternal-graphics_cpu-performance-comparison
- NVIDIA, DOOM Eternal ray tracing and DLSS update:
  https://www.nvidia.com/en-us/geforce/news/doom-eternal-ray-tracing-nvidia-dlss-upgrade-available-now
- Meta, Asynchronous SpaceWarp (PC):
  https://developers.meta.com/horizon/documentation/native/pc/asynchronous-spacewarp/
- Wikipedia: Comparison of virtual reality headsets; Steam Frame; Pimax; Bigscreen Beyond.

Vendor documents and data (local):
- NVIDIA DLSS Super Resolution Programming Guide 310.6.0, execution-time and memory tables:
  `reference/_cache/upscaling/DLSS_Programming_Guide_Release.pdf`
- AMD FidelityFX FSR2 README performance and memory tables: `reference/_cache/FidelityFX-FSR2/README.md`
- OpenXR-Inventory runtime extension lists: `reference/_cache/OpenXR-Inventory/runtimes/`
- DOOM Eternal complete CVAR list: `reference/_cache/cvarlist/`, extracted to
  `reference/performance/eternal-perf-cvars.md`
- SIGGRAPH 2020 "Rendering the Hellscape of DOOM Eternal", Coenen frame study, Billy Khan PCGH interview:
  `reference/idtech7/docs/`

Related notes in this repo: 01 (runtimes, depth submission), 02 (multiview, pipeline caches, pacing
shapes), 03 (dormant VR cvars), 06 (comfort), 07 (upscaling), 08 (foveation).
