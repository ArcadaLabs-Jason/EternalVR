# 04: Prior art: flat-to-VR injectors, UI handling, and surviving patches

Status: research note, 2026-09-25. Scope: how the best existing flat-to-VR conversions are built,
with emphasis on (a) how they separate and present UI in VR and (b) how they survive game patches.
Everything here feeds the design of EternalVR (DOOM Eternal, id Tech 7, Vulkan, OpenXR,
single-pass stereo, 6DoF, tracked controllers).

Tag legend: **[C]** confirmed by reading the source/config/docs we mirrored (file refs given);
**[U]** unverified or second-hand (articles, forum posts, our inference).

Source trees are mirrored (shallow or sparse) under `reference/_cache/<name>/` by
`reference/prior-art/fetch.sh`. Articles are saved as markdown in `reference/prior-art/docs/`.
File refs below are relative to `reference/_cache/` unless stated otherwise.

---

## 1. Project-by-project

### 1.1 UEVR (praydog) -- Unreal Engine 4/5 universal injector

**What it is.** A DLL injected into any UE4.8-5.4 D3D11/D3D12 game that gives 6DoF stereo, UI
reprojection, optional 3DoF/6DoF motion controls, an in-VR ImGui menu, Lua + C++ plugins, and
per-game profiles [C, `uevr/README.md`, `uevr-docs/src/README.md`].

**Licence -- important.** The main UEVR tree is "Copyright (c) 2022-2025 praydog. All rights
reserved." [C, `uevr/LICENSE`]. Only the plugin headers (`uevr/include/uevr/API.h`, `API.hpp`,
`Plugin.hpp`) and the docs repo are MIT [C]. We may **study** UEVR and re-implement techniques, but
must not copy code from `uevr/src/`. Its building blocks (safetyhook, kananlib, bddisasm) are
permissively licensed and can be used directly.

**Engine access -- the model for our typeinfo plan.** UEVR avoids per-game offsets. It starts from
string anchors that have existed in UE for years (e.g. `"CALIBRATEMOTION"` near `GEngine`,
`"emulatestereo"` / `r.EnableStereoEmulation` in `InitializeHMDDevice`,
`Slate.DrawToVRRenderTarget` near the Slate draw), resolves each string to its cross-reference, walks
back to the function start, then disassembles/emulates to find globals, vtable slots and struct
offsets [C, `prior-art/docs/praydog-uevr-exploration.md` sections "String reference analysis",
"Tries to minimize use of plain AOBs"]. Plain AOB signatures are used only "in a localized scenario
(such as within the bounds of a function or near an anchor point)" [C, same]. After it has
`GUObjectArray`, the whole reflected object graph is available (UObjectHook, SDK dumper,
`uevr/src/mods/UObjectHook.cpp`, `uevr/src/mods/uobjecthook/SDKDumper.cpp`) [C]. This is exactly
the pattern we want for id Tech 7: a small number of durable anchors, then the engine's own
reflection (`idTypeInfo`) for everything else.

**Stereo.** UEVR hijacks UE's built-in `FFakeStereoRendering` (the `-emulatestereo` path), rewriting
its vtable slots so the engine's own stereo pipeline renders both eyes ("Native Stereo") [C,
article; `uevr/src/mods/vr/FFakeStereoRenderingHook.cpp`, 8,153 lines]. Fallbacks: "Synchronized
Sequential" (two engine renders per tick, world time frozen; TAA ghosts) and AFR ("causes eye
desyncs and usually nausea") [C, `uevr/README.md`]. Lesson: even the best injector keeps a
sequential fallback for engines/games where native stereo breaks, but the default is native stereo.

**UI capture (the key technique).** UE renders Slate (all UMG/HUD widgets) in
`FSlateRHIRenderer::DrawWindow_RenderThread`. UEVR locates that function by the
`Slate.DrawToVRRenderTarget` CVar reference, or failing that from the return address of
`IStereoRendering::RenderTexture_RenderThread` [C, article; `FFakeStereoRenderingHook.cpp:483-575`].
In its hook it temporarily swaps the viewport's render-target resource pointer for UEVR's own UI
render target, calls the original, then restores it:

```cpp
// uevr/src/mods/vr/FFakeStereoRenderingHook.cpp:6111-6120
const auto old_texture = slate_resource->get_mutable_resource();
slate_resource->get_mutable_resource() = ui_target;
const auto ret = g_hook->m_slate_thread_hook.call<void*>(renderer, a2, a3, a4, params, unk1, unk2);
slate_resource->get_mutable_resource() = old_texture;
```

The UI target itself is created by piggy-backing on the engine's texture-creation call found via
emulation around `AllocateRenderTargetTexture` [C, article]. The result is a clean RGBA UI texture
with no scene behind it. For games that draw HUD through `AHUD`/canvas into the scene target, an
"AHUD UI Compatibility" mode vtable-hooks `FViewport::GetRenderTargetTexture` and redirects
non-scene callers to the UI texture by return address [C, `FFakeStereoRenderingHook.cpp:2170-2200,
2365-2390`].

**UI presentation.** The UI texture is copied into an OpenXR swapchain and submitted as an
`XrCompositionLayerQuad` or `XrCompositionLayerCylinderKHR` with
`XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT` [C, `uevr/src/mods/vr/OverlayComponent.cpp:810-990`].
User settings: overlay type (quad/cylinder), distance, size, x/y offset, cylinder angle,
"UI follows view" (layer in VIEW space instead of STAGE space), "UI invert alpha" (a shader pass
for games that write inverted alpha, `D3D12Component.cpp:274`) [C, `OverlayComponent.hpp:102-115`].
When decoupled pitch is on, the UI quad is counter-rotated by the removed pitch so it stays level
[C, `OverlayComponent.cpp:846-853`]. Motion-controller pointing is a ray/plane intersection in quad
space that is converted to swapchain pixel coordinates and fed to the game/ImGui as mouse input
[C, `OverlayComponent.cpp:866-905`]. UEVR's own ImGui menu is a second quad ("framework UI"), with
an optional wrist-attached mode under OpenVR [C, `OverlayComponent.cpp:402-620`].

