# The wrist HUD, the weapon HUD and the free off hand (M6, T-077)

Builds on the UI layer (`docs/rig-findings/ui-layer.md`), the render size (`docs/rig-findings/render-size.md`)
and the motion controllers (`docs/VR_CONTROLLERS.md`). The owner's request (D-035): a HUD read by looking
at the bottom of the left wrist, with the head-locked HUD kept as an option; and both hands
moving freely.

**Status.** Built, unit-tested and checked in the game on the rig with OpenXR-Simulator (2026-09-28: the
wrist HUD shows at a watch glance, hides with the hand down, forward or turned away, does not flicker at
the threshold, and hides under the pause menu); **not yet tried in a headset**, and glory kills, cutscenes
and real head motion are not scriptable on the rig. All are opt-in until the headset checks pass, and the
wrist HUD becomes a default only together with the free off hand: the head-locked panel stays the default HUD and the wrist or the weapon is one setting
away (`ETERNALVR_HUD=wrist` or `weapon`, or the launcher's Advanced tab); the free off hand needs
`ETERNALVR_OFFHAND=free`. The exception is the weapon arm's IK ("The weapon arm"), on by default because
without it the arm holding the gun is out of view; `ETERNALVR_WEAPON_ARM=game` turns it off.

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
  `ETERNALVR_WRIST_ABILITIES=0` leaves them out). Under hand aim there is no abilities quad (logged once): with
  `ETERNALVR_UI_CROSSHAIR=mask`, or until the crosshair hold is confirmed, the UI copy clears the centre of
  the GUI target (the game's crosshair marks the head's ray; `presenter_ui.cpp`), which takes the ability
  rings with it; with the hold (the default) the rings stay on the head-locked quad. In practice the rings
  show on the wrist only under head aim. Everything else on the GUI target stays on the
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

