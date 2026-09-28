# 13: DOOM Eternal input pipeline, aim, and reliable VR input injection

Status: research notes, 2026-09-25. Nothing here has been run against the game yet. Tags: **[C]** =
confirmed from source code, a primary dump or a first-hand document we hold locally; **[U]** =
unverified, inferred, or confirmed only for an older build. Every [U] that matters has a task in
section 10.

Engine-access mechanics (typeinfo resolver, RTTI map, string-anchored scans, console unlock) are
covered in `03-doom-eternal-internals.md` and are not repeated. The control map itself is in
`06-vr-gameplay-comfort.md` section 4; this document is about how those actions get into the game.

Reference material for this topic is in `reference/input/` (see `reference/input/MANIFEST.part.md`):
the PCGamingWiki input rows, 202 input cvars grouped by purpose, the usercmd button mask and input
event enums observed by existing mods, and a map of the DOOM 3 BFG usercmd code.

---

## 1. Summary

- **Gamepad input is plain XInput from `XINPUT1_3.dll`**, imported by name, only `XInputGetState`,
  `XInputSetState` and `XInputGetCapabilities` [C: Meathook proxies exactly these three by name; the
  Archipelago client checks `XINPUT1_3.dll` is loaded in 2026]. No native Steam Input API, no
  DirectInput pads, no DualSense features [C: PCGamingWiki]. Mouse is raw input with no acceleration
  [C: GyroWiki, PCGamingWiki]. DOOM 2016 used `XINPUT1_4` by ordinal, so an IAT patch written for 2016
  does not apply unchanged.
- **Stick look is a rate controller with a deadzone, curve, smoothing, edge acceleration and
  circle-to-square mapping**, and **aim assist (adhesion, friction, target snapping) is on by default
  for joystick input** (`g_setting_aim_assist 1`; 2 = "joystick only") [C: cvar dump]. Both make stick
  injection a poor carrier for exact yaw; on DOOM 2016 this meant pinning six `joy_*` cvars and closing
  the loop on measured yaw, still with hardcoded RVAs.
- **The engine has a usercmd layer we can write directly.** Eternal's usercmd carries a 64-bit
  semantic button mask (fire, melee, dash, each weapon slot, equipment, hammer, jump...), analog
  `forwardmove`/`rightmove` and view angles [C: Advanced Options enum for Rev 3; field names in
  typeinfo]. The usercmd generator is reachable by name as `engine_t::usercmdGen` [C: Meathook
  `engine_t` dump]. `usercmd_t` itself is not reflected, so its layout is an RE task.
- **Recommendation: engine-level usercmd writing as the primary path, XInput IAT patch as the
  always-installed secondary** (rumble capture, menu gamepad navigation, and a degraded fallback when
  the usercmd hook fails validation). No ViGEm, no `SendInput`.
- **Decoupled aim: make the player's view angles follow the weapon controller and render the camera
  from the headset.** Every aim consumer in the engine (hitscan, projectiles, meathook target,
  lock-on, glory-kill focus, use prompts) keys off the player view, so one override covers all of
  them. Then move the fire origin to the tracked muzzle (engine flags `useMuzzleAsFireAxis`,
  `useMuzzleDirForFireAxis`, `fireFromMuzzle` exist [C: names]) and swap to the head ray for the
  shoulder-mounted equipment.
- **Menus accept the mouse, but the menu cursor is driven by relative deltas with its own
  sensitivity**, not by the OS cursor [C: PCGamingWiki note; `m_menu_sensitivity`]. Laser pointing
  should write the engine cursor position (`engine_t::cursor`) rather than move the Windows cursor.
- **Rumble arrives via `XInputSetState`** (two motors, no hand information) and only after a
  180-frame connect delay (`win_joystickRumbleFrameDelay`) [C: cvar]. Richer per-hand haptics can come
  from the rumble decls/events (`idDeclPlayerProps::controllerRumble_t`, `ae_setControllerRumble`).

---

## 2. How DOOM Eternal reads input on PC

