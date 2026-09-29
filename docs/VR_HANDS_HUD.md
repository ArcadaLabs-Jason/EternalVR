# The wrist HUD, the weapon HUD and the free off hand (M6, T-077)

Builds on the UI layer (`docs/rig-findings/ui-layer.md`), the render size (`docs/rig-findings/render-size.md`)
and the motion controllers (`docs/VR_CONTROLLERS.md`). The owner's request (D-035): a HUD read by looking
at the bottom of the left wrist, with the head-locked HUD kept as an option; and both hands
moving freely.

**Status.** Built and unit-tested; **not yet run in the game or a headset**. All are opt-in until the live
checks below pass: the head-locked panel stays the default HUD and the wrist or the weapon is one setting
away (`ETERNALVR_HUD=wrist` or `weapon`, or the launcher's Advanced tab); the free off hand needs
`ETERNALVR_OFFHAND=free`.

## The wrist HUD

```
XR worker, every XR frame (presenter_wrist.cpp)
  UI quad filled as before: the 16:9 band of the GUI target (wideContentRect), head-locked
  wrist mode, no menu panel up (nor held), controllers attached?
    head-locked pieces: the band minus the two bottom corners (health block, weapon block)
    facing test: the off hand's grip pose and the head (controller snapshot, room space)
      facing angle <= 40 deg and gaze angle <= 40 deg -> shown; > 55 deg either -> hidden
    shown (or fading): three quads in the off hand's grip space, cropped from the same UI image
      vitals | weapon side by side along the forearm, the ability rings above them
```

- **What moves to the wrist.** The HUD's bottom-left block (health, armor, extra lives), its bottom-right
  block (ammo, equipment, flame belch) and the ability rings around the crosshair (cooldowns;
  `ETERNALVR_WRIST_ABILITIES=0` leaves them out). Under hand aim the UI copy clears the centre of the GUI
  target (the game's crosshair marks the head's ray; `presenter_ui.cpp`), which takes the ability rings with
  it, so there is no abilities quad then (logged once); in practice the rings show on the wrist only under
  head aim. Everything else on the GUI target stays on the
  head-locked quad: subtitles, prompts, pop-ups, boss and encounter bars, markers, the damage direction.
  The corners are always cut from the head-locked quad in wrist mode, so they show only on the wrist.
- **Where the crops come from** (`ui_layer/hud_regions.hpp`). The blocks are fixed rectangles in the HUD's
  16:9 band, measured on the rig's GUI captures (2560x2100 and 2054x2068; the same band coordinates to 0.005
  in both) with a margin for animations and key hints. The band is `ui_layer::wideContentRect`, the same
  rows the UI quad shows since the render size made the eye image near-square (1280x1400, 2064x2100), so a
  crop never reaches the empty rows above or below it. Unit tests check every block and cut lies in the band
  at 1280x1400, 2064x2100, 2560x2100, 2054x2068 and 1920x1080.
- **Where the panel is** (`ui_layer/wrist_hud.hpp`). In the off hand's OpenXR grip space: on the inside of
  the forearm (the palm side), 13 cm behind the grip toward the elbow and 4 cm out from the palm, the
  vitals + weapon row 16 cm wide. The panel's normal is the palm's; the image's up is the thumb side. Turning
  the palm up with the forearm across the chest, the look at a watch worn on the inside of the wrist, puts
  it in front of the eyes, upright. The quads are submitted in the grip space itself, so the runtime places
  them at display time and a recenter does not move them off the hand: the room transform (recenter, body
  follow; `InputFrame::roomFromLocal`) is applied to poses the layer locates, not to layers in an action
  space. The facing test takes the grip and the head from the same controller snapshot, both in room space,
  and uses only angles between them, so it does not depend on the transform either.
- **When it shows** (`WristFacing`). Two angles, both with 15 degrees of hysteresis: the facing angle
  (between the panel's normal and the direction to the head) and the gaze angle (between the head's
  forward and the direction to the panel). Shown when both are at most 40 degrees, hidden when either goes
  above 55. The gaze keeps it off when a hand held out to the side happens to face the head. It fades in
  over 0.12 s and out over 0.18 s through `XR_KHR_composition_layer_color_scale_bias` when the runtime has
  it; without it the quads switch at the ramp's midpoint. Hidden while the game forces the view (glory
  kills, cutscenes, the meathook pull; `controllers::forcedView`, with its resume delay) and while a menu is
  up or its panel is held for the backdrop (the menu panel takes the UI quad as before). The desktop window's
  menu image, the wash filter and the crosshair mask work on the UI image itself, which the wrist quads
  share, so they are unchanged.
