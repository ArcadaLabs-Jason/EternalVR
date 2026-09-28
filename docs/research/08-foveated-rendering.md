# 08: Foveated rendering

Research notes, 2026-09-25. Scope: which foveation techniques fit EternalVR's layer-injected, single-frame multiview renderer; the 2025-2026 state of foveated DLSS (including the "DLSS 5" work); eye-tracking access on PC per runtime and headset; a phased architecture with a concrete Vulkan sketch; defaults per headset. Upscaling in general (DLSS/FSR/XeSS choice, per-eye contexts) is covered in `07-upscaling-dlss-in-vr.md`; this document only covers where foveation meets it.

Tags: **[C]** confirmed against a cited source (spec text, source code, vendor or project docs). **[U]** uncertain: vendor claim, community report, inference, or not yet measured by us.

Local references: `reference/foveation/` (spec appendices, READMEs, articles, `MANIFEST.part.md`, `fetch.sh`). Clones are in `reference/_cache/` and are recreated by `reference/foveation/fetch.sh`. Related material already pulled by other topics: `reference/vulkan/spec/*fragment_shading_rate*`, `reference/openxr/extensions/ext_eye_gaze_interaction.adoc`, `reference/_cache/vkspec.html`.

---

## 1. Summary

- **Fixed foveation is a first-class feature for every headset.** The full-rate region is centred per eye where a straight-ahead gaze lands (head-forward projected into each eye's asymmetric frustum), not on the image centre, and is wider than the eye-tracked region [C: §4].
- **Mechanism: `VK_KHR_fragment_shading_rate` with a 2-layer shading-rate attachment**, layer chosen by `ViewIndex` under multiview [C: spec]. It rides on the render-pass and pipeline recreation multiview already needs. NVIDIA Turing+, AMD RDNA2+, Intel Arc; about half of Windows devices on gpuinfo [C].
- **The engine already has VRS.** DOOM Eternal ships `r_VRSEnabled` ("Scarlett + NV Turing"), `r_VRSSobelTolerance`, `r_VRSDebug` [C: `reference/idtech7/typeinfo/kex-cvarlist-2024.tsv`], and Microsoft says Eternal "benefited heavily from hardware VRS in forward-rendered opaque/transparent passes" [C]. Whether the PC build's path works is the first rig capture [U].
- **Eye-tracked foveation** reads `XR_EXT_eye_gaze_interaction` from our own OpenXR session: SteamVR (Beyond 2e, PSVR2 via PSVR2 Toolkit, Steam Frame and Quest Pro via Steam Link), VDXR, Pimax Play, Varjo [C: §5].
- **"DLSS 5 with foveation" is real but not a speed-up by itself.** DLSS 5 (shipped 2026-09-03) is a neural *rendering* pass costing ~8 ms at 4K on an RTX 5090, officially RTX 50 only [C: heise]. Cheeky Foveated DLSS foveates DLSS Super Resolution (the performance win) and can confine DLSS 5 to the fovea to make it affordable [C].
- **Phasing:** 1) fixed VRS with presets; 2) gaze-driven VRS; 3) foveated upscaling. No quad views, no density maps.

---

## 2. Techniques compared

| Technique | GPU vendors (PC) | Needs eye tracking | Integration complexity for us | Expected gain | Risks |
|---|---|---|---|---|---|
| **KHR attachment VRS** (`VK_KHR_fragment_shading_rate`, 2-layer attachment) | NVIDIA Turing+, AMD RDNA2+, Intel Arc [C: gpuinfo ~50% of Windows devices; vendor mapping U] | No (fixed) / optional (dynamic) | Medium: add attachment to promoted render passes, add shading-rate state to recreated pipelines, generate the rate image | 10-40% FPS in other VR titles [U: Pimax, OpenXR Toolkit users]; only raster fragment work shrinks | Pipeline incompatibility if we miss one; DLSS/TAA shimmer in coarse areas; `layeredShadingRateAttachments` may be false on some drivers |
| **NV shading rate image** (`VK_NV_shading_rate_image`) | NVIDIA only | No / optional | Low if the engine already uses it (substitute its image); otherwise similar to KHR but render-pass free | Same as KHR | Mutually exclusive with KHR attachment VRS on one device [C: VUID-VkDeviceCreateInfo-shadingRateImage-04480]; not on AMD/Intel |
| **Engine's own VRS** (`r_VRSEnabled`, Sobel content-adaptive) | NVIDIA Turing+ per cvar text [C]; actual extension [U] | No | Low to try (cvar), Medium to steer (combine with our foveation) | Content-adaptive gains like NVIDIA Adaptive Shading: "up to 15-20%" in Wolfenstein: Youngblood [C: NVIDIA] | Retail cvar may be restricted or broken on PC; image is mono and must be promoted to two layers |
| **Fragment density map** (`VK_EXT_fragment_density_map`) | ~7% of Windows devices [C: gpuinfo]; mainly mobile/Qualcomm [U] | Optional | High: changes rasterisation to non-uniform density and needs subsampled images through post | Large on tilers | Not on the RTX 4080 class in practice [U]; breaks screen-space passes that assume uniform pixels |
| **Quad views** (`XR_VARJO_quad_views` + `XR_VARJO_foveated_rendering`, or Quad-Views-Foveated layer) | Any | Optional (fixed inset fallback) | Very high: four views, every view-dependent compute pass x4, VDXR rejects it [C: 01] | 50-100% claimed [U: Pimax] | Conflicts with our 2-view multiview design; two render resolutions to manage |
| **Radial density masking** (Vlachos GDC 2016; VRPerfKit_RSF "RDM") | Any | No | High: checkerboard depth/stencil mask plus a reconstruction pass inside the engine's frame | Moderate | Reconstruction before TAA/DLSS; interacts with Eternal's depth prepass and decal passes |
| **Hidden-area mask** (`XR_KHR_visibility_mask`) | Any | No | Low: stamp near-plane depth in the prepass target | ~10-20% of pixels [U: 01] | Not foveation, but free and additive |
| **Multi-res / lens-matched shading** (VRWorks, `VK_NV_clip_space_w_scaling`) | NVIDIA only, legacy | No | Very high: warps clip space, every screen-space pass must be taught | Moderate | Breaks SSR/AO/binning/DLSS; not viable in an injected engine |
| **Foveated DLSS** (Cheeky-style: DLSS SR on a centre crop, cheap periphery) | NVIDIA RTX | No / optional | Medium, once we own the DLSS integration (07) | 20%+ FPS claimed in DLSS Performance mode [U: Cheeky README]; saves part of DLSS cost (RTX 4080 preset K ~1.5 ms at 4K output) [C: DLSS guide] | Seam at the crop edge; history resets when a gaze-driven crop jumps |
| **Foveated DLSS 5 / neural rendering** | RTX 50 official; RTX 20-40 via mods [C: heise] | Optional | High and experimental | Negative (costs frame time); foveation only limits the cost | Runtime licensing and availability; quality varies by game [C: Cheeky notes] |
| **Encoder-side foveated streaming** (Steam Frame, VD) | n/a | Yes (on headset) | None: transparent to us [C: 01] | Bandwidth, not GPU | Stacks visually with our peripheral reduction; tune both together |

