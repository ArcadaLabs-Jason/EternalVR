# Presenter record

What the presenter (docs/ARCHITECTURE.md section 6) was measured to do on the test PCs, part by part. Labels:
[log] the layer's log, [rig] measured from a run's files, [code] read in the code, [inferred] reasoning not checked.

## Depth

The depth layer (`ETERNALVR_DEPTH_LAYER=1`, the launcher's "Depth for headset smoothing (experimental)", off by
default; docs/ARCHITECTURE.md section 6 "Depth", T-231). Runs on the second test PC (RTX 3080 Ti, OpenXR-Simulator,
Steam build 25216728, Route S stereo, e1m2's start, the QA suite's set-up: per-eye TAA, 1280 x 720 per eye unless
noted), 2026-11-11, QA runs `r1` to `r6`; after the review fixes (planes per tick, the queue hold, the read-back of
the depth swapchain image, the refusal and timeout fall-backs) runs `f1` to `f7` the same evening.

### What the runtime offers and takes

- OpenXR-Simulator lists `XR_KHR_composition_layer_depth` (version 1) and offers D3D12 swapchain formats 29 28 91 87
  10 2 24 20 40 45 55: `D32_FLOAT` (40), `D32_FLOAT_S8X24_UINT` (20), `D24_UNORM_S8_UINT` (45) and `D16_UNORM` (55)
  among them [log]. T-110's note that the simulator has no depth layers is out of date for the build on this PC.
- The layer takes `D32_FLOAT`; the simulator makes the depth swapchain's images `R32_TYPELESS` (39) with
  `ALLOW_DEPTH_STENCIL`, 3 images, and their copyable footprint has the shared buffers' row pitch [log].
- Every headset frame of a head-tracked image carries both views' depth after the first image (899 to 901 of 899 to
  901 projection frames a 10 s line, repeats included) and `xrEndFrame` never failed [log]. The simulator composes
  colour only: it takes the depth but does not reproject with it, so how a runtime uses it is not seen here
  [inferred from its documentation and output].
- The VDXR runtime on this PC also lists the extension (no headset connected) [rig, a loader query]; Meta's and
  SteamVR's runtimes are not installed here.

### The game's depth

- `_upscaledOpaqueDepth` is `D32_SFLOAT_S8_UINT` at the output size (1280 x 720 in the suite, 2048 x 2208 with the
  render size set), made with `SAMPLED | DEPTH_STENCIL_ATTACHMENT` (so nothing is added at its creation) and
  concurrent over the graphics and async compute families; at each present it is in `SHADER_READ_ONLY_OPTIMAL`, last
  moved by a submit on queue family 0, so the shader reads it in place with no layout change [log].
- With DLSS Quality per eye the eyes render at 853 x 480 and `_upscaledOpaqueDepth` stays 1280 x 720, with the same
  scene in it (`depth-layer-dlss`) [log, rig].
- The eyes' depth row is the engine's (m10 -1.0000074, m11 -0.06): near plane 0.06 units, far plane 8118 units at
  the map's start and 31457 later (the far plane follows the map; the float row holds it only to a few per cent)
  [log]. The stored value is reversed: 0 for the sky, near 1 for the first-person models.
- Read back (`ETERNALVR_TEST_DEPTH_READBACK`, every 8th texel, world size 1) in the hall at e1m2's start, both eyes
  [log]: 7.0% at depth 0 (the sky and the skybox's distant city through the arch), 19% (eye L) and 26% (eye R) at
  the nearest value (the arms and the gun, written at 0.25 m), the rest from 0.25 m to about 4000 m, median 3.6 m,
  the image centre 12.8 m in eye L (the far wall through the arch) and 0.25 m in eye R (its gun). The eyes hold the
  same value in 8% of the texels outside the sky. As images the depth matches the eye images of the same run: the
  arch, the floor's falling distance, the gun and the arm in each eye's own place [rig: `r4-pgm`, the PGM read-backs
  beside the XR shot]. Unit scale 1 holds: a hall of a few metres reads a few metres at world size 1.
- The planted fault (`ETERNALVR_TEST_DEPTH_FAULT=1`, eye R's copy also into eye L's half) reads 100.0% equal in
  every read-back and fails the QA check; without it 7.9% to 8.5% [log: `r6-fault`, `r1` to `r5`].

### What it costs

The Vulkan copy's GPU time is timestamped in its own command buffer (from the end of the eye's colour copy to the end
of the depth shader); the rest from A/B runs of the same map, 2048 x 2208 per eye, `ETERNALVR_GPU_TIMING=1`, 60 s
each (`r5-cost`: `baseline` off, `depth-layer` on with the 10 s read-back) [log]:

| | Off | On |
|---|---|---|
| The depth shader, per eye | none | 0.059 to 0.064 ms average (1.3 to 1.7 ms longest, rare) |
| The game's stereo tick, GPU busy (the game's own submits) | 12.24 to 12.39 ms | 12.44 to 12.57 ms |
| Game ticks a second (the game held to the headset, CPU-bound at about 16 ms) | 62.1 to 62.8 | 61.9 to 62.4 |
| The XR worker's wait for its D3D12 copy | 0.86 to 0.90 ms | 1.02 to 1.06 ms (12 ms longest: the 10 s read-back's 34.5 MiB copy) |
| The process's local video memory | 9077 MB | 9447 MB |

