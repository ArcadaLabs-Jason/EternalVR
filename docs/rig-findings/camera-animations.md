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
| Gore nest | e1m3 `game_interact_gore_nest_1`, `game_interact_gore_nest_3`, teleport | `interact/gore_nest/gore_nest_sync` | 2.75 s | cut to the nest's own side (47 to 178 degrees of yaw from where the player faced, any size: the nest decides it), then a swing of about 30 degrees of yaw, 10 to 25 down, a roll to -14.6 and back with a pull of 6 degrees in one frame at about 60 frames a second (the heart coming out), 2 to 4 degrees of yaw a frame | the view turns to the nest's side in one frame (the cut), then follows the camera's yaw (2 to 4 degrees a frame), no pitch or roll, and keeps that heading after; with Still's hold (below) the same, the cut shown whole two game views late (run ns1) | the cut left out; pitch, yaw and roll played, but the pull left out as a cut (the view stayed 6 degrees rolled off the camera to the end); as the pickup ends the view turned 27.6 (nest 1) and 162.8 (nest 3) degrees in one frame |
| Slayer gate's golden nest | e1m3 `interact_slayer_gate_nest_1`, teleport | `interact/golden_gore_nest/golden_gore_nest_sync` | 2.2 s | as the gore nest (cut 88 to 134); 2.5 s after the pickup's forced view a separate one (inhibit 0xF) teleports the player into the gate's arena, 2917 m away, and turns them to face it | as the gore nest; the teleport turns the view 138 degrees in one frame | as the gore nest (the view keeps its heading, the aim turned back 93.2 degrees as the pickup ends, the eye moved 2.4 m); the teleport, outside the pickup, turns the view 45 degrees in one frame as any teleport does |
| Sentinel Crystal (2026-11-12) | e1m2 in a campaign load: Game Slot 2's Continue Game (a save from Exultia's start), `teleport pickups_progress_argent_cell_1_1318987582_e1m2` (the crystal is then behind the player), two snap turns of 90 degrees to face it and Use with the hand ahead, 30 down: the game's upgrade menu (SELECT SENTINEL CRYSTAL); the trigger held 3.5 s on an upgrade's end unlocks it, the menu closes and the sync starts | `interact/argent_cell/use_sync` | none: no view inhibit in the sync's 3.25 to 3.30 s up to the game's `SetViewAngles` from RVA 0xB4567C, whose own forced view is 3 frames and comes just after the sync ends | traced (`camera: sync trace`, runs cv1 and cv2): the player's angles stay where Use left them (yaw 135, pitch 30 down) under the menu and through the sync; the sync's first frame shows the camera 9.2 to 10.5 degrees off in yaw (125.41 against 135) for that frame only, then 135 and 10 up; then a swing from 121 to 160 degrees of yaw (at most 3 a frame), 0 to 12 up, roll within 4.6; it ends on 134.99, and the set turns the player's angles to it | as the menu hands over, the follow turned the view by the first frame's flick and back (-9.6 and 9.6, run cv1); otherwise the view is on the camera's heading (0.0 off) and the crush in front (eye captures: the hands lift the crystal out of its housing and crush it); now the flick is left out (largest turn in one frame 2.8, cv2) | the camera's motion played on the heading (0.1 degrees off the camera's yaw median, 8.9 at most on the flick's frame, largest step 2.9), back on 135 after; in run q5play, with the hand pointed behind (below), the view turned 180 degrees in one frame as the sync ended; now it keeps its heading |
| Rune (2026-11-12) | e1m2 in the same campaign load: `teleport pickups_progress_rune_1_e1m2` (the rune is then behind the player), two snap turns of 90 degrees to face it and Use with the hand ahead, 30 down: the game's menu (CHOOSE A RUNE); the trigger held 3.5 s on a rune unlocks it, the menu closes and the sync starts. No trial first: the rune's interaction opens when the player touches it (idle, waiting, open on touch, ready) | `interact/rune/use_sync` | 7.6 s, from the sync's first game frame (view inhibited; inhibit 0x1F87F) to the game's `SetViewAngles` from RVA 0xB4567C | its first frame cuts from the player's angles (30 down, the hand's) to level and is 10 to 12 degrees off in yaw for that frame only (a flick, as the crystal's); then the hand reaches into the shrine and pulls the rune out: yaw within 11 degrees of the heading, pitch level to 12 down, roll -6.1 to 4.3, at most 4.5 degrees a frame; it ends level on the heading, and the set turns the player's angles to it | on the camera's heading (0.0 off, median), the flick held (`jumped -11.2 deg`, the only hold), largest one-frame turn 2.1, no pitch or roll written; the shrine and the hand in front of the view (XR shots) | the motion played a game frame late (pitch level to 12.1 down, roll -6.1 to 4.3; each written step the camera's own, at most 4.47), the cut and the flick left out (`2 cut(s) left out`), the heading kept (0.00 off after) |
| Mod bot (2026-11-12) | e1m2 in the same campaign load: `give weapon/player/plasma_rifle` (the save holds both mods of its Combat Shotgun and Heavy Cannon: their tabs offer nothing to unlock), `teleport pickups_progress_mod_bot_1_e1m2`, then `setviewpos -92.61 -547.78 -2.47 228.4` (1.2 m in front of the drone, facing it), Use with the hand ahead, 30 down: the game's menu (CHOOSE A WEAPON MOD, a tab a weapon); two presses of the right grip to the Plasma Rifle's tab, the trigger held 3.5 s on Heat Blast unlocks it, the menu closes and the sync starts | `interact/mod_bot/mod_bot_sync` | 2.8 s, view inhibited (0x1F87F) from the sync's first game frame to the game's `SetViewAngles` | its first frame cuts from the player's angles (30 down) to level; then pitch only: down to 14.6 and back to 8 (the drone handing over), at most 0.66 degrees a frame, yaw within 0.04 and roll within 0.3; it ends 8 down, and the set turns the player's angles to it | the view stays on the heading and the head (0.00 off; no pitch written) | the pitch played (down to 14.6), the cut left out (`1 cut(s) left out`), the 8 degrees left at the end eased back to the head, the heading kept |

Not reached, and why:

