# Stereo re-entry: static reconnaissance (T-069, D-032, D-037)

Static analysis of the retail `DOOMEternalx64vk.exe`, Steam build 25216728 (the same file as
`engine-facts.md`: PE timestamp 2026-08-11 22:00:44 UTC, SHA-256 `69dc13e8...6a1c11`). The game was not
run. Tools: the Python helpers in `analysis/scripts` (PE loader, xrefs, cvar resolver, signature
builder), capstone, and Ghidra 12.1.4 headless (import without auto-analysis; functions decompiled on
demand). All addresses are RVAs in this build.

Every claim carries one tag:

- **[static-verified]**: read in the code or data of this build (decompiled or disassembled end to end,
  or a type-info / RTTI record).
- **[inferred]**: a reading of what the verified code implies; the rig has to confirm it.

## 1. Verdict

**Synchronized sequential stereo: UNKNOWN, needs a live test, and the static picture favours it, in one
specific form.**

- **Re-entering the render frame** (running the render-frame job twice per game tick, the UEVR/REFramework
  pattern taken literally) is **NO-GO**. The render-frame job has a re-entry guard, and each run advances
  the render frame counter, the TAA jitter index, the previous-frame matrices and the per-view history
  ping-pong, and presents once. Every one of those would need save and restore, and the second eye would
  still read the first eye's history (sections 4 and 5). [static-verified for each mechanism, inferred
  for the combined effect]
- **The engine's own two-view path is the natural re-entry point and looks viable.** id Tech 7 already
  renders N `idScreenView`s per frame from one game tick: one loop, one render-view latch per view,
  one Umbra cull request per view, and per-view backend state (TAA accumulation buffers, the history
  ping-pong index and a block of history images) indexed by `idRenderView.viewIndex` and sized by the
  INIT cvar `r_maxRenderViews` (default 1). Some history (auto exposure, AO, light scattering, the Umbra
  occlusion buffer) is allocated per device context instead, and whether two views share it is open. A `leftRightStereo` screen layout exists and
  the dormant VR code selects it. This *is* synchronized sequential stereo (both eyes from the same game
  tick, rendered one after the other by the engine), done by the engine instead of by us. [static-verified]
- **Blockers found statically:** (1) each `idRenderWorldLocal` constructs exactly one `idRenderView`, so
  the second screen view's `RenderViewForIndex(1)` returns null and the loop then copies into it (crash
  expected); (2) `r_maxRenderViews` is 1 unless set on the command line; (3) the TAA jitter index is set
  only on screen view 0; (4) the weapon/hands projection is rebuilt from `weaponFOVX/Y` as a symmetric
  frustum even when the main view uses an explicit projection. (1) needs a small injection (construct and
  register a second `idRenderView`); (2) is a launch argument; (3) and (4) are one-field fixes in a hook.
  [static-verified for the code, inferred for the fixes]

Recommended route: **"engine multi-view"**: `+r_maxRenderViews 2`, a two-entry screen layout, a second
`idRenderView` per world, and a per-view hook that writes each eye's pose and asymmetric projection into
the view the engine is about to latch (section 6). The live experiments in section 7 settle it in a few
launches.

## 2. Render entry chain

Game side (from `engine-facts.md` 4.2): `idMapInstanceLocal::RunFrame` 0x6E1F60 -> build point 0x6A2C10
writes `gameFrameReturn_t.players[i].view` (camera hook at 0x6A31B7). [static-verified]

Frame job graph. `idCommonLocal` vtable slot 0x20 (0x43D1F0) drives the frame; its stages are job
functions in the table at 0x388EDB0 (0x439790 ... 0x43AD10), queued with 0x43DAE0 and run through the
job-list helpers 0x17E5BC0 (begin) / 0x17E5D70 (allocate job parameters) / 0x17E5FC0 (add) / 0x17E5C60
(submit and wait). [static-verified for the table and the helpers' use; inferred for the names and for
which OS threads run the stages]

