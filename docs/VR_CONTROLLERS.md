# Motion controllers (M5)

Builds on `docs/VR_HEAD_TRACKED.md`. The controllers drive the game: buttons, movement and turning go
into the game's own user command, the view can follow the weapon hand instead of the head, shots leave
the gun, and the game's arms and weapon are drawn at the controller. The hook points are
`docs/rig-findings/input-aim.md` (build 25216728); the portable pieces are in `src/features/input/`,
`src/game/eternal/` and `src/xr_math/`.

**Status.** Built, unit-tested and checked live on the rig with OpenXR-Simulator and scripted input (the
live checks below); on by default (`ETERNALVR_CONTROLLERS=0` turns it off). Still for the headset: glory
kills and the meathook under hand aim, the viewmodel offsets per weapon, the aim error over a real play
session, and the real controllers' bindings.

## How it works

```
XR worker (every XR frame)          camera hook (every game frame)         game threads
xrSyncActions -> snapshot  ----+    head located -> controller poses       user command build:
  (buttons, sticks, aim poses,  |    at the same time                        mapper -> buttons ORed,
   head; scripted test input    |    hand aim: view angles -> hand ray       move added (0x43E8DD)
   laid over it)                |    world poses of the weapon hand          turn -> accumulated
                                |    relative to the game's eye              angles (0x17FD3C0)
                                +--> weapon FOV = headset FOV              FireWeapon: shot start and
                                                                             axis = hand ray (0x135D733)
                                                                           UpdatePosition: viewmodel at
                                                                             the grip (0x13807EA)
```

- **OpenXR input** (`src/vkcore/input_xr.cpp`, the families in `input_profiles.cpp`). When the session
  exists, the `gameplay` action set of `features/input/xr_action_set.hpp` is created with both hands as subaction paths, the
  suggested bindings of every controller data file in `data/input/controllers/` (one per family, below;
  a player's file, or each `*.toml` of a player's folder, through `ETERNALVR_CONTROLLER_DATA` replaces
  the one with the same profile) are
  suggested, the set is attached and aim and grip spaces are created per hand. A family whose profile
  comes with an extension is suggested only when the instance has it (below), and a profile the runtime
  refuses is logged without stopping the others. Every XR frame the worker syncs `gameplay` and publishes
  a snapshot: triggers, grips, sticks, buttons, the aim poses with velocity and the head, all in `LOCAL`.
  The runtime's current interaction profiles pick the family whose control map is used
  (`features/input/controller_family.hpp`): both hands the same family, or only one hand with one (the other
  controller off or asleep), that family; two different families keep the one in use if it is one of
  them, else the right hand's; neither keeps the one in use. The profile of each hand is logged when it
  changes (`controllers: the runtime reports ... for the left hand`), and a new family as `controllers: the
  runtime reports <family> controllers`. Handles are destroyed before the presenter destroys its spaces.
- **Mapper** (`src/vkcore/usercmd_hook.cpp`, `runMapper`). The control map of the family and handedness is
  compiled (`buildBindingProfile`; a map with conflicts is refused with its two-sided messages) and
  `InputMapper` runs once per user command. Its `GameInput` goes through `ActionHold` (a tap is held at
  least 50 ms and two commands, so the game samples it). Every action that starts is logged by name
  (`controllers: action quick_switch`), which the weapon-swap protocol needs: the first 500, then one a minute
  with the count left out (`ETERNALVR_CONTROLLERS_TRACE=1` logs them all).
- **User command** (L1). A mid-hook on the call of `idUserCmdMgr::PutUserCmd` (RVA 0x43E8DD; r8 the finished
  command, ebx the local user, r14b set while the game suppresses buttons). For local user 0 the actions'
  bits (`game/eternal/usercmd_buttons.hpp`) are ORed into `buttons` (+0x10) and the move is added to
  `forwardmove` / `rightmove` (+0x18 / +0x19) with a clamp to the keys' +-127. The keyboard, mouse and a
  pad keep working. Nothing is ORed while the game suppresses buttons. Pause is the Escape key (the game's
  `toggleMainMenu` is a command, not a button), sent through key injection, also while buttons are
  suppressed so the menu can be closed.
- **Turning.** A mid-hook at the generator's angle conversion (RVA 0x17FD3C0): for local user 0's build
  (`[rdi+0x8D0]`; the generator builds every local user's command) the turn (smooth, or snap in whole
  steps) is added to the accumulated yaw at `[[rdi+0x8D8]+8]`, where mouse motion goes, so it
  persists like mouse motion; the accumulated yaw is kept within +-3600 degrees.