**Motion controls / plugins.** Out of the box: 3DoF "aim method" tying the game's aim to a
controller. For 6DoF, UObjectHook lets users (not programmers) attach any component (e.g. the
weapon mesh) to a controller and save that as JSON "state" in a profile [C,
`uevr-docs/src/usage/adding_6dof.md`]. Deeper integration goes through Lua and the C/C++ plugin API
(MIT headers) [C, `uevr/include/uevr/API.h`].

**Frame pacing.** Poses are kept in a ring buffer indexed by engine frame count
(`pipeline_states[frame_count % QUEUE_SIZE]`) so the pose used to render a frame is the one
submitted with it, which keeps runtime reprojection correct under render-thread lag [C,
`uevr/src/mods/vr/runtimes/OpenXR.hpp:240-275`, `OpenXR.cpp:116-225`]. The frame counter itself is
discovered by brute force ("offsets that have integer-like values that are increasing by 1 every
call") [C, article].

**Crash and support tooling.** Vectored exception handler logs registers + faulting module and
writes `crash.dmp` to the per-game persistent dir [C, `uevr/src/ExceptionHandler.cpp`]; per-game
config dir `%APPDATA%/UnrealVRMod/<exe>/` with `config.txt`, `user_script.txt`, plugins; bug reports
are "zip the game folder" or "Export Config" [C, `uevr-docs/src/README.md`,
`uevr-docs/src/usage/overview.md`]. Each hook attempt is logged once (`SPDLOG_INFO_ONCE`) and failure
of a non-critical hook just disables that feature [C, `FFakeStereoRenderingHook.cpp:520-575`].

**Transferable:** anchor-then-reflect engine access; UI redirect at the engine's UI draw entry
(not at the GPU); UI as a compositor layer with quad/cylinder, follow-view and alpha options;
controller ray to UI pixel mapping; pose ring buffer keyed by frame; per-feature graceful failure;
crash dump + exportable profile.

### 1.2 REFramework VR (praydog) -- RE Engine (RE2/RE3/RE7/RE8/RE4, others)

**Licence:** MIT [C, `reframework/LICENSE`]. Code can be adapted with attribution.

**Engine access.** RE Engine ships a runtime type database (TDB). REFramework resolves types and
methods **by name** at runtime, and even Lua scripts hook engine methods by name:
`sdk.hook(sdk.find_type_definition("app.WeaponGun"):get_method("shoot"), on_pre_shoot, on_post_shoot)`
[C, `reframework/scripts/re8_vr.lua:663`]. This is the strongest patch-survival pattern we found:
a game update that moves code does not move names. It is the closest analogue to id Tech 7's
`idTypeInfo` (class/member names with offsets) plus its cvar/cmd system.

**Stereo.** Default is synchronized sequential: on end-of-rendering for the left eye it re-enters the
engine's render entry for the right eye within the same tick; AFR is an option
(`AlternateFrameRendering` toggle) [C, `reframework/src/mods/VR.cpp:3045-3110`, `VR.hpp:498`].

**UI handling -- per-element, engine level.** Every GUI element passes through
`VR::on_pre_gui_draw_element` [C, `VR.cpp:2349-2560`]. It:
- keeps an explicit allow/deny list by FNV hash of the GUI game-object name: fades
  (`BlackFade`, `Fade_In_Out_Black`), pillar boxes, cutscene letterbox bars per game
  (`Gui_ui2510` in RE4), crosshair (`GUI_Reticle`) removed when motion controls are active;
- converts screen-space GUI views to world-space views (`set_ViewType(World)`), sets
  `Overlay=true` (renders on top, fixes double vision) and `Detonemap=true` (fixes tint), then
  places each element in front of the camera at a distance-scaled size, or at its world attach
  point for elements like enemy markers (`app.UIWorldPosAttach`).

This is "HUD as world geometry, per element" rather than "HUD as one captured texture".

**Motion controls.** In Lua: the game's shot ray is overwritten in a `shoot` pre-hook with muzzle
position/forward from the tracked weapon (`re8_vr.lua:633-656`); a crosshair world position is found
with asynchronous raycasts against Attack/Bullet collision layers (`re8_vr.lua:906-960`); hand/body
IK is driven from controller poses; input is injected by hooking the game's pad manager update
rather than emulating a virtual gamepad (`re8_vr.lua:478-551`) [C].

**Lazy-follow GUI.** `slerp_gui` re-centres the GUI toward head yaw only when the angle exceeds 20
degrees (or in cutscenes), then eases for up to 1.5 s, faster while moving [C, `re8_vr.lua:963-1011`].
Good comfort pattern for a body-locked HUD.

**Transferable:** name-based hooks through engine reflection; per-element UI classification by
stable names; muzzle-ray override for aiming; raycast-placed reticle; threshold-plus-slerp HUD
follow; engine-level input injection.

### 1.3 DOOM 3 BFG VR "Fully Possessed" (Samson/Koz, Carl Kenner et al.) -- id Tech 4 BFG

**Licence:** GPLv3 [C, `doom3bfg-vr/COPYING.txt`]. Study only; do not copy into an MIT project.
Most relevant lineage: id Tech 4 BFG introduced idSWF (Flash-derived UI) which id Tech 6/7 kept in
evolved form.

**UI capture.** The entire HUD (idSWF `hudManager`, weapon GUI, MP messages) is rendered and
captured with `renderSystem->CaptureRenderToImage("_hudImage", true)` [C,
`doom3bfg-vr/neo/d3xp/Player.cpp:4657`], then drawn in the world on a model (`hud.lwo`) with a
custom material `vr/hud` [C, `Player.cpp:2140-2143`]. Placement in `idPlayer::UpdateVrHud`
[C, `Player.cpp:12930-13008`]:
- `vr_hudPosLock` 0 = face-locked, 1 = body-locked (yaw follows body, fixed pitch);
- `vr_hudType` 2 = "look activate": HUD appears when head pitch passes `vr_hudRevealAngle` (48 deg),
  i.e. look down to see your stats; forced on when health < `vr_hudLowHealth`;
- `vr_hudOcclusion` uses the weapon depth hack so the HUD is not occluded by the world;
- individual toggles for health, ammo, pickups, tips, location, objectives, stamina, comms,
  weapon icons [C, `neo/vr/Vr.cpp:68-92`].

**PDA and in-world GUIs.** The PDA is a model held in the off hand; world GUI panels act as touch
screens (`vr_guiMode` 0 = weapon-aim cursor, 1 = gaze cursor, 2 = touch) [C, `Vr.cpp:56-66`,
`Player.cpp:7867, 8144`]. Full-screen menus get a scale and a fixed screen separation
(`vr_guiScale`, `vr_guiSeparation`) [C, `Vr.cpp:63-64`].

**Cutscenes.** `vr_cinematics` 0 = immersive (camera replaced by HMD-relative), 1 = cropped
(black bars reduce the moving area of the view), 2 = projected (rendered as a flat image on a
virtual screen) [C, `Vr.cpp:209`, `d3xp/PlayerView.cpp:605-630`, `d3xp/Camera.cpp:623,725`].

**Comfort/weapon.** Snap turn (`vr_comfortDelta`), tunnel vision, teleport including a "Doom VFR
style" slow-time + warp mode, weapon pivot/forearm offsets, laser/red-dot sight projected to the
surface (`vr_weaponSightToSurface`), holster slots, flashlight mounts (body/head/gun/hand)
[C, `Vr.cpp:36-199`].

**Transferable:** the look-down-to-reveal body-locked HUD, per-element HUD toggles, the
three-way cinematic option, touch-screen GUIs, sight-to-surface reticle. These all map directly to
DOOM Eternal gameplay.

### 1.4 Doom3Quest (Team Beef / DrBeef, Baggyg) -- Quest standalone source port

Based on d3es-multithread; renders with **GL_OVR_multiview** (single-pass stereo) [C,
`doom3quest/app/src/main/jni/Doom3Quest/VrFramebuffer.c:34-222`, `VrRenderer.c:209-257`]. Menus are
drawn on an `XrCompositionLayerCylinderKHR` whose yaw is captured once from the head pose when a
menu opens and then held (`menuYawNeedsUpdate`) [C, `VrRenderer.c:436-500`]. Imports Fully
Possessed features: IK body/arms, immersive cinematics, touch-screen GUIs, PDA [C, `doom3quest/README.md`].
Licence GPL [C]. **Transferable:** multiview as the performance baseline for single-pass;
yaw-latched cylinder menus.

### 1.5 Crysis VR (fholger / "Cabalistic") -- CryEngine 2, built on the Mod SDK

Licence: CryENGINE 2 Mod SDK licence [C, `crysis-vrmod/README.md:103`, `LICENSE.txt`] -- study only.

**Stereo.** Two sequential renders of the frame from the game DLL (`RenderSingleEye(0)`,
`RenderBegin()`, `RenderSingleEye(1)`), OpenXR submission via D3D interop [C,
`crysis-vrmod/Code/VR/VRRenderer.cpp:140-160, 288-340`]. Particle simulation is suppressed for the
second eye to save CPU [C, same, lines ~292-300].

**UI capture by render order.** After both eyes are rendered, the backbuffer is cleared to
transparent `(0,0,0,0)` and the engine continues to draw its Flash HUD/menus; at Present the
backbuffer is copied into a separate HUD texture that becomes an OpenXR quad [C,
`VRRenderer.cpp:160-190`, `VRManager.cpp:161-198`]. No per-element knowledge needed: "whatever is
drawn after the 3D scene is UI".

**Context-dependent HUD placement.** `SetHudInFrontOfPlayer` (fixed in world, 4 m wide),
`SetHudAttachedToHead` (2 m at 2 m), `SetHudAttachedToOffHand` (binocular view on the off hand),
`SetHudAttachedToWeaponHand` (scope view as a 25 cm quad at the weapon), `SetVehicleHud` [C,
`VRManager.cpp:805-900`]. Render mode switches between VR, a 3D cinema screen (fixed eye distance,
`ModifyViewCameraFor3DCinema`) and a 2D screen for cutscenes (`vr_cutscenes_2d`), loading screens,
scopes and vehicles [C, `VRRenderer.cpp:254-285`, `VRManager.cpp:349-356`]. The world is not
rendered while the main menu is open ("it shows a rotating game world that is disorienting") [C,
`VRRenderer.cpp:320-330`]. Present hook liveness is checked every frame and reinstalled if lost
[C, `VRRenderer.cpp:140-149`].

**Transferable:** clear-to-transparent UI split; scope/zoom rendered as a separate mono view shown
on a small quad at the weapon; explicit per-context render modes; hook watchdog.

### 1.6 The Dark Mod VR (fholger) -- id Tech 4 derivative, OpenXR

GPL [C]. The backend treats the UI as a **third view**: it executes the frame's render command
list three times -- left eye, right eye (3D views only) and a "UI" pass (2D/GUI views only) into a
separate `vr_uiResolution` (2048) swapchain, then submits a projection layer plus an
`XrCompositionLayerQuad` [C, `thedarkmodvr/renderer/vr/OpenXRBackend.cpp:140-200, 226-262,
1011-1034`]. The 3D/2D split is decided per command by whether the view has entities and a map
(`isv3d`). It also offers `vr_force2DRender`, a mouse-aim indicator drawn in 3D, decoupled mouse yaw
inside a dead-zone angle, comfort vignette, hidden-area mesh and fixed foveated rendering
[C, `OpenXRBackend.cpp:34-51`, `VRFoveatedRendering.cpp:25-29`].

**Transferable:** UI rendered at its own resolution (text legibility independent of eye
resolution); classification of passes as 3D vs 2D in one command stream -- the same decision we
must make in DOOM Eternal's Vulkan command stream.

### 1.7 Half-Life 2: VR Mod (Source VR Mod Team; fholger contributes)

Closed source for the main mod; only `DrBeef/HL2VR_d3d9` (Apache-2.0 d3d9 proxy) and the
`HL2VRU` fork README are public [C]. Design [U, `prior-art/docs/hl2vr-faq.md` and store/community
pages]: health/armour on the back of the off hand, ammo in an over-the-shoulder pouch and on
weapons, weapon-wheel selection, two-handed weapons, desktop mirror can hide the HUD
(`hlvr_hud_on_mirror`) [C for the cvar, FAQ]. **Transferable:** body-anchored, diegetic stats;
treat mirror window output as a separate concern.

### 1.8 Luke Ross R.E.A.L. VR (closed source)

**Stereo = AER** (alternate eye rendering): the game renders one eye per frame and the previous
frame is reused for the other eye; runtime timewarp keeps head rotation smooth, but one eye is
always ~11 ms older, so fast motion ghosts [C for the author's own description,
`prior-art/docs/lukeross-gta5-real-readme.md`]. Rationale: many engines cannot render the world
twice per tick without advancing simulation; AER halves the CPU cost and works on nearly any game
[C, same]. AER v2 (2023) adds an optical-flow-based step to reduce ghosting, and DLSS/ray
reconstruction support was added later [U, `prior-art/docs/mixed-news-real-vr-aer.md`].
**HUD:** semi-transparent, world-fixed about 1 m ahead, slightly larger than the FOV so elements sit
at the periphery; becomes smaller and head-locked while aiming; recentred by a head-shake gesture
[C, author README]. **Cutscenes:** three modes -- normal, "dynamic stereo" that adapts to the
cutscene camera FOV, and a flat virtual screen [C, same]. **Controls:** deliberately gamepad/head
aim, no tracked weapons; dominant-eye alignment for iron sights [C, same].
**Transferable:** the aim-time head-lock HUD transition, cutscene FOV adaptation, recentre gesture.
**Not transferable:** AER as default (our requirement is synchronized single-pass stereo).

### 1.9 Helifax Vk3DVision (closed source, Vulkan) -- has a DOOM Eternal VR profile

Most directly relevant closed tool: there is a "DOOM Eternal Virtual Reality v0.90 [WIP]"
(2024-12-30) [C, `prior-art/docs/vk3dvision-game-fixes.md`, notes in
`prior-art/docs/vk3dvision-doom-eternal-vr-notes.md`]. From its profile [C, notes]:
- **Single-Frame Stereo** on DOOM Eternal: SPIR-V decompiled to GLSL, a per-view uniform injected,
  and `gl_Position.x += sep * (gl_Position.w - conv)` inserted after the engine's
  `gl_Position.y = -gl_Position.y` idiom in every vertex shader; 108 hand-fixed shaders in
  `ShaderSwap/`; 6 compute dispatches forced to run once per frame; some render passes forced mono.
- Shader hashes (V2) are taken over **decompiled source** so that recompiling the same shader
  source does not change the hash.
- UI is left in the stereo image at a single fixed depth (`[Params] x = 0.01 ; UI DEPTH`).
- OpenVR only, seated, head-aim; controllers become a virtual Xbox pad via ViGEm; NIS/FSR upscaler
  built in. README reports one eye frequently starting black until the window mode is toggled.

**Transferable:** confirmation that DOOM Eternal's pipeline tolerates single-pass stereo; the
"once per frame vs per eye" dispatch classification problem; source-level shader hashing. **Anti-
patterns for us:** hundreds of hash-keyed shader patches (patch-fragile), UI at a fixed stereo
depth, controllers-as-gamepad.

### 1.10 3DMigoto / Geo-11 / Helix (DX11 stereo fixing tradition)

3DMigoto is a DX11 wrapper whose `d3dx.ini` drives per-shader-hash overrides: per-shader
separation/convergence (`[ShaderOverride] Separation=0 / Convergence=0` pushes UI to screen depth
or infinity), `depth_filter = depth_active|depth_inactive` to tell HUD from world by whether a depth
buffer is bound, texture-hash filtering (`filter_index`) to target a specific HUD texture, and
"stereoParams" in HLSL for manual fixes [C, `3dmigoto/Dependencies/d3dx.ini:111-240, 804-885`].
Geo-11 builds a full-SBS/VR output on top of that ecosystem [U, `prior-art/docs/helixmod-geo11-announcement.md`].
Licence GPL [C, `3dmigoto/LICENSE.GPL.txt`]. **Transferable:** the "depth bound or not" heuristic is
a cheap UI classifier, and a debug-time "frame analysis" dump is invaluable. **Anti-pattern:**
correctness depending on large per-shader-hash fix lists.

### 1.11 vorpX (commercial, closed)

Two stereo families: **Geometry 3D** (true two-camera stereo via shader manipulation, costs roughly
half the frame rate) and **Z3D** reprojection from one image plus depth (Z-Normal fixed
convergence, Z-Adaptive converging on the screen centre with DOF) [C for vendor statements,
`prior-art/docs/vorpx-support-faq.md`, `vorpx-znormal-vs-zadaptive.md`]. UI: **EdgePeek** shrinks the
game image onto a virtual screen you can look around, used for menus and corner HUD elements,
toggled by a key [C, FAQ]; plus HUD/menu scaling. **Transferable:** a one-button "virtual screen"
escape hatch for any UI we fail to classify. **Not transferable:** depth-reprojection stereo.

### 1.12 DOOM VFR (official, id Software 2017) and DOOM 3: VR Edition (Archiact 2020)

DOOM VFR built locomotion around teleport ("zooms you to the next location rather than blink")
and short lateral dashes, plus slow-motion as a mechanic, with telefrag of staggered enemies for
health [C for reviewer descriptions, `prior-art/docs/roadtovr-doom-vfr-review.md`,
`roadtovr-doom-vfr-locomotion.md`]. The same review notes hard-coded handedness and unreliable
laser-pointer UI interaction [C, review]. DOOM 3: VR Edition removed the HUD entirely in favour of
"diegetic UI": stats on a wrist watch and on the weapons [C, developer quote,
`prior-art/docs/psu-doom3-vr-edition-interview.md`]. **Transferable:** the official id-lineage VR
precedent is diegetic wrist/weapon UI; dash/slow-mo are optional comfort modes, not core.

### 1.13 Performance layers: openvr_fsr, vrperfkit, OpenXR Toolkit

fholger's `vrperfkit` (MIT) proxies `dxgi.dll` and hooks the VR submit to run FSR/NIS/CAS and
fixed-foveated VRS on the eye textures, D3D11 only [C, `vrperfkit/README.md`, `vrperfkit/src/`].
OpenXR Toolkit (mbucchia) is an OpenXR API layer that does the same (plus foveation, overlays) at
the OpenXR boundary [U, `prior-art/docs/openxr-toolkit-readme.md` points to the external site].
Vk3DVision's DOOM Eternal profile bundles an openvr_fsr-derived module [C, notes].
**Transferable:** since we own the OpenXR submission, upscaling/sharpening and foveation belong in
our own submit path (and we must coexist with OpenXR Toolkit as an API layer).

---

## 2. Comparison table

| Project | Engine access | Stereo method | UI method | Motion controls | Licence | Patch survival approach |
|---|---|---|---|---|---|---|
| UEVR | Injected; string anchors + disassembly/emulation, then UE reflection (GUObjectArray) | Engine native stereo (FFakeStereoRendering); synced sequential; AFR | Slate draw redirected to own RT; OpenXR quad/cylinder layer; follow-view, invert-alpha; AHUD compat redirect | 3DoF aim out of box; UObjectHook attach (profile JSON); Lua/C++ plugins | Main code all-rights-reserved; plugin API + docs MIT | Multi-fallback resolvers, localized AOBs only, per-feature failure, crash dumps, per-game profiles |
| REFramework VR | Injected; RE Engine TDB reflection, name-based hooks (C++ and Lua) | Synced sequential (re-render right eye same tick); AFR option | Per-element: GUI views converted Screen->World, deny/allow list by name hash, crosshair removal | Lua per game: muzzle-ray override, IK, raycast reticle, pad-manager input hook | MIT | Name-based resolution survives code moves; per-game Lua scripts |
| DOOM 3 BFG VR | Source port (GPL) | Engine stereo (two views) | idSWF HUD captured to `_hudImage` on world model; body/face lock; look-down reveal; per-element toggles; touch GUIs | Full: weapon pivot, IK, holsters, teleport, flashlight mounts | GPLv3 | N/A (source) |
| Doom3Quest | Source port (GPL) | GL_OVR_multiview single pass | Cylinder layer menus, yaw-latched; FP HUD features | Full (from Fully Possessed) | GPL | N/A (source) |
| Crysis VR | Game DLL via Mod SDK + D3D hooks | Sequential two renders per frame | Clear-to-transparent after 3D; Present copy to HUD quad; per-context placement (head/world/hand/scope) | Full 6DoF, OpenXR actions, haptics | CryEngine Mod SDK licence | Present hook watchdog |
| The Dark Mod VR | Source (GPL) | Command list executed per eye | Separate UI "view" at 2048 into quad layer | Seated/gamepad (motion WIP) | GPL | N/A (source) |
| HL2 VR Mod | Source SDK mod (closed) | Engine stereo | Diegetic: hand/wrist stats, shoulder ammo | Full, two-handed | Closed | N/A |
| R.E.A.L. VR | Injected, closed | AER (v2 optical flow) | World-fixed semi-transparent HUD; head-locked when aiming; head-shake recentre | None (gamepad/head aim) | Closed | Per-game builds |
| Vk3DVision | Vulkan driver/layer, closed | Single-frame stereo via GLSL injection (DE profile) | UI at fixed stereo depth | Controllers as ViGEm Xbox pad | Closed (repo BSD-3, empty) | Per-shader hash lists (source-hashed) |
| 3DMigoto/Geo-11 | DX11 wrapper | Driver/3D Vision or SBS | Per-shader separation/convergence, depth-bound filter | None | GPL | Hash lists; frame analysis tooling |
| vorpX | Generic D3D hooks, closed | Geometry 3D or Z-buffer reprojection | EdgePeek virtual screen; HUD scale | Limited | Commercial | Per-game profiles |

---

## 3. UI handling design options for us

DOOM Eternal's UI is rich: combat HUD (health, armour, ammo, equipment/grenade, flame belch/ice bomb
cooldowns, chainsaw fuel pips, blood punch charges, dash pips, crosshair with weapon-mode variants,
objective tracker, damage indicators, glory-kill prompts), a radial weapon wheel with slow-mo, the
Fortress/Codex/upgrade menus, the automap, full-screen menus, subtitles, tutorial popups, loading
screens and in-engine cinematics. All of it is authored in id's SWF-derived UI system [U -- inferred
from id Tech 6 lineage and the `Hud_*` manager RTTI names seen in DOOM 2016].

