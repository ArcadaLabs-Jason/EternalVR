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

## 7. The other pickups (2026-11-11)

Every `interact/...` sync the rig could start, measured with `ETERNALVR_TEST_CAMERA_TRACE=1` under hand aim, with
the default (Still view) and `ETERNALVR_PICKUP_CAMERA=play` (Camera moves). The player is placed with
`teleport <entity>` (the entity names come from `g_dumpSpawnedEntities 1`) or `setviewpos`, then Use (the weapon
hand's stick click) at a list of hand aims until a `controllers: sync` line appears. All in `+map` loads of
`e1m3_cult -checkpoint cp_03_shoot_gate` and `e1m1_intro`.

| Pickup | Where, how | Sync | Forced view | The game's camera | Still view | Camera moves (before this change) |
|---|---|---|---|---|---|---|
| Praetor Suit token | section 6 | `interact/preator_suit_token/preator_suit_token_sync` | 3.1 s | cut 9.4 down, then 6 down and 9 up; no yaw | stays on the head | pitch played, back on the head |
| Automap station | e1m3 `game_interact_automap_1_e1m3`, e1m1 `cathedral_interact_automap_1_e1m1`; teleport or `setviewpos 119.17 72.69 -2.76 90` | `interact/automap/automap_sync` | 2.2 s | cut to 8.5 down; its yaw turns to the station only when the aim is off it, by at most 45 (both runs with the hand 135 degrees off: exactly 45.0); then 5 down and back up | aimed at it: stays on the head; aimed 45 or more off: the view turns 45 to the station in one frame at the start and keeps that heading | aimed at it: 5.2 of pitch played, back on the head; aimed off: the cut left out, then a 44.9 degree turn of the view in one frame as the pickup ends |
| Gore nest | e1m3 `game_interact_gore_nest_1`, `game_interact_gore_nest_3`, teleport | `interact/gore_nest/gore_nest_sync` | 2.75 s | cut to the nest's own side (47 to 178 degrees of yaw from where the player faced, any size: the nest decides it), then a swing of about 30 degrees of yaw, 10 to 25 down, a roll to -14.6 and back with a pull of 6 degrees in one frame at about 60 frames a second (the heart coming out), 2 to 4 degrees of yaw a frame | the view turns to the nest's side in one frame (the cut), then follows the camera's yaw (2 to 4 degrees a frame), no pitch or roll, and keeps that heading after | the cut left out; pitch, yaw and roll played, but the pull left out as a cut (the view stayed 6 degrees rolled off the camera to the end); as the pickup ends the view turned 27.6 (nest 1) and 162.8 (nest 3) degrees in one frame |
| Slayer gate's golden nest | e1m3 `interact_slayer_gate_nest_1`, teleport | `interact/golden_gore_nest/golden_gore_nest_sync` | 2.2 s | as the gore nest (cut 88 to 134); 2.5 s after the pickup's forced view a separate one (inhibit 0xF) teleports the player into the gate's arena, 2917 m away, and turns them to face it | as the gore nest; the teleport turns the view 138 degrees in one frame | as the gore nest (the view keeps its heading, the aim turned back 93.2 degrees as the pickup ends, the eye moved 2.4 m); the teleport, outside the pickup, turns the view 45 degrees in one frame as any teleport does |
| Sentinel Crystal (2026-11-12) | e1m2 in a campaign load: Game Slot 2's Continue Game (a save from Exultia's start), `teleport pickups_progress_argent_cell_1_1318987582_e1m2` (the crystal is then behind the player), two snap turns of 90 degrees to face it and Use with the hand ahead, 30 down: the game's upgrade menu (SELECT SENTINEL CRYSTAL); the trigger held 3.5 s on an upgrade's end unlocks it, the menu closes and the sync starts | `interact/argent_cell/use_sync` | none: no view inhibit in the sync's 3.25 to 3.30 s up to the game's `SetViewAngles` from RVA 0xB4567C, whose own forced view is 3 frames and comes just after the sync ends | traced (`camera: sync trace`, runs cv1 and cv2): the player's angles stay where Use left them (yaw 135, pitch 30 down) under the menu and through the sync; the sync's first frame shows the camera 9.2 to 10.5 degrees off in yaw (125.41 against 135) for that frame only, then 135 and 10 up; then a swing from 121 to 160 degrees of yaw (at most 3 a frame), 0 to 12 up, roll within 4.6; it ends on 134.99, and the set turns the player's angles to it | as the menu hands over, the follow turned the view by the first frame's flick and back (-9.6 and 9.6, run cv1); otherwise the view is on the camera's heading (0.0 off) and the crush in front (eye captures: the hands lift the crystal out of its housing and crush it); now the flick is left out (largest turn in one frame 2.8, cv2) | the camera's motion played on the heading (0.1 degrees off the camera's yaw median, 8.9 at most on the flick's frame, largest step 2.9), back on 135 after; in run q5play, with the hand pointed behind (below), the view turned 180 degrees in one frame as the sync ended; now it keeps its heading |

