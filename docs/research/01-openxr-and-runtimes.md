# 01: OpenXR and VR runtimes

Research notes, 2026-09-25. Scope: the OpenXR API surface we need, how PC runtimes differ, how the loader and third-party layers get in the way, which runtime quirks bite a layer that shares the game's device, and what that means for EternalVR's layer-injected, single-frame stereo design.

Tags: **[C]** confirmed against a cited source (spec text, runtime source, inventory data, or open-source code). **[U]** uncertain: community report, inference, or not yet tested by us.

Local references live in `reference/openxr/` (spec chapters, loader docs, `xr.xml`, VDXR wiki, runtime inventory, fetched web pages). Large or cloned items are in `reference/_cache/` and are recreated by `reference/openxr/fetch.sh`.

---

## 1. Core API: what we use and the rules that bite

### 1.1 Instance, version, loader

- OpenXR is at **1.1.63** in the registry we pulled [C: `reference/openxr/registry/xr.xml`, `OpenXR-Docs/specification/current_version.ini`]. 1.1 promoted `XR_EXT_local_floor`, `XR_EXT_palm_pose` (input renamed `grip_surface`), `XR_VARJO_quad_views` (renamed `XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO_WITH_FOVEATED_INSET`, and optional), `XR_FB_touch_controller_pro` (now `/interaction_profiles/meta/touch_pro_controller`), `XR_META_touch_controller_plus` (now `/meta/touch_plus_controller`), `XR_EXT_hp_mixed_reality_controller`, `XR_EXT_samsung_odyssey_controller`, the HTC Cosmos/Focus3, ML2 and ByteDance controller profiles, `XR_EXT_uuid`, `XR_KHR_locate_spaces` and `XR_KHR_maintenance1` [C: `spec-chapters/versions.adoc`, `promotedto="XR_VERSION_1_1"` in `xr.xml`]. 1.1 also split the Touch profile into per-generation legacy profiles (`meta/touch_controller_rift_cv1`, `..._quest_1_rift_s`, `..._quest_2`) [C].
- VDXR implements 1.0 and 1.1 [C: `vdxr/README.md`]. Whether SteamVR and Meta PC expose 1.1 core is not stated in the inventory data [U]. Practical rule: request `XR_API_VERSION_1_1`, and on `XR_ERROR_API_VERSION_UNSUPPORTED` retry with 1.0 and the equivalent extensions.
- DOOM Eternal does not ship an OpenXR loader, so we ship `openxr_loader.dll` (Apache-2.0) next to our layer and load it explicitly by path, falling back to the system search path.

### 1.2 Session lifecycle

States: `IDLE → READY → (xrBeginSession) → SYNCHRONIZED → VISIBLE → FOCUSED`, back through `STOPPING → (xrEndSession) → IDLE`, plus `LOSS_PENDING` and `EXITING` [C: `spec-chapters/session.adoc`]. Consequences for us:

- Input (`xrSyncActions`) only yields data when **FOCUSED**. When a runtime dashboard (SteamVR, Meta universal menu) takes focus we drop to VISIBLE: keep submitting frames, pause game input, ideally pause the game [C].
- `LOSS_PENDING` means the XR instance must be torn down and recreated (headset unplugged, runtime restart). If the session is bound to the game's `VkDevice`, recovering means recreating objects that the game owns; see section 4 for why this pushes us toward a decoupled binding [C spec / design inference].
- `XrEventDataReferenceSpaceChangePending` fires on recentre; LOCAL/LOCAL_FLOOR origins move and our cached "playspace yaw" must reset [C].

### 1.3 Frame loop

- `xrWaitFrame` throttles the app and returns `predictedDisplayTime`, `predictedDisplayPeriod` and `shouldRender`. It **may be called on a different thread** from `xrBeginFrame`/`xrEndFrame`, but calls must not overlap and must be paired [C: `spec-chapters/rendering.adoc:818-936`].
- `xrBeginFrame` can return `XR_FRAME_DISCARDED`; when `shouldRender` is false we still call `xrEndFrame` with zero layers [C: `rendering.adoc:882-962`].
- Poses come from `xrLocateViews(predictedDisplayTime)`, and the same `displayTime` goes into `XrFrameEndInfo`. id Tech 7 renders frame N while simulating N+1, so the camera must get the pose located for the display time of the frame being rendered, and we must submit the pose we actually rendered with; the runtime reprojects from it [C spec; design inference].
- One workable shape runs `xrWaitFrame + xrBeginFrame` on a dedicated lifecycle thread, triggered when the game calls `vkAcquireNextImageKHR`, and submits at Present. SteamVR can repeat a `predictedDisplayTime`; the fix is to detect it and submit an empty frame instead of a duplicate [U].

### 1.4 Views and projection

