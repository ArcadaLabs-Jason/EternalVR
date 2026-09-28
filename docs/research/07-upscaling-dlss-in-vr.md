# 07 - Upscaling in stereo VR: DLSS (incl. 4.5 and 5), FSR, XeSS, and updating DLSS

Status: research notes, 2026-09-25. Tags: **[C]** = confirmed against a primary source, SDK file or
header we read; **[U]** = unverified, inferred, rumour, or needs a capture/test on DOOM Eternal before
we rely on it. Local copies are listed in `reference/upscaling/MANIFEST.part.md`; source trees are in
`reference/_cache/` (re-fetch with `reference/upscaling/fetch.sh`).

## 1. Summary

- DOOM Eternal already has a complete DLSS integration on the NGX **Vulkan** API (June 2021 update),
  with CVARs for mode (`r_dlssQuality`, including Ultra Performance), sharpness and, importantly for us,
  **`r_dlssForceReset`** ("Reset DLSS history for N frames"). The shipped DLL is reported as 2.1.66 and
  swaps to 3.x work. [C for CVARs and swaps, U for the exact shipped version] (Correction, read on the
  rig 2026-09-25: the installed build ships `nvngx_dlss.dll` 2.3.0 in the game root and in
  `doomSandBox\`, with different hashes; T-103.)
- NVIDIA's own guide says stereo is done with **one DLSS instance per view** (section 3.17). The Vulkan
  resource wrapper carries a `VkImageView` plus `VkImageSubresourceRange`, so a per-layer 2D view of
  our 2-layer arrays is the natural input. [C for the API; U that NGX honours `baseArrayLayer` on an
  array image; test on the rig]
- **Recommended architecture:** a vendor-neutral `Upscaler` interface in our layer, fed from one
  **capture point**: the game's own NGX evaluate call. Our layer hooks `NVSDK_NGX_VULKAN_*` in
  `nvngx_dlss.dll` and calls it **twice per frame, once per eye**, with two feature handles and
  per-eye views. The same captured inputs (color, depth, motion vectors, jitter, reset, MV scale) feed
  the other backends: FSR 3.1 (MIT, Vulkan), XeSS (DP4a, Vulkan), FSR 4 through our D3D12 OpenXR device
  (FSR 4 has no Vulkan path), and FSR1 spatial as the always-works fallback.
- **DLSS update:** do not redistribute `nvngx_dlss.dll` in v1. Offer (1) the NVIDIA App global DLSS
  override, (2) a launcher "swap in a DLL you already have" with backup/restore, and (3) preset forcing
  (K or M) through our CreateFeature hook. Transformer presets need a 310.x DLL.
- **DLSS 5** is not an upscaler. It is a generative relighting pass (released 2026-09-03; officially RTX 50,
  modded onto RTX 30/40 and confirmed running on our RTX 4080 rig; roughly half the frame rate on flat
  screens). Nobody has shown it to be consistent between the two eyes. Decision: DLSS 4.x is the
  baseline; DLSS 5 is a stretch goal behind the same upscaler interface. **Frame generation**
  (DLSS-G, FSR-FG, XeSS-FG) does not apply to VR; the runtime's reprojection already does that job.
- AMD users get: FSR 3.1 per eye on every GPU (RDNA 1-4); FSR 4.1 on RDNA 3 and RDNA 4 through the
  D3D12 side (official since the June 2026 SDK 2.3); RDNA 2 gets FSR 3.1 or XeSS DP4a until AMD's
  announced early-2027 FSR 4.1 port.

## 2. DOOM Eternal's DLSS integration

**What ships.** Ray-traced reflections and DLSS arrived in the June 2021 "RTX" update; NVIDIA claims
"up to 60 % at 4K" [C: NVIDIA GeForce news]. DLL collections list 2.1.66 for the game [U: read the
file version on the rig; read on 2026-09-25: 2.3.0, T-103]. The game's last patch is 6.66, anti-tamper is gone, and Steam users report
running DLSS 3.5 and 3.7 DLLs plus DLSSTweaks forced DLAA without problems [C: Steam thread
4358996214436851253]. DLSS Enabler lists DOOM Eternal as supported (it fakes NGX on GPUs without
DLSS) [C: search snippet of Nexus site mod 757; U: details].

**API path.** DOOM Eternal is Vulkan-only, so it uses the NGX Vulkan entry points. The NGX loader is
statically linked into the game executable and reaches `nvngx_dlss.dll` through the driver's `nvngx.dll`
[U: standard NGX structure, guide section 5.1; confirm with a module list]. Not Streamline: the game
predates Vulkan Streamline and no `sl.*.dll` ships [U: check the install]. The runtime DLL exports
`NVSDK_NGX_VULKAN_CreateFeature`, `..._CreateFeature1`, `..._EvaluateFeature`, `..._ReleaseFeature`,
`..._Init*` [C: exports of 310.9.1 from the DLSS SDK repo; U: same names in 2.1.66, highly likely].

**CVARs** (community full dump, `reference/upscaling/doom-eternal-dlss-integration-notes.md`) [C]:

| CVAR | Meaning |
|---|---|
| `r_antialiasing` | 0 off, 1 "Temporal SSAA (32x)", 2 DLSS |
| `r_dlssQuality` | 0 Ultra Performance, 1 Performance, 2 Balanced, 3 Quality |
| `r_dlssSharpness` | 0-1; the DLSS 2.x sharpening parameter (ignored from DLSS 2.5.1 on) |
| `r_dlssTextureLodBias` | DLSS texture LOD epsilon bias |
| `r_dlssForceReset` | 0 off, -1 force reset, N = reset history for N frames |
| `r_jitter`, `r_TAANumSubSamples` (32) | Projection jitter on/off, TAA sequence length |
| `ui_settings_dummy_dlss_enabled` | Shows the DLSS option when requirements are not met |

**Inputs (to capture on the rig).** The DLSS guide requires color (render res), depth, motion vectors
(normally render-res RG16F, pixel units, pointing to the previous frame), jitter offset in render
pixels, optional exposure texture or auto-exposure, reset flag, MV scale, and create-time flags
`IsHDR / MVLowRes / MVJittered / DepthInverted / AutoExposure` [C: guide 3.6-3.9, 5.3]. What DOOM
passes for each is unknown [U]. The game already has everything TAA needs (32-sample jitter, velocity
buffer, history), so the DLSS inputs are the TAA inputs [U, strongly implied by the CVARs].

**Known interplay.** Dynamic resolution turns off under DLSS [C: user reports]. RT reflections have
their own temporal upscaler and history (`r_raytracedReflectionsTemporalUpscale*`), separate from
DLSS [C: CVAR names]. With a 310.x DLL the sharpness slider is dead and `r_sharpening` (a post pass)
is the only sharpening [U for DOOM specifically]. No published DOOM-specific DLSS artifact analysis
was found; users mention ghosting on some objects with the 2021 CNN model [U].

## 3. What DLSS needs in stereo

### 3.1 One instance per eye, and how to address a layer

- DLSS guide 3.17: "DLSS natively supports multiple views, including virtual reality (VR) by creating
  multiple DLSS instances with one associated to each view." Side-by-side targets use input/output
  sub-rectangles (`InEnableOutputSubrects`). [C]
- Streamline does the same with viewport IDs: `slDLSSSetOptions(viewportId, ...)`,
  `slAllocateResources(kFeatureDLSS, viewportId)` (ProgrammingGuideDLSS section 8.0). [C]
- NGX Vulkan resources are `NVSDK_NGX_ImageViewInfo_VK { ImageView, Image, SubresourceRange, Format,
  Width, Height }` [C: `nvsdk_ngx_defs_vk.h:53`]. For our 2-layer arrays we create a
  `VK_IMAGE_VIEW_TYPE_2D` view with `baseArrayLayer = eye, layerCount = 1` and pass the matching range.
  XeSS documents exactly this ("subresource range ... should point to the image subresource (level and
  layer) ... Only 2D view types are supported") [C]. NVIDIA does not say it in the guide, so treat
  per-layer NGX input as [U] until a RenderDoc capture shows NGX sampling the right layer. The fallback
  is a copy of each eye into per-eye 2D images (4 small copies per eye per frame at render res).
- FSR 2 / 3.1 Vulkan backends create their own views with `baseArrayLayer = 0` and
  `VK_REMAINING_ARRAY_LAYERS`, and barriers over all layers [C: `FidelityFX-SDK-v1.1.4/sdk/src/backends/vk/ffx_vk.cpp`
  ~1208, `RegisterResourceVK` ~2600-2911]. Per-layer FSR needs either per-eye copies or a small patch
  (the backend is MIT, so patching is allowed).

**Stereo in one engine frame is the good case.** Every VR precedent that runs DLSS in "native" stereo
(one engine frame, per-view state) reports it working: UEVR's Native Stereo mode ("DLSS/FSR2 usually
work completely fine with no ghosting"), REFramework's per-eye "temporal fix" for RE Engine TAA, and the
SDK's own guidance [C]. The alternate-eye approaches (R.E.A.L., AER renderers) needed special fixes
because each eye's history is two frames old. Our single-frame multiview design is in the good class.

### 3.2 Jitter

- DLSS wants a Halton pattern with `8 * (output/render)^2` phases (e.g. 32 for Performance, ~18 for
  Quality) [C: guide 3.7.1.1]. The game uses its own 32-sample TSSAA sequence; its DLSS mode may switch
  pattern [U].
- In our design the game computes one jitter per frame. Both eyes get the same pixel-space jitter,
  which each eye's DLSS instance accumulates independently. That is correct as long as the jitter is
  *added* to each eye's asymmetric projection (`P_e[2][0] += 2*jx/W`, `P_e[2][1] += 2*jy/H` in the
  matching convention) rather than overwriting the off-center terms the headset FOV needs [C: math;
  U: where DOOM applies jitter, projection matrix vs. viewport]. The jitter value passed to NGX is
  unchanged per eye.
- Decorrelated jitter per eye (for example, half a sequence apart) might raise perceived resolution
  but can create binocular shimmer. Start identical; A/B later [U].

### 3.3 Motion vectors

- Per-eye motion vectors must be computed with **per-eye current and previous view-projection**,
  where the view includes the HMD pose: `mv_e = ndc(P_e,prev V_e,prev X_prev) - ndc(P_e V_e X)`. Head
  motion is then part of the camera motion and DLSS handles it like mouse look. Using the center-eye
  previous matrix for both eyes produces smearing that grows with IPD and near geometry [U: inferred;
  consistent with UE 5.x VR smearing reports]. Our stereo rewrite already needs per-eye previous
  matrices for TAA (see 02, "Temporal" row); DLSS reuses them.
- The pose used for rendering must be the pose submitted in the projection layer. If we ever re-predict
  or late-latch the pose after the velocity pass, the MVs no longer match the image [U].
- Runtime reprojection (ASW, SteamVR motion smoothing, VD's SSW) operates after submission, on the
  finished eye images; it does not feed back into DLSS history, so it needs nothing from us beyond
  optional depth submission [C: 01, section 1.7].
- Asymmetric and canted FOV: DLSS works in pixel space, so asymmetric frusta are fine. Per-eye
  aspect ratio must stay the same between render and output size [C: guide 3.2.2.1]. Canted displays
  (Pimax) with "parallel projections" off need nothing special beyond per-eye matrices [U].

### 3.4 History reset

DLSS `InReset` (and the game's `r_dlssForceReset`) must be raised on the first frame after anything
that invalidates history [C: guide 3.13]. For us:

| Event | Why | How we detect it |
|---|---|---|
| Level load, checkpoint reload, death respawn | New scene | Loading screen / game state hooks; the game likely resets itself [U] |
| Cinematic cuts, cutscene start/end | Camera jump | Game resets already [U]; we also switch to cinema-screen mode |
| Glory-kill start/end camera snaps | Camera and FOV jump in one frame | Glory-kill state hook; per-eye matrix delta over threshold |
| Slayer-gate / portal / scripted teleports | Camera translation jump | Per-eye view delta over threshold (e.g. > 0.5 m or > 20 deg in one frame) |
| Snap turn, recenter, tracking loss/recovery | Large head-space rotation outside what MVs can reproject | Our own input code knows; plus the delta rule |
| Menu, pause, weapon wheel overlays that freeze the world | Stale history on resume | Menu state |
| Render-size change (resolution setting, mode change) | Feature must be recreated | Our settings |

Reset **both** eyes together. Resetting one eye only gives a one-eye blur flash that is uncomfortable.

### 3.5 Known VR artifacts and presets

- Reported in VR: ghosting on instruments and thin moving detail with CNN presets (DCS used preset C;
  users forced J/K) [C: ED forums]; glass-cockpit smearing in MSFS reduced a lot by preset M [C,
  desktop article]; UE 5.5/5.6 instanced-stereo DLSS bugs (black right eye; smearing blamed on TSR
  path) [C: NVIDIA forums]; Unity HDRP FSR2 with a shifted "ghost" in one eye (shared state across
  eyes) while DLSS and XeSS were fine [C: Unity forum].
- Sharpening: DLSS 2.5.1+ has none; in VR over-sharpening shimmers badly under head motion. If needed,
  a mild RCAS/CAS per eye after upscaling, off by default [U: comfort judgement].
- HUD: our HUD goes to separate quad layers (topic 04), so it never enters DLSS history. Anything the
  game draws into the scene before DLSS (weapon, particles) is fine if its MVs are right [U].
- Recommended presets for VR, given the 310.6 guide [C for descriptions, U for VR ranking]: preset
  **K** for DLAA / Quality / Balanced (cheapest transformer), preset **M** for Performance on RTX 40/50
  (better stability, FP8). Avoid **L** / Ultra Performance: 1/9 of the pixels per eye looks soft at
  headset viewing distance and L is the most expensive model. On RTX 20/30 stay on K.

## 4. The options for our design

### 4.1 Option A: hook the game's DLSS and run it per eye (recommended for NVIDIA)

1. When `nvngx_dlss.dll` loads (hook `LoadLibraryExW` or check modules on first `vkCreateDevice`),
   inline-hook its `NVSDK_NGX_VULKAN_CreateFeature(1)`, `..._EvaluateFeature`, `..._ReleaseFeature`
   exports (safetyhook/MinHook are already in `reference/_cache`). DLSSTweaks uses the same load path
   and hooks, and is MIT [C: `DLSSTweaks/src/DllMain.cpp`, `ProxyNvngx.cpp`].
2. **Create:** call the original twice (left, right); store the second handle keyed by the first. Apply
   our preset override (`DLSS.Hint.Render.Preset.*`) and optional forced DLAA (render = output) on the
   parameter map, exactly as DLSSTweaks does.
3. **Evaluate:** read the parameter map (Color, Output, Depth, MotionVectors, jitter, Reset, MV scale,
   exposure). Map each `VkImage` to our promoted 2-layer image, substitute per-layer views for eye 0,
   call the original with handle 0; substitute eye-1 views, call with handle 1. Restore the map. OR in
   our own reset flag.
4. **Keep NGX's own GPU work out of our rewriter.** NGX records compute dispatches into the game's
   command buffer and creates its own pipelines. If NGX's Vulkan calls come through our layer, a
   thread-local "inside NGX" flag set around the original Create/Evaluate must make every intercept
   pass-through (no SPIR-V rewrite, no view promotion, no barrier translation) [U: whether NGX calls
   route through the layer depends on which `vkGetDeviceProcAddr` it was given; log it].
5. Output: the game's DLSS "display" resolution is our per-eye swapchain size; the output image is
   itself a promoted 2-layer target, so each eye writes its own layer.

Pros: no game RVAs; NVIDIA-quality reconstruction with the game's own well-tested inputs; DLL
upgrades and presets work unchanged; smallest amount of our code in the NVIDIA-licensed area (we link
nothing from NVIDIA; we only need a handful of parameter names and struct layouts). Cons: NVIDIA-only;
depends on DLSS mode being selectable (RTX GPU); per-layer view behaviour and NGX re-entrancy to verify.

### 4.2 Option B: disable the game's DLSS and integrate DLSS ourselves

Our own NGX init on the game's device (static `nvsdk_ngx_s.lib`) or Streamline, with inputs pulled
from the TAA pass (r_antialiasing 1) and the TAA resolve replaced. This means identifying the TAA
resolve dispatch, driving jitter into the game's projection ourselves, and suppressing the game's TAA
upsample. It is what option A gets for free. Licensing is also heavier: we would link NVIDIA object
code and ship it (see 7). Only worth it if option A's hooks fail.

### 4.3 Option C: FSR 3.1 / FSR 4 / XeSS, using the same capture point

The captured per-eye inputs are backend-neutral. On AMD/Intel the game will not offer DLSS mode by
itself, so we need the capture point to exist there too:

- **C1 (preferred): NGX shim.** Our layer answers the game's NGX calls when no NVIDIA GPU is present
  (fake capability query, create returns our handle, evaluate runs FSR/XeSS per eye), plus
  `ui_settings_dummy_dlss_enabled 1`. This is what OptiScaler / DLSS Enabler do, and DOOM Eternal is
  on DLSS Enabler's list [C: OptiScaler README; U: which checks DOOM performs before enabling DLSS].
  OptiScaler is GPL-3.0: study only, write our own.

  **Correction (2026-09-25, design QA round 4):** the citation above is wrong. OptiScaler's README has
  no DOOM Eternal entry; the DLSS Enabler claim rests only on the search snippet cited in section 2.
  OptiScaler's own notes say the opposite of what the bullet implies: its Vulkan vendor spoof,
  including driver vendor, name and info, does not make DOOM Eternal enable DLSS
  (`reference/_cache/OptiScaler/Features.md` line 13, `Changelog.md` lines 408 and 453), and Goghor's
  DLSS Unlocker mod is the only known way (`Spoofing.md` line 68). The design now patches the game's
  own DLSS gate (DECISIONS T-072).
- **C2: TAA-pass replacement** (as in option B) if the shim proves brittle.
- Backends: FSR 3.1.4 from FidelityFX SDK 1.1.4 (MIT source, Vulkan backend, works on any GPU incl.
  NVIDIA and Intel); XeSS-SR 3 (Vulkan 1.1 + `shaderIntegerDotProduct`/int8 DP4a path on all vendors,
  XMX on Arc; binary-only DLL under Intel's simplified license, redistributable unmodified) [C];
  FSR 4.x only on D3D12 (see 4.4).

### 4.4 Option D: run the upscaler on our D3D12 OpenXR device

Topic 01 recommends considering an OpenXR session on D3D12 with Vulkan external-memory interop
(BotW-BetterVR pattern). If we take that route, the D3D12 side can host upscalers that do not exist on
Vulkan: **FSR 4.1** (SDK 2.3 is DX12-only; "Vulkan is currently not supported in SDK") and XeSS DX12.
Cost: share depth + MVs + color per eye (we already share color), plus a cross-API timeline fence
before the upscale. Output goes straight into the D3D12 XR swapchain, which even saves a copy.
OptiScaler already reaches FSR 4 from Vulkan games through a background D3D12 device, so the pattern is
proven [C: OptiScaler README "FSR 4.X (via FSR 3.X/4 w/Dx12 interop)"].

### 4.5 FSR1 / NIS spatial (last resort, always available)

FSR1 EASU+RCAS per eye is engine-agnostic, and AMD's source is MIT licensed (keep the notice) [C:
AMD FidelityFX FSR 1.0]. No history, no MVs, no reset logic, cheapest. Softer and shimmers more than temporal methods;
with the game's own TSSAA running at reduced render scale it is acceptable.

### 4.6 Comparison

| | Quality in VR | Integration complexity | Vendor coverage | Main risks |
|---|---|---|---|---|
| **A** Hook game DLSS, per eye | Best on NVIDIA (transformer K/M) | Medium: 3 export hooks, per-layer views, NGX pass-through flag | RTX 20+ only | NGX per-layer view support; NGX calls re-entering our layer; game's DLSS inputs correct after stereo rewrite |
| **B** Own DLSS integration | Same as A | High: own jitter, TAA pass replacement, NGX init | RTX 20+ only | Game coupling (RVAs/pass IDs); NVIDIA license for linked lib |
| **C1** NGX shim -> FSR 3.1 / XeSS | Good (FSR 3.1 below DLSS on disocclusion/particles; XeSS DP4a between) | Medium-high: shim + backends | All vendors | DOOM's DLSS-availability checks; FSR VK backend layer-0 assumption (patch or copy) |
| **D** Upscale on D3D12 side (FSR 4 / XeSS / DLSS D3D12) | Best available on AMD RDNA 3/4 | High: extra shared resources + fences | FSR 4: RDNA 3/4 (RDNA 2 early 2027) | Only if we pick the D3D12 OpenXR path; sync latency |
| **FSR1/NIS** spatial | Adequate | Low (AMD MIT source) | All | Soft; shimmer |

## 5. Latest state (September 2026): DLSS 4, 4.5, 5 and friends

- **DLSS 4 (Jan 2025)**: first transformer Super Resolution model, presets J and K; K is the default for
  DLAA/Quality/Balanced [C: guide 310.2+]. Presets A-E deprecated (310.3) and A-D removed (310.6) [C].
- **DLSS 4.5 (CES 2026; SDK 310.5.0 on 2025-11-20; public 2026-01-14)**: second-generation transformer,
  presets **L** (Ultra Performance) and **M** (Performance, and "Recommended" in the NVIDIA App),
  FP8-accelerated, "peak performant on RTX 40 series GPUs and above"; RTX 20/30 pay more [C: guide,
  NVIDIA news]. Exposure input is honoured only by J/K; L always auto-exposes [C]. Also Dynamic Multi
  Frame Generation up to 6x (flat screen only).
- **NVIDIA App overrides**: per-game "DLSS Override - Model Preset", and since driver 581.08
  (2025-08-19) a global toggle for all compatible games [C]. Secondary sources say the global override
  covers DX11, DX12 and Vulkan [U: verify with DOOM Eternal].
- **DLSS 5 (GTC March 2026; leaked 2026-08-26; released 2026-09-03)**: "3D-guided neural rendering".
  Takes color and motion vectors and re-renders lighting and materials (faces, skin, hair, fabric). It
  is not upscaling; SR and FG stay "DLSS 4.5" [C: NVIDIA newsroom and GeForce news]. Integration via a
  new Streamline plugin `sl.dlss_nr` (SL 2.14.0) and a UE5 plugin [C: Streamline changelog]. Runtime
  DLL `nvngx_dlssnr.dll`; the leaked 310.8.0.0 build (~158 MB, ~148 M FP8 parameters) was found in an
  NBA 2K27 early build and modders had it in Control (RenoDX), Skyrim (PureDark), FF7 Rebirth within
  hours; a modder patched it onto RTX 40 [C: multiple outlets, same facts]. Official support is RTX 50
  only; NVIDIA says RTX 40 comes later, no date [C: TechPowerUp, Notebookcheck]. Community patches run it on
  RTX 30 and 40; it runs on the project's RTX 4080 rig [C: owner-tested]. Cost: about 45-60 %
  frame rate loss on flat screens; HotHardware measured ~240 -> ~69 fps on a 5070 Ti at 1440p [C].
  - **VR:** NVIDIA says nothing about VR or stereo [C: absence in all official texts]. Community ports
    exist (`eregnier/dlss5-vr` for R.E.A.L. games, `eregnier/uevr-dlss5` for UEVR, both ~3 stars,
    OptiScaler-based "pre-SR" pass at 0.66-0.75x). Their claimed 0.7-1.7 ms per frame does not match
    the flat-screen measurements [U: treat as unverified]. CompoundVR's take: DLSS 5's cost is meant to
    be bought back by multi-frame generation, which VR cannot use [C: article]. Our own concern: a
    generative model run separately per eye has no stereo-consistency constraint, so the eyes may get
    different invented detail [U: nobody has published a test]. It would also be an art-direction
    change on a game with a strong look. **Verdict: stretch goal, not v1 baseline. Some players will want it, so the
    upscaler interface keeps a slot for it; first test is stereo consistency between the eyes.** It would slot in as an optional per-eye
    pass after upscaling via Streamline `sl.dlss_nr` if ever wanted.
- **NVIDIA VR-specific DLSS features in 2025-2026:** none found. The SDK's multi-view section (3.17)
  has been unchanged since 2.1.5; there is no stereo-aware or foveated DLSS mode, no multiview array
  input, no OpenXR integration [C: guide revision history; U: absence of evidence].
- **Recent VR titles/mods shipping upscalers:** R.E.A.L. v19 (April 2025) ships DLSS SR and stereo Ray
  Reconstruction for Cyberpunk, Hogwarts, Indiana Jones, Star Wars Outlaws on AER [C]; UEVR relies on
  the game's DLSS/FSR in Native Stereo [C]; PureDark's Skyrim VR upscaler (DLSS/FSR2/XeSS) [C]; MSFS
  2024 and DCS use native DLSS in VR with transformer overrides [C].
- **AMD FSR:** FSR 3.1 decoupled upscaler from frame gen and ships as a replaceable signed DLL; FSR SDK
  1.1.4 (MIT) is the last with a Vulkan backend [C]. FSR 4 (ML) launched on RDNA 4; AMD's FSR SDK
  2.3.0 "Redstone" (June 2026) adds FSR 4.1.1 on RDNA 3 dGPUs; RDNA 2 support announced for early
  2027; SDK is DX12-only [C: SDK README / what's new; U: RDNA 2 date is AMD's announcement via press].
  An accidental AMD source push in Aug 2025 exposed an INT8 FSR 4 that OptiScaler runs on RDNA 2/3
  (10-20 % extra cost reported) [C: press]. OptiScaler reaches FSR 4 in Vulkan games through a D3D12
  bridge [C].
- **Intel XeSS 3**: XeSS-SR on all GPUs with DP4a (SM 6.4 / Vulkan int dot product), XMX path on Arc;
  Vulkan 1.1 supported; per-layer image views documented [C].

## 6. Frame generation in VR: why not

DLSS-G, FSR-FG and XeSS-FG interpolate between two rendered frames. That needs the *next* frame
before showing the generated one, adding at least one frame of latency, and the generated frame uses
the old head pose. In VR, the runtime already synthesises frames (ASW, motion smoothing, SSW) from
the latest pose and depth, which is strictly better for comfort. DLSS-G is also bound to the
swapchain present path, while we submit through OpenXR, so there is nothing for it to attach to.
UEVR tells users to disable DLSS-G; Luke Ross says FG in VR needs driver changes [C]. If a PC
runtime ever advertises `XR_EXT_frame_synthesis`, giving it DOOM's motion vectors is the correct
"frame generation" for VR (01, section 6) [C: extension exists; no PC runtime yet].

## 7. Updating DLSS: what the launcher should do

**Compatibility.** DLSS 2.x games generally accept 3.x and 310.x DLLs (same NGX ABI); DLSS Swapper
and DLSSTweaks rely on it, and DOOM Eternal users run 3.5/3.7 [C]. With 310.x the old sharpening and
presets A-D are gone; default models become K (Quality/Balanced) and M (Performance) [C: guide 3.12].
There is no file-integrity check to fight (anti-tamper removed) [C: user reports].

**Licensing.** The NVIDIA RTX SDKs license lets us distribute SDK files "as incorporated in object
code format into a software application" with "material additional functionality", not "as a
stand-alone product"; requires terms "at least as protective" for the distributed parts, NVIDIA marks
in credits/about for DLSS/NGX users, notification to NVIDIA before a *commercial* release, NVIDIA-GPU
only use, and forbids anything that would put the SDK under an open-source license [C:
`reference/upscaling/docs/DLSS-LICENSE.md`, sections 1c, 2, 4b, 4e, supplement 1, 4, 7.1b]. The NGX
headers are marked `LicenseRef-NvidiaProprietary` even inside MIT Streamline [C]. Bundling or having
the launcher download `nvngx_dlss.dll` from a third-party mirror is at best a grey area for an MIT
project [U: not legal advice]. Option A needs no NVIDIA binaries or libraries from us at all.

**Recommendation.**
1. **Default: no DLL shipped.** Detect the installed `nvngx_dlss.dll` version and show it.
2. **NVIDIA App path (preferred for users):** if the NVIDIA App global override applies to DOOM
   Eternal, it upgrades the model driver-side with no file changes. Document it; test it on the rig.
3. **"Use a DLSS DLL I already have":** the launcher copies a user-chosen DLL in, renames the original
   to `nvngx_dlss.dll.eternalvr-backup` (never delete), validates the file version and NVIDIA
   signature, and restores on uninstall or on request. Point users at DLSS Swapper or NVIDIA's own
   SDK release page for the file.
4. **Preset control through our hook:** Auto / K / M (and DLAA toggle), applied at CreateFeature.
   Only offered when a 310.x DLL is detected; with the game's own DLL (2.3.0 on the rig, T-103) the CNN model is the only choice.
5. Revisit bundling only after reading the license with someone qualified, keeping the DLL in a
   separately licensed download that carries NVIDIA's terms, not ours.

## 8. Performance budget (RTX 4080, 90 Hz)

Budget: 11.1 ms per stereo frame. Assume ~2000x2100 per eye at 100 % runtime scale (~4.2 Mpx per eye,
8.4 Mpx both), comparable to one 4K frame (8.3 Mpx) [U: varies by headset].

| Item | Estimate |
|---|---|
| DLSS per eye at ~2000x2100 output, RTX 4080 (NVIDIA table interpolated between 1440p and 4K) | K ~0.9-1.0 ms, M ~1.1-1.3 ms [C table, U interpolation] |
| DLSS both eyes | K ~1.9 ms, M ~2.4 ms |
| FSR 3.1 per eye (RDNA 3 class) | ~0.6-1.0 ms [U] |
| FSR1 EASU+RCAS per eye | ~0.2 ms [U] |
| DLSS VRAM, two instances at ~4 Mpx | ~2 x 250 MB (K) to 2 x 370 MB (M) [C table, U interpolation] |

DLSS pays off only when it saves more than ~2 ms of shading. DOOM at native 4K on a 4080 runs well
above 90 fps without RT [U: typical benchmarks], so on the owner's rig the likely best image is **DLAA
(or Quality) with preset K**, not Performance. DLSS earns its keep with RT reflections on, at higher
runtime supersampling, on 120 Hz headsets, and on weaker GPUs. Measure before choosing defaults.

**Foveated upscaling ideas.** (a) Fixed-foveated VRS (`VK_KHR_fragment_shading_rate`) on the promoted
targets lowers peripheral shading while DLSS still sees a full-size image [U: DLSS quality with coarse
periphery]. (b) Quad views (`XR_VARJO_quad_views` / Quad-Views-Foveated) would need four DLSS instances
(two wide low-res, two inset) and four jitter/MV/reset sets; it multiplies the per-view overhead and is
not worth it until the stereo path is solid [U]. (c) Render at Performance-mode resolution and rely on
the lens: the periphery is blurred by optics anyway, so a lower-quality preset hurts less than on a
monitor [U].

## 9. Implications for our design

1. The stereo renderer must keep a table from every game `VkImage` to its promoted 2-layer image, and
   be able to produce per-layer 2D views on demand. DLSS, XeSS and FSR (patched) all consume those.
2. Per-eye previous-frame matrices are a hard requirement (TAA already needs them). Upscalers add no
   new matrix work.
3. One `Upscaler` interface: `create(eyeRenderExtent, eyeOutputExtent, flags)`,
   `evaluate(cmd, eye, color, depth, mv, output, jitter, mvScale, reset, exposure)`, `release()`.
   Backends: DLSS (game NGX, per-eye handles), FSR 3.1 (Vulkan, MIT source vendored with notice),
   XeSS (Vulkan, DLL), FSR 4 / XeSS DX12 (only on the D3D12 OpenXR path), FSR1 (AMD MIT source).
4. The capture point is the game's DLSS evaluate. On non-NVIDIA GPUs our NGX shim creates that
   capture point. The TAA-pass replacement is plan B.
5. A pass-through mode in every Vulkan intercept, keyed by a thread-local flag, so NGX/FSR/XeSS
   internal work is never rewritten.
6. A single history-reset source feeding `InReset` for both eyes, fed by game-state hooks and a
   per-eye camera-delta rule.
7. Launcher settings: Upscaler (Auto / DLSS / FSR 3.1 / FSR 4 / XeSS / FSR1 / Off), Mode (DLAA /
   Quality / Balanced / Performance), DLSS preset (Auto / K / M), sharpening (off by default), DLSS DLL
   version display and swap-with-backup. No frame generation option.
8. Default by vendor: NVIDIA RTX -> DLSS DLAA/Quality, preset K; AMD RDNA 3/4 -> FSR 4 if the D3D12
   path exists, else FSR 3.1; RDNA 2 and older, Intel -> FSR 3.1 or XeSS DP4a; anything that fails ->
   FSR1.
9. Keep all NVIDIA headers and binaries out of the MIT tree; fetch the DLSS SDK at build time and
   carry NVIDIA's attribution in the about box and THIRD_PARTY_NOTICES.

## 10. Open questions to verify on the gaming rig (RTX 4080, 9950X3D)

1. File version of the shipped `nvngx_dlss.dll`; does a 310.x DLL run in flat DOOM (all modes)?
2. Does the NVIDIA App global override change the model in DOOM Eternal (DLSS indicator via
   `OverrideDlssHud`-style registry key or the NGX debug overlay)?
3. RenderDoc capture of the DLSS evaluate: formats and sizes of color/depth/MV, create flags, exposure
   texture presence, jitter values and sequence length, where in the frame it runs (before/after tone
   map and bloom).
4. Do NGX's Vulkan calls pass through our layer (which `vkGetDeviceProcAddr` did the game give NGX)?
5. Does NGX sample the right layer when given a 2D view with `baseArrayLayer = 1`? If not, measure the
   copy fallback.
6. Is `r_dlssForceReset` usable at runtime from our console bridge, and does the game already reset on
   loads, cinematics and glory kills?
7. Which checks gate the DLSS option on non-NVIDIA GPUs (NGX init result, vendor ID, driver version)?
   Is `ui_settings_dummy_dlss_enabled` enough with an NGX shim?
8. Frame-time split in stereo with and without RT reflections: native vs DLAA vs Quality vs
   Performance, K vs M, per eye.
9. Visual A/B in the headset: identical vs decorrelated per-eye jitter; ghosting on the weapon and
   particles during fast head turns; shimmer on distant geometry; mild RCAS on/off.
10. Does DOOM's velocity buffer include the viewmodel correctly after our per-eye projection rewrite?

## Sources

Local copies (see `reference/upscaling/MANIFEST.part.md`):
- DLSS Programming Guide 310.6.0 (March 2026) excerpts: `reference/upscaling/docs/DLSS-Programming-Guide-310.6-excerpts.md`; full PDF in `reference/_cache/upscaling/`. https://github.com/NVIDIA/DLSS
- NVIDIA RTX SDKs license: `reference/upscaling/docs/DLSS-LICENSE.md`
- NGX Vulkan headers: `reference/_cache/DLSS/include/` (pointers in `reference/upscaling/ngx-vulkan-api-pointers.md`)
- Streamline README, DLSS guide, 2.14 changelog: `reference/upscaling/docs/Streamline-*.md`. https://github.com/NVIDIA-RTX/Streamline
- FSR 2.2, FidelityFX SDK 1.1.4 (FSR 3.1), FSR SDK 2.3.0: `reference/upscaling/docs/FSR*.md`, `FidelityFX-SDK-1.1.4-README.md`. https://github.com/GPUOpen-Effects/FidelityFX-FSR2, https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK
- XeSS 3 SDK README, license, SR guide: `reference/upscaling/docs/XeSS-*.md`. https://github.com/intel/xess
- DLSSTweaks README + ini: `reference/upscaling/docs/DLSSTweaks-README.md`. https://github.com/emoose/DLSSTweaks
- OptiScaler README: `reference/upscaling/docs/OptiScaler-README.md`. https://github.com/optiscaler/OptiScaler
- DLSS Swapper README: `reference/upscaling/docs/DLSS-Swapper-README.md`. https://github.com/beeradmoore/dlss-swapper
- DOOM Eternal CVARs: https://github.com/Official-KEX/doom-eternal-full-cvarlist (`reference/_cache/cvarlist/`)
- UEVR docs (Native Stereo and DLSS): `reference/_cache/uevr-docs/src/README.md`; REFramework VR temporal fix: `reference/_cache/reframework/src/mods/VR.cpp`

Web (fetched 2026-09-25; notes in `reference/upscaling/articles/`):
- NVIDIA, DOOM Eternal RTX update: https://www.nvidia.com/en-us/geforce/news/doom-eternal-ray-tracing-nvidia-dlss-upgrade-available-now/
- The FPS Review, DOOM Eternal Vulkan RT + DLSS: https://www.thefpsreview.com/2021/06/01/doom-eternal-getting-vulkan-ray-tracing-and-nvidia-dlss-update-this-month/
- Steam discussion, DLSS DLL swaps in DOOM Eternal: https://steamcommunity.com/app/782330/discussions/0/4358996214436851253/
- DLSS versions collection: https://github.com/BuyMyMojo/dlss-versions
- NVIDIA, DLSS 4.5 SR available now: https://www.nvidia.com/en-us/geforce/news/dlss-4-5-super-resolution-available-now/
- NVIDIA, DLSS 4.5 announcement: https://www.nvidia.com/en-us/geforce/news/dlss-4-5-dynamic-multi-frame-gen-6x-2nd-gen-transformer-super-res/
- NVIDIA, global DLSS overrides (581.08): https://www.nvidia.com/en-us/geforce/news/nvidia-app-update-5-geforce-game-ready-driver/
- NVIDIA Newsroom, DLSS 5: https://nvidianews.nvidia.com/news/nvidia-dlss-5-delivers-ai-powered-breakthrough-in-visual-fidelity-for-games
- NVIDIA, DLSS 5 in NBA 2K27: https://www.nvidia.com/en-us/geforce/news/dlss-5-3d-guided-neural-rendering/
- Club386, DLSS 5 release date: https://www.club386.com/nvidia-dlss-5-release-date/
- HotHardware, DLSS 5 tested: https://hothardware.com/news/nvidia-dlss-5-neural-rendering-tested
- Held Games, DLSS 5 leak explained: https://heldgames.com/guides/dlss-5-mod-explained
- XDA, DLSS 5 leak: https://www.xda-developers.com/nvidia-dlss-5-has-leaked-modders-are-already-creating-uncanny-valley-nightmares/
- OC3D, DLSS 5 on RTX 40 via patched DLL: https://overclock3d.net/news/gpu-displays/modder-add-dlss-5-support-to-rtx-40-series-gpus-using-leaked-dll-files/
- TechPowerUp, DLSS 5 for RTX 40 coming: https://www.techpowerup.com/352330/nvidia-confirms-dlss-5-for-geforce-rtx-40-series-is-coming
- CompoundVR, DLSS 5 and VR: http://compoundvr.com/articles/dlss-5-skyrim-vr/
- eregnier DLSS 5 VR ports: https://github.com/eregnier/dlss5-vr, https://github.com/eregnier/uevr-dlss5
- MIXED, R.E.A.L. v19 DLSS-RR in VR: https://mixed-news.com/en/real-vr-mod-dlss-ray-reconstruction/
- NVIDIA forums, UE 5.5 DLSS 4 VR bug: https://forums.developer.nvidia.com/t/critical-dlss-4-bug-in-ue-5-5-for-vr/325433
- NVIDIA forums, UE 5.6 DLSS VR smearing: https://forums.developer.nvidia.com/t/dlss-plugin-in-unreal-engine-5-6-causing-smearing-and-ghosting-in-vr/337856
- Unity forum, FSR2 one-eye ghosting: https://discussions.unity.com/t/hdrp-vr-fsr2-ghosting-on-one-eye/1568541
- ED forums, DCS DLSS presets: https://forum.dcs.world/topic/356743-update-the-dlss-preset-used-by-dcs-to-remove-ghosting/, https://forum.dcs.world/topic/368163-dlss-4/
- MSFS Addons, DLSS 4.5 cockpit smearing: https://msfsaddons.com/2026/01/17/how-to-finally-fix-msfs-2024-cockpit-smearing-with-dlss-4-5/
- PureDark Skyrim Upscaler (VR branch): https://github.com/PureDark/Skyrim-Upscaler/tree/VR
- GPUOpen, FSR SDK 2.3 / FSR 4.1 on RDNA 3: https://gpuopen.com/learn/amd-fsr-sdk-2-3-blog/
- HotHardware, FSR 4.1 for RDNA 3/2: https://hothardware.com/news/amd-bringing-fsr-41-to-rdna3-rdna2
- Tom's Hardware, FSR 4.1 on RX 7000: https://www.tomshardware.com/pc-components/gpu-drivers/amd-brings-official-fsr-4-1-support-to-rx-7000-series-gpus-int8-model-now-available-in-300-games-rdna-3-apus-also-getting-fsr-4-1-soon
- Wccftech, leaked FSR 4 INT8: https://wccftech.com/leaked-amd-fsr-4-int8-files-helps-users-turn-on-fsr-4-on-rx-7000-on-windows/
- VideoCardz, OptiScaler FSR 4 INT8 on RDNA 2: https://videocardz.com/newz/optiscaler-update-brings-fsr-4-int8-support-to-rdna-2-on-newer-radeon-drivers
- PC Gamer, OptiScaler FSR 4 in Vulkan games: https://www.pcgamer.com/hardware/graphics-cards/games-that-use-vulkan-can-now-use-fsr-4-upscaling-thanks-to-the-latest-optiscaler/