### 3.1 Layer 0 -- Capture: get the UI into its own texture (must-have, generic)

Options, in order of preference:

1. **Engine-level redirect (UEVR/TDM style).** Find the engine's UI/SWF render entry (via typeinfo or
   a string anchor) and point its render target at our own RGBA texture for the duration of the
   call. Pros: clean alpha, no scene contamination, can render at our own resolution (TDM uses 2048).
   Cons: requires finding the right entry and the RT indirection in id Tech 7. [U -- feasibility to
   be confirmed by the engine-RE topic.]
2. **Render-pass classification in our Vulkan layer (Crysis/3DMigoto style).** In the command stream,
   the UI is drawn after tonemapping/post into the final swapchain-sized target, usually with no depth
   attachment. Classify passes by (depth bound?, target format/size, position after the last post
   pass) and redirect UI draws into a cleared-to-transparent target. Pros: API-level, independent of
   engine offsets. Cons: heuristics may need per-version tuning; premultiplied vs straight alpha must
   be handled (UEVR needed "invert alpha").
3. **Fallback: whole-frame virtual screen (vorpX EdgePeek / Crysis RM_2D).** If classification fails
   (new patch), show the mono game image on a cylinder. Always available, used for menus the other
   paths do not understand.

Recommendation: implement (2) first because our Vulkan layer is the most version-independent
foothold, keep (1) as the upgrade once typeinfo gives us the UI renderer, and keep (3) as the
always-on safety net. Render the UI layer at 2048-2560 px height, independent of eye resolution.

