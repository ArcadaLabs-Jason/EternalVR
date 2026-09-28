# Single-frame stereo routes after EngineNativeStereo (T-004, T-066, T-069, T-070)

Static analysis of the retail `DOOMEternalx64vk.exe`, Steam build 25216728 (the file of
`engine-facts.md` and `stereo-reentry.md`). The game was not run for this document. Tools: the Python
helpers in `analysis/scripts` (PE loader, xrefs, cvar resolver, RTTI, signature builder), capstone, the
type-info dump (`analysis/typeinfo_classes.json`) and Ghidra 12.1.4 headless on the stereo-recon project.
All addresses are RVAs in this build. Tags as in `stereo-reentry.md`: **[static-verified]** (read in this
build's code or data) and **[inferred]** (what the verified code implies; the rig has to confirm it).

Starting point: the engine's own two-view path is a proven no-go on this build (`stereo-reentry.md`
section 12 on the `native-stereo` branch: `r_maxRenderViews` is INIT and stays 1, the per-view block at
0x66EF4F0 holds one 0xAF8 entry, the device context keeps one inline view slot, and the render thread's
job setup 0x1CDCD90 copies render-list entries into a one-element local array). What that work proved
and this document reuses: the renderer honours `renderView_t.explicitProjectionMatrix` (+0x50) when
`useExplicitProjectionMatrix` (+0x90) is set (E2: 0 of 2870 latches differed), and the per-eye pose and
projection math is unit-tested (`src/xr_math/stereo_view.*` on `native-stereo`). AER is not a product
option (D-004).

## 1. Verdicts and recommended order

| Route | Verdict | One-line reason |
|---|---|---|
| **S** synchronized sequential | **Needs a live test; the static case is strong** | The engine already renders frames synchronously without a game tick (loading screens, 0x455C10 -> 0x17E86A0 -> 0x1CBEA30). A second call of that same entry per tick, with the proven per-eye view hook, renders eye R through code paths that behave exactly like "the next render frame". The re-run needs no code patch (one `.data` job-pointer swap). Nothing in the render frame has to be saved and restored except the previous-frame matrices; the rest either must advance per render (rings) or is turned off in v1 (temporal effects) |
| **M** multiview / shader-side transform | **Not ready to decide; census tooling prepared** | Needs a SPIR-V patcher, image promotion of a bindless renderer, per-eye compute passes and semantic patches whose share is unknown. The live census (this branch) measures that share in a few seconds of e1m1 |

**Recommended order:**

(Route S v1 is built on the `stereo-seq` branch; its implementation, settings and the S1 to S5 run plan
are in `docs/VR_STEREO.md`.)

1. **Build Route S v1** now, behind `ETERNALVR_MODE=stereo`: the eye-R re-run (section 2.3), the per-eye
   view hook from `native-stereo`, per-eye previous-frame matrices (section 2.4), the v1 temporal cvar set
   (section 2.6), and presenter pairing (section 2.7). It is gated by live experiments S1 to S3 (section
   2.10), which a first build can run in one session.
2. **Run the census in the same session** (observe-only, `ETERNALVR_DUMP_SHADERS`, section 4): it costs
   one extra launch and answers T-070's question for Route M with real data.
