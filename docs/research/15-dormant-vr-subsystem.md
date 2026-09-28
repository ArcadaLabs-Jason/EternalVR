# 15: The dormant VR subsystem in DOOM Eternal: what it is, what it probably does, can we use it

Status: research notes, 2026-09-25. Nothing here has been run against the game. Tags: **[C]** =
confirmed from source code or a primary dump we have read; **[U]** = inferred, second-hand, or
only confirmed for an older build or a sibling engine. The rig test plan in section 6 exists to
turn the important [U]s into [C]s.

Reference material for this topic: `reference/idtech7/vr-subsystem/` (see its `MANIFEST.part.md`).
Background on the engine: `docs/research/03-doom-eternal-internals.md`.

Gap to note up front: this pass could not run general web searches, so Reddit, YouTube, Steam
forum and Meathook Discord mentions of `vr_enable` were not searched. GitHub code search was used
instead, and found no one who has reported testing `vr_enable` or `vr_dummyDevice` in DOOM (2016),
DOOM Eternal or Great Circle.

---

## 1. Summary

- DOOM Eternal carries two separate inherited systems that are easy to mix up:
  1. a **stereo render scaffold** from id Tech 5/6 (`stereoRender_*` cvars, `multiView_60Hz`,
     `r_debugInvert2ndView`, `idRenderFrameInfo::worldViews` with room for two views,
     `idScreenView::guiOriginOffset`), which DOOM (2016) already had, and
  2. a **VR device/input system** from DOOM VFR (`vr_*` cvars, `idVRSystem`,
     `idVRHeadMountedDisplay`, `idVRTrackedDevice`, `idVRInput`, `idVRController`,
     `K_STEAMVR_*`/`K_PSMOVE_*`/`K_PSAIM_*` key codes), which DOOM (2016) does not have.
- The same `vr_*` and `stereoRender_*` set, with identical defaults, ships in Indiana Jones and
  the Great Circle (2024). This is shared id Tech 7 engine code, still compiled in and still
  maintained (the key enum was renumbered in 2024 around the VR block).
- The PC exe's RTTI (2021 build) lists `idVRSystem_Dummy` and `idVRHeadMountedDisplay_Dummy` and
  **no hardware backend class** (no OpenVR, Oculus or PSVR implementation). DOOM VFR's PC version
  used SteamVR/OpenVR only.
- Best estimate: **(b) partially present**. The VR system, dummy device, input classes and the
  two-view render scaffold are compiled in; the hardware backend is not. Whether `vr_enable 1` plus
  `vr_dummyDevice 1` still drives a second rendered view is the one question that decides whether
  this is useful, and it takes about an hour on the rig to answer (section 6).
- If it does render two views, it could replace or back up our multiview + SPIR-V patching plan
  for per-eye rendering, because the engine would handle per-view culling, TAA history and GUI
  offset itself. If it does not, nothing changes in the plan, and we keep a few useful pieces
  (the key-code namespace for motion-controller binds, the debug cvars, and pointers to where the
  second-view code lives).

---

## 2. Lineage

