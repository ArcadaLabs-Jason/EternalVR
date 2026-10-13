# UI layer: the game's 2D target on its own OpenXR layer (M6, T-008, T-031, T-077)

Static analysis of the retail `DOOMEternalx64vk.exe`, Steam build 25216728 (the exe of `engine-facts.md`,
`stereo-routes.md` and `mp-guard.md`). The game was not run for this document. Tools: the Python helpers in
`analysis/scripts` (PE loader, xrefs, signature builder, RTTI), capstone, the type-info dump
(`analysis/typeinfo_rev32.json`) and Ghidra 12.1.4 headless on a copy of the stereo-recon project
(`analysis/ui-layer`, with `dispfind.py` and `cvars.py`). All addresses are RVAs in this build.

**[static-verified]**: read in this build's code or data (every signature below matches once in `.text`).
**[inferred]**: what the verified code implies; section 8 lists the live experiments that confirm it.

Starting point: Route S (`docs/VR_STEREO.md`) renders both eyes, but every 2D element (HUD, crosshair,
subtitles, menus, the FPS counter) appears in eye L only; the screen view's GUI list is the same in both
chains (S1). The plan wants the UI off the eye images anyway (ARCHITECTURE section 8, T-008, T-077, M6).

## 1. Answers

| # | Question | Answer | Tag |
|---|---|---|---|
| 1 | What consumes the GUI draws so eye R has none | Not a double buffer or a frame check: the GUI model's **pending draw list is cleared** when the first render of a tick turns it into surfaces (`idRenderModelGui` vtable slot 0x88, 0x194D0B0). Eye R's commit finds it empty and the GUI entity gets 0 surfaces | static-verified |
| 1b | Minimal fix for eye R | Save the list's four counts in eye L's call of 0x194D0B0 and restore them before eye R's call (section 3.3). **Not recommended**: it puts the HUD at zero disparity into both eye images, which is what the UI layer removes | inferred |
| 2 | Separate UI target? | Yes: **`_gui`**, RGBA8 (FMT_RGBA8, `VK_FORMAT_R8G8B8A8_UNORM`), premultiplied, at the **output** size (post-upscale), render target at renderSystem + 0x5C8. Written by the after-post pass (clear to 0, after-post surfaces) and the GUI pass; blended in the **final view colour upsample** compute pass (0x1CDF6E0) as `guiMap`, not in tone mapping | static-verified |
| 2b | Best hook | Copy `_gui` at the eye L (or mono) present into a D3D12-shared image, show it on a quad; give the composite the engine's own `_black` image instead of `_gui` (the engine's screenshot-without-HUD path does exactly this) in both eyes. **Implemented** behind `ETERNALVR_UI_LAYER=1` (section 7) | static-verified hook points, inferred result |
| 3 | Crosshair | Part of the HUD SWF, drawn into the same `_gui`: no separate target. `g_reticleMode` (0 full, 1 dot, 2 hide), `hud_reticle_scaleOverride`, `hud_reticle_styleOverride` control it; in VR hide it (2) and draw a reticle on the aim ray, or show the centre of `_gui` on its own quad at the aim distance (section 5) | static-verified cvars, inferred use |
| 4 | Menus, loading, pause | Loading screens: a GUI-only screen view (`world` NULL) rendered through the synchronous render without a game tick. Main menu and pause keep a world view (the menu scene; the frozen world, cleared behind the SP pause screen). In all of them the presenter already falls back to the cinema screen (no fresh head-tracked view); with the UI layer the composite is only skipped while the gameplay quad shows, so menus keep the game's own composite on the cinema screen | static-verified (loading), inferred (menu, pause) |

## 2. The GUI target and its composite

### 2.1 Creation (render system init)

The render system's init (vtable slot 1, 0x1CD0A40) calls 0x1CD1940, which creates the frame's named images
through the image manager (`idImageManager::CreateImage`, 0x1C3D890) [static-verified]:

| Image | Stored at | idImageOpts | Tag |
|---|---|---|---|
| `_gui` | renderSystem + 0x3A0 (0x66E2FD0) | 2D, **FMT_RGBA8** (3), width x height = renderSystem vtable 0x200 x 0x208 (globals 0x39AABE4 x 0x39AABE8, the output size; `_viewColorR11G11B10F` uses 0x1E0 x 0x1E8, the render size, unless upscaling), 1 mip, 1 layer, flags 0x207 (render target, storage) | static-verified |
| `_upscaledOpaqueDepth` | renderSystem + 0x3A8 | same size, FMT_DEPTH_STENCIL | static-verified |
| GUI render target (0x230-byte object, 0x1C73CA0) | renderSystem + 0x5C8 (**0x66E31F8**) | colour `_gui`, depth and stencil `_upscaledOpaqueDepth` (0x1C740A0) | static-verified |

The size globals are the output (native) size; with `rs_enable 0` and no DLSS they equal the window's
[inferred; the layer checks at run time that `_gui` and the swapchain have the same size].

**idImage** (0x138 bytes): the idImageOpts copy starts at +0x58 (+0x5C format, +0x64 width, +0x68 height,
+0x9C flags, bit 8 = an image set), **+0xE8 = the VkImage** (stored by 0x1C4A310 after the
`vkCreateImage` wrapper 0x1C49D50; the barrier builder 0x1C49270 reads it) [static-verified].

**VkImageCreateInfo** (0x1C49D50): FMT_RGBA8 maps to 37 (`R8G8B8A8_UNORM`); a render target gets
`SAMPLED | COLOR_ATTACHMENT`, flags bit 1 adds `STORAGE`, bit 3 would add `TRANSFER_SRC` (not set for
`_gui`): **usage 0x1C, no TRANSFER_SRC**. When the device has an async compute family (0x667EB68 != -1) every
image is created `CONCURRENT` over the graphics family (0x667EB50) and the async compute family [static-verified].
The engine's barriers always use `VK_QUEUE_FAMILY_IGNORED` (0x1C49270 writes 0xFFFFFFFF to both indices):
it never transfers queue family ownership. Render passes keep attachment layouts (initial = final =
`COLOR_ATTACHMENT_OPTIMAL` or `GENERAL`, 0x1C36990); every layout change is an explicit
`vkCmdPipelineBarrier` (slot 0x667EE30; no `vkCmdPipelineBarrier2`, no dynamic rendering) [static-verified].

### 2.2 Per frame, on the render thread

1. **After-post pass** (job 0x1C91DC0, param block +0x71AAF8 of the device context, scheduled by 0x1C5BBA0
   unless `r_skipAfterPostProcessSurfaces`; the GUI render target is its +0xC8/+0xD8, set by
   0x1C60BF0/0x1C60EF0). Case 0xF (0x1C92000) binds the GUI render target, **clears colour to (0,0,0,0)**
   and stencil, then draws the after-post-process surfaces (3D surfaces sorted after post processing,
   depth-tested against `_upscaledOpaqueDepth`) into `_gui`. Skipped during a camera cut
   (renderView + 0x29944 > 0) [static-verified code, inferred which materials].
