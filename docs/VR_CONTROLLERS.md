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
- **Input summary** (`features/input/input_summary.hpp`, `vkcore/input_summary_log.cpp`). So that a report of
  "the controllers did nothing" can be read from a log, every 10 s of play in which anything happened (a
  press, a stick out of the centre, input held back), and once a minute while nothing does, one line:
  `controllers: input, last 10 s: left stick max 0.00, off centre 0.0 s; right stick max 0.93, off centre
  4.2 s; move max 0.93 (sent 0.93); turn 0 deg (sent 0); presses mapped/sent: fire 3/3, jump 1/0; held back:
  menu 3.2 s, game suppressing buttons 0.0 s, forced view 0.0 s`. The 10 s are command time while the game
  runs (the mapper's frames, each counted at most 0.1 s): the window stretches below 10 fps and stops while
  no commands arrive, so it does not line up exactly with the log's timestamps. Per hand, the raw stick's
  largest deflection and the time it was past 0.25 (the stick counts as off centre); the move axis' largest
  magnitude and the turn in degrees as the mapper made them and, in brackets, as sent to the game; per game
  action the presses the controls made ("mapped": the action went down in the mapper's output) and how many
  of them reached the game ("sent": the action went down in the game's command that the layer wrote, after a
  menu's hold, the minimum hold and the melee/equipment aim wait; a frame where the layer wrote nothing, as
  while the game suppresses buttons, sends nothing, and the same goes for its move and turn); and the
  seconds gameplay input was held back by a menu (or a control still held from one), by the game
  suppressing buttons (a popup, a cutscene) and by a forced view (a Glory Kill, a camera animation). Only
  actions with a press are listed (`none` otherwise); a list too long for the line ends in `...` and the
  held-back part is kept. Reading it: a stick that never leaves 0.00 reports nothing from the runtime;
  mapped above sent (`jump 2/0`) means the control worked and something kept the press from the game: a
  menu, the game suppressing buttons, a forced view (see the held back seconds), the minimum hold (a double
  tap inside it gives 2/1), or the aim wait (a melee or equipment press released before the wait ends gives
  1/0); no entry at all for an action means it never went down in that window: the player did not press it,
  or the binding did not fire. The first 360 lines with something in them are all logged, then one a
  minute; the idle lines do not count against that.
- **User command** (L1). A mid-hook on the call of `idUserCmdMgr::PutUserCmd` (RVA 0x43E8DD; r8 the finished
  command, ebx the local user, r14b set while the game suppresses buttons). For local user 0 the actions'
  bits (`game/eternal/usercmd_buttons.hpp`) are ORed into `buttons` (+0x10) and the move is added to
  `forwardmove` / `rightmove` (+0x18 / +0x19) with a clamp to the keys' +-127. The keyboard, mouse and a
  pad keep working. Nothing is ORed while the game suppresses buttons. Pause is the Escape key (the game's
  `toggleMainMenu` is a command, not a button), sent through key injection, also while buttons are
  suppressed so the menu can be closed.
- **Gamepad mode** (the controllers off, `ETERNALVR_CONTROLLERS=0`; the launcher's Motion controllers
  unticked). Nothing of this file runs except one hook: the same PutUserCmd hook, installed for room-scale
  body follow alone (`features/input/usercmd_hook_use.hpp`, `docs/VR_ROOMSCALE.md` "Gamepad mode"). It adds
  body follow's move to `forwardmove` / `rightmove` in a command the pad, keyboard or mouse does not move,
  and writes nothing else: no mapper, no buttons, no turn, no keys; head aim is the camera hook's, as before.
  The same holds under the virtual gamepad (`ETERNALVR_XINPUT=1`), where the controllers' input goes through
  the pad instead. Still controllers-only, so dead in gamepad mode: the comfort vignette's motion (the mapper's
  move and turn, the forced-view gate, a held dash), the recenter hold and the capture chord (the mapper's
  bindings), the forced-view gate itself (the aim hooks; body follow's forced-view block and the glory-kill
  options' forced-view input), the piloted-demon check head aim asks (`controllers::pilotingDemon`, kept by
  the viewmodel hook while the controllers are attached), and bHaptics (started with the controllers' hooks).
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
  Plus report as Touch); with `ETERNALVR_THUMBREST_FACE_TOUCH=1` the A/B or X/Y touch (`primary_touch`,
  `secondary_touch`, while touched and not pressed) stands in on controllers without one (Index, Pico 4; not
  the Steam Frame, whose right bumper already holds the wheel). The touch (T-120; some sensors flicker on and
  off very fast): it registers on its first frame, unless the thumb is elsewhere on that controller then: hard
  evidence (a face button pressed, or the hand's own stick past 0.25, which also covers a runtime that reports
  no stick touch) or a touch sensor beside the rest (the stick's, `thumbstick_touch`; beside a thumb rest the
  face buttons', the same `primary_touch` and `secondary_touch`; beside face touch the trackpad's,
  `trackpad_touch`, Index). Then it registers once the thumb leaves, or once the contact (the touch with gaps
  shorter than the hold-over bridged) has lasted 0.06 s (`kRestDebounceSeconds`), so a sensor that also sees a
  resting thumb works as before; a touch sensor on for 1.5 s without a break while the rest keeps coming and
  going no longer holds a landing back (`kStuckVetoSeconds`). A touch lost for less than the hold-over is a
  forgiven gap, neither a lift nor a new landing. The hold-over is `ETERNALVR_THUMBREST_HOLDOVER` (0.15 s by
  default, 0.1 to 0.3), but never longer than the touch has been on since its landing and never under 0.05 s
  (`kRestHoldOverFloorSeconds`), so a brush lifts soon. A longer loss lifts; at once when the thumb went
  elsewhere after the landing (to jump or turn): on hard evidence at once, on a touch sensor once it has been
  on 0.04 s (`kVetoTouchSteadySeconds`), so a sensor that flickers in step with the rest cuts nothing. The
  thumb going elsewhere while the touch is still sensed changes nothing. A stick that leaves the centre during
  a hold-over, where the lift would change whose it is, waits for the touch to decide: held from the game
  meanwhile, it starts the pick when the touch comes back (counted with the held pushes), and otherwise goes
  to the game at once, not kept for the centre; to the turn stick's gestures and turn it was centred all
  along, so it claims, turns or snaps as a fresh push (`RestWheelOutput::deferred`), up to a hold-over late. A
  push short of its furthest point when the touch decides (on its way back, or back in the centre) is not
  lost: that point is played for one frame (`RestWheelOutput::replay`, to the turn stick's gestures and turn,
  so a snap or a quick switch still fires, or to the movement, where a tap of the move stick during the
  hold-over is played as one frame of movement once the touch decides), or to the wheel when the touch comes
  back, so a flick by direction caught on its way back still commits. A snap pushed during a hold-over so
  waits for the touch, at most the hold-over: a flick by direction begun in a dropout picks. A stick that
  leaves the centre on the frame a route starts or ends is not kept out of play: it is a fresh push for the
  game, or for the wheel where the mode now gives it the stick. With the wheel on, a hand with a thumb rest
  also gets its face buttons' and stick's touch, a face-touch hand its stick's and trackpad's touch. A
  player's controls file replaces the built-in file whole, so the layer adds these bindings to a family's data
  that lacks them (`rest_touch_bindings.hpp`; logged as `N touch binding(s) for the thumb-rest wheel added`,
  nothing rewritten). A runtime that refuses the profile with them is asked again first without the sensors
  beside the rest (`withoutVetoTouch`: the rests still count, a thumb on its stick or buttons only when it
  presses or pushes), then without any of them (the family then has no rest for the session); the log says
  which was taken. Modes (off by default since 2026-10-09, after the first headset
  test; the launcher offers edge, full and, since 2026-10-10, `extreme` as "Turn stick picks (standing)"):
  `edge`: a landing arms the other stick for 0.5 s (`ETERNALVR_THUMBREST_WINDOW`, 0.2 to 1); it counts only
  with both sticks within 0.25 at that moment, so a thumb that lands while the other stick moves or turns does
  nothing, and pushing the other stick past 0.25 in the window starts picking; a thumb left resting never
  stops that stick. A thumb that comes straight from its own controls opens no window either: a landing within
  0.4 s of that hand's primary or secondary button going up (`kOwnButtonVoidSeconds`: jump or dash, then the
  thumb on the rest) or of its own stick being out of the centre (`kOwnStickVoidSeconds`: turn, rest, walk;
  walk, rest, turn). After a pick (never after a cancel, so snap flicks with a resting thumb are not taken one
  after another) the window opens again once the stick has been back in the centre for 0.05 s. A forgiven gap
  of 0.06 s or more is a fresh landing (the window opens again, its voids checked again) when nothing is being
  picked, the other stick has been centred for 0.4 s and nothing was cancelled since the landing. `full`:
  while the thumb rests the other stick is the wheel's (it neither moves nor turns; a stick out when the thumb
  lands stays the game's until the touch has been on 0.06 s (gaps not counted), so a brush while turning
  leaves the turn alone, and then waits for the centre, except one that left the centre after the contact
  began or at most 0.1 s before (`kHeldLeadSeconds`): it picks once the touch registers, held from the game
  until then (a touch held back by the thumb elsewhere, gaps in the contact included, or a frame while the
  turn stick's own down sweep lets go), so a flick begun with the touch is not a turn or a quick switch; a
  touch that never registers gives it back once the contact lapses, a turn stick then turning again once it
  has been back in the centre). `extreme` (for standing players who turn with their body, the owner's spec of
  2026-10-09/10): the turn stick is the wheel's while neither thumb rests; the rest on the other hand (the
  move stick's) gives it back its turning, chainsaw and quick switch and cancels a pick in progress once its
  touch has been on 0.06 s, gaps not counted (a brush does not; an open wheel still closes on its highlight);
  the turn hand's rest alone makes the other stick the wheel's instead, and it does not walk meanwhile
  (`extremeStick`); both rests: both sticks are the game's. During a hold-over the push waits the other way
  round where the lift gives the stick to the wheel: a turn-stick push during the other rest's hold-over turns
  if the touch comes back and picks if it lapses. Blocked (piloting a demon, a cutscene, a forced view) gives
  a stick the wheel holds back to the game once it has been back in the centre, under `full` too; the virtual
  gamepad's look (no turn hook) never reads the turn stick while the wheel holds it. `off`. Picking: under
  `edge` and `full` the rest is needed only to start a pick; one under way ends on the stick (or blocked), not
  on a lift, except that a lift on hard evidence (a press, its own stick out) cancels a pick still pointing
  with nothing committed: a push begun at most 0.05 s before the touch first went (`kTurnBeforeLiftSeconds`; a
  flicker back does not restart that) was a turn and goes to the game as a fresh push; an older one (a pick
  under way, then a jump) stays out of play until it is back in the centre, so it neither snaps nor
  quick-switches. Nothing is pressed until the stick
  has stayed 0.1 s in one of the eight directions at 0.5 or more (once the stick stops going further out, a
  direction is kept until the stick is 7.5 degrees past the edge of its eighth; while it still goes further
  out, by more than 0.02, it points at the eighth it is in, so a stick that curves on its way out takes the
  eighth it reaches, `kOutwardStep`), so a flick presses nothing; with `slots` a stick past 0.85
  (`kFlickThreshold`) counts at once, at the eighth of its furthest point, so a flick picks. Then with
  `ETERNALVR_THUMBREST_PICK=wheel` (default) `weapon_wheel` goes down and the stick points as under the
  stick's own hold (`wheelPointer`; below 0.5 the last direction, so the highlight stays); the stick back in
  the centre (or in `extreme` the rest that ends the route) closes it, held at least the game's open delay
  plus 0.12 s from the press, never under 0.3 s (`wheelHoldSeconds`; the delay is
  `weaponWheel_HoldTimeForOpeningWheel`, the game's Weapon Wheel Open Delay, 180 ms by default, read every
  mapper run; 0.3 s when the cvar is not found), so a quick pick is never a quick switch. There is no cancel
  once it is open. With `slots` the wheel stays closed and the direction held last is remembered; letting go
  presses its `weapon_slot_N` once (held by `ActionHold`), from `ETERNALVR_WEAPON_DIRECTIONS`
  (`up=1,up_right=5,...`, `weapon_directions.hpp`; the default is the game's own wheel, from a rig screenshot:
  each direction picks the weapon the wheel shows there). A direction set to `none` has no part of its own:
  the stick picks the nearest direction with a weapon by angle, each reaching halfway to the next one with a
  weapon (`slotDirection`; with all eight set, the eighths), and the dwell, the flick and the hysteresis run on
  that direction; the launcher's Weapon directions dialog writes the table. The mapper runs the wheel before
  the turn stick's arbiter and the turn: the stick it takes goes to the arbiter as a held wheel from the frame it leaves 0.25,
  so a snap (0.70) or a smooth turn (0.35) never fires first; the move stick is zeroed. After a pick or a
  cancel that stick stays out of play until it is back within 0.25. Blocked (no arming, picking cancelled, an
  open wheel closed): a menu's hold, the game suppressing buttons (r14b, kept from the user command before the
  mapper runs), a forced view (`forcedView()`), a skippable cutscene, a piloted demon, and another route
  holding the wheel (a button bound to `weapon_wheel`, the turn stick's down sweep). With both thumbs resting
  the first to start picking wins. Under `ETERNALVR_WHEEL_SELECT=hand` this wheel still points with its stick.
  Vibration (`HapticSource::Wheel`): a tick on the picking stick's hand when picking starts (0.25, 0.02 s) and
  a firmer one on the pick (0.5, 0.035 s), times the strength. The slowdown
  (`features/input/wheel_slowdown.hpp`, `src/vkcore/rest_wheel.cpp`): the game slows time while its wheel is
  open (`weaponWheel_slowTimeScale`, 0.14); with `ETERNALVR_THUMBREST_SLOWDOWN=0` the layer holds that cvar at
  1 from this wheel's press until 0.3 s after it lets go, through the cvar book, and writes the game's own
  value (read just before each hold) back; at once when the stick's wheel or a button takes over (they always
  slow time), when the controllers go stale, on a multiplayer guard trip (the book's restore) and when the
  layer shuts down. With the slowdown on (default) the cvar is never looked up. Log: the `controllers: on:`
  line ends `thumb-rest wheel edge (picks with wheel, face touch off, slowdown on, hold-over 0.15 s)`; the
  control map line is followed by `thumb-rest wheel edge: rest sensors on both hands, picks with the game's
  wheel, window 0.50 s` (or `off for these controllers (no thumb-rest sensor)`); `thumb-rest wheel: the left
  controller reports a resting thumb` at each hand's first touch, and one line when none has after a minute of
  play; `the game opens its wheel 180 ms after the press (weaponWheel_HoldTimeForOpeningWheel): held at least
  0.30 s` when the delay is read or changes; then per use `armed: the right stick picks (left thumb rest,
  edge, 0.21 s after the touch)` (under `extreme`: `armed: the right (turn) stick picks weapons; the left
  thumb rest gives turning back (extreme)`, or with the right thumb resting `armed: the left stick picks
  (right thumb rest, extreme)`), `the game's wheel is held, pointing up`, `the game's wheel let go pointing
  right`, `weapon_slot_3 picked by the right stick pointing right (out to 0.93)`, `cancelled, nothing pressed
  (...; out to 0.41)`, and `the left thumb landed with a stick out of the centre; no window` (or `straight
  from its own stick`, `straight from its own face button`; capped like the action lines). Every 10 s in which
  a rest's sensor reported a touch, one line for that rest: `left rest, last 10 s: 14 touch(es) from the
  sensor, 2 gone within 0.06 s, 1 begun with the thumb elsewhere, 3 gap(s) under 0.15 s forgiven; 11
  landing(s), 3 back within 0.25 s of letting go, 4 with the right stick already out (3 of it out 0.20 s or
  less), 2 push(es) held until the touch registered or came back, 1 lift(s) at once with the thumb elsewhere,
  0 veto(es) ignored as stuck`: a sensor that flickers (short touches, forgiven gaps), touches the thumb
  elsewhere held back, flicks begun before the rest registered, pushes held while the touch registered (under
  `full`) or during a hold-over, which then picked, lifts cut short, and touch sensors that see the resting
  thumb show there. Back within counts from when the touch went, not from the end of the hold-over. Scripted
  input counts as a rest on both hands.
- **A thumb rest as a button** (`"<hand>.thumbrest.<press>"` in a control map, `features/input/touch_button.hpp`,
  T-125; the controls editor's Left and Right thumb rest rows, Touch only). The rest binds like any button
  (`ButtonInput::ThumbRest`, after `menu`), but it is a touch sensor, so it is read through the wheel's own
  touch filter (`features/input/rest_touch_filter.hpp`, the T-120 rules above, the same hold-over): a flicker
  is one touch, a touch begun with the thumb on its stick or face buttons waits, a lift with the thumb gone to
  them is at once. The thumb elsewhere is that hand's stick, face-button or trackpad touch, a face button
  pressed or its own stick past 0.25. `press` is down from the landing to the lift, so after a plain lift up to
  a hold-over late. `hold` fires once the touch has lasted the hold time on a frame the sensor reports it (it
  waits through a gap) and lasts until the lift. `tap` fires on the lift of a touch that did not become a hold,
  so every touch is one or the other (its length as sensed, from the landing to when it last went, was at
  most about the hold time); the buttons' tap and hold detector is not used, since the hold-over would make
  a 0.15 s touch last 0.30 s and every tap a hold. A lift cut short by the thumb going to its buttons or stick
  after a touch sensed for less than the debounce (0.06 s) is no tap: the thumb passed over the rest on its
  way (`passing over` in the log). A landing within 0.4 s
  of that hand's face button going up or of its own stick being out (`kOwnButtonVoidSeconds`,
  `kOwnStickVoidSeconds`, as the wheel's) is no press of any kind until the thumb lifts and lands again. A
  touch still on when a menu lets go is used up (`TouchButton::cancel`): it fires only after a fresh landing;
  the menu release latch leaves the rest alone, so the wheel reads it as before. One rest, one job: a rest
  the map binds is no rest of the wheel's (the mapper clears its sensor), and the wheel works with the other
  one where that one can work it (`restWheelLoss`), otherwise it is off: under `extreme` the rest of the
  hand without the turn stick gives turning back, so binding it turns the wheel off; under `edge` and `full`
  so does binding a rest when the other rest's picking stick has no role. With the wheel off, a family whose maps bind a rest still gets the touch sensors beside it
  (`restTouchOptions`), and a file from before the thumb rest's line gets that line, as with the wheel on;
  with no rest bound nothing is added, and the built-in maps bind none, so nothing changes for anyone who does
  not bind one. A runtime refusing the rest's touch: `'left.thumbrest.tap' is bound (quick_switch) but the
  runtime refused the thumb-rest touch; that binding does nothing this session`. Log: `the left thumb rest is
  bound (tap = quick_switch); the thumb-rest wheel uses the right rest only` (or `thumb-rest wheel extreme:
  off for these controllers (the left thumb rest, which gives the turn stick back its turning, is bound as a
  button)`, `... (both thumb rests are bound as buttons)`), `thumb rest button: the left controller reports a
  resting thumb`, a line when no rest has reported a touch after a minute of play, and every 10 s in which a
  count moved (a tap or hold is counted when the touch ends, so it can fall in the 10 s after its touch; capped
  apart from the wheel's lines) `left rest button, last 10 s: 6 touch(es), 2 short, 1 begun
  elsewhere, 3 gap(s) forgiven, 1 lift(s) at once (1 passing over: no tap), 2 tap(s), 0 hold(s), 1
  landing(s) voided (own controls)`. Prompts name it `Left Thumb Rest`. A layer before 0.1.47 does not
  know the key and rejects the whole file (the built-in controls are used, logged); from 0.1.47 such keys
  are skipped (below, "Forward-compatible control maps").
- **Touch sensors as buttons** (`"<hand>.<touch>.<press>"` in a control map, `<touch>` one of
  `primary_touch` (A/X), `secondary_touch` (B/Y), `stick_touch`, `trackpad_touch`, `trigger_touch`;
  `features/input/touch_inputs.hpp`, `touch_button.hpp` `TouchKind::Control`, T-126; the controls editor's
  rows such as `Right A touch`, `Left trigger touch`). The second step of fully remappable controls after the
  thumb rest: any touch sensor a controller reports binds like a button (`ButtonInput::PrimaryTouch` ..
  `TriggerTouch`, after `thumbrest`), on the families whose profile has the sensor
  (`interaction_profiles.cpp`): Touch and Pico 4 A/B/X/Y, stick and trigger; Index all five; Windows Mixed
  Reality and the Vive wands the trackpad; Cosmos the stick; the Steam Frame A/B on the right (its left face
  buttons are a D-pad), the stick and the trigger; the G2 none. The primary and secondary touch is that of the
  A, B, X or Y button the family binds to primary and secondary (none where that is a trackpad or a D-pad).
  The trigger's touch is a new OpenXR action, `trigger_touch`; no built-in data suggests a binding for it.
  Semantics: a touch on a control is usually the start of using that control, so the thumb rest's elsewhere
  rules (a touch begun with the thumb on the stick waits, a lift with the thumb gone to a button is at once)
  do not apply. The same filter is used (`rest_touch_filter.hpp`: first-frame landing, gaps under the
  hold-over forgiven, the same hold-over), with no sensor beside it, and the controls under the touch decide
  instead (its own controls, `ownControlsOf`). For the thumb's touches (A/X, B/Y, the stick, the trackpad)
  they are every gameplay binding of that hand on the controller's face in the family's data, the same for
  all four, since one thumb works them all: A, B, X, Y, the stick out past 0.25 and its click, the trackpad,
  the Steam Frame's D-pad, Menu and View (the Vive wands' stick is their trackpad, so a trackpad touch more
  than 0.25 from its centre is the stick in use and only a touch near the centre counts; Index's left Menu is
  its trackpad pressed). So a thumb lying on A and B that presses either taps neither, and one that slides
  from A onto B and presses it within the hold-over is no tap of A. For the trigger's touch, the trigger past
  its press threshold. By default (no data) the primary and secondary buttons and the stick, or the trigger.
  Its own control used while the touch is registered (or in its hold-over) ends a press or hold at once, and
  that touch is no tap; nothing more until the thumb or finger lifts and lands again. A landing within 0.4 s
  of the control under the touch itself being used (`kOwnButtonVoidSeconds`; the narrow set,
  `landingControlsOf`: the gameplay bindings sharing the touch's identifier, so A for A's touch, the stick
  and its click for the stick's, the trigger for the trigger's) presses nothing (a thumb back on its button
  between presses); a landing just after another control under that thumb counts (the left stick let go,
  X touched 0.15 s later, taps). A touch sensed for less than the debounce (0.06 s) is no tap (a brush on
  the way past); a press binding is down from its first frame like any button's, brush or not. Press, tap and hold
  otherwise as the thumb rest's (every touch past the debounce is a tap or a hold, never both). A touch
  still on when a menu lets go, or already on when the mapper is built (`TouchButton::startCancelled`: after
  the headset's dashboard, a gap in controller input, or another family's controllers; the thumb rest bound
  like a button too), fires only after a fresh landing (`TouchButton::cancel`). One input, one job: where
  Face buttons as thumb rest makes A/B or X/Y touched the wheel's rest (a controller without a thumb rest,
  never the Steam Frame, even where a map binds that touch and so has its binding: `faceTouchRests`, `wheelRestHands`, assembled for the mapper by
  `applyFamilyTouchSettings`), binding either touch of
  that hand takes the hand out of the wheel (`bindsWheelRest`, as a bound thumb rest; log and editor note as
  for the thumb rest, the editor still saves); the stick's, the trackpad's and the face buttons' touch the
  wheel reads only as the thumb elsewhere keep doing that when bound (the sensor is read twice). OpenXR: a
  touch's binding is added for the session only when a map of that family binds it
  (`addBoundTouchBindings`; on the Steam Frame too), so with no touch key in the player's file nothing is
  suggested or changes. A refused profile is suggested again in steps (`touchRetries`): without the veto
  sensors, keeping a bound touch (it is not only the thumb elsewhere); then without the bound touches too,
  keeping the rests, so a touch the runtime will not bind costs the wheel nothing; then without the rests.
  Each binding a step that went through dropped is logged: `'right.primary_touch.tap' is bound
  (weapon_slot_2) but the runtime refused its touch binding (/user/hand/right/input/a/touch); that binding
  does nothing this session` (none for a key whose sensor the family lacks). Log: `controllers: N touch
  binding(s) for touches bound as buttons added to the ... data`, `touches bound as buttons (using a control
  under that thumb or finger ends one): right.primary_touch.tap = weapon_slot_2`, `touch button: the right
  controller reports primary_touch` (the first touch of each bound sensor), a line when none has reported a
  touch after a minute of play, and every 10 s in which a count moved `right primary_touch button, last 10 s: 4
  touch(es), 1 short, 0 gap(s) forgiven, 2 tap(s), 0 hold(s), 2 ended by its own control, 0 brush(es): no
  tap, 1 landing(s) voided (own control)`. Under `ETERNALVR_CONTROLLERS_TRACE`, every change of the touch
  sensors the frame reports, bound or not (the first 200, then one a minute): `trace: touch sensors: right
  primary_touch` (or `none`). Prompts name them `A Touch`, `Right Trigger Touch`, `Left Stick Touch`.
  Scripted input: `right.trigger_touch = 1` added beside the other touch sensors. A layer before 0.1.47
  does not know the keys and rejects the whole file (the built-in controls are used, logged); from 0.1.47
  such keys are skipped (below, "Forward-compatible control maps").
- **Forward-compatible control maps** (T-127; `usablePlayerData` and `takeSkippedEntries` in
  `features/input/player_controller_data.hpp`, `isLaterBindingKey` in `binding_keys.hpp`,
  `BindingIssue::skipped`). The third step of fully remappable controls: a player's file written by a later
  version (its editor, or its built-in file copied into the player's on the first save) keeps working.
  **Skipped, the rest of the file used:** a map key of the later-version shape (a hand, then two or more
  names of `a-z`, `0-9`, `_`, dot-separated: `left.futurebutton.tap`, `right.trigger.double_tap`,
  `right.stick.up_left`, `left.trigger.hold.seconds`) that this version does not know, whatever its value; a
  `[profile]` key `gameplay.<hand>.<name>` naming an action this version does not have (whatever its path;
  nothing is suggested for it); a `[labels]` key `<hand>.<name>` naming an input it does not know. Each is
  left out of the data, so the compiler and the mapper never see it, and logged once at load when the file
  is used (`controllers: <file>: [map.right] 'left.futurebutton.tap' (= 'weapon_slot_3') is not a binding
  this version knows (a later version's, or a typo); this entry is skipped`; `[profile]` and `[labels]`
  lines with their line number). A misspelt input or press in a hand-edited key (`left.triger.press`) has
  the same shape, so it too is skipped: that one binding does nothing, with the log line and the editor's
  grey note, instead of the whole file falling back. **Still rejected (built-in
  data, logged as before):** any key of another shape (`weapon_hands`, `left.trigger`, `Left.trigger.press`,
  `wheel.mode`: it could be a map-wide setting that changes what the known keys mean); an unknown value of a
  known key (an action on a button or stick gesture this version has, a stick role, a weapon hand, a
  `[profile]` path this version does not list for the controller, a path that does not suit its action); a
  section other than `[profile]`, `[labels]` and the three maps (a misspelt `[map.rigth]` would otherwise
  leave that handedness with no map at all); malformed lines and headers, a key or section set twice, a
  missing `[profile]` or path; the conflicts between known bindings; and a map whose every key would be
  skipped (`buildBindingProfile` adds an issue with no key, `no key in this map is a binding this version
  knows`: left out, the keys would leave that handedness with nothing bound). A file with only known keys
  reads exactly as before. Why keys and not values: an unknown key binds an input, press kind or gesture this
  version cannot read at all, so leaving it out loses nothing this version could have done and the player's
  map is otherwise whole here; an unknown action or role on an input this version has would leave a control
  the player relies on dead (a stick with no role cannot move or turn), where the built-in controls are at
  least complete; and an unknown value on a known key is most often a typo in a hand edit, which the editor
  shows in red. A later version that adds keys should keep them in the later-version shape (per-hand,
  per-input) only when an older version can safely ignore them: a per-hand key is skipped whatever it
  means, so a setting written that way (`left.trigger.hold.seconds`) must be one whose absence leaves the
  known bindings right. Anything that changes what known keys mean must use another shape, which older
  versions reject; one that adds actions, roles or sections makes files using them fall back on older
  versions.
  **Launcher:** `ControlMapRules` and `ControllerMapFile` mark the same entries `ControlIssue.Skipped`
  (`BindingKey.IsLaterKey`); `ControlsEdit.Problems()` (the rest) stops a save, `NotUsed()` lists the
  skipped ones, which the editor shows in grey under **Not used by this version of the mod** and keeps
  untouched: the file is edited line by line, so an unknown line is written back byte for byte. Tests:
  `tests/features/input/forward_maps_tests.cpp` (one case table, read by the launcher's `ForwardMapsTests`
  to check `ControlMapRules` against the compiler), `ForwardMapsTests.cs` (round trip, LF and CRLF).
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
  - A punch (`punch_detector.hpp`) is melee aimed with the head whatever `ETERNALVR_MELEE_AIM` says, held
    back the same way: the mapper marks a melee press that comes from the punch alone
    (`GameInput::meleeFromPunch`), and the policy runs under hand aim even with both settings on the weapon
    hand. The punch is measured along the head's forward, and the fist (often the off hand's) says nothing
    about where the weapon hand's ray points; with that ray the game took the glory kill's target and turn
    from wherever the gun pointed (a player's three 0.1.50 sessions: all 6 kills whose view turned more than 100
    degrees, up to 177, came in 10 s windows with a punch; no kill without a punch in its 10 s window turned
    more than 92). A punch's head target that is not written within 0.1 s goes out anyway, as for the
    setting. A
    stick-click melee keeps the setting. The fists of a punch stay at the hand as before (the viewmodel
    follows the game's view only with the setting at `head` or `offhand`). Log: `action aim: the view follows
    the head for melee (a punch) (generation N)`, then the held-back press line as above.
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
| `ETERNALVR_THUMBREST_WHEEL` | the thumb-rest wheel (above): `edge` (a thumb on its rest, then the other stick pushed within the window), `full` (the other stick picks while the thumb rests), `extreme` (the turn stick picks, for players who stand and turn with their body; the other thumb's rest turns, the turn hand's rest makes the other stick pick), `off` | `off` |
| `ETERNALVR_THUMBREST_PICK` | `wheel`: the game's wheel opens and the stick points; `slots`: each direction presses a weapon slot on letting go | `wheel` |
| `ETERNALVR_WEAPON_DIRECTIONS` | under `slots`, the slot (1 to 8, or `none`) of each direction: `up=1,up_right=5,right=2,down_right=7,down=3,down_left=6,left=4,up_left=8` (the game's wheel: combat shotgun, super shotgun, heavy cannon, chaingun, plasma rifle, ballista, rocket launcher, BFG); a direction left out keeps its default; a direction set to `none` goes to its nearest neighbours with a weapon | that table |
| `ETERNALVR_THUMBREST_FACE_TOUCH` | `1`: a thumb on A/B or X/Y counts as resting, on Index and Pico 4 controllers (their touch bindings are added for the session) | `0` |
| `ETERNALVR_THUMBREST_SLOWDOWN` | `0`: no slowdown while the thumb-rest wheel holds the game's wheel open (`weaponWheel_slowTimeScale` held at 1); `1` writes nothing | `1` |
| `ETERNALVR_THUMBREST_WINDOW` | `edge`: seconds from the thumb's landing to the push, 0.2 to 1 (tuning) | 0.5 |
| `ETERNALVR_THUMBREST_HOLDOVER` | the thumb-rest wheel: seconds a lost touch of the rest is forgiven before it lifts, 0.1 to 0.3 (tuning; never longer than the touch was on, never under 0.05 s) | 0.15 |
| `ETERNALVR_THROW` | `1`: the off hand's overhand throw presses the equipment launcher (above) | `0` |
| `ETERNALVR_THROW_SPEED` | the throw's forward speed, 1 to 5 metres per second | 2 |
| `ETERNALVR_SWING` | `1`: the weapon hand's overhead swing presses the Crucible (above) | `0` |
| `ETERNALVR_SWING_SPEED` | the swing's downward speed, 1 to 5 metres per second | 2.5 |
| `ETERNALVR_POSE_GUARD` | `1` / `0`: with `0` hand positions that jump further than a hand moves are used as they are, and so is any hand velocity (docs/VR_ROOMSCALE.md, Tracked positions that jump) | `1` |
| `ETERNALVR_PUNCH_SPEED` | how fast a hand must move where the head looks to punch, 1 to 4 metres per second; the launcher's Punch speed: Light 1.6, Medium 2.2, Hard 2.8, Very hard 3.4 | 2.8 |
| `ETERNALVR_HOLD_SECONDS` | how long a button is held before its hold action starts (a shorter press is a tap), 0.1 to 1 s; the stick held down waits 0.05 s longer (0.30 s at the default), and when the weapon wheel is all it opens a push within 20 degrees of straight down holds sooner, 0.10 s less than the buttons, at least 0.10 s (0.15 s at the default; up to 0.1.50 always 0.30 s, so 0.48 s with the game's 0.18 s open delay before the wheel opened, 0.33 s now): the wheel's key is also the quick switch's and the game tells a tap from a hold after its own delay, so a flick is still a quick switch (`features/input/turn_stick_arbiter.hpp`). The trade-off: a down push that rolls out of the 45-degree stay cone after 0.15 s is a hold and its release swaps the weapon, where up to 0.1.50 it was cancelled until 0.30 s (R14 failure mode 4); a roll already past 20 degrees at 0.15 s is still cancelled. A player's map that binds the down hold to anything else has no early hold. Logged `controllers: the turn stick's down hold (the weapon wheel's): 0.15 s within 20 deg of straight down, else 0.30 s; ...`; it also sets the both-sticks recenter and capture chords (the recenter hold still totals its own time); the launcher's Hold time: Short 0.15, Medium 0.25, Long 0.4, Very long 0.6 | 0.25 |
| `ETERNALVR_XINPUT` | `auto` (virtual gamepad only if the user-command hook fails), `1` (instead of it), `0` (never) | `auto` |
| `ETERNALVR_SHOT_ORIGIN` | `hand` / `eye` | `hand` |
| `ETERNALVR_AIM_SMOOTHING` | hand-aim smoothing, `0` (off) to `1` (strongest) | `0.3` |
| `ETERNALVR_HAPTICS` | controller vibration strength, `0` (off) to `1`; the launcher's Vibration: Off 0, Light 0.35, Medium 0.6, Strong 1 | `0.6` |
| `ETERNALVR_BHAPTICS` | `1`: bHaptics suits and sleeves through the bHaptics Player ([BHAPTICS.md](BHAPTICS.md)) | `0` |
| `ETERNALVR_BHAPTICS_INTENSITY` | the bHaptics effects' strength, `0` to `1` | `1` |
| `ETERNALVR_VIEWMODEL` | `1` / `0` | `1` |
| `ETERNALVR_WEAPON_ARM` | `ik`: the weapon arm's forearm, elbow and upper arm reach the gun from a shoulder fixed to the head, the gun and wrist where the game puts them; `game`: the game's pose, which points back out of view ([VR_HANDS_HUD.md](VR_HANDS_HUD.md), "The weapon arm"). Most weapons tell the game not to draw the right arm at all (their mesh kit); with `ik` the layer draws it while it poses it and hides it again when the game takes the arm back ("The weapon's mesh kit"). Needs `ETERNALVR_VIEWMODEL=1`; works with either `ETERNALVR_OFFHAND` | `ik` |
| `ETERNALVR_ARMS` | `shown`: the first-person arms drawn; `hidden`: both hidden, the weapon alone, whatever the weapon's mesh kit or the layer's posing ([VR_HANDS_HUD.md](VR_HANDS_HUD.md), "Hidden arms"); `hands`: rig tests, the forearms and upper arms the layer poses shrunk into the wrists, the gloves alone ("Hands only") | `shown` |
| `ETERNALVR_ARM_SOLVE` | `animated`: the arm solve measures the wrist's twist from the game's animated hand, as before; `bind`: from the bind pose, kept continuous from tick to tick, the forearm's share limited and the rest passed to the upper arm, the off hand's wrist swing limited, and every joint written past its target by the hands' modifier weight so it lands there, the off hand's wrist taking the rest of a turn its attach joint cannot make ([VR_HANDS_HUD.md](VR_HANDS_HUD.md), "The whole arm", "Where an override lands") | `animated` |
| `ETERNALVR_TEST_ARM_MOD_WEIGHT` | development use only: the hands' joint-modifier weight (0 to 1) held while the free off hand is posed, to test where the overrides land, the weights found put back when the arm goes back to the game or the multiplayer guard trips, new hands starting at the game's own ([VR_HANDS_HUD.md](VR_HANDS_HUD.md), "Where an override lands") | unset |
| `ETERNALVR_TEST_HANDS_CUFF` | rig tests: how `ETERNALVR_ARMS=hands` closes the sleeve at the glove: `fold` (every joint from the last roll joint to the shoulder scaled to nothing on the wrist), `stub` (a stub of forearm left), `ring` (the roll joints on the wrist at full size) ([VR_HANDS_HUD.md](VR_HANDS_HUD.md), "Hands only") | `fold` |
| `ETERNALVR_ARM_MODEL` | rig tests: the hands' model: `game`, `base` (the base arms mesh, fp_hands.md6) or `skin[:N]` (Slayer skin set N, 3 by default), through the game's own SetModel ([VR_HANDS_HUD.md](VR_HANDS_HUD.md), "Arms model") | `game` |
| `ETERNALVR_CUTSCENE_ARMS` | `hidden`: in cutscenes around the player (`ETERNALVR_CUTSCENES=immersive`) both arms hidden, the game's weapon FOV kept; `shown`: as in play. Nothing with `ETERNALVR_ARMS=hidden` ([VR_HANDS_HUD.md](VR_HANDS_HUD.md), "Arms in cutscenes"; the launcher's "Arms in cutscenes") | `hidden` |
| `ETERNALVR_BUTTON_PROMPTS` | `0`: the game's prompts keep naming keyboard keys (below) | `1` |
| `ETERNALVR_WEAPON_FOV` | `1` / `0` | `1` |
| `ETERNALVR_SEATED` | `1`: seated posture for the input (T-074) and the `[seated]` viewmodel rows, if the table has any (the built-in one has none: the glove follows the real hand in either posture) | `0` |
| `ETERNALVR_VIEWMODEL_OFFSET` | `f,l,u[,pitch,yaw,roll]`: one offset for every weapon (tuning); a `[sights]` row still turns the model on top of it, and the fists of a melee or a Blood Punch still follow the game's view under hand aim with `ETERNALVR_MELEE_AIM` at `head` or `offhand`, as without it | the table |
| `ETERNALVR_WEAPON_SIGHTS` | `1` (or `on`): each weapon with a `[sights]` row in the offsets table is turned about the grip so its sight line runs parallel to the aim ray (below); the shot ray and the aim dot do not change. `0` (or `off`): rig and debug only, the model not turned; never offered to players, the launcher sends nothing for it (T-135) | `1` |
| `ETERNALVR_ZOOM_LOOK` | rig and debug only: what the layer changes while a weapon is zoomed (below, "Zoomed weapons"): `all`, `off` (the game's own: the gun hidden, the scope drawn, and under the crosshair hold no HUD while scoped), or a list of `hands` (the gun stays) and `scope` (no scope GUI, the weapon's own reticle while zoomed, and the crosshair hold under head aim while a scoped weapon is zoomed) | `all` |
| `ETERNALVR_CONTROLLER_DATA` | a player's controller data file, or a folder whose `*.toml` files (not in subfolders) are read in name order; each replaces the built-in data of the profile it names, the later of two files for one profile wins, and a file with issues (its lines, or a control map that does not compile) is logged and the built-in data kept (`features/input/player_controller_data.hpp`). The launcher passes the controls folder of the VR settings profile in use (`<data>\controls` for none, `<data>\controls\profiles\<name>` for a profile) when it holds a map of the player's | built in |
| `ETERNALVR_TEST_INPUT` | a scripted input file (below) | none |
| `ETERNALVR_TEST_ARM_DUMP` | rig tests: a file the layer appends one JSON line to every 0.25 s with both arms' poses as written and read back, the scripted input as read, the arms model's placement, the held item, the weapon's root joint and the ray shots take; read by the hands rig's checker ([rig-findings/hands-rig.md](rig-findings/hands-rig.md)) | none |
| `ETERNALVR_TEST_ARM_FAULT` | `twist`: development use only, the hands rig's positive control: the forearm's roll joints take none of the forearm's share of the wrist's twist, so it sits on the wrist joint (a candy wrapper the checker must fail) | `none` |
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
equipment G, weapon switch Q, the Crucible V, the weapon slots 1 to 8, and jump Space, which A / X send too,
once per press; `menu::popupActionKey`), held while the button is. Fire, the weapon mod and the sticks'
movement and turning send no keys.

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
  stick_click, primary, secondary, menu, thumbrest, primary_touch, secondary_touch, stick_touch,
  trackpad_touch, trigger_touch, stick), for controllers whose buttons are named differently from
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
- **The weapon info row.** The HUD's labels above the Flame Belch, chainsaw and equipment icons (and the
  Crucible's and switch equipment's) are sized to their text over icons about 60 px apart in a 2048 px wide
  GUI, room for the game's one-letter keys: three full names overlapped into one bar ("Left Grip ght St...
  ft Tr..."). The row gets short texts instead: a name of several words by its initials, a one-word name as
  it is, the press in words ("LG", "RS Up", "LT", "X", "Hold LT"). Its labels come from the same HUD helper as
  the demon's ability list and the tutorial objectives, which keep the full names: the layer tells the row's
  calls apart by the helper's return address and the one a frame above it, one of the weapon info update's
  calls to the helper (`vkcore/prompt_weapon_info.hpp`; the update is the one function that loads
  "_quickuse" and calls the helper: Steam 0xF0BF60, Game Pass 1.0.56.0 0xF17A50). Rig, OpenXR-Simulator,
  e1m3, 2026-10-17: "LG", "RS Up", "LT" in boxes 23, 48 and 22 px wide with 23 and 25 px between them.
- **Menu hints.** The menus' hint bars (pause, settings, the Dossier, the main menu) name the menu
  controls (`docs/VR_MENUS.md`), which no control map changes: "[ESC] BACK" reads "[B] BACK" on Touch
  (the right secondary button, which goes back in every menu), "[ENTER] SELECT" names the weapon hand's
  trigger (it clicks what the pointer is on), and the tab lists' Q and E name the left and right grips in
  short (LG, RG: the tab lists show about two letters, so "Left Grip" read "LE...").
  The hint bars write their keys as key tokens through the same text pass as the popups. The tab lists ask
  for their two keys' text on their own (`KeyNameText`, the game's raw key text), and so does a hold button
  in a hint bar: the mission-complete screen's "ENTER HOLD TO CONTINUE" asks once in `GetKeyString()`
  (found by its error string) and once in the command bar's update (its one caller), each call right after
  the button's hold flag test. The layer answers only those four calls, by their return addresses
  (`features/input/prompt_keys.hpp`, `vkcore/prompt_command_bar.hpp`): the tab lists with the grips, the
  hold buttons with the same buttons as the tokens. The screen reads "Right Trigger HOLD TO CONTINUE" on
  Touch, and holding the trigger with the pointer on the button holds it (rig, OpenXR-Simulator, e1m2's
  level transition, 2026-10-16: the game left for the Fortress 3.8 s after the trigger went down; held 6 s
  with the pointer just below the button, the screen stayed). Offline, both calls resolve the same way in
  the Game Pass 1.0.56.0 image (returns 0x15A1415 and 0x15A220F; Steam 0x159F095 and 0x159FE8F); not run on
  Game Pass. The game's key matching (the bar's `KeyNumToString` calls) is left alone. Hint keys the
  controllers have no button for (R restore defaults, F apply, T and X in the Arsenal) keep the game's
  keys.
  A screen's own previous / next inside a tab, A and D, names the left and right sticks in short (LS,
  RS), since the sticks send A and D while such a screen is up (`docs/VR_MENUS.md`, Pages and tabs inside
  a tab): the mission-complete Rewards tab's page arrows ("A 1/2 D" reads "LS 1/2 RS"), whose two
  `KeyNameText` calls are answered by their return addresses (`vkcore/prompt_page_keys.hpp`; Steam
  0xF5FEC9 and 0xF5FFC3, Game Pass 1.0.56.0 0xF6BA19 and 0xF6BB13, found the same way offline), and the
  tab lists on A and D (the Dossier's Arsenal, Codex and Challenges pages), whose calls are the tab lists'
  own and answer by the key's name. Those screens have room for about two letters where the game writes
  A and D: "RS Left" showed as "..." (rig, 2026-10-17), "LS" and "RS" in full.
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
  LG / RG, pages (A / D) LS / RS`) and the first time each fixed key is named (`prompts: the game's text
  names ESCAPE: B`, `prompts: a tab list names Q: LG`, `prompts: a hold button names ENTER: Right
  Trigger`, `prompts: the Rewards tab's arrows name A: LS`). At start the log says which raw key texts are
  answered (`prompts: KeyNameText hook at RVA 0x17E2680; TabList calls at ...; hold buttons' calls at
  0x159F095 (GetKeyString), 0x159FE8F (the command bar's update)`, 0 for a part not found, which keeps the
  game's key there).

## Scripted input for rig tests

`ETERNALVR_TEST_INPUT=<file>` lays a small text file over the runtime's controllers; the camera hook
re-reads it when its write time changes (every 10 game frames), so a script can drive a test by rewriting
it. When the XR worker's snapshot is stale the mapper builds its frame from the file alone:

```
right.trigger = 1          # 0..1 (also grip)
left.stick = 0, 1          # x, y
right.primary = 1          # primary, secondary, face3, face4, shoulder, click (stick click), menu
left.thumbrest = 1         # touch sensors: thumbrest, primary_touch, secondary_touch, thumbstick_touch,
                           # trackpad_touch, trigger_touch
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
and below the eye. Each weapon's row puts the glove's grip on the gun at the controller's grip: the right
glove's wrist (the arms model's right hand joint, as the game animates the weapon at idle) lands where the
off hand's wrist goes (8 cm behind the grip's centre and 3.5 cm toward the back of the hand), and the model
is not turned. The row is that wrist target less the wrist's position in the model's frame, with the
`[sights]` turn below taken into account; the wrist positions are measured on the hands rig
([rig-findings/hands-rig.md](rig-findings/hands-rig.md), section 10), and
`tests/game/eternal/hand_grip_tests.cpp` checks every row against them. The chaingun and the ballista are
shown held by the left hand at idle, with the right glove parked off the gun; their rows put the gun's own
grip point at the controller (the weapon's root joint plus the glove's offset from it on the other guns),
so the gun sits in the hand while the right glove stays off it. The wrist target is kept in the controller
grip's own frame (`xr_math::placeViewmodel`): on a headset the grip is turned from the aim ray (the layer logs
the turn and the move it makes, 'the weapon hand's grip against its aim ray'), so the model is moved, not
turned, until the glove's wrist sits where the real wrist is; the gun keeps the aim ray's orientation, so the
sights and shots do not change. The rows are measured with the sights turn on; with `ETERNALVR_WEAPON_SIGHTS=0`
(rig and debugging only) the wrist lands up to 0.5 cm off target (the 8.7 cm wrist offset turned by
the sights row, at most 3.5 degrees). With the weapon in the left hand the arms model's left glove holds
the gun (the right glove's grip reflected across the gun's middle; the model is not mirrored): the model is
placed by the right glove's row, its turn reflected (only the Crucible's row turns it), then moved, not
turned, until the left glove's wrist sits on the left hand's wrist point in the grip's frame
(`arm::leftGloveShift`), applied once the left glove's wrist on the held item is known and kept while the game
has the arms, so the rows need no left-hand copy
(docs/VR_HANDS_HUD.md, "The weapon in the left hand"). The Unmaykr's row is measured the same way
(its idle holds it with the guns' grip). The Crucible is held one-handed in the right fist, turned 37 degrees
from the guns' grip, so its row also turns the model (pitch -24.39, yaw -23.75, roll -10.59) until the glove
sits as on the guns, with the hilt in the fist as the game holds it; nothing on it aims, since its swing hands
the arm back to the game (DECISIONS T-139; rig-findings/hands-rig.md, section 12). The `default` row is the
first estimate, not measured: the fists (a melee's are out only while the arm is handed back), the chainsaw
(its button is a quick action that hands the arm back), the Sentinel Hammer (not brought out on the rig; its
throw and slam are actions of the hands, which hand the arm back) and anything new. Seated play uses the same
rows: the glove follows the real hand in either posture (a `[seated]` section is still read and wins while
seated).
To measure a weapon: the hands rig's per-weapon sweep with the arm dump, or on the rig
`ETERNALVR_VIEWMODEL_OFFSET=f,l,u` with the test input holding a hand still. The held item's name and
offset are logged on each change. The key is the base weapon's decl: a weapon mod does not change the
held item's decl, so one row covers every mod of a weapon.

Weapon sights (on by default, T-135; `ETERNALVR_WEAPON_SIGHTS=0` turns them off on the rig): the table's
`[sights]` section gives, per weapon, the direction of its sight line in the arms model's frame as the game
animates it at idle (`[pitch, yaw]`, degrees, pitch positive down, yaw positive to the left:
counter-clockwise seen from above). After the offset the model is turned about the grip, so the hand stays
on the controller, until that line runs parallel to the aim ray; a rotation in the offset (or in
`ETERNALVR_VIEWMODEL_OFFSET`) is then a deliberate turn away from parallel. The shots and the aim dot keep
the controller's ray: the sights point where the shots go, a fixed distance beside the ray (the sights'
height and side above the hand). A weapon without a row (there is no default) is not turned. The held-item
line logs the row and how far it turns the model. The rows are measured on the hands rig
([rig-findings/hands-rig.md](rig-findings/hands-rig.md)).

Zoomed weapons (T-136; `src/game/eternal/zoom_look.hpp`, `zoom_look_book.hpp`, `src/vkcore/zoom_look.cpp`): a
weapon's zoom (`ironSightZoom` in its decl) can hide the hands and the gun at the zoom's start
(`hideHandsOnZoom`) or once zoomed for `hideHandsOnZoomDelay` (the Precision Bolt: 74 ms), draw a scope GUI
(`scopeGuiNum`) and switch to a zoomed reticle (`reticleWhenZoomed`), for the Precision Bolt a full-screen scope
(`zoomStyle` `RETICLE_ZOOM_STYLE_SCOPE`). In the headset the world is not magnified (the view keeps the
headset's FOV), so in 0.1.50 the gun vanished from the hand, under head aim a ring and vignette covered the
head-locked panel, and under hand aim the crosshair hold left the game drawing no screen GUI at all while
scoped, the HUD included. Now:

- With the controllers' viewmodel on, the viewmodel hook looks at the held weapon's decls: its own, its
  `secondaryFireDecl`, and per fire mode `idWeapon::weaponModOverrides[].weaponDecl` and
  `superchargeParms[].weaponDecl`, the decls the game takes the zoom from (the weapon is reached through
  `idHandsItem::item`, whose second word is the object, and is used only when it names the held decl). It
  writes only a decl whose own values hide the hands at the zoom's start or draw a scope GUI
  (`zoomLookNeeded`): the hands are hidden neither at the start nor after the delay (`hideHandsOnZoom` false,
  the delay's gameTime and milliseconds 0: the game adds hidden reason 2 when either asks), and where it draws
  a scope, no scope GUI and the weapon's own reticle while zoomed (with the zoomed scope reticle and the
  reticle style held at none the game drew no screen GUI; with its own, the HUD stays). A hide delay alone is
  left as the game has it: the Heavy Cannon's own decl and `heavy_cannon_burst_detonate` carry 150 ms and kept
  the gun on the rig with the look off. A decl is written only when its fields read as a weapon decl's and it
  has a reticle whose decl name checks out (the layout anchor), and only while the multiplayer guard allows game
  touches (asked again right before each write) and the zoom look's trip listener is in. Each written decl is
  logged once (`zoom look: '<decl>': hands hidden on zoom 0 -> 0, after 74 -> 0 ms, scope GUI 2 -> 0, zoomed
  reticle '<a>' -> '<b>'`); a write that fails part way is rolled back, or kept on record as partial and put
  back by the restore. Which decls are written (rig `w2-sweep`: `e5m2_earth` after `give all`, each of the 8 weapon slots drawn
  and zoomed with both its mods: the Combat Shotgun's pop rockets and full auto, the Heavy Cannon's Precision
  Bolt and `burst_detonate_mastery`, the Plasma Rifle's microwave and AOE, the Rocket Launcher's detonate and
  `lock_mod_mastery`, the Super Shotgun with its meat hook, the Ballista's two mods, the Chaingun's turret and
  energy shell, the BFG 9000 and the Unmaykr): only `weapon/player/heavy_cannon_bolt_action`. No other decl
  among them hides the gun at the zoom's start or draws a scope; the supercharge decls read none throughout,
  and the Crucible (no slot on the rig's controls) was not drawn.
- The scope's ring is not in the decls the layer can change after the HUD has loaded (head aim drew it with
  the weapon's own reticle as the zoomed one, and with the zoomed reticle's `zoomStyle` and `showZoom` written
  to none). So while the player is zoomed (`idPlayer` zoom flags byte 0x736E, bit 1) with a decl whose game
  value has a scope GUI, `controllers::scopeZoomed` asks the presenter for the crosshair hold
  (`hud_reticle_styleOverride 0`, `ui_layer/crosshair.hpp`) under head aim too; under hand aim it is held
  anyway. Logged at each start and end (`zoom look: scoped zoom starts: the reticle style held at none (no
  scope ring)` / `... ends ...`, the first 20 together). Under head aim the game's crosshair is therefore off
  while scoped and there is no aim mark at all (the layer's reticle quad is for hand aim); the Precision Bolt's
  shot still goes to the middle of the view. The zoom flag is read on the Steam build only: on Game Pass its
  bit is not checked yet, so there the hold stays off for a scoped zoom (logged once) and head aim shows the
  scope's ring as in 0.1.50; the decl writes should work there (the type info of every class read is the
  Steam build's), but no Game Pass run has checked them yet.

Only what is drawn changes: the zoom itself, its precision shot, speed and sensitivity are the game's. The
game's decl values go back when the multiplayer guard trips (the zoom look's trip listener, at once: `zoom
look: the game's values back in N of N weapon decl(s) (the multiplayer guard tripped)`), and when the layer
shuts down; only into a decl that still has its name and the layer's values. A game that leaves VR without the
layer shutting down keeps the look for the rest of that run (the gun stays, no scope GUI). Under
`ETERNALVR_UI_CROSSHAIR=mask` and in a mono session there is no reticle hold, so head aim shows the ring there.
`ETERNALVR_ZOOM_LOOK` (rig and debug only): `all` (the default), `off` for the game's own look, or a list of
`hands` and `scope`.

Rig check (the RTX 3080 Ti test PC, OpenXR-Simulator, Route S, scripted input, `e5m2_earth` whose start inventory has the
Precision Bolt; a scripted run, its captures kept on the test PC):
the fully zoomed GUI capture (about 4.8 s into the zoom) has the HUD and nothing else, 32828 non-transparent
pixels, the same as unzoomed, under hand aim (`s16-hand`) and head aim (`s17-head`), also on a second zoom
after switching weapons and mods; the gun stays in both eyes (the lower-right box differs from a gun-free
frame by 30 %); the game's screen GUI draws stay at their normal count through the zoom. Positive controls:
0.1.50 (`SCOPECHECK-20261013\a1c`, hand aim) and `ETERNALVR_ZOOM_LOOK=off` on this build (`s5-off`): 0 pixels
and the gun gone (0 % in the box); 0.1.50 under head aim (`a3`): the ring, 1052643 pixels. The restore (QA `mp-guard-trip`): the Bolt's
decl written at 39.2 s, the test trip at 55.86 s and in the same millisecond `zoom look: the game's values back
in 1 of 1 weapon decl(s) (the multiplayer guard tripped)`.
The QA scenarios `crosshair-hold` and `crosshair-hold-head` (e1m3, the Heavy Cannon with both mods) check the
Bolt's decl line with its reticle swap, the scoped zoom and, under head aim, that the hold writes only during
it, that the last zoom ends with the crosshair written back, and that neither log hit its cap; `mp-guard-trip`
draws the Heavy Cannon with the Bolt before the trip and checks that every written decl is put back.

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
- **A thumb rest as a button** is covered by unit tests only, not yet a headset. To confirm: a tap on a
  Quest 3 quick-switches, how often a resting thumb fires a press or hold binding by mistake (the log's
  10 s line), and that SteamVR's and Virtual Desktop's Touch emulation report the rest.
- **Touch sensors as buttons** are covered by unit tests and scripted rig scenarios (`touchbtn-tap`,
  `touchbtn-unbound`, `touchbtn-press`, `touchbtn-face`, `touchbtn-stick` in
  `tools/rig/qa/qa-scenarios-binds.ps1`: A touched taps weapon_slot_2 and the heavy cannon comes up, the touch
  reaches the layer and nothing fires with no touch bound, A pressed while touched jumps and taps nothing, A
  pressed with the thumb also on B whose tap is bound jumps and taps nothing, X touched 0.15 s after the left
  stick is let go taps), not yet a headset. To confirm: a tap of A
  touched on a Quest 3 fires its binding and a jump does not, how often a thumb or finger resting on a control
  fires a press or hold by mistake (the log's 10 s line), the trigger's touch on Touch and Index, and that
  SteamVR's and Virtual Desktop's emulation report each sensor.
- **Arm gestures** (`ETERNALVR_THROW`, `ETERNALVR_SWING`) are covered by unit tests only. To confirm in a
  headset: the thresholds, false triggers in a real fight, and whether the grenade should fly along the
  throw rather than the view (`docs/VR_INTERACTIONS.md` section 5).
- **The virtual gamepad** depends on the game's default pad binds and, for the sticks, on `in_joystick`.
- **One hand model.** The game's hands model holds both arms; placed at the weapon hand, the left arm
  follows it (T-054: no off-hand model in v1). `ETERNALVR_OFFHAND=free` poses the left arm at the off-hand
  controller instead (docs/VR_HANDS_HUD.md, "Off hand"). With the weapon in the left hand the model's left
  glove holds the gun and its right arm is the off hand (docs/VR_HANDS_HUD.md, "The weapon in the left
  hand"; T-140). The arm holding the gun keeps the gun's grip; `ETERNALVR_WEAPON_ARM=ik` (the default) bends its
  forearm and upper arm up to a shoulder beside the head, so it shows in view (docs/VR_HANDS_HUD.md, "The
  weapon arm").
- **Seated** viewmodel rows: the built-in table has none (the glove follows the real hand in either posture); a
  `[seated]` section, if one is added, is chosen by `ETERNALVR_SEATED` or a seated room posture.
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