Vendor notes: KHR attachment VRS is the only PC-wide path. Texel size is 16x16 or 8x8 on nearly all devices (gpuinfo: 176 devices at 16x16, 174 at 8x8) [C]. AMD RDNA2/3 are expected to stop at 2x2 (no 4x4) [U]. PimaxMagic4All and vrperfkit are NVIDIA-only because D3D11 VRS only existed on NVIDIA [C: PimaxMagic4All wiki]; Vulkan has no such limit.

---

## 3. Foveated DLSS and "DLSS 5": state of the art (September 2026)

**What DLSS 5 is.** NVIDIA announced DLSS 5 in March 2026 and shipped it on 2026-09-03 with NBA 2K. It is a "3D-guided neural rendering" model that re-lights and re-materials the finished frame; it does not add frames or resolution [C: NVIDIA newsroom; heise]. NVIDIA quotes about 8 ms per frame at 4K on an RTX 5090, so it roughly halves frame rate [C: heise]. It is RTX 50 only at launch; RTX 40 support is promised without a date; modders have it running on RTX 20-40 [C: heise]. Players get an on/off switch; developers control model, intensity and masking [C]. The DLL is `nvngx_dlssnr.dll` ("NR", neural rendering) [C: dlss5-vr, Cheeky].

**Projects that combine it with VR:**

1. **Cheeky Foveated DLSS** (ClarkCheekyKent, GPL-3.0, very active, last commit 2026-09-25) [C]. Hooks NGX in Vulkan, D3D11 and D3D12 games and in UEVR. How it works [C: `USAGE.md`]:
   - The game's single DLSS SR evaluation is replaced by two: a **centre crop** (default 0.55 x 0.45 of the frame, elliptical blend via "roundness", 0.04 feathered transition) at the game's preset, optionally supersampled 1-2x and area-downsampled; and a **peripheral DLAA** pass (fast preset E) on the whole frame downscaled further (periphery scale 0.75). A composite pass blends them.
   - Motion vectors are resampled per crop in a compute pass; DLSS history is reset when a gaze-moved crop jumps more than max(64 px, 12.5% of the crop).
   - Centre placement: fixed, runtime gaze (OpenXR `XR_EXT_eye_gaze_interaction` through its own OpenXR layer, or OpenVR), or simulated. It projects "a shared forward direction into each eye, including eye-view cant and asymmetric fields of view" for fixed placement. Gaze smoothing 20 ms, crop origin quantised to 8 px, hold last gaze 100 ms then return to fixed over 150 ms.
   - DLSS-NR (DLSS 5): experimental, off by default, user supplies the runtime DLL. NR can be foveated with its own region, and run **before** upscaling at a working scale 0.1-1.0 to cut its cost. The author states no quality or performance improvement has been measured for that mode yet.
   - Claimed result: "With DLSS Performance FPS gains of 20%+ are standard, even more with eye tracked headsets" [U: author claim; no methodology].
2. **dlss5-vr** (eregnier, MIT, 2026-09-11) [C]: a proxy/installer that brings DLSS 5 NR to Luke Ross R.E.A.L. VR mods through OptiScaler, running NR pre-upscale at 0.75 working scale and backing off when SteamVR frame timing is tight. **No foveation** [C: README].
3. **SkyrimVRPerfKIT "Custom-Region DLSS5 Neural Rendering"** (Nexus 182089): VRS foveation plus sub-region DLSS and region-limited NR [U: search snippet; page returned 403].
4. **Earlier tools:** vrperfkit and forks (NVIDIA VRS FFR + FSR/NIS, D3D11) [C]; OpenXR Toolkit (VRS FFR/ETFR for D3D11/D3D12 OpenXR apps, discontinued) [C]; PimaxMagic4All (Nov 2025, Pimax's proprietary LibMagic driven by other headsets' eye trackers; D3D11 + OpenVR + NVIDIA) [C]; NVIDIA VRSS 2 (driver foveated supersampling for D3D11 MSAA titles) [C], not applicable to Vulkan.