| Generation | Stereo / VR code | Evidence |
|---|---|---|
| DOOM 3 BFG (2012, id Tech 4 with id Tech 5 back-ports) | `stereoRender_enable` (six 3D-TV/3D Vision modes), `interOccularCentimeters`, `convergence` (0 = HMD mode), `swapEyes`, `deGhost`, `defaultGuiDepth`, `stereoRender_warp*` ("this is the Rift warp"). Per-eye `renderView_t` with `viewEyeBuffer` and `stereoScreenSeparation`. Carmack's 2012 Rift demo built on this. | GPL source [C]; excerpts in `bfg-stereo-source-excerpts.md` |
| id Tech 5 internal (RAGE era) | `stereoRender_screenSeparation` "screen units from center to eyes": declared `extern` in BFG's `PlayerView.cpp:590` but not defined in the released tree | BFG source [C]; Dishonored 2 (Void, id Tech 5 lineage) ships all four Eternal names, `separation` 1.5, `screenSeparation` 0.25 [C: third-party dump] |
| DOOM (2016), id Tech 6 | Four `stereoRender_*` cvars (separation default 1.5 inches), `multiView_60Hz`, `stereoRenderMode_t` with `STEREO_RENDER_LEFT_AND_RIGHT`/`TOP_AND_BOTTOM`, `HDMI3D`; stereo expressed as two `idScreenView` entries in `idRenderFrameInfo::worldViews`. No `vr_*` cvars, no VR import. | Two independent RE projects (static and reflection work, and live probes) [C for DOOM 2016] |
| DOOM VFR (2017), id Tech 6 branch | Full VR game: SteamVR/OpenVR on PC (Vive, later WMR and Rift via SteamVR), PSVR with Move and Aim controllers on PS4. Bind names `STEAMVR_PRIMARY_*`. Teleport, dash, snap/180 turn, later smooth locomotion. | PCGamingWiki, Bethesda patch notes [C]; renderer internals undocumented [U] |
| DOOM Eternal (2020) and Great Circle (2024), id Tech 7 | All of the above names kept; `vr_*` cvars added; RTTI has only `_Dummy` VR backends; key enum keeps the VFR controller block. | Section 3 |

The chain matters because it tells us which code is likely to work. The stereo scaffold was
exercised by id at least through DOOM 2016's development (split-screen and stereo share the
second-view path, per id's own comments). The VR system was exercised in VFR. Eternal has had no
VR product, so nothing forced id to keep either path working after 2017.

---

## 3. Evidence table

| # | Claim | Evidence | Conf. |
|---|---|---|---|
| 1 | Eternal registers `vr_enable`, `vr_dummyDevice`, `vr_controllerMovement`, `vr_debugRender`, `vr_defaultMinColorOutput`, `vr_dominantHand`, `vr_logLevel`, `vr_metersToGameUnits` | KEX 2024 cvar dump; Meathook 2021 cvar enum | [C] |
| 2 | Same eight `vr_*` + four `stereoRender_*` + `multiView_60Hz` + `r_debugInvert2ndView` in Great Circle, with identical defaults | Great Circle `cvardump.txt` (header "Default value") | [C] |
| 3 | The values in the Eternal dump are defaults (`vr_logLevel 4`, `vr_debugRender 1`, `stereoRender_separation 0.02857`) | Match with Great Circle defaults | [C] for Great Circle, [U] strong for Eternal |
| 4 | DOOM (2016) has the `stereoRender_*` cvars but no `vr_*` cvars | 6,572-cvar DOOM 2016 list searched by doom-2016-vr; live probes found the stereo idCVar constructors | [C] |
| 5 | The `vr_*` family came from DOOM VFR | (4) plus VFR being the only id VR title between them; VFR bind names match Eternal key names (8) | [U] strong |
| 6 | PC VR backend classes are absent; only `idVRSystem_Dummy` / `idVRHeadMountedDisplay_Dummy` exist | RTTI vtable list of the retail exe, Mar 2021 (3,937 classes): `idVRSystem`, `idVRSystem_Dummy`, `idVRHeadMountedDisplay`, `idVRHeadMountedDisplay_Dummy`, `idVRTrackedDevice`, `idVRInput(Common)`, `idVRController(Common)`; nothing with OpenVR/Oculus/PSVR in the name | [C] for 2021 build, [U] for current |
| 7 | VR controller and input classes are part of the live input system | RTTI + typeinfo order puts `idVRController`/`idVRInput`/`vrControllerDeviceDescription_t` beside `idKeyboard`/`idMouse`/`idJoystick`/`idInput` | [C] names, [U] behaviour |
| 8 | `keyNum_t` still contains `K_STEAMVR_PRIMARY/SECONDARY_*`, `K_PSMOVE_*`, `K_PSAIM_*` (0x12C-0x17B in 2021-22, +5 in the 2024 Rev 3 build) | Meathook `mh_inputsys.hpp`; EternalAdvanced `id.h` (2024) | [C] |
| 9 | Eternal keeps DOOM 2016's second-view stereo fields | Property names `worldViews`, `screenViews`, `viewIndex`, `guiOriginOffset`, `inhibitModelFovScale`; types `idRenderFrameInfo`, `idScreenView` | [C] names; capacity 2 and comments [U] until read on Eternal |
| 10 | BFG-style per-view eye fields are gone | `viewEyeBuffer`, `stereoScreenSeparation` absent from Eternal property names and from DOOM 2016's fully walked reflection DB | [C] |
| 11 | The exe does not import a VR runtime | No import list for Eternal in hand. DOOM 2016's import table has none; patch change lists 1.1-6.66 Rev 2.2 touch no VR DLL | [U] |
| 12 | An `openvr_api.dll` ships in the Eternal folder | No evidence either way. (A third-party test fixture lists one in the DOOM 2016 folder; not verified.) | [U] |
| 13 | Anyone has tried `vr_enable` in DOOM 2016 / Eternal | Nothing found in GitHub code; forums not searched this pass | [U] |
| 14 | `stereoRender_separation` in Eternal is a half-IPD in metres (57.1 mm full IPD) | Help text "world units from center to eyes {{ units = m }}"; BFG semantics "Game world units from one eye to the centerline. Total distance is twice this." | [C] meaning, [U] whether read |
| 15 | Eternal's two separation defaults were mechanically unit-converted | 0.02857/1.5 = 0.00476/0.25 = 0.0254 x 0.75; the "screen" value is unitless in BFG yet got metres | [C] arithmetic, [U] interpretation |