2. **GUI pass** (0x1C572A0, and its job variant 0x1C57D90 with param block +0x71AE88, filled by
   0x1C60BF0): binds the GUI render target, clears stencil (and colour when the screen view's
   `HACK_clearViewPreViewGuis`, +0x24, is set: the block's +0x14), and draws every GUI entity of the
   render-list entry's GUI list (+0x38 list, +0x40 count) with 0x1C75090, which draws the entity's
   committed surfaces; also skipped during a camera cut [static-verified].
3. **Final composite** (0x1CDF6E0, the view colour upsample): binds `viewColorMap` (the tone-mapped view
   colour), **`guiMap`** (parameter handle 0x39AB4F0 = the GUI target's colour image, loaded at 0x1CDF7B9
   into `[rsp+0x70]`), `viewVelocityMap`, `viewDepthMap`, and writes `computeUpsampleTex`, the back buffer
   render target (renderBackend + 0x508), with `computeViewColorUpsample` (or its FCAT / YUVM variants,
   or `drawViewColorUpsample` as a draw). The present follows. `guiMap` is bound nowhere else [static-verified].

So in this build the UI is composited **after tone mapping and upscaling**, in the upsample pass, not in the
tone map compute as the 2020 frame study (`reference/idtech7/docs/coenen-doom-eternal-graphics-study.md`,
"UI") describes for the launch build. What that study says about the target still fits the code: 8-bit,
full (output) resolution, colour premultiplied by alpha [inferred for premultiplication: the cleared value
is (0,0,0,0) and `_black` = no GUI].

### 2.3 The engine's own "no GUI" composite

The same function has a mode (render thread + 0x2F4 == 2, 0x1CE0178) that binds the image manager's
**`_black`** (image manager global 0x5BF13C8, +0x20; created at 0x1C4C477 as 16 x 16 FMT_RGBA8 from a
zeroed buffer, i.e. (0,0,0,0)) as `guiMap`, runs the upsample, copies the result to a buffer
(`computeCopyImage2dToBuffer`, target 0x66E2E68 + 0x18), then composites again with the real `_gui`: a
capture without the HUD [static-verified]. Handing the composite `_black` is therefore a path the engine
already runs; the layer's composite skip does the same (section 6).

## 3. Why eye R has no GUI (question 1)

### 3.1 The chain

- **Game tick** (game thread / SWF render jobs): each SWF (HUD, subtitles, menus) renders into an
  `idRenderModelGui` (`idView::guiModelForGuis` and friends; type info). A draw is appended to the model's
  pending list (0x194BF60, 0x194CB40; "MAX_GUI_SURFACES exceeded!") and its vertices go into the GUI vertex
  buffer slot of the current GUI buffer frame (renderBackend + 0x228: triple buffered, slot = stamp % 3 at
  +0x2590, stamp at +0x25C4). The first draw of a new stamp resets the model's list (model +0x4618 holds
  the stamp) [static-verified accesses, inferred roles].
- **Render frame, frontend** (screen-views pass 0x1C75290): for each screen view, every model of `viewGuis`
  (idScreenView + 0x9A0, 24 entries; type-info comment "these guis will be drawn to viewColor, on top of the
  3D rendering") is appended to the render-list entry's GUI list (+0x38/+0x40, at 0x1C757DF) and collected
  once; then for each model (loop at 0x1C75A20): `model+0x140 = 0`, vtable 0x90, **0x1C8E470** re-initialises
  the model's render entity in the frame's GUI world state (per-entity flags word = 0x10000: 0 surfaces),
  **0x18D9FA0** commits the entity, which calls the model's **vtable 0x88 = 0x194D0B0**, then 0x18DCC50
  queues the entity for update [static-verified].
- **0x194D0B0** (idRenderModelGui, build surfaces): sets the changed bit (+0xB0 |= 0x80), resets the surface
  count (+0x490 = 0), and if the model has pending draws (+0x4E8 > 0) for the current GUI buffer stamp
  (+0x4618 == buffers + 0x25C4) builds one surface per draw range (+0x4508 ranges, +0x4F0 draw indices,
  draw records in the table whose base pointer is at 0x57CABE8), copying vertex and index data from the
  stamp's slot. **At the end it clears the pending list: +0x4E8 = 0, +0x4510 = 0, +0x4560 = 0,
  +0x4564 = -1** [static-verified].
- **Backend**: the GUI pass (section 2.2) draws an entity only if its committed surface count (flags bits
  32..43) is non-zero [static-verified].
- **GUI buffer advance**: frame job table 0x388EDB0 entry 18 (0x43A9B0), right after the render-kick stage
  (entry 17, 0x43A8C0, which under Route S renders both eyes), calls 0x1C3B320 (stamp + 1, next slot):
  once per tick [static-verified].

Eye R's chain runs after eye L's commit inside the same tick, so its 0x194D0B0 finds +0x4E8 = 0: the
entity gets 0 surfaces, the GUI pass draws nothing into the `_gui` the after-post pass just cleared, and
the composite adds nothing. The screen view's GUI list is unchanged (S1's log), exactly as observed.
`stereo-routes.md` 2.4 row "GUIs" expected both eyes to draw them; the consuming step is the draw list
inside the model, which that analysis did not follow.

### 3.2 What does not consume it

The frame stamp check passes in eye R (the buffers advance only at entry 18) and nothing in 0x1C8E470 or
0x18D9FA0 frees memory [static-verified]; the tick's vertex slot is reused only three ticks later
[inferred from the stamp % 3 slot choice].

### 3.3 The minimal intervention, and why not

A mid hook at the entry of 0x194D0B0 (rcx = the model) that, in eye L's chain (`seqChainEye()`), saves
(+0x4E8, +0x4510, +0x4560, +0x4564) per model and, in eye R's chain, writes them back before the call,
would make eye R build the same surfaces from the same draw records and vertex slot [inferred]. The engine's
own two-view scaffold even has a per-view GUI shift for HMDs (`idScreenView::guiOriginOffset` +0xA78 =
`stereoRender_guiOffset` x a layout factor, set by 0x17E7FE0 / 0x17E8740).

Not recommended: both eyes would carry the whole HUD at the same screen position, i.e. at infinity for the
asymmetric per-eye frusta, head-locked inside the eye buffers, with the crosshair at the wrong depth and
subtitles as small as on the monitor (the D-035 failure). The UI layer takes the GUI out of both eyes
instead, and eye R's missing GUI stops mattering.

## 4. Menus, loading screens and pause (question 4)

- **Loading screens** (0x455C10): the loading SWF (`LoadingGUI`, "SWF overlay for the loadscreen") renders
  into its own GUI model sized to the output size (render system vtable 0x200 / 0x208), 0x17E7FE0 builds a
  frame whose screen views have **no world** (+0x28 = 0) and that model in `viewGuis`, and 0x17E86A0 renders
  it through the synchronous render 0x1CBEA30 without a game tick [static-verified]. With no world the GUI
  pass still draws, the composite blends `_gui` over the (stale or cleared) view colour.
- **Main menu**: `idMainMenu` carries its own renderView (the menu scene), so a world view is rendered and the
  menu SWF composited over it [inferred from type info and `rvwriters.py`].