### 3.2 Layer 1 -- Present: compositor layers, not stereo-shifted pixels

Submit UI as OpenXR composition layers (quad for small panels, `XR_KHR_composition_layer_cylinder`
for menus and the main HUD sheet), with `XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT` and,
where supported, `XR_KHR_composition_layer_depth` / the projection-layer depth so runtimes can
reproject correctly [C for API usage in UEVR/TDM/Doom3Quest]. Compositor layers are sampled once at
display resolution, so text stays sharper than anything drawn into the eye buffers and then
lens-warped. This beats Vk3DVision's fixed-depth-in-stereo approach on legibility and comfort.

Placement modes (all seen in prior art, all user-selectable):
- **Body-locked lazy follow** (default for HUD): yaw-follows the body; recentres only past a
  threshold angle and eases (REFramework `slerp_gui`: 20 deg trigger, 1.5 s ease). Fixed downward
  pitch (D3BFG body lock) so it sits below the line of sight.
- **Look-down reveal**: HUD fades in when head pitch passes a threshold (D3BFG `vr_hudRevealAngle`,
  default 48 deg), always on at low health.
- **Head-locked while aiming/zoomed** (R.E.A.L.): smaller sheet, quick glance info.
- **Menus**: cylinder, yaw latched when the menu opens (Doom3Quest), 1.5-2.5 m, never head-locked.
  Do not render the 3D world behind full-screen menus, or dim it (Crysis VR rationale).
