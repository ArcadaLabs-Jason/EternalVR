# Camera animations in hands animations, and the chainsaw pickup

Retail `DOOMEternalx64vk.exe`, Steam build 25216728. All addresses are RVAs in this build. The layer side is
`src/vkcore/camera_anim_hook.cpp` (the hook, the logs) and `src/xr_math/camera_anim.cpp` (the math), used by
the camera hook in `src/vkcore/presenter_head.cpp`.

Status: the engine mechanism is found and handled (opt-in), but the chainsaw pickup's camera move in flat
play was not reproduced on the rig (section 4). The default build logs what the camera does in every
forced view and every hands camera animation, so one headset pass through the pickup tells which it is.

## 1. The hands animation's camera [static-verified, rig-verified]

A first-person hands animation may carry a `camera` joint, and `idPlayer::CalculateViewWithoutUpdates`
(0x1451EE0) adds that joint to the first-person view:

- The joint handles are made at static init (0xEC15C0 `player_camera_game`, 0xEC1632 `camera`) and looked
  up on the hands' model at 0x1452238 / 0x1452279. `idHands::additiveCameraAnimator` ("a dedicated channel
  for additive camera anims", string `player_hands_additive_camera_animator`) is the channel that plays them.
- The added camera runs when `idPlayer` vslot 0x4B8 returns true, the cvar `p_applyAnimatedCamera` ("Use
  animated camera data in the hands anims", default 1, object 0x4682420) is set, bit 4 of
  `idPlayer+0x736E` is clear, `hands_updatePos` is set and the `camera` joint exists (0x14524A2..0x14524EA).
- 0x19807F0 reads the joint's transform; 0x3B6E20 (`idMat3::ToAngles`) turns its axis into angles at
  `[rsp+0x48]` (pitch, yaw, roll); the joint's offset is at `[rsp+0x58]`.
- With `p_debugAnimatedCamera 1` the game prints `Added origin: <x, y, z> len.   Added angles <p y r>` every
  time the joint is read (0x1452603), into `qconsole.log`.
- From 0x14526C5: `origin += axis * offset` (the offset in the view frame, before the rotation), then, if any
  angle is non-zero, `axis = (axis.ToAngles() + added).ToMat3()` with the pitch clamped to +-89 degrees
  (0x145277E..0x1452801). The outputs are `firstPersonViewOrigin` / `firstPersonViewAxis` (idPlayer +0x16580 /
  +0x1658C).
- The render-view build (0x6A2C10) copies `idView.gameview` into `players[0].view` (0x6A3098) and, only when
  vslot 0x4B8 returns false, overwrites origin and axis from vslots 0x478 / 0x470 or from 0x1481210
  (0x6A30E0: `test al; jne 0x6A31B7`). In first-person play it returns true, so the camera hook at 0x6A31B7
  sees `idView.gameview`, which holds the first-person view (with the animated camera) and the view effects.

Seen on the rig (`p_debugAnimatedCamera 1`, runs cs3 to cs20): standing and walking the joint adds at most
1.0 degree (weapon raises, landings); a melee punch adds 2 to 4 degrees for a few game frames.

## 2. Not a view effect [static-verified, rig-verified]

`idView::PlayerViewEffects` (0x147DB10; `g_skipViewEffects` skips 0x147E28E..0x147EAB2) holds the bob
(`isBobEnabled` idView+0x101C unless `pm_doom4BobCycle`), the kick (0x1480680), the four advanced screen
shakes (0x147A1F0, "Attempting to apply screen shake at invalid index"), the decl view shake (0x147C6E0;
both shakes also off with `view_skipShakes`), the whiplash (0x147D930, twice), DOF, zoom blur, the eight
overlay layers, the damage ring (0x1483DF0) and the view FOV scales. None reads the hands' camera joint, and
`idView`'s type info has no camera animation member. With `g_skipViewEffects 0` (run cs12, the stereo cvar
set off) the pickup below moved the rendered view no more than with 1.

## 3. What the layer does

- A read-only mid hook at 0x14526C5 (the join of the debug-print and plain paths, found as the target of the
  `je` in the signature at 0x1452564; its first instruction must be `movss xmm5, [rsi+4]`) copies the added
  angles with the idPlayer (rdi) each time the joint is read. The camera hook takes them once per game frame.
- An animation whose largest angle reaches 5 degrees is logged (`camera: camera animation N starts (X deg;
  forced view yes/no)`, `... ends after F game frame(s), largest angle X deg`), below that it is ordinary
  play (section 1).