- We use `XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO`. `xrEnumerateViewConfigurationViews` gives per-eye recommended sizes; SteamVR's value already includes the user's SteamVR resolution slider, so a mod's own render-scale control should step aside on SteamVR rather than multiply it [U].
- `XrFovf` is **asymmetric** and per-eye (four tan-angles). View poses can carry **per-eye rotation** (canted displays: Pimax, some Varjo modes), so the eye transform is a full pose, not "head ± IPD/2" [C spec; canting is a known Pimax property, U for exact models].
- The FOV in `XrCompositionLayerProjectionView` describes what was *rendered*; it need not equal the located FOV. Rendering a symmetric frustum that encloses the asymmetric one and submitting that symmetric FOV is legal and correct, at the cost of wasted pixels [C spec]. When building the enclosing frustum, averaging the two half-angles leaves an uncovered strip; the max of both sides is needed.
- Hidden-area mesh via `XR_KHR_visibility_mask` is available on SteamVR, Meta PC, WMR, Varjo and VDXR [C: `inventory/pc-runtime-extension-matrix.md`]; stenciling it out saves roughly 10-20% of pixels on typical lenses [U: generic figure, depends on HMD].

### 1.5 Swapchains and colour

- Runtimes treat `*_SRGB` formats as sRGB-encoded and everything else as linear [C: `rendering.adoc:27-33`]. DOOM Eternal's final tonemapped image is almost certainly sRGB-encoded data in a UNORM image [U]; copying it byte-for-byte into a `R8G8B8A8_UNORM` swapchain would look washed out. Options: create an `_SRGB` swapchain and write through a UNORM view (`XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT` → `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT`; `XR_KHR_vulkan_swapchain_format_list` exists but no PC runtime in the inventory advertises it), or do the copy in a tiny shader. The shader copy is simplest and also lets us crop/scale [C spec; U for DOOM's format].
- SteamVR's D3D12 swapchain formats include `R8G8B8A8_UNORM_SRGB`, `B8G8R8A8_UNORM_SRGB`, `R16G16B16A16_FLOAT`, `R10G10B10A2_UNORM` and depth `D32_FLOAT`, `D16_UNORM`, `D24_UNORM_S8_UINT`, `D32_FLOAT_S8X24_UINT` [C: `web/steamvr-openxr-supported-features-thread.md`]. Always pick from `xrEnumerateSwapchainFormats`, never assume.

### 1.6 Composition layers

- **Projection** and **quad** layers are core and universal. Runtimes must support at least 16 layers [C spec].
- **Cylinder** (`XR_KHR_composition_layer_cylinder`) is on Meta PC, VDXR and Monado, **not on SteamVR** (still listed unsupported as of the Aug 2026 edit of the community list, and absent from the SteamVR 2.14.4 inventory) and not on WMR or Varjo [C: matrix; `web/steamvr-openxr-supported-features-thread.md`]. Since SteamVR is the largest runtime by headset coverage, a cylinder HUD/menu needs a **quad fallback** (or a curved mesh we render ourselves into the projection layer).
- **Equirect/cube**: not needed (no skybox layers), and SteamVR lacks them.
- Layers are composited in submission order with `XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT` for alpha; UI layers need premultiplied vs unpremultiplied handled via `XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT` [C spec].
- A **world-locked** menu uses a LOCAL/STAGE space pose; a **head-locked** reticle or subtitle uses VIEW space. Putting every full-frame screen (menus, cutscenes, end-of-level screens) on one quad at a fixed distance, for example 2 m x 2 m at 2 m, keeps them from jumping in depth.

### 1.7 Depth submission