- **Weapon wheel** (`features/input/wheel_mouse.hpp`, `docs/rig-findings/menus.md` section 4). The game's
  wheel selects with its menu cursor (the cursor's offset from the wheel's centre, clamped to 200 GUI
  pixels, a segment past 50), and the cursor moves with relative mouse motion even while hidden. So while
  `weapon_wheel` is held, from 0.25 s after it went down (the game opens the wheel 180 ms after the button
  by default; mouse motion before that would still turn the view), the pointer stick's direction becomes
  raw mouse motion through the key injection: the difference from a model of the cursor's offset to the rim
  point in that direction when the direction turns by more than 3 degrees, and a 40-pixel push outward every
  0.1 s while it holds (the game's clamp absorbs it and it corrects any drift from the model). Below half
  deflection nothing is sent, so the stick springing back leaves the highlight, and the release of
  `_changeWeapon` (the sweep ends at the centre) picks it. With the virtual gamepad the pointer goes on its
  right stick instead. Log: `the weapon wheel is up`, `weapon wheel: pointing <direction> (motion dx, dy)`
  at each change of the eight directions, `weapon wheel released after N motion(s)`. A player's map may put
  `weapon_wheel` on a button instead of (or as well as) the down hold, and the Steam Frame's built-in map
  holds it on the right bumper: while a button holds it, the turn stick is the pointer
  (`TurnStickArbiter::update` with `wheelHeld`), the sweep in progress is cancelled, and a stick still out
  of the centre when the button is let go stays cancelled until it comes back, so pointing never turns,
  presses the chainsaw or ends in a quick switch.
- **Weapon wheel by the hand** (`ETERNALVR_WHEEL_SELECT=hand`, `features/input/wheel_hand.hpp`; the
  launcher's Play tab, Controls, "Weapon wheel"). The stick (or a button bound to `weapon_wheel`) only holds
  the wheel; the weapon hand's aim ray points. At the first command of a hold with the hand tracked the
  mapper keeps the aim orientation from the controller snapshot (the same snapshot the buttons come from, so
  no other lock or thread); after that the pointer is the angle between the start and current pointing
  directions over `ETERNALVR_WHEEL_HAND_DEGREES` (20 by default, capped at 1), in the direction of the turn,
  with right level with the room and up tilted with the start's pitch (straight up or down at the start,
  the controller's own right stands in). Only the pointing direction counts, so a roll at the start or
  during the hold never moves the pointer, and the frame follows the start so it works in any facing. The
  pointer replaces the stick's in `GameInput::wheelPointer` and goes the same way (cursor motion, or the
  virtual gamepad's right stick), so the wheel's half-deflection threshold is a 10-degree turn: below it
  nothing is sent and the highlight stays. A hand that loses tracking gives no pointer and keeps its
  reference. Rotation rather than translation: a turn of the wrist is small, quick and the same seated or
  standing, as the menu laser points; moving the hand sideways needs a large arm sweep, has no natural
  centre and drifts with the body. The weapon hand is the handedness's (left in both left modes). Log: the
  `controllers: on:` line ends `weapon wheel by the hand` (or `stick`), and `the weapon wheel is up: the
  weapon hand moves the game's wheel cursor (200 px to the rim at a 20 deg turn)`.
- **The thumb-rest wheel** (`ETERNALVR_THUMBREST_WHEEL`, `features/input/rest_wheel.hpp`, T-118; the
  launcher's Play tab, Controls, "Thumb-rest wheel"). A thumb resting on its controller's thumb rest turns the
  other hand's stick into a weapon picker, beside the turn stick's down hold and the slot bindings, which
  stay. The rest is the `thumbrest` action (`/input/thumbrest/touch`, Touch controllers only; Touch Pro and
  Plus report as Touch), debounced 0.06 s each way; with `ETERNALVR_THUMBREST_FACE_TOUCH=1` the A/B or X/Y
  touch (`primary_touch`, `secondary_touch`, while touched and not pressed) stands in on controllers without
  one (Index, Pico 4; not the Steam Frame, whose right bumper already holds the wheel). A player's controls
  file replaces the built-in file whole, so the layer adds these bindings to a family's data that lacks them
  (`rest_touch_bindings.hpp`; logged as `N touch binding(s) for the thumb-rest wheel added`, nothing
  rewritten), and a runtime that refuses the profile with them is asked again without them (the family then
  has no rest for the session). Modes (off by default since 2026-10-09, after the first headset test; the launcher offers edge and full,
  `extreme` is layer-only until it is redesigned): `edge`: a landing arms the other stick for 0.5 s
  (`ETERNALVR_THUMBREST_WINDOW`, 0.2 to 1); it counts only with both sticks within 0.25 at that moment, so a
  thumb that lands while the other stick moves or turns does nothing, and pushing the other stick past 0.25 in
  the window starts picking; a thumb left resting never stops that stick. A thumb that comes straight from its
  own controls opens no window either: a landing within 0.4 s of that hand's primary or secondary button going
  up (`kOwnButtonVoidSeconds`: jump or dash, then the thumb on the rest) or of its own stick being out of the
  centre (`kOwnStickVoidSeconds`: turn, rest, walk; walk, rest, turn). After a pick (never after a cancel, so
  snap flicks with a resting thumb are not taken one after another) the window opens again once the stick has
  been back in the centre for 0.05 s. `full`: while the thumb rests the other stick is the wheel's (it neither
  moves nor turns; a stick out when the thumb lands waits for the centre). `extreme`: the turn stick is always
  the wheel's; the rest on the other hand (the move stick's) gives it back its turning, chainsaw and quick
  switch, and ends a pick in progress. `off`. Picking: nothing is pressed until the stick has stayed 0.1 s in
  one of the eight directions at 0.5 or more (once the stick stops going further out, a direction is kept until
  the stick is 7.5 degrees past the edge of its eighth; while it still goes further out, by more than 0.02, it
  points at the eighth it is in, so a stick that curves on its way out takes the eighth it reaches,
  `kOutwardStep`), so a flick presses nothing; with `slots` a stick past 0.85 (`kFlickThreshold`) counts at
  once, at the eighth of its furthest point, so a flick picks. Then with `ETERNALVR_THUMBREST_PICK=wheel` (default)
  `weapon_wheel` goes down and the stick points as under the stick's own hold (`wheelPointer`; below 0.5 the
  last direction, so the highlight stays); the stick back in the centre, the thumb lifted (or in `extreme` the
  other thumb resting) closes it, held at least the game's open delay plus 0.12 s from the press, never under
  0.3 s (`wheelHoldSeconds`; the delay is `weaponWheel_HoldTimeForOpeningWheel`, the game's Weapon Wheel Open
  Delay, 180 ms by default, read every mapper run; 0.3 s when the cvar is not found), so a quick pick is never
  a quick switch. There is no cancel once it is open. With `slots` the wheel stays closed and the direction
  held last is remembered; letting go presses its `weapon_slot_N` once (held by `ActionHold`), from
  `ETERNALVR_WEAPON_DIRECTIONS` (`up=1,up_right=5,...`, `weapon_directions.hpp`; the default is the game's own
  wheel, from a rig screenshot: each direction picks the weapon the wheel shows there). The mapper runs the
  wheel before the turn stick's arbiter and the turn: the stick it takes goes to the arbiter as a held wheel
  from the frame it leaves 0.25, so a snap (0.70) or a smooth turn (0.35) never fires first; the move stick is
  zeroed. After a pick or a cancel that stick stays out of play until it is back within 0.25. Blocked (no
  arming, picking cancelled, an open wheel closed): a menu's hold, the game suppressing buttons (r14b, kept
  from the user command before the mapper runs), a forced view (`forcedView()`), a skippable cutscene, a
  piloted demon, and another route holding the wheel (a button bound to `weapon_wheel`, the turn stick's down
  sweep). With both thumbs resting the first to start picking wins. Under `ETERNALVR_WHEEL_SELECT=hand` this
  wheel still points with its stick. Vibration (`HapticSource::Wheel`): a tick on the picking stick's hand
  when picking starts (0.25, 0.02 s) and a firmer one on the pick (0.5, 0.035 s), times the strength. The
  slowdown (`features/input/wheel_slowdown.hpp`, `src/vkcore/rest_wheel.cpp`): the game slows time while its
  wheel is open (`weaponWheel_slowTimeScale`, 0.14); with `ETERNALVR_THUMBREST_SLOWDOWN=0` the layer holds
  that cvar at 1 from this wheel's press until 0.3 s after it lets go, through the cvar book, and writes the
  game's own value (read just before each hold) back; at once when the stick's wheel or a button takes over
  (they always slow time), when the controllers go stale, on a multiplayer guard trip (the book's restore) and
  when the layer shuts down. With the slowdown on (default) the cvar is never looked up. Log: the
  `controllers: on:` line ends `thumb-rest wheel edge (picks with wheel, face touch off, slowdown on)`; the
  control map line is followed by `thumb-rest wheel edge: rest sensors on both hands, picks with the game's
  wheel, window 0.50 s` (or `off for these controllers (no thumb-rest sensor)`); `thumb-rest wheel: the left
  controller reports a resting thumb` at each hand's first touch, and one line when none has after a minute of
  play; `the game opens its wheel 180 ms after the press (weaponWheel_HoldTimeForOpeningWheel): held at least
  0.30 s` when the delay is read or changes; then per use `armed: the right stick picks (left thumb rest,
  edge, 0.21 s after the touch)` (under `extreme`: `armed: the turn stick picks weapons; the left thumb rest
  gives turning back`), `the game's wheel is held, pointing up`, `the game's wheel let go pointing right`,
  `weapon_slot_3 picked by the right stick pointing right (out to 0.93)`, `cancelled, nothing pressed (...; out
  to 0.41)`, and `the left thumb landed with a stick out of the centre; no window` (or `straight from its own
  stick`, `straight from its own face button`; capped like the action lines). Every 10 s in which a rest's
  sensor reported a touch, one line for that rest: `left rest, last 10 s: 14 touch(es) from the sensor, 2 gone
  within 0.06 s (not registered), 1 gap(s) under 0.06 s bridged; 11 landing(s), 3 back within 0.25 s of letting
  go, 4 with the right stick already out (3 of it out 0.20 s or less)`: touches too short to count, a sensor
  that flickers, and flicks begun before the rest registered (under `full` such a stick waits for the centre)
  show there. Scripted input counts as a rest on both hands.
- **Arm gestures** (`ETERNALVR_THROW`, `ETERNALVR_SWING`, `features/input/arm_gestures.hpp`; the launcher's
  Play tab, Gestures; both off by default; design and ranking in `docs/VR_INTERACTIONS.md`). The throw: the
  off hand wound up beside the head (at most 0.15 m below the eyes, no more than 0.10 m ahead of them along
  the head's heading) primes it for 0.8 s, and a forward speed of 2 m/s (`ETERNALVR_THROW_SPEED`) within
  that time presses `equipment` once. The overhead swing: the weapon hand at least 0.10 m above the eyes,
  the other hand not, primes it; a downward speed of 2.5 m/s (`ETERNALVR_SWING_SPEED`) presses `crucible`
  once. Both hands raised together block the swing until the weapon hand has come down. One gesture per
  pose, 0.5 s apart. A primed hand's punch is held back and it has to slow down before it can punch, so the
  gesture's own motion never also punches. The press is one mapper frame, held by `ActionHold` like a tap.
  Log: the `controllers: on:` line ends `throw gesture on|off, overhead swing on|off`, and each gesture
  logs `controllers: gesture: throw` (or `overhead swing`) followed by `controllers: action equipment`
  (or `action crucible`).
- **Locomotion.** The move stick is relative to where the head looks, the left hand points or the right
  hand points (`ETERNALVR_LOCOMOTION=look|left|right`, `features/input/locomotion_direction.hpp`) and is
  rotated into the game's view yaw, which the camera hook measures each frame as the player's view yaw
  less the body yaw, so hand aim never bends the direction of travel. The hand is the one named, whatever
  the handedness (Jason's headset notes, 2026-10-02: three plain choices instead of "the move hand"). A
  hand that is untracked or points near vertically falls back to the head. The values from before are
  still read: `head` is `look`, and `hand` is the map's move stick hand (`locomotionHand`,
  `features/input/binding_profile.hpp`: the left hand under `right` and `left`, buttons swapped, the
  right hand under `left_mirror`; a map with no move stick takes the hand not holding the weapon). Log:
  `controllers: on: ... locomotion left ...` and `controllers: control map for <family> controllers,
  left-handed, move stick left, moving where the left hand points` (or `moving where the head faces`).
- **Hand aim** (`ETERNALVR_AIM=hand`, `src/vkcore/aim_hooks.cpp`). Head aim's closed loop is reused: each
  game frame the game's own angles (command + deltaViewAngles) are read back and moved to body + target,
  where the target is the weapon hand's aim ray (`handAimAngles`) instead of the head. The render camera
  stays `body * head`. A hand that loses tracking keeps its last angles for 0.5 s, then the head aims.
  While the player swims the head aims too: the game swims and dashes along its view, so with the weapon hand
  aiming a level hand kept a dive level (Discord, 2026-10-04). The swim fists in the hands
  (`weapon/player/fists_swim`) mark swimming; the viewmodel hook sees them (`viewmodel_hook.cpp`), so with
  `ETERNALVR_VIEWMODEL=0` the weapon hand still aims in the water. Log: `controllers: swimming: the view
  follows the head` and `controllers: out of the water: the weapon hand aims again`.
- **Melee and equipment aim** (`ETERNALVR_MELEE_AIM`, `ETERNALVR_EQUIPMENT_AIM`,
  `features/input/action_aim.hpp`, `src/vkcore/action_aim_hook.cpp`, `src/vkcore/equipment_launch_hook.cpp`;
  the launcher's Play tab, Controls, "Melee aim with" and "Equipment aim with", GitHub issue 11). Under hand
  aim, melee (one button in the game: melee, Blood Punch, glory kills and use; a punch too) and the
  shoulder-mounted equipment launcher and Flame Belch (the throw gesture too) can aim with the head or the
  off hand instead of the weapon hand.
  - Melee: the game takes its target from its view angles when the press reaches it, so the mapper holds the
    press back (before `ActionHold`) until the camera hook has written the new target into the game's angles
    at least once (a generation number goes from the mapper to the camera hook and back), at most 0.1 s; the
    first command built after that write carries the press. On the rig that is one command, 6 to 9 ms. The
    target stays while the button is held, while the game forces the view (a lunge or a glory kill) and
    0.5 s after both, then the weapon hand aims again at once: the picture is `body * head` whatever the
    target, so neither change moves it, only the game's own angles turn. A press goes out at once where the
    camera hook writes nothing (a menu or popup is up, a scripted camera), and equipment and Belch presses
    are never held back (neither launch reads the view).
  - Equipment: the launcher and the Belch do not aim with the view. Static RE (build 25216728): a frag
    grenade or ice bomb is queued by `UseEquipmentItem` (0x1466CE0) and launched about 0.15 s later (rig) by
    the launch animation's event through `idHandsItem::LaunchEquipmentLauncher` (0x1389470), which takes its
    origin and axis from the shoulder launcher's muzzle joint (0x1389020 -> 0xFDBD10, turned by the decl's
    `launchDirOffsetDegs*`) and passes them to the launch core (0x1389910); the Belch fires through
    `FireWeapon` with the same joint's muzzle as fire position and axis. With the arms model at the weapon
    hand both follow the weapon hand whatever the view does. So the view stays on the weapon hand, and a
    mid hook before that call (RVA 0x13898A1: rsi the hands item, slot 5 or 6, r14 the player, the origin
    at `[rbp-0x80]`, the axis at `[rbp+0x98]`; installed only with `ETERNALVR_EQUIPMENT_AIM` set) moves a
    grenade's start to the head's (or the off hand's) ray start and turns its axis onto that ray, keeping the
    turn and arc the game's axis has from the weapon hand's ray (`xr_math::carryAimOffset`). A Belch shot is
    turned the same way in the fire hook (below), and so is its flames' axis (`idHands` +0x8E3C). A shot is
    the Belch's when that axis is not identity: `GetWeaponFireInfo` resets it for every shot and sets it for
    the Belch alone, so a gun fired while the Belch's button is held keeps the weapon hand.
  - Log: `controllers: on: ... melee aim <source>, equipment aim <source>`, `game hooks: ... equipment launch
    on`, `action aim: the view follows the off hand for melee (generation N)`, `action aim: melee press held
    back C command(s), T ms, until the view took its target`, for the first two game frames after the press
    `action aim: melee press, game frame 1 after it went out: game view P Y (pitch, yaw from the body); head
    ..., weapon hand ..., off hand ...`, `equipment launch (slot 5): origin (...) dir (...) -> (...) dir (...)
    along the head; weapon hand (...)` (the first 12), and a shot while a melee or equipment button is held
    with its decl (`action aim: a Flame Belch shot while flame_belch held, decl '...'` for a Belch shot, the first 12). Nothing changes
    under head or view aim, or with both left on the weapon hand (the default).
- **Aim smoothing** (`ETERNALVR_AIM_SMOOTHING`, `features/input/aim_smoothing.hpp`,
  `src/vkcore/game_view_poses.cpp`). Where the hands are located for a game view, the weapon hand's aim
  orientation goes through a one-euro filter (Casiez et al. 2012) in LOCAL: a low-pass whose cutoff rises
  with the hand's angular speed, so tremor and tracking noise are cut while the hand is still and a moving
  hand is followed closely. The viewmodel, the shots, the aim angles and the reticle all read the filtered
  ray. The strength runs from 0 (off) to 1 (strongest): the still cutoff falls from 8 Hz toward 0.5 Hz and
  the speed gain from 20 toward 10 Hz per radian per second. The default 0.3 (3.5 Hz, 16 Hz per rad/s) cuts
  a still hand's noise to about a third and lags 8 ms at 57 degrees per second, 3 ms in a quick flick
  (`docs/rig-findings/aim-jitter.md`). The log's `controllers: on:` line shows the setting.
- **Reticle.** Under hand aim the dot is placed from the shown frame's own weapon ray (smoothed, at that
  frame's pose time), so it stays where the gun drawn in that frame points and its shots go
  (`src/vkcore/presenter_reticle.cpp`).
- **Forced views.** An entry hook on `idPlayer::SetViewAngles` (RVA 0x1454480) counts calls whose return
  address is not the per-tick view update (0x14562A8, found as the only call to SetViewAngles inside
  `UpdateViewAngles`) and whose player (rcx) is the one the camera hook sees; each distinct caller is
  logged once. With the player's view inhibit bits
  (`inhibitFlags & 0x108`, +0x87FC), `renderView_t.inCutscene` and, when its rotation is played
  (`ETERNALVR_CAMERA_ANIMATIONS=1`), a hands animation that moves the camera
  (docs/rig-findings/camera-animations.md) they feed `ForcedAngleGate`, which
  yields that frame and two more. While it yields, hand aim writes nothing (the camera keeps the body yaw
  without the injected hand yaw, so it does not jump), shots keep the game's start and axis, and the
  viewmodel keeps the game's own placement (glory kills and sync animations use it). A climbable wall
  (below) is the one exception: the gate yields there too, but hand aim aims with the head instead of
  writing nothing.
- **Shots** (T-055). A mid-hook in `idHands::FireWeapon` just after `GetWeaponFireInfo` returns (RVA
  0x135D733; the site is checked to call GetWeaponFireInfo with the fire axis at rbp+0x2B0 and the fire
  position from `lea r9,[rsp+0x68]`). For the local player's hands (`idHands.owner`, +0x358, has
  idPlayer's vtable), the fire axis becomes the hand ray. A shot the game starts at the eye (hitscan) is
  moved to the player's first-person origin (+0x16580) plus the hand's offset from the eye (limited to
  1 m); one it already starts at the muzzle (the Heavy Cannon, projectiles) keeps that start, because the
  muzzle tag follows the viewmodel placed at the hand. One hook covers hitscan and
  projectiles; spread, the muzzle offset for tracers and the multiplayer copy are applied by the game
  afterwards. Each shot's angle between the game's own fire axis and the hand ray is counted (over 0.5
  degrees, and the maximum, logged every 10 s): the M5 aim-error criterion. `ETERNALVR_SHOT_ORIGIN=eye`
  keeps the game's start and changes only the direction.
- **Viewmodel** (T-054, `src/vkcore/viewmodel_hook.cpp`). A mid-hook in `idHands::UpdatePosition` just
  before the render model's stores (RVA 0x13807EA; the eight structure displacements of those stores are
  checked). The final origin (`[rsp+0x48]`) becomes the grip position and the axis (`[rbp-0x30]`) the aim
  ray's, both through the held weapon's offset; the game's own stores then carry them into the deferred
  and current render-model pose. The held item is `idHands.rightItem.itemDecl` (+0x29B0 + 8) and its name
  (`idResource::name`, +8) keys `data/weapons/viewmodel_offsets.toml`. The hand is kept relative to the
  game's eye and re-added to the eye the game has when it places the weapon or fires, so the gun stays on
  the hand while the player moves.
- **Weapon FOV.** After the camera hook writes the headset FOV into `fov_x` / `fov_y`, the same values go
  into `weaponFOVX/Y` (+0x30 / +0x34) and `customFOV2X/Y` (+0x38 / +0x3C), so the arms and weapon use the
  world's projection. The launcher forces `hands_fovScale 1` (T-053) and
  `meatHook_playerViewOverrideMode 1` (`launcher/data/forced-cvars.txt`).
- **Virtual gamepad** (L2, `src/vkcore/xinput_hook.cpp`). The exe imports `XInputGetState` from
  `XINPUT1_3.dll` by ordinal 2 only; that import slot is replaced. For pad 0 the real state is merged with
  ours (`features/input/virtual_gamepad.hpp`: buttons ORed, triggers the larger, sticks added), the packet
  number (our own counter) moves on when the merged state or the real pad's packet changes, and pad 0
  reads as connected while the controllers are attached, even while their input is stale, so the game
  never sees the pad come and go. Without the turn hook the turn stick goes to the right stick, so the
  game's own stick look turns (smooth only). It maps onto the game's default pad binds, so direct weapon slots and next/previous weapon are not
  available through it. It is used only when the user-command hook cannot be installed, or with
  `ETERNALVR_XINPUT=1`; the turn hook works with either path.
- **Vibration** (`ETERNALVR_HAPTICS`, `features/input/haptics_policy.hpp`, `src/vkcore/haptics_xr.cpp`).
  Short pulses on the gameplay set's `haptic` action (`xrApplyHapticFeedback`, frequency unspecified),
  sent by the XR worker after each sync and only while the session is focused: the weapon hand when fire
  goes down (0.6 x strength, 40 ms) and every 0.15 s while it is held (0.35, 30 ms); the hand that punched
  (1.0, 80 ms); the pointing hand when its ray comes onto a menu panel (0.2, 15 ms) and on a click (0.35,
  20 ms; ticks at least 60 ms apart); both hands for the capture chord (0.8, 120 ms). The game's own rumble
  comes from a detour on `idRumbleComponent::GetMagnitudes` (RVA 0xAAE730, `src/vkcore/rumble_hook.cpp`,
  `docs/rig-findings/haptics.md`), which the game runs every frame for the local player whatever the input
  device: its low-frequency motor goes to both hands and its high-frequency one to the weapon hand, as
  0.1 s pulses renewed while the level lasts and stopped when it ends; none while a menu holds gameplay
  back. Each hand gets the strongest pulse of a frame, and a weaker one never cuts a stronger one from
  another source short. The strength (0 off, default 0.6) scales every pulse. Log: `haptics: on, strength
  S`, `haptics: rumble hook at RVA 0x...`, `haptics: the game's first rumble: low L, high H`, and every
  10 s with something new `haptics: N pulses (fire a, punch b, menu c, game d, capture e), r refused`.
- **bHaptics** (`ETERNALVR_BHAPTICS`, `ETERNALVR_BHAPTICS_INTENSITY`; [BHAPTICS.md](BHAPTICS.md)). Off by
  default. Vests and arm sleeves through the bHaptics Player's local WebSocket: shots, damage with its
  direction, a low-health heartbeat, glory kills and death, from the fire hook and the player's health
  component. Experimental, untested on hardware.
- **Comfort vignette** (`ETERNALVR_VIGNETTE`, `src/features/comfort/vignette.hpp`,
  `src/vkcore/presenter_vignette.cpp`). While the stick turns or moves the player, or the game moves the
  camera itself (the dash action held, or a forced view: a glory kill, the Meathook pull, a scripted
  camera), the edges of the view darken and leave a clear centre; head motion never counts. Each mapper run
  publishes the turn rate (the turn's degrees over the frame) and the move stick's magnitude, both zero
  while a menu holds the controllers back. The XR worker turns them into an amount (full from 120 degrees
  per second or two thirds of the stick; in within 0.2 s, out within 0.5 s) and shows the nearest of 8
  images made at start (256 x 256, premultiplied black) on a head-locked quad 5 m ahead that spans 75
  degrees each way. It sits right over the game's view, under the HUD, the aim dot and the collision fade,
  so the HUD stays readable. Never with a menu up or on the flat screen (cutscenes, the multiplayer guard).
  `light` closes to 40 degrees with 80 % dark edges, `strong` to 25 degrees with black edges. Log:
  `vignette: <look>, 8 level(s) of 256x256 ready ...` once, then every 10 s `vignette: <look>, shown N of
  M frame(s), max level L of 8; turn up to D deg/s, move up to X, game camera G frame(s), H frame(s) held
  back (menu)`.
- **Multiplayer guard.** Every hook is installed only while the guard is armed (`installGameHooks` is
  called next to the camera hook's install), and every callback that writes asks
  `mp_guard::allowsGameTouch()` first and again just before it writes (`docs/rig-findings/mp-guard.md`).
  Anything that cannot be located or validated stays off and the log says which.

- **Look-at triggers** (`src/vkcore/facing_hook.cpp`). A few places open only when the player looks at
  something (idTrigger_Facing: the ladder panel, the tram exit and a door in Doom Hunter Base, and others).
  The game tests the player's first-person view axis, which under hand aim is the gun's direction, so the
  trigger waited for the gun. A mid hook in the test (RVA 0xD9D17F in build 25216728; the signature is
  unique in the Game Pass build too) puts the head's horizontal forward, from the view the camera hook
  wrote, in the test's copy of the look direction. The test is horizontal only. Rig check (e1m4, the ladder
  trigger, the weapon hand 70 degrees off the head): with the head on the target and the gun off it the
  trigger fired on its first test; without the hook it never did, and with the gun on it and the head off
  it the hook kept it from firing. Each test is logged for the first three and then once a minute
  (`look-at trigger test N: head yaw, view yaw`).
- **Climbable walls** (`src/vkcore/climb_hook.cpp`, GitHub issue 3). The game's wall-climb mechanic
  (`idPlayerMechanicWallClimb`) owns the view while the player clings to a climbable wall when
  `wallclimb_takeoverViewAngles` is 1, its default: the jump to the wall sets the inhibit bits to 0xF (with
  the view bit), and every tick the mechanic turns its own copy of the view by the user command's angle
  changes only (mouse and turn stick), clamps it to the wall and calls `idPlayer::SetViewAngles` with it
  (return RVA 0x13B87CE). The jump off the wall (RVA 0x13B97B0) goes along the player's first-person view
  axis, pitched up by a fixed angle, so in VR the head could look anywhere while the jump followed the
  stick. The layer holds `wallclimb_takeoverViewAngles 0` and `wallclimb_deadZone_enable 0` while it drives
  the view (head or hand aim): the mechanic then sets the inhibit bits to 7 (no view bit) and leaves the
  angles to the player's own update, so head aim keeps the view on the head and the jump goes where the
  player looks; the dead zone would otherwise set the angles itself whenever the view looks down at the
  wall (a circle 0.9 m below the eye, 0.3 m ahead). Under hand aim a mid hook at the entry of the
  mechanic's dead-zone step (RVA 0x13B88F0, called by its per-tick update only while the player is on a
  wall; unique in the Game Pass build too, where the two cvar loads in it are checked) tells the gate the
  player is climbing: shots, viewmodel and off hand leave the game alone as before, and the view follows
  the head. The game's own `SetViewAngles` calls on the wall do not end that: every tick the player's
  update applies the climb animation's root motion to the player (`pmec_ApplyAnimDeltasToPlayer`, return
  RVA 0x138F33B) and re-sets the angles the view update has just made, and the let-go
  (`DisconnectFromWall`, return 0x13B4677) sets them once to the player's own; a cutscene or the view
  inhibit bits still take the view. The stick turns as it did.
  Log: `climb: wall-climb step hook at RVA 0x13B88F0`, the two cvar writes
  (`climb: wallclimb_takeoverViewAngles 1 -> 0 (reads 0)`), once `climb: on a climbable wall
  (...): the view there is the player's own, so the jump goes where the player looks`, under hand aim
  `controllers: forced view starts (climbable wall; inhibit 0x7)`, and every 10 s with something new
  `climb: N frame(s) on a climbable wall in C climb(s) (S wall-climb step(s)), hand aim on the head in H;
  the game set the view W time(s) on the wall, T of them turned it (largest D deg, from RVA 0x...)`: T
  above 0 means a climb animation turns the player (head aim then takes the turn back).
  The game's own values are saved before the first write and written back once when the multiplayer guard
  stops game touches or the presenter shuts down (`climb: wallclimb_takeoverViewAngles 0 -> 1, the game's
  own value (...)`); neither cvar is saved to the game's config.
  `ETERNALVR_CLIMB_LOOK=0` leaves both cvars and the gate as the game has them and only counts.
- **Piloting a demon** (`src/vkcore/demon_view.cpp`, `demon_aim.cpp`): the Revenant in Cultist Base.
  The idPlayer's controlled entity (+0x88B0) is the demon; while piloting, the move stick follows the
  demon's view, body follow and head aim leave the Slayer alone, the actions press the demon's own
  bindings, and a detour on the demon's update aims it where the weapon hand points (the head under head
  aim) through the game's view and basis setters. `ETERNALVR_DEMON_AIM` chooses the demon's aim apart
  from `ETERNALVR_AIM`: under hand aim for the demon the reticle and the aim smoothing follow the weapon
  hand while piloting, under head aim for the demon the game's crosshair stays. The `controllers: on:` line
  names it (`demon aim head (ETERNALVR_DEMON_AIM)`, or `(as aim)` when unset), and the first aimed update
  of each piloting stretch logs `demon aim: the demon follows the head` (or `hand`).