- **Sentinel Crystal in a `+map` load** (`game_progress_argent_cell_1_1313774949`): from its spot and from in front
  of it (`setviewpos 89.84 61.2 -2.84 180`, `selectDebugEntity` hits it, the housing shows the usable outline), Use
  throws a melee punch and no sync or upgrade menu starts. As on 2026-09-30 in e1m2: in a `+map` load the
  crystal is not usable, whatever the aim. In a campaign load it is (the table's row, 2026-11-12). With the 100%
  save loaded from Mission Select the e1m3 crystal's housing is empty (the save took it): Mission Select gives
  the menus' path, not that save's pickups.
- **Mod bot, before 2026-11-12** (`game_progress_mod_bot_2_e1m3`, `barge_progress_mod_bot_1_e1m1`, and in the
  Exultia campaign load `pickups_progress_mod_bot_1_e1m2`, run clp7): no drone in view and Use starting nothing came
  from the teleport, which puts the player on the drone's origin: its clip model reaches from about 0.55 to 1.55 m
  over that origin and the eye stood 1.29 m over it (`where`), so the eye was inside the drone. Placed 1.2 m in
  front of it, Use opens its menu (the table's row; runs pr3 to pr9). With the Exultia save the menu has nothing
  to unlock until a weapon without its mods is held (pr6, pr7: click, double click, A, the stick click and held
  triggers on the Combat Shotgun's and Heavy Cannon's mods do nothing). The first mod bot of Hell on Earth, next to
  the chainsaw (`barge_progress_mod_bot_1_e1m1`), opens its menu the same way with an UNLOCK on the Combat Shotgun's
  mods, in e1m1 loaded by the console's `map` from the Exultia campaign load (pr10; the load saved into the slot,
  so the session kept its save slot); its sync was not run. Not retried in a `+map` load.
- **Rune, before 2026-11-12** (`game_progress_rune_1_e1m3`, `pickups_progress_rune_1_e1m2`): nothing seen at its spot
  in `+map` loads. It needs no trial: in the Exultia campaign load it opens on touch (the table's row; pr1, pr3). Not
  retried in a `+map` load. The console's `invalid function call for slot::progress/...` warnings (rune, mod bot,
  crystal, token) come at the map load of a campaign load too (pr2, pr6), so they do not tell a `+map` load's
  unusable pickups apart.
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
- Still at a gore nest with the hold (2026-11-12, run ns1: nest 3 by teleport and Use looking down, hand aim, on the
  test PC at about 57 game frames a second, not in a headset; the `pickup-nest` QA scenario). The camera cut -178.4
  degrees of yaw in one frame; the hold started there (`jumped -178.4 deg`, the only hold line) and the view stayed
  on its heading for that view and the next (0.00 moved), then turned to the nest's side in one step (180.00 to
  6.58, two game views and 46 ms by the log's clock after the cut: the swing's first steps went the other way, so
  the hold ran its two views). From there the written yaw was on the camera's on every line of the forced view
  (0.00 off; the swing from 14.7 to -17.3 degrees, at most 4.42 in a frame), with no written step over the camera's
  own (4.42); the game's `SetViewAngles` as the pickup ends set the player's angles to the camera's (-17.24) and the
  view did not move (largest step from there 0.00), facing -17.24 after. No pitch or roll is written (the head's
  0). `pickup-nest-play` passed in the same run on the same build (the heading kept, 0.00 off; largest step 5.43
  against the camera's 5.43; roll down to -14.6, 0.03 off at the end).
- Camera moves: a menu's views keep the heading the player had before it (`PickupHeading::noteBody`), so a pickup
  that starts from a menu (the crystal, the rune and the mod bot) holds it (`the view keeps the body yaw 135.0`);
  and the game's own forced view of its `SetViewAngles`, which for the crystal comes just after the
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

The rune and the mod bot (2026-11-12): `menu-rune`, `menu-rune-play`, `menu-modbot` and `menu-modbot-play` run the
facing checks at the same save's rune and mod bot (the table's rows), and the six facing scenarios (the crystal's, the
rune's and the mod bot's, each with both settings) now also check, with Still view, the view within 1 degree of the
camera's heading (median; it takes the camera's yaw itself) and no pitch or roll written (within 0.5 degrees of the
head's), and with Camera moves the camera's pitch and roll played (a range of 4 degrees or more) with no step of the
view in any angle over 1.25 times the camera's own largest step that is not a cut, or the release's steepest (bounds
that follow the frame rate; at first a fixed 5 degrees, made for about 60 frames a second). On my test PC at about 60
game frames a second, not in a headset (runs pr3 and pr9; the crystal's checks on the logs of cv2):

| Scenario | The view off the camera's heading in the sync | Largest step in one frame | Written pitch and roll | Heading in the menu, after |
|---|---|---|---|---|
| menu-rune (Still) | 0.0 median, 11.2 at most (the flick's frame, held) | 2.07 (yaw) | 0.00 | 179.4, 179.4 |
| menu-rune-play | 0.0 median, 10.1 at most | 4.47 (pitch, the camera's own) | pitch 0 to 12.1, roll -6.1 to 4.3 | 179.4, 179.4 |
| menu-modbot (Still) | 0.0, 0.0 | 0.01 | 0.00 | -131.6, -131.61 |
| menu-modbot-play | 0.0, 0.0 | 0.65 (pitch) | pitch 0 to 14.6 | -131.6, -131.61 |

No fault there with either setting: Still shows the motion's yaw on the heading and holds the rune's first-frame
flick; Camera moves plays the motion a game frame late and leaves the cuts out (the rune's two, the mod bot's one);
the heading is the menu's after the sync.

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
crystal; since measured, section 8); the crystal with the hand turned partly off the head (only 0 and 180 degrees); a
gore nest or automap station
reached the normal way; Still's hold at gore nest 1 or the golden nest (only nest 3, run ns1), or at an automap station; frame rates other than the rig's (about 60 game frames a second); a rune or mod
bot reached the normal way (both by teleport, the mod bot also by `setviewpos`, in one save of Exultia; the mod bot with
a Plasma Rifle given by the console), with the hand pointed away from them, or another rune or mod bot than Exultia's
(only Hell on Earth's first mod bot's menu, not its sync); Praetor token 4 and the chainsaw (not retried).

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
| ns1 | the merged build (crystal-view), Still at gore nest 3 (the new `pickup-nest` scenario) and `pickup-nest-play` | both pass: Still's cut shown whole two game views after the camera's, the view on the camera's heading after (0.00 off), no turn at the end (above) |
| pr1 | Still: Game Slot 2's Continue Game, the rune by teleport, Use at hand aims (the hand behind: 180, -30) | CHOOSE A RUNE, a held trigger unlocks one, `interact/rune/use_sync` 7.6 s, a forced view all along; the head looks away (180 off the camera), as at the crystal with the hand behind |
| pr2 | the same save: the mod bot by teleport with a Plasma Rifle and a Rocket Launcher given; e1m1 loaded by `map` and its first mod bot by teleport | no drone, nothing starts (the eye inside the drone, above) |
| pr3 | `menu-rune`, `menu-rune-play`; the mod bot from 1.2 m in front | both pass (the table's row); the mod bot's menu opens, nothing unlocks |
| pr4 to pr8 | `menu-modbot`, `menu-modbot-play` and the menu's inputs: a held trigger, click, double click, A, the stick click, the left trigger, the tabs, then a Plasma Rifle given | only with the Plasma Rifle given, its tab's held trigger unlocks Heat Blast and the sync starts (pr8) |
| pr9 | `menu-modbot`, `menu-modbot-play` (the Plasma Rifle given, two tabs) | both pass (the table's row) |
| pr10 | e1m1 loaded by `map` from the campaign load, its first mod bot from 1.2 m in front | CHOOSE A WEAPON MOD with UNLOCK on the Combat Shotgun's mods; no mod picked |
| pr11 | the committed scenarios: `menu-crystal`, `menu-crystal-play`, `menu-rune`, `menu-rune-play`, `menu-modbot`, `menu-modbot-play` | all 6 pass; the crystal 0.0 off the camera's heading (median) with both settings, largest one-frame turn 2.8 (Still) and 2.7, written tilt 0.00 (Still), range 15.6 with a largest step of 3.30 (Camera moves); the rune and the mod bot as in the table above (the rune's largest step with Camera moves 4.21) |

## 8. Head aim and gamepad mode (2026-11-12)

Every run in sections 6 and 7 used hand aim. The launcher's Aim setting has two more values players use, `head` and
`view`, and gamepad mode (Motion controllers unticked: `ETERNALVR_CONTROLLERS=0`, the game's own pad) aims with the
head (the launcher turns Aim `hand` into `head` there).

What changes at a pickup with the aim [code]:

- Hand aim: the game's view angles hold the weapon hand's, and hand aim yields to a forced view (nothing is written
  in the token's, a gore nest's, the rune's or the mod bot's pickup). The crystal's sync has no forced view, but its
  camera stayed more than 15 degrees from the hand's angles (30 down) in every view. So under hand aim every view of
  every pickup measured was one the game's camera drives (`drivenCamera`, or the yield's game yaw), which is what
  Still view's "followed" rests on.
- Head aim and gamepad mode: the view angles are the head's, and `controllers::aimAngles` gives the head in every
  view, forced or not. Head aim therefore writes the head's angles in a pickup unless the camera drives the view,
  and whether it does was decided in each view by `drivenCamera`'s 15 degrees between the camera's forward and the
  view angles'.
- View aim: no head aim. The body is the game's yaw, which already holds the camera's: Still shows the camera's yaw
  itself in every view (its cuts and the crystal's flick included: the hold only feeds head aim), Camera moves adds
  only its pitch and roll (`turnOnBody`), and nothing keeps or turns back a heading after (`PickupHeading` needs head
  aim's own body). Not run on the rig then (since run, section 9).
- Still's hold (`CameraHeadingHold`) starts only in a view whose view angles moved under 0.5 degrees: under head aim
  with the head moving they move while head aim writes.
- Camera moves: `PickupView::turn` keeps head aim on the player's own heading in every view, as under hand aim; head
  aim writes the head's angles meanwhile. `PickupHeading` and the turn back after it work the same.
- Gamepad mode's menus (the crystal's, the rune's, the mod bot's): the pad navigates them, and the game keeps its
  cursor hidden, which is how the layer told a menu.

So, before measuring, per pickup with Still view under head aim: the token's camera yaw is the token's own heading
(45): a player not facing it exactly sees the cut to it once, then the view stays (pitch never written); a gore nest's
cut (over 90 degrees) drives every view as under hand aim; the crystal's camera (121 to 160 degrees of yaw about the
player's 130 or so, pitch 0 to 12 up against a level head) and the rune's (yaw within 11 degrees, pitch level to 12
down) cross the 15 degrees, so the view would jump to the camera's heading and back; the mod bot's yaw is the
player's, so crossing the line changes nothing. With Camera moves all as under hand aim. Gamepad mode the same, and
its menus not held.

What the rig showed (my test PC, about 60 to 80 game frames a second, not in a headset; the QA suite's `-head` and
`-pad` scenarios, `tools/rig/qa/README.md`, with the simulator's head sweeping 8 degrees of yaw each way every 3 s
and, at the gore nest, held 60 degrees down; `ETERNALVR_TEST_CAMERA_TRACE=1`, and `=all` for the menus the layer did
not see):

- Still view under head aim, before the change (run pa3): the crystal's camera passed the 15 degrees and the view
  went to its heading in one frame (9.5 to 9.8 degrees, trace 893) and back (8.6, trace 932); the rune's the same,
  18.4 degrees in one frame; the mod bot's stayed within them, head aim wrote the head's angles all through its forced
  view while the game held the player's, and the game's set as the pickup ended turned the view by the head's turn in
  it (13.4 degrees in one frame, the heading 12.8 off after). The view angles never moved in the crystal's sync while
  head aim wrote them (129.88 all along). The token and the gore nest showed nothing wrong: a token pickup started off
  the token's own heading shows the camera's cut to it once (15.3 degrees, held two views by Still's hold), and the
  nest's cut drives every view. [rig, log]
- Camera moves under head aim, before (pa3): the token, the nest, the crystal and the mod bot as under hand aim (the
  heading kept, 0.00 to 0.36 off after); the rune's camera went from the player's heading to its own in two frames
  (4.02, then 5.55 degrees of yaw, the player 9.6 degrees off it), neither step a lone one, so both were played as
  motion: a 5.5-degree step of the view. [rig, log]
- Gamepad mode, before (exploration runs pa1, pa2): the menus work with the pad (A at the title, A on Campaign, the
  D-pad down and A for Game Slot 2, A on Continue Game; Use is the right stick's click; A held 3.5 s unlocks the
  highlighted item, its first; the right shoulder goes one tab right), but the game hides its cursor, the layer saw
  no menu (`0 frame(s) with the panel up`), and the menus show on the head-locked HUD quad. Head aim went on under
  them while the game held its view; each push of the look stick turned the world behind the menu (the rune's: from
  -126.0 to -65.8 while the game's view stayed at -126.0), and the view came back by that much in one frame as the
  sync began (60.2 degrees at the rune, 152.9 at the crystal, 175.3 at the mod bot). [rig, log]

What changed (sections 6 and 7's decisions kept):

- Still view: every game view of a pickup is one its camera drives (`xr_math::cameraDrivesView`,
  `PickupView::followsCamera`), whatever its distance from the player's view angles: under head aim and in gamepad
  mode as under hand aim, the view follows the camera's heading from where the head was when the pickup began, head
  aim writes nothing in it, and the game's set at its end turns no view. [code; unit test from the pa3 trace]
- Camera moves: the pickup's first two steps are one cut, left out, when the step after them is under a quarter of
  their sum and either each is at least a quarter of the other, neither is huge and together they pass 5 degrees
  (the rune's 4.0 then 5.6), or the first passes 5 degrees and the second is huge (below, pa5); the first step then
  waits a frame more. Later in a pickup two such steps stay motion (a gore nest's heart pull). [code; unit tests from
  the pa3 and pa5 traces]
- Gamepad mode: a menu over the game without a cursor is the GUI's full-screen backdrop over a head-tracked frame
  (`menu::padMenuOverGame`, logged `menu: a menu over the game without a cursor ...`): head aim holds the body under
  it as under any menu, and the pickups and the trace take it for a menu. The mod bot's menu shows the weapon's model
  in its middle, so the backdrop test does not see it: under it the look stick still turns the world in gamepad mode
  (not fixed; the prompts' tab-list hook that could tell it is not installed with the controllers off). [code, rig]

After the change (runs pa4 to pa6 on my test PC; the checks of the written view less the head's angles, `-head` and
`-pad` as in `tools/rig/qa/README.md`; pa5 and pa6 also push the pad's look stick for 0.4 s under a menu):

| Scenario | Under head aim (`-head`) | In gamepad mode (`-pad`) |
|---|---|---|
| token, Still | the camera's cut to the token's heading shown once (8.3 degrees), then the heading kept (0.02) | the cut shown once (6.0), then kept (0.01) |
| token, Camera moves | the heading kept (within 1.3, 0.01 off at the end): a 1.3-degree tail of the start's cut played | kept (within 0.6, 0.00 off at the end) |
| gore nest 3, Still | the cut (170.4) shown two views after it in one step, then on the camera's heading (0.01) to the game's set, no turn after it | the same (174.4, 0.01) |
| gore nest 3, Camera moves | the cut left out, the heading kept (0.00 off), the roll down to -14.6, largest step 5.81 (the camera's) | the same (0.01, 5.73) |
| crystal, Still | largest one-frame turn 2.4 (the camera's swing), the view 6.0 from the camera's heading (the head's turn since the sync began) on every line, no pitch or roll written, the heading after the sync's | the player 36 degrees off the crystal's heading (the pad turns in steps): its cut shown once (36.3, one way), then followed (0.0 spread); the look stick pushed under the menu turned nothing (0.01) |
| crystal, Camera moves | pa5: the start's jump (15.7 degrees of yaw off the player's heading, then 21.1 back) played as motion, a 15.7-degree step; pa6, below | heading kept, the motion played 41.4 degrees from the camera's heading (the player's offset), largest step 2.1; the push under the menu turned nothing |
| rune, Still | the cut to its heading shown once as the menu closed (6.7, one way), then followed (0.0 spread) | the player 58 degrees off: the cut shown once (57.6), then followed; the push turned nothing |
| rune, Camera moves | the start's two-frame cut left out (largest step 2.0, pitch and roll range 12.1) | pa5: the start's jump (17.4, then 70.8 back) played, a 17.4-degree step; pa6, below |
| mod bot, Still | the cut to its heading (10.3) shown once after the hold's two views, then followed | the menu not seen (below): the push turned the world 57.8 degrees under it, and the view came back 58.5 in one frame as the sync began |
| mod bot, Camera moves | heading kept (0.00), largest step 0.61 | the same push's 53 degrees stayed after the pickup (the heading Camera moves kept was the pushed one) |

The Camera moves rows marked pa5 showed the pickup's opening a second time: a first step over 5 degrees (the camera's
first frame off the player's heading) into a huge one (its cut back to its own heading), the first played as motion
because the huge one after it was not under a quarter of it. Under hand aim the first frame's pitch cut (30 down to
level) was itself huge, so it never showed. The opening rule now also takes a first step over 5 degrees followed by a
huge one for one cut (`CameraTurnTiming::openingPair`, unit test from the pa5 trace). menu-crystal-play-head failed in
pa4 for the QA's pointer, not the view: the trigger was held 71 pixels left of the upgrade (the cursor at 478, 312 for
549, 313), the menu stayed up and no sync ran (it passed in pa3 and ran in pa5).

Open then (both since closed, section 9): in gamepad mode the mod bot's menu is not seen (its middle shows the weapon's model, so the backdrop test finds
none; the prompts' tab-list hook that would see it is not installed with the controllers off): the look stick still
turns the world behind it, and the view comes back in one frame as its sync begins (Still) or keeps the pushed heading
(Camera moves). Not checked: a headset; view aim (`ETERNALVR_AIM=view`, from the code only, above); frame rates other
than the rig's (about 60 to 85 game frames a second); gamepad mode with a keyboard and mouse; other menus with a pad
(the pause menu, the Dossier: the backdrop rule covers any menu the GUI's backdrop shows, measured at the crystal's
and the rune's); the automap station, gore nest 1 and the golden nest under head aim or with a pad; a fast head turn
during a pickup (the sweep is 8 degrees each way every 3 s, at most about 17 degrees a second).

Review round (2026-11-12, runs pa7 to pa9):

- Gamepad mode's menu without a cursor counts once its backdrop has held 0.5 s, not within 5 s of a map load, never
  in a cutscene, and for 300 s at most (`menu::PadMenuWatch`, logged as it comes, goes and is capped); a reconnect
  clears it. In pa7 (a `kill` and the checkpoint's reload, gamepad mode) the death's fade was not taken for a menu,
  but the fade in from black after each map load was (1.4 to 1.7 s over head-tracked frames), hence the 5 s after a
  load. [rig, log; unit tests] In pa9 (this build) neither the death nor the reload's fade was taken for one, and the
  crystal's and the rune's
  menus with a pad still held the view (the look stick pushed under them).
- Under a menu over a pickup shown with Still view (the rune's first views, the crystal's with a pad) the camera
  drives the view too, and the menu's log line tells it by the same rule (`menuBodyYaw`'s `pickupFollow`). [code;
  unit test] In pa8 the hand-aim menu scenarios passed as before, and under head aim the mod bot's pickup ended
  on the heading it had at the sync's end (0.00 off; the 13.4-degree turn as it ended is gone: its largest one-frame
  turn, 12.1, is the cut to its heading at the start, the player 11.7 degrees off it).
- The mod bot's menu with a pad: no cheap signal found in the time box (its backdrop came 12 s after the menu opened
  in pa5, after the tab presses; the prompts' hooks are not installed with the controllers off). It stayed open then;
  section 9 has the signal found since (the menu's 3D model).
- QA bounds that changed from the hand-aim runs of section 7: Still view's one-frame check allows one cut at the
  sync's start (one step over 5 degrees there, 25 degrees at most, not capped with a pad), and its heading after the
  sync is checked
  against the one at the sync's end and, under hand aim, the menu's too; the Camera moves step bound is 1.25 times
  the camera's own step or the release's steepest (was 5 degrees); under head aim and with a pad the view's gap from
  the camera is checked for its spread after the opening and, with Still view, 10 degrees at most; the token's heading
  within 1 degree (Still, after its start's cut) or 2 (Camera moves); gore nest Still's heading up to the game's set
  rather than the forced view's last line, and its hold not required with the head moving. Known flake: under the
  head's sweep the QA's pointer once held the trigger 71 pixels off the crystal's upgrade (pa4).

| Run | What | Result |
|---|---|---|
| pa1, pa2 | gamepad mode by the pad (exploration): the menus to Game Slot 2's Continue Game, the crystal, the rune and the mod bot (`ETERNALVR_TEST_CAMERA_TRACE=all` in pa2) | the menus and the pickups work by the pad; no cursor and no menu for the layer; the look stick turned the world behind the menus and the view came back in one frame as the syncs began (152.9, 60.2, 175.3) |
| pa3 | the ten `-head` scenarios on the build before | Still: the crystal 9.8 and 8.6, the rune 18.4, the mod bot 13.4 degrees in one frame; Camera moves: the rune's start 5.5; the token and the nest as under hand aim |
| pa4 | the twenty `-head` and `-pad` scenarios on this change | the table above; the pad's mod bot not reached (the QA's search), menu-crystal-play-head's pointer off the upgrade |
| pa5 | the ten hand-aim scenarios (`pickup-camera`, `pickup-camera-play`, `pickup-nest`, `pickup-nest-play`, the six facing menu ones), the six `-pad` menu ones with the look stick pushed under the menu, menu-crystal-play-head | the ten hand-aim ones pass; the pad's crystal and rune menus hold the view (0.01 degrees); the opening's jump into a huge cut played (crystal-play-head 15.7, rune-play-pad 17.4); the pad's mod bot as above |
| pa6 | the opening rule taken further: the five hand-aim Camera moves scenarios, menu-crystal-play, menu-rune-play and menu-modbot-play with `-head`, menu-crystal-play-pad, menu-rune-play-pad | all 10 pass: the crystal's and the rune's opening jump left out under head aim and with the pad (largest steps 3.7, 4.2, 2.3 and 4.9, each the camera's own motion; the pad's player 45.3 and 54.5 degrees off the pickup's heading); the hand-aim ones as before (menu-crystal-play range 15.5, largest step 3.14; menu-rune-play 4.13; menu-modbot-play 0.65; pickup-camera-play and pickup-nest-play pass) |
| pa7 | gamepad mode: a `kill` and the checkpoint's reload | the death's fade not a menu; each map load's fade in was (1.4 to 1.7 s), hence the wait after a load |
| pa8 | the review round's build: the 18 menu-started scenarios (hand, `-head`, `-pad`), pickup-nest-head, pickup-camera-pad | 18 of 20: menu-modbot-pad (open, above) and menu-rune-head, whose start's cut came with the camera's own 0.7-degree step the other way (the QA's one-way rule too strict; now one step over 5 degrees at most) |
| pa9, pa10 | the load rule: pad-x-death, menu-crystal-pad, menu-rune-pad; the final QA rule: menu-rune-head, menu-crystal-head, menu-crystal | all pass: no menu without a cursor at the death's or the reloads' fades (pa9); the pad's crystal and rune menus held the view under the push; menu-rune-head's start followed the camera's own small steps (at most 4.4 degrees in one frame, out and back within the opening: under the hold's 5), one step over 5 at most |

## 9. View aim, and the mod bot's menu with a gamepad (2026-11-12)

View aim (the launcher's Aim with: Mouse, `aim = view`, `ETERNALVR_AIM=view`) keeps the game's own aim: nothing writes the player's view
angles, and the view is built on the game's yaw with the head on top (`presenter_head.cpp`). The QA suite's `-view`
variants (`tools/rig/qa/README.md`) ran the ten pickup scenarios of section 8 under it, with the motion controllers on
and the simulator's head sweeping 8 degrees each way every 3 s (my test PC, about 70 to 80 game frames a second, not in
a headset).

Two things the rig needed first [code, rig]:

- `ETERNALVR_DEBUG_COMMANDS` started its clock when head aim's check passed, so under view aim no step ran: no
  teleport, no menu, every scenario failed at its start (run pv1). Under view aim it now starts at the first game view
  of play (no cutscene, forced view or menu, a play map).
- The forced view's log line and the trace's `angles (...)` came from head aim only (`angles (none)` under view aim,
  so the token's and the set's checks found nothing). The view-aim body now hands the player's view angles to them
  as head aim does (`camera_anim::noteForcedView`, from the angles `PickupView` reads anyway).

What view aim did before the change (run pv2) [rig, log]:

- The Sentinel Crystal's flick was shown with either setting: the view turned -10.5 degrees and back in one game frame
  each as the sync began with Still view (trace 306), -11.0 with Camera moves; the rune's 9.8 with Camera moves (its
  Still run stopped at the QA's pointer, which held the trigger off the rune: the known flake). Still view's hold
  started (`jumped -10.5 deg`), but it fed only head aim's follow. Two steps over 5 degrees, out and back: a fault by
  section 7's rules.
- Gore nest 3: the cut (-178.5 degrees) shown in the game view it came, in one step, then the camera's heading on
  every line (0.01 off), with both settings; Camera moves played the roll (down to -14.6, 0.03 off the camera's at
  the end). With Camera moves the view ended 162.8 degrees from its start, facing where the camera ended, and no
  heading was held (`pickup: camera turn 1 starts ...; no heading held (not head aim)`).
- The mod bot and the token: nothing over 5 degrees; the token's pitch checks could not read the trace then (above).

What that leaves to view aim itself [code]: with nothing writing the player's angles, the view faces where the game's
camera faces after a pickup, as the flat game does, and a cut of the camera's heading is shown once as it comes (as
Still view shows it under every aim). Camera moves could keep the player's heading only by writing the aim back after
the pickup (head aim's turn back, section 7), which view aim never does; holding the heading through the pickup would
only move the cut to its end. So under view aim Camera moves plays the camera's pitch and roll on the game's yaw
(`xr_math::turnOnBody`, as before), and its yaw is Still view's.

The change [code; unit test from the pv2 trace]: the body built on the game's yaw (view aim, or head aim not on yet)
leaves out a flick of a pickup camera's heading with either setting (`PickupView::withoutFlick`: the hold of section 7,
`xr_math::CameraHeadingHold`, now kept with Camera moves too; head aim's follow still uses it only with Still view). A
cut that stays is shown two game views late, once, as under hand aim. The unit test feeds the crystal's first sync
views from run pv2 (the camera at 135, then -9.66, 124.49 for one view, then 135 again; the player's angles at 135) at
30, 60, 90 and 144 frames a second: one hold, no step of the body over 0.01 degrees.

After the change (run pv4, the ten `-view` scenarios, all pass):

| Scenario | Still view (`-view`) | Camera moves (`-play-view`) |
|---|---|---|
| crystal | largest one-frame turn 2.7 (the camera's swing); the flick held (11.1 off the camera on its frame, 0.0 median); no pitch or roll written; heading after the sync's (0.01 off) | largest turn 2.7; pitch and roll played (range 15.2, largest step 3.25 against the camera's 3.26); heading after the sync's (0.00) |
| rune | largest turn 1.9; the flick held (11.5 on its frame); heading after the sync's | largest turn 2.0; range 12.1, largest step 4.40 (the camera's own) |
| mod bot | no turn (0.0) | range 14.6, largest step 0.66 |
| token | written pitch 0.0; heading kept (0.01) | written pitch -3.5 to 5.8, largest step 0.65, back on the head; heading kept (0.01) |
| gore nest 3 | the cut (-178.5) shown two game views after it, in one step; then the camera's heading (0.01); the game's set at the end turns no view (0.01) | the same cut two views late; roll down to -14.6, 0.03 off at the end; no other step over 1.25 times the camera's own (5.80 against 7.25); the view ends facing where the camera ended (162.8 from its start) |

The mod bot's menu with a gamepad [code, rig]: the weapon mod screen's weapon is an `idSWFWidget_Model` whose tick
calls `idMenuWidget_3D_Stand::UpdatePosition` (`menu_model_hook.hpp`), and the layer's two hooks there are installed
with the controllers off too (with the menu pointer's hooks, which gamepad mode keeps). The first hook now notes when it
ran, panel or not (`menu_model::modelShown`: within the last 0.25 s), and in gamepad mode a menu's 3D model over a
head-tracked frame counts as a menu over the game as the backdrop does (`menu::padMenuOverGame`, through the same
`PadMenuWatch`: 0.5 s, not within 5 s of a map load, 300 s at most). Logged as it comes and goes: `menu: a menu's 3D
model is placed every tick (a weapon mod or customize screen)` / `menu: no menu's 3D model placed any more`.

- On the rig (runs pv2, pv4: `menu-modbot-pad`): the model line came as Use opened CHOOSE A WEAPON MOD and the pad's
  menu line 0.5 s later (62.9 and 63.4 s in pv4); both went in the same millisecond as the sync's line (73.109 s).
  The look stick pushed under the menu turned nothing (0.01 degrees over 804 menu lines, the push applied in
  `pad.log`); the sync's start showed the camera's cut to the drone's heading once (6.2 degrees, the player 5.8 off
  it). The scenario now checks the menu line and the push like the crystal's and the rune's (its two-presses path is
  gone) and passes.
- No model line in the other runs of pv4 (the main menu, play, the token, a gore nest, the crystal and the rune with
  the controllers on), and `pad-x-death` (a death and the checkpoint's reload in gamepad mode) still sees no menu
  without a cursor (pv4).
- The Dossier's customize screen shows a 3D model too: with a pad it is now taken for a menu over the game, which it
  is (not run).

Regression runs on this change's build (pv5): the ten hand-aim scenarios, menu-crystal-head, menu-rune-play-head,
pickup-nest-head, menu-crystal-pad, menu-rune-play-pad, menu-modbot-play-pad and pickup-camera-pad. 14 of 17 pass with
the numbers of sections 7 and 8 [rig, log]. The other three:

- menu-crystal-play (hand aim): `pickup: camera turn 1 starts ...; no heading held (not head aim)`, though nothing
  turned over 2.8 degrees in one frame and the heading after was the menu's (0.01 off). Likely cause [code; not
  proven on the rig]: the pickup's step reads the menu flag early in the game view and head aim reads it again later
  in the same view (the XR worker sets it); a menu that came up between the two made that view's body head aim's
  menu hold while the heading's record took it for a body that was not the player's own outside a menu, which drops
  the heading from before. Not seen in the 25 Camera moves menu runs before (pa3 to pa8). Now head aim's own menu
  view counts too (`PickupHeading::noteBody`'s `menu`; unit test; logged with Camera moves when it is what kept the
  heading: `pickup: a menu came up within a game view ...`). In pv6 (run twice on the same build) menu-crystal-play,
  menu-crystal-play-head, menu-crystal-play-pad, menu-rune-play and menu-rune-play-head all held the heading (`the
  view keeps the body yaw`) and passed, ten of ten; the race did not come up in them, so the fix itself is checked by
  the unit test only.
- menu-rune-play-head: the QA's pointer held the trigger off the rune and the menu stayed up (the known flake).
- menu-crystal-pad: its opening showed two steps over 5 degrees, -6.9 as the camera took the view under the pad's
  menu and -35.7 at the camera's cut to its own heading (the player 42.6 off it). Pre-existing: run pa9 shows the same
  two steps (-8.2 and -37.1) and passed the one-way rule of then; the one-step rule came with pa10, which did not run
  this scenario. In the trace [log] the menu held the body at 178.22 while the game's yaw stood at 170.71 and the aim
  head aim last wrote was -0.57, so the body the camera's heading gives (170.71 less -0.57) is 6.9 off the held one.
  Likely [code, not proven]: the half second before a pad's menu counts (`PadMenuWatch`'s settle), when head aim
  still wrote while the game held its view. pv6 again: 8.2 degrees the other way, then -35.1; and -5.4, then -38.9. Not fixed here: it is
  head aim's menu hold, outside this change; with a pad, a player who stands off a crystal or a rune and moves the head
  as its menu comes up can see that small turn just before the cut to the pickup's heading.

  Cause and fix (2026-11-18, the `takeover-left` branch): with a pad, Use (the right stick's click) at a crystal or a
  rune makes the game put back a delta head aim had written some frames before, frame after frame until the menu
  is up: `aim: rewrite 2 ... (a value head aim wrote)` and `rewrite 3` at the press in every `-pad` crystal and rune
  run on the rig (pa4, pa5, pa6, pa8, pa9), never at the motion controllers' Use. Head aim's own step keeps the body
  there (the delta's head yaw is the one that value carried), but the view angles never take its writes, so the
  head yaw in them is the put-back value's, not the one injected last. Under the menu the crystal's camera starts
  from those angles, and the body it gives (`xr_math::menuBodyYaw`, and the camera branch after the menu) took off
  the head yaw injected last: in pa9 the put-back value carried -6.38 degrees, the last write 1.87, so the camera at
  the player's own 172.13 gave a body of 170.26 instead of the held 178.51, the -8.2 step [log]. A camera now takes
  off the head yaw the view angles held (`xr_math::viewHeadYaw`, `cameraBodyYaw`), and a value of the game's own
  that replaces a put-back delta (the sync's end) is taken with it too (`headAimStep`), so the body stays on through
  the sync's end; unit tests `menu_camera_tests.cpp` from pa9's numbers and `forced_view_tests.cpp`. This differs
  from before only when the game was holding a delta at the last frame head aim aimed (head aim's writes not
  reaching the view: a value head aim wrote earlier, as at the pad's Use, or one of the game's own, as for some
  frames after a cutscene): a camera or menu that starts then, and a new value of the game's own, take the head yaw
  that held delta carries instead of the one injected last, by which the body would have jumped. With no delta
  held, as at the motion controllers' Use, the values are the ones of before. Rig, tl-x4 and the runs below: `as the menu handed over to the sync the
  view turned 0.0 deg in one frame (body yaw 166.8 from 166.8)`, then the one cut to the crystal's heading (28.7),
  the heading after the sync the one at its end (0.01 off) [rig].

Not checked: a headset; a mouse turning the view during a pickup under view aim; view aim with a gamepad (Aim with: Mouse, Motion controllers unticked), at the automap station,
gore nest 1 or the golden nest; frame rates other than the rig's (about 70 to 80 game frames a second here). The
Dossier's screens, the Fortress and combat with a pad: below.

| Run | What | Result |
|---|---|---|
| pv1 | the ten `-view` scenarios on the build before this change | none reached its pickup: the debug schedule never started under view aim |
| pv2 | the debug schedule under view aim and the mod bot's model signal; `menu-modbot-pad` and the ten `-view` scenarios | `menu-modbot-pad` passes; under view aim the crystal's and the rune's flicks shown (10.5, 11.0, 9.8), the nest's cut shown at once, Camera moves' heading not kept (as above); menu-rune-view stopped at the QA's pointer |
| pv3 | the same with `pad-x-death` | refused at its start: `pad-x-death` is an exploration scenario outside the repository's suite (run through the audit file in pv4) |
| pv4 | this change (the trace's angles under view aim too): `menu-modbot-pad`, `pad-x-death`, the ten `-view` scenarios | all 12 pass (the table above) |
| pv5 | regression: the ten hand-aim scenarios, menu-crystal-head, menu-rune-play-head, pickup-nest-head, menu-crystal-pad, menu-rune-play-pad, menu-modbot-play-pad, pickup-camera-pad | 14 of 17 pass; menu-crystal-play `no heading held`, menu-rune-play-head the pointer's flake, menu-crystal-pad's opening (above) |
| pv6 (twice) | the menu's race fix: menu-crystal-play, menu-crystal-play-head, menu-crystal-play-pad, menu-rune-play, menu-rune-play-head, menu-crystal-pad, menu-crystal-play-view | 6 of 7 pass both times (the heading held in all ten Camera moves runs with head aim's body); menu-crystal-pad's opening as in pv5 |

### Gamepad mode: which screens place a 3D model, and whether a hold outlives its screen (2026-11-13)

With a menu's 3D model taken for a menu over the game in gamepad mode, any other screen or moment that places one
would hold the body too. The pass over the screens and the play a pad reaches (`qa-scenarios-padmodel.ps1`, runs x1 to
x4; gamepad mode, `ETERNALVR_AIM` unset, `ETERNALVR_TEST_CAMERA_TRACE=all`, the simulator's head still), measured with
probes of the look stick (pushed 0.4 s: about 102 degrees a second of push, 41 to 45 degrees a probe in play) [rig,
log]:

- The Dossier and the pause menu (pad-model-screens, Game Slot 1's Continue Game: the Fortress with the 100% save).
  The model line came under the Arsenal (each weapon's model, more weapons by the right trigger), CUSTOMIZE WEAPON
  (the skins, with the model) and a weapon's upgrades page; on the way to SUIT, RUNES and CODEX the right shoulder
  passes the Arsenal, whose model line came and went within 1.2 s while the menu line stayed (the backdrop). With a
  pad all of those hold the body (0.00 degrees under a push), as a menu should; the pause menu, SUIT and RUNES by
  their backdrop. Every hold ended with its screen: the menu line went 0.14 to 0.30 s after the B
  that closed the Dossier (the extra 1.3 to 1.9 s on two pages is the first B leaving a page for the Arsenal, still a
  menu), the model line with it, no trace line with the menu flag after, and every probe after a close turned the
  view within 0.5 degrees of what the play probes' rate gives for that push (the push itself lasts 0.40 to 0.49 s
  through the harness, so the probes are taken per second of push).
- Not held, as before the model's rule [code: `menu::padMenuOverGame` only added the model to the backdrop]: the
  Dossier's map page (the 3D automap: no model, and the backdrop test does not see it) and the CODEX page (the menu
  line went as the page came up): a push of the look stick turned the world behind them (43 and 45 degrees), as it
  does in play. A gap of the backdrop test with a pad, outside this change (closed since: below).
- The Fortress of Doom (pad-model-fortress): about 50 s of walking and turning, teleported past the mission select
  dais, the ARC console and the master level panel, nothing opened: no model line, no menu line, no trace line with
  the menu flag out of 3,827, every probe turned the view.
- Combat (pad-model-combat, e1m1's barge with three weapons given): the right shoulder's tap switched between the
  Rocket Launcher and the Combat Shotgun four times (the bHaptics fire line names each), the weapon wheel came up four
  times (`forced view starts (weapon wheel; inhibit 0x18)`): no model line, no menu line, every probe after a switch
  or the wheel turned the view (23 to 45 degrees: the probe right after the wheel's last pick turned least).
- Not checked: the main menu's Customize (the Slayer's model; not over the game, and a map load ends any hold), a
  CODEX entry with a demon's model, the mod bot's own screen again (no code changed: menu-modbot-pad and pad-x-death
  were not rerun), a headset.

| Run | What | Result |
|---|---|---|
| x1 | the three new scenarios, first cut | fortress and combat pass; screens fails on its own steps: one B only left a page, so the Dossier stayed up (the game window shows no GUI: shots from the headset's view since) |
| x2, x3 | screens with B again while the menu line is up, then SUIT, RUNES and CODEX | x2 passes; x3 fails one probe after CUSTOMIZE WEAPON at 51.7 degrees: its push lasted 0.488 s (pad.log), so probes are measured per second of push since |
| x4 | the three, final | all pass (the numbers above) |

### Gamepad mode: the Dossier's map and CODEX pages (2026-11-13)

Why those two were missed [log, code]: with a pad the layer tells a menu without a cursor by the GUI's backdrop
round the crosshair or by a menu's 3D model (`menu::padMenuOverGame`). The map page draws the 3D automap as the
game's frame and the CODEX page a black frame, each with only the tab bar, the lists and the hints on the GUI: the
backdrop test finds no backdrop round the crosshair there, and neither places a model. In x4 the menu line came on the
way to CODEX (the right shoulder passes the Arsenal, whose backdrop and model count) and went 0.5 s after the CODEX
page came up. The trace stayed head-tracked under both pages, the game's own view angles held still, and head aim went
on writing the body plus the pushed look stick.

What the player saw before [rig, log; runs x4 and d1, my test PC, the simulator's head still]: the push under the map
page turned the written view 43.4 (x4) and 44.0 (d1) degrees, under CODEX 44.7 and 43.1. On the map page that shows:
the automap slid off to the side (the d1 shots `dossier-map-up` and `dossier-map-pushed`), on top of the game's own
rotation of the map by the same stick. CODEX's frame is black, so nothing shows there. As the Dossier closed the view
came back by the whole turn in one frame (43.4 and 44.7 in x4, 44.0 and 43.1 in d1), the game's held angles taking
over again: a snap back, not a kept turn. No other one-frame step over 5 degrees in either run but the map's start.

The change [code; unit tests]: the Dossier's open and close, its vtable's slots 6 (RVA 0xF38450, the HUD event 0x21)
and 7 (0xF38560, event 0x22; menus.md section 8), each with a read-only mid hook at its first instruction next to
ShowScreen's (`dossier_screen_hook.hpp`; the close installed first, the open only with it; the open's hook takes the
map loads then, published with its count). `menu::dossierOpen` makes it a state (open from an open call until a close
call or a map load after that open) and `menu::padDossierOverGame` a menu over the game in gamepad mode, without a
cursor's menu, over a head-tracked frame. `PadMenuWatch` takes it as known: up in the first frame that sees the open
(no half-second settle, map load or not: no fade looks like it), down in the frame that sees the close unless the
backdrop test still sees the backdrop, and not capped (review round below). Logged: `menu:
the Dossier opens (its open call N); gamepad mode: ...`, `menu: the Dossier is closed`, and the pad's menu line names
the Dossier. With the motion controllers on the Dossier shows the cursor, which already holds the body; nothing
changes there.

After the change (run d2, this branch's Debug build) [rig, log]:

- pad-model-screens, extended (every screen's probe must turn nothing, the close must turn no frame over 1 degree, one
  Dossier open and close per Dossier screen): passes. Under each of the eight screens the push turned 0.00 degrees
  over 148 to 175 trace lines, all with the menu flag (the map page 153, CODEX 170); the largest one-frame turn from
  the close's first press to 1 s after the last was 0.00 on every screen; the hold came on in the same log
  millisecond as the open (`the Dossier opens` and the menu line) and went with the close on the map and CODEX pages
  (on the others with the backdrop 0.13 to 0.25 s later, as before). Seven open and seven close lines for the seven
  Dossier screens, open call 7 the last: the open hook ran for nothing else, the map loads included. Each probe after
  a close turned the view within a tenth of the play rate (41.5 to 44.2 degrees). The map page's push now only
  rotates the automap in place (the game's own rotation; `dossier-map-pushed`).
- pad-model-fortress, pad-model-combat: pass, no Dossier open line, no menu line, no menu flag on the trace; every
  probe turned the view. menu-modbot-pad passes (the mod bot's menu held by its model as before). pad-x-death (run
  d3: a death and the checkpoint's reload, three map loads): passes, no menu without a cursor and no Dossier line.
- The d1 run is the same extended scenario on the build before (pad-model-menus' layer): it fails the three new
  checks on the map and CODEX pages only (the numbers above) and logs no Dossier line, so the checks see the fault.

Not checked: a headset; the CHALLENGES page (the scenario does not open it; the open Dossier holds whatever its page);
a CODEX entry's 3D model view; the Dossier opened other than by the pad's View button; gamepad mode with a mouse.

Review round (2026-11-13, runs e1 to e4):

- No cap for the Dossier: `PadMenuWatch`'s 300 s cap is for a backdrop or a model that never goes; under it a player
  reading CODEX for more than five minutes got the body turning again and the snap back as the Dossier closed. With
  the Dossier's own close hooked, a missed close (not seen on the rig) ends at the next map load instead; the cap
  counts only the time held without the Dossier (a backdrop's tail after it), and stays for the backdrop and model
  rule. [code; unit tests]
- The map loads at an open are taken in the open's own hook and published with the open count, so a map load between
  the open call and the frame that reads it cannot keep a stale open. [code; unit test]
- Every way a pad closes the Dossier (pad-dossier-close, run e2; e1 found the paths) [rig, log]: B, 0.03 to 0.06 s from
  the B to `the Dossier is closed` and the hold's end in each step; Start inside the Dossier does nothing with a pad
  (the Dossier stays open and held: 0.00 degrees under a push after it), so the pause menu comes only after B: B then
  the pause menu's Resume, B then Load Checkpoint (its NOTICE opens on Cancel: Left, A; the map load 7.2 s after the
  close), B then Exit to Main Menu (the load 12.0 s after it). Each closed by its own close call, never by a map load
  (`(a map load ended it)` absent); a probe under each turned 0.00 degrees, and after each step back in play the view
  turned as in play. Five open lines for the five steps over four map loads (the main menu's, the Fortress twice, the
  main menu again): the open hook never ran at HUD set-up, at a map load or in play (also none in pad-model-fortress,
  pad-model-combat and pad-x-death).
- Not checked: Esc from a keyboard (tools/rig/keys.ps1 cannot bring the game window to the front from the QA's
  unelevated task: `could not focus`, run e1); with a pad the Dossier's hint names it as its back, as B is.

| Run | What | Result |
|---|---|---|
| e1 | pad-dossier-close, first cut | fails on its own steps: Start inside the Dossier closes nothing, the NOTICE's A cancels, Esc not sent; every close that came was logged at once |
| e2 | pad-dossier-close, final | passes (above) |
| e3 | pad-model-screens, pad-model-fortress, pad-model-combat, menu-modbot-pad | all 4 pass, as in d2 (no turn under or at the close of any screen, seven Dossier opens and closes) |
| e4 | pad-x-death (the audit file) | passes: no menu without a cursor and no Dossier line |

| Run | What | Result |
|---|---|---|
| d1 | pad-model-screens, extended, on the build before | fails as expected: the map and CODEX pages turned 44.0 and 43.1 degrees under the push and snapped back as much in one frame at the close; no Dossier line |
| d2 | this change: pad-model-screens (extended), pad-model-fortress, pad-model-combat, menu-modbot-pad | all 4 pass (above) |
| d3 | pad-x-death (the audit file) | passes |