- `XrCompositionLayerDepthInfoKHR` chains to each projection view: a depth swapchain sub-image plus `minDepth/maxDepth/nearZ/farZ`. **Reversed-Z is expressed as `nearZ > farZ`**, and `farZ` may be `+infinity` [C: `extensions/khr_composition_layer_depth.adoc:62-128`]. For an infinite reversed-Z projection (depth 1 at the near plane, 0 at infinity) that means `nearZ = +inf, farZ = zNear` given `minDepth=0, maxDepth=1` [C derived from the spec's mapping; U until verified on a runtime].
- Every PC runtime advertises the extension, but that does not mean it uses the data: the community SteamVR list notes depth is "only passed on to third party Oculus/WMR/etc. drivers, and usable by API layers" [C: SteamVR thread]. Meta PC uses depth for positional timewarp/ASW [U: long-standing Meta guidance, not re-verified here].
- Recommendation: submit depth when cheap (we already have the game's depth buffer; a copy/convert per eye), make it a setting, default on for Meta/VDXR.

### 1.8 Action system and interaction profiles

- Actions are created in action sets, bindings are **suggested per interaction profile**, action sets are attached to the session once, and the runtime picks the profile for the physical controller. Suggesting more profiles is strictly better: runtimes remap only when nothing matching was suggested [C: `spec-chapters/input.adoc`].
- Profiles we should suggest (paths from `xr.xml`): `oculus/touch_controller`, `meta/touch_plus_controller` and `meta/touch_pro_controller` (1.1, or via the FB/META extensions on 1.0), `valve/index_controller`, `htc/vive_controller`, `microsoft/motion_controller` and `hp/mixed_reality_controller` (Reverb G2, via `XR_EXT_hp_mixed_reality_controller` on 1.0), `htc/vive_cosmos_controller`, `htc/vive_focus3_controller`, `bytedance/pico4_controller` (via `XR_BD_controller_interaction` on 1.0), `khr/simple_controller` as last resort, and `khr/generic_controller` when a runtime lists `XR_KHR_generic_controller` (ratified; no PC runtime advertises it yet per inventory, the SteamVR community list claims support) [C paths; U SteamVR generic support].
- **There is no PSVR2 Sense profile in the registry** [C: no Sony profile in `xr.xml`]. On SteamVR the Sense controllers bind through SteamVR's rebinding layer from whichever profile we suggest (Touch/Index), so good Touch and Index bindings are what PSVR2 users get [U: SteamVR remap behaviour]. Adaptive triggers are only reachable through PSVR2 Toolkit's own IPC, which a mod can integrate as an optional bridge [U]. PSVR2 eye tracking reaches OpenXR only through third-party layers at the time of writing [U: UploadVR / BattleAxeVR repo].
- **Steam Frame** (released 2026-09-18 per Wikipedia [U]) streams from SteamVR on PC; its controllers present as Touch by default, with a Frame-specific profile from `XR_VALVE_frame_controller_interaction` that is not yet in the public registry [C: Steamworks Steam Frame "custom engines" page; U for the exact path]. Touch bindings therefore cover it on day one.
- Useful input extensions on SteamVR: `XR_EXT_dpad_binding` (turn thumbstick into 4-way buttons, good for weapon wheel), `XR_VALVE_analog_threshold` (tune trigger/grip click points), both require `XR_KHR_binding_modification` [C: matrix]. `XR_EXT_active_action_set_priority` lets a "menu" action set shadow gameplay bindings (SteamVR, Meta, VDXR) [C].
- **Haptics**: `xrApplyHapticFeedback` with `XrHapticVibration` (duration, frequency, amplitude) is core and universal. `XR_FB_haptic_pcm`/`amplitude_envelope` are Meta-PC only; `XR_EXT_haptic_parametric` is new and unadvertised [C: matrix, `extensions/ext_haptic_parametric.adoc`]. Mapping the game's XInput rumble into OpenXR haptics per hand is the natural approach for DOOM Eternal.

### 1.9 Reference spaces

- `LOCAL` (seated origin at recentre), `STAGE` (floor, play-area centre), `VIEW` (head), and `LOCAL_FLOOR` (LOCAL origin at floor height). `LOCAL_FLOOR` is mandatory in 1.1; on 1.0 it needs `XR_EXT_local_floor`, which SteamVR, Meta PC and VDXR advertise but WMR and Varjo do not [C: `versions.adoc:49-60`, matrix]. The spec explicitly says LOCAL_FLOOR guarantees nothing about floor-estimation quality [C].
- Plan: gameplay in `LOCAL_FLOOR` (or `STAGE`, else `LOCAL` plus a height calibration), with our own yaw offset for snap/smooth turn applied in the game camera rather than by moving spaces. Whichever space is used, `ReferenceSpaceChangePending` must be handled for it [C spec].

### 1.10 Eye tracking and foveated rendering on PC

- `XR_FB_foveation` and `XR_META_foveation_eye_tracked` are **standalone-Quest/Android** features: no PC runtime in the inventory or VDXR advertises them [C: matrix]. Steam Frame's docs list them for *native* Android/Linux apps only [C: Steamworks page].
- Quad views (`PRIMARY_STEREO_WITH_FOVEATED_INSET` / `XR_VARJO_quad_views`): native on Varjo and on Pimax Play's integrated runtime [C Varjo inventory; C Pimax announcement, U for current release state]. VDXR recognises the enum but returns `XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED` [C: `VirtualDesktop-OpenXR/virtualdesktop-openxr/system.cpp:274-280`]. The `Quad-Views-Foveated` API layer emulates it elsewhere [C: its README]. Quad views means four views, which conflicts with a 2-view multiview/shader-rewrite path; treat it as a later option.
- Eye gaze (`XR_EXT_eye_gaze_interaction`) is on SteamVR, WMR (Omnicept), Varjo and VDXR; Meta PC only exposes `XR_FB_eye_tracking_social` in developer mode [C: matrix]. The PC-realistic foveation path is therefore **our own variable-rate shading** (`VK_KHR_fragment_shading_rate` on DOOM's passes) steered by eye gaze where available and fixed otherwise. Encoder-side "foveated streaming" (Steam Frame via Steam Link, Virtual Desktop) is transparent to us [C Wikipedia/Steam Frame]. Quest Pro users report blur with VD foveated streaming [U].

### 1.11 Hand tracking

`XR_EXT_hand_tracking` is on SteamVR (driver-provided skeletons), WMR, Varjo, VDXR and Meta PC **developer mode only** [C: matrix, `inventory/meta_pc_dev_mode.json`]. For an FPS it is a nice-to-have (finger curl on Index/Touch via skeletal data), not a v1 requirement.

---

## 2. Vulkan binding: `XR_KHR_vulkan_enable` vs `enable2` when the device is the game's

This is the single most important runtime question for our architecture.

**enable (v1).** The app asks `xrGetVulkanInstanceExtensionsKHR` / `xrGetVulkanDeviceExtensionsKHR` for extension *strings*, adds them to its own `vkCreateInstance`/`vkCreateDevice`, and must use the physical device from `xrGetVulkanGraphicsDeviceKHR` [C: `extensions/khr_vulkan_enable.adoc`]. Problems:
- Only extensions, not features. VDXR (and the OpenXR-Vk-D3D12 layer) require the **timeline semaphore feature** to be enabled by the app when enable1 is used [C: `vdxr/Developers.md` note 1; OpenXR-Vk-D3D12 README].
- SteamVR over-reports device extensions (deprecated `VK_EXT_debug_marker`, `VK_NV_*` on AMD); querying the graphics device first shrinks the list, and unavailable optional entries must be filtered [C: `web/steamvr-openxr-vulkan-extension-list-thread.md`, Proton issue #7737 shows SteamVR's list: external_memory, external_semaphore, dedicated_allocation, get_memory_requirements2, external_memory_win32, external_semaphore_win32, debug_marker].

**enable2.** The runtime creates the objects: `xrCreateVulkanInstanceKHR` and `xrCreateVulkanDeviceKHR` take the app's create-info and `pfnGetInstanceProcAddr`, merge in whatever the runtime needs (extensions *and* features), and call Vulkan themselves [C: `khr_vulkan_enable2.adoc:123-311`]. The binding's valid usage requires the instance and device **to have been created through those calls** [C: `khr_vulkan_enable2.adoc:379-386`]. That is the "runtime must create the device" issue: an injector cannot legally hand the runtime a device the game already made.

**How injectors cope.**
1. *Hijack creation.* The Vulkan layer intercepts DOOM's `vkCreateInstance`/`vkCreateDevice` and forwards them to `xrCreateVulkanInstanceKHR`/`xrCreateVulkanDeviceKHR`, passing the **next layer's** GIPA so the runtime's nested `vkCreateDevice` does not recurse into us. DOOM then runs on a runtime-created device. Cost: the OpenXR instance must exist before DOOM's first `vkCreateInstance` (runtime must be up at game launch), re-entrancy and dispatch-level bugs, and a long tail of runtime-specific code (section 5).
2. *enable1 + feature injection.* Append the runtime's extension list and chain `VkPhysicalDeviceTimelineSemaphoreFeatures` into DOOM's `vkCreateDevice`. Simpler, but Fred Emmott's guidance is "use enable2, never mix", and Meta is reported to require runtime-managed enable2 device creation [C: `web/fredemmott-openxr-api-layer-best-practices.md` §8; U on Meta and on why it refuses enable1 in that setup].
3. *Decouple (BotW-BetterVR).* A Vulkan-layer VR mod for Cemu creates its OpenXR session with **`XR_KHR_D3D12_enable`**, adds `VK_KHR_external_memory(_win32)`, `VK_KHR_external_semaphore(_win32)`, `VK_KHR_timeline_semaphore` (+ feature) and sync2 to the game's `vkCreateDevice`, allocates shared textures and a shared fence on D3D12, imports them into Vulkan, renders/copies into them from the game's queue, then copies into the XR swapchain on a D3D12 queue [C: `_cache/BotW-BetterVR/src/hooking/layer.cpp:227-370`, `src/rendering/texture.cpp:320-451`]. A Vulkan-Loader issue from 2026-03 describes exactly why layer authors do this: the runtime calls `vkGetPhysicalDevice*` through pointers from a different dispatch level with the layer's wrapped handle and the loader rejects it; the reporter cites BetterVR's D3D12 route as the workaround [C: KhronosGroup/Vulkan-Loader#1881]. Any layer on this path hits the same class of problem; compare GPUs by vendor/device ID and `deviceUUID`, never by raw `VkPhysicalDevice` value.

**Queue sharing.** With a Vulkan binding, the runtime may use our `VkQueue` inside `xrBeginFrame`, `xrEndFrame`, `xrAcquireSwapchainImage` and `xrReleaseSwapchainImage`, and the app must externally synchronise those calls with every other use of that queue [C: `khr_vulkan_enable2.adoc:400-420`]. id Tech 7 submits from job threads, so a shared queue needs lock callbacks around every runtime call. A D3D12 binding removes this entire hazard: the runtime never touches DOOM's queues.

**What runtimes do with Vulkan internally.** VDXR implements Vulkan by **D3D11 interop**: it requires `VK_KHR_external_memory_win32`, `VK_KHR_external_semaphore_win32`, `VK_KHR_timeline_semaphore` (+feature), dedicated allocation, imports a D3D11 fence as a timeline semaphore, and checks the Vulkan device LUID against its adapter LUID [C: `_cache/VirtualDesktop-OpenXR/virtualdesktop-openxr/vulkan_interop.cpp:44-93, 300-330, 440-500`]. The OpenXR-Vk-D3D12 layer does the same on top of D3D12-only runtimes (legacy WMR) [C: its README]. So on VDXR a "native" Vulkan session is already a D3D interop session under the hood.

---

## 3. Runtimes and their quirks

### 3.1 Runtime-quirk matrix

| Runtime | Headsets (PC) | Vulkan | Cylinder | Depth used | Eye gaze | Notable quirks | Status |
|---|---|---|---|---|---|---|---|
| **SteamVR OpenXR** (2.14.x) | Index, Vive, Pimax (SteamVR mode), Pico (Streaming Assistant), PSVR2, WMR via Oasis, Quest via Steam Link/ALVR/VD, Steam Frame | enable + enable2 [C] | **No** [C] | Forwarded to third-party drivers only [C] | Yes [C] | Over-reported Vulkan extension list [C]; creates a nested Vulkan instance `steamvr_vrclient_interop` in-process that our layer sees [U]; render-scale slider baked into recommended size [C]; duplicate `predictedDisplayTime` reported [U]; binding UI can remap anything [C]; name contains "Meta compatibility mode" when fronting a Quest via Steam Link [U] | Current, broadest coverage |
| **Meta PC (Quest Link / Air Link)** 1.205 | Quest 2/3/3S/Pro, Rift S | enable + enable2 [C] | Yes [C] | Yes (ASW) [U] | No (social ET in dev mode only) [C] | Hand/body tracking only in developer mode [C]; a shared-device layer needs runtime-managed enable2 and must defer session creation until DOOM's startup swapchain churn settles, or the shared device is lost [U]; FB haptics PCM [C] | Current |
| **VDXR** (Virtual Desktop) | Quest family, Pico 4/Neo 3, Vive Focus 3/XR Elite, Play For Dream, Galaxy XR | enable + enable2 via D3D11 interop [C] | Yes [C] | Yes [C] | Yes (Quest Pro etc.) [C] | Timeline semaphore feature required for enable1 [C]; LUID must match [C]; stereo only (foveated inset rejected) [C]; a shared-device layer needs a custom enable2 "bridge" GIPA, and a symmetric centred FOV if the game camera cannot take an asymmetric one [U]; not officially conformant (fee) [C] | Current, open source (MIT) |
| **Pimax Play** (integrated) / PimaxXR | Crystal, Crystal Light, 5K/8K | [U] (PimaxXR shares VDXR's D3D11 interop lineage [U]) | [U] | [U] | Crystal [U] | Canted displays, "parallel projections" compatibility option exists [U]; quad views native in Pimax Play [C announcement] | Pimax Play runtime supersedes PimaxXR [C/U] |
| **WMR** (Microsoft) | Reverb G2, Odyssey | **No Vulkan** (D3D11/12 only) [C] | No [C] | Yes [C] | Omnicept [C] | Removed in Windows 11 24H2 [C]; Vulkan apps need OpenXR-Vk-D3D12 layer [C]; users on 24H2 use SteamVR + Oasis driver [C OpenXR-Vk-D3D12 README] | Legacy |
| **Varjo** | Aero, XR-3/4 | enable + enable2 [C] | No [C] | Yes [C] | Yes [C] | Native quad views + foveated rendering ext [C] | Niche |
| **ALVR** | Quest, Pico etc. | via SteamVR (driver) [C] | as SteamVR | as SteamVR | as SteamVR | Not an OpenXR runtime on Windows; inherits SteamVR behaviour [C: VDXR wiki comparison lists Steam Link/Pico/VD under SteamVR] | Current |
| **PICO Connect** | Pico 4 / 4 Ultra | [U] | [U] | [U] | [U] | Ships an *experimental* built-in OpenXR runtime; most users run SteamVR [U: DCS forum] | Experimental |
| **OpenComposite** | n/a | n/a | n/a | n/a | n/a | Translates OpenVR apps to OpenXR; irrelevant to a native OpenXR app [C] | n/a |

### 3.2 Third-party OpenXR API layers

- **OpenXR Toolkit** is discontinued since 2024; its author recommends against installing it and its rendering features do not support Vulkan [C: `web/openxr-toolkit-site-index.md`], yet it remains an implicit layer on many machines and is a common first suspect in VR performance complaints [U]. **ReShade's OpenXR layer** has been reported to stop injected VR mods from starting, and OpenKneeboard is also a known source of trouble [U: community reports].
- Legitimate layers users may run: Quad-Views-Foveated, OpenXR-Vk-D3D12, PSVR2 eye-tracking layers [C].

---

## 4. The OpenXR loader

- **Runtime selection** (Windows): `XR_RUNTIME_JSON` env var overrides everything; otherwise `HKLM\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime` (a manifest path; 32-bit apps read `WOW6432Node`). Installed runtimes list themselves under `...\AvailableRuntimes` (DWORD 0 = discoverable); tools must not edit that list to switch runtimes [C: `loader/runtime.adoc:86-120, 420-460, 677-703`].
- **API layers**: implicit layers under `HKLM\...\ApiLayers\Implicit` and `HKCU\...\ApiLayers\Implicit`; value name = manifest path, DWORD 0 = enabled, non-zero = disabled. Manifests can declare `enable_environment` / `disable_environment` variables. `XR_API_LAYER_PATH` overrides explicit-layer search, `XR_ENABLE_API_LAYERS` enables explicit layers [C: `loader/api_layer.adoc:174-222, 342-372, 500-640`]. Layer ordering is only controllable within one hive; Fred Emmott recommends HKLM only, signed DLLs, and graceful degradation on runtimes a layer does not support [C: fetched best-practices page].
- **Practical consequences for our launcher**: (a) because we load our own `openxr_loader.dll`, all of the above still applies; (b) the launcher can offer a per-launch runtime choice by setting `XR_RUNTIME_JSON` for the DOOM process instead of rewriting the system `ActiveRuntime` [C mechanism; design choice]; (c) the launcher should **enumerate implicit layers** in both hives, show them, and offer to set `DISABLE_*` environment variables (where the manifest declares one) for the game process only, rather than editing the registry [C mechanism]; (d) log the manifest path, `xrGetInstanceProperties` runtime name/version, and the enabled layer list on every start.
- **Runtime identification**: a runtime can be classified by substring-matching the manifest path ("virtualdesktop", "oculus", "vdxr4steam", "steamxr") and by the reported runtime name. We should key quirks on `XrInstanceProperties::runtimeName` + version first and manifest path second, and keep every quirk behind a named flag that can be forced from config.

---

## 5. Runtime quirks for a layer that shares the game's device

What an injected layer has to handle when the OpenXR session runs on the game's own Vulkan device [U: not yet tested by us].

1. **Runtime detection** from `XR_RUNTIME_JSON` or `HKLM\...\ActiveRuntime` (with `REG_EXPAND_SZ` expansion) → `VirtualDesktop | MetaOculus | SteamVR | VDXR4Steam | Unknown`.
2. **Vulkan path per runtime**: VD with mediation → enable2 + custom "VD bridge" GIPA; SteamVR-backed → enable2 runtime-managed, enable1 fallback; Meta → enable2 runtime-managed (required); unknown → enable2 then enable1.
3. **DOOM's instance/device are created by the runtime** via `xrCreateVulkanInstanceKHR`/`xrCreateVulkanDeviceKHR`, with the downstream GIPA handed to the runtime to prevent recursion.
4. **SteamVR auxiliary Vulkan instance** named `steamvr_vrclient_interop` (or any nested create on a Steam-backed runtime) is passed straight through, otherwise the layer re-initialises OpenXR recursively while holding its own mutex.
5. **After device creation on SteamVR**, the retained GIPA is switched to the public loader's for session-time dispatch; physical-device handles must stay at the dispatch level that enumerated them.
6. **GPU identity** checked by vendor/device ID and `deviceUUID`; mismatches refuse to bind rather than fall back.
7. **enable1**: call `xrGetVulkanGraphicsDeviceKHR` before `vkCreateDevice`.
8. **Deferred session creation on VD and Meta** until DOOM has completed N stable presents; creating eye swapchains during DOOM's startup swapchain replacement lost the shared device.
9. **Early swapchain-replacement recovery** limited to the first 8 submitted frames after `VK_ERROR_DEVICE_LOST` races.
10. **Frame-loop threading on SteamVR**: wait/begin on a dedicated thread at acquire time, end at present; duplicate predicted display times replaced by empty frames.
11. **Image release timing**: early `xrReleaseSwapchainImage` after queue submission only on Steam/VD, with a private copy fence; otherwise queue-idle.
12. **Queue access locks** around runtime calls that touch DOOM's queue.
13. **FOV**: Virtual Desktop gets a centred symmetric frustum enclosing both eyes (max half-angle, not average); others use asymmetric crop.
14. **Render scale** owned by SteamVR on SteamVR.
15. **SteamVR "Meta compatibility mode"** detected from runtime name when a Quest is streamed via Steam Link.
16. **User-facing guidance**: Quest/Pico → VDXR; Index/PSVR2 → SteamVR; disable OpenXR Toolkit and ReShade's OpenXR layer; disable VD foveated streaming on Quest Pro.

The pattern: most of this list exists because the OpenXR session shares DOOM's `VkInstance`/`VkDevice`/`VkQueue`. Items 3-8, 10-12 and 15 either disappear or become ordinary code if the session is on its own device.

---

## 6. Recommended OpenXR extensions

**Required (fail with a clear message if absent)**
- One graphics binding. Recommended: `XR_KHR_D3D12_enable` (see section 7); alternative path `XR_KHR_vulkan_enable2`. Every PC runtime in the inventory has D3D12 [C].

**Strongly recommended (use when present, degrade gracefully)**
- `XR_KHR_composition_layer_depth` (reprojection quality on Meta/VDXR).
- `XR_KHR_composition_layer_cylinder` (UI; quad fallback on SteamVR/WMR/Varjo).
- `XR_KHR_visibility_mask` (hidden-area stencil).
- `XR_EXT_local_floor` on 1.0 runtimes (core in 1.1).
- `XR_KHR_win32_convert_performance_counter_time` (correlate QPC timing with XR time; universal).
- `XR_FB_display_refresh_rate` (show/choose refresh rate; SteamVR, Meta, VDXR, WMR).
- Controller profiles on 1.0 runtimes: `XR_EXT_hp_mixed_reality_controller`, `XR_HTC_vive_cosmos_controller_interaction`, `XR_HTC_vive_focus3_controller_interaction`, `XR_BD_controller_interaction`, `XR_META_touch_controller_plus`, `XR_FB_touch_controller_pro`; `XR_KHR_generic_controller` when listed.
- `XR_KHR_binding_modification` + `XR_EXT_dpad_binding` + `XR_VALVE_analog_threshold` (SteamVR input polish).
- `XR_EXT_active_action_set_priority` (menu vs gameplay action sets).
- `XR_EXT_palm_pose` on 1.0 (weapon grip alignment; `grip_surface` in 1.1).

**Optional / later**
- `XR_EXT_eye_gaze_interaction` (gaze-driven VRS; SteamVR, VDXR, Varjo, WMR).
- `XR_EXT_hand_tracking` (finger curl, not required).
- `XR_EXT_user_presence` (pause on headset removal; SteamVR, Varjo).
- `XR_META_recommended_layer_resolution`, `XR_META_performance_metrics` (SteamVR/Meta telemetry).
- `XR_EXT_debug_utils` (development builds only).
- Quad views (`PRIMARY_STEREO_WITH_FOVEATED_INSET`) for Varjo/Pimax Play, only if the renderer ever supports four views.
- `XR_EXT_frame_synthesis` (app motion vectors for runtime frame generation): interesting given DOOM has motion vectors for TAA, but no PC runtime advertises it yet [C: matrix].

**Not planned**: `XR_FB_foveation`, `XR_META_foveation_eye_tracked` (Android only), `XR_FB_passthrough`, equirect/cube layers, `XR_KHR_vulkan_enable` (legacy; only as last-resort fallback if enable2 is chosen and missing, which no current runtime needs).

---

## 7. Implications for our design

**D1. Run the OpenXR session on its own D3D12 device, not on DOOM's Vulkan device (recommended primary path).**
Rationale: (a) the spec does not let us bind a device the game created under enable2, and enable1 is legacy with known extension-list problems; (b) the runtime would share DOOM's multi-threaded queue and need external locks; (c) session creation would be tied to DOOM's `vkCreateInstance` time, so the runtime has to be up before the game starts and a lost session cannot be recovered without the game recreating its device; (d) SteamVR's in-process Vulkan instance and the loader dispatch-level bug disappear; (e) VDXR, the runtime most Quest users will pick, converts Vulkan to D3D11 internally anyway, and legacy WMR becomes supported for free; (f) BotW-BetterVR demonstrates the pattern in a shipping Vulkan-layer mod. Cost: one extra GPU copy per eye (shared texture → XR swapchain on a D3D12 queue, roughly two 2k x 2k RGBA copies per frame [U: measure]), a shared D3D12 fence imported as a Vulkan timeline semaphore, and a LUID match between DOOM's physical device and `xrGetD3D12GraphicsRequirementsKHR::adapterLuid`.
Required change to DOOM's device in our layer regardless of path: add `VK_KHR_external_memory_win32`, `VK_KHR_external_semaphore_win32` (and their base extensions), `VK_KHR_dedicated_allocation`, and enable `timelineSemaphore` at `vkCreateDevice` if DOOM did not. This is one-time, runtime-independent, and does not need OpenXR to be running.

**D2. Keep a Vulkan `enable2` path as an experiment, not the default.** If the copy/sync cost proves significant on a benchmark, try hijacked creation (section 2) behind a flag. Do not ship enable1.

**D3. Frame loop on a dedicated XR thread.** `xrWaitFrame` there; publish `predictedDisplayTime` + located views to the render-side hook; `xrBeginFrame` before our first GPU work for the frame; `xrEndFrame` after the copy fence is submitted. Always submit the pose/FOV we actually rendered with, and handle `shouldRender == false` and `XR_FRAME_DISCARDED`.

**D4. Asymmetric per-eye projection.** Since we rewrite the projection in shaders, inject the true off-axis projection from `XrFovf` and the full per-eye pose (support canted displays). Keep a "symmetric enclosing frustum + crop" mode as a fallback, using the max half-angle rule from section 1.4.

**D5. UI layers.** Menus and HUD capture go to cylinder layers where supported, quads elsewhere, chosen at runtime from the enabled extensions; one fixed menu distance (2 m) to avoid depth jumps. Head-locked elements in VIEW space, menus world-locked in LOCAL_FLOOR recentred on open.

**D6. Spaces.** LOCAL_FLOOR → STAGE → LOCAL+calibration. Turning is applied in the game camera, not by recreating spaces. Handle `ReferenceSpaceChangePending`.

**D7. Input.** Suggest bindings for every profile listed in section 1.8 from one data file; per-runtime binding polish via dpad/analog-threshold when present; separate "menu" and "gameplay" action sets with priority. Rumble from the game's XInput path feeds `xrApplyHapticFeedback`. PSVR2 adaptive triggers through PSVR2 Toolkit as an optional module.

**D8. Runtime quirks are data, not code paths.** Identify runtime by name/version (manifest path as secondary), and map to named quirk flags that config can override. Log runtime, version, manifest path, enabled extensions and active implicit layers at startup.

**D9. Launcher responsibilities.** Offer runtime choice via `XR_RUNTIME_JSON` per launch; detect and warn about OpenXR Toolkit, ReShade's OpenXR layer and other implicit layers, and offer to disable them for the game process via their `disable_environment` variables; never edit the user's ActiveRuntime without asking.

**D10. Colour and depth.** Choose an `_SRGB` swapchain and write via a small copy shader that also crops; submit depth as reversed-Z (`nearZ=+inf, farZ=zNear`) behind a setting, default on for Meta and VDXR.

**Recommended support tiers for v1**: SteamVR (Index, Vive, Pico, PSVR2, WMR-via-Oasis, Steam Frame, Quest via Steam Link), Meta PC (Link/Air Link), VDXR. Pimax Play and Varjo best-effort. WMR native only if the D3D12 path lands (it then works without extra work).

### Open questions

1. What Vulkan API version and device features does DOOM Eternal request at `vkCreateDevice`, and does it already enable timeline semaphores / sync2? (Needs a capture; affects D1's injection code.)
2. Does DOOM Eternal ever pick a GPU other than the HMD's adapter on hybrid laptops, and can we steer its physical-device choice so the LUIDs match?
3. Measured cost of the extra D3D12 copy and cross-API fence wait at 2x 2160x2160 @ 90-120 Hz on NVIDIA and AMD; Intel Arc has a reported crash importing D3D11 shared textures under VDXR [U: Osiris-VR-Viewer issue #5], so Intel needs its own check.
4. Does Meta PC's reprojection actually consume our depth, and does reversed-infinite depth need any runtime-specific handling?
5. SteamVR and Meta PC OpenXR 1.1 support: request 1.1 and verify, or stay on 1.0 + extensions.
6. Pimax Play's integrated runtime: extension list and Vulkan/D3D12 behaviour (no inventory entry yet).
7. Should the renderer ever target quad views for Varjo/Pimax Play, given the multiview design assumes two views?
8. `XR_EXT_frame_synthesis`: worth tracking in case PC runtimes adopt it; DOOM already produces motion vectors.

---

## Sources

Spec, registry and loader (local copies in `reference/openxr/`):
- OpenXR 1.1.63 specification and chapters: https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html ; https://github.com/KhronosGroup/OpenXR-Docs
- `xr.xml` registry: https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/registry/xr.xml
- Loader design document: https://github.com/KhronosGroup/OpenXR-SDK-Source/tree/main/specification/loader ; https://registry.khronos.org/OpenXR/specs/1.1/loader.html
- OpenXR-Inventory runtime data: https://github.com/KhronosGroup/OpenXR-Inventory ; http://github.khronos.org/OpenXR-Inventory/extension_support.html
- OpenXR Tutorial (Windows/Vulkan): https://openxr-tutorial.com/windows/vulkan/
- OpenXR 1.0 reference guide: https://www.khronos.org/files/openxr-10-reference-guide.pdf

Runtimes and layers:
- VirtualDesktop-OpenXR source and wiki: https://github.com/mbucchia/VirtualDesktop-OpenXR ; https://github.com/mbucchia/VirtualDesktop-OpenXR/wiki
- SteamVR OpenXR feature list (community, edited Aug 2026): https://steamcommunity.com/app/250820/discussions/8/3121550424355682585/
- SteamVR Vulkan extension list thread: https://steamcommunity.com/app/250820/discussions/8/2448217320137020268/ ; https://steamcommunity.com/app/250820/discussions/8/2950376208547832257/
- Proton issue #7737 (SteamVR device extension list via wineopenxr): https://github.com/ValveSoftware/Proton/issues/7737
- Vulkan-Loader issue #1881 (OpenXR inside a Vulkan layer, invalid physicalDevice): https://github.com/KhronosGroup/Vulkan-Loader/issues/1881
- BotW-BetterVR (Vulkan layer + D3D12 OpenXR): https://github.com/Crementif/BotW-BetterVR
- OpenXR-Vk-D3D12 layer: https://github.com/mbucchia/OpenXR-Vk-D3D12
- Quad-Views-Foveated layer: https://github.com/mbucchia/Quad-Views-Foveated
- OpenXR Toolkit (discontinued notice): https://mbucchia.github.io/OpenXR-Toolkit/ ; https://github.com/mbucchia/OpenXR-Toolkit
- Fred Emmott, Best Practices for OpenXR API Layers on Windows: https://fredemmott.com/blog/2024/11/25/best-practices-for-openxr-api-layers.html
- Pimax Play integrated OpenXR announcement: https://store.pimax.com/blogs/blogs/announcement-pimax-play-to-get-integrated-openxr-and-quadviews-functionality ; https://github.com/mbucchia/Pimax-OpenXR
- WMR removal in 24H2: https://roadtovr.com/windows-11-drops-wmr-support-24h2/
- Steam Frame: https://en.wikipedia.org/wiki/Steam_Frame ; https://partner.steamgames.com/doc/steamframe/engines/custom
- PSVR2 Toolkit: https://github.com/BnuuySolutions/PSVR2Toolkit ; PSVR2 OpenXR eye tracking: https://github.com/BattleAxeVR/PSVR2_OpenXR_Eye_Tracking ; https://www.uploadvr.com/playstation-vr2-eye-tracking-now-works-on-pc-via-open-source-driver-mod/
- PICO Connect experimental OpenXR runtime: https://forum.dcs.world/topic/372463-pico-headsets-pico-connect-built-in-experimental-openxr-runtime/
- Intel Arc + VDXR D3D11 shared-texture crash report: https://github.com/BerZerker96/Osiris-Vr-Viewer/issues/5
- ALVR: https://github.com/alvr-org/ALVR