3. Decide Route M after the census: if both T-105 shares are at or below 15.0%, Route M is the
   performance follow-up to S (one scene pass for both eyes, temporal AA and DLSS kept); if either exceeds
   15.0%, Route S stays the product route and M is dropped (T-049's criterion, reweighed as T-066 says).
4. Route S v2 (after v1 works): per-eye temporal history (section 2.8), which brings back TAA and DLSS.

## 2. Route S: synchronized sequential

### 2.1 Why the re-run unit is the whole render frame

The per-view parts of the frame cannot be re-run on their own:

- The **screen-views pass** 0x1C75290 produces one render-list entry per screen view; two entries in one
  render frame is exactly what crashed E4 (the render thread's job setup 0x1CDCD90 has room for one entry,
  crash at 0x1C5CD4B). [static-verified in `stereo-reentry.md`, live-verified in its section 12]
- The **backend** (render thread body 0x1CD8380) consumes what the frontend built for one view: the
  latched matrices, the Umbra result and the gathered draw surfaces. A second backend pass would need a
  second frontend pass anyway, because culling, LOD and draw gathering depend on the eye. [static-verified
  calls, inferred dependence]

So the smallest unit that renders a different view is one full render frame: frontend job chain plus one
backend frame plus one present. The engine has an entry that does exactly that.

### 2.2 The render frame chain

| # | Function | RVA | What it does | Tag |
|---|---|---|---|---|
| 1 | Render-kick stage (frame job table 0x388EDB0, entry 17) | 0x43A8C0 | begins a job list, calls 0x17E8560, then **submits and waits** (0x17E5C60): the whole render frame chain below finishes inside this stage | [static-verified] |
| 2 | Queue render frame | 0x17E8560 -> 0x1CBE9B0 | allocates the 0x20-byte packet `{renderSystem, frameInfo, *(renderSystem+0xF60), byte +0x18, byte +0x19 = frameInfo.screenshotMode != 0}` and queues the job in the `.data` slot 0x39A95F0 (= 0x1CB9EE0) | [static-verified] |
| 2' | **Render one frame synchronously** (renderSystem vtable slot 0x50) | **0x1CBEA30** | `(renderSystem, *(renderSystem+0xF60), frameInfo, byte)`: its own job list, the same packet, the same job 0x1CB9EE0, submit and wait. Called by 0x17E86A0 from the loading-screen renderer 0x455C10, i.e. the engine renders frames through it **with no game tick** | [static-verified] |
| 3 | Render-frame job | 0x1CB9EE0 | guard: `if (renderSystem+8 == 0) { renderSystem+8 = 1; ... }`; `renderSystem vtbl[0x78](1)` (0x1CD6DB0, frame timing); counter `renderSystem+0x10`++; TAA jitter bytes of screen view 0 (`g.subSampleIndex` +0x652, `upsamplerSubSampleIndex` +0x654, i.e. screen view +0x682/+0x684); world-views pass 0x1C75CF0; queues the `.data` slot 0x39A95F8 (= 0x1CBA0A0) | [static-verified] |
| 4 | World-views pass | 0x1C75CF0 | per world view: `rv->g = view.g`, **0x1CE2340** (previous-frame matrices, its only call site 0x1C75D7C), latch 0x1CE1400 (0x1C75D8B), per-world jobs `world vtbl[0x120]` (0x18E5070) for view 0 | [static-verified] |
| 5 | Frame middle job | 0x1CBA0A0 | `vtbl[0x78](0)`; stats 0x1CBBC50; clears the per-frame counters renderSystem+0x138..+0x198; screen-views kick 0x1C75E60 (-> 0x1C75290, the per-eye hook at 0x1C754BC and the latch at 0x1C7576D); render-thread preparation 0x1CDF3B0; queues the `.data` slot **0x39A9600** (= 0x1CBA1C0; only reader 0x1CBA17E, only pointer to 0x1CBA1C0 in the image) | [static-verified] |
| 6 | Frame end job | 0x1CBA1C0 | per-world end-of-frame work 0x1CDA860 (for each world in the render list); backend kick: synchronous 0x1CD6AD0 when `r_useSMP` is 0, else `vtbl[0x1D8]` + 0x1CDAFE0 (hands the frame to the render thread); `vtbl[0x78](0 / 2)`; **clears the guard** at 0x1CBA3AA (`mov byte [rbx+8], 0`, rbx = renderSystem, rsi = packet) and returns after its job list | [static-verified] |
| 7 | Render thread frame | 0x1CD8380 | per render-list entry (stride 0x118) 0x1C5DED0 / 0x1C5EF80; then `renderBackend(0x66E3B88)+0xB0`++ and the swap 0x1C35410(1, `r_swapInterval`); the dormant HMD submit | [static-verified] |

Static objects: renderSystem 0x66E2C30 (RTTI `idRenderSystemLocal`, vtable 0x2EAD3F8); render thread
`*(renderSystem+0xF38)` (0x66E3B68, RTTI `idRenderThread`); frameInfo = frameBuilder = `*(0x47DDAF8)`
(`idRenderFrameInfo`, 0x2A50 bytes, `screenViews` at +0). [static-verified]

### 2.3 The re-run point

**Mechanism** (no code patch):

1. Swap the job pointer in `.data` 0x39A9600 (0x1CBA1C0) for a wrapper `void eyeRightAfter(packet*)`.
   The wrapper calls the original. When it returns, eye L's frame has been handed to the render thread and
   the guard at renderSystem+8 is clear (0x1CBA3AA ran). [static-verified that the slot is the only way
   to 0x1CBA1C0 and that the guard is cleared before it returns]
2. If this tick is a stereo tick (world view present, gameplay or camera takeover, guard armed), the
   wrapper sets the current eye to R, calls **0x1CBEA30** `(packet->renderSystem, packet[+0x10],
   packet->frameInfo, packet[+0x18])` with `frameInfo.screenshotMode` (+0x2A44) cleared for the call (so a
   screenshot is taken once), then sets the eye back to L. Eye R's own chain reaches 0x39A9600 again; a
   re-entry flag makes the wrapper call only the original there. [inferred design]
3. The per-eye hook at 0x1C754BC (from `native-stereo`) reads the current eye and writes that eye's
   `vieworg`, `viewaxis`, explicit projection and `inhibitModelFovScale` into the view before the latch and
   the Umbra request. Eye L runs in the engine's own chain, eye R in ours; the chains never overlap (the
   guard and the synchronous call make them strictly sequential), so one global is enough.