**Confirmed vs rumour.** Confirmed: DLSS 5 exists, costs frame time, is RTX 50 official; foveated DLSS SR and NR exist in an open project with Vulkan support. Not confirmed: any measured VR figure for foveated DLSS 5, any NVIDIA-official foveated DLSS mode.

**For us:** foveating the *upscaler* is a proven community technique and composes with VRS. DLSS 5 is an optional RTX 50 visual feature, viable in VR only if foveated; not a first-release item. Cheeky is GPL-3.0: study the design, copy no code.

---

## 4. Fixed foveation done properly

### 4.1 Where the centre goes

Existing tools agree: centre the full-rate region on the projection of head-forward `(0, 0, -1)` in view space into each eye's frustum, using that eye's pose and asymmetric FOV. OpenXR Toolkit does it at session start and adds an empirical +4% horizontal offset towards the nose [C: `_cache/OpenXR-Toolkit/XR_APILAYER_MBUCCHIA_toolkit/vrs.cpp:725-735`, `layer.cpp:1737-1766`]; Quad-Views-Foveated computes the same "resting gaze" [C: `layer.cpp` `populateFovTables`]; vrperfkit derives it from OpenVR projection tangents [C: `openvr_manager.cpp:332-356`]; The Dark Mod VR uses `ProjectCenterUV(eye)` [C].

PC headsets have asymmetric per-eye FOV (more temporal than nasal), so the straight-ahead point sits off the image centre towards the nose. Centring on the image midpoint wastes full-rate pixels at the temporal edge and coarsens the nasal side of the sweet spot. On canted headsets (Pimax) head-forward differs from the eye's own forward by the cant; head-forward is right because that is where a relaxed gaze lands. Both come from `xrLocateViews`.

Region sizes should be **angles from that centre**, computed per tile by un-projecting the tile centre with the eye's FOV tangents. Percent-of-image radii mean different things on 90- and 120-degree headsets.

### 4.2 How wide the fixed region must be

Evidence gathered:

- Quad-Views-Foveated: fixed inset ±0.50 x ±0.45 NDC around the resting gaze without eye tracking, ±0.35 by default with it, ±0.29-0.33 in the Varjo/Pimax/Omnicept sections [C: `_cache/Quad-Views-Foveated/settings.cfg`, `layer.cpp:3076-3077`]: 1.4-1.7x wider per axis without gaze.
- OpenXR Toolkit presets (fraction of vertical half-extent, 1.25 horizontal stretch): Wide 55/80%, Balanced 50/60%, Narrow 30/55%; Quality = 1x, 1/2x, 1/8x shading, Performance = 1x, 1/4x, 1/16x [C: `articles/openxr-toolkit-fr.md`, `vrs.cpp:693-722`]. Wide is "barely noticeable" through the lenses; Narrow "is noticeable even with the distortion created by the lens" [C].
- vrperfkit defaults 0.6/0.8/1.0 of half-height; The Dark Mod VR 0.3/0.75/0.85 [C].
- Most natural saccades are 15 degrees or less; larger shifts recruit the head (Bahill, Adler, Stark 1975) [U: not fetched]. A fixed zone of 20-25 degrees radius covers most gaze.

On a ~100-degree headset (vertical half-FOV ~48 degrees) the Toolkit inner rings are about 31 degrees (Wide), 29 (Balanced) and 18 (Narrow) vertically [U: our arithmetic].

### 4.3 Lens type changes the right default

Fresnel and aspheric optics (Index, PSVR2, Quest 2, Pimax Crystal) are soft towards the edge, so they hide peripheral coarsening well. Pancake optics (Quest 3/3S/Pro, Bigscreen Beyond 2/2e, Steam Frame) are sharp much closer to the edge, so the same preset is more visible [U: widely reported, not measured by us]. Default one notch gentler on pancake headsets. Wide-FOV headsets (Pimax) gain the most because more of the image is periphery.

### 4.4 Presets

Rates are given as fragment sizes. "4x4" falls back to 2x2 on devices that do not report it (expected on AMD) [U].

| Preset | Full rate (1x1) | Ring 2 | Ring 3 | Outside | Visibility (expected) |
|---|---|---|---|---|---|
| Off | everything | | | | |
| Subtle (fixed) | ≤ 30° | 2x1 / 1x2 to 38° | 2x2 beyond | | Close to invisible in headset; Toolkit "Wide/Quality" equivalent |
| Balanced (fixed, default on fresnel) | ≤ 24° | 2x1 / 1x2 to 30° | 2x2 to 40° | 4x4 | Visible in screenshots, hard to spot in motion |
| Aggressive (fixed) | ≤ 18° | 2x2 to 28° | 4x4 beyond | | Toolkit "Narrow/Performance" equivalent; visible edge shimmer |
| Eye-tracked Balanced | ≤ 10° + margin | 2x1 / 1x2 to 16° | 2x2 to 26° | 4x4 | Should be invisible if latency is in budget (§5.2) |
| Eye-tracked Aggressive | ≤ 7° + margin | 2x2 to 18° | 4x4 beyond | | For wide-FOV headsets |

