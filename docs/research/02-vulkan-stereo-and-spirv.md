# 02: Vulkan layer mechanics, stereo rendering techniques and SPIR-V tooling

Status: research notes, 2026-09-25. Tags: **[C]** = confirmed against a primary source or open-source
code we read; **[U]** = unverified, inferred, or needs a capture/test on DOOM Eternal before we rely on it.
Paths under `reference/` are local copies listed in `reference/vulkan/MANIFEST.part.md`.

## 1. Summary

- An implicit Vulkan layer gated by an `enable_environment` variable that only our launcher sets is the
  right injection vehicle. Existing VR layers ship this way, and the loader documents it for exactly
  this use. [C]
- Single-frame stereo is feasible with multiview plus two-layer render targets, but DOOM Eternal's
  renderer is harder than DOOM 2016's. It is fully forward, fully bindless and GPU-driven (compute
  triangle culling, merged indirect draws, compute light binning), and it has RT reflections and DLSS
  2. [C: SIGGRAPH 2020 talk, NVIDIA June 2021 notes]
- Treat the engine's CPU camera as a **centred enclosing camera** and transform clip space per eye with
  a full 4x4 matrix. Everything view-dependent that runs once (culling, light binning, gameplay
  visibility) stays mono but must be made conservative for both eyes.
- For shader rewriting, **patch SPIR-V directly and link small precompiled GLSL helpers**, the way the
  Vulkan validation layers' GPU-AV instrumentation works. Do not decompile to GLSL with SPIRV-Cross,
  regex-patch, then recompile with glslang at pipeline-creation time.
- Deliver per-frame stereo constants through a descriptor set slot the layer reserves, written on the
  GPU timeline. Do not use a device-idle-retired host buffer injected at bindings 30/31 of every set
  layout. Bindless variable-count layouts make a fixed 30/31 scheme unworkable on Eternal.

## 2. Vulkan loader and layer mechanics

### 2.1 Discovery, gating and ordering

- Windows implicit layers are found through registry values under
  `HKLM|HKCU\SOFTWARE\Khronos\Vulkan\ImplicitLayers`. Each value name is an absolute path to a JSON
  manifest and its DWORD data is 0. HKCU is skipped for elevated processes. The driver-PnP keys are
  reserved for drivers. [C: `reference/vulkan/loader/LoaderLayerInterface.md` "Windows Layer Discovery"]
- `VK_ADD_IMPLICIT_LAYER_PATH` / `VK_IMPLICIT_LAYER_PATH` (and `VK_ADD_LAYER_PATH` for explicit layers)
  let a launcher add a manifest directory for one child process without touching the registry. They are
  ignored under elevation. [C] This is cleaner than registry installs, but only if the game process
  inherits our environment (see open questions on Steam relaunch). [U]
- Filtering (loader >= 1.3.234): `VK_LOADER_LAYERS_ENABLE`, `VK_LOADER_LAYERS_DISABLE` (with
  `~implicit~`, `~explicit~`, `~all~`) and `VK_LOADER_LAYERS_ALLOW`. Layers forced on by the enable
  filter load "after implicit layers but before other explicit layers". [C]
- An implicit layer manifest must declare `disable_environment` and may declare `enable_environment`.
  With `enable_environment` set, the layer is dormant in every process that lacks the variable. [C:
  manifest section] The usual pattern: the launcher writes an HKCU registration, sets the enable
  variable only on the game's `ProcessStartInfo`, and disables the registration on exit, and
  `vkNegotiateLoaderLayerInterfaceVersion` additionally checks that it is running in the game process.
- Ordering among implicit layers is not controllable by the layer. Within a directory it follows
  `readdir` order, which the docs call "random". [C] Our design must not depend on sitting above or
  below the Steam overlay, OBS, RTSS or Fossilize.

### 2.2 Negotiation and dispatch

- The loader calls `vkNegotiateLoaderLayerInterfaceVersion` first. The layer clamps the version to 2,
  returns its GIPA/GDPA (and GPDPA if it adds physical-device functions), and **must not call down the
  chain**. [C]
- Instance chain: take `VkLayerInstanceCreateInfo` (`VK_LAYER_LINK_INFO`) from `pCreateInfo->pNext`,
  save `pfnNextGetInstanceProcAddr`, advance `pLayerInfo`, then call down. Device chain: the same with
  `VkLayerDeviceCreateInfo`. Resolve every downstream entry point once into a per-device table keyed by
  the dispatch key (the first pointer-sized word of any dispatchable handle). vkBasalt (`_cache/vkBasalt/src/basalt.cpp`,
  `GetKey`) and `Vulkan-Utility-Libraries/include/vulkan/utility/vk_dispatch_table.h` show the
  standard pattern. [C]