- **Loading screens / Bink videos / cutscene fallback**: cinema screen in a neutral void.

### 3.3 Layer 2 -- Separate: element-level HUD (the part that makes it feel native)

Split the combat HUD into anchored pieces:
- **Off-hand wrist**: health, armour, extra lives, equipment/grenade cooldowns (DOOM 3 VR Edition
  watch; HL2VR back-of-hand).
- **On the weapon**: ammo count and mod state (DOOM 3 VR Edition; D3BFG weapon GUI).
- **Crosshair**: removed when motion controls are active (REFramework `GUI_Reticle`), replaced by a
  laser/dot projected to the hit surface (D3BFG `vr_weaponSightToSurface`, RE8 async raycast).
- **Weapon wheel**: world-anchored radial at the hand when opened, with controller-direction
  selection; keep the game's slow-mo.
- **Objective tracker, subtitles, tutorial popups**: lazy-follow sheet.
- **Screen-space world markers** (objective markers, glory-kill prompts): either hide (Vk3DVision
  README recommends disabling objective markers) or reproject to world depth.

How to separate, from most to least durable:
1. **By engine UI object identity via typeinfo/RTTI class *names*** (e.g. the `Hud_WeaponInfo`,
   `Hud_BottomLeft` managers seen in DOOM 2016), resolved at runtime by name (kananlib
   `rtti::find_vtable(module, "Hud_WeaponInfo")` or idTypeInfo), then render each widget into its
   own atlas sub-rect or layer. Survives patches because names survive; raw
   vtable RVAs do not.