"Margin" is gaze error plus latency drift, 3-5 degrees (§5.2). Rings are stretched ~1.2-1.25x horizontally (Toolkit, Cheeky do the same), ring edges are dithered over one tile, and a vertical offset slider is exposed. Radii are starting points to tune on the rig, not measured optima.

### 4.5 Measured gains elsewhere

- Pimax: VRS "typically 10 to 40% extra FPS", quad views "up to 50 to 100%" [U: vendor]. Bigscreen: DFR ≈ "RTX 5090 performance from an RTX 4090" [U: vendor].
- OpenXR Toolkit: FFR plus NIS "a total boost of 15 FPS" in MSFS 2020; the lowest rate everywhere did not beat the aggressive preset because the bottleneck moved [C: doc]. MSFS 2024 users report about 1 FPS [U: forum].
- NVIDIA Adaptive Shading in Wolfenstein: Youngblood (id Tech, Vulkan): "up to 15%"/"up to 20%", hardly perceptible at Quality/Balanced [C: NVIDIA]. Content-adaptive, not foveated, but the closest engine relative.

Estimate for Eternal on an RTX 4080: fixed Balanced 10-20% frame time, eye-tracked 20-35%, if the forward passes dominate [U]. VRS does nothing for compute (binning, AO, SSR, post, DLSS), shadows or vertex work; GPU culling already removes ~70% of triangles [C: SIGGRAPH 2020]. Rig captures decide (§10).

---

## 5. Eye tracking on PC

### 5.1 Availability per headset and runtime

| Headset | Runtime path | `XR_EXT_eye_gaze_interaction` | Setup the user must do | Notes |
|---|---|---|---|---|
| Quest Pro | VDXR | Yes [C: 01] | VD "Forward tracking data" [C: QVF wiki] | Disable VD foveated streaming blur issues [C: 01] |
| Quest Pro | Steam Link → SteamVR | Yes [C: QVF wiki] | SteamVR "Share eye tracking data to other apps on this PC" | |
| Quest Pro | Meta Link (Oculus OpenXR) | Only `XR_FB_eye_tracking_social` [C: 01] (OpenXR-Eye-Trackers wiki lists native support [U: conflict]) | Developer account, "Developer Runtime Features" + "Eye tracking over Oculus Link", phone developer mode [C: QVF wiki] | Support `XR_FB_eye_tracking_social` as a second source |
| Steam Frame | Steam Link → SteamVR | Yes via the same "share eye tracking" toggle [C: QVF and PimaxMagic4All wikis] | Toggle in SteamVR settings | Launched 2026-09-18 [C: Wikipedia]; exact SteamVR exposure details still thin [U] |
| PSVR2 | SteamVR + PSVR2 Toolkit ≥ 0.1.2 | Yes [C: OpenXR-Eye-Trackers wiki] | Install toolkit, run its calibration | Eye data "must not be used in commercial environments" [C: README]; fine for a free mod |
| Bigscreen Beyond 2e | SteamVR | Yes, native [C: OpenXR-Eye-Trackers wiki] | Enable eye tracking in Beyond Utility [C: Bigscreen] | DFR early access since 2025-12-22 [C] |
| Pimax Crystal / Super | Pimax Play OpenXR | Yes [C] | Pimax eye calibration | 120 Hz tracking camera [C: Pimax]; native quad views too |
| Pimax Crystal | SteamVR | Needs OpenXR-Eye-Trackers layer [C] | Install layer | |
| Varjo Aero / XR-3 / XR-4 | Varjo OpenXR | Yes [C] | | Native quad views + `XR_VARJO_foveated_rendering` |
| Galaxy XR, Play For Dream MR | VDXR | Yes [C: PimaxMagic4All wiki, UploadVR] | VD forward tracking data | |
| Vive Pro Eye | VIVE Console | Yes [C: OpenXR-Eye-Trackers wiki] | | Legacy |
| Vive Focus Vision | VIVE Hub / SteamVR | [U] | | Not verified |
| HP Reverb G2 Omnicept | WMR / Oasis driver | Yes via Oasis [C] | | WMR is a dead end on current Windows [U] |
| Quest 3/3S, Index, Beyond 2, Pico 4 | any | No eye tracker | | Fixed foveation only |

`XR_FB_foveation` and `XR_META_foveation_eye_tracked` are Android-only; no PC runtime advertises them [C: 01].

### 5.2 Latency, prediction, vergence, permissions