| Device | API / path | Evidence | Conf. |
|---|---|---|---|
| Xbox-style gamepad | XInput, `XINPUT1_3.dll`, three functions by name | Meathook `main.cpp` exports exactly `XInputGetState`, `XInputSetState`, `XInputGetCapabilities` with plain `dllexport` (no ordinals) and loads System32's copy; `WINEDLLOVERRIDES="XINPUT1_3=n,b"`; AP client checks the module (2026) | C |
| PlayStation / Switch / Steam Controller | Only through Steam Input's XInput emulation | PCGamingWiki: PS controllers "hackable", Steam Input API false, Steam hook input "limited" | C |
| Mouse | Raw input, no acceleration, optional smoothing (`m_smoothing`, `m_smooth`) | GyroWiki "Raw Input: Yes"; PCGamingWiki | C (third party) |
| Keyboard | Win32 key events into the engine event queue (`idInputLocalWin32`, `SE_KEY`/`SE_CHAR`) | Meathook input hook | C |
| DirectInput / SDL | No evidence of either | PCGamingWiki DirectInput rows blank; no SDL strings known | U |
| Tracked controllers | Not supported, but dormant VR input exists: `SE_VR` event type, `keyNum_t` block `K_STEAMVR_PRIMARY_*`, `K_PSMOVE_*`, `K_PSAIM_*` (0x12C-0x17C), `idVRInput`, `idVRController`, `vr_controllerMovement`, `vr_dominantHand` | Meathook enums (reflected), type names, KEX cvars | C names / U live |

Other behaviour that matters for injection:

- **Mixed input.** `in_MarkJoystickInactiveOnMouseInput 1` (default): any mouse event marks the
  joystick inactive, which flips prompts to keyboard glyphs and switches aim assist mode.
  PCGamingWiki: "Button prompt flickers when both inputs are used simultaneously" and "when gamepad
  is active, weapon wheel accepts gamepad input only" [C]. Any design that mixes synthetic mouse and
  gamepad has to set this cvar to 0 or choose one device per context.
- **Focus.** `in_requireGameWindowActive 1` clears input when the window loses focus;
  `in_controlInactiveWindow 0` blocks joystick input to an unfocused window; `in_noFocusJoystickInput`
  and `sys_toggleMenuOnMinimize` also exist [C: cvars]. VR players often have another window
  focused (runtime dashboard, Virtual Desktop). One workaround is to focus the game window once at
  launch. We should set `in_controlInactiveWindow 1` and `in_requireGameWindowActive 0` and
  verify whether usercmd-level writes bypass the focus gate anyway [U].
- **Glyphs.** Xbox prompts only; PlayStation/Switch prompts need a data mod (Nexus 279) [C].
  `swf_platformOverride` (0 xbox, 1 ps, 2 pc, 3 switch, 4 stadia) is a test cvar; whether PS art ships
  in the PC data is [U].
- **Controller layouts.** Presets (Default, Tactical, and others) plus full remapping; the layout
  labels are an in-memory list of `idStr` bind labels [C: Advanced Options `ControllerLayout.h`].
  Bindings map keys to usercmd buttons; injecting at the usercmd level makes us independent of them.

---

## 3. Gamepad aim behaviour, and why the stick is a bad carrier for VR yaw

### 3.1 Look pipeline [C: cvar names and dump values; order U]

Right stick -> `joy_deadZone` (0.15) with `joy_mergedThreshold` / `joy_mergedDeadZoneAngle` (10 deg
angular deadzone) -> `joy_circleToSquare` (2) with `joy_circleToSquarePower` -> response curve
`joy_gammaLook` (log) or `joy_powerScale` (2) -> `joy_range` -> rate `joy_yawSpeed` (cvar range
100-600 deg/s; menu slider 40-540, default 240) and `joy_pitchSpeed` (menu 30-310, default 120) ->
`joy_dampenLook` limited by `joy_deltaPerMSLook` -> "Look Smoothing" = `joy_smoothingEnabled` +
`joy_smoothingAcceleration` (menu 600-7600) -> edge acceleration (`joy_edgeAccelerationThreshold`
0.9, `joy_edgeAccelerationScalar` 0.4 = +40 % turn rate, `joy_edgeAccelerationAcceleration` 520) ->
integration by `joy_useGameDeltaTime` or wall-clock delta -> aim assist friction/adhesion.