**Why this is the right level.** Eye R's chain is, for the renderer, the next render frame: the same job
functions with the same packet, started after eye L's chain finished, exactly as a fast next frame would
start after the previous one. Everything the renderer does to keep frame N+1's frontend apart from frame
N's backend (render lists, visibility results, per-frame rings) therefore also keeps eye R apart from eye
L. The one thing that differs from a normal next frame is that no game tick ran in between, which is also
the case for every loading-screen frame. [inferred from the two static facts above]

**Where it runs.** Inside the render-kick stage's job list (row 1), which waits for it, so the frameInfo
that eye R reads cannot be rebuilt by the next frame-info stage (0x43A120) before eye R finishes.
Nested job lists inside jobs are normal in this engine (0x1CB9EE0, 0x1CBA0A0, 0x1CBA1C0 each begin and
wait on their own). [static-verified pattern, inferred safety]

Fallback hook for the same point if the pointer swap is ever unsuitable: a mid-function hook at
0x1CBA3A5 (`lea rcx,[rsp+0x70]; mov byte [rbx+8],0`), whose callback must clear renderSystem+8 itself
before calling 0x1CBEA30 (it runs before the relocated clear). [static-verified bytes]

### 2.4 Per-frame state: what advances, and what to do

Rule: a render-frame resource that *must be distinct* for two frames in flight (rings, pools, fences,
counters that index them) is left to advance twice per tick; restoring it would make eye R reuse eye L's
in-flight slot. State that carries *history between frames of one view* is kept per eye or switched off.
State that must advance *once per tick* is pinned for eye R.