- **Latency.** Albert et al. (NVIDIA, 2017): no significant loss in acceptable foveation up to 50-70 ms total system latency; 80-150 ms added tracker latency reduced it; larger foveal regions tolerate more [C: `_cache/foveation/Albert2017-latency-foveated.pdf`]. Streamed headsets add network and encode time to gaze [U]. Hence the 3-5 degree margin and the 2x1/1x2 ring.
- **Prediction.** Runtimes that cannot predict must clamp to the nearest sample; `XrEyeGazeSampleTimeEXT` reports the time used [C: spec]. Log sample age.
- **One ray, two eyes.** The extension gives one combined gaze pose. Projecting it at infinity into both eyes (OpenXR Toolkit [C]) ignores vergence: at 1 m, ~1.8 degrees per eye [U: arithmetic]. Start with a wider margin; sample depth at the gaze point later if needed.
- **Permissions.** Under a permission system the runtime reports the action inactive and clears location flags until access is granted [C: spec]. Treat "extension present" and "gaze valid" separately, fall back to fixed silently, show status in settings, never store gaze.
- **Filtering.** Cheeky: 20 ms smoothing, quantised origin, 100 ms hold, 150 ms ease back to fixed [C]. Saccadic omission (~50 ms of reduced sensitivity) makes a short hold safe [C: Albert]; a pattern that jumps once beats one that swims.

---

## 6. Applying VRS inside DOOM Eternal's frame

### 6.1 Which passes

| Pass (from SIGGRAPH 2020 and 02) | VRS? | Reason |
|---|---|---|
| Depth prepass | No | Depth/stencil rate is per sample anyway; saves nothing and alpha-tested geometry may clamp to 1x1 |
| Geometry decal R8 index pass | No | Writes indices; coarse shading would smear decal IDs |
| Forward opaque (main uber shaders, clustered lights and decals) | **Yes** | The main cost; Eternal already shipped VRS here on Xbox [C: Microsoft] |
| Transparents, particles, before/after water | **Yes**, one notch coarser allowed | Low-frequency content |
| Water G-buffer (half horizontal res) | No at first | Already reduced |
| Shadow maps | No | No colour attachment; not screen-space |
| Compute: binning, AO, SSR, post, bloom, DLSS | Not applicable | VRS only affects rasterised fragments |
| UI / HUD / our overlays | No | Text legibility; our UI is composited separately |

Pass identification uses the same attachment-signature matching the multiview promotion already needs (colour format + depth format + render resolution + 2-layer promotion), plus the engine's debug labels when present [U: whether retail builds emit them].

### 6.2 Pitfalls from the spec and from other tools