DOOM 3 BFG's `HandleJoystickAxis` shows the core of this: `viewangles[YAW] += MS2SEC(pollTime -
lastPollTime) * lookValue * joy_yawSpeed` (`reference/input/doom3bfg-usercmd-reference.md`). The angle
turned depends on frame time, curve state and history. To hit an exact yaw through it, an injector has
to linearise every stage and then correct against the measured result.

### 3.2 Aim assist [C: cvars, type names; strengths U]

- `g_setting_aim_assist` 0 off / 1 on / 2 joystick only. The menu slider writes
  `ui_settings_controls_internal_aa_scale`, which bridges `aa_OPTIONS_AdhesionScalar` and
  `aa_OPTIONS_FrictionScalar`. Per-weapon settings live in `idDeclAimAssist`
  (`aimAssist_Adhesion_t`, `_Friction_t`, `_Tracking_t`, `_Selection_t`, `_Melee_t`, zoom variants);
  `aa_targetForceUseDefaults` forces cvar values instead.
- **Adhesion** drags the view with a target once the player moves faster than
  `aa_targetAdhesionPlayerSpeedThreshold` (0.19 m/s) or looks faster than
  `aa_targetAdhesionPlayerLookSpeedThreshold`. **Friction** scales look rate down near targets.
  `aimAssist_enableHighFPSAdhesionFix` caps strength above 60 fps.
- **Target snapping** (menu "Target Snapping"): `aa_OPTIONS_TargetSnapAllowed`, `aa_targetZoomSnapMode`
  (0 time-based, 1 full lock-on) - snaps the view to a weak point or enemy when zooming with mods such
  as Precision Bolt or Sticky Bombs.
- Community reports say aim assist is controller-only and on at maximum by default [C: Steam
  discussion 2021, Steam guide 2021].

Why this fights motion aim: if our turning or movement travels through the XInput stick, the game
treats the player as a joystick user and applies adhesion (rotating the view, which in our design is
the weapon aim) and friction (turning slower near enemies than the player asked for). Snapping would
yank the aim on zoom. If our angles come through the usercmd layer and the stick is idle, aim
assist may still be computed from the device state [U]. Either way the settings layer must force
`g_setting_aim_assist 0`, both `aa_OPTIONS_*Scalar 0` and `aa_OPTIONS_TargetSnapAllowed 0`, and
re-assert them after the options menu is closed.

### 3.3 Lessons from VR input on DOOM 2016 [C]

- Pinned at launch: `+joy_yawSpeed 600 +joy_deadZone 0.15 +joy_dampenLook 0 +joy_gammaLook 0
  +joy_smoothingEnabled 0 +joy_edgeAccelerationScalar 0` (a linear, unsmoothed native carrier), and re-held every
  frame by writing the cvars' current-value RVAs.
- Smooth turn is computed in OpenXR time; the game body is dragged after it by converting remaining
  degrees into stick magnitude `deadzone + desired/(600 * dt) * (1 - deadzone)` and snap angles are
  measured from DOOM's resulting player/body yaw. The stick path is used only as a
  catch-up carrier; the view is ours.
- The 600 deg/s cap makes a 45 deg snap take at least 75 ms of game frames.
- Movement through the left stick needed an atomic X/Y publish and a travel curve to reach full speed
  (1.03 "movement fix", root cause of the slowdown unconfirmed).
- Direct weapon keys: JOY7 and D-pad binds for `_weapN` were ignored by the game, so keyboard
  1-8 had to be synthesised instead; native Xbox-Y was treated as the BFG channel regardless of
  binding. Bind-level injection has
  hidden per-button semantics.
- Head-look via `SendInput` mouse deltas was also tried: pixels-per-radian was
  tuned by feel (1200 -> 2400 -> 3000), and a closed-loop body slew through `SendInput` went unstable:
  a feedback loop through SendInput with the render pipeline's latency inside it.

---

## 4. Injection options compared

| Option | Survives patches | Latency | Decoupled-aim support | Anti-cheat / AV risk | Complexity |
|---|---|---|---|---|---|
| (a) XInput virtual pad via IAT patch of `XINPUT1_3` | High: import by name, no game code touched; coexists with Meathook (patch our main-module IAT slot regardless of which DLL it resolved) | One XInput poll (~4 ms sampler in BFG) plus the joystick pipeline | Poor: rate control through curves and aim assist; exact yaw needs pinned cvars and a measured-yaw loop; no pitch/absolute aim | Low: in-process, no driver. Denuvo Anti-Cheat was removed; BATTLEMODE must stay off | Low |
| (a') Proxy `XINPUT1_3.dll` | High, but the slot is taken by Meathook and was used by Advanced Options | Same | Same | Low; dropped DLLs are more AV-visible | Low, conflicts with other mods |
| (b) ViGEm virtual controller | High (outside the game) | Same as (a) plus driver hop | Same as (a) | Kernel driver install with admin rights; ViGEmBus has been retired by its author [U: date]; system-wide device also seen by Steam Input and other apps | Medium, poor install story |
| (c) `SendInput` / synthetic raw mouse | High (outside the game) | OS queue plus raw-input read; frame-quantised | Yaw/pitch deltas are linear and fast, but still relative; needs calibration of `m_yaw * m_sensitivity`; marks the joystick inactive (glyph flip); global side effects if focus changes | Low | Low, fragile in practice (the `SendInput` feedback loop in 3.3) |
| (d) Engine usercmd hook (write buttons, analog move, angles) | Medium-high: anchor via `engine_t::usercmdGen` + RTTI vtable + validated slot; layout of `usercmd_t` per build | Lowest: same tick the cmd is built | Full: absolute angles and semantic buttons, bind-independent, no aim assist on stick | Low (in-process; singleplayer) | Medium-high (RE of the build step and layout) |
| (e) Direct write of `idPlayer` `viewAngles` each frame | High for the field (typeinfo name), medium for timing | Lowest | Full for angles only; fights `deltaViewAngles`, scripted cameras and prediction; buttons still need another path | Low | Medium |
| Event queue injection (`SE_KEY`, `SE_MOUSE`, `SE_JOYSTICK` via `idInputLocalWin32` QueueEvent) | High (RTTI vtable slot, Meathook-proven) | One event pump | Same limits as the device it imitates | Low | Low-medium; ideal for menus |

---

## 5. Recommended input architecture

Four layers, each independently validated at startup and disabled with a log line if it fails
(the degrade-not-crash rule from `03`).

**L0 - settings.** Applied at map load and after every options-menu close: `g_setting_aim_assist 0`,
`aa_OPTIONS_AdhesionScalar 0`, `aa_OPTIONS_FrictionScalar 0`, `aa_OPTIONS_TargetSnapAllowed 0`,
`in_controlInactiveWindow 1`, `in_requireGameWindowActive 0`, `in_MarkJoystickInactiveOnMouseInput 0`,
`in_joystickRumble 1`, `meatHook_playerViewOverrideMode 1` (see 6.4), and the joy_* linear carrier
values from 3.3 so the fallback path is predictable. Snapshot the user's values and restore them on
exit.

**L1 - usercmd writer (primary).** Hook the point where the usercmd generator finalises the current
command (BFG: `BuildCurrentUsercmd` -> `MakeCurrent`; Eternal slot to be found). Anchors:
`engine_t::usercmdGen` by typeinfo name -> object -> RTTI name `.?AVidUsercmdGenLocal@@` -> vtable ->
slot confirmed by a disassembly check; string xrefs and the Advanced Options `SendButtonPress` and
`idPlayer::ProcessInput` signatures as cross-checks. After the original runs we:
- OR our semantic buttons into the 64-bit mask (edge-latched so a press shorter than one game tick is
  never lost, as BFG's XInput sampler latches `buttonBits`);
- write analog `forwardmove`/`rightmove` from the locomotion stick, already rotated into the player's
  frame (6.2), bypassing joystick curves;
- add a view-angle delta so the player's resulting `viewAngles` equal our target (6.1);
- respect `idUCmdTracker` inhibit flags (`UCMD_INHIBIT_VIEW`, `_BUTTONS`, `_DASH`...): when the game
  inhibits a channel we stop writing it.

Validation self-test: inject a zero command and check that `idPlayer` state is unchanged; inject a
2 deg yaw delta and read `idPlayer::viewAngles` back; press the BUTTON_WEAP bit for a known slot and
read the selected weapon.

**L2 - XInput IAT patch (always installed).** Patch `XInputGetState`, `XInputSetState` and
`XInputGetCapabilities` in the game module's import table, matching by name (and by ordinal 2/3 as a
fallback). Report a connected virtual pad at user index 0 from process start so the
5-second reconnect poll and the 180-frame rumble delay are paid before gameplay. Uses:
- rumble capture (always);
- menu gamepad navigation when L3 cursor control is unavailable;
- weapon-wheel stick selection (the wheel takes gamepad input only);
- full fallback when L1 fails validation: a virtual pad with the L0 linear carrier and a
  measured-yaw loop. Documented as degraded (no exact aim, some latency).

Merge rather than replace a physical pad: our values win on axes while a VR action is active; buttons
are ORed.

**L3 - event queue (menus and bind-only actions).** Hook `idInputLocalWin32` QueueEvent (RTTI slot
0x60/8, Meathook-proven) to inject `SE_KEY` (`K_MOUSE1`, `K_MWHEELUP/DOWN`, `K_ESCAPE`, `K_JOY_*`) and,
if needed, `SE_MOUSE` deltas. Also useful to observe what the game receives when debugging.

**Not used:** ViGEm (driver, admin rights, system-wide side effects) and `SendInput` (global,
focus-dependent, marks the pad inactive, proven unstable in a closed loop).

**Threading.** OpenXR actions are sampled on our frame loop, predicted to the game's input time, and
published as one immutable snapshot with a sequence number. L1 and L2 read the latest snapshot;
button edges are latched per snapshot so the game thread and the XInput sampler (a separate thread in
BFG [U for Eternal]) each see every press exactly once.

---

## 6. Decoupled aim design

### 6.1 Frames and ownership

Three orientations: **head** (render camera, owned by OpenXR), **body yaw** (ours: smooth/snap turn
plus physical yaw policy from `06` 3.1) and **aim** (weapon controller). The game has only one: the
player view.

**Primary design: player view angles = weapon aim; render camera = head.** Each frame we set the
player's yaw/pitch to the weapon controller's forward axis (roll dropped; pitch clamped to the game's
limit), and the render hook builds each eye from head pose relative to the body frame, ignoring the
game's view angles (as in `03` section 6, render view path). This is the approach of UEVR's
controller-aim mode and of the DOOM 3 BFG VR fork's motion-control aim [C: BFG fork `Vr.cpp`
`CalcAimMove`, `Weapon.cpp` 4808-5058].

Why the view and not per-consumer hooks: in Eternal the view drives the hitscan trace, the aim point
that projectiles converge on, meathook target acquisition, rocket lock-on, the `focusTracker`
(`idPlayer.focusTracker.focusEntity`, which selects glory-kill and use targets), aim-assist target
selection and the crosshair. One override makes all of them follow the gun; the per-consumer
alternative (keep the view on the head, hook each consumer) needs a hook for every one of those and
breaks whenever one is missed.

Writing the angle: BFG computes `viewAngles = SHORT2ANGLE(usercmd.angles) + deltaViewAngles`, and the
game rewrites `deltaViewAngles` whenever it sets the view itself. So L1 adds `wanted - current
idPlayer::viewAngles` to the generator's accumulated angles instead of writing an absolute value.
That survives teleports, scripted view sets and save/load. Direct writes to `idPlayer::viewAngles`
(option e) are the fallback if the usercmd hook fails.

### 6.2 Consequences to handle

- **Movement direction.** The game moves relative to view yaw, which is now the gun. L1 rotates the
  locomotion vector by `(locomotion-frame yaw - aim yaw)` before writing `forwardmove`/`rightmove`,
  with the locomotion frame chosen per `06` (head by default, off hand optional). Dash uses the same
  movement vector, so dash direction stays correct.
- **Traversal checks.** Ledge grab and wall-climb detection look along the view; on DOOM 2016 the
  ledge trace can be redirected to the HMD (`pmec_lg_RequireLookAtLedge 0`, `pmec_lg_AssumeAlwaysForwardPress 1`).
  Check which Eternal traversal checks use view yaw [U].
- **Game-authored view changes.** Glory kills, chainsaw, Crucible, cinematics and the meathook pull
  set the view (`ae_setViewAnglesFromCamera`, sync start/end, `meatHook_playerViewOverrideMode`). While
  a sync is active or `UCMD_INHIBIT_VIEW` is set, stop writing angles; afterwards re-base our body yaw
  on the game's result.
- **Aiming at nothing.** When the weapon hand is untracked or holstered, fall back to head forward so
  use prompts and targeting still work.

### 6.3 Fire origin and direction

What the engine does [C: cvar descriptions; U: exact code]: `hands_adjustFirePosDistCheck` - "Min units
difference between aim position and trace endpos required to adjust firePos to viewPos from
muzzlePos"; `hands_useDeferredViewAimMuzzleTraces` defers "view aim muzzle traces"; on DOOM 2016
`idHands::FireWeapon` normally redirects the muzzle toward the 2D crosshair, and the decl
flag `useMuzzleAsFireAxis` bypasses that. So shots start at the weapon muzzle and head for the point
the view ray hits, falling back to the view origin when the muzzle is blocked.

With view = gun direction but view origin = the eyes, the aim point lies on a ray from the eyes
parallel to the gun, 20-40 cm from the gun's own ray. The shot then converges from the muzzle onto
that point: a small error far away, a visible miss at close range and on weak points. Two fixes:

1. **Data path.** Eternal still has `useMuzzleAsFireAxis`, `useMuzzleDirForFireAxis`,
   `fireFromMuzzle`, `muzzlePos`, `muzzleAxis`, `firePos`, `fireAxis` [C: names]. Setting the flags on
   every player weapon decl (runtime write by typeinfo, or a decl mod) makes the game fire along the
   muzzle, provided the rendered weapon's muzzle follows the tracked controller (the weapon-pose
   work). On DOOM 2016 the equivalent is forcing that branch at fire time.
2. **Code path.** Hook the fire routine and replace `firePos`/`fireAxis` with our tracked muzzle pose
   after the game's own wall-clip test (the BFG fork traces from the owner to the muzzle to stop firing
   through walls). Use this where the decl flag is not honoured (hitscan vs projectile vs BFG
   tracers vs Ballista may differ) [U].

The laser sight and any reticle we draw use the same muzzle pose so what the player sees is what is
fired.

### 6.4 Per-mechanic aim sources

| Mechanic | Aim source | How |
|---|---|---|
| Guns, weapon mods, lock-on (Rocket Lock-on Burst), Ballista, BFG | Weapon controller | Primary design |
| Meathook (SSG mod) | Weapon controller | Target acquisition follows the view; keep `meatHook_chainUsesWeaponTag 1`. `meatHook_playerViewOverrideMode` 0 (dump value) auto-orients the view "until player adjusts view stick"; set 1 ("only oriented initially") and suspend angle writes for the pull, or let the render path drop the rotation |
| Precision Bolt / Sticky Bombs zoom | Weapon controller | Zoom FOV ignored by our projection (`06` 3.1); snapping off (L0) |
| Equipment launcher, Flame Belch | Head (default) | On the press edge of `BUTTON_USE_EQUIPMENT` / Flame Belch, write head angles for the ticks until the projectile spawns (duration to measure), then return to the gun; or hook the projectile spawn [U] |
| Glory kill / melee / Blood Punch target | Weapon controller (default), head optional | `focusTracker` follows the view; `dp_gloryKillMaxAllowableDistanceForRandom` etc. unchanged |
| Use / interact prompts | Weapon controller | Same focus system (`focus_defaultUsableDistance` 3.05 m) |

Aim assist stays off; motion aim needs none (`06` 3.6).

---

## 7. Menu input design

Evidence [C]: menus support the mouse (PCGamingWiki "mouse menu true"); the menu cursor uses its own
fixed sensitivity (`m_menu_sensitivity`, refcheck with a 36000 dpi mouse) rather than the Windows
cursor; `engine_t` has an `idCursor* cursor`; `swf_debugMouseCoords` shows the SWF-space cursor;
codex scrolling works on hover (`dossier_codex_scrollPanel_mouseScrollOnHoverOnly`); the automap has
separate mouse, keyboard and joystick input structs; the weapon wheel has both a mouse mode
(`swf_wheel_mouse_centerRadius` 190, `_deadZone` 50, `_clampRadius` 200) and a stick mode
(`swf_wheel_joyStick_deadZone` 0.5, `weaponWheel_selectStick` 0 = left stick in the dump).

Design:

1. **Menu state** from `03` (paused flag, `idHUD` mode, menu screen hooks) switches the input mode.
2. **Laser pointer to cursor.** Intersect the weapon-hand ray with the UI quad, convert to SWF
   coordinates, and write the `idCursor` position directly (field names from `mh_type idCursor`
   [U]). Fallback: inject `SE_MOUSE` deltas through L3 in a closed loop against the cursor position
   the engine reports. Moving the Windows cursor with `SetCursorPos` will not work if the engine
   integrates raw deltas.
3. **Click, back, scroll.** Trigger = `K_MOUSE1` via L3; B/Menu = `K_ESCAPE` or `K_JOY_B`; stick =
   `K_MWHEELUP/DOWN` or D-pad. Hover-only codex scrolling works because the cursor is real.
4. **Gamepad navigation fallback** (L2 pad or `K_JOY_*` events) for screens where cursor control
   misbehaves, and always for the weapon wheel.
5. **Glyphs.** With `in_MarkJoystickInactiveOnMouseInput 0` the pad stays active, prompts stay Xbox and
   stop flickering. Tutorials and prompts then show Xbox buttons; we show a small legend that maps
   the Xbox names to the player's VR controller (from the OpenXR bindings). Replacing glyph art is a
   later data-mod option. `swf_platformOverride` is worth one test.
6. **Weapon wheel.** Hold `BUTTON_CHANGEWEAPON` past `weaponWheel_HoldTimeForOpeningWheel` (180 ms;
   menu "Weapon Wheel Open Delay" 70-500 ms) to open; select by feeding the configured select stick
   from either the thumbstick or the hand direction (`06` 3.7); release selects. The wheel slows time
   (`weaponWheel_slowTimeScale` 0.14). Direct slot bits (0x800-0x40000) remain available for holsters.
7. **Text entry** is not needed: cheat codes are menu buttons (`idSWFWidget_Button_CheatCode`); the
   console is a developer tool.
8. **Cinematic skip** is a hold (`hud_skipCinematic_holdTimeOverride`); map to a held button.

---

## 8. Haptics

- The game drives rumble through `XInputSetState` (BFG: `win_input.cpp` :961; Eternal proxies forward
  it) [C]. Controls: `in_joystickRumble`, `in_joystickHaptics`, menu vibration scale (default 0.35,
  max 0.467), `rumble_leftRightTriggerScale`, `rumble_screenShakeToRumbleMultiplier` (5.0: screen shake
  adds rumble), `player_focusCausesRumble` (tick when hovering a usable), `com_skipJoystickRumble`,
  and `win_joystickRumbleFrameDelay` 180 ("XBone controllers can freak out if we call SetState too
  quickly after connecting") [C: cvars].
- Plan: L2 captures `XInputSetState`, forwards the original call (a real pad still rumbles), and maps
  the two motors to OpenXR haptics:
  low-frequency motor to both hands, high-frequency motor to the weapon hand. Keep the virtual pad
  connected from startup so the frame delay has elapsed.
- Open questions [U]: whether rumble is sent while the joystick is marked inactive, and whether
  `view_skipShakes 1` also removes shake-driven rumble (the multiplier suggests rumble is derived from
  shake).
- Better routing later: rumble originates in data (`idDeclRumble`, `idDeclPlayerProps::controllerRumble_t`,
  `idDeclWeapon::chargeInfo_t::rumble_t`, `idRumbleComponent`, events `ae_setControllerRumble`,
  `ae_clearControllerRumble`). Hooking the rumble component gives the source (weapon fire, charge,
  damage, glory kill) so it can go to the right hand. Our own fire events from L1 give zero-latency
  recoil pulses independent of the game.

---

## 9. Hold, toggle and remapping options that affect mapping

| Option (menu name where known) | cvar | Notes for VR |
|---|---|---|
| Dash style | `g_settings_dashStyle` 0 hold / 1 on press only | Press only (dump value 1) suits a face button |
| Weapon Wheel Open Delay | `ui_settings_controls_weaponWheel*` (70-500, 180), `weaponWheel_HoldTimeForOpeningWheel`, `weapon_OpenWeaponWheelDelay`, `weapon_weaponQuickSwitch_bufferTime` 20 | Tap = quick switch, hold = wheel; our tap/hold threshold must sit below the game's |
| Wheel select stick | `weaponWheel_selectStick` 0 left / 1 right | Must match the stick we feed |
| Sentinel Hammer activation | `ui_settings_controls_hammerActivateTypeDefault` 0 press / 1 hold; hold 250-750 ms | Keep press |
| Chainsaw / BFG on one button | `weapon_BFG_HoldMS`, `weapon_BFG_DoubleTapMS` (0 = off) | Leave off; we have separate bits |
| Reload toggles fire mode | `weapon_reloadTogglesFireMode` | Off |
| Hold to use | `p_holdToUseMS` 0 | Instant |
| Aim Assist, Target Snapping, Look Smoothing, sensitivities | section 3 | Forced by L0 |
| Controller layout presets / remap | bind table | Irrelevant to L1; matters for L2 fallback, which must write known binds or use raw XInput channels |
| Weapon mod: hold or toggle | none found in the cvar dump | Check the options menu [U]; 06 assumes hold on grip |
| Crouch / sprint toggles | `pm_crouchToggle`, `pm_togglesprint` | Not used in the campaign |

---

## 10. Implications for our design

1. Build input on the engine-access module from `03`: L1 depends on `engine_t` field lookup, RTTI and
   a validated vtable slot. Until L1 works, L2 carries the game (at the quality of a pad-based mod).
2. Use the semantic button mask, not key binds. It removes the bind-channel surprises seen on DOOM 2016
   (ignored JOY binds, Xbox-Y as BFG) and makes remapping purely ours.
3. The player view is the aim. The render path must never read the game's view angles for the
   camera; it builds eyes from head pose plus our body yaw and the game's view origin.
4. Keep one "aim source" function per action (gun, head, off hand) so equipment and options like
   "meathook from head" are table entries, not special cases.
5. Settings enforcement (L0) is a feature with its own self-test: read back every forced cvar after
   options menus and map loads, log drift.
6. Coexist with Meathook and Advanced Options: do not ship an `XINPUT1_3.dll` proxy; patch the IAT
   from our own module, which works whichever DLL satisfied the import.
7. Refuse to run input hooks in BATTLEMODE and in any online session.
8. Put the latching snapshot (section 5) in a small, unit-tested module: it is shared by L1, L2 and L3
   and is where press-loss and double-press bugs would live.

---

## 11. RE tasks for the gaming rig

1. **Imports** (10 min): dump the import table of `DOOMEternalx64vk.exe` and `DOOMSandBox64vk.exe`;
   confirm `XINPUT1_3.dll` with only GetState/SetState/GetCapabilities by name; check for
   `DINPUT8.dll`, `user32!RegisterRawInputDevices`, SDL, `steam_api64!SteamInput`.
2. **Enums and types** via Meathook: `mh_type` on `idUsercmdGenLocal`, `idUCmdTracker`, `idCursor`,
   `idInput`, `idJoystick`, `idVRInput`, `idPlayerAimAssist`, `idDeclAimAssist`, `idRumbleComponent`;
   `FindEnumInfo` on the usercmd button enum (name to be found with `mh_kw BUTTON_DASH`), `keyNum_t`,
   `inputEventType_t`, `idUCmdTracker::inhibitFlags_t`. Save under
   `reference/idtech7/typeinfo/<build>/`.
3. **usercmd layout and build step** (x64dbg + Ghidra): from `engine_t::usercmdGen` get the object and
   vtable; break on the generator slots during gameplay; find where buttons, analog move and angles
   are written; record `usercmd_t` offsets (look for the 64-bit mask with known bits, e.g. press dash
   and watch 0x400000). Confirm `idPlayer::ProcessInput` consumes it and how `cmdAngles` and
   `deltaViewAngles` combine.
4. **Angle injection test**: add +2 deg yaw via the generator for one tick; read
   `idPlayer::viewAngles`; repeat across a glory kill, a meathook pull and a teleport to see how
   `deltaViewAngles` behaves.
5. **Focus**: with the window unfocused, test XInput, usercmd-level writes and events against
   `in_controlInactiveWindow` / `in_requireGameWindowActive`.
6. **Aim assist**: `aimAssist_Debug 1`, `aimAssist_DebugAdhession 1`, `aimAssist_DebugFriction 1`; turn with
   a stick near enemies, then with our usercmd angles and an idle stick; confirm L0 values zero it.
7. **Fire path**: `hands_drawMuzzlePos 1`; find `FireWeapon` (string xrefs, `useMuzzleAsFireAxis` field
   readers); log `firePos`/`fireAxis` for a hitscan gun, a projectile gun, Ballista and BFG; flip
   `useMuzzleAsFireAxis` on the Combat Shotgun decl and compare.
8. **Meathook and lock-on**: log `lockOnTargetEnt`, `meatHookTargetIsViewPosition` and the meathook
   target while pointing the view off-target; test `meatHook_playerViewOverrideMode 1`.
9. **Equipment timing**: ticks between `BUTTON_USE_EQUIPMENT` edge and grenade spawn, to size the
   head-aim window.
10. **Cursor**: `swf_debugMouseCoords 1`; write `idCursor` fields and confirm hover/click in the main
    menu, Dossier, codex, automap and weapon wheel; test `in_MarkJoystickInactiveOnMouseInput 0` for
    glyph stability.
11. **Rumble**: log `XInputSetState` calls for fire, charge, damage, glory kill, hover; with pad active
    and inactive; with `view_skipShakes 1`; find the rumble component's call into the device layer.
12. **Dormant VR input**: with `vr_dummyDevice 1`, check whether `SE_VR` or `K_STEAMVR_*` events are
    consumed anywhere (bind `K_STEAMVR_PRIMARY_TRIGGER` to `_attack` and inject it through L3).
13. **Options menu names**: record the exact Controls menu entries (hold/toggle for weapon mod, dash
    style label, wheel delay, aim assist, target snapping) and the bindings file path, to close the
    [U] rows in section 9 and in `docs/notes/eternal-pc-keybinds.md`.

---

## Sources

- PCGamingWiki, DOOM Eternal (rev 1805165, 2026-09-23): https://www.pcgamingwiki.com/wiki/Doom_Eternal ;
  local: `reference/input/pcgamingwiki-doom-eternal-input.md`
- GyroWiki, DOOM Eternal (raw input, JoyShockMapper calibration): http://gyrowiki.jibbsmart.com/game:doom-eternal
- Steam guide "Playing Doom Eternal with a Controller" (controller option names, 2021):
  https://steamcommunity.com/sharedfiles/filedetails/?id=2464347025
- Steam discussion on default aim assist (2021): https://steamcommunity.com/app/782330/discussions/0/3118172724627697276/
- KEX full cvar list (MIT): https://github.com/Official-KEX/doom-eternal-full-cvarlist ; local:
  `reference/idtech7/typeinfo/kex-cvarlist-2024.tsv`, `reference/input/eternal-input-cvars.md`
- Meathook (input event hook, XInput proxy, `engine_t`, enums, event ids): https://github.com/brongo/m3337ho0o0ok ;
  local `reference/_cache/meathook/m34thook/{mh_inputsys.cpp,main.cpp,gameapi.hpp}`
- DE Advanced Options Mod (usercmd button mask, isKeyPressed, SendButtonPress, ProcessInput, controller
  layouts; BSD-2): https://github.com/SteamKaibz/DE_AdvancedOptionsModPublic ; local
  `reference/_cache/de_advancedoptions`, `reference/input/eternal-usercmd-and-input-events.md`
- DOOM Eternal Archipelago client (XINPUT1_3 check, 2026): https://github.com/snowzzrra/DoomEternal-AP-Mod
- DOOM 3 BFG source: https://github.com/id-Software/DOOM-3-BFG ; BFG VR fork:
  https://github.com/CarlKenner/DOOM-3-BFG-VR ; local `reference/input/doom3bfg-usercmd-reference.md`
- Meathook / typeinfo name dumps: `reference/idtech7/typeinfo/meathook-{type,property,cvar}-names-6.66.txt`
- Project docs: `docs/research/03-doom-eternal-internals.md`,
  `06-vr-gameplay-comfort.md`, `docs/notes/eternal-pc-keybinds.md`