- With `ETERNALVR_CAMERA_ANIMATIONS=1` its rotation is played: the part played is a smoothstep of the largest
  angle from 5 to 10 degrees (`cameraAnimWeight`), so it fades in without a step; the game's axis without
  the animation (`removeCameraAnim`, which checks that adding it back gives the game's axis) feeds the body
  and head aim, so aim and the scripted-camera check (15 degrees) see the player's own view; the head-tracked
  view gets the angles added the way the game adds them (`addCameraAnim`), folded into the head the stereo
  eyes are built from (`headWithCameraAnim`). The head stays tracked: the animation turns the view relative
  to where the head looks. It counts as a forced view (`ForcedReason::CameraAnimation`): hand aim, shots and
  the viewmodel leave the game alone, and the head's room offset eases out. Never in a cutscene.
  `ETERNALVR_CAMERA_ANIM_MIN=<degrees>` moves the ramp (start there, full at twice) for rig tests.
- Every forced view ends with `camera: forced view N: F game frame(s); the rendered view left the player's
  view angles by up to X deg (largest pitch, yaw, roll)`: the gap between the frame's rendered axis and the
  player's own view angles (the first 50).

## 4. The chainsaw pickup on the rig

- `game/sp/e1m1_intro/e1m1_intro` starts in the Fortress (entity prefix `barge_`), facing +Y at (36, -1604).
  `g_dumpSpawnedEntities 1` lists `barge_pickup_weapon_chainsaw_1` (dormant), the doors
  `barge_func_mover_chainsaw_door_l/_r`, `barge_target_timeline_get_chainsaw`, `barge_trigger_facing_first_door`,
  `barge_target_spawn_intro_1..3`, `barge_target_relay_open_doors` and the `tutorial_*` chainsaw tutorial.
- The doors stay shut on the rig: walking into them (they stop the player at y -1586.9), `trigger` on the
  doors, the timeline or the door relays, `activatetargets` on the timeline and `gibalicious` did not open
  them; `noclip` does not exist. Behind them an invisible wall stops the player at y -1556.5 from the pickup
  room side too (`teleportposition 39.5 -1557 12 124`).
- `teleport barge_pickup_weapon_chainsaw_1` once in play puts the player on the pickup (35.81, -1551.34) and
  runs the pickup: view inhibited (inhibit 0x1F86F) for 4.8 s, then the game teleports the player to the
  chainsaw tutorial (79, -1623) and adds the objective "Chainsaw the Demons". In that pickup the rendered view
  does not move: the camera joint adds at most 0.8 degrees (exactly 0 with the controllers and viewmodel off,
  mono, run cs18), the rendered axis stays within 0.9 degrees of the player's view angles for all 4.8 s
  (`camera: forced view 8: 1321 game frame(s) ... up to 0.9 deg`, run cs20), with `g_skipViewEffects` 1 or 0.
- So the rotation the flat game shows at this pickup was not seen. It may come from the normal approach
  (the timeline behind the doors), which the rig could not reach. The next headset pass through the pickup
  with the default build answers it: a `camera: camera animation N starts` line during the pickup means the
  hands camera joint moves it (then `ETERNALVR_CAMERA_ANIMATIONS=1` plays it), and the `camera: forced view N`
  line says how far the rendered view left the view angles in any case.

## 5. Runs (tmp-vr/rs, build 25216728, OpenXR-Simulator)

| Run | What | Result |
|---|---|---|
| cs2 | `g_dumpSpawnedEntities 1` | the entity names above |
| cs3, cs5, cs8 | `teleport` to the pickup (cs8 with `p_debugAnimatedCamera 1`) | the pickup runs; joint at most 0.8 deg |
| cs6, cs7, cs13, cs14, cs17, cs19 | walk to the doors; `trigger` / `activatetargets` / `gibalicious` | doors stay shut |
| cs10 | the pickup with the per-frame angle check and eye captures | rendered view static; hands in view |
| cs12 | the pickup with the game's own view cvars (`g_skipViewEffects 0`) | rendered view static |
| cs18 | mono, controllers and viewmodel off | joint exactly 0 through the pickup |
| cs20 | `ETERNALVR_CAMERA_ANIMATIONS=1`, `ETERNALVR_CAMERA_ANIM_MIN=1.5`, three melee punches, the pickup | each punch logged and played on top of the head (1.6 to 2.9 deg, 2 to 5 game frames, hand aim yields); the pickup: no camera animation, forced view gap 0.9 deg |
| cs21 | default build, three melee punches, the pickup | no camera animation line (the punches stay under 5 deg) and no forced view from them; the pickup: `camera: forced view 2: 1404 game frame(s); the rendered view left the player's view angles by up to 0.9 deg` |

## 6. A pickup the rig reaches: the Praetor Suit token (2026-11-11)

The chainsaw pickup still cannot be reached the normal way, and `teleport barge_pickup_weapon_chainsaw_1` after
the intro (skipped with `ETERNALVR_SKIP_CINEMATICS=1`) only moved the player this time: no pickup, no forced view
past the teleport's own (run m2). The Praetor Suit token at `game/sp/e1m3_cult/e1m3_cult -checkpoint
cp_03_shoot_gate` is reached the normal way (the `token-pickup` scenario's spot and Use): its sync animation
`interact/preator_suit_token/preator_suit_token_sync` is a pickup animation of the same kind as the Sentinel
Crystal, rune, mod bot and automap ones (`comfort::isKillSync` false). `ETERNALVR_TEST_CAMERA_TRACE=1` logs every
game view of a forced view: the game's camera (the render view before the layer writes it: what the flat game
shows), the player's view angles, the view written and the eye. The `pickup-camera` scenarios
(`tools/rig/qa/qa-scenarios-pickup.ps1`) run it under hand aim with the weapon hand 15 degrees down.

- The pickup is a forced view (`view inhibited; inhibit 0x1F87F`) of about 3.1 s (132 to 135 game frames) from
  the sync's start; the hands camera joint does not move (no `camera animation` line): the camera is the sync
  animation's own. [rig, log]
- The game's camera, pitch in degrees (down positive), yaw 45 and roll 0 all along: the player's 15 (the weapon
  hand's aim), then in one frame 5.6 (a cut to the animation's start, 9.4 degrees), 5.6 to 11.4 over 1.3 s
  (about 4.5 degrees a second), held 1.3 s, 11.3 to 2.2 over 0.45 s (about 20 degrees a second, at most 0.65 in
  one frame), then the game sets the player's view angles to the camera's 2.2 (SetViewAngles from RVA 0xB4567C)
  and the forced view ends; hand aim puts the angles back on the hand. The eye moves under 0.2 units. [rig, log]
- So the flat game's own view cuts at the start, turns 6 degrees down at the token in the hands and 9 up, and
  leaves the player looking where the camera ended. The rotation never leaves the player's yaw here.
- What the headset shows today (the default): the view written stays on the head (pitch 0 in the trace, the
  simulator's head still), only the eye follows the camera. The hands play the animation in front of the head:
  the eye captures show both hands holding the token up in front of the view and the ghostly Praetor suit behind
  it, then the HUD's Praetor Suit Point notice. The camera's 6 and 9 degree turns are not seen; nothing freezes.
  (the eye captures of run m1 during the pickup.) [rig]
- `ETERNALVR_PICKUP_CAMERA=play` (launcher: Pickup animations, Camera moves) plays the camera's motion since the
  pickup took the view on top of the head (`xr_math::CameraTurn`): the cut at the start is left out (a step over
  5 degrees in one frame), so the view goes 5.8 degrees down and 9.4 up from where the head was (in run m2 the
  camera went from 11.4 down to 2.0 at the end, 9.4; run m1's 11.3 to 2.2 is 9.1), at most 0.65
  degrees in one game frame, and the 3.6 degrees left when the forced view ends ease out over 0.3 s. A first
  version that played the camera's gap from the player's view angles jumped 9.4 degrees at the start and 12.7
  at the end (run m1): under hand aim the angles are the hand's, not the head's. [rig, log]

What this says about the known issue: in this pickup the animation plays in the headset; what the layer leaves
out is the camera's small turn. The chainsaw report (a view held still until the tutorial popup, 2026-09-28) came
before a later headset session in which the chainsaw pickup played; no log covers a natural chainsaw pickup.

| Run | What | Result |
|---|---|---|
| m1 | `pickup-camera` and a first `pickup-camera-play` (the gap from the view angles) | default: camera as above, view on the head; play: steps of 9.4 and 12.7 degrees at the start and end |
| m2 | `pickup-camera`, `pickup-camera-play` (the summed motion), the chainsaw by teleport | default unchanged; play: written pitch -3.6 to 5.8, largest step 0.65, back on the head 0.3 s after; chainsaw: no pickup ran |