## Settings

Environment variables for the game process (the rig passes them with `launch-ht.ps1 -ExtraEnv`):

| Variable | Values | Default |
|---|---|---|
| `ETERNALVR_CONTROLLERS` | `1` / `0` | `1` |
| `ETERNALVR_AIM` | `head` (view follows the head), `hand` (follows the weapon hand), `view` (the game's own) | `head` |
| `ETERNALVR_DEMON_AIM` | what aims a piloted demon (the Cultist Base Revenant, above): `head` or `hand`; unset or empty follows `ETERNALVR_AIM`. No effect under `view` aim, where the demon keeps the game's own aim | unset |
| `ETERNALVR_MELEE_AIM` | what aims melee, Blood Punch, glory kills and use under hand aim (above): `head` or `offhand`; unset, empty or `hand`, the weapon hand | unset |
| `ETERNALVR_EQUIPMENT_AIM` | the same for the equipment launcher, the Flame Belch and the throw gesture | unset |
| `ETERNALVR_LOCOMOTION` | `look` / `left` / `right` (that hand, whatever the handedness); the older `head` (= `look`) and `hand` (the hand with the move stick) are still read | `look` |
| `ETERNALVR_TURN` | `smooth` / `snap` / `off` | `smooth` |
| `ETERNALVR_TURN_RATE` | smooth turn, 150 to 400 degrees per second | 230 |
| `ETERNALVR_SNAP_DEGREES` | 30, 45, 90 (15 to 90 accepted) | 45 |
| `ETERNALVR_VIGNETTE` | `off`, `light`, `strong`: the comfort vignette (above) | `off` |
| `ETERNALVR_HANDEDNESS` | `right`, `left` (triggers, grips and clicks swap), `left_mirror` (sticks and face buttons swap too) | `right` |
| `ETERNALVR_DOSSIER` | `hold`: X tap switches equipment, X hold (0.25 s) opens the Dossier; `tap`: the other way round (below) | `hold` |
| `ETERNALVR_MAP_STICKS` | `weapon`: on the Dossier's map the weapon hand's stick pans and the other stick zooms and rotates; `other`: the other way round (docs/VR_MENUS.md) | `weapon` |
| `ETERNALVR_WHEEL_SELECT` | `stick`: the turn stick points at the weapon wheel, whether it or a button holds it; `hand`: the weapon hand points, the stick or button only holds it (above) | `stick` |
| `ETERNALVR_WHEEL_HAND_DEGREES` | the hand's turn that reaches the wheel's rim, 5 to 45 degrees (half of it highlights a weapon) | 20 |
| `ETERNALVR_THUMBREST_WHEEL` | the thumb-rest wheel (above): `edge` (a thumb on its rest, then the other stick pushed within the window), `full` (the other stick picks while the thumb rests), `extreme` (the turn stick always picks; the other thumb's rest turns), `off` | `off` |
| `ETERNALVR_THUMBREST_PICK` | `wheel`: the game's wheel opens and the stick points; `slots`: each direction presses a weapon slot on letting go | `wheel` |
| `ETERNALVR_WEAPON_DIRECTIONS` | under `slots`, the slot (1 to 8, or `none`) of each direction: `up=1,up_right=5,right=2,down_right=7,down=3,down_left=6,left=4,up_left=8` (the game's wheel: combat shotgun, super shotgun, heavy cannon, chaingun, plasma rifle, ballista, rocket launcher, BFG); a direction left out keeps its default | that table |
| `ETERNALVR_THUMBREST_FACE_TOUCH` | `1`: a thumb on A/B or X/Y counts as resting, on Index and Pico 4 controllers (their touch bindings are added for the session) | `0` |
| `ETERNALVR_THUMBREST_SLOWDOWN` | `0`: no slowdown while the thumb-rest wheel holds the game's wheel open (`weaponWheel_slowTimeScale` held at 1); `1` writes nothing | `1` |
| `ETERNALVR_THUMBREST_WINDOW` | `edge`: seconds from the thumb's landing to the push, 0.2 to 1 (tuning) | 0.5 |
| `ETERNALVR_THROW` | `1`: the off hand's overhand throw presses the equipment launcher (above) | `0` |
| `ETERNALVR_THROW_SPEED` | the throw's forward speed, 1 to 5 metres per second | 2 |
| `ETERNALVR_SWING` | `1`: the weapon hand's overhead swing presses the Crucible (above) | `0` |
| `ETERNALVR_SWING_SPEED` | the swing's downward speed, 1 to 5 metres per second | 2.5 |
| `ETERNALVR_POSE_GUARD` | `1` / `0`: with `0` hand positions that jump further than a hand moves are used as they are, and so is any hand velocity (docs/VR_ROOMSCALE.md, Tracked positions that jump) | `1` |
| `ETERNALVR_PUNCH_SPEED` | how fast a hand must move where the head looks to punch, 1 to 4 metres per second; the launcher's Punch speed: Light 1.6, Medium 2.2, Hard 2.8, Very hard 3.4 | 2.8 |
| `ETERNALVR_HOLD_SECONDS` | how long a button is held before its hold action starts (a shorter press is a tap), 0.1 to 1 s; the stick held down for the weapon wheel waits 0.05 s longer; it also sets the both-sticks recenter and capture chords (the recenter hold still totals its own time); the launcher's Hold time: Short 0.15, Medium 0.25, Long 0.4, Very long 0.6 | 0.25 |
| `ETERNALVR_XINPUT` | `auto` (virtual gamepad only if the user-command hook fails), `1` (instead of it), `0` (never) | `auto` |
| `ETERNALVR_SHOT_ORIGIN` | `hand` / `eye` | `hand` |
| `ETERNALVR_AIM_SMOOTHING` | hand-aim smoothing, `0` (off) to `1` (strongest) | `0.3` |
| `ETERNALVR_HAPTICS` | controller vibration strength, `0` (off) to `1`; the launcher's Vibration: Off 0, Light 0.35, Medium 0.6, Strong 1 | `0.6` |
| `ETERNALVR_BHAPTICS` | `1`: bHaptics suits and sleeves through the bHaptics Player ([BHAPTICS.md](BHAPTICS.md)) | `0` |
| `ETERNALVR_BHAPTICS_INTENSITY` | the bHaptics effects' strength, `0` to `1` | `1` |
| `ETERNALVR_VIEWMODEL` | `1` / `0` | `1` |
| `ETERNALVR_WEAPON_ARM` | `ik`: the weapon arm's forearm, elbow and upper arm reach the gun from a shoulder fixed to the head, the gun and wrist where the game puts them; `game`: the game's pose, which points back out of view ([VR_HANDS_HUD.md](VR_HANDS_HUD.md), "The weapon arm"). Most weapons tell the game not to draw the right arm at all (their mesh kit); with `ik` the layer draws it while it poses it and hides it again when the game takes the arm back ("The weapon's mesh kit"). Needs `ETERNALVR_VIEWMODEL=1`; works with either `ETERNALVR_OFFHAND` | `ik` |
| `ETERNALVR_ARMS` | `shown`: the first-person arms drawn; `hidden`: both hidden, the weapon alone, whatever the weapon's mesh kit or the layer's posing ([VR_HANDS_HUD.md](VR_HANDS_HUD.md), "Hidden arms") | `shown` |
| `ETERNALVR_CUTSCENE_ARMS` | `hidden`: in cutscenes around the player (`ETERNALVR_CUTSCENES=immersive`) both arms hidden, the game's weapon FOV kept; `shown`: as in play. Nothing with `ETERNALVR_ARMS=hidden` ([VR_HANDS_HUD.md](VR_HANDS_HUD.md), "Arms in cutscenes"; the launcher's "Arms in cutscenes") | `hidden` |
| `ETERNALVR_BUTTON_PROMPTS` | `0`: the game's prompts keep naming keyboard keys (below) | `1` |
| `ETERNALVR_WEAPON_FOV` | `1` / `0` | `1` |
| `ETERNALVR_SEATED` | `1`: the `[seated]` viewmodel offsets (T-074) | `0` |
| `ETERNALVR_VIEWMODEL_OFFSET` | `f,l,u[,pitch,yaw,roll]`: one offset for every weapon (tuning) | the table |
| `ETERNALVR_CONTROLLER_DATA` | a player's controller data file, or a folder whose `*.toml` files (not in subfolders) are read in name order; each replaces the built-in data of the profile it names, the later of two files for one profile wins, and a file with issues (its lines, or a control map that does not compile) is logged and the built-in data kept (`features/input/player_controller_data.hpp`). The launcher passes the controls folder of the VR settings profile in use (`<data>\controls` for none, `<data>\controls\profiles\<name>` for a profile) when it holds a map of the player's | built in |
| `ETERNALVR_TEST_INPUT` | a scripted input file (below) | none |
| `ETERNALVR_TEST_RUNTIME_NAME` | a runtime name the input side takes instead of the real one (`SteamVR` on the simulator tests the Touch controllers' dashboard pause and stick capture) | none |
| `ETERNALVR_LOOK_TRIGGERS` | `0`: look-at triggers test the game's view (the gun under hand aim) instead of the head, and the tests are only logged (above) | `1` |
| `ETERNALVR_CLIMB_LOOK` | `0` (or `off`): on a climbable wall the game's wall-climb mechanic keeps the view and the jump follows the stick, as in the flat game (above) | `1` |
| `ETERNALVR_DEMON_VIEW` | `0`: no handling for a piloted demon (above) | `1` |
| `ETERNALVR_CONTROLLERS_TRACE` | `1`: log the eye, view angles, body and hand 4 times a second, the game's own command bits, and the turns | `0` |

A value that cannot be used is logged with its name and the default is kept
(`features/input/controller_settings.hpp`).

## Default control map (right-handed, Touch; Index keeps the layout)

| Input | Action | What the game receives |
|---|---|---|
| Right trigger | Fire | `_attack1` 0x1 |
| Right grip | Weapon mod | `_zoom _altfire` 0x10 \| 0x4 |
| Right stick click | Melee, Glory Kill, Blood Punch, use | `_attack2 _use` 0x2 \| 0x8 |
| A | Jump | `_jump` 0x100000000 and up-move 127 |
| B | Dash | `_dash` 0x400000 |
| B held during a cutscene | Skip the cutscene | the skip key R, held (below) |
| Right stick left / right | Turn (smooth or snap) | accumulated yaw |
| Right stick up | Chainsaw | `_quick3` 0x8000000 |
| Right stick down, tap / hold | Quick switch / weapon wheel | `_changeWeapon` 0x40 |
| Left stick | Move | forward / right move |
| Left trigger | Equipment launcher | `_quickuse` 0x800000 |
| Left grip | Flame Belch | `_bfg` 0x100000 |
| Left stick click | Crucible | `_crucible` 0x400000000 |
| X tap / hold | Switch equipment / Dossier (swapped with `ETERNALVR_DOSSIER=tap`) | `_quick0` 0x1000000 / `_inventory` 0x40000000 |
| Y tap / hold | Switch weapon mod / mission info | `_reload` 0x80 / `_objectives` 0x8000000000 |
| Left Menu tap | Pause | the Escape key |
| Both sticks pressed, held 2 s | Recenter the room (below, docs/VR_ROOMSCALE.md) | the layer's own |
| Left Menu held + a trigger | Save a capture of each eye for a bug report (below) | the layer's own |
| Both sticks held + a trigger (SteamVR, Touch) | The same capture, since SteamVR keeps the left Menu button (below) | the layer's own |
| A physical punch | Melee | as the stick click |
| Off hand wound up by the ear, then thrown forward (`ETERNALVR_THROW=1`) | Equipment launcher | as the left trigger |
| Weapon hand raised above the head, then swung down (`ETERNALVR_SWING=1`) | Crucible | as the left stick click |

While a menu is up the controllers drive the menu instead (laser pointer, trigger clicks, B / Y back, the
grips switch tabs, sticks scroll and switch tabs or move the Dossier map, a stick click centres it) and
these actions are held back: `docs/VR_MENUS.md`. In a tutorial popup the game raised by itself, press the
button for the mechanic it introduces (or A / X): each gameplay button also sends its action's default
Slayer key (Flame Belch R, chainsaw C, equipment Left Ctrl, dash Left Shift, switch weapon mod F, switch
equipment G, weapon switch Q, the Crucible V, the weapon slots 1 to 8; `menu::popupActionKey`), held while
the button is. Fire, the weapon mod and the sticks' movement and turning send no keys.

**Recenter: both sticks held.** Recenter: hold both sticks pressed for 2 s, or use the headset's own
recenter (hold the Meta / Oculus button). Holding the left Menu button used to recenter, but Virtual Desktop
watches that button too: on the owner's Quest 3 (2026-09-27) the hold dropped him to the VD desktop. The
chord is built into the mapper (`features/input/stick_chord.hpp`), not a binding, so it is the same in
every handedness and on Index. A single stick click stays instant: melee and the Crucible see it the frame
it goes down. Only a stick pressed while the other is already down waits, at most 0.15 s: pressed within
0.15 s of the other or still held after 0.15 s, it is the chord, and both sticks' bindings are released
until each is let go (the first stick's action has already gone out; a two-stick press cannot be told
apart from a click at the first press); let go within 0.15 s, it is an ordinary click, sent late for one
frame. The chord's `recenter` action goes down 0.25 s after the second stick (like a binding's hold) and
the layer counts the rest of `ETERNALVR_RECENTER_HOLD` (2 s by default; 0 turns the chord off) as before
(`noteRecenterBinding`). In a menu the router sends no C / E for the second stick of the chord. A player's
own map can still bind `left.menu.hold = "recenter"`; the built-in maps no longer do.

**X: tap or hold for the Dossier.** `ETERNALVR_DOSSIER=tap` swaps the two actions on the off hand's X
(A in the full mirror): a tap opens the Dossier and a hold (0.25 s) switches equipment. The swap is made on
the compiled control map (`features/input/dossier_press.hpp`), so it applies to Touch and Index in every
handedness; a button a player remapped (no longer tap = switch equipment, hold = Dossier) is left alone.
The choice is in the `controllers: on:` start-up line (`Dossier on X hold` or `tap`). The launcher's Play tab sets it (Controls, "X button"; `dossier` in `launcher.ini`).

**The in-headset capture (left Menu held + a trigger).** Pulling either trigger while the left Menu
button is held saves the next complete stereo pair for a bug report (an effect that shows in one eye only,
blocky lighting), one capture per pull (`features/input/capture_chord.hpp`). The chord takes both buttons
away from what they normally do: while Menu is held both triggers are held back (no fire, no equipment,
no menu click; a trigger pulled meanwhile stays held back until it is let go), and a Menu press during
which a capture fired neither pauses on its release nor recenters (a player's own Menu-hold recenter;
`TapHoldDetector::cancel`). A Menu press without a trigger pull pauses on release as before. The chord is on
the left Menu button in every handedness (the right one belongs to the system on Touch; on Index it is
the firm left trackpad press), and it works in gameplay and in menus (`GameInput::capture`, outside the
actions a menu holds back; the menu pointer runs its own `CaptureChord` to drop the trigger's click).
Under SteamVR the left Menu button of Touch controllers opens SteamVR's dashboard and never reaches the
game, so with that runtime and family both sticks held as the recenter chord
(`features/input/stick_chord.hpp`) work as the chord's button too: hold both sticks pressed, then pull a
trigger (`CaptureButtons::MenuOrSticks`, `captureButtonsFor` in `features/input/dashboard_pause.hpp`; the
mapper and the menu pointer both take it from the runtime's name and the family in use). Index keeps the
Menu chord alone: its Menu input is the firm trackpad press, which SteamVR leaves alone, and the other
families have a system button of their own (`kDashboardMenuFamilies`). Every Touch button carries a gameplay
action in some map (Y is dash in the full mirror, the mod switch in the others), and a chord on one of them
took a capture and ate the shot when that action and fire overlapped. The stick chord already takes both
sticks away from their bindings in every map, so it overlaps no gameplay combination. The sticks count from
the hold time (0.25 s after the second stick, when the stick chord's recenter level starts), so a quick
two-stick click with a shot is a shot. They hold back only a trigger pulled while they are held (one already
firing keeps firing), and a stick chord with a capture does not recenter
(`CaptureChordOutput::cancelSticks`). A double tap of Menu was the first idea; Virtual Desktop already uses
it (it switches to the desktop view), so it is not used.

The layer saves into `<ETERNALVR_LOG_DIR>\captures\` (`vkcore/bug_capture.hpp`, `presenter_snapshot.cpp`):
`capture-<date>-<time>-p<pair>-t<tick>-L.png` and `-R.png` (the two eye images as presented), `-UI.png` (the
game's GUI target, what the HUD quad shows, with its alpha) and `.txt` (the head and eye poses and FOVs, where
the player stands in the map (the game's view origin and yaw, as `where` and `setviewpos` use them), the
render size, the TAA / DLSS state, the tick). In a menu or loading screen there is no stereo pair: after 0.3 s
the next mono frame is saved as `-mono.png` instead. Under Parallel Eye Rendering a capture's `-L.png` and
`-R.png` are both halves of the ring slot the headset gets (eye R from view 1; the same image in both without
eye views), at the headset's eye image size, and the text file names the game frame record. With
`ETERNALVR_CAPTURE_BURST=<n>` (docs/VR_STEREO.md) a capture is n consecutive pairs (Route S or Parallel Eye
Rendering frames, or mono frames), saved as `-f00-L.png`, `-f00-R.png`, `-f01-L.png` ... (`-f00-mono.png` ...)
with one text file and one GUI image (frame 00's); a frame of the other kind ends a burst early. The copies
are the periodic capture's (`ETERNALVR_CAPTURE_EYES`, `ETERNALVR_CAPTURE_UI`), the PNG files are written on a
background thread straight from the host buffers and the log says `capture: saved eye L/R + UI to ...` with
the number of frames and the time since the trigger pull. At most 50 frames per session
(`bug_capture::kMaxFramesPerSession`): a capture of one frame counts one, a burst each of its frames, and a
burst takes no more than are left. The PNG files are compressed (unlike the periodic eye pairs;
`stereo_seq/deflate.hpp`, each row with the PNG filter that suits it): at a 2056x2216 render size an eye image
is 6 to 7.5 MB (about half its raw size) and the UI image well under 1 MB, so a capture is about 13 MB and a
session's captures at most about 650 MB; the background thread takes about 0.35 s per eye image and 0.1 s for
the UI image. A burst is a little under n times that (about 12 MB per pair, 0.7 s of writing), and until it is
written it holds n pairs of host buffers (about 36 MB per pair at that size; freed once it is written). The
launcher's Export report takes the newest captures, up to 20 MB (`ReportManifest.CapturesCapBytes`: one
capture at that render size, and the zip stays under GitHub's 25 MB attachment limit); of a burst, its text
file, its GUI image and as many of its frames, in order, as fit (the first at that size), and the report's
list of left-out files says how many frames stayed out.

**Skipping a cutscene by hand.** With the automatic skip off (`ETERNALVR_SKIP_CINEMATICS=0`, the
launcher's "Skip cutscenes automatically" unticked), holding the dash action (B on the weapon hand; the
control map decides, so handedness and a player's own map apply) while a cutscene plays holds the game's
skip key R (`features/input/cutscene_skip.hpp`, sent from the user-command hook with the pause key). The
key goes up when the button does or when the cutscene ends; only a press that starts during the cutscene
counts, so a dash held into one does not skip it. The first hold in each cutscene is logged (`controllers:
dash held in a cutscene: holding the skip key`). The game skips after its own hold time, and parts of some
cutscenes do not accept a skip: in the e1m1 intro (run `<workspace>\runs\20260927-155146-mr3`)
a hold 14 s in did nothing, and one 38 s in ended the cutscene 1.0 s later (it runs 63 s or more unskipped;
the automatic skip ends it about 24 s in).

Every command with an action also carries BUTTON_ANY (1 << 57), as the keys' commands do.
The bits are what each action's default Slayer key is bound to in the game's shipped config (bindset 0:
`E` is `_attack2 _use`, `R` is `_bfg`, `C` is `_quick3`, `G` is `_quick0`, `F` is `_reload`), so a
command means exactly what that key press means; the player's own key binds do not matter.

## Controller families

One data file per OpenXR interaction profile (`src/game/eternal/controller_data.hpp`, the input lists in
`features/input/interaction_profiles.cpp`, taken from the registry's `<interaction_profile>` entries in
`reference/openxr/registry/xr.xml`; the Steam Frame's, which the registry does not have, from Valve's
Steam Frame input documentation). SteamVR and the other runtimes pick the suggested profile that fits
the controllers in hand, so each family gets bindings made for its buttons instead of a runtime's guess
from another profile, and SteamVR's "Manage controller bindings" starts from them.

| Family | File | Profile | Extension | Maps |
|---|---|---|---|---|
| Meta Quest / Rift Touch | `oculus_touch.toml` | `oculus/touch_controller` | core | Touch |
| Valve Index | `valve_index.toml` | `valve/index_controller` | core | Touch |
| HP Reverb G2 | `hp_reverb_g2.toml` | `hp/mixed_reality_controller` | `XR_EXT_hp_mixed_reality_controller` | Touch |
| Windows Mixed Reality | `windows_mixed_reality.toml` | `microsoft/motion_controller` | core | own (below) |
| HTC Vive Cosmos | `htc_vive_cosmos.toml` | `htc/vive_cosmos_controller` | `XR_HTC_vive_cosmos_controller_interaction` | Touch |
| HTC Vive wands | `htc_vive_wand.toml` | `htc/vive_controller` | core | reduced (below) |
| Pico 4 | `pico4.toml` | `bytedance/pico4_controller` | `XR_BD_controller_interaction` | Touch |
| Steam Frame | `steam_frame.toml` | `valve/frame_controller_valve` | `XR_VALVE_frame_controller_interaction` (SteamVR's; not in the Khronos registry) | own (below) |

- **Extensions.** `createXrInstance` enables each profile's extension when
  `xrEnumerateInstanceExtensionProperties` lists it (logged: `xr: controller extension ... enabled` or `not
  offered by the runtime`); if the runtime then refuses the instance, it retries with the D3D12 extension
  alone. `attach` suggests a profile when it is core in OpenXR 1.0, its extension is enabled, or the
  instance is 1.1 and 1.1 made the profile core under the same path (the G2, Cosmos and Pico 4 ones). One
  line lists what was suggested and what was skipped. From `reference/openxr/inventory`: SteamVR offers
  the G2 and Cosmos extensions, WMR the G2 one, Meta's PC runtime and VDXR neither. SteamVR also offers
  the Steam Frame's extension, which OpenXR 1.1 did not make core.
- **Touch Pro and Touch Plus** (`XR_FB_touch_controller_pro`, `XR_META_touch_controller_plus`) are left
  out on purpose. Meta's runtime reports them as Touch controllers when the application suggests nothing
  for them, and a family of their own would only split the Touch data: a player's Touch file would stop
  applying on a Quest 3 over Link.
- **Touch-like families** (G2, Cosmos, Pico 4) keep the Touch maps; only `[profile]` differs. The Cosmos
  grip is a click, read by the analog grip action as 0 or 1; its shoulder buttons are left free.
- **The left Menu button is the pause on every family** (View on the Steam Frame). The capture chord
  reads it (`features/input/capture_chord.hpp`), so no family puts a gameplay action on it. Under SteamVR
  with Touch controllers the dashboard takes that button, so the hold that shows mission info pauses
  instead (`applyDashboardPause`, `features/input/dashboard_pause.hpp`): Y in the right-handed map and the
  button swap, B in the full mirror, where mission info is on the right hand. The log says `the runtime
  keeps the Menu button for its dashboard: holding the Y button pauses` (or `B`). A player's map that
  already pauses on another button is left alone.
- **Windows Mixed Reality.** No A/B/X/Y. The trackpad click is the primary button on both hands (jump;
  switch equipment tap, Dossier hold), the right Menu button is the right secondary button (dash; back in
  menus), the left Menu button the pause. The missing left secondary button's jobs move: switch weapon mod
  to a left stick-click tap and the Crucible to a left stick-click hold; mission info has no input. The
  trackpad surface is unused (the stick moves and turns). In the full mirror the dash stays on the right
  Menu button, because the left one is the pause.
- **Vive wands, a reduced layout.** A trigger, a grip button, a trackpad and a Menu button per hand. The
  trackpad is the stick action (`/input/trackpad` as the vector2, its click the stick click), so touching
  the pad moves or turns and the turn pad's up and down gestures are the chainsaw and the weapon switch.
  The right Menu button is the right secondary button (dash; back in menus). Right-handed: right trigger
  fire, right grip weapon mod, right pad click melee, right Menu dash; left pad click jump, left trigger tap
  equipment and hold Flame Belch, left grip tap switch equipment and hold the Dossier, left Menu pause.
  Fire, jump, dash and melee stay instant presses; the tap and hold pairs go on the off hand, where a
  quarter second matters least. Switch weapon mod, the Crucible and mission info have no input. The
  handedness maps swap the triggers, grips and pad clicks (and in the full mirror the pads' move and
  turn); the dash stays on the right Menu button.
- **Steam Frame, a gamepad split in two.** A/B/X/Y (Y top, B outside, A bottom, X inside) and a Menu
  button on the right, a D-pad and a View button on the left, a bumper above each trigger; no trackpad.
  The profile is asymmetric, so each face button has its own action: A and D-pad down are `primary`, B and
  D-pad left `secondary`, X and D-pad right `face3`, Y and D-pad up `face4` (the same place on each hand),
  the bumpers `shoulder`, View the left `menu` and Menu the right one. Triggers, grips, sticks and the turn
  stick's gestures are Touch's; the buttons take the gamepad's actions. Right-handed:

  | Input | Action |
  |---|---|
  | A / B | Jump / dash |
  | X / Y | Chainsaw / Flame Belch (also on the turn stick up and the left grip) |
  | Right bumper, tap / hold | Quick switch / weapon wheel (also on the turn stick down; the turn stick points, above) |
  | Menu, tap | Dossier |
  | Left bumper | Equipment launcher (also on the left trigger) |
  | D-pad up / right / down | Switch weapon mod / Crucible (also on the left stick click) / mission info |
  | D-pad left, tap | Switch equipment |
  | View, tap | Pause |

  Switch equipment is a tap, as on Touch's X. The capture chord is View held + a trigger, under SteamVR
  too: the Frame's View reaches the game. The button swap map moves only the
  triggers, grips and stick clicks; the full mirror swaps the sticks, the bumpers and each face button with
  the one in its place on the other hand (A with D-pad down, B with D-pad left, X with D-pad right, Y with
  D-pad up), and keeps the pause on View and the Dossier on Menu. `ETERNALVR_DOSSIER=tap` finds no X tap and
  hold pair on the Frame and leaves its map alone. SteamVR 2.17.10 renamed the bumper's input from
  `/input/bumper/` to `/input/shoulder/` and still accepts the old name: the file binds `shoulder`, and when
  the runtime refuses the suggestion with `XR_ERROR_PATH_UNSUPPORTED` it is suggested again with `bumper`
  (`olderInputName`; logged: `the runtime refuses a path; trying the bumper's older name`, then `N
  binding(s) suggested for ... (the bumper as /input/bumper/)`, or `(the bumper as /input/shoulder/)` when
  the first try took). Without our bindings SteamVR would present the Frame as Touch controllers.
- **Tests** (`tests/game/eternal/controller_data_tests.cpp`): every file parses against its profile's
  input list, binds each gameplay action on each hand unless the controller lacks the button, compiles
  every map without issues, keeps the pause on the left Menu tap and reaches the essential actions (fire,
  jump and dash, melee as instant presses) in every handedness. `controls_coverage_tests.cpp` builds every
  family and handedness under SteamVR and another runtime as the layer does (the dashboard pause, the
  capture chord's buttons) and checks a pause on a button the runtime passes on, mission info on a button
  (except WMR, the Vive wands and Touch under SteamVR, whose mission-info hold is the pause), that the
  chord's own buttons carry no gameplay action and capture, and that no gameplay button or pair of
  buttons held with a trigger pull, early or after the hold time, captures or holds the pull back.
  `button_labels_tests.cpp` checks that every
  input a built-in map binds has a name for game prompts ("X", "D-pad Up", "View", "Right Bumper").

## Button prompts

Tutorials, hints and HUD prompts name the player's own buttons instead of keyboard keys: "Press [Right
Grip]" rather than "Press [RMB]", "Hold X" for the Dossier on Touch, "Left A" on Index, where both hands
have an A (`vkcore/prompt_hooks.cpp`, `features/input/button_labels.hpp`).

- **Where the names come from.** Each input is named from the path its gameplay action is bound to in the
  family's data file (`/input/x/click` is X, `/input/squeeze/value` is Grip, `/input/thumbstick/click` is
  Stick Click), with the hand in front when the other controller has the same control. An optional
  `[labels]` section in the data file names inputs directly (`"left.primary" = "X"`; inputs trigger, grip,
  stick_click, primary, secondary, menu, stick), for controllers whose buttons are named differently from
  their paths.
- **What a prompt says.** The first control the player's current map binds to the action: presses and taps
  first ("X"), then holds ("Hold X"), then turn-stick gestures ("Right Stick Up", "Hold Right Stick
  Down"). It follows the handedness and a remapped control file. While piloting a demon, the demon's
  abilities name the buttons that press its bindings (the Revenant's rocket barrage is the weapon mod).
- **How.** The game builds prompts from two lookups: an action's bind name to the keys bound to it, and a
  key to its text. The layer answers both for the tooltip text (tutorial popups, tips, hints) and the three
  HUD prompt helpers (blood punch and dash; the demon's ability list, weapon info and tutorial objectives;
  the reticle), and leaves every other caller alone: the weapon wheel and tutorial dismissal match real
  key presses, and the game's key bindings menu lists the real binds. A popup's fixed key token names the
  button that sends that key: tutorial popups take Space for the jump button, E for melee and Tab for the
  Dossier, so "[SPACE] TO DISMISS" reads "[A] TO DISMISS" on Touch. The game is held on its keyboard
  prompts (`swf_platformOverride 2`), so a gamepad or Steam Input cannot switch them to pad glyphs.
- **Menu hints.** The menus' hint bars (pause, settings, the Dossier, the main menu) name the menu
  controls (`docs/VR_MENUS.md`), which no control map changes: "[ESC] BACK" reads "[B] BACK" on Touch
  (the right secondary button, which goes back in every menu), "[ENTER] SELECT" names the weapon hand's
  trigger (it clicks what the pointer is on), and the tab lists' Q and E name the left and right grips in
  short (LG, RG: the tab lists show about two letters, so "Left Grip" read "LE...").
  The hint bars write their keys as key tokens through the same text pass as the popups; the tab lists
  ask for their two keys' text on their own, and the layer answers only those two calls. Hint keys the
  controllers have no button for (R restore defaults, F apply, T and X in the Arsenal) keep the game's
  keys, as do the few hold buttons in hint bars (the game names their key without a token).
  Everything is found by signature at start; if a part is missing the prompts stay the game's and the log
  says which part.
- **From the title screen on.** The names come with the control map, and the mapper that builds it runs
  with the game's user commands, which the title screen and the main menu do not build: the owner's
  main menu still read "[ESC] BACK" (2026-10-02), the map first built about 1.5 s into the first level.
  The XR worker now builds it as soon as the runtime reports a controller (or scripted input stands in
  for one), for the family in use, so the menus name the buttons before any level
  (`vkcore/control_map.cpp`; `controllers: control map for oculus_touch controllers (ahead of the first
  user command, for the menus' prompts), ...`, after `controllers: the runtime reports ...`). Publishing
  the names bumps the bind generation, so the game drops the prompt texts it cached with the keys; whether
  a hint bar already on screen when the controllers wake redraws at once, or only on the next screen, is
  not yet seen on the rig.
- **Limits.** Text is printable ASCII. A prompt for an action the player's keyboard binds leave unbound
  keeps the game's text, since the game only replaces bound actions. The log shows the first few prompts
  each path answered (`prompts: ... asks for _altfire (bindset 0): Right Grip`), the menu hints' texts when
  the control map is set (`prompts: menu hints: back (ESC) B, select (ENTER) Right Trigger, tabs (Q / E)
  Left Grip / Right Grip`) and the first time each fixed key is named (`prompts: the game's text names
  ESCAPE: B`, `prompts: a tab list names Q: Left Grip`).

## Scripted input for rig tests

`ETERNALVR_TEST_INPUT=<file>` lays a small text file over the runtime's controllers; the camera hook
re-reads it when its write time changes (every 10 game frames), so a script can drive a test by rewriting
it. When the XR worker's snapshot is stale the mapper builds its frame from the file alone:

```
right.trigger = 1          # 0..1 (also grip)
left.stick = 0, 1          # x, y
right.primary = 1          # primary, secondary, face3, face4, shoulder, click (stick click), menu
left.thumbrest = 1         # touch sensors: thumbrest, primary_touch, secondary_touch
right.aim = 20, -10        # the right hand points 20 degrees left, 10 down (LOCAL)
left.aim = 0, 0, 90        # an optional roll, counter-clockwise as the user sees it: the left palm up
right.position = 0.2, -0.35, -0.3   # metres from the head (default: the side's rest position)
right.velocity = 0, 0, -3  # metres per second, LOCAL (default: still); the pose does not move with it
```

A hand given an aim is tracked there, with the grip at the same pose, still unless the file gives it a
velocity (for a punch or an arm gesture). Without the file the runtime's
controllers are used alone. OpenXR-Simulator binds our actions but its own controller emulation is driven
through files under the home directory, which the rig does not write; the scripted input covers the same
ground. The simulator also turns some keyboard keys into controller buttons while it runs, so keys sent
with `keys.ps1` during a test can press controller actions too.

## Viewmodel offsets and tuning

`data/weapons/viewmodel_offsets.toml` holds `[forward, left, up, pitch, yaw, roll]` per inventory decl
name in the controller's aim frame (metres and degrees): the game draws each viewmodel relative to the
eye, so the offset moves the model's origin back from the hand by how far the gun is drawn ahead, right
and below the eye. The built-in values are one starting estimate for all weapons; per-weapon rows are
added as each weapon is checked. To tune: run with `ETERNALVR_VIEWMODEL_OFFSET=f,l,u` and the test input
holding a hand still, change the value between runs until the gun's grip sits on the controller, and write
the row. The held item's name and offset are logged on each change.

## Verified

- Unit tests (`ctest`): the button table against the shipped binds (with BUTTON_ANY and the jump's up-move),
  every default-map action reaching the game on Touch and Index in every handedness, the tap hold, the move
  clamp, snap and smooth turns making a whole circle both ways through the accumulator (whole turns land
  on the same 16-bit angle), the forced-view gate, the settings parser, the shot ray and the eye-relative
  controller poses, the local offset maths, the offset table's lookup rules and the built-in table, the
  virtual gamepad's mapping and merge, and the scripted input reader.
- Offline against the exe of build 25216728: every signature above matches once in `.text`; the
  viewmodel site's eight displacements, the fire site's fire-axis displacement and call target, and the
  single SetViewAngles call in UpdateViewAngles (returning to 0x14562A8) hold.
- Rig (2026-09-26, OpenXR-Simulator, mono head-tracked, e1m2, `ETERNALVR_HEAD_POSITION=0`, scripted input,
  `ETERNALVR_CONTROLLERS_TRACE=1`): the live checks below. Evidence is the layer log's `controllers:`
  lines (the trace logs the eye, the game's view angles, the body and the hand four times a second) and
  window captures of the HUD.

## Live checks (OpenXR-Simulator, `ETERNALVR_TEST_INPUT`)

Each run: `launch-ht.ps1 -Layer <staged build> -Label <label> -Map game/sp/e1m2_battle/e1m2_battle
-XrRuntimeJson <simulator json> -ExtraEnv ETERNALVR_TEST_INPUT=<file>,ETERNALVR_CONTROLLERS_TRACE=1[,...]`.

| # | Check | Result |
|---|---|---|
| 1 | Every hook at its RVA; both profiles' bindings suggested | Pass: `user command 0x43E8DD`, `turn 0x17FD3C0`, `forced view 0x1454480` (per-tick return 0x14562A8), `fire 0x135D733`, `viewmodel 0x13807EA`; 28 bindings each for Touch and Index |
| 2 | Buttons, real input kept, console suppression | Pass: the trigger fires (Combat Shotgun ammo 16 to 14); A jumps (eye z 14.66 to 15.89) after the fix below; with the trigger held and `keys.ps1 -Press SPACE` both happen (Heavy Cannon 60 to 49 while the eye rises to 16.01); with the console open the held trigger fires nothing (49 stays 49). B sends exactly the keyboard's dash bits (0x400000 with BUTTON_ANY); the dash did not move the player from the keyboard either at this point of e1m2 |
| 3 | Locomotion, head and hand | Pass: head: the stick walks along the view yaw (45 degrees: x and y rise together); hand (`ETERNALVR_LOCOMOTION=hand`, `left.aim = 90, 0`): the walk goes along yaw 135 (x falls, y rises) with the view unchanged |
| 4 | Snap and smooth turn | Pass: eight snaps right step the view yaw 45, 0, -45, ... 90, 45 (360 degrees), eight left return to the start, one left and one right step 45 to 90 and back; smooth turn at 230 degrees per second (about 1.7 degrees per frame at 135 fps) |
| 5 | Weapon actions | Pass for the quick switch (Combat Shotgun to Heavy Cannon), the weapon wheel (opens on a held stick-down), Flame Belch (its HUD icon goes to cooldown), melee (the bits of `E`), pause and unpause (the Menu button opens and closes the pause menu through the Escape key). Chainsaw and equipment: the bits match the keyboard's `C` and `LCTRL` exactly (rig capture of the game's own command); no target or equipment was available to see them act. The wheel's pointer selection is not confirmed (it now goes through the game's cursor: live check 11) |
| 6 | Hand aim and forced views | Pass: `right.aim = 20, -10` turns the view to yaw 65, pitch 10 (body 45) and holds; falling off a ledge (respawn) logged `forced view starts (forced view angles; inhibit 0xF)` then `ends`, and the view took the game's respawn yaw. Glory kill and meathook: not reached (no enemies near the e1m2 start) |
| 7 | Shots | Pass: hitscan (Combat Shotgun): the start moves from the eye to the hand, the direction is the hand ray, view-to-hand error 0.00 degrees. Heavy Cannon: the game already starts it at the muzzle of the moved viewmodel (so the muzzle tag follows the viewmodel hook); that start is kept and only the direction is set (1.4 to 1.7 degrees between the game's converging axis and the hand ray) |
| 8 | Viewmodel | Pass: the gun follows `right.aim` (60 degrees left, 40 up, 40 down, level) and `right.position`; placement at level aim is close to the game's own. Per-weapon tuning is for the headset |
| 9 | Virtual gamepad (`ETERNALVR_XINPUT=1`, `+in_joystick 1`) | Pass: A jumps, the left stick walks, the trigger fires (16 to 14), turning goes through the turn hook |
| 10 | Guard trip (`ETERNALVR_GUARD_TEST_TRIP_MS=40000`) | Pass: after the trip the trace and the action log stop, the view stays where it was, and a held trigger fires nothing (15 stays 15) |
| 11 | Weapon wheel through the cursor: in e1m2 with two or more weapons, the file `right.stick = 0, -1` for 1 s, then `right.stick = 1, 0` for 1 s, then `right.stick = 0, 0` | Pending. Expect `action weapon_wheel`, about 0.25 s later `the weapon wheel is up`, `weapon wheel: pointing down (motion 0, 200)`, then `pointing right (motion 200, -200)`, then `weapon wheel released after N motion(s)` and a `held item` line for the weapon on the wheel's right; no `menu: the game shows its cursor` (if the game does show it: `menu: the weapon wheel is up: ...` and no panel); the `aim:` line's body yaw unchanged across the wheel |
| 12 | Vibration: in e1m2, the file `right.trigger = 1` for 2 s, then `right.trigger = 0`; then `left.menu = 1` with `right.trigger = 1` (the capture), both back to 0; then the Menu button (pause), `right.aim = 0, 0` and a `right.trigger = 1` tap on the pause menu | Pending. Expect `haptics: on, strength 0.60 ...`, `haptics: rumble hook at RVA 0xAAE730` and `rumble on` in the `game hooks:` line, `haptics: the game's first rumble: low L, high H` on the first shot, then within 10 s `haptics: N pulses (fire a, punch 0, menu c, game d, capture e), r refused` with a up to 14 for 2 s held (one at the press, then every 0.15 s; fewer where the game's rumble on that hand is stronger), d above 0 while shooting, e 2 (both hands), c 1 or more after the click. A runtime without haptics shows the pulses as refused (the first one logged with its result). A punch needs hand velocity: `right.velocity = 0, 0, -3.5` with an aim for 0.3 s |
| 13 | Weapon wheel by the hand (`ETERNALVR_WHEEL_SELECT=hand`): in e1m2 with two or more weapons, the file `right.aim = 0, 0`, then with it `right.stick = 0, -1` for 1.5 s, then (the stick still down) `right.aim = -25, 0` for 1 s, `right.aim = 0, 25` for 1 s, then `right.stick = 0, 0` | Pending. Expect `weapon wheel by the hand` at the end of the `controllers: on:` line, `action weapon_wheel`, `the weapon wheel is up: the weapon hand moves the game's wheel cursor (200 px to the rim at a 20 deg turn)`, no `pointing` line while the hand is still, then `weapon wheel: pointing right (motion 200, 0)`, `pointing up (motion -200, -200)`, `weapon wheel released after N motion(s)` and a `held item` line for the weapon at the wheel's top |
| 14 | Arm gestures (`ETERNALVR_THROW=1`, `ETERNALVR_SWING=1`), in e1m2: the file `left.aim = 0, 0` with `left.position = -0.15, 0.05, 0.15` (wound up) for 1 s, then `left.position = -0.15, -0.05, -0.3` with `left.velocity = 0, -0.5, -3.5` for 0.3 s, then the left hand at rest; later `right.aim = 0, 0` with `right.position = 0.15, 0.3, -0.1` (raised) for 1 s, then `right.position = 0.15, -0.1, -0.35` with `right.velocity = 0, -3.5, -1.5` for 0.3 s; and a control: `left.position = -0.2, -0.3, -0.45` with `left.velocity = 0, 0, -3.5` (a punch from the chest) | Pending. Expect `throw gesture on, overhead swing on` at the end of the `controllers: on:` line; `controllers: gesture: throw` then `controllers: action equipment` and no `action melee` for the throw; `gesture: overhead swing` then `action crucible` and no `action melee` for the swing (the Crucible itself needs the weapon; the press is what is checked); `action melee` and no gesture line for the control |
| 15 | Inputs held through a menu's close, in e1m2: `ETERNALVR_TEST_KEYS=<ms>:ESC` opens the pause menu; once the log has `menu: the game shows its cursor`, the file `left.secondary = 1` for 0.15 s, then `left.secondary = 0`. Then the Dossier: `left.primary = 1` for 0.5 s, then 0; once the Dossier is up, `right.stick = 0, -1` for 0.5 s, then `left.secondary = 1` for 0.15 s with the stick still down, the stick held 0.5 s more, then `right.stick = 0, 0` | Pending. Expect for the pause menu `menu: key down 0x1b`, `menu: the cursor is gone`, `menu: controllers' gameplay input back on`, `controllers: gameplay input back on: presses begun in the menu are dropped` and no `controllers: action switch_weapon_mod` after it (before the fix: `action switch_weapon_mod` in the same millisecond); for the Dossier the same line ending `; a control still held stays out of the game until let go`, and no `action weapon_wheel`, `action quick_switch` or `weapon wheel` line after it |
| 16 | Melee and equipment aim (`tools/rig/qa` scenario `action-aim`: e1m3 `cp_03_shoot_gate`, hand aim, `ETERNALVR_MELEE_AIM=offhand`, `ETERNALVR_EQUIPMENT_AIM=head`, the weapon hand 90 degrees to the left) | Pass (2026-10-02, Debug, runs QA-aim-actions-2 and -3): each melee press held back 1 command (6.0 to 8.8 ms) until the view took the off hand; the first game frame after it has the game's view on the off hand (yaw -90.0, and pitch 15.0 with the off hand at the token) while the weapon hand is at +90; back to the weapon hand 0.5 s after; a frag launched about 0.15 s after the press from the weapon hand's side along yaw 135 (`dir (-0.707 0.707 0.035)`) was moved to the eye and turned to the head's yaw 45 with the same arc (`dir (0.707 0.707 0.035)`), and in screenshots it explodes in front of the head instead of out of view to the left; Flame Belch shots turned the same way. The Praetor token's Use is a trigger box (picked up with the view 90 degrees away), so a target picked by the view (a lunge, a glory kill) is for the headset |

Fixes the rig found: the jump key sets the command's up-move (+0x1A) to 127 as well as its bit, and the
player jumps on the axis (jump now does both); the keys also set BUTTON_ANY (1 << 57), which is now sent
with any action; the generator builds every local user's command and user 1's reaches the angle
conversion first, so the turn is applied only to user 0's (`[gen+0x8D0]`); the scripted input is read on
the camera hook and fed to the mapper directly, because OpenXR-Simulator's window can stall the XR worker
for seconds (below).

## Gaps and deviations

- **OpenXR-Simulator stalls the XR worker.** Its preview window is pumped inside `xrReleaseSwapchainImage`
  on our XR worker, and it sometimes blocks there for seconds or for good (seen in 5 of 11 runs; the
  game keeps running, the headset image freezes). It is the simulator's window, not the layer's code
  (stack: the presenter's `completeCopy` into the simulator's message loop). Scripted input no longer
  depends on the worker; a real runtime is not affected.

- **Aim through deltaViewAngles, not the accumulator.** input-aim.md recommends adding the aim correction
  to the generator's accumulated angles; hand aim uses head aim's deltaViewAngles loop instead, because
  that loop is verified live, already handles the game rewriting the delta around cutscenes, and keeps
  the body frame in one place. The turn does go through the accumulator. If hand aim lags or fights the
  game on the rig, the accumulator is the next step.
- **No wall check for the shot start.** The shot starts at the hand (within 1 m of the eye) with no trace,
  so a gun pushed through thin geometry can fire from behind it. T-062's "shots from the last valid head
  position" needs a collision query; until then `ETERNALVR_SHOT_ORIGIN=eye` is the safe setting.
- **Weapon wheel pointer** through the game's cursor (above) is found by static analysis and covered by
  unit tests; selection in the headset is still to be confirmed. The first version moved the accumulated
  angles, which the wheel does not read (the owner's Quest 3 session: the wheel opened, nothing could be
  selected).
- **Weapon wheel by the hand** (`ETERNALVR_WHEEL_SELECT=hand`) is covered by unit tests only. To confirm in
  a headset: that 20 degrees (10 to highlight) feels right, that the wheel's segments match the directions
  the hand turns as the player sees the wheel, and that hand aim turning the view while the hand points
  does not disturb the wheel (the game reads the cursor, not the view).
- **The thumb-rest wheel** (`ETERNALVR_THUMBREST_WHEEL`, off by default) is covered by unit tests
  and scripted rig scenarios (`tools/rig/qa/qa-scenarios-wheel.ps1`), not yet a headset. To confirm: that
  SteamVR's and Virtual Desktop's Touch emulation report the thumb rest at all (the log's `reports a resting
  thumb` line), the window and the dwell, how often a thumb landing after a jump arms the move stick while
  walking starts, the ticks, the face-button touch on Index, and the slowdown's feel. The slot table's
  default is a guess until a screenshot of the wheel on the rig shows where the game puts each slot. The
  wheel cannot be cancelled once open. The wheel is held for the game's open delay, but the pointer still
  starts moving 0.25 s after the press, so with a Weapon Wheel Open Delay above about 0.25 s the first motion
  can turn the view, as with the stick's own wheel. Under the virtual gamepad (`ETERNALVR_XINPUT`) weapon by
  direction presses nothing: the pad has no weapon slot binds. No hint in the headset yet.
- **Arm gestures** (`ETERNALVR_THROW`, `ETERNALVR_SWING`) are covered by unit tests only. To confirm in a
  headset: the thresholds, false triggers in a real fight, and whether the grenade should fly along the
  throw rather than the view (`docs/VR_INTERACTIONS.md` section 5).
- **The virtual gamepad** depends on the game's default pad binds and, for the sticks, on `in_joystick`.
- **One hand model.** The game's hands model holds both arms; placed at the weapon hand, the left arm
  follows it (T-054: no off-hand model in v1). `ETERNALVR_OFFHAND=free` poses the left arm at the off-hand
  controller instead (docs/VR_HANDS_HUD.md, "Off hand"), mirrored to the right side with the weapon in the
  left hand. The arm holding the gun keeps the gun's grip; `ETERNALVR_WEAPON_ARM=ik` (the default) bends its
  forearm and upper arm up to a shoulder beside the head, so it shows in view (docs/VR_HANDS_HUD.md, "The
  weapon arm").
- **Seated** offsets are chosen by `ETERNALVR_SEATED`; posture detection is not wired to them yet.
- **Bindings from the player's profile** (REQ-11) come through `ETERNALVR_CONTROLLER_DATA`: the launcher's
  controls editor (Edit controls) saves the player's edited copies of the built-in files in the controls
  folder, checked with the layer's rules first (launcher/src/EternalVR.Launcher.Core/Controls,
  docs/release/CONTROLS.md); each VR settings profile keeps its own folder (`controls\profiles\<name>`).
  The layer uses only the file whose profile the runtime reports, so the editor opens on the controllers of
  the newest session log with a `the runtime reports <profile> for the right hand` line
  (`SessionLogs.LastControllerProfile`). Aim assist is not forced off (the injected turn does not use the
  stick path that gates it).
- **Stereo.** Checked live with Route S (docs/VR_STEREO.md, re-test table): with hand aim and snap turn
  every action works as in mono, and the weapon is drawn at the hand in both eyes (Route S retargets the
  hands-and-guns matrices to each eye's frustum). The weapon FOV copy is per game frame.
- **Hook slots.** `kMaxMidHooks` is 448 and `kMaxInlineHooks` 48 (src/vkcore/mid_hook.hpp), about twice the
  most a session can use: 48 mid hooks measured with bHaptics and the free off hand, 13 inline hooks with the
  launcher's newer DLSS DLL, plus Parallel Eye Rendering's 168 mid and 9 inline hooks (docs/VR_STEREO.md). The log's `controllers: hooks in use` line gives the count; a full pool names itself in the
  failing hook's line. Up to v0.1.14 the inline pool was 12, so on the newer DLSS DLL the demon aim hook
  was refused and a piloted Revenant kept the game's own aim.