- **Fallbacks.** No controllers, a stale controller snapshot, no grip space: the whole HUD stays on the
  head-locked quad as in panel mode (logged once). Loading screens and menus are unchanged.
- **Head-locked HUD.** `ETERNALVR_HUD=panel` (the default; launcher: "Health and ammo" = On the HUD panel) is
  the earlier behaviour, unchanged.

## The weapon HUD

The public README's planned "health, armour and ammo on your wrist or weapon": the weapon half, ammo on
the gun the way a VR shooter shows it, read with a glance at the hand that holds it.

```
XR worker, every XR frame (presenter_wrist.cpp, the same path as the wrist)
  UI quad filled as before; weapon mode, no menu panel up (nor held), controllers attached?
    head-locked pieces: the band minus the bottom-right corner (ammo block); with
      ETERNALVR_WEAPON_HUD_VITALS=1 minus both corners, as on the wrist
    the gun's frame: the weapon hand's grip position with its aim orientation (the viewmodel's)
    facing test: the panel's normal and the head (controller snapshot, room space)
      facing angle <= 60 deg -> shown; > 75 deg -> hidden (no gaze test)
    shown (or fading): one quad (two with the vitals) in the weapon hand's aim space
```

- **What moves to the gun.** The HUD's bottom-right block: the ammo count, the equipment (grenade, its
  cooldown) and the flame belch, cropped from the GUI target exactly as the wrist does (`hud_regions.hpp`).
  Health and armor stay on the head-locked quad unless `ETERNALVR_WEAPON_HUD_VITALS=1`, which puts the
  bottom-left block beside the ammo at the same scale (on the image's left, as on screen). The ability rings
  stay head-locked. Everything else on the GUI target is as in wrist mode.
- **Where the panel is** (`ui_layer/weapon_hud.hpp`). The viewmodel hook (`vkcore/viewmodel_hook.cpp`) puts
  the gun at the weapon hand's grip position, turned to its aim ray, plus the held weapon's offset. The
  panel uses the same frame without the per-weapon offset: 7 cm above the grip and 5 cm behind it (over the
  back of the hand, where the rear of the gun is), 10 cm wide (the ammo block is then about 5.7 cm tall),
  its face turned 45 degrees from straight back along the barrel toward straight up, so with the gun held
  at the chest it looks at the eyes from below and in front (about 26 degrees off). The line from the eye
  to the aim dot passes about 30 cm above a hand held at the chest, so the panel stays below it; raising the
  gun to the eye brings the panel into view, like a sight. `ETERNALVR_WEAPON_HUD_OFFSET` (x, y, z in the
  right hand's gun frame: +X right, +Y up, +Z back toward the player; x mirrored for the left hand),
  `_WIDTH` and `_TILT` tune it; one offset for every weapon for now.
- **How it follows the hand.** The quads are submitted in the weapon hand's OpenXR aim space
  (`controllers::weaponHandAimSpace`, whatever the aim source), so the runtime places them at display time
  and a recenter does not move them. The grip's position in that space is the controller's own fixed
  offset, taken from each controller snapshot (both poses located at the same time) and kept while the grip
  is lost. The gun in the eye images is drawn from the pose the game view was built with and, under hand
  aim, the smoothed ray (`ETERNALVR_AIM_SMOOTHING`), so in fast swings the panel can lead the gun slightly;
  it settles as the hand stops.
- **When it shows.** One angle with 15 degrees of hysteresis: the facing angle between the panel's normal
  and the direction to the head. Shown at 60 degrees or less, hidden above 75 (`ETERNALVR_WEAPON_HUD_ANGLE`),
  which hides it when the gun points back across the body or at the player, seen edge-on or from behind.
  There is no gaze test: like a sight on the gun, it is there whenever it faces the eyes. It fades with the
  wrist's times (`ETERNALVR_WRIST_FADE`) and is hidden in the same cases: while the game forces the view
  (glory kills, cutscenes, the meathook pull; the game's own hands animation has the gun then), while a menu
  is up or its panel is held, and while the weapon hand is not tracked.
- **Handedness.** It follows the weapon hand: the left controller with `ETERNALVR_HANDEDNESS=left` or
  `left_mirror`, the x offset mirrored.
- **Fallbacks.** No controllers, a stale snapshot or no aim space: the whole HUD stays on the head-locked
  quad (logged once: `ui: weapon HUD: no controllers (or no weapon-hand aim space); the HUD stays on the
  panel`). With the viewmodel off the gun is the game's, in front of the head, and the panel still follows
  the controller.

## The free off hand (ETERNALVR_OFFHAND)

The first-person arms are one skinned model that the viewmodel hook places at the weapon hand. The left
hand follows the `lefthandattach` joint modifier the game writes every tick
(`idHands::UpdateWeaponLagJointMods`). With `ETERNALVR_OFFHAND=free` a mid hook just before its
`SetJointMod` call (`vkcore/offhand_hook.cpp`) rewrites that modifier so the wrist goes to the off-hand
controller's grip with its orientation, and poses the forearm, elbow and upper arm with two-bone IK
(`features/arm/`, "The whole arm" below), blended with the game's own by the arm policy
(`features/input/offhand_policy.hpp`), which hands the arm back to the game at once for glory and sync
kills, melee, throws, weapon switches, custom animations, hidden hands and any forced view, and takes it
back after 0.25 s, blended over `ETERNALVR_OFFHAND_BLEND` (0.15 s).

- **Default: `game`.** Nothing is installed and the game's animation drives the left arm, as before. The
  off hand is still tracked for the wrist HUD, which uses the real controller, not the game's arm.
- **Fail closed.** The function, its calls of `SetJointMod` and `GetJointTransforms` and every frame offset
  the hook reads or writes are checked by signature and bytes at install time (build 25216728): each of the
  three signatures must match exactly once in .text (`findUnique`), the eight byte checks must hold and the
  two calls must land on the other two matches; any mismatch logs `offhand: ... off hand stays the game's`
  and installs nothing. The hook is installed only with the other controller hooks (behind the multiplayer
  guard, on the build `PlayerAim` recognises, with the viewmodel hook on) and only when
  `ETERNALVR_OFFHAND` is `free` or `probe` or `ETERNALVR_OFFHAND_TRACE=1`. Each tick it writes only
  while the multiplayer guard allows it (`mp_guard::allowsGameTouch()`), the hands belong to the local
  player, the game's animated pose and the result are plausible, and the policy gives the controller a
  weight above zero.
- **Unverified.** The hook site, the frame offsets, the `idHands` / `idPlayer` field offsets and the bit
  positions of `handsFlags` come from static analysis and type info only; the bits are marked [inferred].
  A byte scan of the exe on the rig's disk (2026-09-27, PE timestamp 0x6A7B9B8C) finds each signature once
  (UpdateWeaponLagJointMods at RVA 0x138D170, SetJointMod 0x138CF10, GetJointTransforms 0x19807F0), the
  eight byte checks hold and both calls land on the matches: the install checks would pass. That says
  nothing about what the offsets mean at run time.
  The rig run of 2026-09-27 (`free`, scripted input) moved the hand with the controller, weight 1.0, none
  rejected, and showed two faults the arm below fixes: the forearm and sleeve stretched from the shoulder
  to the hand, and the hand did not take the controller's orientation.

### The whole arm (free mode)

**Why the sleeve stretched.** The arms skeleton (`md6/player/human/base/assets/mesh/arms.md6skl`, read out
of `gameresources.resources`) hangs the arm from its attach joint the other way round from a body:
`lefthandattach` (a child of `origin`) carries `LeftHand` (the wrist), which carries the forearm's roll
joints `leftforearmroll3`, `leftforearmroll2`, `leftforearmroll1` and `LeftForeArmRoll` (5.5 to 6 cm apart
toward the elbow), then `LeftForeArm` (at the elbow, 0.28 m from the wrist) and last `LeftArm` (at the
shoulder, 0.28 m from the elbow) [static, build 25216728: joints 30, 31, 52 to 57 of 79]. The skeleton
the game actually loads for the arms is `marine.md6skl` (92 joints, seen on the rig in run ik2): the same
joints at the same indices, plus a forearm device (`extendfront01` and ten children, 58 to 68) hanging
from `leftforearmroll1`, some camera joints, the legs and the body. The body's
shoulder joints (`leftshoulder_body`, `leftarm_body`) hang from the spine instead. So the game's one
modifier on the attach joint carries the whole arm along rigidly, and the skin between the body's shoulder
and the arm's upper end stretched. Also, the attach joint sits about half a metre from the wrist (it is
the weapon's grip point), so putting it at the controller did not put the hand there.

**What the layer does** (`features/arm/`, pure and unit-tested; `vkcore/offhand_arm.cpp` for the game):

- **Joints by shape, checked by name.** The layer reads the parent and name-handle tables of the loaded
  skeleton the way `GetJointTransforms` reaches it (the animator's model +0x80, +0x310, data +0x60; count
  at +2, parent table at the u16 at +0xC, name handles at the u16 at +0x10). At run time the names are not
  there as text (the first rig run, ik1, read garbage where the file keeps them): the game's joint lookup
  compares 16-bit name handles against that table [static: 0x19BFD40, from AddJointMod 0x138B360], and
  gets a name's handle from the global name table (the pointer at RVA 0x47DDA28, vtable +0x38), which
  InitJointMods (0x138B080) asks for "lefthandattach". So the layer starts from the game's own left
  attach joint (hands+0x28E4): `LeftHand` is its only child, and the forearm is the one child of the wrist
  that starts the one line of descent exactly six joints long ending in a leaf (side branches such as
  the forearm device are allowed; the fingers, the prop joint and the device are shorter or
  branch). It then asks the name table for the eight names, the same call, and checks each joint's handle.
  It logs them once: `offhand: arm joints (skeleton of 92 joints, found by shape, names checked with the
  game's name handles): lefthandattach 30, LeftHand 31, ...` (or `names NOT checked` when the name table
  could not be used). Any failure to read the skeleton also logs, once, the pointers it followed
  (`offhand: arm: skeleton path: ...`), hex dumps of the data's first 0x80 bytes, its name handles and the
  file's name block, the parent table (up to 100 entries) and the game's left attach joint's children.
- **The wrist** (`hand_offset.hpp`) is the controller's OpenXR grip with a fixed offset: the grip's
  forward (along the curled fingers) is the hand's `y` (wrist to fingers), its up (thumb side) the hand's
  `x`, its left the hand's `z` (the back of the left hand; the palm faces the grip's +X, which OpenXR puts
  on the palm's normal). From the skeleton: the finger joints curl toward the hand's `-z`, the thumb sits on
  `+x`, the forearm lies along `-y`. The default offset puts the wrist joint 8 cm behind the grip's centre
  and 3.5 cm toward the back of the hand (`ETERNALVR_OFFHAND_OFFSET`, forward/left/up in the grip's frame,
  then pitch/yaw/roll about its axes) [inferred from the OpenXR grip definition; tune in the headset].
- **The elbow** (`two_bone_ik.hpp`): two-bone IK from the shoulder to the wrist with the animated bone
  lengths, bending toward a pole (`ETERNALVR_OFFHAND_ELBOW`, default down, out to the left and a little
  back in the head's yaw frame). A controller beyond reach stops the wrist at 99.9 % of the arm's length on
  the line toward it (logged as `(clamped)`); one closer than 5 % of it is pushed out. The shoulder is a
  point fixed to the tracked head (`ETERNALVR_OFFHAND_SHOULDER`, default 8 cm behind, 18 cm left and 24 cm
  below the eyes, turning with the head's yaw), so the hand reaches the controller wherever the gun is;
  `model` uses the game's own animated shoulder instead, which travels with the arms model at the weapon
  hand (the arm then stays joined to the model's body, but the hand stops short whenever the two
  controllers are further apart than the gun pose allows).
- **The forearm and upper arm** (`arm_solve.hpp`): each roll joint, the elbow and the shoulder joint keep
  their animated offset and orientation relative to a frame along their bone (turned by the elbow's bend
  plane), so the rig's own axis conventions carry over. The wrist's twist about the forearm (the palm
  turning) is shared out by distance from the elbow: none at `LeftForeArm`, 20 % at `LeftForeArmRoll`, 39,
  59 and 78 % at the three rolls, all of it at the wrist, so the sleeve twists instead of the wrist.
- **The attach joint** gets the pose that carries the wrist to its target with the animated
  attach-to-wrist offset, through the game's own modifier (the hook's rewrite, as before). The six other
  joints get modifiers of the layer's own: they are appended to the hands' joint-modifier list (both
  generations) when both have room for six more without reallocating, and written every tick with the
  game's `SetJointMod` as whole-pose overrides (flags 0x22B: model space, rotation, translation, override,
  reference pose) while the controller has the arm, and as no change (identity, the game's 0x20B) the
  rest of the time, so the game's animation runs through them untouched. The list is checked before every
  write; when the game rebuilds it (a new map, a respawn) the entries are added again.
- **Room in the list** (`features/arm/mod_room.hpp`, `vkcore/offhand_mods.cpp`). Rig run ik3 found both
  lists full (`no room for 6 more joint modifiers (3 in use, room for 3 and 3)`). `idHands::InitJointMods`
  (0x138B080) takes a new modifier node from the animation pool each time and adds its three attach-joint
  modifiers with `AddJointMod` (0x138B360: `int AddJointMod(idHands*, u16 nameHandle, u16 flags, int16*
  jointOut)`; the joint looked up by name handle in the hands' skeleton through 0x19BFD40, then
  `SetNum(node, num + 1)`, then the joint, the flags and an identity pose written into the current
  generation's list only; returns the new index, its only callers the three in InitJointMods). The node's
  `SetNum` (0x19A61F0: `void SetNum(node*, int num)`) grows each of the two lists that is too small to
  exactly `num` with idList `Resize` (0x4AAB80: a new block from the game's allocator, the old entries
  copied, the old block freed), fills the new entries with no change (joint -1, flags 0) and sets both
  counts; a smaller count only sets the counts. It compares the new count with the first list's only. The
  blend tree build copies the current list's data pointer and count into the blend's parameters
  (0x19BEEF0), so a list must not be reallocated while a blend job may still read it, and AddJointMod is
  never called from the tick. Instead a mid hook right after InitJointMods' third AddJointMod
  (InitJointMods +0x20D, RVA 0x138B28D, the hands in rbx), on the node the game has just taken, calls the
  game's `SetNum` for six more entries and then for the old count again: the game allocates and owns the
  room (size 9 in both lists), the count stays 3, and the layer appends its six into that room at the
  tick as before. InitJointMods, AddJointMod (its call of SetNum at +0xA3) and SetNum are checked byte by
  byte at install; the calls are SEH-guarded; a count SetNum's early return leaves raised is written back
  and the result read back. When no room was made the tick still refuses the arm.
- **Where an override lands** [rig runs ik4 to ik6]. Written with the joint's pose as GetJointTransforms
  reads it, every override landed about 1.5 lower and 0.23 further back than written, the same offset for
  every joint and the orientation exact, and the arm drew as one long thin tube from below the view. The
  offset is the skeleton's root (joint 0, `origin`, read at (-0.240, 0, -1.591)): an override's translation
  is taken from the root, so the layer subtracts the root's position read that tick. The joints then read
  back within about a centimetre of where they were written. GetJointTransforms reads the blend's final
  pose, which includes the layer's overrides of the last frame, so feeding it to the solver made the arm
  run away (bone lengths of several metres in ik4); for 150 ms after the last override the layer's joints
  are taken instead as the last read without overrides placed them relative to `LeftHand`, which the
  overrides do not move. An override read back more than 0.1 from where it was written for 30 ticks in a
  row stops the arm for the session (`offhand: arm: the layer's overrides do not land where written ...`).
- **Hand-over.** The arm policy's weight mixes the game's arm (every joint carried by the game's own
  modifier) and the solved one joint by joint, the wrist first and the attach joint placed to carry the
  mixed wrist, so a punch, a throw or a glory kill blends out as before.
- **How the modifiers work** [static: the blend's joint-modifier pass 0x19E2A60, SetJointMod 0x138CF10,
  AddJointMod 0x138B360, idList::SetNum 0x19A61F0; not yet seen live]. A model-space modifier is applied
  after the joint's parent: a change multiplies its quaternion on the left and adds its translation, an
  override (0x20) replaces the joint's model-space pose. SetJointMod makes the quaternion from the matrix it
  is given as if the rows were the joint's rotated axes, so the joint's axis becomes animated x matrix,
  which is what `xr_math/offhand_pose.hpp` has always assumed. Positions go in divided by the animator's
  model scale (+0x500), which GetJointTransforms multiplies them by (1 expected; outside 0.5 to 2 the arm
  is refused).
- **Fail closed, again.** In free mode the install also checks the animator getter the hook's function
  calls (+0x94) and the offsets GetJointTransforms and SetJointMod use; any mismatch logs
  `offhand: arm: ... off hand stays the game's` and nothing is installed. Each tick the animator, the
  scale, the skeleton, the joints, the list and the eight animated poses are checked (the attach joint read
  back must equal the one the game read); any refusal is logged once with its reason
  (`offhand: arm: ...; the off hand stays the game's`), counted in `N rejected`, and the game's arm is kept
  for that tick with the layer's modifiers set back to no change.

## Settings

| Variable | Values (default) | Meaning |
| --- | --- | --- |
| `ETERNALVR_HUD` | `panel` / `wrist` / `weapon` (`panel`) | The whole HUD head-locked, the corner blocks on the wrist, or the ammo block on the gun |
| `ETERNALVR_WRIST_ALWAYS` | 0 / 1 (0) | Show whenever the hand is tracked, whichever way it turns |
| `ETERNALVR_WRIST_ANGLE` | 5..90 degrees (40) | Facing angle to show; hidden above it + 15 |
| `ETERNALVR_WRIST_GAZE` | 5..90 degrees (40) | Gaze angle to show; hidden above it + 15 |
| `ETERNALVR_WRIST_FADE` | 0..2 s (0.12) | Fade-in time; the fade-out takes 1.5 times as long |
| `ETERNALVR_WRIST_WIDTH` | 0.05..1 m (0.16) | Width of the vitals + weapon row |
| `ETERNALVR_WRIST_OFFSET` | x,y,z m (0.04,0,0.13) | Row centre in the left grip frame (+X out of the palm, +Y thumb side, +Z to the elbow); x mirrored on the right hand |
| `ETERNALVR_WRIST_ABILITIES` | 0 / 1 (1) | The ability rings above the row |
| `ETERNALVR_WEAPON_HUD_OFFSET` | x,y,z m (0,0.07,0.05) | Panel centre in the right hand's gun frame (grip position, aim orientation: +X right, +Y up, +Z back toward the player); x mirrored on the left hand |
| `ETERNALVR_WEAPON_HUD_WIDTH` | 0.03..0.5 m (0.10) | Width of the ammo block (health and armor at the same scale) |
| `ETERNALVR_WEAPON_HUD_TILT` | -90..90 degrees (45) | The panel's face: 0 straight back along the barrel, 90 straight up |
| `ETERNALVR_WEAPON_HUD_ANGLE` | 5..90 degrees (60) | Facing angle to show; hidden above it + 15 |
| `ETERNALVR_WEAPON_HUD_VITALS` | 0 / 1 (0) | Health and armor on the gun too, beside the ammo |
| `ETERNALVR_OFFHAND` | `game` / `free` / `probe` (`game`) | Who drives the game's left arm |
| `ETERNALVR_OFFHAND_OFFSET` | f,l,u[,pitch,yaw,roll] m, deg (-0.08,0.035,0) | The wrist (`LeftHand`) from the off-hand grip, in the grip's forward/left/up, then turned about its axes |
| `ETERNALVR_OFFHAND_SHOULDER` | `head` / `model` / f,l,u m (`head`: -0.08,0.18,-0.24) | Where the arm's IK starts: fixed to the head (f,l,u from the eyes in the head's yaw frame) or the game's animated shoulder |
| `ETERNALVR_OFFHAND_ELBOW` | f,l,u (-0.2,0.6,-1) | The elbow's bend direction in the head's yaw frame |
| `ETERNALVR_OFFHAND_PROBE` | f,l,u m (0.10,0,0) | Probe mode: added to the game's own left-hand modifier |
| `ETERNALVR_OFFHAND_BLEND` | 0..1 s (0.15) | Hand-over blend |
| `ETERNALVR_OFFHAND_TRACE` | 0 / 1 (0) | Log the left-arm signals when they change, the poses once a second |

**The weapon in the left hand** (`ETERNALVR_HANDEDNESS=left` or `left_mirror`). The off hand is then the
right controller. The arms model is not mirrored (a reflected model would turn its triangles inside out),
so its left arm reaches for the right controller: `ETERNALVR_OFFHAND_OFFSET`, `_SHOULDER` and `_ELBOW`,
given for the left hand, are mirrored left to right (the left distance, yaw and roll change sign), and
`_SHOULDER=model` uses the head's point, since the model's left shoulder then sits beside the weapon. The
left hand keeps its thumb up and its fingers along the grip, so its palm faces out; the start-up line says
`mirrored for the right controller (weapon in the left hand)`. Rig lh0 (before): the arm came from the left
shoulder across the body and filled the lower view; lh1 (after): it comes from the lower right, the four
scripted poses mirrored (`tmp-vr\rs\lh-drive.sh`), 0 rejected.

The launcher has a row for it on the Advanced tab, in the HUD panel group ("Health and ammo": On the HUD
panel / On your wrist / On your weapon, default the panel, `hud` in `launcher.ini`: `panel`, `wrist`,
`weapon`); in stereo it always sends `ETERNALVR_HUD`, `panel` when the controllers are off, and the row is
greyed out without controllers or in mono. The tuning and off-hand variables are environment only.

## Live-test plan

Run in stereo with the controllers (the launcher defaults) and the wrist turned on: launcher, Advanced tab,
"Health and ammo" = On your wrist (or `ETERNALVR_HUD=wrist`). Read the layer's
`eternalvr-<date>-<time>-<pid>.log`. Today's controls that matter here: recenter is both sticks held 2 s (or
the headset's own recenter), the in-headset capture is the left Menu held + a trigger, the Dossier is on X
(hold by default), pause is a left Menu tap.

1. **Start-up.** Look for `ui: HUD wrist (corner blocks on the off hand's wrist); wrist: shown while
   facing the head (facing 40/55 deg, gaze 40/55 deg), 0.16 m row ...` and whether it says `(colour scale)`
   (fades) or `(no colour scale: switched)`. `controllers: game hooks: ... off hand off (game)` by default
   (`on (game)` with `ETERNALVR_OFFHAND_TRACE=1`: the hook then only logs). Under hand aim (the default) also
   `ui: wrist HUD: hand aim masks the crosshair and the ability rings; no abilities quad` once the wrist runs.
2. **Wrist glance.** In a level: the bottom corners are gone from the head-locked HUD; turn the left palm
   up across the chest and look at it. Expect `ui: wrist HUD shown for the first time (left hand, facing
   N deg, gaze M deg)`, health and armor on the image's left, ammo and equipment on its right (the abilities
   above only under head aim), all upright and sharp. Every 900 frames: `ui: wrist HUD: F frame(s) in wrist
   mode, S with the wrist shown; last facing N deg, gaze M deg`. Check it hides with the hand down or
   pointing forward, and that it does not flicker at the edge. If it sits badly on the arm, tune
   `ETERNALVR_WRIST_OFFSET` and `ETERNALVR_WRIST_WIDTH`; if it is hard to bring up, raise
   `ETERNALVR_WRIST_ANGLE` / `ETERNALVR_WRIST_GAZE`.
3. **Recenter and walking.** Turn away from the play area's front, hold both sticks for 2 s, then glance at
   the wrist again: the quads stay on the arm (they are in the grip space) and the glance still triggers at
   the same angles. Walk a few steps with body follow on and turn with the stick: same. The left stick,
   left trigger and left grip still move, fire equipment and Flame Belch while the wrist is shown.
4. **Crops.** Take a few captures in combat (left Menu held + a trigger; each saves `-UI.png`, the GUI target
   the quads show) at the headset's eye size and check the blocks fall inside the crops (`hud_regions.hpp`);
   a clipped digit means a margin to widen. `ETERNALVR_CAPTURE_UI` still works for a series.
5. **Hiding.** A glory kill and a cutscene hide it; the pause menu (left Menu tap) and the Dossier (X hold)
   show the menu panel with no wrist quads, also while the panel stays for the menu's backdrop after the
   cursor goes; a loading screen is unchanged. The low-health red wash stays off the wrist as it does off
   the panel. `ETERNALVR_HUD=panel` (or the launcher row back on the panel) gives back the whole head-locked
   HUD.
6. **Weapon HUD.** `ETERNALVR_HUD=weapon` (launcher: "Health and ammo" = On your weapon). Start-up:
   `ui: HUD weapon (ammo above the gun in the weapon hand); ...` and `ui: weapon HUD: ammo (health and armor
   stay on the panel), 0.10 m wide at (0.000, 0.070, 0.050) in the gun's frame, tilted 45 deg, shown while
   facing the head (60/75 deg)`. In a level: the bottom-right block is gone from the head-locked HUD and
   health and armor are still there; with the gun held at the chest `ui: weapon HUD shown for the first time
   (right hand, facing N deg, gaze 0 deg)` and the ammo sits just above the back of the gun, upright,
   readable, below the aim dot. Fire until the count changes, throw a grenade, switch weapons: the numbers
   follow. Point the gun across the body to the left or back at yourself: it fades out; bring it back: it
   fades in, without flicker at the edge. A glory kill, a cutscene, the pause menu and the Dossier hide it.
   Judge the place in the headset: `ETERNALVR_WEAPON_HUD_OFFSET` (higher, further back), `_TILT`, `_WIDTH`;
   note the values that work for the most weapons. Repeat once with `ETERNALVR_HANDEDNESS=left` (the left
   controller carries it) and once with `ETERNALVR_WEAPON_HUD_VITALS=1` (both corners on the gun, both gone
   from the head-locked HUD).
7. **Off hand, probe.** `ETERNALVR_OFFHAND=probe ETERNALVR_OFFHAND_TRACE=1`: expect three `offhand: ... at RVA
   0x...` lines and `offhand: left hand modifier hook at RVA 0x138D903` (or `... differs at +0x..` / `a
   signature did not match exactly once`, each followed by `off hand stays the game's`, which ends the test).
   The left hand should sit 10 cm forward of where the game puts it; `offhand: arm controller (controller)
   ...` lines as the policy changes. If the hand does not move, the attach joint does not drive the arm and
   free mode cannot work this way.
8. **Off hand, free.** `ETERNALVR_OFFHAND=free ETERNALVR_OFFHAND_TRACE=1`: the left hand follows the left
   controller, and goes back to the game for a punch, a grenade (left trigger), Flame Belch (left grip), a
   weapon switch and a glory kill (`offhand: arm game (left-arm action)`, `(left-arm animation)`,
   `(forced view)`, `(sync)`). Check the `flags 0x...` values against the actions to confirm the [inferred]
   bits. `N rejected` in the pose lines should stay at 0. Recenter (both sticks 2 s) and check the hand
   still meets the controller.
9. **Off hand, the arm.** Same settings. At start-up: `offhand: InitJointMods at RVA 0x138B080`, `offhand:
   the joint modifier node's SetNum at RVA 0x19A61F0`, `offhand: arm: InitJointMods at RVA 0x138B080
   (AddJointMod 0x138B360, SetNum 0x19A61F0), hooked at RVA 0x138B28D to make room for the layer's joint
   modifiers`, `offhand: arm: animator getter at RVA 0x135EC20, name table at RVA 0x47DDA28; ...`
   and the hook line with `wrist offset (-0.080 0.035 0.000) m ... shoulder head (-0.08 0.18 -0.24) m, elbow
   toward (-0.20 0.60 -1.00)`. When the controller first takes the arm: `offhand: arm joints
   (skeleton of 92 joints, found by shape, names checked with the game's name handles): lefthandattach 30, LeftHand 31, leftforearmroll3 52, leftforearmroll2 53,
   leftforearmroll1 54, LeftForeArmRoll 55, LeftForeArm 56, LeftArm 57`, `offhand: arm: 6 joint modifiers
   added at 3..8 (the hands' list now holds 9, room for 9)` and `offhand: arm: upper arm 0.283, forearm 0.281 (animated, game units), model
   scale (1.000 1.000 1.000) ...`; once a second `offhand: ik: wrist (...) target (...), reach R, elbow
   (...), shoulder (...), twist T deg, weight 1.00; 0 rejected` (model space). Scripted poses
   (`ETERNALVR_TEST_INPUT`, metres from the head; the reaches assume the headset faces LOCAL -Z, since the
   shoulder turns with the head): `left.position = -0.1, -0.3, -0.25` with `left.aim = 20, 0` (at the
   chest: reach about 0.46, the elbow bent down and out, no stretched sleeve); `left.position = -0.15,
   -0.2, -0.55` with `left.aim = 0, 0` (straight ahead at arm's length: reach about 0.98, the arm nearly
   straight, not clamped); `left.position = -0.1, -0.1, -1.2` with `left.aim = 0, 0` (beyond reach:
   `(clamped)`, reach about 2.15, the hand stops short on the line to the controller, the arm straight and
   unstretched); `left.position = -0.1, -0.3, -0.35` with `left.aim = 0, 0, 90` (palm up: reach about
   0.66, the twist about 90 deg away from its value with `left.aim = 0, 0` there, the palm facing up and the forearm twisted along its length rather than at
   the wrist). Then `left.aim = 0, 60` and `left.aim = 70, 0` at the chest position (the hand pointing
   up, then across the body, following the controller's orientation). A line `offhand: arm: ...; the off
   hand stays the game's` names what did not check out. On loading a map (and on each respawn or load):
   `offhand: arm: the game's SetNum made room for 6 more joint modifiers in the hands' new lists (3 in use,
   room for 9 and 9)`; `SetNum did not make room ...`, `... do not check out; no room made` or `... cannot
   be read; no room made` instead mean the arm will be refused with `no room for 6 more joint modifiers`.