- **Pause (SP)**: the game view with the pause SWF; `idScreenView::HACK_clearViewPreViewGuis` ("Force a
  GL_Clear right before view guis are rendered ... hide frozen world behind pause screen. Only for SP"),
  which reaches the GUI pass as its colour clear flag, and `idSWF::pausedRender` exist for it
  [static-verified names and the flag's path, inferred use].

The presenter shows every frame without a fresh head-tracked view (main menu, loading) on the cinema
screen. The UI layer keeps it that way: the quad and the composite skip apply only while the projection layer
is shown with a fresh GUI capture (under 0.25 s old), so the main menu and loading screens keep the game's own
composite on the cinema screen (live U5). **The SP pause menu is different** (live U5): the camera hook keeps
running while paused, so the frame stays head-tracked, the world behind the menu is cleared to black (the
`HACK_clearViewPreViewGuis` path) and the pause menu lands on the head-locked UI quad, readable, with the
projection black around it. That is the M6 menu panel in its simplest form. Loading screens are a full-screen,
opaque `_gui` (U3's first captures). A menu on its own panel with a laser pointer
(M6) then needs no engine change: show the captured `_gui` on the curved panel instead of the whole frame,
and skip the composite there too.

## 5. Crosshair and HUD elements (question 3)

The crosshair is an idSWF widget (`idHUD_Reticle`, `idSWFWidget_Hud_Reticle`, per-weapon
`idDeclWeaponReticle` styles) in the HUD SWF, drawn into `_gui` with the rest [static-verified names]. It
cannot be separated at the target level. Controls (cvar object, default, type flags from the registration;
all runtime-settable, `analysis/ui-layer/cvars.py`) [static-verified]:

| Cvar | Object | Default | Use for VR |
|---|---|---|---|
| `g_reticleMode` | 0x45F8610 | 0 | 0 full, 1 dot, 2 hide. VR default 2 with our own reticle; 1 for a small centre dot |
| `hud_reticle_scaleOverride` / `_styleOverride` | 0x4641150 / 0x46410D0 | 0 / -1 | size of the game's reticle on the quad |
| `g_showHud` | 0x463AAC0 | 1 | hides all HUD elements (debug; T-043 captures) |
| `g_setting_hud_show`, `g_setting_hud_preset` | 0x45F46E0, 0x4696000 | 1, 4 | menu settings: HUD on/off; preset 0 none .. 4 all, 5 custom |
| `g_setting_hudNotifications`, `g_setting_hud_*` | 0x45F4980, ... | 1 | per element (powerups, tooltips, ability indicators, mission challenges) |
| `g_setting_objectiveMarkers`, `g_setting_interact_prompt` | 0x45F4900, 0x45F4760 | 1 | screen-projected markers (T-058) |
| `hud_globalAlpha` | 0x4642E50 | 1.0 | HUD opacity |
| `swf_safeFrame` | 0x47CBF60 | 0.005 | margin to the screen edge (region cutting, T-077) |
| `hud_subtitles_textSize`, `swf_useSubtitles` | 0x46413D0, 0x467DB20 | 1, 0 | subtitle size (0 small .. 2 large) |
| `hud_drawPerspective` (+ `Angle`, `OffsetX/Y/Z`, `Planes`) | 0x463D2B0 | 0 | the engine's tilted HUD; not needed with a quad |
| `swf_skipRender`, `swf_skipFilesMatching` | 0x47CC0E0, 0x47CC2E0 | 0, "" | drop all SWFs / those whose file name matches (debug) |
| `r_skipGuis`, `r_skipInGameGuis`, `r_skipAfterPostProcessSurfaces` | 0x6681D00, 0x6681D80, 0x6681980 | 0 | render-side skips (device context +0x4D9C81, +0x4D9C82, +0x4D9C7D) |
| `stereoRender_guiOffset` | 0x46C5160 | 0 | per-view GUI shift of the dormant stereo path (section 3.3) |

**Stereo-correct crosshair.** On a head-locked quad at distance d the game's centred reticle lies on the
head's forward ray, so under head aim its direction is right, but its depth is d, not the target's: looking at
a distant demon the reticle doubles. Under hand aim (the launcher's default since M5) it marks the wrong ray
altogether. Options considered:

1. **v1 (recommended): `g_reticleMode 2`** on the launch command line, and a layer-drawn reticle: a small
   quad (or a dot in a separate `XrCompositionLayerQuad` from our own swapchain) on the aim ray at the
   convergence distance, resized with distance to keep its angular size. The distance comes from the depth
   at the screen centre (the eye's `_viewDepth` read back one frame late) or a fixed 10 to 20 m to start;
   `hud_*` reticle cues that matter (lock-on, charge) move to a weapon panel later (research 12, "G").
2. **Crop instead of hide**: keep the game's reticle and show a centre rectangle of `_gui` on its own quad at
   the convergence distance, the rest of `_gui` (with that rectangle cleared) on the HUD quad. Keeps the
   per-weapon reticle art; same distance source. One shared image serves both quads (two `imageRect`s).

**v1 decision (implemented, live U6):** under hand aim the game's crosshair is **left out of the copy**
(a centred square of 12% of the target's height is cleared: the crosshair and the ability indicators around
it), and a **layer-drawn dot** (white with a dark ring, 64x64 static swapchain, premultiplied) is submitted as
its own quad in the weapon hand's **aim space**, 10 m along the ray (`ETERNALVR_UI_RETICLE_DISTANCE`), 1.0
degree across at any distance (`ETERNALVR_UI_RETICLE_SIZE`; `ETERNALVR_UI_RETICLE=0` turns it off). The
runtime placed it every frame from the controller pose, without game-side latency; since the aim-jitter
work (`aim-jitter.md`) it is placed in LOCAL from the shown frame's own weapon ray (smoothed, at the frame's
pose time), so it stays on the line of the gun drawn in that frame and of its shots. Since v0.1.7 it sits where
that ray meets the world (the head sweep's collision query, `src/vkcore/reticle_depth.hpp`; 100 m when the
ray is clear) instead of 10 m out: the eyes are about 0.3 m from the hand, so a fixed distance put far
targets over a degree off the dot (a player's Precision Bolt report). `ETERNALVR_UI_RETICLE_DISTANCE` is
now only the fallback without the query.
Hiding the reticle with cvars does **not** work from the command line: `+g_reticleMode 2` and
`+hud_reticle_scaleOverride 0.01` left the game's crosshair unchanged in U6 (the menu setting in the profile
wins), and `+g_showHud 0` left the HUD on (U3). Under head aim the game's crosshair stays on the quad
(direction right, depth d). Not yet: the hit distance (depth at the ray) for the dot, per-weapon cues
(lock-on, charge) on a weapon panel.

**The crosshair hold (`ETERNALVR_UI_CROSSHAIR`, 2026-10-12).** The mask took everything else the game draws
in that square with the crosshair: an objective marker straight ahead, the chainsaw's `NO TARGET` and the
low ammo texts, the ability indicators (players' report: "the invisible rectangle that blocks HUD objects at
the center of your view"). What the game draws there and what controls it [static: the HUD decls and SWFs in
the game's resources, the exe]:

- `idHUD_Reticle` (`swf/hud/hud_reticle.swf`): every weapon's reticle with its cues (lock-on, charge and heat
  bars, BFG kill count, chainsaw pips, the hit marker, scope overlays). Its update (vtable slot 7, 0xEFD230)
  picks a style every update: the weapon decl's, style 1 (dot) for `g_reticleMode 1`, **style 0, no reticle,
  for `g_reticleMode 2` and for the player's `hideReticle`**, and then `hud_reticle_styleOverride` when it is
  0 or more (clamped to 36), which replaces all of them; `hud_reticle_scaleOverride` above 0 scales it.
- `idHUD_AbilityIndicators` (`hud_abilityindicators_singlestack.swf`), its own element:
  `g_setting_hud_ability_indicator_type` 0 hides it, 1 to 4 show the SWF frames `version1` to `version4`
  (large, small, lower large, lower small; the registration's default is 3), per icon
  `g_setting_hud_ability_indicator_*` (0xED21B0). No reticle cvar touches it.
- `hud_lowwarning.swf` (`helmetTag reticle_display`, centre plane): LOW AMMO, OUT OF AMMO, LOW HEALTH and the
  chainsaw's NO TARGET, OUT OF RANGE, INVALID TARGET and OUT OF FUEL; no cvar of its own.
- `idHUD_POI` (`hud_poi.swf`, full screen, world-projected): objective markers (`g_setting_objectiveMarkers`),
  lock-on brackets on enemies, mortar markers. `idHUD_FocusInfo` sits at the centre anchor.

Why `g_reticleMode` cannot be held: it is a profile field. The profile's load runs
`idGameSettings::SetCVarsFromFields()` (0x6AB320), which wrote over U6's command line, and **every profile
save runs `idGameSettings::SetFieldsFromCVars()` (0x6ABA90, called from the `profile.bin` writer 0x14978E0)**,
which copies the live cvar into the profile, so a held 2 would hide the crosshair in the player's flat game
(the same function copies `g_setting_objectiveMarkers`, `g_setting_interact_prompt`, `g_setting_hud_show`
and the compass; the ability indicator settings go through the profile serializer 0x141D040). So no
`g_setting_*` or other profile field is held. `hud_reticle_styleOverride` is none of those: registration flags
0x2 (no save flag), not among idGameSettings' fields, no menu, and its registration and the reticle's update
are its only two references in the exe [static].

So by default (`ETERNALVR_UI_CROSSHAIR=hold`, `src/ui_layer/crosshair.hpp`, `src/vkcore/runtime_cvars_reticle.*`)
the layer holds `hud_reticle_styleOverride 0` with the other run-time cvars (Route S and Parallel Eye
Rendering) while the UI layer is on, only while the presenter asks for it on its GUI copies, that is under hand
aim; when hand aim ends (head aim, a demon piloted under head aim) or the last request is over 0.5 s old and 4
presents have read it (the copies stopped: VR off for the game, a failed target check; a single long frame is
one or two presents and does not end the hold), the value from before that hold's first write goes back. A
value set while no hold runs (a console style under head aim) is the player's: not written back.
The GUI copy keeps its centre once the cvar has read 0 for 8 presents in a row; until then, and again as soon
as a reading is not 0, the guard stops the writes, no present has read it for 0.5 s (a mono session with the UI
layer never reads it), or an `ETERNALVR_DEBUG_CVARS` value entry for the cvar (`name=value`; a `name=?` entry
only logs beside the hold) replaces the hold, the mask clears the square as before. Menus and
backdrops keep their exceptions. `ETERNALVR_UI_CROSSHAIR=mask` is the earlier behaviour unchanged. The cvar
is found by name like every held cvar: in the Game Pass image dump the layer's registration pattern finds it
once (default -1) and the reticle update's mode and override code matches byte for byte (0xF08DB2,
0xF08DFD) [static]. Log: the `ui: UI layer ...` line names the mode (`the game's crosshair under hand aim:
hold (...)` or `mask (...)`), `cvars: held at run time: ... hud_reticle_styleOverride 0 while hand aim is on
...`, each write `cvars: hud_reticle_styleOverride -1 -> 0 (reads 0); hand aim: the game's crosshair hidden`
(the first 13; the give-back ends `the hold's request ended (no hand aim) ...` or `the hold's request lapsed (no
GUI copy for 0.5 s and 4 presents) ...`), `ui: crosshair hold confirmed ...` / `not confirmed ...` on each
change, and the 10 s line's
`N for the crosshair hold (confirmed | not confirmed now, N change(s))`.

Rig (the RTX 3080 Ti test PC, OpenXR-Simulator, 2026-10-12; QA scenarios `crosshair-hold`, `crosshair-mask`,
`crosshair-hold-head` with the GUI target at 1280x1400; shots of the simulator's composed frame,
`<workspace>\QA-ch5\`, side by side in `compare\`):

- The hold reaches the game: `hud_reticle_styleOverride -1 -> 0 (reads 0)` at the first hand-aim copy,
  `crosshair hold confirmed` 63 ms later, and no game crosshair in any shot under hand aim. Under head aim nothing is
  written and the game's crosshair shows. The fallback stayed rare: 5032 of 5041 copies kept their centre (the 9
  before the confirmation), one change in the run, none through a pause, a death and the death screen.
- The chainsaw's NO TARGET with nothing in reach: `mask` cuts the middle of the banner out (the players' "OUT OF
  RANGE" cut at the square's edge), `hold` shows it whole, as head aim does. The Praetor Suit token's name and the
  ability indicators (the default lower layout sits below the square) show in all three. The map markers near the
  middle of the view at the token checkpoint sat just outside the square.
- Style 0 also takes out the reticle's own overlays: with the Heavy Cannon's Precision Bolt scoped, head aim shows
  the scope's arcs and RELOADING, `mask` leaves the arcs outside the square, `hold` shows none. The hit marker was
  not seen (no demon in the scene).
- The pause menu and the death screen show whole in every mode (no see-through square). At the suite's 1280x720
  window the square is half the angle a headset gets, so the scenarios run with `ETERNALVR_RENDER_SIZE=auto`.

**The mask and full-screen backdrops (bug B3).** Menus and cinematics draw a full-screen opaque layer into
`_gui` (the pause and settings backdrop, a cinematic's fade: alpha 255, colour 0 to 3). Masked, the square
showed as a see-through hole in a head-locked black rectangle. The mask is therefore off while a menu is up
(the game's cursor, the menu panel or its hold, `docs/VR_MENUS.md`) and while a **backdrop test** sees such a
layer: each GUI copy also copies 8 pixels of `_gui` into a small host-visible buffer (the corners and edge
midpoints of a square 4 px outside the masked one, `ui_layer/backdrop.hpp`, `vkcore/backdrop_probe.hpp`; up
to 4 readings in flight, each read once the shared timeline shows its copy done). A reading with at least 6
of the 8 alphas at 32 or more (any visible full-screen layer, a fade on its way in or out included) shows a backdrop; 2 such readings in a row switch the mask off, 8 without one
switch it back on. Latency: the copies of the first 3 or 4 frames of a backdrop are still masked. Over the
game those pixels hold nothing or the HUD's thin elements, and wherever they are opaque a hole would show, so
the test errs only toward showing the game's crosshair.

The other HUD elements are separated by fixed screen rectangles of the same capture (T-077: wrist panel,
message panel). `swf_safeFrame 0` makes the rectangles stable.

## 6. Recommended implementation

**Layer (Vulkan)** [implemented, section 7]:
1. At `vkCreateImage`, images created exactly like `_gui` (2D, `R8G8B8A8_UNORM`, 1 mip, 1 layer, 1 sample,
   usage exactly `COLOR_ATTACHMENT | SAMPLED | STORAGE`) get `TRANSFER_SRC` added and are recorded (size,
   usage, sharing, queue families). The engine's temporary image for its memory size query has the same
   profile, so its requirements stay consistent.
2. Their layouts are followed through the game's `vkCmdPipelineBarrier` calls per command buffer and applied
   in submission order at `vkQueueSubmit` (secondaries through `vkCmdExecuteCommands`).
3. At the present of a mono frame or of eye L (Route S), the GUI target is read from the game (renderSystem
   + 0x5C8 -> +0x10 -> idImage: format, size, flags, VkImage), checked (FMT_RGBA8, single image, a recorded
   candidate of the same size with `TRANSFER_SRC`, a known layout, concurrent over the copying queue's family
   or last used on it, the swapchain's size), and copied in the eye copy's command buffer, which already
   waits on the present's semaphores: `_gui` layout L -> `TRANSFER_SRC` -> L, into the ring slot's own
   D3D12-shared image.

**Engine** [implemented]: one mid hook at 0x1CDF7CB (`mov [rsp+0x70], rax`, rax = the GUI image the
composite is about to bind) that replaces rax with the image manager's `_black` while skipping is requested
and `mp_guard::allowsGameTouch()`, and only when rax is the GUI image read from the render target (else
nothing changes). It runs in every render frame, so eye L and eye R both lose the GUI.

**Presenter** [implemented, `presenter_ui.cpp`, small hooks in the ring, copy and frame code]:
- each ring slot has a UI image (D3D12 committed, shared, `R8G8B8A8_UNORM`, the swapchain's size) imported
  into Vulkan; it travels with the slot's eye image and its timeline value;
- the XR worker copies it into a UI swapchain (`R8G8B8A8_UNORM_SRGB`, bytes unchanged like the eye images)
  in the same D3D12 command list as the eye copy, and releases it with the eye image;
- `frame()` adds an `XrCompositionLayerQuad` after the projection layer: `VIEW` space (head-locked), pose
  (0, `ETERNALVR_UI_OFFSET_Y`, -`ETERNALVR_UI_DISTANCE`), `ETERNALVR_UI_WIDTH` wide, height from the aspect,
  `XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT` (premultiplied, OpenXR's default);
- the composite skip is requested only while that quad is submitted with a capture under 0.25 s old;
  menus, loading screens, a stopped capture and a guard trip bring the game's own composite back.

**Next steps (not in this branch)**: T-077's split into a wrist panel and a message panel (sub-rectangles of
the same swapchain image), the crosshair of section 5, the curved menu panel with the laser pointer (M6),
world-locked or lazy-follow placement, cylinder layers where supported, and the UI's own size (T-031:
`_gui` has the output size, which follows the window; once T-031 decouples the eye size from the window,
the window and with it `_gui` can stay 16:9, as T-031 asks; to confirm then).

**Formal gap, accepted**: the copy runs on the present queue right after the frame's work; the next frame's
clear of `_gui` (graphics queue, after its world rendering) is not ordered after it by a semaphore. The
margin is the whole next frame's scene work. If U2 ever shows a torn or empty capture, add a wait on the
shared timeline to the game's next graphics submit (our `vkQueueSubmit` hook already sees it).

## 7. What this branch implements

**On by default in Route S stereo** (`ETERNALVR_MODE=stereo` without an experiment; the launcher sets
`ETERNALVR_UI_LAYER=1` in stereo and in mono, where menus over the game need it for their panel), off otherwise; `ETERNALVR_UI_LAYER=0/1` overrides. Off, no hook is
handed out and nothing changes.

| Piece | Files | Tests |
|---|---|---|
| Settings (`ETERNALVR_UI_LAYER`, `_SKIP_COMPOSITE` default 1, `_DISTANCE` 1.5 m, `_WIDTH` 2.0 m, `_OFFSET_Y` 0), quad size | `src/ui_layer/ui_settings.*` | `tests/ui_layer/ui_settings_tests.cpp` |
| Layout tracker (per command buffer, submission order, secondaries, bounds) | `src/ui_layer/layout_tracker.*` | `layout_tracker_tests.cpp` |
| `_gui` profile, engine offsets, target checks | `src/ui_layer/gui_target.*` | `gui_target_tests.cpp` |
| Vulkan hooks (`vkCreateImage`, `vkDestroyImage`, `vkCmdPipelineBarrier`, `vkBeginCommandBuffer`, `vkCmdExecuteCommands`, `vkQueueSubmit`; chained with the shader dump's) | `src/vkcore/ui_vulkan.*` | |
| Engine: locate and cross-check, read the target, composite hook | `src/vkcore/ui_engine.*` | |
| Presenter: shared UI images, copy, UI swapchain, quad, skip policy, 10 s statistics | `src/vkcore/presenter_ui.cpp` (+ small calls in `presenter_copy/ring/seq/frame.cpp`, `xr_presenter.cpp`, `layer_entry.cpp`) | |
| `ETERNALVR_CAPTURE_UI=<dir>[,N]`: every Nth capture as an RGBA PNG | `src/vkcore/ui_capture.*`, `stereo_seq::encodePngRgba8` | `png_writer_tests.cpp` |
| Hand-aim reticle (settings, dot image, angular size, centre mask regions; the quad in the weapon hand's aim space) | `src/ui_layer/ui_settings.*`, `presenter_ui.cpp`, `controllers::weaponAimSpace` | `ui_settings_tests.cpp` |
| The game's crosshair under hand aim: the hold, its confirmation, the mask fallback (`ETERNALVR_UI_CROSSHAIR`) | `src/ui_layer/crosshair.*`, `src/vkcore/runtime_cvars_reticle.*`, `presenter_ui.cpp` | `crosshair_tests.cpp` |
| The mask's backdrop test (probe points, reading, hysteresis; the readback) | `src/ui_layer/backdrop.*`, `src/vkcore/backdrop_probe.*` | `backdrop_tests.cpp` |

**Fail closed**: any signature that does not match once, a GUI target slot that is not render system +
0x5C8, binds that load different parameters, or unexpected bytes at the hook site leave the engine
untouched and the UI layer off (`ui: ... UI layer off`). Per frame, a target that fails a check is not
copied (counted with the reason); without a fresh capture there is no quad and no skip. Image creation is
changed, the target read and the composite hook act only while the multiplayer guard allows touching the
game; the hook re-checks it every frame, and shutdown turns the skip off. The skip is a 250 ms lease the XR
worker renews every frame it shows the quad, so a stalled worker or runtime brings the game's own GUI
composite back by itself. Only the layout of the image the game currently uses as `_gui` is followed (`GUI
target is image ...`); every image created with its profile gets TRANSFER_SRC (264 in e1m2, 427 after a
resize), which is cheap.

**Log lines** (`ui:`): the prepared images (first 8), the located addresses and the hook RVA, `N shared GUI
image(s)`, `UI swapchain`, `first GUI image on the quad`, and every 10 s: captures, not captured (last
reason), images prepared, composites seen and composited without the GUI, target read failures, skip on/off,
and the backdrop test (`backdrop test: N reading(s), N backdrop(s) (up | none now), hand-aim copies not
masked: N for a backdrop, N for a menu`). The test's first switch each way is logged once with the 8 alphas:
`backdrop test, reading N (alpha ... around the crosshair square): a full-screen backdrop (menu or fade)
...` and `... the backdrop is gone; the crosshair mask is back under hand aim`.

## 8. Live experiments

Staging as in `docs/VR_STEREO.md` (`$common`, `tmp-vr\seq1`); each run from its own PowerShell call, ended
with `& tools\rig\stop.ps1 -Run latest`; map `game/sp/e1m2_battle/e1m2_battle` for combat HUD.

| # | Run | Pass |
|---|---|---|
| U1 | Mono head-tracked, `-ExtraEnv 'ETERNALVR_UI_LAYER=1','ETERNALVR_UI_SKIP_COMPOSITE=0','ETERNALVR_CAPTURE_UI=<workspace>\tmp-vr\u1-ui,60'`, 2 min of e1m2 with a fight, a subtitle and the weapon wheel | log: `GUI target slot at RVA 0x66E31F8`, prepared images 1 or a few (note the count), captures = presents, 0 not captured after the first second; PNGs: HUD, crosshair, subtitles on transparent black, alpha premultiplied (colour <= alpha); the eye images still carry the HUD |
| U2 | Route S: `-Stereo -CaptureEyes '<workspace>\tmp-vr\u2-eyes,120' -ExtraEnv 'ETERNALVR_UI_LAYER=1','ETERNALVR_CAPTURE_UI=<workspace>\tmp-vr\u2-ui,60'` (skip on by default), 2 min of e1m2 | eye PNGs of both eyes without HUD; `composites N, M without the GUI` with M close to N during play; the quad in the simulator preview shows the HUD; no torn or empty UI PNGs |
| U3 | U2 in the Fortress of Doom and a level with glowing world screens/holograms; again with `+r_skipAfterPostProcessSurfaces 1` | which world surfaces live in `_gui` (after-post pass): they vanish from the eyes and show on the quad in U2, and vanish from the UI PNG with the cvar. Decide: acceptable, or composite only the after-post part (would need the GUI pass split, not available) |
| U4 | Headset (Quest 3): quad legibility at 1.5 m / 2.0 m wide; `ETERNALVR_UI_DISTANCE`, `_WIDTH` sweeps; brightness against the in-image HUD (the composite's intensity boost is not applied to the quad) | readable HUD and subtitles; note the settings; edge fringes of premultiplied alpha acceptable |
| U5 | Pause menu, main menu, a loading screen, a cutscene | the quad disappears within about 0.25 s, the cinema screen shows the menu with its GUI (composite back), and returns after resuming |
| U6 | `+g_reticleMode 1`, then `2`; `+hud_reticle_scaleOverride 0.5` | the reticle changes on the quad; diplopia at far targets with the full reticle on the quad (motivates section 5) |
| U7 | Window resize or `vid_restart` equivalent during U2 | new `prepared` line, captures resume after one frame (layout learned from the first barrier), no validation error |
| U8 | `ETERNALVR_GUARD_TEST_TRIP_MS=60000` during U2 | at the trip: skip off, quad gone, HUD back in the eyes, cinema screen |
| U9 | U2 under the Khronos validation layer for 30 s | no error from the UI copy barriers or the imported images |

Order: U1, U2, U5 and U8 in one session decide whether the UI layer can become the default for Route S;
U3 and U4 decide placement work (T-077); U6 feeds the crosshair choice.

## 9. Signatures (this build, each unique in `.text`)

| Name | RVA | Signature |
|---|---|---|
| Composite loads the GUI image (hook at +0x12 = 0x1CDF7CB) | 0x1CDF7B9 | `48 8B 05 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 48 8B 40 10 48 89 44 24 70 E8 ?? ?? ?? ?? 25 01 00 00 80` |
| Composite binds guiMap from `[rsp+0x70]` | 0x1CDFBFC | `4C 8B 44 24 70 49 8B CF 48 8B 15 ?? ?? ?? ?? 49 81 C0 C8 00 00 00 E8 ?? ?? ?? ?? 48 8B 15 ?? ?? ?? ?? 4C 8D 83 C8 00 00 00` |
| Composite's no-GUI bind of `_black` (image manager, guiMap) | 0x1CE0178 | `41 83 BD F4 02 00 00 02 75 21 48 8B 05 ?? ?? ?? ?? 49 8B CF 48 8B 15 ?? ?? ?? ?? 4C 8B 40 20 49 81 C0 C8 00 00 00 E8 ?? ?? ?? ??` |
| `_gui` creation | 0x1CD20F6 | `48 8B 05 ?? ?? ?? ?? 4C 8D 44 24 70 48 8B 0D ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 80 65 A9 FE 48 89 45 B8 8B 85 BC 00 00 00 89 44 24 7C 8B 85 B4 00 00 00 89 45 80 44 89 64 24 70` |
| idRenderModelGui build surfaces (clears the pending list) | 0x194D0B0 | `40 55 57 41 55 41 57 48 8D 6C 24 98 48 81 EC 68 01 00 00 80 89 B0 00 00 00 80 4C 8D B9 88 04 00 00 33 FF 4C 8B E9 89 B9 04 46 00 00 45 8B 47 0C 45 85 C0 79 2B` |
| Screen-views pass: GUI model commit loop | 0x1C75A20 | `48 8B 44 24 70 45 33 C9 45 33 C0 33 D2 48 8B 1C 06 48 8B CB 48 8B 03 4C 89 B3 40 01 00 00 FF 90 90 00 00 00 48 8B 8B A8 00 00 00 83 FF 18` |
| Screen-views pass: GUI list append | 0x1C757DF | `49 63 44 24 40 41 3B 44 24 44 7D 1C 49 8B 4C 24 38 48 8B D0 48 8B 83 A8 00 00 00 48 89 04 D1 41 FF 44 24 40` |
| GUI pass setup and draw | 0x1C572A0 | `48 89 5C 24 18 48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 60 4C 8B 69 50 48 8B 11 4D 8B 65 30 4D 85 E4 74 0C 49 63 84 24 90 89 02 00 48 8D 14 C2 48 8B 02` |
| GUI pass job | 0x1C57D90 | `48 89 6C 24 20 56 57 41 56 48 83 EC 50 48 8B 79 20 48 8B 71 08 0F B6 69 14 4C 8B 71 18 48 85 FF 74 0D 83 BF 44 99 02 00 00 0F 8F ?? ?? ?? ??` |
| After-post pass on the GUI target (clear + draw) | 0x1C92000 | `48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 60 48 8B 29 48 8B F9 48 8B CD 4C 8B B5 00 01 00 00 E8 ?? ?? ?? ??` |
| GUI buffer begin frame | 0x1C3B320 | `40 53 48 83 EC 20 80 B9 C0 25 00 00 00 48 8B D9 0F 84 ?? ?? ?? ?? 48 63 81 90 25 00 00 85 C0 78 23 48 6B C8 68 48 03 CB E8 ?? ?? ?? ?? 48 63 83 90 25 00 00 48 83 C0 03 48 6B C8 68 48 03 CB E8 ?? ?? ?? ?? 8B 83 A8 25 00 00` |
| Frame stage 18 advancing it | 0x43A9DB | `48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 90 F0 02 00 00 48 85 C0 74 0C 48 8B 88 28 02 00 00 E8 ?? ?? ?? ??` |

The layer locates the first three and cross-checks them (slot = render system + 0x5C8, the same guiMap
parameter in both binds, all within 0x1000 bytes, the store bytes at the hook site).

## 10. Open items

1. **After-post surfaces in `_gui`** (U3): which world materials render after post processing into the GUI
   target. With the composite skipped they leave the eyes and appear on the quad from eye L's viewpoint.
2. **Composite intensity boost**: the upsample may scale the GUI (the frame study's "magic number"); the
   quad shows the raw target. U4 compares; a brightness factor on the D3D12 copy would need a shader.
3. **Premultiplied alpha in display space** blended by the runtime (sRGB swapchain) gives slightly different
   edges than the game's blend. U4.
4. **Camera cuts** skip the clear and the GUI pass (renderView + 0x29944), so `_gui` keeps the last GUI for
   those frames; the quad shows it for them, as the game's composite would.
5. **Size** (T-031): `_gui` follows the output size; the shared UI images and the UI swapchain follow the
   game's swapchain size and are rebuilt with the ring.
6. **Not the ghost** (owner's first headset session, 2026-09-26): the faint, off-centre full-scene copy he
   saw was suspected on the head-locked quad. The GUI captures stayed clean in every flow tried on the rig
   (straight into e1m2, title screen to Continue, pause and resume, his 1415x1440 window): 2.2 to 3.4% of
   pixels covered, no scene. The copy was in the eye images: TAA, left on by his settings, read the other
   eye's history (`docs/VR_STEREO.md`, Cvars and Ghost check).

## 11. Live results (2026-09-26, rig, OpenXR-Simulator, build 25216728)

Runs `<workspace>\runs\20260926-052558-u1` to `-055907-u9`; layer logs in
`<workspace>\tmp-vr\ui1-logs` to `ui4-logs`; captures and simulator views in `tmp-vr\u1-ui`,
`u2-eyes`, `u2-ui`, `u2-sheet.png`, `u2-simulator.png`, `u5-*.png`, `u6c-sim.png`, `u7-sim.png`, `u8-game.png`,
`u3-ui`, `u3-sheet.png`, `u9-validation.txt`. Stereo runs e1m2 at about 105 ticks/s (210 renders/s), XR 90 Hz.

| # | Result | Evidence |
|---|---|---|
| U1 | **Pass.** Target located (`GUI target slot at RVA 0x66E31F8`, guiMap 0x39AB4F0, image manager 0x5BF13C8); every present captured after the first (1 not captured: layout not known yet). PNGs: HUD, crosshair, FPS counter on transparent black; 2.7% of pixels covered; colour <= alpha except where alpha is 0 and colour is not (additive glows; 0.3 to 0.5% of pixels, up to 128): premultiplied with additive parts, which OpenXR's premultiplied blend reproduces. First run found a bug, fixed: 264 images share `_gui`'s creation profile, so following all of them overflowed the tracker (the real one was not followed); now only the current target is followed. | `u1-ui\`, log `ui2-logs\eternalvr-...-11524.log` |
| U2 | **Pass.** Both eye images without HUD (0 health-block pixels in both), the UI capture with it, the simulator's right eye (which never had a HUD in Route S) shows HUD, crosshair and FPS counter on the quad; `composites 23812, 22328 without the GUI` (the rest: loading); 0 torn or empty captures in 118 PNGs. | `u2-sheet.png`, `u2-simulator.png` |
| U3 | **No after-post surfaces seen.** With the HUD on the quad, the UI captures in the e1m2 spawn and the Fortress hall during a 120-degree head sway kept a constant 2.06% coverage in the same HUD rectangles: nothing world-space drew into `_gui`. Loading screens are full-screen opaque `_gui` images. Not yet tried: holograms and screens in later levels. | `u3-ui\`, `u3-sheet.png` |
| U4 | Not run (headset). | |
| U5 | **Pass.** Pause: head-tracked, black world, pause menu readable on the quad; resume: HUD back on the quad. Exit to main menu: frames go to the cinema screen, `skip off`, composites no longer skipped. One stall, caused by the test itself: key presses went to the simulator's own window (same process) and opened its menu, whose modal loop blocks the XR worker; the skip then stayed on because it was a flag the worker set. Fixed: the skip is now a 250 ms lease. | `u5-pause-sim.png`, `u5-resume-sim.png`, `u5-menu-*.png`, dump `u5-stall2.dmp` |
| U6 | **Pass with the v1 reticle.** `+g_reticleMode 2` and `+hud_reticle_scaleOverride 0.01` did not hide the game's crosshair; the centre mask does; the dot sits on the simulated controller's ray. | `u6b-sim.png`, `u6c-sim.png` |
| U7 | **Pass.** Game window 2586x2171 to 2080x2139: swapchain 2054x2068, ring and shared GUI images rebuilt, new target followed one frame later, captures resumed, quad correct. | `ui4-logs` at 124 s, `u7-sim.png` |
| U8 | **Pass.** Test trip at 110 s: `skip off`, target reads stop, composites no longer skipped, cinema screen, HUD back in the game's image. | `ui3-logs`, `u8-game.png` |
| U9 | **Pass for the UI layer.** 3 minutes under the Khronos validation layer (hand aim, mask, reticle): no message about the UI copy, its barriers, `vkCmdClearColorImage` or the imported images. The game's own errors are listed (ray tracing scratch overlap, sampler pNext, a swapchain semaphore reuse in its own submits). | `u9-validation.txt` |

**Through the launcher** (main after the merge, Release launcher, layer staged in `tmp-vr\launcher-layer`,
OpenXR-Simulator, `+map game/sp/e1m2_battle/e1m2_battle` as extra arguments): the launcher sets
`ETERNALVR_UI_LAYER=1` with stereo, controllers and hand aim; the HUD shows on the quad in the simulator, the
game's crosshair is left out and the dot sits on the controller ray; `composites 17733, 16267 without the
GUI` at 97 s. Evidence `<workspace>\tmp-launcher\ui-live\L-ui` (simulator.png, layer-logs).

Rig notes: in mono (FIFO) the game runs at 4 fps on the virtual display while the owner's monitor is off (no
display consuming frames): a baseline without the UI layer showed the same; stereo's immediate present mode
is unaffected. The launch helper's HUD detector no longer fires once the HUD leaves the eye images.

## 12. The red sheet at low health: the GUI target's additive wash

Owner's session 2026-09-27 (captures `capture-20260927-1808*`, health 34 and 23, LOW HEALTH showing): the
headset showed a flat red rectangle exactly over the HUD quad. The quad's image is the GUI target copied
byte for byte (`vkCmdCopyImage` into the shared image, `CopyTextureRegion` into an R8G8B8A8 sRGB swapchain,
no shader) and submitted with `XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT` and no
`XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT`, so the runtime blends it premultiplied:
`out = ui.rgb + world * (1 - ui.a)`. That matches the game: in every capture without damage the colour is
within the alpha everywhere but the health pips' glow (1750 pixels, alpha 29 to 64).

At low health the game adds a red vignette to the whole target with alpha 0: colour (1, 0.315, 0.112) times
0 at the centre up to 93 of 255 at the edges, on 94 % of the pixels (the cvars `view_skipDamageEffect`,
`view_showPlayerDamageViewEffect` and `view_damageBlur` do not stop it). Premultiplied, colour without alpha
is added light, so the quad lights a red sheet over the world; on the flat screen it only tints the scene.

A plain clamp (`rgb = min(rgb, a)`) removes it but also the pickup notification's icon (drawn the same way,
additive with alpha 0) and the pips' glow. The layer instead removes only light that fills the whole
target: `ui_layer::removeAdditiveWash` (additive_wash.hpp), per channel, subtracts from each pixel's
excess over its alpha the smallest excess found in its 64x64 block, taking the largest of those minima over
the block and its neighbours. HUD pieces smaller than a block keep their light; the vignette goes down to
the few levels it changes across a block. `vkcore::UiWash` runs the same rule in two D3D12 compute passes
on the XR worker before the copy (not while a menu is up); on the three captures its output is byte for
byte the reference's, and the capture without the vignette comes out unchanged. `ETERNALVR_UI_WASH=0`
turns it off. Section 13 revises the rule (three passes now). Evidence:
`<workspace>\tmp-vr\redfix` (before and after composites over the eye images, the plain clamp
beside them).

The eye images carry a second, separate red vignette at low health (scene side, in both eyes); that is a
game view effect, not the GUI target, and needs a cvar. Static reading of this build (not yet run):
`view_skipDamageEffect` is read only by the damage view material (`idView::InitMaterials`' overlay,
distortion and aberration maps), while `g_skipViewEffects` ("skip damage and other view effects", default
0, cvar object RVA 0x463ACC0) is read at RVA 0x1481D43 and passed as the skip flag into the per-frame view
effects (RVA 0x147DB10), where it jumps past the double vision, the view shakes, the loop over the 8
screen overlay layers ("Overlay material, uses $overlayColorMap and $overlayFlowMap") and the 3D damage
ring. The candidate is `g_skipViewEffects 1`, tried first with `ETERNALVR_DEBUG_CVARS`; the HUD's
directional arcs are SWF (`idHUD_DirectionalFeedback`, `hud_showDamage`) and should stay.

## 13. Squares on the HUD quad when hit: the wash rule, revised (2026-10-11)

Owner's report (headset, 0.1.46, Route S and Parallel Eye Rendering): in The Ancient Gods parts 1 and 2,
square, odd damage effects on the HUD panel when taking damage. Rig repro on the second test PC (OpenXR
simulator, 2048x2208, GUI target captured raw every 2 s, no god mode, the player teleported into hazard
volumes: `game_envhazard_volume_electrical_blue_1` in e5m2_earth, `destroyed_rig_envhazard_volume_electrical_blue_1`
in e4m1_rig, `trenches_envhazard_volume_fire_3` in e1m2_battle as the base game control).

What draws it [rig]: nothing DLC-specific. Every hit brings the HUD's full-screen damage feedback into
the GUI target: the red vignette (alpha 0) and a translucent frame in the top-left corner (about 80 x 150
pixels, coverage 3 to 54), on top of the directional wedge; at low health the vignette stays and the hex
pattern behind the LOW HEALTH banner appears. `hud_showDamage 0` removes the hit vignette, the frame and
the wedges together (the low-health vignette stays); `view_enableHelmetFX 0` changes nothing in the GUI
target. The DLC's hazards and demons hit harder, and the hit vignette scales with the hit: about 42 of
255 on average after an electrical hit in TAG1 and TAG2 against about 6 after the first fire hits in
e1m2 (mean colour over the target), which is why it shows far more there. The DLC's screen particles
(`fx/player/screen_damage` items such as `screen_effects/electric_damage`, `berserk_slash_1p`) use
world-pass materials (`MATERIALPASS_BLEND`), so they render in the eye images, not the GUI target
[static; the eye images were not checked for them].

Why squares [rig, code]: the rule of section 12 took one level per 64x64 block. HUD pieces larger than a
block (the hex pattern, the panels' glow) lift some blocks' floors, so the wash taken changed in steps at
the block edges: rectangles around the banner and beside the panels. And the vignette lies behind the HUD
(on captures the light a hit adds to a covered pixel is the vignette's times 1 - alpha, at every
coverage), so where HUD covered a pixel partly the rule, which only took light above the coverage, left
the tint: the corner frame and the panels' backgrounds lit as orange boxes (corner frame red 30 of 255
after the 0.1.46 rule on a TAG2 hit).

The revised rule (`additive_wash.hpp`): a block's floor is the least of `(colour + 1/2) / (1 - alpha)`
(the most wash a pixel can hold behind its coverage; the half is the byte's rounding, which matters under
high coverage, where a wash leaves a colour of only 0 to 2), read only from blocks with an uncovered pixel;
a block's level is the largest floor of the known blocks around it, kept when those with light outnumber
those without two to one (a glow filling a block or two without a vignette stays); the level is
interpolated bilinearly between block centres; each pixel gives up `wash * (1 - alpha)`.

Measured with the C++ reference on the captures (raw / 0.1.46 / revised). A TAG2 hit against the same view
without damage: light above that view 186.2 M / 6.9 M / 3.9 M levels summed over the target, light below
it 11.6 M / 13.6 M / 14.1 M (the raw figure is the two views' own difference, so the revised rule takes
about 2.6 M levels more than it, mostly the vignette's share behind solid HUD, against 2.0 M for 0.1.46),
pixels more than 16 levels above it 3.68 M / 68 k / 20 k (most of what is left is the wedge, kept by
design), corner frame red 45 / 30 / 4. At low health: block seams (the mean step across block edges less
the same inside blocks) 0.14 / 0.57 / 0.10 in TAG2 and 0.15 / 0.56 / 0.10 in e1m2, vignette left
69 / 0.5 / 0.3 and 92 / 0.5 / 0.3 levels; solid HUD loses 6 to 7 levels of red (the vignette's share
behind it). A capture carries no wash when the 0.1.46 rule leaves it byte for byte unchanged: 369 of the
446 RGBA GUI captures (the runs above and earlier rig sessions; GUI sizes 2048x2208, 2056x2216, 2496x2688
and 1664x1792), and the revised rule leaves all 369 byte for byte unchanged too. The D3D12 passes match
the C++ reference byte for byte on all 446 (a scratch D3D12 harness running `vkcore::UiWash` on the
captures). At 1664x1792 (runs `dd-s7a`, without damage, and `dd-s7b`, a TAG2 hit, the layer's own filter
running): 45 captures without a wash unchanged; on a hit corner frame red 42 / 21 / 4 and seams
0.21 / 0.08 (0.1.46 / revised), at low health seams 0.48 / 0.06. Evidence: `tmp-vr\research\dlc-damage`
on the second test PC (`cmp-*.jpg`, `washlab.py`), runs `tmp-vr\rs\dd-*`. `ETERNALVR_UI_WASH=0` still turns the
filter off.