2. **By screen region**: capture the whole HUD sheet and cut fixed rectangles (bottom-left =
   health/armour, bottom-right = ammo). Cheap and survives code changes, but breaks with HUD scale
   or aspect settings; we can force the game's HUD layout options to known values.
3. **By shader/texture hash** (3DMigoto): last resort, patch-fragile.

### 3.4 Interaction

- **Laser pointer to game cursor**: ray/quad intersection mapped to UI pixel coordinates and
  injected as mouse input (UEVR `OverlayComponent.cpp:866-905`), with hover smoothing and a
  near-touch "poke" mode for wrist panels (D3BFG touch GUIs).
- **Gamepad-nav emulation** as a fallback for screens that do not accept mouse input well
  (UEVR keeps controller shortcuts; D3BFG `vr_joystickMenuMapping`).
- Input injection at the engine's input layer (REFramework pad-manager hook) is preferable to
  ViGEm virtual pads (Vk3DVision), which need a kernel driver install and lose analog precision of
  tracked controllers.

### 3.5 Cutscenes

Offer three modes (D3BFG `vr_cinematics`, R.E.A.L., Crysis VR):
1. **Immersive**: add HMD rotation/position to the cinematic camera, correct FOV to the headset
   (R.E.A.L. "universal FOV fix"/dynamic stereo to avoid in-face close-ups);