---

## 4. What each name did before, and what it probably does in Eternal

**`stereoRender_separation`** (0.02857 m). In BFG the HMD path moves `vieworg` by
`eye * worldSeparation` along the view's left axis for each eye and renders a full scene per eye.
"From center to eyes" means half the IPD, so Eternal's default is a 57.1 mm IPD, below the adult
average (about 63 mm, half 0.0315-0.032 m). It looks like a conservative 3D-TV default, not an HMD
value: 1.5 inches in DOOM 2016 (76 mm full), scaled by 0.75 before the metric conversion. With a
real headset, VFR would have taken eye offsets from the runtime (OpenVR's eye-to-head transform),
so this cvar was probably the fallback for the dummy device and for non-HMD stereo [U]. For us: if
the engine path is alive, drive it from OpenXR every frame (`separation = ipd / 2` in metres, since
`vr_metersToGameUnits` is 1.0). Note that a single scalar only gives symmetric eye positions; the
per-eye asymmetric projection still has to come from somewhere (dummy HMD projection, or our hook).

**`stereoRender_screenSeparation`** (0.00476). In BFG this is a horizontal projection skew for 3D
TVs (`jitterx += stereoScreenSeparation` in the projection builder) and is forced to 0 in HMD mode.
For an HMD it must be 0. If a rig test shows it changing the image, that confirms the projection
code still reads it.

**`stereoRender_guiOffset`** (0 m). BFG drew the 2D GUI once per eye with opposite horizontal
offsets so menus sit at a chosen depth instead of at infinity. DOOM 2016 and Eternal keep a
per-view `idScreenView::guiOriginOffset` ("for stereo 3D, the guis can be offset differently in
each screenView"). Our plan captures the UI target into an OpenXR quad layer, which makes this
unnecessary; it only matters if we end up compositing the UI into the eye images.

**`stereoRender_swapEyes`**. Swaps which target each eye writes. A cheap check that a second view
exists: toggling it should swap left/right content.

**`multiView_60Hz`** (1). "0 = alternate frame rendering, 1 = render both each frame". The name
suggests a 60 Hz console stereo mode (PSVR runs 60 Hz reprojected to 120). Default 1 means both
views every frame. "Multiview" here means two views submitted by the engine, not Vulkan
`VK_KHR_multiview` [U].

**`r_debugInvert2ndView`**. "Invert the second render view in order to debug multiview." The best
single probe we have: if a second world view is being rendered anywhere, setting this makes it
obvious in the output or in a capture.

**`vr_enable`**. VFR's master switch. Most likely read once at startup to create the VR system
(so it may need to be set on the command line), and read per frame by view setup to decide on
two views [U].

**`vr_dummyDevice`**. Selects `idVRSystem_Dummy` / `idVRHeadMountedDisplay_Dummy`: a fake HMD
id's developers used to run the VR path without a headset. A dummy HMD typically reports a fixed
pose, fixed per-eye projection and a fixed render size [U]. It is the only backend in the PC exe.

**`vr_metersToGameUnits`** (1.0). World scale for converting tracking-space metres. 1.0 is
consistent with id Tech 7 using metric world units (Eternal's `{{ units = m }}` cvars hold
converted values such as `pm_normalViewHeight` 1.65735 = 65.25 in). VFR on inch units would have
needed about 39.37 [U]. Useful to us as a statement that one Eternal unit is one metre.

**`vr_defaultMinColorOutput`** (0.02). Black-level floor "to avoid oled smearing" on OLED
headsets (Vive and PSVR were OLED; VFR's January 2018 patch fixed "discoloring seen on some
HMDs"). If we ever need this we do it in our compositor pass.

**`vr_logLevel`** (4) and **`vr_debugRender`** (1). Development defaults left at maximum. The VR
system will log everything it does once enabled, which is what makes the rig test cheap.
`vr_debugRender` probably draws tracked-device debug geometry.

**`vr_controllerMovement`** (1) and **`vr_dominantHand`** (0). VFR's controller locomotion and
handedness options, read by `idVRInput`/`idVRController` [U].

**`idVRInput`, `idVRController`, `vrControllerDeviceDescription_t`, `vrAxis`**. Input devices on
the same level as keyboard, mouse and joystick. Without a backend nothing produces their events.
The `K_STEAMVR_*` key codes are the part we can use regardless: they are valid bind targets, so a
virtual input device can raise them and users can bind them in the normal bind system.

---

## 5. Likelihood assessment

These are judgement calls from the evidence above, stated so the rig results can prove them wrong.

**(a) Fully functional with a hardware backend: about 3%.** Against: no backend class in the
2021 RTTI, only `_Dummy`; id's platform-stub pattern elsewhere in the same list
(`idStub*Provider`); no VR DLL in any patch change list; Eternal never advertised VR. The residual
is the chance that the backend is a non-polymorphic C-style wrapper around `openvr_api.dll`
loaded at runtime, which the RTTI list would not show. Step 1 of the test plan (strings and
folder) closes this in minutes.

**(b) Partially present, stereo/GUI/input code without an HMD backend: about 60%.** For: the VR
system, dummy device and input classes have vtables in the retail exe, which means their
constructors are linked and something references them; the cvars are registered with a live
object each (DOOM 2016 precedent: constructed, just hidden); the two-view render structures and
their debug cvar still exist in 2024 builds; the key enum was renumbered around the VR block in
2024 rather than trimmed. Within (b), I would split it as roughly even odds between
(b1) `vr_enable 1` + `vr_dummyDevice 1` produces a visible second view, and (b2) the pieces are
there but the switch does nothing useful (for example the view setup no longer branches on it,
or it asserts/crashes because nothing was maintained since 2017).

**(c) Names only (registered cvars with no readers, dead classes): about 37%.** For: Eternal
shipped no VR mode, id Tech 7 moved to a new renderer (binning, GPU culling, new geometry
caches), and nothing required the VR path to keep compiling into something that works. Cvar
registrations and vtables survive easily as dead weight when a subsystem is disabled at runtime
rather than removed.

---

## 6. Rig test plan

Budget: about 2 hours. Use a copy of the retail exe (or the sandbox exe) in single-player,
offline. Record the build (exe SHA-256, PE timestamp, SizeOfImage) first. Write results to
`reference/idtech7/typeinfo/<build>/vr-probe.md`.

### Step 1: Static checks (20 min, no game launch)

1. List the install folder: `dir /s /b "...\DOOMEternal" | findstr /i "openvr ovr openxr vr_ .vrmanifest"`.
   Look for `openvr_api.dll`, `LibOVRRT64_1.dll`, `openxr_loader.dll`, any `*.vrmanifest` or
   `actions.json` (SteamVR input manifest). Note: Steam can install a `.vrmanifest` for VR apps;
   its absence is expected.
2. Imports: `dumpbin /imports DOOMEternalx64vk.exe` (and the sandbox exe). Expect no VR DLL.
3. Strings (the exe has no Denuvo since 6.66 Rev 2.2): run `strings -n 5` and grep
   case-insensitively for `openvr`, `IVRSystem_`, `IVRCompositor_`, `IVRInput_`, `VR_Init`,
   `VR_GetGenericInterface`, `openvr_api`, `LibOVR`, `ovr_Initialize`, `openxr`, `xrCreateInstance`,
   `sceHmd`, `PSVR`, `Morpheus`, `vrmanifest`, `idVRSystem`, `dummy`, and the `vr_*` help strings.
   An `IVRSystem_0xx` version string or `openvr_api.dll` inside the exe would mean a dynamic
   OpenVR loader exists (outcome a). Collect every string near the `vr_*` help strings; VR log
   message formats will sit nearby and tell us what to look for in the log.
4. Ghidra (x64, with MSVC RTTI analysis):
   - From the `vr_enable` help string, find the idCVar constructor call and the static cvar object.
     List all readers of that object (data xrefs). Classify each: startup init, per-frame view
     setup, input, UI. Do the same for `vr_dummyDevice`, `stereoRender_separation`,
     `stereoRender_guiOffset`, `multiView_60Hz` and `r_debugInvert2ndView`.
   - From the `.?AVidVRSystem_Dummy@@` type descriptor, find its vtable, then the constructor, then
     who calls it and under which condition. Name every virtual in `idVRHeadMountedDisplay` from
     the call sites (expect: get eye pose, get projection/FOV, get render size, submit frame).
   - Outcome signal: zero readers of `vr_enable` = (c). Readers only in init = the VR system can be
     created but may not affect rendering. Readers in view setup near `r_debugInvert2ndView` = (b1)
     is likely.

### Step 2: Console and cvar state (10 min)

1. Unlock the console (Meathook v7.2 `XINPUT1_3.dll`, DE Advanced Options, or EternalPatcher's
   unrestrict patches; see doc 03, section 3.3).
2. `listcvars vr_`, `listcvars stereo`, `listcvars multiView`. With Meathook, read each cvar's
   `cvarData_t.flags` (layout in doc 03). In DOOM 2016 the relevant bits were CVAR_CHEAT 0x8,
   CVAR_INIT 0x4000 (command line only), CVAR_ROM 0x8000, CVAR_SHIPPINGDISABLED 0x400000 (from a
   community decode); confirm Eternal's values from its cvar-flag enum before trusting them. An INIT
   flag on `vr_enable` means only the launch-time test is meaningful.
3. `mh_type idRenderFrameInfo`, `mh_type idScreenView`, `mh_type idVRInput`,
   `mh_type idVRController`, `mh_type vrControllerDeviceDescription_t`; `mh_kw vr`, `mh_kw stereo`,
   `mh_kw multiview`. Save the output. Check `worldViews` capacity and its comment.

### Step 3: Logging setup

Command line (launch the exe directly with Steam running; the launcher may not forward
arguments): `+logFile 2 +logFilePathType 1 +in_terminal 1 +vr_logLevel 4`. `logFile 2` flushes
every print; `logFilePathType 1` puts the log (default name `consoledevoutput.log`) under the base
path instead of the save path. Take a baseline run with no VR cvars and keep its log for diffing.

### Step 4: Runtime toggle (15 min)

In the main menu, then again in a loaded map (Hell on Earth start):
1. `vr_dummyDevice 1`, then `vr_enable 1`. Watch the console and log for any VR lines (device
   creation, "dummy", errors, asserts). Note any change to window size, aspect ratio, frame time.
2. `r_debugInvert2ndView 1`. Any inverted image anywhere means a second view is being rendered.
3. `stereoRender_separation 0.5` (exaggerated), then `stereoRender_swapEyes 1`, then
   `stereoRender_guiOffset 1`. Any visible reaction proves a live reader.
4. `vr_debugRender 1` is already the default; look for debug geometry (controller or HMD axes)
   near the player.
5. Reverse the order (`vr_enable 1` first) and repeat; then set both back to 0 and check the game
   recovers.

### Step 5: Launch-time toggle (15 min)

Launch with `+vr_logLevel 4 +vr_dummyDevice 1 +vr_enable 1` plus the logging options. Run three
variants: (i) SteamVR not running, (ii) SteamVR running with a headset connected and
`vr_dummyDevice 0`, (iii) dummy on. Variant (ii) is the only way to see whether a hidden OpenVR
path tries to initialise (watch for SteamVR reporting the game as a VR app, or a log line about an
init error). Crashes are informative: capture the dump and the last log lines.

### Step 6: RenderDoc (30 min)

Capture one gameplay frame at the same spot for baseline (`vr_enable 0`) and for each variant that
did not crash. Compare:
- Count of main-scene passes (depth pre-pass, opaque/forward, TAA resolve, tone map). Doubling
  means two world views.
- Number of distinct view uniform blocks (view/projection matrices) per frame. Two blocks whose
  translations differ by `2 x stereoRender_separation` along the view's left axis is the
  signature of engine stereo; identical matrices means two identical views ("both centered
  between the eyes") with the offset applied somewhere else, or not at all.
- A second colour target of the same size, or a double-width target (side-by-side), or a render
  size change to a headset-like resolution (dummy HMD).
- Any `VkRenderPassMultiviewCreateInfo` with a view mask of 0b11 or `ViewIndex` use in shaders
  (unlikely, but it would change our SPIR-V plan).
- Whether TAA history and motion vectors are allocated per view.

### Step 7: Input namespace (10 min, independent of the above)

`bind STEAMVR_PRIMARY_TRIGGER _attack1` and `bind STEAMVR_SECONDARY_A _jump`. If the bind command
accepts them, inject those key codes through Meathook's input hook (`idInputLocalWin32` event path)
and check the actions fire. If yes, our motion-controller layer can raise dedicated `K_STEAMVR_*`
keys instead of borrowing gamepad buttons, and users get separate, rebindable VR binds.

---

## 7. How each outcome changes the architecture

**(a) Working OpenVR (or other) backend.** Unlikely, but the cheapest outcome: present an
OpenVR-to-OpenXR shim as `openvr_api.dll` (OpenComposite-style) and let the engine run its own VR
mode, then add our comfort, UI and motion-control work on top. Our Vulkan layer shrinks to
diagnostics and optional upscaling/foveation.

**(b1) Dummy path renders two views.** The engine already does per-view culling, TAA history and
GUI placement for two views. Plan: keep the dummy device, hook the `idVRHeadMountedDisplay_Dummy`
virtuals (found by RTTI name, which survives patches) to return OpenXR's per-eye poses,
projections and render size, and set `stereoRender_separation` from the runtime IPD if the engine
still uses it. The Vulkan layer then copies the two eye targets into the XR swapchains at submit
instead of synthesising a second eye. Multiview + SPIR-V patching becomes an optimisation to
reduce CPU/GPU cost later, not the foundation. Risks: two full views roughly double render
cost; Eternal-specific systems added after 2017 (GPU culling, decals, particles, the UI composite
in tone mapping) may assume one view and show artefacts.

**(b2) Scaffold present, switch dead.** Use Ghidra's map of `worldViews`/`viewIndex` and the
`r_debugInvert2ndView` reader to see whether we can populate the second `idScreenView` ourselves
from an engine hook (the split-screen path is the other client of this code). Compare that cost
honestly against the Vulkan-level plan; it is an engine-hook path with more per-build risk. Keep
the Vulkan plan as the primary route until a hand-populated second view renders cleanly.

**(c) Names only.** No change to the plan. Keep `vr_metersToGameUnits` as documentation that the
world is metric, use the `K_STEAMVR_*` codes for binds if step 7 passes, and do not spend more
time on this subsystem.

In every outcome, steps 1-2 also give us: the flags semantics of Eternal cvars, the full
reflected layout of `idRenderFrameInfo`/`idScreenView`, and code addresses for the view setup
function, which the per-eye camera work needs anyway (doc 03, RE task 5).

---

## 8. Other findings

- Neither DOOM (2016) nor Eternal had official stereo 3D; both are covered by helifax's closed
  Vk3DVision driver (Eternal has a "FullVR" profile, doc 03). BFG officially supported 3D TVs and
  quad-buffer 3D Vision, and id's Rift demo code (the `stereoRender_warp*` lens pre-distortion) is
  in the BFG release.
- PCGamingWiki's Eternal page carries a March 2020 tip to unplug an Oculus headset if the game
  will not launch. The linked thread is mostly Vulkan driver and launcher failures; this is
  probably unrelated (a Rift can appear as an extra display or Vulkan layer), not evidence of a VR
  probe at startup [U].
- A community DOOM 2016 VR mod notes that "DOOM VFR explicitly runs with g_weaponKick disabled".
  The source of that claim is not given; if a VFR config
  confirms it, it is a first-party comfort default worth copying [U].
- VFR's comfort and input design (teleport, dash, snap and 180 turns, later smooth locomotion and
  a weapon pitch option) is covered in `reference/idtech7/vr-subsystem/doom-vfr-facts.md` and
  complements doc 06.

---

## 9. Sources

- DOOM 3 BFG GPL source: https://github.com/id-Software/DOOM-3-BFG (commit 1caba19), files
  `neo/d3xp/PlayerView.cpp`, `neo/renderer/RenderSystem_init.cpp`, `neo/renderer/GuiModel.cpp`,
  `neo/renderer/tr_backend_draw.cpp`, `neo/renderer/OpenGL/gl_backend.cpp`,
  `neo/renderer/GLMatrix.cpp`, `neo/renderer/RenderWorld.h`, `neo/renderer/RenderSystem.h`
- DOOM Eternal cvar dump: https://github.com/Official-KEX/doom-eternal-full-cvarlist
  (local `reference/idtech7/typeinfo/kex-cvarlist-2024.tsv`)
- Meathook dumps and key enum: https://github.com/brongo/m3337ho0o0ok (local
  `reference/_cache/meathook`, `m34thook/mh_inputsys.hpp`, `alltypes.h`)
- RTTI vtable list: https://github.com/FishnCrisps/m3337ho0o0ok/blob/10dee47/m34thook/declare_vtbl_feature_vars.hpp
- 2024 key enum: https://github.com/Decimation/EternalAdvanced/blob/eb52836/EternalAdvanced/id.h
- Great Circle cvar dump: https://github.com/Lyall/GreatCircleFix/blob/1ce0a86/cvardump.txt
- Dishonored 2 cvars: https://github.com/LordRadai/Dishonored2-Debug/blob/ed1a72b/cvars.md
- DOOM 2016 RE: https://github.com/TefMeister/doom-2016-vr (commit 08cc186), notably
  `dev-archive/recon/2026-08-26-phase0-static/`, `dev-archive/recon/2026-09-05-reflection-eye-field-hunt/`,
  `engine-research/ENGINE-DOSSIER.md`
- DOOM VFR: https://www.pcgamingwiki.com/wiki/Doom_VFR ; Bethesda Steam announcements for app
  650000 (release FAQ 2017-11-29, PC patch 2018-01-30) via
  https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=650000 ;
  https://en.wikipedia.org/wiki/Doom_(2016_video_game) ; https://github.com/omarehaly/DOOMVFL
- DOOM Eternal and DOOM (2016) PCGamingWiki pages (VR/3D sections, troubleshooting):
  https://www.pcgamingwiki.com/wiki/Doom_Eternal , https://www.pcgamingwiki.com/wiki/Doom_(2016)
- Patch change lists: https://github.com/mcdalcin/DoomEternalDownpatcher (local
  `reference/_cache/downpatcher/data/`)
