# Per-eye temporal history for Route S (TAA, DLSS, exposure)

Static analysis of the retail `DOOMEternalx64vk.exe`, Steam build 25216728 (SHA-256 `69dc13e8...6a1c11`,
the file of `stereo-reentry.md` and `stereo-routes.md`), and the implementation behind
`ETERNALVR_STEREO_TAA=1`. The game was not run for this document; the live experiments are in section 7.
Tools: Ghidra 12.1.4 headless on a copy of the stereo-recon project (`analysis/stereo-taa`), the Python
helpers in `analysis/scripts`, capstone. All addresses are RVAs. Tags as in `stereo-reentry.md`:
**[static-verified]** (read in this build's code or data) and **[inferred]** (what the verified code implies;
the rig has to confirm it).

## 1. Summary

- **Every temporal image the TAA path uses is picked by the parity of the backend frame counter**
  (device context + 0xB0, one step per backend frame, read through render system vtable slot 0x70,
  0x1CBB2D0). Route S renders two backend frames per game tick, so the parity is constant per eye: each
  eye always writes one image of a pair and reads the other, which the other eye wrote. That is the
  cross-feed, for the TAA accumulation pair, the anti-ghosting mask pair and the auto-exposure pair alike.
  [static-verified]
- The per-view block at 0x66EF4F0 and its ping-pong 0x1CFC050, named in `stereo-reentry.md` as the TAA
  history, is the **light-scattering / light-grid** state (`lightScatteringVolumesUBO`, bin tiles), not TAA.
  [static-verified]
- **TAA and DLSS need no engine change to become per eye, only different images at four read points.**
  The TAA output and history come from two leaf functions (0x1CBB5A0, 0x1CBB6C0) that nothing else
  bypasses; eye R gets its own pair, built by the engine's own slot builder when the renderer starts, and
  each eye alternates within its pair by its own frame count. DLSS keeps its history inside the NGX
  feature, so eye R gets a twin feature through the game's own exported NGX entry points. [static-verified
  read points; inferred end-to-end behaviour]
- The **jitter phase** comes from the render frame counter (two steps per tick): each eye would see every
  other Halton phase only, and with base 2 that puts all of an eye's x offsets in one half of the pixel.
  Both eyes of a tick now get the tick's phase. The jitter is applied in shaders through
  idRenderView + 0x298B8 (the latch builds the projection without it), so it works with our explicit
  per-eye projections. [static-verified]
- **`r_jitter` is not a setting**: every backend frame the engine sets it to 1 exactly when `r_TAASafeMode`
  is 0 and the effective AA mode is above 0 (0x1C5EA85). Reading it back is the proof that TAA or DLSS
  runs; `+r_jitter 0` in the v1 set does nothing. [static-verified]