2. **Comfort/cropped**: vignette or crop the moving camera;
3. **Cinema**: render mono or fixed-IPD stereo to a large curved screen in a dark void.
Default to cinema for camera-driven sequences, immersive for first-person scripted moments;
per-sequence override list keyed by cinematic name (via typeinfo), not by timing.

### 3.6 Recommendation

Build the UI stack in three tiers with each tier degrading to the one below:
**Tier A (ship first)**: Vulkan-layer UI capture (render-order/depth-bound classification,
clear-to-transparent), 2048-px UI swapchain, OpenXR cylinder for menus/HUD sheet, body-locked lazy
follow, look-down reveal, laser pointer mouse emulation, cinema-mode cutscenes, virtual-screen
fallback bound to a button. **Tier B**: region split of the HUD sheet onto wrist and weapon quads,
crosshair suppression plus surface-projected reticle. **Tier C**: typeinfo-driven per-widget
rendering and world-anchored markers. Every tier is independently switchable in config and in the
in-VR menu, and a failed resolver disables only its tier.

---

## 4. Hooking and patch-survival toolkit

### 4.1 Libraries

| Library | Use | Licence | MIT-compatible | Notes |
|---|---|---|---|---|
| **safetyhook** (cursey) | Inline, mid-function and VMT hooks | Boost 1.0 [C] | Yes; BSL requires the notice only in source distributions | C++23, uses Zydis (MIT); freezes threads and fixes their IP on install/remove; relocates RIP-relative and short branches; mid-hooks give a full register context [C, `safetyhook/README.md`, `prior-art/docs/aixxe-safetyhook-midhooks.md`]. Used by UEVR/REFramework. |
| **kananlib** (cursey/praydog) | String scan, xref, function-start (`.pdata`), exhaustive decode, emulation, MSVC RTTI vtable lookup, landmark sequences | Boost 1.0 [C] | Yes | Depends on bddisasm (Apache-2.0, needs NOTICE) and spdlog (MIT) [C, `kananlib/cmake.toml`]. `kananlib-cli` can verify signatures against archived binaries in CI [C, `kananlib/README.md`]. |
| **libhat** (BasedInc) | SIMD AOB scanning, section-scoped, compile-time patterns | MIT [C] | Yes | Fastest plain pattern scanner; no xref/RTTI helpers [C, `libhat/README.md`]. |
| Hooking.Patterns (ThirteenAG) | Simple pattern scanning | MIT-style [C] | Yes | Small, widely used in widescreen-fix mods; no SIMD, no xrefs. |
| MinHook | Inline hooks | BSD-2 [C] | Yes (keep notice in binary docs) | C, mature, widely used; no mid-hooks, older length disassembler, no thread-IP fixing beyond basic suspend. |

**Recommendation:** safetyhook for all code hooks (mid-hooks are the right tool for grabbing
matrices and pointers from registers inside id Tech functions without reconstructing signatures),
kananlib for anchor resolution (string -> xref -> function start, RTTI by name, exhaustive decode),
libhat only if we need raw AOB throughput. Record Boost/Apache/MIT notices in
`THIRD_PARTY_NOTICES.md`. Do not copy code from UEVR's `src/` (all rights reserved); REFramework
(MIT) code may be adapted with attribution. For Vulkan interception keep a proper Vulkan layer
(loader-supported, no code patching).

### 4.2 Resolver design (patch survival)

Order resolvers from most to least stable and stop at the first that validates:
1. **Engine reflection by name**: idTypeInfo class/member lookup, cvar/cmd by name (REFramework-style).
2. **String anchor -> xref -> function start**, optionally requiring two strings in one function
   (`find_function_with_string_refs`) [C, kananlib API].
3. **MSVC RTTI by class name** (`rtti::find_vtable`) then vtable-slot heuristics.
4. **Localized AOB** inside a function already found by 1-3 (UEVR rule).
5. **Per-version table** (exe SHA-256 / PE timestamp -> RVAs) as the last resort only.