| # | Stage | RVA | Signature / arguments | Evidence | Tag |
|---|---|---|---|---|---|
| 1 | Frame-info stage (job 0x388EDF8) | 0x43A120 | `void (packet*)`; asserts `idCommonLocal::Frame - frameInfo->mapInstance == NULL!` | calls `vtbl[0x70]` of the object at `*(0x47DDA60)` (returns the frame number passed on), then 0x17E7E20, then `vtbl[0xE8]` of the same object; that the object is the render system (`idRenderSystemLocal` slots 0x70 = 0x1CBB2D0, 0xE8 = 0x1CD6A10) is inferred | [static-verified calls, inferred object] |
| 2 | Build frame info | 0x17E7E20 | `(frameBuilder* this = *(0x47DDAF8), gameFrameReturn_t*, gameSystem*, int frameNumber, bool, bool, bool)` | stores the arguments at this+0x2A50/0x2A58/0x2A64; when `r_threadedRenderGui` is set it queues the body as a job, else runs it inline | [static-verified] |
| 3 | Build screen views (body) | 0x17E8740 | `(frameBuilder*)` | builds `idRenderFrameInfo.screenViews` (this+0x0, `idStaticList<idScreenView,2>`) and `worldViews` (this+0x1520) from the screen layout at this+0x2A70; reads every `stereoRender_*` cvar and `multiView_60Hz`; see section 3 | [static-verified] |
| 4 | Render-kick stage (job 0x388EE38) | 0x43A8C0 | | calls 0x17E8560 | [static-verified] |
| 5 | Queue render frame | 0x17E8560 -> 0x1CBE9B0 | `(renderSystem 0x66E2C30, jobList, ..., frameInfo, bool)` | queues the job function at 0x39A95F0 = 0x1CB9EE0 (also reached from 0x17E85E0 / 0x1CBEA30 for loading and menu frames) | [static-verified] |
| 6 | Render frame job | 0x1CB9EE0 | `(packet{renderSystem*, frameInfo*, ...})` | **re-entry guard** `if (renderSystem+8 == 0) { renderSystem+8 = 1; ... }`; `renderSystem+0x10`++ (render frame counter); sets `screenViews[0].g.subSampleIndex` (+0x682 in the screen view) = counter % `r_TAANumSubSamples` and `upsamplerSubSampleIndex`; calls 0x1C75CF0; queues 0x39A95F8 | [static-verified] |
| 7 | World views pass | 0x1C75CF0 | `(frameInfo*, jobList*, ..., int)` | unless `r_skipUpdateInView`: for each `worldViews[i]`: `rv = world->RenderViewForIndex(view.viewIndex)`; `rv->g = view.g` (0x43C530); **0x1CE2340** (store previous matrices); **0x1CE1400**`(rv, &view.renderRect, 0)`; then for view 0 only `world->vtbl[0x120]` (0x18E5070, kicks the per-world jobs 0x18DEAF0 ... 0x18E05E0); then queues the job at 0x39A5C70 = 0x1C75290 | [static-verified] |
| 8 | **Screen views pass (the per-view render loop)** | 0x1C75290 | `(packet{frameInfo*, renderList*, mode})` | for each `screenViews[i]` with a world and without `usePreviousRendering`: `world->vtbl[0x240]`; `rv = world->RenderViewForIndex(screenView.viewIndex)` (vtable slot 0x118, 0x18E6C00); `rv->g = screenView.g` (call 0x1C754B7, returns to 0x1C754BC); VR pose (section 3.3); `r_debugInvert2ndView` for i == 1; **0x1CE1400**`(rv, &rect, screenView.viewIndex)` (call 0x1C7576D, returns to 0x1C75772); when `r_umbraJobKickoff == 2` ("super early", the default) submits the cull request for this view (0x1D19280, 0x1D1A5A0); then collects and renders the view GUIs | [static-verified] |
| 9 | Render-view latch and matrix build | 0x1CE1400 | `void (idRenderView*, const idScreenRect*, int viewIndex)` | `r = g`; `viewIndex = arg3` (+0x28990); builds projection / view / VP / custom VP / centred VP / frustum vectors; skipped entirely when `r_skipCommits` | [static-verified] |
| 10 | Render-view jobs | 0x1C54650 (job pointer 0x39A1DC8, queued from 0x1C5C2F0), 0x1C5DED0, 0x1C5DFD0, 0x1CEEDF0, 0x1CEF430, 0x1CFB7B0, 0x1CFBF90, 0x1CFC050, 0x1D015D0, 0x1D01A50 ... | take `idRenderView*` | all index per-view state by `idRenderView.viewIndex` (section 5) | [static-verified] |
| 11 | Render thread body | 0x1CD8380 (from `idRenderThread` 0x1CDE500) | `(renderThread*)` | per-view loops over the render list (stride 0x118); `renderBackend(0x66E3B88)+0xB0`++; VR HMD submit (`hmd->vtbl[0x20]`, `[0x30]`) when VR is active; swap (0x1C35410 with `r_swapInterval`) | [static-verified for the calls, inferred for "backend frame"] |