| State | Where | Twice per tick with Route S | Handling | Tag |
|---|---|---|---|---|
| Re-entry guard | renderSystem+8 (0x66E2C38): set in 0x1CB9EE0, cleared at 0x1CBA3AA | eye R starts after the clear | none (ordering) | [static-verified] |
| Render frame counter | renderSystem+0x10 (0x66E2C40)++ in 0x1CB9EE0; vtbl[0x70] 0x1CBB2D0 returns it (or backend+0xB0); world jobs use it +3 as a delay (0x18E5070) | +2 per tick | **leave** (indexes per-frame state; counts frames the GPU really renders) | [static-verified writes, inferred use] |
| Backend frame counter, swap | renderBackend 0x66E3B88 +0xB0++ then 0x1C35410 in 0x1CD8380 | +2, two presents | **leave**; presenter pairs them (2.7); E7 showed both counters move in lockstep | [static-verified] |
| Command pools, rings, fences | render thread, per backend frame | one set per eye render | **leave** (they are what makes eye R safe while eye L's GPU work is in flight) | [inferred] |
| TAA jitter | screen view 0 `g.subSampleIndex` / `upsamplerSubSampleIndex` from the counter (0x1CB9EE0) | eyes get different phases | v1: TAA off (`r_TAASafeMode 1`, `r_antialiasing 0`, `r_jitter 0`); v2: eye R copies eye L's bytes in the 0x1C754BC hook | [static-verified] |
| Previous-frame matrices | 0x1CE2340 (only call 0x1C75D7C), per idRenderView: prev VP +0x29480 <- +0x29440, prev custom VP +0x295F0 <- +0x295B0, prev custom VP2 +0x29670 <- +0x29630, prev centred VP +0x296F0 <- +0x296B0, +0x297B0 <- +0x29770, +0x29830..+0x29858 <- +0x297F0..+0x29818, +0x29860 <- +0x29820, prev origin/offsets +0x29870..+0x29890 <- r+0x94/+0xC8/+0xD4, +0x298C0 <- +0x298B8, prev render size +0x298E0 <- +0x298D8; when `r_lockView` is 0 also +0x29500..+0x295A8 (r.vieworg, viewaxis, view matrix +0x293C0, VP); camera-cut counter +0x29944-- | eye L's "previous" would be eye R of the last tick, eye R's would be eye L: wrong motion vectors | **per eye**: snapshot the source fields after each eye's latch (post-latch hook 0x1C75772), and after 0x1CE2340 (hook at 0x1C75D81, rdi = idRenderView) rewrite the destinations from that eye's snapshot. About 0x500 bytes per eye | [static-verified fields] |
| Per-view history ping-pong | 0x1CFC050: block 0x66EF4F0 + viewIndex*0xAF8, +0x0 = index, `(x+1)%2`; image pairs at +0x28, +0x38, +0x48, +0x58, +0x68 selected by it | toggles twice: each eye reads the other eye's output as history | v1: off with the temporal set (2.6). Per-eye history needs a second set of images (2.8) | [static-verified] |
| Per-device-context accumulation | 0x1C1CD80: `autoExposure`, `ambientOcclusionAcc0/1`, `lightScatteringPacked*Acc0/1`, `dofAccBuffer*`, `waterSSRAccumulationBuffer0/1`, water simulation | shared between eyes | v1: temporal parts off (2.6); exposure stays shared on purpose (eye R skips its update, 2.5) | [static-verified allocation] |
| Umbra query | per screen view, `r_umbraJobKickoff 2` from 0x1C75290; one visibility context | one query per eye, sequential (like consecutive frames) | none; per-eye culling for free. Temporal reuse (previous-frame occlusion) is live test S5 | [static-verified kick, inferred reuse] |
| World per-frame jobs | 0x18E5070 (per-world counter +0xBFD8++, entity and model update jobs) and 0x1CDA860 (per-world end of frame) | run twice | CPU cost only if idempotent without a tick; S1 checks. Later optimisation: skip them for eye R | [static-verified calls, inferred idempotence] |
| GPU particles, water, shader time | driven by the view's `renderTimeUs` (renderView_t +0x0), copied from the game's view, identical for both eyes | if stepped by render-time deltas: eye R steps 0; if by frame count or wall time: stepped twice | live test S1 (identical-view diff) settles it | [inferred] |
| Dynamic resolution | `rs_enable` (0x66EC860, archive, readers 0x1422AC0, 0x1CEE390, 0x1CEE520); scale compare renderSystem+0xD30/+0xD34 -> 0x1C53420 in 0x1CB9EE0 | eyes could get different scales | v1: `rs_enable 0`, and `forceFullResolution` (+0x11) on both eye views | [static-verified] |
| Frame timing | `vtbl[0x78]` 0x1CD6DB0 timestamps renderSystem+0xF48/+0xF50 | two samples per tick | none (feeds stats and `com_adaptiveTick`, which adapts the game rate to the longer frame) | [static-verified] |
| Screenshot request | frameInfo +0x2A44 (packet +0x19) | taken twice | cleared for eye R's call | [static-verified] |
| GUIs (HUD) | 0x1C75290 re-emits each `viewGuis` model per run (`vtbl[0x90]`), then 0x18DCC50 only queues the model for an update (not a clear) | drawn in both eyes, at zero disparity | none in v1; UI placement is M6. S2 confirms the HUD in eye R | [static-verified, inferred result] |

### 2.5 Per-view switches in `renderView_t` (type info, no code patch)

The eye hook can set these on each eye's view (offsets in `renderView_t`, the view's `g` at idRenderView
+0, screen view +0x30):