Each resolver returns `std::optional` with a reason string, is validated structurally (pointer in
module, vtable in `.rdata`, expected field types -- UEVR's structure-analysis checks), and the result
is cached per exe hash so start-up is fast after the first run. A feature whose resolvers all fail is
disabled and reported in the overlay; nothing else is affected.

### 4.3 Version detection, CI and support

- Identify the build by exe SHA-256 plus PE timestamp plus the engine build string; log all three.
- Keep an archive of every DOOM Eternal exe we can obtain and run the resolver suite offline against
  each in CI (kananlib-cli maps PE images from disk, even on Linux [C, `kananlib/README.md`]); fail if
  any anchor is missing or ambiguous.
- Vectored exception handler: log registers, faulting module+offset, recent log lines, write a
  minidump to our persistent dir (UEVR pattern). Crash upload, if ever added, is opt-in only.
- One-click "export report" zip: log, config, resolver results, exe identity (UEVR "Export Config").
- Config: human-readable per-game file plus the in-VR ImGui menu (UEVR framework UI as its own quad,
  controller pointer, optional wrist mode); every setting hot-reloadable.
- Hook liveness watchdog for anything installed on objects that the game can recreate (Crysis VR
  reinstalls its Present hook when the swapchain changes).

---

## 5. Implications for our design

1. **Stereo**: the field splits into engine-native stereo (UEVR native, source ports, Vk3DVision SFS)
   and sequential/AER. Our requirement (single-pass, synchronized) is proven feasible on this exact
   game by Vk3DVision SFS [C, profile], but Vk3DVision gets there with 108 hand-patched shaders. We
   should get the two views from the engine/view level (as UEVR does through the engine's own stereo
   hooks) and reserve shader patching for a short, source-hashed
   list. Keep a synchronized sequential mode as a debug fallback, and AER only as a debug mode.
2. **Engine access**: adopt the UEVR/REFramework model -- a handful of durable anchors, then
   idTypeInfo for everything by name. No hard-coded RVAs outside a last-resort version table.
3. **UI**: UI must leave the eye buffers. Capture to its own high-resolution target and present via
   OpenXR layers. Build the tiered stack in section 3.6; element separation via engine UI object
   names is the long-term target, region split is the pragmatic first step.
4. **Diegetic defaults matching id's own VR precedent**: wrist stats, ammo on the gun, laser/dot
   reticle, body-locked HUD with look-down reveal, cylinder menus, cinema-mode cutscenes.
5. **Motion controls**: override the game's shot origin/direction from the tracked muzzle
   (REFramework `shoot` hook), inject input at the engine input layer (no ViGEm), weapon wheel at the
   hand. Head-aim/gamepad (R.E.A.L., Vk3DVision) stays available as an accessibility mode.
6. **Frame pacing**: store poses per engine frame in a ring buffer and submit the matching pose
   (UEVR); never mix poses across the stereo pair.
7. **Tooling from day one**: resolver CI against archived exes, crash dumps, report export, in-VR
   config overlay, per-feature kill switches.
8. **Licensing hygiene**: safetyhook/kananlib (Boost), libhat (MIT), Zydis (MIT), bddisasm
   (Apache-2.0), OpenXR SDK (Apache-2.0) are all fine for an MIT project with notices. UEVR `src/`
   (all rights reserved), GPL projects (D3BFG VR, Doom3Quest, TDM VR, 3DMigoto) and the CryEngine
   SDK licence (Crysis VR) are reference-only -- re-implement ideas, never paste code.

---

## Sources

Mirrored source (see `reference/prior-art/MANIFEST.part.md` for commits):
- UEVR -- https://github.com/praydog/UEVR ; docs https://github.com/praydog/uevr-docs , https://praydog.github.io/uevr-docs
- REFramework -- https://github.com/praydog/REFramework
- kananlib -- https://github.com/cursey/kananlib
- safetyhook -- https://github.com/cursey/safetyhook
- libhat -- https://github.com/BasedInc/libhat
- Hooking.Patterns -- https://github.com/ThirteenAG/Hooking.Patterns
- MinHook -- https://github.com/TsudaKageyu/minhook
- DOOM-3-BFG-VR (Fully Possessed) -- https://github.com/CarlKenner/DOOM-3-BFG-VR (originally KozGit/DOOM-3-BFG-VR)
- Doom3Quest -- https://github.com/DrBeef/Doom3Quest
- Crysis VR -- https://github.com/fholger/crysis_vrmod
- The Dark Mod VR -- https://github.com/fholger/thedarkmodvr
- vrperfkit -- https://github.com/fholger/vrperfkit
- HL2VR_d3d9 -- https://github.com/DrBeef/HL2VR_d3d9 ; HL2VRU -- https://github.com/vittorioromeo/HL2VRU
- Vk3DVision-Public -- https://github.com/helifax/Vk3DVision-Public ; DOOM Eternal VR package https://3dsurroundgaming.com/Vk3DVision/SFS_Releases/DOOM-Eternal-VR-0.90.7z
- 3DMigoto -- https://github.com/bo3b/3Dmigoto

Articles (saved in `reference/prior-art/docs/`, fetched 2026-09-25):
- praydog, "UEVR: An Exploration of Advanced Game Hacking Techniques" -- https://praydog.com/reverse-engineering/2023/07/03/uevr.html
- aixxe, "SafetyHook mid-function hooking" -- https://aixxe.net/2022/12/safetyhook-midfn-hooking
- Luke Ross, GTA V R.E.A.L. mod README -- https://github.com/LukeRoss00/gta5-real-mod
- MIXED, "R.E.A.L. VR mod brings DLSS Ray Reconstruction" -- https://mixed-news.com/en/real-vr-mod-dlss-ray-reconstruction/
- vorpX Support FAQ -- https://www.vorpx.com/support-faq/ ; Z-Normal vs Z-Adaptive -- https://www.vorpx.com/forums/topic/difference-between-z-normal-and-z-adaptive/
- Helix Mod, geo-11 announcement -- https://helixmod.blogspot.com/2022/06/announcing-new-geo-11-3d-driver.html
- Vk3DVision game fixes list -- https://3dsurroundgaming.com/Vk3DVisionGames.html
- Road to VR, DOOM VFR locomotion video -- https://www.roadtovr.com/doom-vfr-devs-detail-gameplay-setting-locomotion-new-video/ ; review -- https://roadtovr.com/doom-vfr-review/
- PSU, DOOM 3: VR Edition interview -- https://www.psu.com/news/doom-3-vr-edition-interview-remote-working-history-with-prey-vr-psvr-optimisations-more/
- Half-Life 2: VR Mod FAQ -- https://halflife2vr.com/faq/
- OpenXR Toolkit README -- https://github.com/mbucchia/OpenXR-Toolkit
- Luke Ross Patreon (AER v2) -- https://www.patreon.com/realvr/posts/aer-v2-152398605 (not retrievable without login; cited via MIXED)