This explains the rig observation in `VR_HEAD_TRACKED.md` ("two latches per game view, on render job
threads"): 0x1CE1400 is called once from the world-views pass (0x1C75D8B) and once from the screen-views
pass (0x1C7576D) for the same view. [static-verified for the two call sites]

## 3. The multi-view / stereo scaffold

### 3.1 Screen layouts

`idRenderFrameInfo` (type info, size 0x2A50): `screenViews` is `idStaticList<idScreenView, 2>`, comment:
"There is typically a single screenView, but split-screen multiplayer or stereo-3D will define two
views ... The views are processed in order". `worldViews` (+0x1520): "two identical ones in stereo-3D
(both centered between the eyes)". `idScreenView.viewIndex` (+0x20): "determines which viewColor image
will be rendered to, and which idRenderView from world will be used." `usePreviousRendering` (+0x25):
"consoles can't render two views at 60Hz, so we toggle between them". [static-verified]

The layout is a null-terminated table of 0x28-byte entries `{const char* name; int slot; float x, y, w,
h; bool 0x1C; float eye 0x20; bool 0x24}`, one header entry per layout followed by its views
[static-verified from the data and its use in 0x17E8740]. Built-in tables at 0x2E50950 (`.rdata`):

| Layout | Views (slot, rect x/y/w/h, eye) |
|---|---|
| `single` | (0, 0/0/1/1, 0) |
| `singleHalf` | (0, 0.5/0/0.5/1, 0) |
| `leftRightStereo` | (0, 0/0/0.5/1, 0), (1, 0.5/0/0.5/1, 0) |

[static-verified]

The layout pointer lives at frameBuilder+0x2A70 (frameBuilder = `*(0x47DDAF8)`). 0x17E8480 sets it to
`leftRightStereo` when the VR-active flag (0x6BDA9A8) is set, else `single`; its only caller is
`idGameSystemLocal` vtable slot 0x18 (0x66B320). 0x17E83E0, called every frame, only acts for two local
players (it looks up `splitscreen`, which is not in the table, and falls back to `single`).
[static-verified] Slot 0x18 is an init-time call, so the VR flag has to be set before it runs (command
line). [inferred]

Per view, 0x17E8740: `screenRect = rect * window size`; `viewIndex = slot`; `g = players[localPlayer[slot]].view`
(0x43C530; with one player, slot 1 maps to player 0); then, **when the entry's eye value is non-zero**:
`g.vieworg += g.viewaxis[1] * eye * (swapEyes ? +1 : -1) * stereoRender_separation`,
`g.inhibitModelFovScale = 1`, `g.explicitProjectionMatrix = Projection(fov_x, fov_y, zNear or r_znear,
r_zfar, xOffset = eye * sign * stereoRender_screenSeparation)` (0x39A310), `g.useExplicitProjectionMatrix
= 1`; `guiOriginOffset = eye * stereoRender_guiOffset`. The world-views copy (list 2) gets the uncorrected
view. All built-in layouts have eye = 0, so this offset code never runs on the built-in data. [static-verified]

When the view count is above 1 and `multiView_60Hz` is 0, all views except `frameNumber % count` get
`usePreviousRendering = 1` (alternate-frame rendering). Default is 1 (every view every frame).
[static-verified]

### 3.2 The dormant VR system (R15 "EngineNativeStereo")

| Item | Value | Tag |
|---|---|---|
| `vr_enable` | object 0x6BDA9B0, `CVAR_BOOL`, default 0, "Enable to run in VR mode." | [static-verified] |
| `vr_dummyDevice` | object 0x6BDAA30, `CVAR_BOOL`, default 0 | [static-verified] |
| Init | 0x1DCF240 (from 0x430EC0, `idCommonLocal` slot 0x8): if `vr_dummyDevice`, 0x1DCF0C0 creates `idVRSystem_Dummy` (vtable 0x2ED0F40) with one `idVRHeadMountedDisplay_Dummy` (vtable 0x2ED0EE0, 0xB8 bytes); system pointer at 0x6BDA9A0; if `vr_enable`, sets the VR-active flag 0x6BDA9A8 | [static-verified] |
| Per frame | 0x1DCF330 (from 0x43FB20): sets or clears the VR-active flag as `vr_enable` changes; raises `com_adaptiveTickMaxHz` to at least 120 while active | [static-verified] |
| HMD accessor | `hmd = **(system + 8)` (0x1DCF670, 0x1DCFC30) | [static-verified] |
| HMD vtable (dummy) | +0x38 projection `(hmd, eye, zNear, zFar, out)`: 0x1DCEFE0 = `Projection(90, 90, zNear, zFar, 0, 0)`; +0x40 eye-from-head transform: 0x1DCF040 = identity (no IPD); +0x48 render size: 0x1DCF020 = 1344 x 1512; +0x20, +0x30 (frame submit): empty stubs | [static-verified] |
| Use in 0x17E8740 | when VR is active: `useExplicitProjectionMatrix = 1` and `hmd->vtbl[0x38](hmd, slot != 0, ..., &g.explicitProjectionMatrix, r_zfar)` | [static-verified] |
| Use in 0x1C75290 | when VR is active, after `rv->g = screenView.g`: `M(g) * inverse(M(hmd+0xC origin, hmd+0x18 axis)) * pose(hmd+0x78, 3x4) * hmd->vtbl[0x40](slot != 0)` written back to `rv->g.vieworg / viewaxis` | [static-verified for the calls and fields, inferred for the exact matrix order] |
| Other users | `idPlayer::CalculateView` 0x14514D0 reads `hmd+0xC / +0x18`; 0x440E70 (debug draw of two tracked devices); the render thread submits to the HMD | [static-verified reads, inferred purpose] |

**Why the native path cannot work unmodified.** `RenderViewForIndex` (0x18E6C00) returns
`world->renderViews[i]` for `i < count` (+0x5E71C8) and otherwise prints
`idRenderWorld::RenderViewForIndex: invalid index %i [0, %i]` and returns null. The world constructor
0x18E1DB0 builds that list with inline capacity 1 (+0x5E71CC = 1), allocates **one** `idRenderView`
(0x29950 bytes, constructor 0x1CE0960, the only call site at 0x18E2A0A) with `viewIndex = 0` and
`owningWorld = world`. The only other writer of the list is vtable slot 0x110 (0x18E7480), which changes
the count within capacity and allocates nothing. [static-verified] The screen-views loop copies into the
result without a null check. So `leftRightStereo` (slot 1) should log the invalid-index error and crash
at the copy after 0x1C754B7. [inferred: this is live experiment E1]

### 3.3 Answer to T-069 "does the scaffold already loop the render over views?"

Yes. 0x1C75290 loops over `screenViews` (up to 2) and runs, per view, the latch 0x1CE1400 with that view's
`viewIndex`, the Umbra cull request, and the GUI pass; the backend then processes the render list per view
(stride 0x118). The per-view backend state is indexed by `viewIndex` (section 5). The loop is the natural
re-entry point: adding a second view there *is* rendering the scene twice in the same frame, and the
engine keeps the per-view state apart. [static-verified for the loop and indexing; inferred that the
second view renders correctly end to end]

## 4. Per-frame state (T-069 list) and where it advances

"Once per render frame" means once per run of the render-frame job 0x1CB9EE0. "Per view" means once per
screen view in that run.

| State | Where it advances | Rate | If the render frame were re-entered | With two screen views | Tag |
|---|---|---|---|---|---|
| Render-frame re-entry guard | `renderSystem+8` set in 0x1CB9EE0 | per frame | a second run in the same frame is a no-op until the flag is cleared | n/a | [static-verified set; inferred clear point] |
| Render frame counter | `renderSystem+0x10`++ in 0x1CB9EE0 | per frame | advances twice | once | [static-verified] |
| TAA jitter index | `screenViews[0].g.subSampleIndex / upsamplerSubSampleIndex` from the counter in 0x1CB9EE0 | per frame, **view 0 only** | eyes get different jitter phases each tick | view 1 keeps the game's value (not jittered): copy view 0's two bytes | [static-verified] |
| Previous-frame matrices | 0x1CE2340: `previousViewProjectionMatrix = viewProjectionMatrix` and the custom / centred / origin / size equivalents, and `cameraCutCounter`-- ; per `idRenderView`, before the latch, from the world-views pass | per frame per `idRenderView` | the second eye's "previous" = the first eye's current: wrong motion vectors | correct, once a second `idRenderView` exists | [static-verified] |
| Per-view history ping-pong | 0x1CFC050: `perView[viewIndex].pingPong ^= 1` (block at 0x66EF4F0, stride 0xAF8) | per view render | with one viewIndex, eye 2 reads eye 1's output as history | separate per viewIndex | [static-verified] |
| Per-view Umbra / cull context | `renderBackend+0x220 + viewIndex*8`; 0x1C5DED0 increments its +0x2BC1F4 counter | per view render | shared context, advanced twice | separate per viewIndex | [static-verified index, inferred meaning of the counter] |
| Umbra cull request | `r_umbraJobKickoff` (default 2 "super early"; 0 no, 1 early, 3 deferred): mode 2 submits from the screen-views loop 0x1C75290 right after each view's latch; mode 3 from the render thread (0x1C5DED0); other modes from the render-view job 0x1C5C2F0 | per view | re-queried per entry | queried per view (per-eye culling for free) | [static-verified] |
| Game-thread Umbra kick | none found: all kick sites are in the render-frame job graph, not in the game frame | | | | [static-verified for the sites found; inferred that there is no other] |
| World jobs (`world->vtbl[0x120]`, 0x18E5070) | from the world-views pass, **view 0 only** | per frame | run twice | once, from the centred world view | [static-verified] |
| Backend frame counter, fences, present | 0x1CD8380: `renderBackend+0xB0`++, HMD submit, swap | per frame | two presents per tick (T-069 "suppress and count") and a second fence slot | one | [static-verified calls, inferred ring behaviour] |
| Screen-view lists | cleared and rebuilt in 0x17E8740 (0x17E8250) | per frame | rebuilt, fine | fine | [static-verified] |
| GPU particles / simulations | not traced (`r_skipGPUParticles`, `enableAsyncGPUParticles`, water sim buffers `waterHitsSim0/1` are per device context) | unknown | likely stepped twice | per-view buffers suggest per-view stepping | [inferred] |
| Frame-info layout pointer | frameBuilder+0x2A70, set at game-system init | once | | | [static-verified] |

## 5. Temporal history storage (TAA, exposure, AO, light scattering)

- `r_maxRenderViews` (object 0x6676110): `CVAR_INTEGER | CVAR_INIT` (0x4002), default "1", "Maximum of
  render view rendered per frame - used for per view resource allocations". [static-verified]
Three levels of storage, all static-verified in their allocation code:

- **Per render view inside each device context.** The `idDeviceContext` constructor 0x1C19C10 loops
  `i < r_maxRenderViews` and calls 0x1C1CC40 and 0x1C20150 for each `i`; 0x1C20150 creates
  `accumulationBuffer%d%d` (the TAA accumulation pair), `accumulationBufferOpaque%d`, `viewColor%d` and
  `distortion%d`. [static-verified]
- **Per view, global.** The block at 0x66EF4F0 (stride 0xAF8, `r_maxRenderViews` entries; loops 0x1CFD310,
  0x1D00FD0, 0x1D04390, 0x1D04510) holds the history ping-pong index and about a dozen history image
  handles that the render jobs bind as shader parameters; every accessor indexes it by
  `idRenderView.viewIndex` (0x1CEEDF0, 0x1CEF430, 0x1CEF620, 0x1CEF930, 0x1CEFCB0, 0x1CFB7B0, 0x1CFBF90,
  0x1CFC050, 0x1D015D0, 0x1D01A50, 0x1D03460). A second table at `renderBackend+0x28 + viewIndex*0xA8` is
  used the same way. [static-verified]
- **Per device context, not per view.** 0x1C1CD80, called once per device context by the constructor
  (before the per-view loop), creates the `_dc%d_%s` images `autoExposure%d`, `autoExposureLum%d`,
  `ambientOcclusionAcc0/1`, `umbraOcclusionBuffer%d`, `lightScatteringPacked*Acc0/1`, `dofAccBuffer*0/1`,
  `waterSSRAccumulationBuffer0/1`, the water simulation buffers and others. [static-verified] Which of
  these the renderer then selects per view (through the 0xAF8 block) and which two views would share is
  not traced. [inferred: open question 2]

So the TAA accumulation and the history ping-pong are **per view**, keyed by `viewIndex`, with
`r_maxRenderViews` slots. With the default of 1, a view with `viewIndex` 1 would index past the
allocated block. [inferred] Both eyes must therefore have distinct `viewIndex` values *and* the game must
start with `+r_maxRenderViews 2`. Auto exposure shared between the eyes would be harmless (both eyes should
match); shared AO, light-scattering or Umbra occlusion history would show as cross-eye ghosting or
culling errors (E5, E6).

## 6. `explicitProjectionMatrix` is honoured

In 0x1CE1400, after `r = g`: if `r.useExplicitProjectionMatrix` (+0x28A60 = r+0x90) is 0, the projection
is built from `r.fov_x / fov_y` (0x39A310) and inverted (0x39A070); otherwise
`projectionMatrix = r.explicitProjectionMatrix` (+0x28A20 = r+0x50) and it is inverted with 0x39B4D0
(`Projection Matrix Invert failed!` on failure). The same choice feeds `centeredViewProjectionMatrix`
(+0x296B0). [static-verified] So an arbitrary asymmetric per-eye projection can be set in the view with no
code patch, as far as the main view is concerned.

Caveats:

- `customViewProjectionMatrix` (+0x295B0, hands and guns) and `customViewProjectionMatrix2` (+0x29630) are
  always rebuilt from `weaponFOVX/Y` and `customFOV2X/Y` as symmetric frusta, with no check of
  `useExplicitProjectionMatrix`. [static-verified] The stereo path sets `inhibitModelFovScale` ("For
  stereoscopic 3D rendering, we don't want to allow the hands/weapons to use a custom (inconsistent) FOV"),
  so the model side presumably switches to the main matrix when it is set; its reader was not found
  (there is no direct displacement to `r+0x13`). [inferred; live experiment E3]
- 0x1C5DFD0 reads `useExplicitProjectionMatrix` together with the matrix element [3][3] (+0x28A5C) and sets
  a backend flag when [3][3] is non-zero (an orthographic matrix). A perspective matrix has [3][3] = 0, so
  this does not trigger. [static-verified read, inferred meaning]
- The projection builder 0x39A310 is `(fovXDeg, fovYDeg, zNear, zFar, xOffset, yOffset, out)` with the
  offsets in near-plane units, so it can build the asymmetric OpenXR frustum directly; the matrix layout is
  the one the latch expects (entries [14] = -1, [10] = -zFar/(zFar-zNear), [11] = -zNear*zFar/(zFar-zNear)).
  [static-verified]
- The rig already showed the latched projection matches `fov_x/fov_y` exactly (`VR_HEAD_TRACKED.md`), so
  the non-explicit branch is live; the explicit branch is live experiment E2.

## 7. Recommended hook points

For the engine multi-view route:

1. **Two screen views.** Point frameBuilder+0x2A70 (frameBuilder = `*(0x47DDAF8)`) at a layout table in
   our memory, `{hdr}, {slot 0, 0/0/0.5/1, eye 0}, {slot 1, 0.5/0/0.5/1, eye 0}, {end}`, or set
   `vr_enable` + `vr_dummyDevice` before game-system init to get `leftRightStereo`. Keep eye = 0 and write
   the per-eye pose ourselves (item 3) rather than using `stereoRender_separation`. Side by side at half
   width each: a window of 2W x H gives W x H per eye. [static-verified mechanism; inferred behaviour]
2. **A second `idRenderView` per world.** After the world constructor 0x18E1DB0 (or lazily, before the
   first frame), allocate 0x29950 bytes, construct with 0x1CE0960, set `viewIndex` (+0x28990) = 1 and
   `owningWorld` (+0x29918) = world, grow the list at world+0x5E71C0 (data), +0x5E71C8 (count), +0x5E71CC
   (capacity) to two entries (the inline storage holds one, so the list needs a heap array), and destroy it
   before the world destructor 0x18E2FE0 frees the list. [inferred: the design; the offsets are
   static-verified]
3. **Per-eye pose and projection: the screen-views loop after the view copy**, call site 0x1C754B7
   (`call 0x43C530`, returns to **0x1C754BC**), in 0x1C75290. At 0x1C754BC, `r12` = render-list entry
   (`+0x30` = `idRenderView*`), `r13` = `idScreenView*` (`+0x20` = viewIndex). Write `rv->g.vieworg`,
   `viewaxis`, `explicitProjectionMatrix`, `useExplicitProjectionMatrix = 1`, `inhibitModelFovScale = 1`,
   and copy `subSampleIndex / upsamplerSubSampleIndex` from view 0 to view 1. This runs before the latch
   at 0x1C7576D and before the Umbra request, so culling and matrices follow the eye. Signature below.
   [static-verified registers from the disassembly; inferred as the best point]
   - Alternative with no code patch on the render side: with the dummy VR device, replace the dummy HMD's
     vtable slots 0x38 (projection per eye) and 0x40 (eye-from-head) and write the head pose at hmd+0x78;
     the engine then does the per-eye math itself. [static-verified slots; inferred usability]
4. **Keep the existing hooks.** The camera hook at 0x6A31B7 still sets the head-centred view once per game
   tick (both eyes derive from it, so they share a tick). The latch observer at 0x1CE1464 will now see
   `viewIndex` 0 and 1 (+0x28990) and can confirm each eye's matrices.
5. **Present.** One present per frame carries both eyes (the side-by-side swapchain image), so the T-069
   rule about suppressing a second present does not arise. [inferred]

If the literal re-entry route is ever tried anyway, the entry is the render-frame job 0x1CB9EE0, and the
state in section 4 marked "per frame" has to be saved and restored around the second run (guard flag,
counter, jitter bytes, the 0x1CE2340 previous matrices, the 0x1CFC050 ping-pong, the Umbra context
counter), plus a second present suppressed. Not recommended. [inferred]

## 8. Live experiments

Each one is a launch with the rig scripts and the logging arguments from `launch.md`; none needs the
layer except E2 to E5.

| # | Experiment | Settles | Expected (static reading) |
|---|---|---|---|
| E1 | `+vr_dummyDevice 1 +vr_enable 1` (and again with `+r_maxRenderViews 2`) | whether the native two-view path runs; R15 outcome b1/b2 | `leftRightStereo` chosen; `RenderViewForIndex: invalid index 1 [0, 1]` in the log, then a crash in 0x1C75290 after 0x1C754B7 |
| E2 | Hook at 0x1C754BC (one view, the default layout): write an asymmetric explicit projection (for example the Quest 3 left eye, 54/40 horizontal) and read the latched `projectionMatrix` at 0x1CE1464 | explicit projection honoured end to end; hands and gun projection | latched matrix equals the one written; check whether the gun still uses the symmetric weapon FOV with and without `inhibitModelFovScale` |
| E3 | E2 plus `inhibitModelFovScale = 1` | whether the flag moves hands and guns onto the main projection | weapon drawn consistently with the world in the asymmetric frustum |
| E4 | `+r_maxRenderViews 2`, custom two-view layout, second `idRenderView` injected (section 7 items 1 and 2), both eyes given the same pose | the engine multi-view path end to end: backend, per-view resources, GUIs, composite | two identical half-width images; frame time about 1.7-2x render cost; no validation errors |
| E5 | E4 with per-eye poses and projections, TAA on, fast head motion | per-eye TAA history, jitter copy, motion vectors | no cross-eye ghosting; if view 1 is blurrier or aliased, the jitter copy is missing |
| E6 | E4 with `r_umbraJobKickoff 3` and `r_useUmbraCulling 0` | culling per eye | no pop-in at the edges of the outer-eye frustum |
| E7 | Watch `renderSystem+0x10` and `renderBackend+0xB0` (x64dbg or the layer's log) during E4 | per-frame counters advance once per frame with two views | +1 per frame each |
| E8 | GPU particles and water in E4 (a map with both) | per-view stepping of GPU simulations | particles look identical in both eyes and run at normal speed |

## 9. Open questions

1. Does anything else break with a second `idRenderView` (world-view GUIs, feedback composition, the
   `viewColor%d` blit into `screenRect`, dynamic resolution)? E4.
2. Which history images are per view and which are per device context (auto exposure, AO accumulation,
   light scattering, DOF, `umbraOcclusionBuffer`)? Only the 0xAF8 block and the `renderBackend+0x28`
   table were traced as per view. E5 and E6.
3. Where is `inhibitModelFovScale` read, and does it switch hands and guns to the main projection? E3.
4. Are GPU particle and water simulations stepped once per frame or once per view? E8.
5. Where is the render-frame guard (`renderSystem+8`) cleared, and which OS threads run the frame stages
   (pipeline depth, `engine-facts.md` question 17)? Only needed for the literal re-entry route.
6. Does `idPlayer::CalculateView` apply the HMD pose to the game view when VR is active (it reads
   `hmd+0xC/+0x18`)? Only matters if the dummy-HMD alternative in section 7 item 3 is used.

## 10. Signatures (this build, each unique in `.text`)

`??` masks rel32 and RIP displacements.

| Name | RVA | Signature |
|---|---|---|
| Screen-views loop (function start) | 0x1C75290 | `40 55 41 56 48 8D AC 24 ?? ?? ?? ?? B8 28 52 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 ?? ?? ?? ?? 48 89 9C 24 ?? ?? ?? ?? 33 D2 48 8B D9` |
| Per-view copy, then VR check (hook after the first call, 0x1C754BC) | 0x1C754B7 | `E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ?? 49 8B 44 24 30 4C 8D 85 10 02 00 00 33 FF 41 39 7D 20 4C 8D B0 A0 00 00 00 40 0F 95 C7 48 8D B0 94 00 00 00 49 8B D6 48 8B CE E8 ?? ?? ?? ??` |
| Latch call in the screen-views loop (call at 0x1C7576D) | 0x1C75750 | `48 89 44 24 48 41 8B 45 18 41 2B 45 10 89 44 24 50 41 8B 45 1C 41 2B 45 14 89 44 24 54 E8 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 83 78 08 00 75 0A` |
| World-views pass | 0x1C75CF0 | `4C 89 44 24 18 55 41 55 41 56 41 57 48 83 EC 28 48 8B E9 45 8B E9 48 8B 0D ?? ?? ?? ?? 4D 8B F8 4C 8B F2 E8 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 83 78 08 00 0F 85 ?? ?? ?? ?? 8B 85 28 15 00 00` |
| Render-frame job | 0x1CB9EE0 | `48 89 5C 24 10 57 B8 70 38 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 60 38 00 00 48 8B F9 33 D2 48 8D 4C 24 20 E8 ?? ?? ?? ?? 48 8B 1F 80 7B 08 00` |
| Build screen views | 0x17E8740 | `4C 8B DC 49 89 5B 20 55 56 57 41 56 41 57 49 8D AB E8 FE FF FF 48 81 EC F0 01 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 80 00 00 00 8B 81 64 2A 00 00 48 8B F9 4C 8B B1 50 2A 00 00` |
| Layout selection | 0x17E8480 | `48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B E9 E8 ?? ?? ?? ?? 84 C0 74 65 80 3D ?? ?? ?? ?? 00 74 39 48 8D 1D ?? ?? ?? ?? 48 8D 3D ?? ?? ?? ?? 66 0F 1F 84 00 00 00 00 00` |
| `RenderViewForIndex` | 0x18E6C00 | `48 83 EC 28 85 D2 78 1B 3B 91 C8 71 5E 00 7D 13 48 8B 81 C0 71 5E 00 48 63 D2 48 8B 04 D0 48 83 C4 28 C3` |
| Store previous matrices | 0x1CE2340 | `0F 10 81 40 94 02 00 8B 81 6C 8A 02 00 0F 10 89 50 94 02 00 0F 11 81 80 94 02 00 0F 10 81 60 94 02 00 0F 11 89 90 94 02 00 0F 10 89 70 94 02 00 0F 11 81 A0 94 02 00` |
| Per-view ping-pong | 0x1CFC050 | `48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 48 89 48 08 55 41 54 41 55 41 56 41 57 48 8D 68 C8 48 81 EC 10 01 00 00 0F 29 70 C8 49 8B F9 0F 29 78 B8 49 8B F0 44 0F 29 40 A8 4C 8B EA 44 0F 29 48 98 48 8B D9 44 0F 29 50 88 48 63 81 90 89 02 00 4C 69 E0 F8 0A 00 00` |

String anchors: `leftRightStereo`, `r_maxRenderViews`, `idRenderWorld::RenderViewForIndex: invalid index
%i [0, %i]`, `multiView_60Hz`, `r_debugInvert2ndView` (read only in 0x1C75290 and 0xFA6B80),
`r_umbraJobKickoff`.

## 11. Consequences for the plan

- T-069's "second entry must not advance per-frame state twice" is met by construction in the multi-view
  route: the engine advances frame state once per frame and view state once per `viewIndex`.
- "Temporal history per eye" is met for TAA by `r_maxRenderViews 2` plus distinct `viewIndex` values,
  with the jitter fix from section 7 item 3; the per-device-context history (AO, light scattering, Umbra
  occlusion buffer) is open question 2.
- "Umbra per eye" is met by the default `r_umbraJobKickoff 2`, which queries per screen view.
- "Projection without view-constant patching" holds for the world (section 6); hands and guns depend on
  E3.
- The same machinery is the concrete form of `EngineNativeStereo` (T-004): the dormant VR code already
  feeds per-eye poses and projections into it, but the second `idRenderView` it needs is never created.

## 12. Live results (2026-09-26, rig, OpenXR-Simulator, build 25216728)

Runs are under `<workspace>\runs\20260926-00*` and `20260926-01*` with the layer logs in
`<workspace>\tmp-vr\<run>-logs`. Crash addresses come from full dumps taken with procdump and
read with `tmp-vr\dumpinfo.py` (the dumps themselves, 10-15 GB each, were deleted after reading).

**Verdict: the engine's two-view path cannot render a second view in this build.** The renderer keeps
per-view state for exactly one render view per frame, in at least four places that are sized at compile
time, not by `r_maxRenderViews`. The multi-view scaffold of sections 3 and 5 is real code, but the storage
behind it was built for one view. [live-verified]

| # | Result | Evidence |
|---|---|---|
| E1 | `+vr_dummyDevice 1 +vr_enable 1`, with and without `+r_maxRenderViews 2`: no crash in 75 s in the e1m1 intro, no `RenderViewForIndex: invalid index` line. The VR path did not visibly engage. `r_maxRenderViews` read back from memory stays 1 with `+r_maxRenderViews 2` and with `+set r_maxRenderViews 2` (an INIT cvar the command line does not reach), so these launches never had a second view to crash on. | runs `20260926-002131-e1a`, `-002338-e1b`; dump read of the cvar (`tmp-vr\dumpread.py`) |
| E2 | **Explicit projection honoured.** The per-eye hook (0x1C754BC) wrote the Quest 3 left eye's asymmetric matrix (m[0] 0.9027, m[2] -0.2425, m[5] 0.8492, m[6] -0.1805) and the latch built exactly that matrix (0 of 2870 latches differed). The engine's matrix convention is GL-like (view space x right, y up, -z forward: 0x39A490), m[2] = (r + l) / (r - l); unit tests reproduce the engine's builder and check that a point lands at the same place through the engine's view and the OpenXR eye. | run `20260926-003948-e2`, log `tmp-vr\ns1-logs` |
| E3 | Inconclusive. With a 40-degree weapon FOV forced, the gun was not in view at the captured moments with `inhibitModelFovScale` 0 or 1, and the late capture picked the simulator's window. Moot while there is no second view. | runs `20260926-0132*-e3b-*` |
| E4 | Second view with `viewIndex` 1: RenderViewForIndex(1) returns null (as expected); with a second render view injected the world views pass and the screen loop run, the latch builds both eyes' matrices, then the Umbra request of view 1 crashes (0x1D18BE9 via 0x1D19280): the world's visibility contexts are allocated per `r_maxRenderViews` (0x1D16F90) and there is one. Raising `r_maxRenderViews` in memory before the renderer starts overflows the renderer's **static per-view block** (0x66EF4F0, one 0xAF8 entry; the next global starts at +0xAF8; crash in its init loop 0x1CFD310). Moving that block (14 RIP-relative references, all repointed) gets further, then the **device context's view slot** overflows: `idDeviceContext` keeps its per-view slots inline at +0x8 with stride 0xA8, and other members start at +0xB0 (crash freeing slot 1's "pointers" in 0x1C1CC40). Relocating that is not practical: about 30 renderer sites index it as `backend + index * 0xA8 + field`. | runs `20260926-004155-e4` to `-005409-e4e` |
| E4 (slot 0 shared) | Both eyes through view slot 0 (the second render view is filed under `viewIndex` 0 after its latch, the world's own view list untouched, RenderViewForIndex(1) served through the world vtable): per-eye poses and projections latch correctly for both views; each eye needs its own visibility context (two queries on one context crash inside Umbra at 0x2277B71, also with `r_umbraJobKickoff 0/3` and `r_useUmbraCulling 0`); with a second visibility context (the count raised at 0x1D17151 while a map loads) the render thread crashes at 0x1C5CD4B: its per-frame job setup (0x1CDCD90) copies the render-list entries into a **one-element local array followed by the view count** (`mov [rbp + rdx*8 - 0x21], rcx`), so the second entry overwrites the count. The per-view render contexts behind it (`param + 0x2F8 + viewIndex * 8`, 0x1C5DFD0) also exist once, so two views on slot 0 would share one render context across parallel jobs. | runs `20260926-010016-e4f` to `-012239-e4j`, dump analysis of `ns11` |
| E5 | Not reached (needs two rendered views). Jitter: `subSampleIndex / upsamplerSubSampleIndex` of screen view 0 observed cycling (e.g. 30/31, 31/31, 0/0, 1/0); the copy to view 1 is implemented and unit-tested as a rule. | E2 log |
| E6 | Only as part of E4: `r_umbraJobKickoff 3`, `r_umbraJobKickoff 0` and `r_useUmbraCulling 0` did not stop the per-view visibility job (0x1D15C90) from running both views on one context. | runs `-010314-e6a`, `-010602-e6b`, `-010914-e6c` |
| E7 | The render frame counter (renderSystem + 0x10) and the backend counter advance once per frame: 1432 render frames, 1432 backend frames and 1432 eye latches in 10 s with one view. With two views the engine never completed a frame. | E4 logs (`stereo: eye views ... last 10 s`) |
| E8 | Not reached. | |

What the static reading got right: the latch honours `explicitProjectionMatrix`; the per-eye hook point and
its registers; one latch, one Umbra request and one render-list entry per screen view; the layout pointer
at frameBuilder + 0x2A70 (read at 0x17E88A1 after the world is known, 0x17E879D). What it missed: per-view
storage beyond the 0xAF8 block is not sized by `r_maxRenderViews` (the device context's slot, the render
thread's job descriptor, the per-view render contexts), and the cvar itself cannot be set from the command
line.

Consequence for T-004 / T-066 / D-032: `EngineNativeStereo` is not available on build 25216728 without
rebuilding the renderer's per-view storage (a larger device context, a second per-view render context and
a rewritten job setup), which is a re-implementation, not a hook. The remaining single-frame routes are
`MultiviewTransform` (shader-side per-eye transform, T-004 default) and synchronized sequential re-entry
(section 1: every per-frame state listed in section 4 saved and restored around a second render-frame run).