- Dispatchable objects the layer creates itself (command buffers for XR copies) need the loader
  dispatch pointer installed through the `VK_LOADER_DATA_CALLBACK` / `pfnSetDeviceLoaderData`
  callback. [C: "Creating New Dispatchable Objects"]
- "A layer, when inserted into an otherwise compliant Vulkan driver, must still result in a compliant
  Vulkan driver." Changed behaviour must not let upper layers make invalid calls to lower ones. [C] For
  us, layers above ours (overlays, capture tools, RenderDoc if loaded implicitly) see the mono world;
  layers below (validation, the driver) see two-layer images and multiview passes. Loading the
  validation layer *below* ours is the correct way to validate our transformations.

### 2.3 Co-existence pitfalls

- Common crash sources are `VK_LAYER_OBS_HOOK`, `VK_LAYER_RTSS`, `VK_LAYER_OW_OVERLAY`,
  `VK_LAYER_reshade`, `VK_LAYER_bandicam_helper`, `VK_LAYER_EOS_Overlay` and
  `VK_LAYER_VALVE_steam_overlay`. Each has its own disable variable, for example
  `DISABLE_VULKAN_OBS_CAPTURE`, `DISABLE_RTSS_LAYER` and `DISABLE_VK_LAYER_VALVE_steam_overlay_1`. [C:
  `reference/vulkan/articles/bad-vulkan-layers-mattstevens.md`] The Steam overlay layer has a known bug
  with swapchains from several `VkDevice`s. [C: steam-for-linux #9120] That matters if the OpenXR
  runtime ever creates a second device.
- **DOOM Eternal runs its own blacklist check.** It shows a startup dialog listing "unsupported" layers,
  which can be dismissed; `+r_allowBlackListedLayers 1` bypasses it. [C: Steam discussion threads; the
  blog above shows the dialog] It is a blacklist of known names, so a new layer name should not trigger
  it. [U] Test it on day one.
- The launcher should offer a "clean" mode that puts `VK_LOADER_LAYERS_DISABLE=~implicit~` plus
  `VK_LOADER_LAYERS_ALLOW=VK_LAYER_ETERNALVR_*` into the child environment, for users whose overlays break
  the game. [C: filter semantics]
- **The OpenXR runtime's own Vulkan calls re-enter the layer.** Runtime-created images go through the
  stereo hooks unless the runtime is handed *downstream* proc-addr functions. [U]
  With `XR_KHR_vulkan_enable2`, give `xrCreateVulkanInstanceKHR`/`xrCreateVulkanDeviceKHR` the next
  layer's GIPA. With enable v1 the runtime resolves through the loader, so we would need a
  thread/return-address guard instead. [U]
- Anti-cheat: DOOM Eternal's single player has no kernel anti-cheat today. BattleMode is out of scope;
  the launcher should refuse to inject into it. [U, needs confirmation from the engine research topic]

## 3. Stereo techniques for a mono engine

### 3.1 Options

| Technique | How | Verdict |
|---|---|---|
| **Multiview (VK_KHR_multiview, core 1.1)** | Promote targets to 2-layer arrays, add `viewMask=0b11` to render passes (`VkRenderPassMultiviewCreateInfo`, `VkSubpassDescription2::viewMask`, or `VkRenderingInfo::viewMask`), inject `ViewIndex` into shaders | **Primary.** One CPU submission, per-view state is automatic, spec-mandated with `maxMultiviewViewCount >= 6` [C] |
| Instanced stereo | Double `instanceCount`, vertex shader writes `Layer` (or viewport index) from `InstanceIndex & 1`, pass eye to the fragment stage as a flat varying | Poor fit. Eternal's merged draws are *indexed indirect* with GPU-written args and instance IDs packed into the index [C: talk], so doubling means rewriting indirect buffers with an extra compute pass. Needs interface rewriting. Cannot combine with multiview (VUID-VkGraphicsPipelineCreateInfo-flags-00764) [C] |
| Side-by-side viewport array | One wide target, two viewports | Rejected. Screen-space filters bleed across the seam and every screen-space UV must be remapped |
| Per-pass CPU replay | Record each render pass twice into per-eye 2D views | Useful for **a few** passes (fullscreen post, DLSS) where array rewriting is awkward. Too costly for geometry passes |
| Alternate-eye (AER) | Render one eye per frame | Debug fallback only: halves temporal rate and breaks TAA and motion vectors |
| Synced sequential (UEVR) | Engine renders the view twice per tick without advancing the world | Needs engine-level hooks, not a layer. TAA ghosts and motion blur must be off [C: `reference/vulkan/articles/uevr-overview.md`]. Worth keeping as a plan B if single-frame stereo stalls on GPU-driven passes |

Multiview's GPU benefit varies by vendor. NVIDIA Turing+ has hardware multi-view for
position-only-varying views [C: NVIDIA MVR blog]. On other hardware the driver may replay per view, so
the saving is mainly CPU and driver overhead. [U: AMD details not documented]

### 3.2 Multiview rules that shape the implementation [C: `reference/vulkan/spec/multiview-and-layers-excerpts.md`]

1. At each `vkCmdBeginRenderPass`/`vkCmdNextSubpass` of a multiview pass, *all* non-render-pass state
   is undefined. A layer that swaps in a multiview pass must re-bind the pipeline, descriptor sets,
   vertex/index buffers, dynamic state and push constants.
2. Queries inside a multiview pass consume N consecutive query slots. Game query indices must be
   remapped.
3. Load/store ops, clears and automatic layout transitions apply per view. `vkCmdClearAttachments`
   clears all views in the mask.
4. With dynamic rendering, the pipeline's `VkPipelineRenderingCreateInfo::viewMask` must match. Every
   attachment view needs `layerCount > msb(viewMask)`.
5. Shaders in a multiview pipeline must not write `Layer`.
6. With a two-layer shading-rate attachment, each view reads its own layer. This gives per-eye
   foveation maps for free (section 6).

### 3.3 Projection: the stereo correction formula and why we should not use it

The 3D Vision / 3DMigoto / Geo-11 family patches vertex output as
`clip.x += separation * (clip.w - convergence)`. This is a w-proportional horizontal image shift plus
a constant. [C: 3DMigoto issues #66/#97] It models a fixed display with convergence, which is wrong
for a head-mounted display: pure IPD with parallel eyes needs a *constant* clip-space offset
(`x_clip' = x_clip - P00 * e`), and asymmetric per-eye FOV needs a scale and offset too. Rebuilding the
term in affine form fixes that, but an x/y-only transform still cannot handle canted eyes or
longitudinal offsets. [C: linear algebra]

Better: if we know the game's centred projection `P_c` (reverse-Z, invertible), each eye's exact clip
transform is one 4x4 matrix:

`C_e = P_e * V_e * inverse(V_c) * inverse(P_c)`, and `clip_e = C_e * clip_c`.

This covers IPD, asymmetric FOV, canted displays and z (depth) exactly, with no per-vertex division.
[C: linear algebra; `P_c` is invertible for reverse-Z infinite projections] It needs `P_c`, which we get
from the engine camera hook or from the view uniform block once identified. [U] The same `C_e`
converts centred reconstruction matrices to per-eye ones: `invVP_e = invVP_c * inverse(C_e)`,
`VP_e = C_e * VP_c`. id stores matrices as row vectors (`mvpmatrixx..w`,
`viewprojectionmatrixw` in DOOM 2016 shaders [U]), so
each patched row is `r'_j = sum_i C_e[j][i] * r_i`.

**Enclosing camera.** Put the engine's CPU camera at a point *behind* the eyes, with a symmetric FOV
that encloses both eye frusta. Its frustum is then a strict superset of both eye frusta, so CPU and GPU
frustum culling, light binning in its screen space, LOD selection and Umbra-style occlusion become
nearly conservative with no per-system changes. Since `C_e` is computed relative to that camera, the
apex position does not affect rendering. [U: standard "combined stereo culling frustum" practice; exact
Eternal behaviour to be measured]

## 4. What breaks, and a strategy per pass type

The rule is to promote images whose contents depend on the eye and keep everything else mono.
Promoting *every* single-layer 2D colour/depth/storage attachment is simple but wastes work (section 8).

| Pass type | Examples in id Tech 7 | Stereo strategy |
|---|---|---|
| Raster geometry (depth prepass, opaque forward, decals, transparents, particles, first-person weapon) | Merged indexed-indirect draws; geometry-decal R8 index pass; forward uber-shaders [C: talk] | Multiview. Vertex: `gl_Position = C_e * gl_Position` in a wrapper `main`. Fragment: array-promote screen-space inputs, index by `ViewIndex`. Indirect args are shared and unchanged |
| Fullscreen post (tonemap, bloom composite, DoF, motion blur, CA, film grain) | Fullscreen triangle into scene-sized targets | Multiview with array-promoted inputs (cheap, one draw). Effects that use camera matrices get per-eye matrices via `C_e`. Motion blur off at first |
| Compute on screen-sized images | SSAO/SSDO, SSR, downsample chains, Hi-Z builds, TAA resolve if compute | Two dispatches, one per eye, with the eye from a **specialization constant** (two pipelines) and images arrayed. Z-doubling is a later optimisation. Compute that writes only buffers stays mono |
| Screen-space reconstruction (SSR, SSAO, fog/volumetrics, refraction, water) | `world_pos` from depth + frustum vectors; `refr_tc`; water SSR [C: talk] | Replace *centred* reconstruction constants with per-eye ones at their load sites (row/matrix patch above). This fixes whole classes at once instead of one textual patch per effect |
| Temporal (TAA, SSR/SSAO history, motion vectors) | History buffers are render targets and get promoted automatically | Needs per-eye previous-frame matrices: keep `C_e(prev)` next to `C_e`. Motion-vector shaders use previous VP, so patch them the same way. Jitter can be shared by both eyes. Disable temporal effects that cannot be fixed (for example with a cvar such as `r_SSDOTemporalAA=0` in DOOM 2016) |
| GPU culling / geometry merging | Backface, frustum, micro-triangle and Umbra-depth occlusion culling; merged index buffer reused by depth and opaque passes [C: talk] | **One** dispatch, shared output, made conservative: enclosing frustum (automatic with the enclosing camera), backface test against both eye positions (cull only if back-facing for both) or disabled, micro-triangle rejection disabled or evaluated at the finer eye scale, occlusion kept (Umbra from the enclosing apex is close to conservative) [U: dilation needed?] |
| Light/decal binning (hybrid tile + cluster) | Compute rasterizer; fine raster uses min/max depth downsample; per-fragment picks the shorter of tile or cluster list [C: talk] | Mono in the enclosing camera's screen space. Per-eye fragments remap their tile/cluster coordinate into that space. Fine-tile depth rejection would read a per-eye depth, so force it conservative (min/max = near/far). The cluster list then wins the "fewer entries" choice where tiles are wrong [U] |
| Gameplay visibility queries | Same binning code, bitfields read back by CPU next frame [C: talk] | Must stay mono and centred. Never let it see eye-specific data |
| Shadows | Depth-only, depth-biased, light-space atlas | Mono: no promotion, no multiview. Promoting them and rendering both layers identically doubles shadow cost for nothing |
| Environment/probe/cube/3D/LUT, water sim grid, caustics | Non-camera or world-space | Mono. Classify by image shape and usage, not by pipeline heuristics |
| Water projected grid | 256x256 screen-space grid from the camera [C: talk] | Hard case. Either duplicate the grid chain per eye or project from the enclosing camera and accept slight edge error [U] |
| RT reflections | `vkCmdTraceRaysKHR` over a screen-sized target | Off at first. Later: array-promote the raygen output and trace twice with a spec-constant eye |
| DLSS 2 | NGX records its own compute work into our command buffer | Off at first. NGX's internal pipelines must be excluded from rewriting, and stereo needs two feature instances evaluated per eye on per-layer views [U] |
| UI / HUD | GUI drawn into the backbuffer after tonemap | Not stereo. Redirect the UI pass into an offscreen mono image and submit it as an OpenXR quad layer. Identify it by shader family (UI shaders, UI vertex layout) or by render pass [U] |

**Bindless and descriptor indexing.** Eternal keeps a global texture descriptor list and pooled
uniform buffers with only three indexable layouts. [C: talk] If render targets and material textures
share `sampler2D[]` arrays, a per-binding decision about which images are stereo is impossible. The
safe rule is that every 2D sampled or storage view becomes `2D_ARRAY` and every shader is
array-promoted. Sampled reads can use `layer = ViewIndex` unconditionally, since
the spec clamps the layer to the view's range, making mono textures read layer 0 at no cost. [C: spec
20.4.1] Emitting `min(eye, textureSize(...).z - 1)` on every sample instead would cost a size query
per fetch. Integer fetch/read/write/texel-pointer ops do need an explicit clamp.
Descriptor-indexing layouts carry `VkDescriptorSetLayoutBindingFlagsCreateInfo`. A
`VARIABLE_DESCRIPTOR_COUNT` binding must be the highest binding in its set, so appending bindings
30/31 to such layouts is invalid, and a layer that rejects any `pNext` on set layouts cannot run
Eternal. [C]

**Push and specialization constants.** Push constants are shared by both views and need replay after
a multiview begin. [C] Specialization constants are the cheap way to build per-eye compute variants
(one extra pipeline per stereo compute pipeline, created alongside the original).

## 5. Delivering per-frame stereo constants

One approach appends UBO bindings 30 and 31 to every descriptor set layout, grows every pool, writes both
descriptors into every allocated set, and updates one host-coherent buffer, retired with
`vkDeviceWaitIdle` when the owner fence cannot prove completion. This fails on Eternal (variable-count bindless layouts) and serialises the GPU in
the worst case. Recommended instead:

1. **Reserve a descriptor set slot**, as GPU-AV does: report `maxBoundDescriptorSets - 1` to the game,
   append our set layout at the last index of every `VkPipelineLayout`, and bind our set lazily before
   draws/dispatches whenever a game bind could have disturbed it. [C: `reference/vulkan/articles/VVL-gpu_validation.md`]
   The game already created its pipeline layouts against the real limit, so we must check at runtime
   that it never uses the top slot. [U]
2. Put the constants (`C_e`, `C_e(prev)`, inverses, eye positions, frame serial) in a small
   device-local buffer, and **update it on the GPU timeline** with `vkCmdUpdateBuffer` in a layer-owned
   command buffer prepended to the frame's first submit. Add a barrier to uniform reads and a semaphore
   for the async-compute queue. The CPU then never waits. [C: spec semantics; queue choice U]
3. Pin the pose used for each frame in a per-frame record and look it up again at present, so the
   layer submits to OpenXR the exact pose it rendered with.

## 6. Foveation and performance

- **VK_KHR_fragment_shading_rate.** Adding a two-layer shading-rate attachment to promoted geometry
  passes gives per-eye fixed or eye-tracked foveation. It needs RenderPass2 (we can always create our
  stereo pass variant with `vkCreateRenderPass2` even when the game uses v1) or dynamic rendering. The
  gain is limited to raster fragment work. Eternal's forward opaque pass is the natural target;
  compute post passes gain nothing. [C: spec; gain U]
- **Quad views** (Varjo-style 4 views; `_cache/Quad-Views-Foveated`) fit multiview's 6-view minimum,
  but every layer must have the same resolution and every view-dependent compute pass runs four times.
  Not a first-release goal. [C/U]
- **Upscaling per eye.** FSR1 as a final spatial pass on each eye is trivial. FSR2 or
  DLSS need per-eye depth, motion and history, so they run twice on per-layer 2D views with separate
  contexts. For DLSS the layer must also *not* transform NGX's internal shaders. Identify them by call
  window or module return address. [U]
- **Budget.** At 2x the pixels plus VR headroom, expect GPU-bound frames at typical headset
  resolutions. The cheapest wins, in order: do not stereo-render mono work (shadows, probes), keep
  culling and binning single-dispatch, allow dynamic resolution per frame, then fragment shading rate.

## 7. SPIR-V tooling and the rewriting approach

### 7.1 The GLSL round-trip and its weak points

The obvious approach subclasses `spirv_cross::CompilerGLSL`: promote images while emitting GLSL, rename
`main`, append a wrapper `main`, run a set of `std::regex` rewrites keyed on engine member and function
names, then compile with glslang for Vulkan 1.1 / SPIR-V 1.3. All of this happens inside
`vkCreateGraphicsPipelines`, twice per pipeline (a mono variant and a stereo variant), with an in-memory
module cache. Weak points:

- It is **lossy and fragile.** Decompilation breaks on opcodes SPIRV-Cross does not emit (`OpGroupAll`
  on AMD modules produces "unimplemented op 261"). It needs special cases such as bool stores from
  integers, and every semantic fix is a textual regex against generated GLSL. That breaks as soon as
  SPIRV-Cross changes its output or the game's compiler emits a different form. [U]
- It is **slow.** Two full decompile-compile cycles per pipeline at creation time, and it needs large
  offline corpus audits before it can be trusted. Eternal compiles at load, so this becomes load-time
  stutter. [U: not measured]
- It **depends on names.** Recognition uses `mvpmatrixw`, `projectionmatrixz`, `ssdoparms` and so on.
  If Eternal ships stripped SPIR-V, most of the recognition disappears. [U: capture needed]

### 7.2 Recommendation: direct SPIR-V patching plus linked helpers

Each transformation we need is local and mechanical at the SPIR-V level:

| Transform | Direct SPIR-V edit |
|---|---|
| Enable multiview | `OpCapability MultiView` (4439), `SPV_KHR_multiview` for SPIR-V < 1.3, an Input `int` variable with `BuiltIn ViewIndex` (4440), appended to `OpEntryPoint` [C: `reference/spirv/spirv-stereo-excerpts.md`] |
| Per-eye position | New entry function: `OpFunctionCall` to the original main, load `Position`, multiply by `C_e[ViewIndex]` from our set, store. The wrapper-main idea, without GLSL |
| Array promotion | Clone `OpTypeImage` with `Arrayed=1`, retype the affected variables and their consumers, widen coordinates by one component, narrow `QuerySize` results with `OpVectorShuffle`. Run spirv-opt exhaustive inlining first so no function parameters carry images |
| Per-eye camera constants | At `OpAccessChain`/`OpLoad` of identified view-UBO members, call a linked helper (`evr_eye_row(rowIndex, r0..r3)`) that returns the corrected row |
| Compute eye | `OpSpecConstant` for the eye; two pipelines |
| Constant delivery | New `OpVariable` Uniform at our reserved set index; interface list only for SPIR-V >= 1.4 |

Author the helpers (matrix fix-ups, UV remaps, tile/cluster remap) in GLSL, compile them offline with
`glslang --no-link` to a SPIR-V library, and link them into game modules at runtime. This is exactly
GPU-AV's `LinkFunction` flow. [C: `reference/vulkan/articles/VVL-gpu_av_shader_instrumentation.md`] It
keeps readable sources for the maths, with microsecond-scale patch cost and no dependence on
decompiler output.

Tooling:

- **SPIRV-Tools**: `spirv-val` in debug builds on every patched module; `opt` passes for inlining, DCE
  and `SwitchDescriptorSet`; `spirv-dis` for logs.
- **SPIRV-Reflect** (or our own small parser): classify bindings, stages and UBO layouts.
- **SPIRV-Headers** for enums.
- **SPIRV-Cross**: *only* as an offline or diagnostic tool, to dump readable GLSL for captured modules
  while we work out which ones need semantic patches.
- **glslang**: offline, for helpers and test fixtures.
- Consider a GPU-AV-style minimal in-house SPIR-V IR (a few hundred lines) over the full
  `spvtools::opt::IRContext`, which is heavy to link into a layer DLL. [U: decide after a prototype]

Keep a small GLSL round-trip escape hatch for rare shaders whose fix is easier to write by hand. Use a
*replacement file keyed by module hash* rather than runtime regex.

### 7.3 Pipeline caches, identifiers and Fossilize

- Pass the game's `VkPipelineCache` through. The driver keys on the (patched) SPIR-V, so our variants
  cache naturally. On top of that, keep our own disk cache of patched SPIR-V keyed by
  `(input hash, transform options, patcher version)`. [C/U]
- `VK_EXT_shader_module_identifier` and `VK_KHR_maintenance5` (inline `VkShaderModuleCreateInfo` in
  `pNext`, `module == VK_NULL_HANDLE`) both let SPIR-V reach pipeline creation without
  `vkCreateShaderModule`. The layer must handle `pNext` SPIR-V, and must hide the identifier extension
  (or fail those pipelines so the game falls back) because identifiers bypass our rewrite. [C: spec
  appendices; Eternal's usage U, probably neither]
- `VK_EXT_graphics_pipeline_library`: if used, the stereo decision must be made per library. The
  pre-raster library needs a `viewMask` that matches the fragment library. [C] Eternal predates it. [U]
- Fossilize / Steam shader pre-caching (`VK_LAYER_VALVE_steam_fossilize`) records whatever reaches it.
  If it sits below us it records our variants, which is harmless. We must never break its replay by
  returning mono-only handles it cannot recreate. [U]

## 8. Single-frame-stereo layer: practices to keep and to avoid

Keep (these work for single-frame stereo in a Vulkan layer):

- Two-layer promotion at `vkCreateImage`/`vkCreateImageView` with a device-owned registry, erased
  before destroy so recycled handles never inherit stereo state.
- A mono and a multiview twin for every render pass and pipeline, chosen at
  `vkCmdBeginRenderPass` from the framebuffer's attachments, with state replay. Mono auxiliary passes
  must use *uncorrected* shader variants.
- Barrier, clear, copy and blit range widening from layer 0 to layers 0-1.
- Indirect compute: two fixed-eye pipelines over the unchanged GPU-owned grid, never rewriting counts.
- Pose pinned per acquired image, and source-pose qualification.
- A per-thread cached device lookup, a shared/exclusive metadata lock, and no allocation on record
  paths; hook cost is otherwise measured in milliseconds per frame.
- Fail loudly on unsupported paths instead of silently rendering one eye.

Pitfalls, and what to do instead:

1. **Borrowed shader profiles.** Our project must be self-contained under MIT: no third-party
   "ShaderSwap"-style profile files, no hash-compatible filenames, no hashes reverse-engineered from
   other stereo drivers such as Vk3DVision.
2. **Coverage gaps.** Eternal needs most of: RenderPass2 and dynamic rendering, imageless framebuffers,
   secondary command buffers with render-pass inheritance, derivative pipelines, `pNext` on set layouts,
   query remap and synchronization2. [Eternal usage U]
3. **Classification.** "Every 2D attachment/storage image is stereo" makes shadow maps, water sims and
   LUTs render twice. Classify images by size relative to the render resolution, usage and debug name
   (`VK_EXT_debug_utils` names if the game sets them [U]). Keep an override table.
4. **Constants.** Use a reserved set and GPU-timeline updates instead of bindings 30/31 and
   `vkDeviceWaitIdle` (section 5).
5. **Rewriter.** Use direct SPIR-V patching instead of GLSL round-trip plus regex (section 7).
6. **Projection.** Use a full 4x4 `C_e` from the real projection instead of the x/y affine form, which
   supports canted headsets.
7. **Sampling clamp.** Drop `textureSize` clamps for sampled reads and rely on hardware layer clamp.
8. **Compute.** Use spec-constant eye variants and two dispatches as the default. Z-doubling (which
   remaps `gl_WorkGroupID`/`gl_NumWorkGroups` and risks the 65535 limit) becomes an optimisation.

## 9. Timing, synchronisation and OpenXR

- **Instance and device creation.** Inject the runtime's required extensions via
  `xrCreateVulkanInstanceKHR`/`xrCreateVulkanDeviceKHR` from our `vkCreateInstance`/`vkCreateDevice`
  hooks. Also enable `multiview`, `timelineSemaphore`, `shaderOutputLayer` (optional) and
  `fragmentShadingRate` (optional) in the device features chain. [C: `XR_KHR_vulkan_enable2`; features
  list is our design]
- **Queue sharing.** The XR graphics binding names one queue, and the runtime submits on it. Game
  submits and runtime submits must be serialised with a mutex, passed to the runtime as queue lock and
  unlock callbacks. Bind XR to the game's present queue so our eye copies are queue-ordered after the frame. [C]
- **Present interception.** In `vkQueuePresentKHR`, submit the copy of both eye layers into the XR
  swapchain image. That submission consumes the present's wait semaphores (binary semaphores can be
  waited once) and signals new ones for the desktop present, if any. The same pattern is used by
  vkBasalt-style post layers. [C] Then `xrReleaseSwapchainImage` and
  `xrEndFrame` with the pose recorded for that frame, never a fresher one.
- **Owning the swapchain.** A "source ring" returns synthetic swapchain images, bypasses desktop
  WSI and keeps them in `GENERAL` layout. Useful, but it blacks out the desktop and needs full layout translation. Start with the real WSI plus a copy,
  and consider the ring later.
- **Pacing.** OpenXR expects `xrWaitFrame` may run on a different thread from `xrBeginFrame`/`xrEndFrame`.
  [C: OpenXR spec, `rendering.adoc`: "a pipelined system may call xrWaitFrame on a
  separate thread from xrBeginFrame and xrEndFrame"] Two workable shapes:
  1. Call `xrWaitFrame`/`xrBeginFrame`/`xrLocateViews` inside the present hook immediately after
     `xrEndFrame`, and publish the predicted pose for the engine's next camera. Simple, and it throttles
     the game naturally. Do not hold the queue mutex while blocked in `xrWaitFrame`.
  2. A dedicated XR thread loops on `xrWaitFrame` and hands predicted poses to an engine hook at the
     start of the game frame. Lower latency if id Tech 7 pipelines simulation ahead of rendering, but
     more moving parts, and a worker thread risks Win32 message deadlocks with the simulator. [U]
  Start with shape 1 and measure pose age (from pose publication to use).
- **Timeline semaphores** (core 1.2) replace fence polling for "frame N's GPU work done": retiring
  per-frame resources, reading timestamps, ordering the async-compute queue against our constant
  updates. One layer-owned timeline value per frame gives a monotonic frame clock across all queues. [C: spec]

## 10. Implications for our design

1. Ship the layer as an implicit layer, dormant unless `ETERNALVR_ENABLE_LAYER=1`, registered in HKCU by
   the launcher, with a process-name check at negotiation. Provide a "clean layers" launch mode.
2. Engine camera = enclosing centred camera. The layer receives `P_c` and per-eye `C_e`, and transforms
   clip space per eye. Culling, binning and gameplay queries stay single and conservative.
3. Image classes: stereo (render-resolution-derived targets), mono (shadow, probe, LUT, sim, UI), and
   ignored (external or runtime-owned, NGX-owned). The class is fixed at creation time; an override table
   is keyed by (format, extent ratio, usage, debug name).
4. Every graphics pipeline gets a mono twin (uncorrected, arrays promoted) and a multiview twin.
   Every stereo compute pipeline gets two spec-constant eye variants.
5. A SPIR-V patcher (in-house IR plus linked GLSL helpers), with `spirv-val` in debug builds, a
   per-module transform log and a disk cache. SPIRV-Cross is for offline diagnosis only.
6. A reserved descriptor set for our constants, updated on the GPU timeline. No `vkDeviceWaitIdle` in the
   frame loop.
7. Full coverage of RenderPass (v1 and v2), dynamic rendering, synchronization2, imageless
   framebuffers, secondary command buffers and query remap, even if the first captures show Eternal uses
   only some of them.
8. Disable at first: RT reflections, DLSS, motion blur, lens flares, possibly the SSR and SSAO temporal
   paths. Re-enable one at a time with per-eye support.
9. UI goes to an OpenXR quad layer from a redirected mono UI pass.
10. Build a capture tool first: dump every SPIR-V module,
    pipeline, render pass, image create and debug name from a real session. Most [U] items resolve from
    one capture.

## 11. Open questions

1. Does DOOM Eternal ship SPIR-V with `OpName`/`OpMemberName` debug names, and does it name Vulkan
   objects with `VK_EXT_debug_utils`? Both decide how much semantic recognition is cheap.
2. Which render-pass API does it use (v1, v2, dynamic rendering)? Secondary command buffers?
   `VK_EXT_graphics_pipeline_library`? How many pipelines and modules at load?
3. Is there a cvar to disable GPU triangle culling, micro-triangle culling or Umbra occlusion, or must
   the culling shader be patched?
4. How are view constants stored (one view UBO block, dynamic offsets into a pool)? Can we identify the
   block by layout instead of names?
5. Does launching `DOOMEternalx64vk.exe` directly trigger a Steam relaunch that drops our environment?
   Does a `steam_appid.txt` avoid it?
6. Does the async-compute queue touch per-eye data (culling does; binning?), and which semaphores order
   it relative to graphics?
7. How are water, the projected grid and caustics handled per eye? Acceptable to keep mono?
8. Does the game ever use all `maxBoundDescriptorSets` slots (validates the reserved-slot plan)?
9. Does multiview on current AMD drivers keep geometry-pass cost near 1x CPU and about 2x vertex, or
   worse?
10. Can DLSS be kept (two NGX features, NGX pipelines excluded), or is FSR1 or native TAA the only stereo
    upscaling path?

## Sources

Primary documents (local copies in `reference/`):
- Vulkan-Loader docs: LoaderLayerInterface.md, LoaderInterfaceArchitecture.md, LoaderApplicationInterface.md, LoaderDebugging.md: https://github.com/KhronosGroup/Vulkan-Loader/tree/main/docs
- Vulkan specification (multiview, queries, dynamic rendering, layer selection, fragment shading rate): https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html
- Vulkan-Docs appendices and proposals for VK_KHR_multiview, dynamic_rendering, descriptor_indexing, fragment_shading_rate, shader_viewport_index_layer, graphics_pipeline_library, shader_module_identifier, maintenance5, timeline_semaphore, synchronization2: https://github.com/KhronosGroup/Vulkan-Docs
- SPIR-V specification and SPV_KHR_multiview: https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html, https://github.com/KhronosGroup/SPIRV-Registry
- GL_EXT_multiview: https://github.com/KhronosGroup/GLSL
- SPIRV-Cross, glslang, SPIRV-Tools, SPIRV-Reflect, SPIRV-Headers, MinHook READMEs and sources: https://github.com/KhronosGroup, https://github.com/TsudaKageyu/minhook
- Vulkan-ValidationLayers GPU-AV docs: https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/main/docs/gpu_av_shader_instrumentation.md, https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/main/docs/gpu_validation.md
- Geffroy, Gneiting, Wang, "Rendering the Hellscape of Doom Eternal", SIGGRAPH 2020: https://advances.realtimerendering.com/s2020/RenderingDoomEternal.pdf
- Vlachos, "Advanced VR Rendering" (GDC 2015) and "Advanced VR Rendering Performance" (GDC 2016): https://media.steampowered.com/apps/valve/2015/Alex_Vlachos_Advanced_VR_Rendering_GDC2015.pdf, https://media.steampowered.com/apps/valve/2016/Alex_Vlachos_Advanced_VR_Rendering_Performance_GDC2016.pdf
- NVIDIA, "Turing Multi-View Rendering in VRWorks": https://developer.nvidia.com/blog/turing-multi-view-rendering-vrworks/
- Unity Manual, Single Pass Instanced rendering: https://docs.unity3d.com/Manual/SinglePassInstancing.html
- Meta, Multi-View: https://developer.oculus.com/documentation/native/android/mobile-multiview/
- ARM, Multiview Rendering (SIGGRAPH 2016): https://community.arm.com/cfs-file/__key/communityserver-blogs-components-weblogfiles/00-00-00-20-66/5_2D00_mmg_2D00_siggraph2016_2D00_multiview_2D00_cass.pdf
- UEVR documentation, rendering methods: http://docs.uevr.io/usage/overview.html
- Vk3DVision: https://github.com/helifax/Vk3DVision-Public, https://3dsurroundgaming.com/Vk3DVision.html
- 3DMigoto separation/convergence discussions: https://github.com/bo3b/3Dmigoto/issues/66, https://github.com/bo3b/3Dmigoto/issues/97
- Matt Stevens, "Bad Vulkan Layers": https://www.mattstevens.co.uk/posts/bad-vulkan-layers/
- LunarG, "The Vulkan Loader and Vulkan Layers: Diagnosing Layer Issues": https://www.lunarg.com/wp-content/uploads/2022/12/The-Vulkan-Loader-and-Vulkan-Layers_-Diagnosing-Layer-Issues.pdf
- Steam overlay multi-device bug: https://github.com/ValveSoftware/steam-for-linux/issues/9120
- DOOM Eternal unsupported-layer dialog: https://steamcommunity.com/app/782330/discussions/0/4699035627968057488/
- DOOM Eternal RT/DLSS update (Vulkan ray tracing, June 2021): https://www.thefpsreview.com/2021/06/29/nvidia-dlss-and-ray-tracing-upgrade-available-now-for-doom-eternal/, https://www.nvidia.com/en-us/geforce/news/june-2021-rtx-dlss-game-update/
- Fossilize, vkBasalt: https://github.com/ValveSoftware/Fossilize, https://github.com/DadSchoorse/vkBasalt