So about 0.12 ms of GPU time a stereo tick for the two shaders, about 0.15 ms more on the XR worker's D3D12 copy
(both eyes' depth, 34.5 MiB a slot), and 4 shared buffers of 34.5 MiB (138 MiB) plus the runtime's 3 depth images
(about 104 MiB, in the simulator's case inside the game's process). The rest of the 370 MB measured, and the 0.2 ms
of the game's own GPU time, are within what two runs differ by or not explained by these runs [inferred]. At
1280 x 720 per eye the shader takes 0.017 to 0.023 ms. Off, the layer makes nothing and records nothing: one start-up
line (`depth layer off (ETERNALVR_DEPTH_LAYER is not on); the runtime lists ...`) in each off scenario of `r2` [log].

### After the review fixes (`f1` to `f7`)

- The read-back now copies the depth swapchain's image itself (after the layer's copy into it, as the runtime gets
  it): the same numbers as the shared buffer's (7.0% sky, 19% and 26% at the clamp, median 3.6 m, 7.9% equal),
  in TAA, DLSS Quality and with per-eye TAA off (the game's own `r_antialiasing 1`) [log: `f1`, `f5`].
- The source's layout at every copy: shader read-only in all 1262 to 1272 copies a 10 s line, in all three modes;
  never depth read-only or the attachment layout, so the layout change is never used on this PC [log: `f1`, `f5`].
- The queue hold: the game's next submit on its other queues waits for each depth copy, about 3 waits per copy
  (3700 a 10 s line). At 2048 x 2208 per eye, A/B against `baseline` (`f6-cost`, 60 s each) [log]:

  | | Off | On (with the hold) |
  |---|---|---|
  | The depth shader, per eye | none | 0.055 to 0.059 ms |
  | The game's stereo tick, GPU busy | 12.17 to 12.34 ms | 12.35 to 12.53 ms |
  | Async compute (family 2) GPU busy a frame | 1.25 to 1.30 ms | 1.45 to 1.59 ms |
  | Game ticks a second | 62.1 to 62.5 | 61.4 to 62.0 |
  | The XR worker's wait for its D3D12 copy | 0.85 to 0.91 ms | 1.00 to 1.02 ms |
  | Local video memory | 9061 MB | 9445 MB |

  The same as without the hold (`r5-cost`) within what runs differ by; the async compute queue's busy time takes the
  waits (about 0.2 ms a frame). On this PC the game presents from queue family 0 (graphics), so the waits fall on
  its other queues; where it presents from its compute queue (a level on the first test PC) they fall on the
  graphics queue, which is not measured here.
- A refusal: `ETERNALVR_TEST_DEPTH_REFUSE=1` (minDepth 1 above maxDepth 0, which the extension forbids) is taken by
  the simulator without an error, so it does not validate depth infos [log: `f2-refuse1`]. With `=fake` the first
  frame with depth is taken as refused: `xrEndFrame failed (-1) on a frame with depth`, `the runtime refused the
  depth layer; the depth layer is off for this game`, and the headset's frames go on head-tracked without depth (900
  frames a 10 s line, 6750 of 7201 head-tracked by the end) [log: `f3-refusefake`].
- Alternate eyes on Auto with the layer on: `depth layer off (not with alternate eyes, on or auto)`; the game
  switched to alternating 7 s in (one eye per tick, 84 to 89 ticks a second) and no depth was copied or submitted
  [log: `f4-altauto`].
- Off: baseline, foveation-eyes, hud-stall and pickup-camera pass with one start-up line of the layer [log: `f7-off`].

- The game asks the layer for `vkQueueSubmit2` 0.9 s after start (the device's set-up), which the layer does not
  hook (logged once since the third review round). Its frames' submits reach the `vkQueueSubmit` hook: the layout
  tracker follows `_upscaledOpaqueDepth` through them and the queue hold adds about 3700 waits a 10 s line there.
  Whether some submit goes through `vkQueueSubmit2` is not checked [log: `g1-final`; the rest inferred].

### Not checked here

A headset; VDXR, Meta Link and SteamVR taking and using the depth (whether their reprojection looks better or warps
at edges); the D3D12 debug layer over the copy (with `ETERNALVR_D3D12_DEBUG=1` the presenter's device could not be
made on this PC, 0x887A0007, after the Graphics Tools were installed, so VR did not start in that run); the Vulkan
validation layer over the copy; a runtime that really refuses depth; the `D32_FLOAT_S8X24_UINT` fallback; the queue
hold where the game presents from its compute queue; whether `_upscaledOpaqueDepth` is written in every frame of
every mode (it is the output size in all three modes tried, and its contents match the eye images in TAA and DLSS);
a ring rebuild (a window size change) with the layer on; AMD and Intel GPUs.