**What the layer does** (`features/arm/`, pure and unit-tested; `vkcore/game_arm.cpp` for the game):

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
- **The forearm and upper arm** (`arm_solve.hpp`, `arm_twist.hpp`). Two ways, by `ETERNALVR_ARM_SOLVE`:
  `animated` (the default, as before: each roll joint, the elbow and the shoulder joint keep their animated
  offset and orientation relative to a frame along their bone, turned by the elbow's bend plane; the wrist's
  twist relative to the animated hand carried by the forearm, wrapped to half a turn every tick, shared out
  as below; the wrist takes the controller's orientation as it is) or `bind`, the prototype of the arms
  track's third step, for both arms, until it has been seen in a headset. With `bind` every joint is built
  from the bind pose's relation to its bone, rebuilt on the solved bones (the "neutral" frame): in the bind pose the
  wrist, the four roll joints and the elbow share one axis whose `y` runs along the forearm (elbow to wrist
  on the left arm, the other way on the right) and whose `z` is the elbow's bend normal turned around, and
  the shoulder joint has the same `z` with its `y` along the upper arm [static: arms.md6skl and
  marine.md6skl; the game's animated elbow joint sat on that frame to 0.0 degrees in every hold of the
  hands rig's baseline]. The roll joints keep their animated distance along the forearm.
  - *Twist.* The wrist's turn about the forearm away from the forearm's neutral frame (swing-twist split),
    measured from the bind pose, not from the game's animated hand: the shotgun's idle turns the left hand
    by -132 degrees, and measured from it the twist wrapped at 48 degrees from the bind pose, inside the
    range a palm reaches (the flashing forearm). The twist is kept continuous from tick to tick: each tick
    takes the way round nearest to the last (`ArmTargets::lastTwist`, held per arm and dropped when the
    arm goes back to the game), so it never jumps as it crosses half a turn; only more than half a turn
    plus 30 degrees from the centre does it flip by a whole turn.
  - *Shares.* The forearm takes the twist up to 90 degrees either way from a centre of -30 degrees (the
    twist of a hand holding a controller level in the rig's neutral pose: -32 on both arms with the
    default wrist offset; a real forearm turns 80 to 90 degrees either way from thumb up), shared out by
    distance from the elbow: none at `LeftForeArm`, 20 % at `LeftForeArmRoll`, 39, 59 and 78 % at the
    three rolls, all of it at the wrist. Twist past that goes to the upper arm, up to 90 degrees more (a
    shoulder's rotation): the elbow joint and every roll joint turn further by it about the forearm, and
    the shoulder joint turns about the upper arm by its part along the forearm (all of it for a straight
    arm, none at a right angle, which keeps the elbow from twisting against a bent upper arm). Beyond both
    the wrist takes the rest.
  - *Swing.* The off hand's wrist bends away from its forearm no further than an ellipse of 70 degrees of
    flexion and extension (about the hand's `x`, toward or away from the palm, which faces `z`) by 35
    degrees of deviation (about `z`, toward the thumb or the little finger), its twist kept: a real wrist
    flexes 70 to 80 and deviates 20 to 35 degrees, and skinned as the game skins it the base arms mesh
    keeps 0.71 of its wrist ring at 90 degrees of swing and 0.50 at 120 [static, the arm design study].
    The hand then turns less than the controller at the extreme. The weapon arm's wrist is the game's,
    under the gun, and is never limited.
  - Rig (OpenXR-Simulator, docs/rig-findings/hands-rig.md, section 7): against the bind pose the largest
    twist between neighbouring forearm joints fell from 67.6 to 26.7 degrees (left) and 43.9 to 34.0
    (right), the left wrist's swing from 92 to 52 degrees, the turn of a roll joint between two roll steps
    30 degrees apart from 171 to 24 degrees, and the narrowest left wrist cross-section of the base mesh
    proxy rose from 0.60 to 0.83 of the bind pose's.
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
  **The mix** [static: InitJointMods 0x138B080 +0xB0 hands the modifier node's create (0x497E00) the
  constant 0.95, which it copies to the owner's (idHands+0x2888) current weight +0x3C and target weight
  +0x40; GetWeight (0x497DE0) returns +0x3C, SetWeight moves +0x40 and fades +0x3C toward it; the Game Pass
  build has the same bytes; rig: ap-base0, ap-final, ap-w1]. The blend mixes the arm's pose without
  modifiers and its pose with every modifier of the node applied whole (an override puts its joint at the
  written pose, a change moves its joint from the pose without modifiers, the children follow) joint by
  joint in each joint's parent's frame at the node's weight, offsets linearly and rotations by nlerp (the
  quaternions mixed linearly the shortest way round, then normalised; not slerp: 90 degrees written lands
  85.9 round, 150 lands 144.3), from the root down (`features/arm/mod_mix.hpp`) [static, round 4: the
  blend command 0x19EAE50, (1 - t) qA + t qB with the sign of qA.qB taken and rsqrtps; the modifiers are
  applied to a copy of the pose without them by 0x19E1040 in list order, each model-space one against its
  parent as modified so far, so an override listed before a change on its parent is carried off by that
  change (rig r4-bind: the wrist's change below listed after the six put the forearm 10 degrees off)]. The
  weight covers every modifier of the node,
  the game's attach joints' and the layer's: held at 1 (`ETERNALVR_TEST_ARM_MOD_WEIGHT=1`, run ap-w1) every
  override of both arms read back exactly where it was written (0.000 mm in all 72 holds), and the gun
  moved to the middle of the view and the weapon hand turned (its twist at rest from -32 to +23 degrees),
  so the weight is the game's and is left alone. This model predicts the weapon arm's read-back to 1.5 mm
  (median) and the left arm's to 0.9 mm once the attach joint below is allowed for; "lands as written" is
  5.9 mm off on the weapon arm and 26 mm on the left (its wrist 200 mm).
- **The off hand's attach joint never reached its target** [rig: every hands-rig run since the first,
  hr-base1 to ap-final; captures]. `GetJointTransforms` reads the final pose of the last frame, the game's
  modifiers mixed in, so the left attach joint's change, computed from that read as if it were the
  animated pose, was applied by the game to its pose without modifiers instead: the joint settled about
  half way (from a still start it swings about the target with a factor of -0.95 a frame), the left glove
  stayed 0.2 m (median) from its target near the shotgun's pump, and the forearm the layer posed stretched
  from it to the solved wrist. Implied by the dumps: the pose without modifiers taken back from each read
  and the change written is the same in every hold of a run (2 mm and 0.8 degrees median spread) while the
  read moves by 74 mm and 11 degrees.
- **Written past the target** (`ETERNALVR_ARM_SOLVE=bind`; `features/arm/mod_mix.hpp`,
  `offhand_mods::modWeight`). The layer reads the live weight every tick (the owner's +0x3C, and +0x40 as a
  check; outside (0.5, 1] it writes as before and logs once per hands: `offhand: arm: the hands' modifier
  weight reads ...`; the bytes it comes from, and the owner it is copied to (idHands+0x2878 handed to the
  create, whose +0x10 it copies to), are checked at install on both builds: `offhand: arm: the hands' modifier
  weight is the owner's +0x3C ...`). The left arm's pose without modifiers is taken back from the read and the
  change the game was handed for the attach joint last tick (the hook notes it every tick, the layer's or the
  game's, with the hands it was handed for: other hands' read, a new level's, has not mixed it in, so it is
  dropped; the hands are told apart by address only, so a new level's hands at the old address would take that
  change for one tick). Then each joint is written past its target: the attach joint's change from the pose
  without modifiers, each of the layer's joints as an override whose offset from its parent's written pose is
  the animated offset moved past the wanted one (the offset by 1/weight, the turn by the inverse of nlerp's:
  a turn of g written as 2 (g/2 + asin((1 - w) / w sin(g/2)))). A turn is never written half a turn or more
  from the animated pose (the mix would take it the other way): written 179 degrees, the attach joint lands
  172.98 round, and the rest of a turn it cannot make the wrist takes with a change of its own (a seventh
  modifier, `LeftHand`, listed before the six and no change otherwise), so every turn up to half a turn
  lands; the joints after a joint that lands short are placed from where it lands. The wrist is read live while
  its change has been no change for 3 ticks (only the attach joint carries it then, and its offset from the
  attach joint is taken again from that read); after a change it is carried by the attach joint at that
  offset. The offset held still in the shotgun holds of the rig (0.02 mm and 0.0007 degrees over a whole
  sweep; other weapons and their animations not measured). The wrist's landing counts in `landed` but not
  toward the landing guard, so a wrist offset of the game's own cannot stop the arm for the session. With the
  wrist's change the offsets taken from a clean read are dropped when the hands, the modifier node or the
  skeleton change (a new level, a skin), and a read counts as clean only 150 ms and 3 ticks after the last
  write (the time rule alone took a read the overrides still showed in after a hitch); without `bind` both
  stay as they were. The weapon arm's wrist and attach joint stay the game's: its wrist as read is taken as
  the parent of the first roll joint, an approximation, since the read has the game's attach change (the
  weapon lag) mixed in at the weight and the joints are mixed from the wrist with it whole, so they land off
  by (1 - weight) of that change (1.4 mm median, 1.8 at most on the rig with nlerp, `r4-bind2`; the 1.5 and
  4.4 of `ap2-comp2` included the slerp model's own error). The read-back is measured against the target
  (`landed`).
  Rig (`ap2-comp2`, 72 holds, against `ap-final`): the weapon arm's read-back from 5.9 mm median (45 at
  most, 26 holds over 10 mm) to 1.5 (4.4 at most, none over 10); the left arm's from 26 mm median (106
  at most, all 72 holds over 10 mm) to 2.3 (19 at most, 4 holds between 10 and 19 mm: the roll -90 lead-in
  and sweep pose, whose attach joint lands 7 degrees short of its target (12 mm at the wrist; it needs
  about 177 degrees, the mix reaches 170), the right roll +90 (5 degrees short, 10 mm) and the mirror-2
  hold, the shoulder joint off by 19 mm); the left wrist on its target (0 mm
  median, from 201). No new failing check against `ap-final`; the gun, the weapon wrist (146.4 mm from the
  grip) and the sight line (2.29 degrees) as before, and the captures show the left glove at the wrist
  with the forearm whole. Without `bind` nothing is read or written for it.
  Round 4 (`r4-bind2` against `r4-ref-bind`, origin/main with `bind`; hands-rig.md section 9): with nlerp
  and the wrist's change every left joint lands within 0.8 mm of its target in all 72 holds (the worst joint
  of each hold from 2.2 mm median and 19.1 at most, 6 holds over 10 mm; `landed`, the layer's own measure,
  from 2.3 and 19.5, 4 holds failing it), the wrist within 0.05 mm and 0.03 degrees (from 13.8 mm and
  8 degrees in the roll -90 and two-hand holds), the first roll joint within 0.05 degrees (from 2.9); the
  weapon arm's read-back 1.4 mm median and 1.8 at most (from 1.5 and 4.5). Twist, swing, wrap, flicker,
  bones, the gun and the sight line as before; 0 regressions against the baseline.
- **Hand-over.** The arm policy's weight mixes the game's arm (every joint carried by the game's own
  modifier) and the solved one joint by joint, the wrist first and the attach joint placed to carry the
  mixed wrist, so a punch, a throw or a glory kill blends out as before.
- **How the modifiers work** [static: SetJointMod 0x138CF10, AddJointMod 0x138B360, idList::SetNum
  0x19A61F0; the pass that applies the hands' modifiers is 0x19E1040 (Steam, one pass, round 4), which
  supersedes the 0x19E2A60 cited here before (that pass skips them: it filters on flag 0x400); the behaviour
  below is seen live on the rig since ik5]. A model-space modifier is applied after the joint's parent: a
  change multiplies its quaternion on the left and adds its translation, an
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

## The weapon arm (ETERNALVR_WEAPON_ARM)

**Why there was no right arm.** Two reasons. First, the viewmodel hook moves the whole arms model so the
gun's grip sits at the weapon-hand controller, and the arm holding it keeps its flat-screen pose relative to
the gun: the forearm and upper arm run back from the grip toward where the flat camera would be, below and
behind the hand, out of view in a normal VR grip. Second, and the reason nothing ever showed: the game does
not draw the right arm at all with most weapons ("The weapon's mesh kit", below). The rig capture once read
as "the right forearm" (2026-10-01, the right hand far out front-right) was the left hand on the pump.

**What the layer does** (`vkcore/weapon_arm.cpp`, on the same hook and arm code as the free off hand).
**Default: `ik`**, whatever `ETERNALVR_OFFHAND` is; `game` leaves the arm as before.

- **The arm.** The arms model is never mirrored, so the weapon arm is always the model's right arm,
  whichever hand holds the weapon. Its joints are found the off hand's way from the game's right attach
  joint: `righthandattach` 2 carries `RightHand` 3, then `rightforearmroll3`, `rightforearmroll2`,
  `rightforearmroll1`, `RightForeArmRoll`, `RightForeArm` and `RightArm`, 24 to 29 [static: marine.md6skl
  and arms.md6skl, read out of `gameresources.resources`; no device hangs from the right forearm]. The
  right arm's bind pose is the left's reflected, each joint's axes negated (`features/arm/arm_joints.hpp`);
  the solver uses only the arm's own geometry, so it serves both (unit-tested: the right arm solves as the
  left's mirror image). The joint's index is the one `InitJointMods` stores at idHands+0x28E6 for the name
  handle of "righthandattach"; install checks those instructions and the name (or logs `weapon arm:
  InitJointMods does not store the joint named righthandattach ...; the weapon arm stays the game's`).
  The names are checked by name handle, and here a name table that cannot be used refuses the arm.
- **The wrist stays.** The gun, its attach joint, the game's modifier of it and the wrist are left exactly
  as the game animates them. Only the four roll joints, `RightForeArm` and `RightArm` get the layer's
  modifiers, six more in the hands' list (room for twelve when both arms are on), as for the off hand.
- **The shoulder and the elbow** are the off hand's mirrored to the weapon hand's side: by default 8 cm
  behind, 18 cm to the side and 24 cm below the eyes, turning with the head's yaw, and the elbow bending
  down, out and a little back (`ETERNALVR_OFFHAND_SHOULDER`'s f,l,u and `ETERNALVR_OFFHAND_ELBOW` tune both
  arms; `model` does not apply here). With the weapon in the left hand the side is the left. Two-bone IK
  reaches the wrist from there with the animated bone lengths; a wrist beyond the arm's reach pulls the
  shoulder along the line to it instead of parting the sleeve from the glove.
- **Hand-over.** The arm policy hands the arm back to the game, blended over `ETERNALVR_OFFHAND_BLEND`, for
  glory and sync kills, melee, throws, weapon switches, custom animations, hidden hands and forced views,
  and while the head and weapon hand are not tracked or the arms are not placed at the hand.
  `ETERNALVR_VIEWMODEL=0` installs nothing and the arm is the game's.
- **Logs.** At start-up `weapon arm: ik on the arms model's right arm (righthandattach, idHands+0x28E6): the
  wrist stays under the gun, shoulder (-0.08 -0.18 -0.24) m from the eyes, elbow toward (-0.20 -0.60
  -1.00), blend 0.15 s, hold 0.25 s` and `weapon arm on (ik)` in the `controllers: game hooks:` line; on
  first use `weapon arm: arm joints (skeleton of 92 joints, found by shape, names checked with the game's
  name handles): righthandattach 2, RightHand 3, rightforearmroll3 24, ...`, `weapon arm: 6 joint modifiers
  added at ...` and `weapon arm: upper arm ..., forearm ... ; the first pose moves the arm up to M from the
  game's`. A refusal logs once `weapon arm: <reason>; the weapon arm stays the game's`. With
  `ETERNALVR_OFFHAND_TRACE=1`: `weapon arm: ik (controller)` or `game (<reason>)` as the policy changes,
  and once a second `weapon arm: ik: wrist (...), elbow (...), shoulder (...), D from the head's point,
  reach R, twist T deg, weight W, up to M from the game's pose, read back within L; N rejected`. `M` is how
  far the layer moves the arm's joints from the game's pose (game units): near 0 where the game's arm
  already reaches a shoulder at the head, as in a flat-screen-like grip, and the picture is then the same as
  with `game`. `L` is how far the joints read back from where they were written a frame before.
- **Rig check of the overrides.** `ETERNALVR_WEAPON_ARM_TEST_SHOULDER=f,l,u` (test only; metres from the
  eyes in the head's yaw frame, not mirrored) puts the shoulder anywhere. Use one in front of the face,
  e.g. `0.45,0.15,0.05` (45 cm ahead, 15 cm left, 5 cm up): with the chest grip of the live test, the arm
  crosses the middle of both eyes, the forearm from the bottom-right corner to the lower middle and the
  upper arm up to the left of centre. `M` is well above 0.1, `D` above 0 when the point is out of the
  arm's reach (the shoulder is pulled toward the wrist), and `L` a few hundredths. A shoulder out to the
  side, such as `0,-0.5,0.3`, sends the arm out of both eyes. The start-up line ends `, TEST shoulder
  (ETERNALVR_WEAPON_ARM_TEST_SHOULDER)`.
- **Unverified.** The overrides read GetJointTransforms, which gives the last frame's pose, so in a fast
  animation (recoil) the sleeve may trail the glove by a frame. First rig runs (2026-10-02, simulator,
  `right.position = 0.15, -0.25, -0.35`): joints found, modifiers added, 0 rejected, overrides read back,
  but the picture was the same as with `game`, even with the arm across the view: the weapon's mesh kit
  had the arm switched off (below). Showing it is not yet confirmed on the rig.

### The weapon's mesh kit

The first-person hands model draws each arm as its own piece (a "surface"): the right arm, glove included,
is one surface, `arm_low_rt_base`; the left arm is `arm_low_lf_base` plus two armour plates on its hand. The
model also has named sets of surfaces, its mesh kits: `BaseSimple` (both arms), `ArmLeft` (the left arm
only), `ArmRight` (the right arm only), `HideAll` (neither) and a few more. Every weapon names the kit to
draw while it is held, and the game switches to it when the weapon comes up:

| kit | weapons |
|---|---|
| `ArmLeft` | combat shotgun, super shotgun, heavy cannon, plasma rifle, rocket launcher, chaingun |
| `HideAll` | ballista, BFG 9000, unmaykr |
| `BaseSimple` | fists, chainsaw (and the game's melee state) |
| `ArmRight` | the pistol decl (not a campaign weapon) |

So with nearly every gun the right arm is switched off. In flat play that costs nothing: the arm would be
below the screen anyway. In VR it meant the weapon arm's IK posed an arm that was never drawn (rig runs
rikA and rikAg, 2026-10-02: the arm placed across the middle of both eyes, nothing drawn, the shotgun's
pistol grip bare).

**What the layer does** (`vkcore/hands_surfaces.cpp`). While the layer poses an arm (its weight above zero
and the pose written this tick) it switches that arm's surfaces on with the game's own "show surface"
function. When the arm goes back to the game (weight zero: untracked, hands hidden, forced view, glory or
sync kill, melee, throw, weapon switch; a rejected tick; a multiplayer guard trip) it switches off again
exactly the surfaces it switched on, with the game's "hide surface", so the game's own arm pose, which
points back at the camera, never pokes into view. A surface the weapon's kit already shows (fists,
chainsaw, melee) is never touched. The free off hand (`ETERNALVR_OFFHAND=free`) gets the same: with the
ballista, BFG and unmaykr its left arm is shown while the layer poses it.

- **Found, not fixed.** The surfaces are looked up on each new hands model by name (`arm_low_rt_base`; the
  left arm's three), compared the game's way (up to a `$`, ignoring case), and failing that taken from the
  kit holding only that arm (`ArmRight`, `ArmLeft`). The mesh file lists `arm_low_rt_base` second (index 1
  of 7 expected), but no index is assumed.
- **Fail closed** [static, build 25216728, every byte checked at install]. idHands' kit apply (RVA
  0x137F690) takes the model at idHands+0x370 and jumps to the model's apply (0x19C8590), which calls
  SetMeshKit (0x19CF4B0) for kit group 5 and the per-surface FindSurfaces (0x19CEE30), Hide (0x19CF620)
  and Show (0x19D0340). Install follows exactly those calls, so the model, Show and Hide the layer uses are
  the ones the game's kits use. Show and Hide set and clear the surface's bit in the model's 128-bit
  visibility set (+0x518) and mark the model's surfaces for a rebuild (byte +0x57C |= 0x10), nothing else.
  Surfaces by name: model+0x4D8 -> +0x78 (pointers) and +0x80 (count), a surface's name at +0x8. Kits:
  model+0x4D0 -> +0x3B8 + group*0x18 (0x48 bytes each, count at +0x3C0 + group*0x18), a kit's name at +0x8,
  its surface indices at +0x30 and their count at +0x38. A check that fails leaves every surface to the
  game's kits (logged); a faulting call turns this off for the session. Same game thread as the pose, no
  hook of its own.
- **Not certain.** The layer sees the bits, not the game switching kits. If the game switches to a kit that
  shows the arm while the layer still shows it (say, a switch to the fists before the arm's blend-out
  ends), the hand-back hides it once, and it stays hidden until the game applies a kit again. The weight
  drops within a third of `ETERNALVR_OFFHAND_BLEND` as soon as a switch starts, which should be well before
  the new weapon's kit; unverified on the rig.
- **Logs.** At start-up `arm surfaces: idHands show/hide mesh apply at RVA 0x137F690`, `... surface Show at
  RVA 0x19D0340`, `... surface Hide at RVA 0x19CF620`, `... FindSurfaces at RVA 0x19CEE30`, `... SetMeshKit
  at RVA 0x19CF4B0`, then `arm surfaces: idHands+0x370's model, kit group 5 (the model's apply at RVA
  0x19C8590); Show 0x19D0340, Hide 0x19CF620 (bits at +0x518); the arms found by name, else by kit; a posed
  arm is shown`. On the first pose: `weapon arm: right arm surface 1 of 7 on the hands model, found by name
  (the kit ArmRight lists 1); shown with the game's Show while the layer poses the arm` and `weapon arm:
  right arm surface 1 shown (the weapon's mesh kit had hidden it)`; on the first hand-back `weapon arm:
  right arm surface 1 hidden again as the weapon's mesh kit has it (the arm is the game's)`. The free off
  hand logs the same as `offhand: arm: left arm surface ...`. Not found: `weapon arm: the right arm's
  surfaces are not on the hands model (N surfaces: <index name, ...>; kits of group 5: <names>); ...`. With
  `ETERNALVR_OFFHAND_TRACE=1` the once-a-second pose line ends `; right arm surface shown` (the layer shows
  it), `the game's` (the weapon's kit shows it), `hidden by the weapon's kit` or `off` (not found, or the
  checks failed). After a multiplayer guard trip, once: `weapon arm: the multiplayer guard tripped: the
  layer's arm modifiers set back to no change; ...` and `... surface 1 hidden again as the weapon's mesh
  kit has it`.

## Hidden arms (ETERNALVR_ARMS)

`ETERNALVR_ARMS=hidden` (the launcher's "Show arms" off) hides both first-person arms and leaves the weapon
alone. It is the fallback for the weapon in the left hand: the arms model is not mirrored, so the game's
right arm keeps reaching across the view to the gun in the left hand (GitHub issue #23). Mirrored arms are
the real fix and need an arm model of their own (the free-arm survey above).

- **How.** The same surfaces as above (`arm_low_rt_base`; `arm_low_lf_base` with its two armour plates),
  hidden with the game's own Hide on every hands tick, whatever the weapon's kit or the layer's posing: an
  equip applies the weapon's kit again and fists, melee and glory kills show the arms, so a visible surface
  is hidden again on the tick it shows. The weapon arm's IK and the free off hand still run; they draw
  nothing. The hands hook is installed for this alone when neither of them is on.
- **After a multiplayer guard trip** the layer stops hiding and writes nothing more. It does not show the
  surfaces it hid: once a surface is hidden it cannot tell whether the current weapon's kit wants it hidden
  too (most weapons hide the right arm), and showing it would bring back the game's pointing-back arm. The
  weapon's next kit (an equip) or new hands (a map load, as a multiplayer map loads) show what the game
  wants. Logged once per arm: `... the multiplayer guard tripped: right arm no longer hidden by the layer;
  ...`.
- **Not hidden.** Hidden arms need the viewmodel and hands hooks (motion controllers on, a known game
  build); without them the start-up says `controllers: the arms stay shown (ETERNALVR_ARMS=hidden needs the
  viewmodel and hands hooks)`, and if the arms' surfaces cannot be reached `offhand: the arms' surfaces could
  not be reached (above); the arms stay shown`.
- **Logs.** The start-up line ends `both arms hidden (ETERNALVR_ARMS=hidden)`; on each model `... surface N
  of M on the hands model, found by name (...); hidden with the game's Hide (arms hidden)`, then once
  `weapon arm: right arm surface 1 hidden (arms hidden)` and `offhand: arm: left arm surface ... hidden
  (arms hidden)`. With `ETERNALVR_OFFHAND_TRACE=1` the pose line ends `hidden (arms hidden)`.

## Arms model (ETERNALVR_ARM_MODEL, rig tests)

Rig tests only, nothing changes without the variable (`vkcore/arm_model.hpp`). The first-person arms are one
model owned by the hands (idHands, idPlayer+0xD2C8: render model +0x370, its md6 decl +0x4D0), set only by
`idHands::SetModel(hands, decl)` (Steam RVA 0x1369450, Game Pass 0x136DA30) from two callers: idPlayer's
InitHands (Steam 0x13EAB10, GP 0x13EF0F0; the player's hands decl at spawn and on a map load, then the
hands' kit) and the cosmetic skin's apply (Steam 0x1355EA0, GP 0x135A480; the skin's first-person decl,
then the skin's kit). Weapon switches only apply the weapon's kit to whatever model the hands have
[static, build 25216728]. A mid hook on each of the two calls hands SetModel another decl, so the game's
own code builds the model, its animator, its joint modifiers (the layer's room among them) and the kits
at the moment it would anyway, and SetModel's own check (the model's decl is the one asked for) keeps a
later InitHands from building it again.

- `base`: the decl the game's inherits from whose name ends in `fp_hands.md6`, reached through the decl's
  parent (+0xB8): on the rig `md6def/player/human/base/praetor.md6` has the parent
  `md6def/player/human/base/fp_hands.md6` (the base arms mesh `arms.md6mesh`, 79 joints).
- `skin` or `skin:<N>`: `md6def/customization/player/human/set<N>/base/praetor_set<N>.md6` (set 3 by
  default, in `gameresources.resources`), found by name in the game's md6 decl list: the resource list of
  the class `idDeclMD6` (the game's lookup of a resource list by class name) and the list's find by name
  (the call `idDeclInfo::FindWithInheritance` makes before it loads anything). The decl names carry their
  type's folder (`md6def/...`); the name without it is tried too.
- A candidate is used only when it has the game's decl's vtable; otherwise the game's decl is kept and the
  reason logged once (`arm model: the hands' model ... stays the game's: ...`). The two call sites,
  SetModel and the two lookups are found by signature, each unique in both the Steam and the Game Pass
  images [static], and checked at install (both calls must reach SetModel, the find's prologue must
  match). Nothing is swapped while the multiplayer guard refuses.
- Logs: `arm model: ETERNALVR_ARM_MODEL=... hooked`, then per decl `the hands' model <game> (decl ...,
  parent ...) becomes <candidate>` and `SetModel N for hands ...: <candidate> instead of <game>`.

Rig (OpenXR-Simulator; docs/rig-findings/hands-rig.md, section 7):

- `base` swaps (the log names fp_hands.md6) but is not usable as it stands: its animated arm reads 2.5
  times longer (upper arm 0.707 game units against 0.276 on the game's model) and far from where the
  layer's targets are (the weapon wrist at (0.71, -1.21, 3.16)), the view looked up at the ceiling in
  every capture, and the left arm's overrides read back 2.8 units from where they were written, so the
  layer's landing guard gave the off hand back to the game after 30 ticks. What it is (round 4): the base
  model is not animated at all, and it is drawn 2.5 times its size. Its arm reads back as the bind pose of
  its skeleton (`arms.md6skl`, decoded from the skeleton's inverse bind matrices) multiplied by exactly 2.50
  about the model's origin: both attach joints at the one point (0.585, 0, 1.268) x 2.50, the wrist at
  (0.378, 0.484, 1.264) x 2.50 on all three axes, where the game's model has its arms in the weapon's idle
  pose; the bind poses of `arms.md6skl` and the game's `marine.md6skl` agree to about 3 cm, so the skeleton's
  own units are not it [rig: ap-base-model's dump lines before the layer posed the arm; static: the two
  skeletons]. `fp_hands.md6` is the abstract parent of the player's hands decl (`praetor.md6`: `inherit
  "md6def/player/human/base/fp_hands.md6"`, its own `mesh` `marine.md6mesh` and `offset ( -0.24003 0
  -1.59067 )`; `fp_hands.md6` has `arms.md6mesh` and `offset ( -0.24003 0 0 )`) [static: the decls in
  gameresources]; its 79-joint skeleton does not take the hands' animations, made for the 92-joint one
  [inferred], so the camera joint stays at its bind pose too (the view at the ceiling). Where the 2.5 comes from
  is not found (the animator's model scale at +0x500 reads 1). `base` is not a candidate: scaled back it
  would still be an unanimated arm and camera. The switch still works for rig tests; whether to remove it is a
  later decision. The bare arms mesh it was meant to try (`arms.md6mesh`) would need the mesh swapped inside
  the game's own decl instead.
- `skin` (set 3) swaps, keeps the 92-joint skeleton and gives the same joint-level results as the game's
  model (the solve does not depend on the mesh), but its left arm's overrides read back 57 to 102 mm from
  where they were written in the roll poses (19 to 24 mm on the game's model), and the captures show the
  left glove away from the solved wrist (toward the shotgun's pump at roll 0, in front of the camera at
  roll +90). The cause was the off hand's attach joint ("Where an override lands"): on the game's model
  too the glove never reached the wrist, it stayed near the pump. With the mix allowed for (`bind`, run
  ap2-skin3) the set 3 glove follows the solved wrist in every capture and the left arm reads back 2.3 mm
  from its target (median; 19 at most, the same four holds as on the game's model), the weapon arm 1.5. Round 4
  (r4-skin3, nlerp and the wrist's change): every left joint within 0.8 mm of its target in all 72 holds.
- The weapon's kit: SetModel applies none; after InitHands the hands' kit and the next equip's weapon kit
  are the game's own, as without the switch. The surfaces the layer shows and hides are found on the new
  model by name or by kit (set 3: by kit); the base model has one `armsMesh` surface for both arms and
  none per arm, so the layer leaves them to the weapon's kit.

## Hands only (ETERNALVR_ARMS=hands, rig tests)

`ETERNALVR_ARMS=hands` (rig tests only) leaves the gloves: while the layer poses an arm (the free off hand,
the weapon arm in `ik`), its forearm roll joints, elbow and shoulder joint are put on the wrist with its
orientation (`features/arm/arm_solve.hpp`, `handsOnly`, the `fold` cuff) and written with the modifier's scale
flag (4) and the zero scale SetJointMod leaves in the entry, so the skin they carry shrinks into the wrist
[static: SetJointMod 0x138CF10 zeroes the entry's +0x20..+0x2B; the modifier pass 0x19E1040 copies +0x20
over the joint's scale under an override with flag 4 (0x19E1486; Steam, one pass, round 4); 0x19E2A60, cited
here before, is superseded; rig: ap-hands2, ap2-hands-fold, r4-hands]. Those joints then read back scaled to nothing, so
while the overrides show the layer checks only the attach joint and the wrist and does not count the landing
(`vkcore/game_arm.cpp`). When the game has the arm (a glory kill, melee, a weapon switch) it draws whole, as
before.

Rig run ap-hands2: both arms posed in all 72 holds; the forearms and upper arms are gone from every capture;
the cuff closed into a long cone with spikes. That cone was the off hand's attach joint ("Where an override
lands"): the left glove stayed near the pump while the folded joints sat at the solved wrist about 0.2 m
away, and the skin between them stretched. The checker's bone, swing and mesh checks fail by design in this
mode.

`ETERNALVR_TEST_HANDS_CUFF` (rig tests) closes the cuff three ways (`arm::handsOnly`): `fold` (the default,
as above), `stub` (the last roll joint left where the arm has it, a stub of forearm about 6 cm long, the
rest folded onto it) and `ring` (the forearm's roll joints on the wrist at full size, the elbow and the
shoulder folded and scaled to nothing). With the mix allowed for (`ETERNALVR_ARM_SOLVE=bind`; runs
ap2-hands-fold, ap2-hands-stub, ap2-hands-ring, both arms posed in all 72 holds each): `fold` looks best,
the glove at the wrist with a short rounded cuff and no spikes, a little smearing on the back of the hand at
roll +90; `stub` leaves a bulky stump of armoured forearm behind the glove; `ring` opens the gauntlet's
ring, its inside and a flap of skin showing. Without `bind` the glove of the free off hand is not at the
wrist and every way shows the stretch above.

## Arms in cutscenes (ETERNALVR_CUTSCENE_ARMS)

In a cutscene shown around the player (`ETERNALVR_CUTSCENES=immersive`) the game draws its own hands model
in its own pose (the layer's posing yields to the forced view), with the headset's FOV the layer gave it, and
the Slayer's left forearm and blade showed at the bottom of the view (GitHub issue #24). With
`ETERNALVR_CUTSCENE_ARMS=hidden` (the default; the launcher's "Arms in cutscenes" off) the camera hook marks
each such cutscene frame (`controllers::noteImmersiveCutscene`) and while it lasts:

- every visible surface of both arms is hidden with the game's Hide on each hands tick, and when the
  cutscene ends exactly the surfaces it hid are shown again (`arm::planHeldSurface`); what the weapon's kit
  hides stays hidden;
- the game's weapon FOV is left as it is (no headset FOV in `weaponFOVX/Y` and `customFOV2X/Y`), and the
  stereo eyes neither set `inhibitModelFovScale` nor retarget the hands and guns matrices.

Glory kills, the Meathook and menus are not cutscenes (`renderView_t.inCutscene`) and keep all of this as
before. `shown` keeps the old behaviour in cutscenes, and so does `ETERNALVR_ARMS=hidden` whatever this says
(the arms are hidden anyway, and the weapon FOV stays the headset's). It needs the hands hook and its surface
code (the controllers on); without them nothing changes. What the cutscene hid is shown again after a
multiplayer guard trip too. On the test PC the hands tick did not run during cutscenes, so the surface hide
above took effect only at the cutscene's end (rig, 2026-10-08); the kept weapon FOV is what changes during the
cutscene, and whether it keeps the arms out of sight has not been checked in a headset. Logs: `controllers:
cutscene around the player starts: the arms hidden, the weapon FOV the game's` / `... ends`, and per arm
`... surface N hidden for the cutscene` / `... shown again after the cutscene`.

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
| `ETERNALVR_OFFHAND_TRACE` | 0 / 1 (0) | Log the left-arm signals when they change, the poses once a second (the weapon arm's too) |
| `ETERNALVR_WEAPON_ARM` | `ik` / `game` (`ik`) | Who poses the weapon arm's forearm, elbow and upper arm; the wrist and gun stay the game's |
| `ETERNALVR_WEAPON_ARM_TEST_SHOULDER` | f,l,u m (unset) | Rig tests: the weapon arm's shoulder at this point from the eyes (head's yaw frame), not mirrored |
| `ETERNALVR_TEST_ARM_DUMP` | file (unset) | Rig tests: both arms' poses every 0.25 s as JSON lines for the hands rig ([rig-findings/hands-rig.md](rig-findings/hands-rig.md)) |
| `ETERNALVR_TEST_ARM_FAULT` | `none` / `twist` (`none`) | Development only: the whole wrist twist on the wrist joint, the hands rig's positive control |
| `ETERNALVR_ARMS` | `shown` / `hidden` / `hands` (`shown`) | Both first-person arms drawn, or hidden with the weapon alone ("Hidden arms" above; the launcher's "Show arms"); `hands`, rig tests: the gloves alone ("Hands only" above) |
| `ETERNALVR_ARM_SOLVE` | `animated` / `bind` (`animated`) | The arm solve's wrist twist measured from the game's animated hand (as before) or from the bind pose, kept continuous and shared to the upper arm, the off hand's swing limited, and every joint written past its target by the hands' modifier weight so it lands there, the off hand's wrist taking the rest of a turn its attach joint cannot make ("The whole arm" and "Where an override lands" above) |
| `ETERNALVR_TEST_ARM_MOD_WEIGHT` | 0..1 (unset) | Development only: the hands' joint-modifier weight held while the free off hand is posed, the weights found put back when the arm goes back to the game or the multiplayer guard trips; new hands (a new level's) start at the game's own ("Where an override lands" above) |
| `ETERNALVR_TEST_HANDS_CUFF` | `fold` / `stub` / `ring` (`fold`) | Rig tests: how `ETERNALVR_ARMS=hands` closes the sleeve at the glove ("Hands only" above) |
| `ETERNALVR_ARM_MODEL` | `game` / `base` / `skin[:N]` (`game`) | Rig tests: the hands' model, the game's, the base arms mesh or Slayer skin set N, 3 by default ("Arms model" above) |
| `ETERNALVR_CUTSCENE_ARMS` | `shown` / `hidden` (`hidden`) | In cutscenes shown around the player the arms hidden with the game's weapon FOV kept, or as before; nothing with `ETERNALVR_ARMS=hidden` ("Arms in cutscenes" above and in the launcher) |

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
`weapon`); it always sends `ETERNALVR_HUD` (in mono too, where it also turns the UI layer on), `panel` when
the controllers are off, and the row is greyed out without controllers. The tuning and off-hand variables are environment only.

## Live-test plan

Run in stereo with the controllers (the launcher defaults) and the wrist turned on: launcher, Advanced tab,
"Health and ammo" = On your wrist (or `ETERNALVR_HUD=wrist`). Read the layer's
`eternalvr-<date>-<time>-<pid>.log`. Today's controls that matter here: recenter is both sticks held 2 s (or
the headset's own recenter), the in-headset capture is the left Menu held + a trigger (under SteamVR with
Touch controllers, both sticks held, then a trigger), the Dossier is on X (hold by default), pause is a left
Menu tap (under SteamVR with Touch controllers, a Y hold; B in the full mirror).

1. **Start-up.** Look for `ui: HUD wrist (corner blocks on the off hand's wrist); wrist: shown while
   facing the head (facing 40/55 deg, gaze 40/55 deg), 0.16 m row ...` and whether it says `(colour scale)`
   (fades) or `(no colour scale: switched)`. `controllers: game hooks: ... off hand off (game), weapon arm on
   (ik)` by default (`off hand on (game)` with `ETERNALVR_OFFHAND_TRACE=1`: the hook then only logs for the
   off hand). Under hand aim (the default) also
   `ui: wrist HUD: under hand aim no abilities quad (the ability rings stay on the head-locked quad, or the crosshair mask takes them)` once the wrist runs.
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
4. **Crops.** Take a few captures in combat (the capture chord above; each saves `-UI.png`, the GUI target
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
   modifiers (12: the off hand and the weapon arm)` (6 with `ETERNALVR_WEAPON_ARM=game`), `offhand: arm:
   animator getter at RVA 0x135EC20, name table at RVA 0x47DDA28; ...`
   and the hook line with `wrist offset (-0.080 0.035 0.000) m ... shoulder head (-0.08 0.18 -0.24) m, elbow
   toward (-0.20 0.60 -1.00)`. When the controller first takes the arm: `offhand: arm joints
   (skeleton of 92 joints, found by shape, names checked with the game's name handles): lefthandattach 30, LeftHand 31, leftforearmroll3 52, leftforearmroll2 53,
   leftforearmroll1 54, LeftForeArmRoll 55, LeftForeArm 56, LeftArm 57`, `offhand: arm: 6 joint modifiers
   added at 9..14 (the hands' list now holds 15, room for 15)` (at 3..8 when the weapon arm is off or took
   none yet) and `offhand: arm: upper arm 0.283, forearm 0.281 (animated, game units), model
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
   `offhand: arm: the game's SetNum made room for 12 more joint modifiers in the hands' new lists (3 in
   use, room for 15 and 15)` (6 and 9 without the weapon arm); `SetNum did not make room ...`, `... do not
   check out; no room made` or `... cannot be read; no room made` instead mean the arm will be refused with
   `no room for 6 more joint modifiers`.
10. **The weapon arm.** Defaults (`ETERNALVR_OFFHAND` unset), plus `ETERNALVR_OFFHAND_TRACE=1`. At start-up:
   `weapon arm: ik on the arms model's right arm (righthandattach, idHands+0x28E6): the wrist stays under the
   gun, shoulder (-0.08 -0.18 -0.24) m from the eyes, elbow toward (-0.20 -0.60 -1.00), ...`, `offhand: arm:
   InitJointMods ... (6: the weapon arm)`, `offhand: arm: animator getter ...; ... (two-bone IK) on the weapon
   arm` and `controllers: game hooks: ... weapon arm on (ik)`. In a level: `weapon arm: arm joints (skeleton
   of 92 joints, found by shape, names checked with the game's name handles): righthandattach 2, RightHand 3,
   rightforearmroll3 24, rightforearmroll2 25, rightforearmroll1 26, RightForeArmRoll 27, RightForeArm 28,
   RightArm 29`, `weapon arm: 6 joint modifiers added at 3..8 (the hands' list now holds 9, room for 9)`,
   `weapon arm: upper arm ..., forearm ...` and once a second `weapon arm: ik: wrist (...), elbow (...),
   shoulder (...), 0.00 from the head's point, reach R, twist T deg, weight 1.00; 0 rejected`. Scripted
   poses (metres from the head, headset facing LOCAL -Z): `right.position = 0.15, -0.25, -0.35` with
   `right.aim = 0, 0` (a normal grip at the chest: the forearm runs from the glove back and down to the
   elbow, below and out to the right, the upper arm up toward the right shoulder); `right.position = 0.25,
   -0.15, -0.6` with `right.aim = 0, 0` (further out: the arm straighter, `reach` near 1, or the shoulder
   pulled along once past it, depending on where the gun puts the wrist);
   `right.position = 0.1, -0.1, -1.0` (beyond reach: `D from the head's point` above 0, the arm straight,
   no gap at the wrist); `right.position = 0.0, -0.25, -0.3` with `right.aim = 40, 0` (across the body:
   the elbow out to the right). Compare with `ETERNALVR_WEAPON_ARM=game` at the same poses (no arm, or the
   forearm running off to a screen corner). The gun and hand must sit exactly where they do with `game`. A
   melee, a grenade, a weapon switch and a glory kill give `weapon arm: game (left-arm action)`, `(left-arm
   animation)`, `(forced view)` or `(sync)` and the game's arm, then `weapon arm: ik (controller)` again.
   Repeat with `ETERNALVR_HANDEDNESS=left` (the start-up line says `for the left controller`, the shoulder
   `0.18` to the left) and with `ETERNALVR_OFFHAND=free` (both arms, `room for 15 and 15`).
   The right arm's surface ("The weapon's mesh kit"): with the combat shotgun, `weapon arm: right arm
   surface 1 of 7 on the hands model, found by name ...` and `... surface 1 shown (the weapon's mesh kit
   had hidden it)` on the first pose, and the pose line ends `; right arm surface shown`; a glove on the
   pistol grip and a sleeve behind it, where `game` shows a bare grip. With
   `ETERNALVR_WEAPON_ARM_TEST_SHOULDER=0.45,0.15,0.05` the arm crosses the view. The first melee or switch
   logs `... surface 1 hidden again ...` and no arm points at the camera during it.