Not reached, and why:

- **Sentinel Crystal in a `+map` load** (`game_progress_argent_cell_1_1313774949`): from its spot and from in front
  of it (`setviewpos 89.84 61.2 -2.84 180`, `selectDebugEntity` hits it, the housing shows the usable outline), Use
  throws a melee punch and no sync or upgrade menu starts. As on 2026-09-30 in e1m2: in a `+map` load the
  crystal is not usable, whatever the aim. In a campaign load it is (the table's row, 2026-11-12). With the 100%
  save loaded from Mission Select the e1m3 crystal's housing is empty (the save took it): Mission Select gives
  the menus' path, not that save's pickups.
- **Mod bot** (`game_progress_mod_bot_2_e1m3`, `barge_progress_mod_bot_1_e1m1`): the teleport lands in a corridor
  with no drone in view; Use at ten aims around starts nothing. In the Exultia campaign load too
  (`pickups_progress_mod_bot_1_e1m2`, dormant): no drone at its spot, Use at six aims starts nothing (run clp7).
- **Rune** (`game_progress_rune_1_e1m3`, `pickups_progress_rune_1_e1m2`): nothing at its spot; it appears only
  after its trial or encounter, which a `+map` load does not run.
- **Praetor token 4** (`game_progress_praetor_token_4_e1m3`): `selectDebugEntity` hits the token, Use at ten aims
  starts nothing (token 1 at the same checkpoint works, section 6).
- **Chainsaw** (`barge_pickup_weapon_chainsaw_1`): `teleport` puts the player on it (35.81, -1551.34) and nothing
  more: with the intro skipped, with the 155 s intro played in full, and with the chainsaw removed from the
  inventory first (`removeInventoryItem weapon/player/chainsaw`). It is not an `interact/...` sync either: the
  Pickup animations setting does not touch it (section 4: a 4.8 s forced view, the view static, in September).
- No forced view at all: an extra life (`game_pickup_extra_life_extra_life_1_43_e1m3`) and a Sentinel battery
  (`game_progress_sentinel_battery_1_e1m3`) by teleport; the Super Shotgun (`game_pickup_weapon_double_barrel_1_e1m3`)
  is already held in these loads.

What changed for Camera moves:

- The view keeps the heading it had before the pickup (`comfort::PickupHeading`), and for 0.5 s after it head
  aim turns the game's aim back to it, as after a glory kill shown steady. The jump came from the game's
  `SetViewAngles` (from RVA 0xB4567C) as the pickup ends: it sets the player's angles to where the camera
  ended, its cut included, and the body took that as its new heading. Now: `pickup: the aim turned back 44.9 deg`
  (automap, hand 135 off), `-162.8` (nest 3), `-27.8` (nest 1), the written yaw back on its start. In every
  pickup traced the game sets the angles 47 to 63 ms before the forced view ends and the pickup's episode ends
  one or two game frames after the forced view: the set always falls inside the pickup, where the body keeps
  its heading, and the 0.5 s window starts after it. Menu time does not count toward the window.
- No turn back after a pickup that moved the player more than 10 m from where it started (a pickup moves the
  player 0.2 m at the token, 0.9 at the automap, 2.4 to 5.4 at gore nests from their middle; the Slayer gate's
  arena is 2917 m away), across a map load, or after a death in it (`pickup: camera turn N: the view takes the
  game's heading after it ...`); a map load or a death in the window ends it. No measured pickup triggers
  these: the 10 m is a margin over the 5.4 m maximum, and the golden nest's own teleport comes 2.5 s after its
  pickup, as a forced view of its own, so neither applies there. They are a safety net, checked by unit tests.
- A step over 20 degrees is a cut (a cut spread over two frames, or one next to a fast motion); below that a
  cut is a lone step: over 5 degrees, with the steps before and after it under a quarter of it. On the rig
  the cuts' larger neighbour was at most 0.05 of them, the heart pull's 0.4 to 0.7 (0.63, 6.08, 4.16; and 3.02,
  6.67, 2.77 with one game frame twice as long). Each step is played one game frame late, once the next is known,
  so no step is doubled up: the written view's largest step at the gore nest is the pull's own.
- The `pickup-nest-play` QA scenario runs gore nest 3 (teleport, Use looking down): the cut over 90 degrees, the
  written yaw back within 0.5 of its start, no written step over 1.25 times the camera's largest step under 20
  degrees (a bound that follows the frame rate), the roll played and within 1.5 of the camera's a frame earlier
  at the end. On the build before this change it fails all but the first (162.9 degrees off, a 162.7 step, 5.8
  off the roll); the version that played a step and its next one at once (a 9.6 step against the camera's 6.1)
  fails the step bound.

The Sentinel Crystal (2026-11-12, runs q4, q5play, clp6, clp8, cv1, cv2):

- The 180 degrees in the first runs came from the rig, not from the game. Teleported onto the crystal, the player
  faces away from it, and the scenario turned only the weapon hand to it (`Use at hand aim 180, -30`) while the
  simulator's head looked ahead. Under hand aim the game's angles hold the hand's yaw and the view keeps the head's
  (the body yaw is the game's yaw without the hand's, `drivenBodyYaw`): the game's camera, which faces the crystal
  where the hand pointed, was 180 degrees from the view. In all three Still view runs the yaw head aim had injected
  was the hand's 180 (game view yaw minus body yaw 180.0). [log] Turned to face the crystal first (two snap turns,
  Use with the hand ahead) the head looks where the hand points, and the crush plays in front of the view with
  either setting, before this change too (run cv1). [rig, log] A player who looks away from what the hand points
  at sees the camera's motion from where the head looks: the view never turns by the head's distance from the
  hand, as with any camera the game drives under hand aim.
- Still view's turn as the menu closed (-9.7 in q4, -11.4 in clp8): the sync's first frame shows its camera about
  10 degrees off in yaw for that frame only (125.41 against 135 in cv1, with the eye 0.3 units off), and the
  15-degree follow turned the view by it and back the next frame. In clp6 and clp8 the teleport had left the player
  at another heading (Use from 107.2): the sync turns the camera to its own 135 and stays (clp6's 27.8), a cut as at
  the automap station. [log]
- Camera moves' 180 degree turn (q5play): the upgrade menu, up until the sync starts, holds the body without head
  aim's own step, which `comfort::PickupHeading` took as "not the player's heading", so no heading was held through
  the sync (`no heading held (not head aim)`). Meanwhile the hand moved from 180 to the menu (5.4). As the sync
  ended the game's `SetViewAngles` set the player's angles to the camera's 135, and with the hand's yaw now near 0
  head aim made that the body (`aim: the body turned 180.0 deg in one game frame (-45.0 -> 135.0)`). [log]

What changed for the crystal:

- Still: during a pickup the follow's heading leaves out a flick (`xr_math::CameraHeadingHold`). It works on the
  camera's yaw from the player's own view angles, and starts a hold only in a view whose view angles moved under
  0.5 degrees since the last. Measured in the crystal's sync (Still, hand aim, runs cm1): with the hand swept 30
  degrees each way every 0.25 s and two snap turns sent (`turn 180 deg (sent 180)`), and in another run with the
  head swaying 10 degrees each way every 0.2 s, the view angles did not change in any game view of the sync and
  the only hold was the sync's own flick [log]. Under head aim, or in a pickup whose camera stays within 15 degrees
  of the view angles, head aim may write them in the sync (not measured): such a view starts no hold and ends one.
  A hold starts on a jump of the offset over 5 degrees and over 240 degrees a second for the frame's time (at most
  0.25 s of it), out of a camera that moved under a quarter of that the view before (a steady motion never starts
  one, at any frame rate); the heading then stays until the camera comes back within 5 degrees (a flick, also one
  that comes back over two views: never seen), goes on the way it jumped by more than 1 degree (a motion that
  started fast: followed a view late), or for two game views at most (a cut that stays, a gore nest's to its side
  or the crystal's from another heading: followed that late, about 33 ms at 60 frames a second). Outside pickups
  the views keep the last offset, so a pickup's first view can start a hold. After 10 s of one pickup (a sync flag
  that never clears; a gap of over 0.5 s without it starts a new one) nothing more is held until it ends.
  Unit-tested at 30, 60, 90 and 144 frames a second with the crystal's flick
  (held at all four), a flick back over two views, the gore nest's swing and its heart pull from the rig's trace
  (never held at 60 and up; at 30 the sampling that puts the pull's two steps into one view, 8.5 out of 0.7, is
  followed one view late). Logged: `pickup: a pickup's camera jumped X deg in one game frame; the view's follow
  keeps its heading until it comes back (two game views at most)` (the first 30), and once a pickup `pickup: a
  pickup's sync has run 10 s; ...`.
- Camera moves: a menu's views keep the heading the player had before it (`PickupHeading::noteBody`), so a pickup
  that starts from a menu (the crystal; the mod bot's weapon mod menu, not reached) holds it (`the view keeps the body
  yaw 135.0`); and the game's own forced view of its `SetViewAngles`, which for the crystal comes just after the
  pickup ends (it has no forced view of its own), keeps the restore heading too, before head aim turns the aim back
  (`pickup: the aim turned back -180.0` with the hand behind, cv2). Only a forced view that starts within 0.1 s of
  the pickup's end, until it ends (`comfort::EndSetWindow`, `comfort::keptHeading`): a pause menu, or a forced view
  (a glory kill's) that starts more than 0.1 s after the pickup's end, keeps its own body in the 0.5 s window.
- The trace (`ETERNALVR_TEST_CAMERA_TRACE=1`) also logs every game view of a sync and of a menu once head aim is on
  (`camera: sync trace N:`, a pattern the forced-view scenarios leave out), and each line ends with the body yaw,
  the head's angles, the yaw the game's aim holds for the head or the hand (`held`) and the menu and sync flags.
- The `menu-crystal` scenarios (by name only): `menu-crystal` and `menu-crystal-play` face the crystal (snap turns),
  `menu-crystal-behind` and `-behind-play` point only the hand at it. All four: no turn of the view over 5 degrees
  in one game frame from the menu to 2 s after the sync, and the heading after within 1 degree of the menu's; the
  facing pair also the view on the camera's heading through the sync (median within 3 degrees, all within 15).
  In run cv2 (this change), all four pass:

| Scenario | The view off the camera's heading in the sync | Largest turn in one frame | Heading in the menu, after |
|---|---|---|---|
| menu-crystal (Still) | 0.0 median, 9.2 at most (the flick's frame) | 2.8 (the camera's swing) | 135, 134.99 |
| menu-crystal-play | 0.1 median, 8.9 at most | 2.9 | 135, 134.99 |
| menu-crystal-behind (Still) | 180 (the head looks away) | 2.3 | -45, -45.01 |
| menu-crystal-behind-play | 180 | 2.3 | -45, -45 |

Before this change (cv1, q4, q5play): Still facing the crystal turned 9.6 degrees and back as the menu closed; with
the hand behind, Still turned 9.7 and Camera moves turned 180 degrees in one frame as the sync ended. The token and
gore nest scenarios (`pickup-camera`, `pickup-camera-play`, `pickup-nest-play`) pass on this change as before
(cv2: written pitch 0.0 with Still; -3.5 to 5.8, largest step 0.64 with Camera moves; the nest's
yaw 0.00 off its start, largest step 5.77 against the camera's 5.78, roll within 0.03).

The campaign loads (2026-11-12): the QA suite can start a level from the main menu as a player does
(`tools/rig/qa/qa-menu.ps1`, README "Menu-started scenarios"): title screen, Campaign, a save slot, then Mission
Select and a mission or Continue Game. The crystal above is its `menu-crystal` scenarios (run by name only: they need
the rig's Game Slot 2 save of Exultia). The upgrade menu comes up as a popup over the game (`menu: the game shows its
cursor (a popup over the game, not asked for ...)`): head aim holds the body yaw and writes nothing, the menu is on the
world-locked panel in front with the world held behind it, readable in the simulator's composed frame
(`menu-crystal-upgrade-menu-xr-both.png`); nothing in it looked wrong. Its upgrades unlock on a held trigger; a hold
on the line between two upgrades only previews the other routing.

Not checked: a headset; head aim (`ETERNALVR_AIM=head`) or gamepad mode in a pickup (head aim was not tried at the
crystal); the crystal with the hand turned partly off the head (only 0 and 180 degrees); a gore nest or automap station
reached the normal way; frame rates other than the rig's (about 60 game frames a second); a rune (not tried in a
campaign load).

| Run | What | Result |
|---|---|---|
| d1b, d2 | entity dumps (e1m2, e1m1, e1m3) | the names above |
| d3, d4 | Still: crystal, rune, mod bot, automap, battery, extra life, Super Shotgun, token 4, chainsaw, e1m1 automap | automap synced (hand 135 off); the rest as above |
| d5 | Still: gore nests 1 and 3, golden nest, automap aimed at it (Still and Camera moves) and 135 off (Camera moves), chainsaw without the chainsaw | gore nest and automap as in the table |
| d6, d9main | Camera moves: gore nest 1 (hand 135 off) and 3 (looking down) on the build before; chainsaw with the intro played | 27.6 and 162.8 degree turns at the end; the pull left out; chainsaw: no pickup |
| d7main | `setviewpos 89.84 61.2 -2.84 180`, in front of the Sentinel Crystal | the focus on the crystal, Use at six aims: no sync |
| d11main | `pickup-nest-play` on the build before | fails as above |
| f1 | the follow-ups: `pickup-camera`, `pickup-camera-play`, `pickup-nest-play`, the golden nest with Camera moves | all pass; nest: largest written step 6.27 against the camera's 6.27; golden nest as in the table |
| d12fix | `pickup-camera`, `pickup-camera-play`, `pickup-nest-play`, gore nest 1 and the automap (135 off and aimed) with Camera moves on this change | all pass: the written yaw back on its start (0.00 off), largest written step 5.5 at the gore nests (the pull) and 0.3 at the automap, the roll within 0.03 of the camera's; `the aim turned back` 44.9 (automap 135 off), -0.1 (aimed at it), -27.8 (nest 1) |
| clp3 | e1m3 from Mission Select with the 100% save, `setviewpos 89.84 61.2 -2.84 180` | the crystal's housing empty; Use at five aims: no sync |
| clp6, clp8 | Still: e1m2 from Game Slot 2's Continue Game, the crystal by teleport and Use, the upgrade unlocked by a held trigger (clp8: eye captures every 60 pairs) | the menu, then the sync with no forced view; the view turned 27.8 (clp6) and -11.4 (clp8) degrees as the menu closed, the game's camera 180 degrees from the view; no crystal in the eye captures during the crush |
| clp7 | the same save: the mod bot by teleport, Use at six aims | no drone, nothing starts |
| q4, q5play | the `menu-crystal` scenario (then with the hand behind), Still and Camera moves | both pass; Still: a -9.7 degree turn as the menu closed; Camera moves: `pickup: camera turn 1`, 253 game frames, ends on the camera's heading |
| cv1 | the trace of syncs and menus; Still and Camera moves facing the crystal, on the build before | the crystal in front with both; Still turns 9.6 degrees and back as the menu closes (the sync's first frame); Camera moves: largest step 2.9, the heading kept |
| cv2 | this change: the four `menu-crystal` scenarios, `pickup-camera`, `pickup-camera-play`, `pickup-nest-play` | all pass (the table above) |
| cv3 | the hold as it is now (frame-rate aware, from the player's view angles, capped) and the ending set's window, rebased on the menu path's step gating: `menu-mission`, the four `menu-crystal` scenarios, `baseline`, `pickup-camera`, `pickup-camera-play`, `pickup-nest-play` | all 9 pass; the crystal facing it: largest one-frame turn 2.8 with both settings, the view 0.0 (Still) and 0.1 (Camera moves) off the camera's heading (median), the flick held (`jumped -9.9 deg`); behind: 2.4 and 2.3, heading kept |