| Field | Offset | Use for Route S |
|---|---|---|
| `renderTimeUs` | +0x0 | leave identical in both eyes (one tick, one time) |
| `discontinuousViewPosition` | +0xC | "occlusion queries from the previous frame are no longer valid": fallback for S5 if culling pops at the eye edges |
| `forceFullResolution` | +0x11 | both eyes: no dynamic resolution |
| `inhibitModelFovScale` | +0x13 | both eyes (weapon on the main projection; E3 still open) |
| `cameraCut` / `cameraCutFrames` | +0x16 / +0x18 | not per eye (the game's cuts pass through) |
| `explicitProjectionMatrix`, `useExplicitProjectionMatrix` | +0x50, +0x90 | each eye's asymmetric projection (E2) |
| `vieworg`, `viewaxis` | +0x94, +0xA0 | each eye's pose |
| `subSampleIndex`, `upsamplerSubSampleIndex` | +0x652, +0x654 | v2: eye R copies eye L's |
| `skipAutoExposureUpdate` | +0x74C | eye R: exposure adapts once per tick, both eyes use the same value |
| `disableTssaaNextFewFrames` | +0x750 | alternative TAA switch per view |

### 2.6 Temporal effects and the v1 cvar set

Every name below exists in this build's string table; flags from the registration calls.

| Effect | Cvar(s) | v1 value | Note |
|---|---|---|---|
| TAA and everything using temporal accumulation | `r_TAASafeMode` (0x66DE720, flags 0x2, runtime, read by 0x1C5DFD0, 0x1C6FCC0, 0x1C71630, 0x1C927D0, 0x1C939A0, 0x1CDE740) | 1 | help text: "disable anything relying on temporal accumulation"; its exact reach is S3 |
| TAA / DLSS | `r_antialiasing` (0x6685CE0, flags 0x12) | 0 | DLSS is temporal, so v1 has no DLSS |
| Jitter | `r_jitter`, `r_TAANumSubSamples` | 0 | no jitter without TAA |
| Ambient occlusion (SSDO) history | `r_SSDOTemporalAA` | 0 | `ambientOcclusionAcc0/1` |
| Light scattering history | `r_lightScatteringTAA` | 0 | froxel volume history |
| Depth of field history | `r_dofTAA` (or `r_dof 0`) | 0 | |
| Screen-space reflections | `r_SSR` | 0 in v1 | whether SSR reads the previous frame is S3; re-enable if not |
| RT reflections temporal upscale | `r_raytracedReflectionsTemporalUpscaleQuality` (or `r_raytracedReflections 0`) | 0 | also feeds `upsamplerSubSampleIndex` |
| Water | `r_waterReflectionsTAA`, `r_waterGridTAA`, `r_refractionTAA` | 0 | |
| Motion blur | `r_motionblur` | 0 (default) | needs correct previous matrices, which 2.4 provides |
| Auto exposure | `r_hdrAutoExposureSpeed` | default | shared by design; eye R skips its update |
| Dynamic resolution | `rs_enable` | 0 | |
| Present rate | `r_swapInterval` (0x66E4020, read per frame at 0x1CD869F and at swapchain creation) | 0 | two presents per tick: with FIFO at the rig's 144 Hz the tick rate would be capped at 72 |

### 2.7 Present and per-eye capture

Each eye's frame ends in its own present (0x1CD8380 -> 0x1C35410), carrying the full post-processed
image with the HUD. The layer already copies every present into the D3D12 ring
(`presenter_copy.cpp`, `onPresent`). Route S needs:

- **Eye tags.** The frontend pushes the eye (L, R, or mono) of each render frame into a FIFO when its
  chain starts (the wrapper knows it); the present hook pops one per present. The render thread
  presents frames in order, one per render frame, so the FIFO order holds. A check against
  renderBackend+0xB0 (read at present) catches a skew. [inferred]
- **Pairing.** One ring record per tick holds both eye images and both eye poses (T-069, T-041's 3
  slots); the XR worker submits a projection layer only when a record has both eyes, and a tick that
  rendered mono (menus, loading) is submitted as today.
- **Present suppression.** Not needed: both presents reach the window (the desktop shows both eyes in
  turn). Dropping eye L's present in the layer is not possible without leaking its acquired swapchain
  image; `r_swapInterval 0` removes the pacing cost. T-069's "suppress and count" becomes "tag and count".
- **Size.** Eye images are the window's size, as in mono (T-031 is still open). Per-eye capture at the
  final blit instead of the present is not needed for v1.

### 2.8 Cost, and the path back to temporal AA

**Frame time.** Per tick: two frontend chains (job threads; eye R's frontend overlaps eye L's backend),
two backend frames on the render thread, and about twice the GPU scene work (shadow maps are largely
cached across renders, `r_shadowMaxStaleFrames`). Reference point: mono head-tracked at 2560x2100
(5.4 MP) presented at 143-144 fps on the rig's RTX 4080, FIFO-capped at 144 Hz, so at most 6.9 ms per
frame (`VR_HEAD_TRACKED.md`). Estimate [inferred]:

| Per-eye size | Per-eye render | Stereo tick | Rate |
|---|---|---|---|
| 2064 x 2208 (4.6 MP) | 5-6 ms | 10-12 ms | 72 Hz yes, 90 Hz marginal |
| 2496 x 2688 (6.7 MP, VDXR's recommended) | 7-9 ms | 14-18 ms | below 72 Hz; needs render scale about 0.8 |

T-075's budget for this route (GPU at most 30% over a mono reference of the same total pixel count,
render-thread CPU at most 2x) is plausible for the GPU (the overhead is the per-view fixed passes) and at
the limit for the CPU; S4 measures both. Latency: eye R finishes one backend frame after eye L, so
pose-to-photon grows by one eye render compared with mono.

**Temporal AA (v2).** TAA, DLSS and the other accumulations need one history per eye. The per-view block
holds the history images as pairs selected by the ping-pong index (0x66EF4F0 +0x28..+0x78); a second set
of images for eye R, swapped into the block around eye R's chain together with its own ping-pong value and
jitter bytes, gives each eye its own history. The images have to be created with the engine's image code
(0x1C20150 creates the per-view set) or by the layer. DLSS additionally needs one NGX feature per eye
(T-033, T-069). Not in v1. (The TAA history turned out to be the device context's accumulation pair, picked
by the backend counter's parity, not the 0x66EF4F0 block, which is the light-scattering state; the scheme and
its implementation are in `stereo-temporal.md`.)

### 2.9 Risks

1. **Something that assumes a game tick between render frames** (a list consumed by the first render, a
   simulation stepped by frame count). The S1 identical-view diff shows it directly. [inferred]
2. **Previous-frame occlusion reuse** culls objects visible only from the other eye (pop-in at the outer
   edges). Mitigations: `discontinuousViewPosition` on both eyes, or `r_useUmbraCulling 0` (T-038 option
   a). S5.
3. **Cost.** Twice the scene work and no DLSS in v1; 90 Hz at Quest 3 size needs a lower render scale.
4. **The render-kick stage now holds the frame for two renders**, so the next game tick starts later;
   `com_adaptiveTick` adapts the game rate, but input-to-photon latency grows by one eye frontend. S4.
5. **A nested job list inside the frame-end job.** The engine nests job lists this way everywhere in the
   chain, but 0x1CBEA30 is normally called from the loading path, not from inside a job. S1 (stability).
6. **Engine code the hooks rely on changes with a game update.** Every hook point is signature-located
   (2.11); a miss disables stereo (mono) instead of guessing.
7. **Multiplayer.** Everything here touches the game; it must sit behind the multiplayer guard when it
   lands on main (`origin/mp-guard` is not merged yet).

### 2.10 Minimal live experiments (in order)

| # | Experiment | Settles | Pass |
|---|---|---|---|
| S1 | Eye-R re-run with **both eyes given the same view** (the game's), v1 cvar set, `+r_swapInterval 0`, 2 minutes in e1m1 including a fight; log renderSystem+0x10 and backend+0xB0 per tick, the game's frame count, and pixel-diff each pair of presents | stability; per-tick counters; which state steps per render (any diff between the two images of a pair is such state: particles, water, exposure, animated materials) | no crash or validation error; counters +2 per tick, game frame +1 per tick, game speed normal; pair diff zero outside listed effects |
| S2 | S1 with per-eye poses and projections (the 0x1C754BC hook reading the current eye) and presenter pairing | stereo end to end; HUD and weapon in both eyes | each eye latches its own matrices (0x1C75772 check, as E2); HUD and weapon visible in both eyes; stereo fusion in the simulator and the headset |
| S3 | S2, toggling one temporal cvar at a time back to its default (TAA safe mode, SSR, SSDO, light scattering, water) while strafing | what `r_TAASafeMode` covers; which effects can stay on | no cross-eye ghosting with the effect on, or it stays off |
| S4 | S2 at two per-eye sizes, 60 s on a benchmark route; GPU timestamps and render-thread time per eye | cost against 2.8 and T-075 | recorded; decides the default render scale |
| S5 | S2 at the edge of an arena with dense occluders, looking along walls; then with `discontinuousViewPosition` on both eyes | culling per eye; previous-frame occlusion reuse | no pop-in at the outer eye edges (or the flag fixes it at a measured cost) |

The census (section 4) runs in the same session as S1, as a separate launch.

### 2.11 Signatures (this build, each unique in `.text`)

| Name | RVA | Signature |
|---|---|---|
| Render one frame synchronously | 0x1CBEA30 | `48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 B8 70 38 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 60 38 00 00` |
| Frame middle job queuing the frame-end job (the `mov rdx,[rip+X]` at 0x1CBA17E reads the `.data` slot 0x39A9600) | 0x1CBA160 | `48 8D 54 24 20 E8 ?? ?? ?? ?? 48 8B 8B 38 0F 00 00 48 8D 54 24 20 E8 ?? ?? ?? ?? 48 8B 15 ?? ?? ?? ?? 48 8D 4C 24 20 4C 8B C7 E8 ?? ?? ?? ?? 48 8D 4C 24 20 E8 ?? ?? ?? ??` |
| Guard clear in the frame-end job (fallback hook) | 0x1CBA3A5 | `48 8D 4C 24 70 C6 43 08 00 E8 ?? ?? ?? ?? 48 8B 8C 24 B0 38 00 00 48 33 CC E8 ?? ?? ?? ??` |
| Previous-matrix store call in the world-views pass (hook after it at 0x1C75D81, rdi = idRenderView) | 0x1C75D6D | `48 8D 56 30 48 8B F8 E8 ?? ?? ?? ?? 48 8B CF E8 ?? ?? ?? ?? 48 8D 56 10 45 33 C0 48 8B CF E8 ?? ?? ?? ??` |
| Per-eye view hook, post-latch hook, store previous matrices, render-frame job | 0x1C754B7, 0x1C75750, 0x1CE2340, 0x1CB9EE0 | `stereo-reentry.md` section 10 |

## 3. Route M: shader-side per-eye transform (MultiviewTransform, T-004 default)

**What it needs** (ARCHITECTURE section 7, T-041, T-049 to T-053, T-070, T-071, T-083): the game renders
one enclosing view; the layer makes every world draw produce both eyes.

- A SPIR-V rewrite `gl_Position = C_e * gl_Position` in every vertex shader that reads the view constants,
  with `C_e = P_e E P_c^-1` in a reserved descriptor set (T-041), clip conventions matched (T-083).
- Multiview (or instancing) on every render pass that draws the world: every attachment and every image
  those passes touch promoted to 2-layer arrays; DOOM Eternal is bindless, so every 2D view that can land in
  a shared descriptor array becomes `2D_ARRAY` and its readers change (T-052), in layer-owned memory.
- Compute passes that are per view (the deferred and post chain: light and decal binning, SSDO, SSR,
  light scattering volumes, TAA, DOF, bloom, tone mapping, GPU triangle culling, particles): each is
  dispatched once per eye with per-eye data (T-050's data patch, T-071's barriers) and per-eye image
  layers, or patched to handle both.
- Semantic patches: `gl_FragCoord` bin lookups remapped into the enclosing camera's screen space and
  screen-texture fetches not (T-051), previous-clip outputs taking `C_e(prev)` (T-051), viewmodel draws if
  they carry their own projection (T-053).
- Async compute frame association (T-041) and NGX per eye (T-033).

**Effort.** The largest item in the plan: the patcher, the promotion closure and the per-eye compute
dispatch each touch every pipeline, and the oracles of T-043 and T-051 are the only way to know it is
right. It is months, not weeks, and it is per driver and per game update.

**Risk.** The deciding unknown is the share of modules that need semantic patches. id Tech 7 does most of
its screen-space work in compute and bins lights and decals by screen tile, so the fragment and compute
classes of T-070 are likely large; the 15.0% criterion of T-105 may well fail. Benefits if it passes: one
scene traversal for both eyes, temporal AA and DLSS kept (the engine sees one frame), a GPU cost well under
Route S.

**Why the census comes next.** It is observe-only (no game memory is read or written), takes seconds of
play, and replaces the M1.5 offline capture as the first measurement: it gives the module count, the
draw-weighted count, and the classification T-070 asks for. The value-matching step of T-050 (which
uniform member *is* the view constant) still needs the enclosing projection at draw time; the census uses
a structural heuristic until then (section 4).

## 4. Census tooling (this branch)

Usage and output are in `tools/shader_census/README.md`. In short:

- **Dump (layer).** `ETERNALVR_DUMP_SHADERS=<dir>` (off when unset: no hook is handed out and dispatch
  is unchanged), `ETERNALVR_DUMP_SKIP_FRAMES` (default 0) and `ETERNALVR_DUMP_FRAMES` (default 60)
  presents for the draw log. It writes `modules/<fnv1a64>.spv` (every module once, also inline stage
  code), `pipelines.jsonl` (stages, module hashes, entry points, specialization data, layout),
  `layouts.jsonl` (set layouts, push-constant ranges), `sets.jsonl` (buffer descriptor ranges) and
  `drawlog.jsonl` (pipeline binds, set binds with dynamic offsets, push constants, draws, dispatches).
  Modules are created at map load, so the variable is set at launch; a skip of about 4000 presents is
  about 30 s into e1m1. It reads no game memory and writes nothing the game sees, so it is not gated on
  the multiplayer guard (not on main yet); once the guard lands it must also stay off after a trip.
- **Census (offline).** `python tools/shader_census/census.py <dir> [--frames A:B] [--view-members
  members.json] [--disasm]` classifies every module by parsing SPIR-V directly (no SDK needed;
  `spirv-dis` from the SDK only for `--disasm`) and joins the draw log: T-105's module share and
  draw-weighted share, with counts and denominators, and whether 15.0% is crossed.
- **Semantic patch** (T-070, T-105): a fragment, compute or other non-position stage reads view
  constants; `gl_FragCoord` feeds a buffer index or texel-buffer fetch (bin lookup, T-051); or a
  position stage writes a vec4 besides `gl_Position` from a matrix product (previous clip, T-051).
  Counted but not semantic: `gl_FragCoord` screen fetches from 2D images, images array promotion would
  change (bindless runtime arrays included, T-052), and compute using `GlobalInvocationID`.
- **Limits.** Until a live value match (T-050) supplies the view-constant members (`--view-members`), any
  4x4 or 4x3 matrix read from a uniform, storage or push-constant block counts as a view constant, so the
  shares are upper bounds (object and bone matrices inflate them). Data flow is followed through locals
  and calls, not through memory a shader writes and reads back. A draw is tagged with the present count at
  recording time; handle reuse after destruction takes the last definition; module-identifier stages,
  push descriptors, descriptor buffers, mesh and ray-tracing commands are not logged; an indirect draw is
  one record; one global mutex per recorded command (fine for seconds, not for continuous use).
- **Tests.** 18 Python tests on hand-built SPIR-V (valid under `spirv-val` when the SDK is present) run
  under ctest (`evr_shader_census_tests`), and the dump's pure helpers (hashing, JSON escaping, frame
  window, `pNext` walk) are C++ unit tests in `evr_vkcore_tests`. The dump itself has not run in the game.

**Census run for the next rig session** (after S1, a separate launch; e1m1, 60 frames of gameplay):
`tools\rig\launch-ht.ps1 ... -ExtraEnv 'ETERNALVR_DUMP_SHADERS=<workspace>\tmp-vr\census1',
'ETERNALVR_DUMP_SKIP_FRAMES=4000'` (`-ExtraEnv` is a string array), then `census.py` on that folder;
record the result in `docs/rig-findings/stereo-spike.md` as T-070 asks.

## 5. Consequences for the plan

- T-066 / T-069: Route S's re-run point is 0x1CBEA30 called from a wrapper on the frame-end job slot
  0x39A9600; T-069's "suppress the first present" becomes "tag and pair"; its "no per-frame state
  advances twice" is refined by 2.4 (rings and counters must advance per render; history is per eye or
  off; exposure is updated once per tick).
- ROADMAP M4's sequential criteria hold as written, with one change: "frame index and command-pool and
  ring indices advance once per tick" should read "once per render, twice per stereo tick", since that is
  what keeps the two renders' resources apart.
- T-049 / T-070: the census replaces the offline capture as the first input to the Route M decision; the
  decision itself is unchanged (15.0% of modules or of draws, T-105).
- D-004: Route S is not alternate-eye rendering: both eyes come from one tick and are submitted together.