- **Pipelines ignore the attachment by default.** Without `VkPipelineFragmentShadingRateStateCreateInfoKHR`, the pipeline rate is 1x1 and both combiners are KEEP, so the attachment rate is ignored [C: Vulkan spec]. We must add the struct with `combinerOps = {KEEP, REPLACE}` (or MAX to honour the engine's own rate), or declare `VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR` and set it per pass. With dynamic rendering, pipelines also need `VK_PIPELINE_CREATE_RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR` [C: VUID-vkCmdDraw-imageView-06183].
- **Render pass compatibility.** Adding an attachment reference to a subpass changes compatibility; pipelines and framebuffers must be recreated against the new pass [C: spec §8.3]. Multiview (`viewMask`) already forces this recreation in our design (02), so VRS reuses that path instead of adding a new one.
- **Per-eye layers.** "If multiview is enabled and the shading rate attachment has multiple layers, the shading rate attachment texel is selected using layer = ViewIndex" [C: spec]. This needs `layeredShadingRateAttachments`; if it is false, `layerCount` must be 1 [C: VUID-VkImageViewCreateInfo-usage-04551] and both eyes share one pattern. Fallback: the union (finest rate) of both eyes' patterns in one layer, which costs some gain because the two centres are offset towards each nose.
- **Texel size and format.** `R8_UINT` image, one texel per `shadingRateAttachmentTexelSize` block, power of two within `min/maxFragmentShadingRateAttachmentTexelSize` [C: VUIDs 06149-06156]. Use 16x16 on NVIDIA, 8x8 where allowed. Encoding is `(log2(w) << 2) | log2(h)`.
- **NV and KHR cannot coexist.** If the device enables `shadingRateImage` (NV), it must not enable `attachmentFragmentShadingRate` [C: VUID-VkDeviceCreateInfo-shadingRateImage-04480]. Our layer owns device creation; if the engine asks for the NV extension (for `r_VRSEnabled`), we choose one path per session.
- **Silent clamps to 1x1**: depth/stencil writes, sample mask, interlock, custom sample locations, conservative raster, unless the matching `fragmentShadingRateWith*` property is true [C: spec]. Harmless, but hides gains.
- **Centroid.** A coarse fragment's centre can lie outside the primitive; AMD recommends centroid interpolation [C: FidelityFX docs]. Watch for texture bleeding on thin geometry.
- **Screen-space noise** keyed on `gl_FragCoord` becomes 2x2 blocks; DLSS may hide it.
- **Upscaler interaction.** OpenXR Toolkit saw visible artifacts when the in-app render scale was lowered before its VRS [C]. Eternal shipped VRS with TAA on Xbox [C: Microsoft], so static coarse regions should be tolerable; a gaze-driven pattern changes shading under DLSS history. Move it in whole-tile steps, filter gaze, keep 2x1/1x2 rings between 1x1 and 2x2.

### 6.3 The engine's own VRS path

The cvars describe VRS on "Scarlett + NV Turing" with Sobel-based rates (`r_VRSSobelTolerance`), a forced-2x2 option and a debug overlay [C: cvar dump]. First, flat and unmodded: `r_VRSEnabled 1`, `r_VRSDebug 1` (console unlock per 03), then capture which extension and passes it uses [U]. If it works, the engine has already picked the passes and pipeline state, and we could substitute or combine (coarser of content-adaptive and foveated) a 2-layer rate image instead of adding attachments. NVIDIA-only, so an optimisation, not a replacement for the KHR path.

---

## 7. Recommended architecture

### Phase 1: fixed foveation with KHR attachment VRS (all vendors)

1. At device creation, query `VkPhysicalDeviceFragmentShadingRateFeaturesKHR` and properties (`attachmentFragmentShadingRate`, `layeredShadingRateAttachments`, texel sizes, `vkGetPhysicalDeviceFragmentShadingRatesKHR` for 4x4 support). Enable the feature and extension if present. Refuse the NV feature if the engine requests it, unless we are running the §6.3 path.
2. Create one `R8_UINT` 2-layer image per render resolution, `usage = FRAGMENT_SHADING_RATE_ATTACHMENT | STORAGE`, sized `ceil(w/texel) x ceil(h/texel)`. When DLSS render resolution changes, recreate it.
3. Each frame, a small compute pass in our layer-owned command buffer writes it from a UBO: per eye, the tan-space centre (projection of head-forward, or gaze in Phase 2), the FOV tangents, the preset's ring angles, the horizontal stretch and the per-tile dither. Cost is negligible: 2 x ~150x150 texels at 16x16 for a 2400 px eye.
4. In `vkCreateRenderPass2` (or when we convert a v1 pass to v2 for multiview), for passes classified as foveatable, append an attachment description (format R8_UINT, load, `layout = FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR`) and chain into the subpass:

   ```c
   VkAttachmentReference2 sriRef = { VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, NULL,
       sriIndex, VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR, 0 };
   VkFragmentShadingRateAttachmentInfoKHR fsr = {
       VK_STRUCTURE_TYPE_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR, subpass.pNext,
       &sriRef, { texel, texel } };
   subpass.pNext = &fsr;        /* subpass.viewMask = 0b11 already set by the multiview rewrite */
   ```

   For dynamic rendering, chain `VkRenderingFragmentShadingRateAttachmentInfoKHR { imageView, layout, texelSize }` into `VkRenderingInfo` and add `VK_PIPELINE_CREATE_RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR` to matching pipelines.
5. Framebuffers built against the new pass get one more view (the 2-layer SRI view). Imageless framebuffers get one more `VkFramebufferAttachmentImageInfo` and the begin-info attachment list is extended.
6. In the pipeline recreation that multiview already needs, chain:

   ```c
   VkPipelineFragmentShadingRateStateCreateInfoKHR rate = {
       VK_STRUCTURE_TYPE_PIPELINE_FRAGMENT_SHADING_RATE_STATE_CREATE_INFO_KHR, NULL,
       { 1, 1 }, { VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR,
                   VK_FRAGMENT_SHADING_RATE_COMBINER_OP_REPLACE_KHR } };
   ```

   If the engine already provides the struct (its own VRS), use MAX for the attachment combiner so the coarser of the two wins, subject to `fragmentShadingRateNonTrivialCombinerOps`.
7. Barrier: compute write → `FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR` at `FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR` stage, layout transition to `FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR`.
8. Debug view: an overlay that tints tiles by rate, and a toggle hotkey for A/B timing. User settings: Off / Subtle / Balanced / Aggressive / Custom (ring angles, horizontal stretch, vertical offset), with the per-headset default from §8.

### Phase 2: eye-tracked foveation

1. In our OpenXR session, if `XrSystemEyeGazeInteractionPropertiesEXT::supportsEyeGazeInteraction` is true, create an action set with one pose action bound to `/user/eyes_ext/input/gaze_ext/pose` and an action space; on Meta Link, alternatively `XR_FB_eye_tracking_social`. Since the game has no OpenXR input of its own, there is no host action set to merge with.
2. Locate the gaze space at the predicted display time each frame; check `isActive` and the valid flags; read `XrEyeGazeSampleTimeEXT`.
3. Filter (20 ms smoothing, whole-tile quantisation, 100 ms hold, 150 ms ease to fixed), project into each eye with the vergence margin, and write the centres into the rate-image UBO as late as possible on the GPU timeline (the same late-latched constant buffer pattern 02 uses for poses), so the rate image is built after the newest gaze sample.
4. Switch to the eye-tracked ring set only while gaze is valid; blend back to fixed rings over ~150 ms on loss.

### Phase 3: foveated upscaling

1. **One strength control** moves the VRS rings and the DLSS ratio together; with eye tracking the peripheral savings buy a lower DLSS input or more centre supersampling.
2. **Foveated DLSS output.** Once we own per-eye DLSS (07), run the quality preset on a centre crop via NGX subrects (`InRenderSubrectDimensions`, `In*SubrectBase`, `InEnableOutputSubrects`) [C: DLSS guide] and a fast preset or spatial upscale on the periphery, feather-blended: Cheeky's design, reimplemented. Preset K at 4K output costs ~1.5 ms on an RTX 4080 (L: 2.5 ms) [C: DLSS guide]; two eyes exceed 4K, so the saving is about 1 ms per frame [U].
3. **DLSS 5 / NR.** Optional, officially supported GPUs only, foveated, off by default, after Phases 1-2.

Not recommended: quad views (four views break our two-view multiview design and VDXR rejects the configuration [C: 01]), fragment density maps (not on target desktop GPUs [C/U]), lens-matched and multi-res shading (break every screen-space pass).

---

## 8. Recommended defaults per headset

| Headset | Eye tracking path | Default | Notes |
|---|---|---|---|
| Quest 3 / 3S (pancake) | none | Fixed **Subtle** | Pancake edges are sharp; let users choose Balanced |
| Quest 2 (fresnel) | none | Fixed Balanced | |
| Quest Pro | VDXR or Steam Link | Eye-tracked Balanced, fixed Subtle when gaze invalid | Meta Link needs developer settings; say so in the UI |
| Valve Index | none | Fixed Balanced | Fresnel, soft edges |
| Steam Frame | Steam Link share toggle | Eye-tracked Balanced | Tune alongside Steam's foveated streaming |
| PSVR2 | PSVR2 Toolkit | Eye-tracked Balanced | Fresnel; periphery tolerant |
| Bigscreen Beyond 2 | none | Fixed Subtle | Pancake, small FOV: small gain |
| Bigscreen Beyond 2e | SteamVR native | Eye-tracked Balanced | |
| Pimax Crystal / Super / Light | Pimax Play (not Light) | Eye-tracked Aggressive (Crystal/Super); fixed Balanced (Light) | Widest FOV, largest gain |
| Varjo Aero / XR-4 | native | Eye-tracked Balanced | |
| Galaxy XR, Play For Dream | VDXR | Eye-tracked Balanced | |
| Unknown headset | none | Fixed Subtle | Safe default |

On GPUs without KHR attachment VRS (GTX 10 series, RDNA1), foveation is hidden and only the hidden-area mask applies.

---

## 9. Implications for our design

1. **VRS is part of the multiview rewrite.** Passes, framebuffers and pipelines are already recreated for `viewMask`; the rate attachment and pipeline state are two more chained structs. One pass classifier serves both.
2. **A 2-layer rate image lives beside the promoted targets**, keyed by render size, recreated when the DLSS ratio changes.
3. **Choose NV-engine-path or KHR-attachment at `vkCreateDevice`**; they are mutually exclusive.
4. **Late-latch foveation constants** with the pose constants so gaze and pose come from the same late sample.
5. **Specify foveation in degrees** from a per-eye centre derived from OpenXR FOV and pose: headset- and resolution-independent.
6. **Gaze is our own OpenXR input**; no action splicing, unlike Cheeky and Quad-Views-Foveated.
7. **One performance control** drives VRS, DLSS ratio and later foveated DLSS, with an expert page.
8. **Licensing.** Cheeky (GPL-3.0) and Pimax's DLLs: study only. OpenXR Toolkit, Quad-Views-Foveated, vrperfkit, OpenXR-Eye-Trackers are MIT; small helpers can be adapted with attribution.

---

## 10. Open questions to test on the gaming rig (RTX 4080)

1. `vulkaninfo`: `attachmentFragmentShadingRate`, `layeredShadingRateAttachments`, min/max texel size, supported fragment sizes (4x4?), `fragmentShadingRateNonTrivialCombinerOps`, `fragmentShadingRateWithShaderDepthStencilWrites`, and whether `VK_EXT_fragment_density_map` is exposed.
2. Flat game: does `r_VRSEnabled 1` work on PC? Which extension does the engine enable (NV or KHR)? Which passes bind a rate image (Nsight or RenderDoc capture)? Frame time with `r_VRSDebugForceShadingRate 4` (forced 2x2) versus off: this upper bound tells us how much of Eternal's frame VRS can ever save.
3. Does the engine use `vkCreateRenderPass` v1, v2 or dynamic rendering for the forward opaque and transparent passes? (Shared with 02's open question.)
4. Which Eternal fragment shaders write depth, use sample masks or interlock (and so clamp to 1x1)?
5. At VR resolution (2 x ~2400², 90 Hz), GPU time of forward opaque + transparents as a share of the frame: the realistic ceiling for foveated VRS.
6. Visual A/B of Subtle/Balanced/Aggressive with DLSS Quality and Performance: shimmer in coarse regions, decal bleeding, dither blockiness, HUD-adjacent artifacts.
7. Eye tracking: gaze sample age on Steam Link (Quest Pro or Steam Frame), VDXR and Beyond 2e; whether the eye-tracked presets are invisible during fast combat.
8. With `layeredShadingRateAttachments` false (if seen on any vendor), the cost of the union-pattern fallback.
9. DLSS evaluation time per eye at our render sizes, to size the Phase 3 saving before building it.

---

## Sources

Local copies are under `reference/foveation/` unless noted.

- Vulkan specification, fragment shading rates, render pass compatibility, device-creation VUIDs: https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html (`reference/_cache/vkspec.html`)
- VK_KHR_fragment_shading_rate appendix and proposal: https://github.com/KhronosGroup/Vulkan-Docs (`reference/vulkan/spec/`)
- VK_EXT_fragment_density_map (+2, +offset), VK_QCOM_fragment_density_map_offset, VK_NV_shading_rate_image appendices: https://github.com/KhronosGroup/Vulkan-Docs (`spec/`)
- Vulkan Hardware Database (extension coverage, shading-rate property distributions): https://vulkan.gpuinfo.org/
- Khronos Vulkan-Samples fragment_shading_rate, fragment_shading_rate_dynamic, fragment_density_map: https://github.com/KhronosGroup/Vulkan-Samples
- XR_EXT_eye_gaze_interaction: https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html (`reference/openxr/extensions/ext_eye_gaze_interaction.adoc`)
- XR_VARJO_quad_views, XR_VARJO_foveated_rendering: https://github.com/KhronosGroup/OpenXR-Docs (`spec/`)
- Quad-Views-Foveated and wiki (mbucchia): https://github.com/mbucchia/Quad-Views-Foveated
- PimaxMagic4All and wiki (mbucchia): https://github.com/mbucchia/PimaxMagic4All
- OpenXR-Eye-Trackers and wiki (mbucchia): https://github.com/mbucchia/OpenXR-Eye-Trackers
- OpenXR Toolkit source and docs (fr, et): https://github.com/mbucchia/OpenXR-Toolkit, https://mbucchia.github.io/OpenXR-Toolkit/
- vrperfkit: https://github.com/fholger/vrperfkit; VRPerfKit_RSF: https://github.com/RavenSystem/VRPerfKit_RSF; Granther fork: https://github.com/Granther/foveated-rendering
- The Dark Mod VR, `VRFoveatedRendering.cpp` (`reference/_cache/thedarkmodvr`), see 04
- Cheeky Foveated DLSS: https://github.com/ClarkCheekyKent/CheekyFoveatedDLSS
- dlss5-vr: https://github.com/eregnier/dlss5-vr
- SkyrimVRPerfKIT Custom-Region DLSS5 (not fetched, 403): https://www.nexusmods.com/skyrimspecialedition/mods/182089
- PSVR2 Toolkit: https://github.com/BnuuySolutions/PSVR2Toolkit
- NVIDIA, "NVIDIA DLSS 5 Delivers AI-Powered Breakthrough in Visual Fidelity for Games": https://nvidianews.nvidia.com/news/nvidia-dlss-5-delivers-ai-powered-breakthrough-in-visual-fidelity-for-games
- heise, "DLSS 5: Nvidia is finally bringing neural rendering to RTX 40 cards" (2026-09-04): https://www.heise.de/en/news/DLSS-5-Nvidia-is-finally-bringing-neural-rendering-to-RTX-40-cards-11441795.html
- NVIDIA DLSS Programming Guide (31 March 2026), execution-time table and subrects: https://github.com/NVIDIA/DLSS (`reference/_cache/upscaling/`)
- NVIDIA, "Turing Variable Rate Shading in VRWorks": https://developer.nvidia.com/blog/turing-variable-rate-shading-vrworks/
- NVIDIA, "NVIDIA Adaptive Shading: A Deep Dive" (Wolfenstein: Youngblood): https://www.nvidia.com/en-us/geforce/news/nvidia-adaptive-shading-a-deep-dive/
- NVIDIA, "NVIDIA VRSS 2: Dynamic Foveated Rendering, No Assembly Required": https://developer.nvidia.com/blog/nvidia-vrss-2-dynamic-foveated-rendering-no-assembly-required
- AMD GPUOpen, FidelityFX Variable Shading: https://gpuopen.com/fidelityfx-variable-shading/ (and `reference/_cache/FidelityFX-SDK-v1.1.4/docs/techniques/variable-shading.md`)
- Microsoft Game Dev, "How Variable Rate Compute Shaders Improved GPU Performance in DOOM: The Dark Ages" (2026-04-09): https://developer.microsoft.com/en-us/games/articles/2026/04/variable-rate-compute-shaders-doom-the-dark-ages/
- DOOM Eternal cvar dump (KEX 2024) and Meathook cvar names: `reference/idtech7/typeinfo/`
- Geffroy, Gneiting, Wang, "Rendering the Hellscape of Doom Eternal", SIGGRAPH 2020: https://advances.realtimerendering.com/s2020/RenderingDoomEternal.pdf
- Albert, Patney, Luebke, Kim, "Latency Requirements for Foveated Rendering in Virtual Reality", ACM TAP 2017: https://research.nvidia.com/sites/default/files/pubs/2017-09_Latency-Requirements-for/a25-albert.pdf
- Bahill, Adler, Stark, "Most naturally occurring human saccades have magnitudes of 15 degrees or less", Investigative Ophthalmology 1975 (not fetched)
- Vlachos, "Advanced VR Rendering Performance", GDC 2016: https://media.steampowered.com/apps/valve/2016/Alex_Vlachos_Advanced_VR_Rendering_Performance_GDC2016.pdf
- Bigscreen, "Now available: Dynamic Foveated Rendering with Bigscreen Beyond 2e": https://store.bigscreenvr.com/blogs/beyond/dynamic-foveated-rendering-with-bigscreen-beyond-2e
- Pimax, "The Crystal Super's secret weapon: Dynamic Foveated Rendering": https://store.pimax.com/blogs/blogs/the-crystal-supers-secret-weapon-dynamic-foveated-rendering
- UploadVR, "Free Tool Adds Eye-Tracked Foveated Rendering To Many SteamVR Games" (2025-11-18): https://www.uploadvr.com/pimaxmagic4all-adds-eye-tracking-to-many-steamvr-games/
- UploadVR, "Microsoft Flight Simulator 2024 Now Has Foveated Rendering": https://www.uploadvr.com/microsoft-flight-simulator-2024-now-has-foveated-rendering/
- Wikipedia, "Steam Frame": https://en.wikipedia.org/wiki/Steam_Frame
- 01-openxr-and-runtimes.md, 02-vulkan-stereo-and-spirv.md, 04-prior-art-injectors-and-ui.md (this repository)