- **`r_TAASafeMode 1` removes the TAA/DLSS pass entirely** (the post-process job 0x1C927D0 calls the AA pass
  0x1C9B560 only when it is 0). S3a (`+r_TAASafeMode 0` with v1's `r_antialiasing 0`) ran the TAA pass in
  its "disabled transfers" mode: no temporal AA. S3c (the game's own settings) most likely had TAA on; the
  cross-fed history is offset by the full stereo disparity (500-620 px at infinity), which TAA's variance
  clamp rejects, so it degrades to little accumulation rather than visible double images. That explains
  "no ghosting seen". [static-verified for the gate; inferred for S3c]
- **v1 bug found on the way: auto exposure stops adapting in steady stereo.** With eye R skipping its
  exposure update (`ETERNALVR_STEREO_EXPOSURE_ONCE=1`, the default), eye L's parity is constant, so it always
  writes one exposure image and reads the other as "previous", which nobody writes any more. [static-verified
  selection; inferred effect, live test T6]
- **Implemented** behind `ETERNALVR_STEREO_TAA=1` (section 6): second accumulation pair, selector detours,
  per-eye jitter phase, per-eye resets, per-eye exposure index, DLSS twin, the still-shared temporal effects
  switched off (on the command line, checked and if needed written through the engine's cvar setter), fail
  closed to the v1 set. Unit tests for the pure logic;
  not yet run in the game.

## 2. The TAA path

### 2.1 Where it runs

| # | Function | RVA | Role | Tag |
|---|---|---|---|---|
| 1 | Backend frame | 0x1CDAFE0 | from the frame-end job (SMP hand-over) and the render thread loop 0x1CDE500; job setup 0x1CDCD90 -> 0x1C5CD00 -> 0x1C5C2F0 queues the render-view job; then the render thread frame 0x1CD8380 (swap, backend counter + 1) | [static-verified calls] |
| 2 | Per-view render settings snapshot | 0x1C5DFD0 | per backend frame (from 0x1CD8380 and 0x1C5CD00): effective AA mode = clamp(`r_antialiasing`, 0, DLSS usable ? 2 : 1) at render context + 0x4D9D08; sets `r_jitter` (below) | [static-verified] |
| 3 | Render-view job | 0x1C54650 (job pointer 0x39A1DC8) | fills the post-process context (render context + 0x71A770): output (+0xE0) and history (+0xE8) accumulation targets from the selectors, velocity (+0x60), exposure index (+0x140), backend frame (+0x148), reset flags (+0x258 = `cameraCut`, +0x37F = `disableTssaaNextFewFrames`) | [static-verified] |
| 4 | Exposure and TAA parameters | 0x1C988E0 | exposure pair index (+0x140, section 3.3); TAA filter weights 0x1C99980 | [static-verified] |
| 5 | TAA setup | 0x1C99980 | Halton(2, 3) weights from `renderView_t.subSampleIndex` (+0x652) of the idRenderView (not the latched copy); the TAA reset state (below); clears `r_TAAClearAccumulationBuffer` | [static-verified] |
| 6 | Post-process job | 0x1C927D0 (job pointer 0x39A64E0; 0x1C939A0 via 0x39A64E8 is the second variant) | if `r_TAASafeMode` == 0: the AA pass; auto-exposure update | [static-verified] |
| 7 | AA pass | 0x1C9B560 | DLSS branch (NGX feature usable, 0x667EC53) or the compute TAA: program `computeTAA` when the mode is 1, `computeTAAGhostingFixup` with the anti-ghosting mask, else `computeTAADisabledTransfers` | [static-verified] |

Shader parameter slots are 0x20-byte entries whose name pointer is at slot - 0x10 (for example 0x39A7830
`taaViewColorLastFrameMap`). [static-verified]

### 2.2 Temporal resources

| Resource | Storage | Selection | Readers / writers | Per eye in Route S (before this work) | Tag |
|---|---|---|---|---|---|
| **TAA accumulation (history colour)** | device context slot 0 (context + 0x8, stride 0xA8 per view): images `_accumulationBuffer00/01` at slot + 0x30/+0x38, render targets at slot + 0x58/+0x60; built by the slot builder 0x1C20150 from the context constructor 0x1C19C10 only | output = RT[(b + 1) & 1] (0x1CBB5A0), history = RT[b & 1] (0x1CBB6C0), b = backend counter | TAA reads history (`taaViewColorLastFrameMap`), writes output (`computePostProcessTarget`); DLSS writes output; the history is also bound as `viewColorLastFrameMap` for the next frame's scene passes (0x1C56614) and to a second pass (0x1C56A96) | **cross-fed** | [static-verified] |
| Opaque accumulation | slot + 0x40 image, + 0x68 RT (`accumulationBufferOpaque`), selector 0x1CBB580 | single image, current frame (`viewColorOpaqueMap`) | TAA setup | no history | [static-verified] |
| TAA reset state | slot + 0x8 -> 0x40-byte block {reset counter, width, height, mode} | per view | 0x1C99980: counter - 1; set to 3 on `cameraCut` or `disableTssaaNextFewFrames`; history ignored (+0x2E3) while the counter is above 0, the size or mode changed, or `r_TAAClearAccumulationBuffer` | shared by the two renders of a tick; harmless (same size and mode) | [static-verified] |
| Anti-ghosting mask | `_taaGhostingMask0/1`, render system + 0x560/+0x568 (0x66E3190/98) | read [(b - 1) & 1] (`prevFrameGhostingMask`), write [b & 1] | TAA with `r_TAAAntiGhosting` (default 1) | **cross-fed** | [static-verified] |
| Motion vectors | `_motionVector0/1`, render system + 0x360/+0x368; RTs in the context at + 0x528/+0x530 | current = [b & 1] (post-process + 0x60, written and read in the same frame); + 0x68 = [(b + 1) & 1], the previous frame's | TAA and DLSS read the current one; the previous one's reader was not found (motion blur, off) | current: fine; previous: cross-fed | [static-verified selection; inferred reader] |
| Auto exposure | context + 0x5D0/+0x5D8 (`autoExposure0/1`), luminance chain + 0x598..+0x5C8 | index at post-process + 0x140 (section 3.3) | exposure update writes `autoExposureTarget` [i], reads `autoExposurePrevExposureMap` [i ^ 1]; TAA, tone mapping and DLSS (`ExposureTexture`) read [i] | see 3.3 | [static-verified] |
| Jitter | `renderView_t.subSampleIndex` (+0x652), `upsamplerSubSampleIndex` (+0x654) of screen view 0, from the render frame counter (render system + 0x10) in the render-frame job 0x1CB9EE0 | index % `r_TAANumSubSamples` (default 32) | 0x1CE1E40 turns it into a pixel offset (Halton(2, 3) of index + 1; a grid of sub-buffers instead when `numSubBuffersRow` (+0x651) is 2 or more, for screenshot up-resolution), stored by the latch at idRenderView + 0x298B8 (previous at + 0x298C0, part of the per-eye previous matrices); DLSS `Jitter.Offset`; TAA weights | two phases per tick, each eye every other phase | [static-verified] |
| Previous matrices | idRenderView + 0x29480..+0x298E8 | per render | velocity, TAA | per eye since v1 (`prev_matrices.*`) | [static-verified] |
| DLSS history | inside the NGX feature | one feature, handle in the command context + 0x190 (0x1CC5760) | evaluate per render | **shared** | [static-verified] |
| Light-scattering volumes | per-view block 0x66EF4F0 (stride 0xAF8), ping-pong + 0x0 toggled per view render (0x1CFC050, gated by 0x66ECF24) | per render | light grid and scattering passes | cross-fed; `r_lightScatteringTAA 0` | [static-verified] |
| Other per-context histories | `ambientOcclusionAcc0/1`, `lightScatteringPacked*Acc0/1`, `dofAccBuffer*`, `waterSSRAccumulationBuffer0/1`, `_refractionAccumulationBuffer%d%d` (0x1C1CD80) | not traced | SSDO, scattering, DOF, water, refraction | assumed cross-fed; their TAA cvars 0 | [static-verified allocation; inferred selection] |

`disableTssaaNextFewFrames` (`renderView_t` + 0x750) is read at 0x1C57081 from the latched view into
post-process + 0x37F. It does not disable TAA: it resets the history for three renders. [static-verified]

### 2.3 Why the latch leaves explicit projections unjittered

The latch 0x1CE1400 calls 0x1CE1E40 and stores the jitter at idRenderView + 0x298B8; its projection builder
0x39A310 is called with zero offsets, and the explicit-projection branch copies
`explicitProjectionMatrix` unchanged. So the jitter reaches the GPU as a separate constant, the post-latch
check (`latched view ... equals the matrix written`) still holds with TAA on, and each eye's jitter is
applied to its own asymmetric frustum. [static-verified]

## 3. Findings per item

### 3.1 The ping-pong and "previous vs current"

The selectors (bytes identical except `inc eax` in the output one):

```
0x1CBB5A0  output:  slot = *(renderSystem + 0xF58) + viewIndex * 0xA8; return slot[0x60 + ((vtbl70() + 1) & 1) * 8]
0x1CBB6C0  history: ...                                             return slot[0x60 + (vtbl70() & 1) * 8]
```

Both are called only from the render-view job (0x1C56614, 0x1C56A96, 0x1C56B9A, 0x1C56BAC); a scan for
other `imul 0xA8` readers of the slot found none that read the accumulation images directly.
[static-verified]

### 3.2 Jitter

The render-frame job (0x1CB9EE0): `subSampleIndex = counter % r_TAANumSubSamples`,
`upsamplerSubSampleIndex = (counter / k^2) % r_TAANumSubSamples` with k from
`r_raytracedReflectionsTemporalUpscaleQuality` (table 0x2EADE90), written into screen view 0's
`renderView_t`, copied into the idRenderView by the screen-views pass before the per-eye hook
(0x1C754BC). With two renders per tick one eye gets the even counters and the other the odd ones; the
base-2 Halton coordinate of every other index lies in one half of [0, 1), so each eye's horizontal jitter
covers half a pixel only. [static-verified]

### 3.3 Auto exposure

0x1C98D0E..0x1C98D40:

```
lastUpdated = slot + 0x28 (int, written by the exposure update in 0x1C927D0)
index = (b != lastUpdated && !view.skipAutoExposureUpdate) ? b & 1 : lastUpdated & 1
```

The update (0x1C927D0) runs under the same condition and writes `autoExposure[index]` from
`autoExposure[index ^ 1]` and this frame's luminance. Mono: b alternates, each frame reads the one before.
Route S with eye R skipping: eye R reads `autoExposure[b_L & 1]`, eye L's current exposure (as intended), but
eye L's b is always even or always odd, so it writes one image and reads the other, which no frame writes
any more: the "previous exposure" freezes at its value from the last mono frame of that parity. Without
the skip (`ETERNALVR_STEREO_EXPOSURE_ONCE=0`) the two eyes form one chain (L reads R's, R reads L's), which
adapts, twice per tick. [static-verified selection; inferred effect]

### 3.4 DLSS (NGX)

- The game links NGX 1.x statically (`C:/dvs/p4/build/sw/devrel/libdev/NGX/core/rel_1_6`) and **exports**
  the NGX API: `NVSDK_NGX_VULKAN_CreateFeature` 0x2268B30, `..._EvaluateFeature_C` 0x2268CF0,
  `..._ReleaseFeature` 0x2268F40, `NVSDK_NGX_Parameter_SetI` 0x2268080, `..._GetI` 0x2267DA0 and the
  rest. Our layer finds them with `GetProcAddress` on the exe; no signatures. [static-verified]
- Create: 0x1CC5760, called by the AA pass every frame with (render size, output size); it recreates the
  feature (release, then `CreateFeature(cmd, 1 = super sampling, params, &handle)` with `Width`, `Height`,
  `OutWidth`, `OutHeight`, `PerfQualityValue` from `r_dlssQuality`, `DLSS.Feature.Create.Flags` 0x4B or
  0x6B with sharpening, `CreationNodeMask`/`VisibilityNodeMask` 1) when the mode, the sizes or the
  sharpening flag change. The handle is cached at the command context + 0x190 with the settings at
  + 0x198..+0x1AC. Log line `Nvidia NGX: Created DLSS feature (%i). (%ix%i)->(%ix%i)`. [static-verified]
- Evaluate: 0x1CC5B30 -> 0x1CC7AA0 -> `EvaluateFeature_C(cmd, handle, params, nullptr)` with `Color`
  (scene colour), `Output` (the accumulation output target, section 2.2), `Depth`, `MotionVectors`
  (`_motionVector[b & 1]`), `ExposureTexture` (auto exposure [index]), `Jitter.Offset.X/Y` (0x1CE1E40, in
  render pixels), `MV.Scale` (-width, -height), `Sharpness`, `Reset` = `r_dlssForceReset != 0` (the cvar
  counts itself down per evaluate). The same parameter block (0x66E8B28) is used for create and evaluate.
  No camera-cut reset is passed. Three failed evaluations in a row reset `r_antialiasing` (DLSS branch
0x1C9B5D0, `Nvidia NGX: DLSS failed to evaluate. Resetting AA mode.`).
  [static-verified]
- Release: 0x1CC5F00 (shutdown) and inside 0x1CC5760. [static-verified]
- Availability: `r_enableDLSS` (INIT, default 1), NGX init in 0x1CC80E0 (renderer start), the flags
  0x667EC52 (DLSS supported) and 0x667EC53 (feature usable); the AA mode is clamped to 1 without them.
  [static-verified]

### 3.5 What S3 measured

S3a: `r_TAASafeMode 0`, `r_antialiasing 0`: the AA pass ran `computeTAADisabledTransfers`, `r_jitter` 0.
S3b/c: the game's own settings (AA from the player's config, default 1): TAA on, history from the other eye.
[static-verified for the program choice; inferred for S3c's actual cvar values, which were not read back]

## 4. Options considered

| Option | What | Cost | Risk | Verdict |
|---|---|---|---|---|
| A. Engine-image swap (chosen) | a second accumulation pair built by the engine's slot builder at renderer start; the two selectors return the frame's eye's pair, alternating by that eye's frame count | about 5 extra render-size images (the builder also makes `viewColor1`, `distortion1`, `accumulationBufferOpaque1`: roughly 100-180 MB at 2064x2208 [inferred]); no extra GPU work | the images are the engine's own objects (layout tracking, bindless registration, resize handled like slot 0); needs a hook installed before the renderer starts | best |
| B. Pointer swap in the slot around eye R's chain | swap slot + 0x30..+0x60 before eye R | same memory | the slot is read on the render thread's jobs, not in eye R's frontend chain: needs the backend eye anyway | no gain over A |
| C. Layer-level copies | save/restore history images with `vkCmdCopyImage` around each eye's TAA dispatch | 4 full-size copies per tick (about 0.4 ms at 4.6 MP [inferred]) | needs the TAA dispatch and the history VkImages identified in the command stream (shader hashes, descriptor tracking); fragile | fallback only |
| D. Descriptor swap | rewrite the bindless descriptors of the history image per eye | no memory | DOOM Eternal binds by bindless index through its own parameter blocks; rewriting them per frame is invasive | no |
| E. Keep TAA off (v1) | | none | aliasing | the fail-closed state |

For DLSS: two NGX features (NVIDIA's own guidance for multiple views) versus one feature with `Reset` every
frame (no temporal accumulation at all): two features.

## 5. The scheme

Per backend frame the render jobs need to know which eye they render. The eye tags already give every
frame handed to the render thread the backend counter value its present will carry; the render jobs of
that frame run before its present, while the counter is one below. So the jobs look up the tag for
`counter + 1` (`EyeTagQueue::peek`, `seqTagInFlight`). Each tag also carries its eye's own frame count
(`eyeSeq`, eye L counting mono frames too).

| Piece | Where | What |
|---|---|---|
| Eye R's images | hook after the slot builder call in the device context constructor (0x1C1A1B9, installed from `vkCreateInstance`) | for slot 0, call the builder (0x1C20150) once more with a slot of our own whose index is 1: `_accumulationBuffer10/11`, `_viewColor1`, `_distortion1`, `_accumulationBufferOpaque1`, their render targets and two small buffers, made on the renderer's start-up thread exactly like slot 0 |
| Accumulation | inline hooks on 0x1CBB5A0 and 0x1CBB6C0 | tagged frame of view 0: pair = eye (L and mono: the engine's slot, R: ours), image = eye frame count parity (output the other one); untagged frames, other views, per-eye TAA off: the engine's own choice |
| Jitter | per-eye hook (0x1C754BC) | both eyes: `subSampleIndex = gameFrame % r_TAANumSubSamples`; eye R copies eye L's `upsamplerSubSampleIndex` |
| History reset | per-eye hook | `disableTssaaNextFewFrames` on both eyes of a tick when eye R did not render the previous game frame (first stereo tick, after mono ticks, a skipped eye R) |
| Exposure | hook after the index store (0x1C98D46) | eye L and mono: eye frame count parity; eye R: eye L's of the tick |
| DLSS | inline hooks on the exported `EvaluateFeature_C` and `ReleaseFeature` | eye R's first evaluation of a game feature creates a twin from the same parameter block (the game's create keys are still in it) into the same command buffer; eye R evaluates the twin, `Reset` raised on its first use and after eye R missed a tick; releasing the game's feature releases the twin |
| Still-shared effects | the launch helper's command line (`-StereoTaa`); at the first stereo tick the layer writes any that differ through the engine's cvar setter (idCVar::SetString, 0x376020, which the engine itself calls from its render threads) | `r_TAAAntiGhosting`, `r_SSDOTemporalAA`, `r_lightScatteringTAA`, `r_dofTAA`, `r_waterReflectionsTAA`, `r_waterGridTAA`, `r_refractionTAA`, `r_raytracedReflectionsTemporalUpscaleQuality`, `rs_enable` 0 |

**Fail closed.** The first stereo tick checks that every piece is in place (selectors, eye R's images,
exposure hook, `r_TAANumSubSamples`, the setter and every cvar object). If one is missing it writes
`r_antialiasing 0` and `r_TAASafeMode 1` (the v1 state: no TAA pass at all) and, if even that does not read
back, turns stereo off (mono). DLSS asked for without a working twin (no NGX hook, a twin that failed to
create) switches to `r_antialiasing 1` (per-eye TAA). Every write, redirect and creation asks
`mp_guard::allowsGameTouch()`; after a trip every hook forwards to the engine (the DLSS twin is still
released with its game feature: it is the layer's own object).

**Cost.** GPU: none beyond TAA or DLSS itself per render. Memory: eye R's slot images (above) and a second
DLSS feature. CPU: a tag lookup under the tag mutex per selector call (four per backend frame) and per DLSS
evaluation.

**Risks.** [inferred]
1. The engine could resize or recreate slot images through a path not found here. Only the slot builder
   references the `accumulationBuffer%d%d` name and it is called only from the constructor; the context is
   built once, from the renderer start 0x1CC80E0 -> 0x1CC34D0 -> 0x1CC5F70. A second construction gets a
   fresh slot (the old one is leaked, not freed on a possibly different device).
2. A backend frame's jobs reading the counter after the swap of the frame before and before its own: the
   engine's own parity choice depends on exactly this, so it holds as long as mono TAA works.
3. Creating an NGX feature inside the evaluate hook: the game itself creates right before evaluating in
   the same pass.
4. The shared TAA reset state (slot + 0x8): a reset asked for one tick resets both eyes for up to three
   renders; wanted.

### 5.1 What the live runs added

- **Eye R's targets must follow the engine's resizes.** The engine resizes slot 0's render targets in place
  when the render size changes (0x1C21600 -> 0x1C743C0 per target: the accumulation pair, the opaque
  accumulation, the view colour, the distortion target), which happens right after start-up when the window
  is placed. Eye R's slot kept the size it was built with (1264x713 against 1280x740 on the rig), and the
  rows it did not cover showed the same image in both eyes: that band alone made up most of a 0.12 ghost
  coefficient. The resize is now detoured and eye R's targets are resized to the engine's sizes, target by
  target. [live-verified: sizes logged every 10 s]
- **Resizing eye R's targets one by one fails on a 12 GB card** (a second test PC, RTX 3080 Ti, i7-9700K, 2026-09-27): each
  per-target resize (0x1C743C0) made the engine ask for a new block of about 1 GB whatever the size
  (931495936 B for 3808x1952 -> 1415x1415, 1282277376 B for 1280x720 -> 1280x1400), the game's budget refused
  it, and the renderer stopped at a modal "Failed to allocate video memory" box on the primary display, so
  the game looked hung after its first present (the launcher's default render size grows the context once at
  start-up). The detour now re-runs the slot builder on eye R's slot after the engine's own resize (the
  builder frees what the slot held and allocates the set as at start-up); if a target is then null or of
  another size than slot 0's, per-eye TAA fails closed to the v1 set. Live on the second test PC: no dialog, per-eye
  on, balanced picks, ghost coefficient 0.017 / 0.010 (control 0.004). [live-verified]
- **A DLSS resize changes only the view colour** (second test PC, 2026-09-27; the five target sizes of both
  slots are logged on every resize that leaves them unequal). With DLSS the context is resized to the render
  size (742x812 for a 1280x1400 output): in place, the engine only shrinks the view colour (+0x70, 320x350
  -> 185x203) and keeps the pair, the opaque accumulation and the distortion target at 1280x1400, while the
  slot builder makes the opaque and distortion targets at the render size. Rebuilding eye R's slot therefore
  left its opaque accumulation smaller than the engine's, and per-eye TAA failed closed (no AA at all). Eye
  R only uses its pair and its opaque accumulation (the view colour and distortion targets of slot 0 serve
  both eyes), so the slot is rebuilt only when one of those three differs, and only those three must match
  afterwards. Live: per-eye DLSS for the whole 120 s run (about 1400 evaluations per eye per 10 s); ghost
  coefficient 0.009 / 0.005 with DLSS against 0.016 / 0.009 for TAA in the same build (control 0.005).
  [live-verified]
- **The opaque accumulation** (slot + 0x68, one image, read and written by the TAA pass) is taken from eye
  R's slot for eye R (third selector, 0x1CBB580), and `distortionLastFrameMap` (slot + 0x48, the previous
  frame's scene colour, bound at 0x1C5664B) is bound to each eye's own history.
- **No render view overlap.** A wait for the previous frame's render-view job before the next chain
  rewrites the view never waited in any run, and each frame's eye tag always matched the side of its
  latched projection (`tag eye vs latched projection side`: 0 mismatches), so the wait was removed.
- **The player's profile overrides the command line at run time** (the owner's `r_antialiasing 1`, `r_dof`,
  `r_SSR`). Per-eye TAA checks its cvars on every stereo tick and writes only what differs; it reports itself
  to `runtime_cvars::setStereoTemporal(PerEye)`, so the layer's run-time v1 hold stands down. TAA safe mode
  also forces `r_SSDO 0` and `r_SSR 0` in the game's config (0x1C6FCC0), which the game then saves.

## 6. Implementation (this branch)

| File | Content |
|---|---|
| `src/stereo_seq/stereo_taa.*` | pure logic: `pickAccumulation`, `taaSubSample`, `TaaResetPlanner`, `ExposurePlanner`, `NgxTwins`, the cvar sets, `taaMissingPiece`; tests in `tests/stereo_seq/stereo_taa_tests.cpp` |
| `src/stereo_seq/eye_tags.*` | `RenderTag::eyeSeq`, `EyeTagQueue::peek` (tests in `eye_tags_tests.cpp`) |
| `src/vkcore/taa_locate.*` | signatures (below), cvar objects by their registration |
| `src/vkcore/taa_hooks.*` | early slot hook, selectors, exposure hook, cvar enforcement and fail-closed, counters |
| `src/vkcore/taa_ngx.*` | DLSS twins |
| `src/vkcore/mid_hook.*` | `installInlineHook` (safetyhook inline hook, created disabled, then enabled) |
| `src/vkcore/presenter_seq.cpp` | per-eye hook: jitter phase and resets; the 10 s `seq-taa:` line |
| `tools/rig/launch-ht.ps1` | `-StereoTaa`: `ETERNALVR_STEREO_TAA=1`, `+r_TAASafeMode 0 +r_antialiasing 1 +rs_enable 0 +r_swapInterval 0` and the still-shared effects at 0 |

Log lines start with `seq-taa:`. At start-up: the slot loop hook, eye R's render targets, the selectors, the
NGX hooks; at the first stereo tick `per-eye TAA on` with the cvar writes, or `writing the v1 set`. Every
10 s: `per-eye on/off; accumulation picks A eye L / B eye R / C engine; resets; DLSS evaluations ... eye L /
... eye R (... without a twin), twins made / failed, twin resets; r_antialiasing r_TAASafeMode r_jitter
r_TAAAntiGhosting`. In steady stereo A and B are four per backend frame of their eye (one output, three
history reads), C counts menus and mono frames, and `r_jitter 1` confirms the TAA pass.

### Signatures (each unique in `.text` of build 25216728)

| Name | RVA | Signature |
|---|---|---|
| Accumulation output selector | 0x1CBB5A0 | `40 53 48 83 EC 20 48 63 82 90 89 02 00 48 8B 11 48 69 D8 A8 00 00 00 48 03 99 58 0F 00 00 FF 52 70 FF C0 25 01 00 00 80 7D 07 FF C8 83 C8 FE FF C0 48 98 48 8B 44 C3 60` |
| Accumulation history selector | 0x1CBB6C0 | `40 53 48 83 EC 20 48 63 82 90 89 02 00 48 8B 11 48 69 D8 A8 00 00 00 48 03 99 58 0F 00 00 FF 52 70 25 01 00 00 80 7D 07 FF C8 83 C8 FE FF C0 48 98 48 8B 44 C3 60` |
| Device context slot loop (hook + 0x29, builder call + 0x24) | 0x1C1A190 | `8B C3 49 8D 4E 08 48 69 F8 A8 00 00 00 8B D3 48 03 CF E8 ?? ?? ?? ?? 45 8B 06 49 8D 56 08 48 03 D7 49 8B CE E8 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? FF C3 3B 58 08 7C C9` |
| Auto-exposure index (hook + 0x38) | 0x1C98D0E | `8B 8E 48 01 00 00 8B 54 1F 30 3B CA 74 0E 48 8B 46 38 80 78 21 01 74 04 B0 01 EB 02 32 C0 3C 01 0F 45 CA 81 E1 01 00 00 80 7D 07 FF C9 83 C9 FE FF C1 89 8E 40 01 00 00` |
| idCVar::SetString | 0x376020 | `48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B D9 48 8B 09 41 0F B6 F0 48 8B FA 48 85 D2 75 04 48 8B 79 30 48 8B 09 48 8B D7 E8` |
| Cvar registration (object by name) | many | `4C 8D 05 ?? ?? ?? ?? 48 8D 15 <name> 48 8D 0D <object> E8`: one match each for the 16 cvars used (for example `r_antialiasing` 0x6685CE0, `r_TAASafeMode` 0x66DE720, `r_jitter` 0x66E47A0, `r_TAANumSubSamples` 0x66DE880, `r_TAAAntiGhosting` 0x66DFA40, `r_dlssForceReset` 0x66E8E30) |
| NGX entry points | exports | by name from the exe's export table |

## 7. Live results (2026-09-26, rig, OpenXR-Simulator, build 25216728)

e1m2 spawn view, head still (`ETERNALVR_TEST_HEAD_SWAY=0,0,1`), `ETERNALVR_HEAD_POSITION=0`, eye captures
every 45 pairs, `tools/stereo/ghost_coeff.py` over the newest 12 pairs (one launch per PowerShell call;
the virtual display ran at 1280x800, so each eye rendered at 1280x740). Runs `runs60926-10*` and
`-11*`, logs in `tmp-vr	aa*-logs`, captures in `tmp-vr\cap-*` and `tmp-vr	3-*`.

| # | Run | Ghost eye L <- R / eye R <- L (control) | Notes |
|---|---|---|---|
| T3 | v1 (no temporal accumulation) | 0.032 / 0.034 (0.021) | the reference for no cross-feed |
| T3 | the game's TAA, history shared | 0.206 / 0.216 (0.013) | double images visible in the crops |
| T3 | per-eye, before the resize fix | 0.121 / 0.097 (0.021) | a 19-row band at the bottom identical in both eyes; 0.049 without it |
| T3 | **per-eye (final)** | **0.029 / 0.029 (0.014)**; default build 0.028 / 0.029 (0.015) | within 0.015 of the control, as v1; `r_jitter 1`, `r_antialiasing 1`, `r_TAASafeMode 0` read back every 10 s; accumulation picks 7772 eye L / 7768 eye R per 10 s (4 per frame each), 0 engine |
| T4 | **DLSS per eye** (`r_antialiasing 2`) | 0.030 / 0.030 (0.016) | eye R's feature created at the first eye R evaluation (`created (2, result 0x1)`), 1901 / 1902 evaluations per 10 s, one twin reset at start |
| T5 | guard trip at 30 s | | tags drop to mono, no cvar writes or picks afterwards, no crash |
| T6 | exposure | | not measured against mono; the per-eye runs are 60% brighter than v1 at the same view (mean luma 40 against 25), consistent with v1's frozen exposure (3.3) but not proof |

Sharpness: the rock silhouettes that stair-step in v1 are smooth with per-eye TAA
(`tmp-vr\cmp-v1-vs-taa.png`); the band-passed high-frequency energy is equal in both eyes (12.8 / 12.8).

Cost (the `rates:` line, 10 s windows, 1280x740 per eye, RTX 4080, no frame cap): v1 201.2 ticks/s
(402.5 presents/s), per-eye TAA 194.4 ticks/s (388.7), DLSS per eye 190.2 ticks/s: TAA costs 0.17 ms per tick
(about 0.09 ms per eye) at this size.

## 8. Open questions

1. Who reads the previous frame's velocity (post-process + 0x68)? Cross-fed; harmless while motion blur is
   off. [inferred]
2. The formats and sizes of the slot images (0x127/0x137 accumulation, 0x107 opaque and view colour, 0x147
   distortion) for the exact memory figure; the rig can read them from `vkCreateImage`.
3. Whether the histories of SSDO, light scattering, DOF, water and refraction can be made per eye the same
   way (their selectors were not traced); until then their TAA stays off.
4. Whether a second NGX feature raises NGX's own memory beyond the estimate at Quest 3 size.
5. The v1 exposure freeze (section 3.3) should be confirmed against mono at the same view.
6. Cost at headset size (2064x2208 per eye) and the headset check itself.
