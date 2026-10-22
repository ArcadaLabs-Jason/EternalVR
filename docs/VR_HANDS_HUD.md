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
  wrist mode, no menu panel up (nor held), controllers attached, both corner blocks measured?
    head-locked pieces: the band minus the two bottom corners (health block, weapon block), where the game
      drew them (the GUI readback, hud_blocks.hpp)
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
- **Where the crops come from** (`ui_layer/hud_regions.hpp`, `ui_layer/hud_blocks.hpp`). Each corner block
  is cut where the game drew it, measured from the GUI image ("Where the corner blocks are" below), and
  grown to the band's side and bottom edges it sits at; its hand quad shows that same rectangle. The band is
  `ui_layer::wideContentRect`, the same rows the UI quad shows since the render size made the eye image
  near-square (1280x1400, 2064x2100), so a crop never reaches the empty rows above or below it; on a GUI
  target wider than 16:9 it is the centred columns the game fits its 16:9 frame into (up to 0.1.70 such a
  target was taken whole and the corners were cut at the wrong columns). The ability rings are a fixed
  rectangle (x 0.39..0.61, y 0.43..0.59 of the band): they are drawn the same at every HUD Scale. Unit tests
  check every cut lies in the band at 1280x1400, 2064x2100, 2560x2100, 2054x2068, 1920x1080, 2048x864 and
  2560x1080 and holds its block.
- **Where the corner blocks are** (`ui_layer/hud_blocks.hpp`, `vkcore/wheel_probe_corners.cpp`). The blocks'
  size and place follow the game's HUD Scale (Large, which VR holds, draws them about 17% larger than Medium;
  `hud_regions.hpp` has the band coordinates at Small, Medium and Large) and what they hold: the extra lives
  row above health and armor, the equipment row and its key hints above the ammo come and go, and the opening
  cinematic's skip prompt sits where the ammo block goes. Up to 0.1.75 the cuts were fixed rectangles made for
  Medium (x 0..0.215 and 0.765..1 from y 0.76 of the band): at Large about 6.5% of the health block's drawn
  pixels and 2.6% of the ammo block's stayed on the head-locked HUD. The weapon wheel's readback
  (`vkcore/wheel_probe.hpp`, docs/VR_MENUS.md, "Where the wheel shows") reads the corners too: in wrist or
  weapon mode, outside menus and full-screen backdrops (a fade) with the wheel down, a GUI copy copies every 4th row of the band's two bottom corners (the bottom 40% of
  its height, the outer 40% of its width on each side) into the same host buffer. The layer then labels the
  connected drawn shapes (alpha 16 or more on a 4-pixel grid) in each corner and takes the largest one that
  touches neither the corner's top nor its inner edge (a menu or a fade fills the corner and touches them; so
  do the open wheel's bottom and a long subtitle reaching in from the middle): the block's bars and icons,
  which their glow joins. It must reach below 0.85 of the band's height and be at least 5% of its width
  across; with it go the shapes whose box comes within 2% of the band's height of the block's (a key hint
  label), until none does; a result more than 30% of the band's width or height across is not a block. The
  box is grown by the grid's step and 1% of the band's width. The head-locked HUD and the hand quads use the
  union of each block's last 8 readings: a block that grows (the extra lives row appears) is cut whole from
  its next reading on, and the space it leaves comes back 8 readings later. Readings are made every GUI copy
  until one finds both blocks (at most 30 of them: a HUD the player hid has nothing to find), then every
  30th; a copy that finds no reading slot free leaves the read to the next one; a new target size starts over
  (the cuts stay across map loads at one size). Wrist mode needs both blocks measured, weapon mode the ammo
  block (and health and armor with `ETERNALVR_WEAPON_HUD_VITALS=1`): a player who hid one in the game's HUD
  settings keeps the whole HUD on the head-locked quad. A corner more than half drawn (a fade, a backdrop) is not labelled.
  Until both blocks of a mode are measured the hand HUD waits and the whole HUD stays on the head-locked quad
  (logged once: `... HUD waits for where the game drew the corner blocks`); on the rig, after a map's loading
  screen and its fade, the fourth GUI copy read found both blocks (the three before it found neither). Each change of the cuts
  is logged (`... HUD cuts the corner blocks where the game drew them: health and armor GUI rect ..., ammo
  ...`, the first 40), and the readback's `ui: corner blocks:` lines (the first reading that finds both, then
  every 60 readings) give its CPU and GPU time. On the rig (Release, 2048x2208 GUI target, 3 minutes in play
  in e1m3) a reading took 270 to 290 us of the present hook's CPU on average (65 to 80 of them reading the
  grid out of the host buffer), 490 us at most, and its copy about 105 us of GPU time; at every 30th GUI copy
  that is 9 to 10 us a copy on average. It runs on the present hook as the wheel's does; moving the measuring
  to a worker thread would take it off there. In panel mode nothing is read. A GPU without cached host memory has no readback: the wheel stays
  where the game draws it and the hand HUDs are not shown (logged). Nothing here reads game memory, so it is
  the same on Steam and Game Pass. Unit tests measure both corners of real GUI captures at Small, Medium and
  Large, with the wheel open, in a cinematic and on a loading screen, against the blocks as drawn at full
  resolution; rig QA `hand-hud-large` checks the GUI captures against the logged cuts.
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
- **Fallbacks.** No controllers, a stale controller snapshot, no grip space, or the corner blocks not
  measured yet: the whole HUD stays on the head-locked quad as in panel mode (logged once). Loading screens
  and menus are unchanged.
- **Head-locked HUD.** `ETERNALVR_HUD=panel` (the default; launcher: "Health and ammo" = On the HUD panel) is
  the earlier behaviour, unchanged.

## The weapon HUD

The public README's planned "health, armour and ammo on your wrist or weapon": the weapon half, ammo on
the gun the way a VR shooter shows it, read with a glance at the hand that holds it.

```
XR worker, every XR frame (presenter_wrist.cpp, the same path as the wrist)
  UI quad filled as before; weapon mode, no menu panel up (nor held), controllers attached?
    head-locked pieces: the band minus the bottom-right corner (ammo block) where the game drew it; with
      ETERNALVR_WEAPON_HUD_VITALS=1 minus both corners, as on the wrist
    the gun's frame: the weapon hand's grip position with its aim orientation (the viewmodel's)
    facing test: the panel's normal and the head (controller snapshot, room space)
      facing angle <= 60 deg -> shown; > 75 deg -> hidden (no gaze test)
    shown (or fading): one quad (two with the vitals) in the weapon hand's aim space
```

- **What moves to the gun.** The HUD's bottom-right block: the ammo count, the equipment (grenade, its
  cooldown) and the flame belch, cropped from the GUI target where the game drew it, exactly as the wrist
  does (`hud_blocks.hpp`).
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
  the controller. The same under head aim, where the gun is in front of the face (docs/VR_CONTROLLERS.md,
  "Head aim and the gun"): the ammo stays on the weapon hand.

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

### When the arms go back to the game

The arm policy hands an arm back to the game (`untracked`) when the hands' poses it is posed from are not
current: the weapon hand and the head (the weapon arm's shoulder and its gun), the off hand, and the arms
model's placement at the weapon hand. Under head aim the model is never placed at the hand (the gun is in
front of the face, docs/VR_CONTROLLERS.md "Head aim and the gun", DECISIONS T-143), so both arms are the
game's there, as in gamepad mode. The camera hook writes the hands for each view of the game, at the
runtime's pose predicted for when that frame is shown, and the viewmodel hook places the arms model at each
tick of the game's hands; the hooks of the following tick read both (`vkcore/view_poses.cpp`). Until
2026-10-19 a pose counted as current while it was at most 0.1 s old at the read. That age is the gap since
the last view, not the pose's age: a single frame longer than 0.1 s (a hitch) made the newest poses the
runtime had given look stale, and every arm went back to the game's animation for about 0.45 s (handed
back over 0.05 s, the 0.25 s hold, eased in over 0.15 s). On the Chaingun and the Ballista the game's right
hand sits on a top handle about 0.6 m away and turned about 78 degrees, so the glove slid off the grip and
back; below 10 fps every frame did it.

Now (`features/input/view_pose_clock.hpp`) a pose is current while the views keep coming: written after the
tick before the last one, however long the frames were, or at most 0.1 s old (`kWorldStaleSeconds`) once
they stop (no camera hook: a menu, the session losing focus), and never older than 1 s
(`kWorldHitchCapSeconds`, longer than any single frame the rig logged in play, 891 ms at most): when the views
and the ticks both stop (a load, a freeze), a shot or the off hand read before the first tick after it does
not take the pose from before it, and a hitch over 1 s hands the arms back as before. The viewmodel hook
counts every tick of the local player's hands (it checks the owner first), forced views included, so the old
poses of a stopped camera hook still age out by ticks. Two ticks back, so the rule holds whether the off-hand
hook reads before or after the viewmodel hook within a tick. Without the viewmodel hook
(`ETERNALVR_VIEWMODEL=0`, or its install checks failed) nothing counts the ticks and the rule is the age
alone, as before this change; the off-hand hook, which poses both arms, is then not installed at all
(`controllers.cpp` installs it only with the viewmodel hook), so that leaves the shots and the equipment
launches on the age rule. If the game ran `idHands::UpdatePosition` more than once a tick for the player's
hands, the window would shrink and a hitch could hand the arms back again [unverified: the rig logs do not
count its calls per frame; the scripted hitches below kept the arms in 12 of 12]. The same rule
covers the gun's placement (the viewmodel hook), the shots and the equipment launches (aim and launch
hooks). A lost controller does not go through this: the runtime's location flags (orientation and position
valid) make its pose invalid at once and the arm goes back that tick, as before. What the player sees on
the first frame after a hitch is the hand where the runtime predicted it for the frame before (no newer
pose exists then); the next view has the new one.

On the rig (docs/rig-findings/hands-rig.md, section 14) scripted hitches of 150 and 400 ms with the Chaingun
and the Heavy Cannon held handed both arms back in 12 of 12 on dev main and in none now, the weapon arm at
weight 1 and the off hand's written wrist on its target through all of them. Posing through a hitch also
needed the arms' clean read to count ticks (a read counts as clean only 150 ms and 3 ticks after the last
write, on both arms; "The whole arm" below): with the time alone, the off hand's glove on the Heavy Cannon
ended up to 36 cm off the controller after six hitches.

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
  run away (bone lengths of several metres in ik4); for 150 ms and 3 ticks after the last override the
  layer's joints are taken instead as the last read without overrides placed them relative to `LeftHand`,
  which the overrides do not move. An override read back more than 0.1 from where it was written for 30 ticks
  in a row stops the arm for the session (`offhand: arm: the layer's overrides do not land where written
  ...`). **The mix** [static: InitJointMods 0x138B080 +0xB0 hands the modifier node's create (0x497E00) the
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
  skeleton change (a new level, a skin); without `bind` they stay as they were. A read counts as clean only
  150 ms and 3 ticks after the last write, on both arms (the time rule alone took a read the overrides still
  showed in after a hitch: with the wrist's change first; from 2026-10-19, when the arms keep posing through
  a hitch, the off hand's arm on the Heavy Cannon too: its reach shrank from 0.74 to 0.35 over six scripted
  hitches of 150 and 400 ms, and the glove was left up to 36 cm off the controller). The weapon arm's wrist
  and attach joint stay the game's: its wrist as read is taken as the parent of the first roll joint, an
  approximation, since the read has the game's attach change (the weapon lag) mixed in at the weight and the
  joints are mixed from the wrist with it whole, so they land off by (1 - weight) of that change (1.4 mm
  median, 1.8 at most on the rig with nlerp, `r4-bind2`; the 1.5 and 4.4 of `ap2-comp2` included the slerp
  model's own error). The read-back is measured against the target (`landed`).
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
  override (0x20) replaces the joint's model-space pose. Flag 1 is what makes a modifier model-space
  [static: 0x19E1040 tests it per entry, and the Game Pass build's pass, at 0x19EC8A0, the same; SetJointMod
  stores the flags as given]: without it the modifier is in the joint's parent's frame, an override's
  rotation, scale and translation becoming the joint's pose under its parent as they are, so it rides the
  parent's pose of the frame the game draws (`features/arm/parent_frames.hpp`; the glove on a grip, below). SetJointMod makes the quaternion from the matrix it
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

- **The arm.** The arms model is never mirrored. With the weapon in the right hand the weapon arm is the
  model's right arm; with the weapon in the left hand the model's left arm holds the gun and its right arm
  is the off hand ("The weapon in the left hand", below). Its joints are found the off hand's way from the game's right attach
  joint: `righthandattach` 2 carries `RightHand` 3, then `rightforearmroll3`, `rightforearmroll2`,
  `rightforearmroll1`, `RightForeArmRoll`, `RightForeArm` and `RightArm`, 24 to 29 [static: marine.md6skl
  and arms.md6skl, read out of `gameresources.resources`; no device hangs from the right forearm]. The
  right arm's bind pose is the left's reflected, each joint's axes negated (`features/arm/arm_joints.hpp`);
  the solver uses only the arm's own geometry, so it serves both (unit-tested: the right arm solves as the
  left's mirror image). The joint's index is the one `InitJointMods` stores at idHands+0x28E6 for the name
  handle of "righthandattach"; install checks those instructions and the name (or logs `weapon arm:
  InitJointMods does not store the joint named righthandattach ...; the weapon arm stays the game's`).
  The names are checked by name handle, and here a name table that cannot be used refuses the arm.
- **The wrist stays** on every gun but two. The gun, its attach joint, the game's modifier of it and the
  wrist are left exactly as the game animates them. The four roll joints, `RightForeArm` and `RightArm` get
  the layer's modifiers, as for the off hand; the chaingun and the ballista put the glove's wrist and fingers
  on a grip instead ("The glove on the grip", below), and for that the weapon arm's block in the hands'
  modifier list is 22: the wrist's (first), the six, then the fifteen finger bones, each no change unless the
  glove is on a grip.
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
  name handles): righthandattach 2, RightHand 3, rightforearmroll3 24, ...`, `weapon arm: the glove's joints
  (names checked with the game's name handles): RightHandThumb1 4 .. RightHandPinky3 22`, `weapon arm: 22
  joint modifiers added at ...` and `weapon arm: upper arm ..., forearm ... ; the first pose moves the arm up
  to M from the game's`. A refusal logs once `weapon arm: <reason>; the weapon arm stays the game's`. With
  `ETERNALVR_OFFHAND_TRACE=1`: `weapon arm: ik (controller)` or `game (<reason>)` as the policy changes,
  and once a second `weapon arm: ik: wrist (...), elbow (...), shoulder (...), D from the head's point,
  reach R, twist T deg, weight W, up to M from the game's pose, read back within L; N rejected; right arm
  surface S; glove G (grip g, fingers f)`. `M` is how
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
  animation (recoil) the sleeve may trail the glove by a frame (on every gun but the two whose glove is on a
  grip, where the forearm is written in its joints' parents' frames and rides the glove: "In the gun's frame",
  below). First rig runs (2026-10-02, simulator,
  `right.position = 0.15, -0.25, -0.35`): joints found, modifiers added, 0 rejected, overrides read back,
  but the picture was the same as with `game`, even with the arm across the view: the weapon's mesh kit
  had the arm switched off (below). Showing it is not yet confirmed on the rig.

### The glove on the grip

**Why two guns had no glove in the hand** [static: the idle animations, meshes and skeleton of build 25216728,
decoded from `gameresources.resources`; rig: the hands rig]. The gun hangs from `righthandattach`, the
weapon's root (the weapon model is placed at it, as the hands model's hammer prop is, `parent
"righthandattach"`; the decoded weapon meshes put each gun's grip under the right hand's fist in that frame;
the hands rig reads it as the gun's pose). The glove's wrist (`RightHand`) is that joint's child and the
fingers the wrist's, so a modifier on the wrist moves the glove and leaves the gun. The other six guns' idles
hold the gun with the glove's wrist 13 to 20 cm from the root and the hand within 7 degrees of the Heavy
Cannon's grip, the fist around the gun's pistol grip (on the Heavy Cannon, the combat shotgun and the
chaingun's zoom grip the decoded mesh is 0.3 to 0.4 cm from the finger joints).
The chaingun's and the ballista's idles hold the gun by its rear top handle instead (`chaingun_base_handle_rear`,
`gauss_base_handle`: a bar along the gun at its back, beside the camera in flat play): the wrist at (-0.79,
-0.04, 0.28) and (-0.56, -0.03, 0.20) m in the root's frame, 84 and 59 cm from it, the hand turned 78 and 73
degrees from the other guns' grip, the fist 1.5 and 1.2 cm from the handle's mesh. The chaingun's idle,
shooting, turret, bring-up and switch animations all keep it there; its zoom and reload put the hand on the
gun's pistol grip in exactly the Heavy Cannon's idle grip (wrist and fingers to 3e-6). No ballista animation
holds anything else (its shots move the hand 10 to 20 cm), and its mesh kit (`HideAll`) draws no arm in flat
play. Placed at a controller by the hand-grip rows (the gun's pistol-grip point in the hand), that glove sat 72
and 48 cm from the real hand (rig `hg-grip3`, dev main) and, put in the hand, it would be turned across the
controller's grip.

**What the layer does** (`features/arm/grip_hand.hpp`, `vkcore/weapon_arm.cpp`; the weapons by data:
`[right_grip]` in `data/weapons/viewmodel_offsets.toml`). For a weapon with a `[right_grip]` row the weapon
arm puts the glove's wrist on the row's grip, held at that pose in the weapon's root frame, so the glove rides
the gun's own animation as it does on every other gun, and the fifteen finger bones close as that grip's do.
The grip is the game's own: `"heavy_cannon"`, the Heavy Cannon's idle right hand, which is how the chaingun's
zoom and reload hold the chaingun. The ballista has no pistol grip (its mesh has none: nothing within 7 cm of
the fist at the Heavy Cannon's offset, which is inside its stock), so its row moves that grip's wrist to
(-0.302, -0.039, -0.181) m, where the fist closes on the ridge under its stock; that placement is a choice
(DECISIONS T-138). The `[standing]` rows of both then put the glove's wrist on the real hand like every
other gun's: 8 cm behind the controller grip's centre and 3.5 cm toward the back of the hand, fixed in the
grip's frame. The gun itself is never written: its root joint and the game's modifier of it stay the game's.
The forearm, elbow and upper arm reach the wrist by the same IK as on every gun.

- **Written past the target.** The hands' modifier node mixes each modifier in its joint's parent's frame at
  its weight (0.95; "Where an override lands"), so the wrist and fingers are written past their targets from
  their animated poses, which the layer's own overrides hide once they show. The fingers' are taken from the
  last read the overrides had not reached (the handle hand's fingers do not move in these idles); the
  wrist's is taken back out of each read with the override written for it (`arm::WristTracker`), so the
  glove stays put while the game's own wrist moves under it (the chaingun's zoom takes it onto the grip in
  0.33 s; written past a stale pose the glove would land up to 3.4 cm off). That estimate is taken only
  while the overrides are steady (each within 3 mm and 0.5 degrees of the one before); otherwise the last
  one is kept. The read is taken to show the override of the tick before: in every read the rig could tell
  apart (the overrides more than 1 cm apart, during the glove's blend in) it showed that one, never an older
  one (`ga-lag1`: 36 of 36). A read two frames behind, or a dropped frame, still lands the glove within
  3 mm while the game's wrist moves under it (unit tests). In the default `animated` solve the forearm's
  overrides are placed under the wrist's override as written, so they mix as on every gun. The hands'
  modifier weight is read every tick, the current weight and the one it fades toward (both checked at
  install on both builds); without them, or before the first clean read, the glove is not posed and stays
  the game's.
- **A fading weight.** The blend mixes at the current weight, which the game fades toward its target, so a
  glove written past by the weight read can land past its target when the blend runs further along the
  fade. The glove is always written past by the larger of the current and the target weight (not past at all
  when that is below 0.5), so it lands short when the blend mixes at less and never beyond. Only while the two
  are within 0.01 (settled) does the tracker take reads back out; while they are not, the glove eases off the
  grip over `ETERNALVR_OFFHAND_BLEND`, and back on once the weight settles. The game's own change of weight
  between ticks is not eased: where a rising fade starts (the target jumps above the current weight) the glove
  lands short at once by 1 - current / target of the way, and where the weight drops below 0.5 it lands at
  that weight's share at once; from the next tick it eases. Unit tests: a fade from a settled 0.95 to 0.6 and
  back, and a rise from a settled 0.6 to 0.95, each mixed a tick further along than read, never land past the
  grip; a target 0.009 above a settled current of 0.6 neither; a weight of 0.4 for 20 ticks eases the glove
  off and back after the game's own drop. The rig never saw the weight away from 0.95.
- **Released once.** When the arm goes back to the game the layer writes its modifiers as no change for
  three ticks, then not again until it next poses the arm, both arms (it wrote all 22 every tick before, even
  with `ETERNALVR_WEAPON_ARM=game` once the arm had been posed). Three cover both lists: SetJointMod writes
  the list the node's generation's low bit picks [static: its bytes, checked at install], and the game steps
  the generation by one every tick [rig `ga-trip3`: the low bit switched in 3346 of 3346 ticks on one node,
  every step 1]. After a multiplayer guard trip no tick runs again, so the give-back writes both lists at once
  (`offhand_mods::clearLayerMods`), both arms, into memory with no call into the game (so it also runs after
  a game call faulted). In the run that measured it (`ga-trip3`, a build that first made one round of
  SetJointMod) that round left all 22 of the weapon arm's overrides in the other list (0 and 22 per list),
  and both lists then held none; the trip now logs the overrides left per list after its write. Unit test:
  the trip's writes put every modifier once into each list and leave no override in either; written to one
  list only, it fails.
- **Blends.** The glove's grip weight eases in and out over `ETERNALVR_OFFHAND_BLEND` as the held weapon has
  a row or not (the last row's grip is kept while it eases out), and the arm policy's weight (every
  hand-back) mixes it like the rest of the arm, the fingers too. On a switch the weapon's kit then hides the
  right arm, glove included, as on every gun, until the arm comes back on the new gun. Holding the weapon mod
  does not hand the arm back on either gun (rig); switching the mod does, for about 2 s. A frame longer than
  0.1 s no longer hands it back ("When the arms go back to the game" above; until 2026-10-19 the glove then
  eased off the grip toward the game's handle hand and back within about 0.45 s).
- **The left glove** is unchanged: with the free off hand (the baseline) it follows the left controller and
  is shown on the ballista, whose kit hides both arms; with `ETERNALVR_OFFHAND=game` the game's left glove
  holds the chaingun's front handle as in flat play, and the ballista has none [rig: `ga-view2`].
- **Logs.** At start-up `weapon arm: room for the glove's modifiers too (its wrist and 15 finger joints), to
  put it on a grip for a weapon whose idle holds it by a handle turned away from a controller's grip`; on the
  first pose `weapon arm: the glove's joints (names checked with the game's name handles): RightHandThumb1 4
  .. RightHandPinky3 22` and `offhand: arm: the hands' modifier weight 0.950 ...`; when such a weapon comes up
  `weapon arm: 'weapon/player/chaingun' holds its gun by a handle turned away from a controller's grip: the
  glove goes on a grip in the Heavy Cannon's idle grip ...`. With `ETERNALVR_OFFHAND_TRACE=1` the pose line
  ends `glove on the grip (grip G, fingers F)` or `glove the game's`, and every tick the glove was written
  the tick before (up to 6000 lines) `weapon arm: glove tick: wrist W mm off in the gun's frame, the sleeve's
  end S mm off the glove, the shoulder end H mm off; the gun's root moved G mm at the grip (R mm at the root,
  turned T deg) from the read the write was placed under`.
- **Rig** (OpenXR-Simulator, the grip sweep, five holds and a view hold per gun; docs/rig-findings/hands-rig.md,
  section 11): the glove's wrist from its target, median / largest, chaingun 72.3 / 72.5 cm before (dev main)
  and 0.1 / 0.3 after, ballista 48.0 / 48.2 and 0.2 / 0.5, the other six unchanged (0.1 to 0.2 / 0.2 to 0.3);
  the sights check passes (largest 0.20 degrees on both); the merge-QA hands check (combat shotgun) has no
  regression against its baseline. The captures show the forearm going into the chaingun's grip from below
  (the fist inside the grip's housing) and the fist closed under the ballista's stock.
- **In the gun's frame** (`features/arm/parent_frames.hpp`). With the glove on a grip all 22 of the weapon
  arm's modifiers are overrides in their joint's parent's frame (flags 0x22A, the layer's 0x22B without flag 1;
  "How the modifiers work"): the wrist in the gun's root joint's frame, each finger under its parent, each
  forearm joint under the one before it and the first under the glove's wrist, with the poses the layer has
  always computed, taken relative to their parents as written. Written in model space under the root joint
  as read (the last frame's), the glove landed where the gun had been wherever the game's animation moved
  the gun within a frame: on the rig (docs/rig-findings/hands-rig.md, section 13), per tick, the glove's miss
  in the gun's frame was the root's move at the grip between the read and the frame drawn times the hands'
  modifier weight (correlation 1.000, slope 0.95 to 0.97), up to 17.1 mm while the chaingun fires, 33.5 mm as
  its mod comes up and 151.3 mm in the ballista's first shot. Held in the parents' frames, the game places
  the glove by the gun's pose of the frame it draws: 0.0 mm in every phase on the chaingun, and on the
  ballista 0.0 mm median and 1.6 at the 95th percentile while firing. What is left is the blend's share of
  the game's own hand under the glove (1 - 0.95 of how far the ballista's animation moves its handle hand in
  the frame of a shot, which the read shows a frame late): 4 of 259 firing ticks over 5 mm, in the two
  shots' first frames, the largest 15.0 mm; and, as before the change, up to 10.1 mm for a few ticks as the
  ballista's animation moves its hand after the shots while the wrist tracker waits for steady overrides. The
  forearm, elbow and upper arm ride the glove and the gun for that frame, as the game's own arm does (the arm
  hangs from the attach joint), so the shoulder end moves with the ballista's kick by up to 17 cm for a frame
  before the next tick's solve takes it back to the head's point; the sleeve's end stays on the glove (2.9 mm
  at most, as before). The landing guard measures each joint against where it was meant to land carried by
  the root joint read then. The other six guns, whose glove is the game's, write as before. Only a uniform
  model scale divides out of an offset in a parent's frame (the scale is along the model's axes), so with a
  scale whose three axes differ (by more than 1e-4 of the largest) the glove's modifiers are written in model
  space as before, trailing a fast gun by a frame but landing where written (unit test: at (1, 1.2, 1) the
  glove would land 18 mm off in the parent's frame); the rig reads 1, 1, 1. Not run: `ETERNALVR_ARM_SOLVE=bind`
  with either gun (its forearm is written past its targets, `modifiedFor`, and taken into the parents' frames
  the same way; untested on the rig).
- **Not verified.** In a headset; the ballista's ridge as a grip (the game never holds the ballista this
  way); which of each gun's mods the rig's save had equipped; melee and glory kills on these two guns (the
  same policy hand-back as a switch, not run).

### The other held items

[rig: the hands rig, docs/rig-findings/hands-rig.md, section 12; static: the arm policy's code] The
**Unmaykr**'s idle holds it with the guns' grip (the glove 3.6 degrees from the Heavy Cannon's, at the guns'
offset from the root joint), so its `[standing]` row is measured like theirs: the glove's wrist 13.8 cm from
the real hand before, 0.2 cm after. The **Crucible** comes out as a held item (its button, then it idles until
a switch) and is held one-handed in the right fist: the hilt hangs from the root joint as a gun does, the fist
closed on it just under the guard, turned 37 degrees from the guns' grip; the game's left glove hangs behind
it, not on it. Nothing on it aims (its swing hands the arm back to the game: the weight was 0 at the next of
the rig's dump lines, 0.25 s apart, and came back 1.5 s later),
so its row turns the whole model (pitch -24.39, yaw -23.75, roll -10.59) until the glove sits on the
controller as on the guns, and the hilt stays in the fist exactly as the game holds it (DECISIONS T-139): the
wrist 40.1 cm off before, 0.4 cm after, the glove 37.0 degrees from the guns' grip before, 0.1 after. The game's
swing then plays in the turned model. With the free off hand the left glove follows the left controller as on
every gun; the Crucible stays in the right hand.

The rest stays on the `default` row because the arm is the game's while they are out: the **chainsaw**'s
button is a quick action (its held item for 0.8 s, the arm handed back for all of it: its hands states and
actions are in the policy's list), a **melee**'s fists are out for 0.5 s, handed back the same way, and the
**Sentinel Hammer**'s throw and slam are actions of the hands the policy hands back (`HammerThrow`,
`HammerSlam`); the rig did not bring the hammer out (the Crucible swapped for it, its button changed nothing on
e1m1, nor on the first level of The Ancient Gods, Part Two: hands-rig.md, section 16). Not seen on the rig: the
Berserk's fists (the console's give and spawn put none in the hands) and the chainsaw as a weapon outside an
inventory emptied by the console.

The **swim's fists** (`weapon/player/fists_swim`, in the hands while the player is under water) are held, not
an action: the layer poses both arms as on a gun (the free off hand's left glove on the left controller). On
the `default` row the right glove's wrist sat 26.4 cm (median; 31.0 at most) ahead of and right of the real
hand; their own row puts it there by the guns' rule, measured from the swim idle's median (hands-rig.md, section
16): 4.7 cm (median; 6.3 at most) on the rig after. The game's swim stroke keeps moving the glove about that
point, up to 11 cm (5.7 median), mostly side to side: the glove follows the stroke as it follows a gun's sway.
Going under water the model's placement changes by 34 cm with the row (8 cm on `default`) while the game has the
arms for the swap. With the weapon in the left hand the left glove sits 11.0 cm (median) from the left hand: its
grip is read at one moment of the stroke. Not chosen: the glove held on the controller
while swimming (the stroke dropped; the game's left glove, with the default off hand, would still swim).

**At the water's surface the arms swim with the gun in the slot** (`features/input/offhand_policy.hpp`, armsItem;
`vkcore/arms_item.cpp`; DECISIONS T-142; hands-rig.md, section 17). Floating at the surface the game puts the gun back
in the right hand's slot now and then while the arms keep the swim's pose: the gun's root, which the gun hangs from,
stays where the swim parks it, about 1.5 m from each glove behind the camera (the left attach joint with it, to
0.0 mm), so no gun shows but for a few frames as the game raises or lowers it, and the trigger fires nothing [rig].
Placed by the gun's row, the swim's right glove sat 28.3 to 34.6 cm (the median of the settled dump lines) from the real
hand; with the weapon in the left hand the left glove was held on the gun's parked root, 1.3 m behind the left hand.
While the hands swim (the hands' flag `swimming`, bit 35 of idHands::handsFlags, or their state HS_SWIM_IDLE [static:
the reflected type info; rig: set through every swim and never on land]) the arms are placed and held as the swim's
fists' whatever the slot holds: their row, their grips with the weapon in the left hand (the player's own swim fists,
idPlayer::weaponFistsSwim, checked by name), and the view follows the head as for the swim's fists. The item is read
once per tick of the hands, by the viewmodel hook, and the rows, the grips and the arm dump all go by that one answer
(`vkcore/controllers_impl.hpp`, ArmsTick). After: the right glove 5.7 cm (median, 17.1 at most once settled) from the
real hand, as with the swim's fists, the left glove 11.2 / 12.4 cm (from 127.8), and no step of the model at the changes
of the held item in the water (before: one of the rows' difference, 28 to 55 cm, at each). The game's own raise or lower
at those changes, a few frames with the gun in view while the game has the arms, is now placed by the swim's row too, so
the gun shows there up to 34 cm from where the gun's row would put it. The game's grips of the held item (the weapon in
the left hand) are read only while the arms hold that item, the hands do not swim, and they last swam 1 s ago or more
(input::gripTakingAllowed): the swim's pose and the hands' signals change a few ticks apart, and a grip read from the
swim's pose is kept as the gun's, by name, across levels. At a dive the hands swim a tick before their pose leaves the
gun's, and a grip read then held the left glove 1.3 m off through the whole swim (rig, a build without the rule). On the
way out the 1 s outlasts the hands' signal's clear spells in the water (a lower: one dump line, 0.25 s) and is as long as
the game's longest raise after a teleport onto land (0.5 to 1.0 s) [rig]; a way out by swimming to a shore or climbing out has not been
run. Logs: `controllers: the hands swim with '<item>' held: the arms are placed and held as the swim fists'` as that
starts (up to 40 lines); `controllers: the hands swim ('<item>' held)` at a dive and `controllers: the hands have not
swum for 1.0 s ('<item>' held): the arms' grips of the game's are read again` on the way out (up to 40 lines);
`controllers: idPlayer::weaponFistsSwim reads as '<name>', not the swim fists: the arms keep the held item's row` once
per pointer if the field holds another item; with the weapon in the left hand, `left-handed arms: the <right|left> arm's
grip of '<item>' kept, read with the gun's root at (x y z) m (model)` when a kept grip's item changes or its root moves
1 cm (up to 80 lines).

**Quick actions and the row.** A melee or the chainsaw's button puts its own item in the hands, and the model's
placement takes that item's row (`default`) at once: 8.3 cm and 2.2 degrees from the combat shotgun's, 38.1 cm
from the chaingun's, 48.7 cm and 37 degrees from the Crucible's (rig, the dump's placement at the change). The
arms are the game's for the whole action (the dump's weights 0), and the captures every 12 frames through a
Crucible melee show the game's punch, then the Crucible back in the right fist within about 0.7 s; the frame of
the change itself was not captured, and the dump holds no joints while the game has the arm, so whether the
change shows as a jump is not measured.

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

## The weapon in the left hand

With the weapon in the left hand (`ETERNALVR_HANDEDNESS=left` or `left_mirror`) and the weapon arm on (the
default), the Slayer's left glove holds the gun in the left hand and his right arm is the off hand
(`vkcore/left_hand_arms.cpp`, `features/arm/left_hand.hpp`; DECISIONS T-140; GitHub issue #23). Before, the
arms model's right arm held the gun on the left controller and stretched across the view to it, and its left
arm floated beside it or reached across for the right controller.

**Why it works without mirroring the model** [static and rig]. The gun hangs from the right attach joint
(`righthandattach`, the weapon's root; "The glove on the grip"). The left attach joint (`lefthandattach`, the
left arm's) is posed with it: in the hands rig's arm dumps, every tick the game had the left arm, the two read
the same pose to 0.0 mm and 0.0000 in their axes, the game's weapon-lag modifier included, on the combat
shotgun, the Heavy Cannon, the chaingun, the ballista, the BFG and the Crucible, in their idles and their shots;
the bind pose has them at the same point too [static: marine.md6skl]. The chaingun's mod is the one animation
found that parts them (below). So the left glove, the left attach joint's child, can be held in that
frame exactly as the right glove is on a grip ("In the gun's frame"): it rides the gun's pose of the frame the
game draws. Each arm takes the other's role in the gun's root frame, reflected across the root's x-z plane (the
gun's middle: the root's y axis is the model's left to 0.991 or better on every gun dumped), and the
reflection keeps each joint's frame right-handed the way the skeleton does (the left arm's bind pose is the
right's reflected with each joint's axes negated, the hand and fingers to 0.5 mm [static]): a joint held in
the root's frame takes the position reflected and each axis reflected and negated; a hand joint held in its
parent's frame keeps its turn, its offset pointing the other way (unit-tested against the bind pose of both
arms and the Heavy Cannon's grip).

**What the layer does.**

- **The left glove holds the gun.** Its wrist and fifteen finger bones take the game's right grip of the held
  item, reflected (the `[right_grip]` row's grip on the chaingun and the ballista, reflected the same way),
  written in their parents' frames like the right glove on a grip; the forearm, elbow and upper arm reach it
  by the weapon arm's IK from a shoulder beside the head on the left (`ETERNALVR_OFFHAND_SHOULDER` and
  `_ELBOW` as given). The game's modifier of the left attach joint (the weapon lag) is left as it is. The
  grip is in the gun's root frame and the glove is written in the left attach joint's, so each tick the
  grip is placed on the gun's root as read: the same pose wherever the game poses the two joints together,
  and the left glove stays on the gun where it does not (the chaingun's mod moves the left attach joint 13
  to 25 cm from the gun's root: the glove 0.0 mm median and 13.2 at most off its grip in the gun's frame over
  the mod's 2.5 s, 373.7 mm without the placement on the root; rig `lf-fire3`, `lf-fire2`). The gun is not
  mirrored: its markings read the right way round, and its sight line and shots are the same as in the right
  hand.
- **The right arm is the off hand.** With `ETERNALVR_OFFHAND=game` (the default) the right glove takes the
  game's left grip of the held item, reflected (on the combat shotgun's fore-end), held in the gun's frame, its
  forearm reaching a shoulder on the right; on the chaingun, whose front handle is on its left side only, it
  follows the right controller instead (below). That grip is read under the left attach joint and taken onto the gun's root only where the two
  were read within 1 cm and 2 degrees of each other (`arm::onGunRoot`); a grip read while the game held them
  apart is read again. With `free` the right glove follows the
  right controller: the left hand's wrist rule reflected (8 cm behind the grip's centre, 3.5 cm toward the
  back of the hand, `ETERNALVR_OFFHAND_OFFSET` as given for the left hand), written in model space so the
  gun's kick does not move it, its fingers as the game's right hand holds them. The right arm's surface is
  shown while the layer poses it, as the weapon arm's always was.
- **The gun in the left hand.** The viewmodel rows are measured for the right glove. For the left glove the
  model is placed by the same row, its turn reflected (the yaw and roll change sign; only the Crucible's row
  turns the model), then moved, not turned, until the left glove's wrist, where its grip puts it on the gun as
  the grip was read, sits on the left hand's wrist point in the grip's own frame (8 cm behind its centre,
  3.5 cm toward the back of the left hand; `arm::leftGloveShift`). A gun's sight line is its own, so the
  sights turn is the same as in the right hand. The move applies once the left glove's wrist on the held item
  is known (from the first switch to it) and stays while the game has the arms (a switch, a glory kill, a
  melee), so the gun stays on the left hand; the game's right glove then holds it 0.8 to 2.4 cm from where the
  right glove's row would put it. Following the arms' hand-backs instead, the gun slid by that much each time
  (rig `lf-hb1`; hands-rig.md, section 15). A first version reflected the row across the plane of the model's forward and
  up axes through the gun's root instead: the root is turned up to 2.2 degrees from the model's axes (the
  combat shotgun's, the gun's own sight line), so the left glove landed up to 2.4 cm off (rig `lf-free4`).
- **The grips are read, not stored.** Each grip is the game's own, read from its arm while the layer's
  overrides are off it (a clean read: 150 ms and 3 ticks after the last write) and taken once the gun's root
  has held still for 3 reads in a row (within 0.5 mm and 0.2 degrees, `arm::SteadyPose`), kept per held item
  for the hands it was read on and by the item's decl name across new hands (`arm::GripBook`): the game's
  animations are the same on every level, so after a level load each item is held with the grip read on the
  level before until one is read on the new hands (which replaces it). New hands forget which item each decl
  pointer named, which a new level may give to another item; a name that does not read (empty) is not
  learned and is read again. A kept grip is used only while the gun's root as read last sits where the grip
  was read (within 1 cm and 2 degrees, `arm::sameRootPose`): a weapon mod that holds the gun elsewhere waits
  for a read on the new hands, both arms the game's until then. Taken from any clean read, a grip read while the gun was still coming back up
  after a melee put the gun 8 cm off the left hand until the next switch (rig `lf-hb1`, `lf-hb2`). Until both
  arms have one for the held item (the right arm's only with the free off hand) both arms are the game's (`no
  grip read yet`); the first switch to a weapon hands both arms back anyway. Until then the gun sits at the
  right glove's row: on each item's first equip of the session, the grips were read
  0.38 to 0.42 s after the held item changed (rig `lf-hb3`), and the gun then moves at once by the difference
  of the rows, 0.8 to 2.4 cm by gun, while the arms blend in; after a level load the held items take their kept
  grips at once (rig `hc-d-lvl`: the Crucible's first raise on the reloaded level placed in the left hand from its
  first frame, 68.5 cm and 59 degrees on the level before; the combat shotgun's kept grip 0.5 cm from the one read
  on the new hands). Each still read replaces a grip; a weapon mod
  that moved the gun's root in the model would keep the grip read before it until the next hand-back with a
  still read. A held item whose reads never hold still keeps both arms the game's and is logged once
  (`... grip of '<item>' not taken: ... the last moved ...`), as is an item past the 24 grips kept per arm. The arm policy decides for both as for the weapon arm, and for the free off hand as for the
  off hand.
- **Modifiers.** Both arms have the glove's 22 (the wrist, the six, the fifteen fingers), 44 in all
  (`offhand: arm: ... both gloves (the weapon in the left hand)`); the left arm has no wrist change of the
  bind solve. Right-handed play makes exactly the room it did before.
- **Rig** (OpenXR-Simulator, the hands rig's per-weapon sweep with the weapon in the left hand: every hold
  mirrored onto the left controller, the right controller at rest; docs/rig-findings/hands-rig.md, section 15).
  The left glove's wrist from the left hand's wrist point, median / largest: 0.1 to 0.3 / 0.2 to 0.4 cm on
  all eight guns (`lf-free5`); the right glove on the right controller with the free off hand: 0.0 / 0.0 cm on
  all eight; the sights check passes (largest 0.20 degrees on a gun with a sight row, the same as in the right
  hand). Firing (`lf-fire3`, the default off hand), the left glove's wrist in the gun's frame, median / 95th
  percentile / largest: combat shotgun 0.0 / 0.6 / 1.0 mm while the gun's root moved up to 41 mm at the grip,
  chaingun 0.0 / 0.0 / 0.1 mm (root 20 mm), ballista 0.0 / 0.0 / 3.3 mm (root 156 mm): the left attach joint
  moves with the gun in the shots too. Captures show the left glove's fist closed on the combat shotgun's
  pistol grip and the right glove free in the right hand, and with the default off hand the right glove on the
  shotgun's fore-end. Right-handed play is unchanged: the grip sweep within 0.1 cm and the sights within 0.01
  degree of dev main's, the merge-QA hands check 0 regressions.
- **The chaingun's free hand** (DECISIONS T-141; `[left_handed_off_hand]` in
  `data/weapons/viewmodel_offsets.toml`). The game's left glove holds the chaingun by a front handle on its
  left side only: reflected, the right glove's fist closed beside the gun's right side on nothing [static: the
  chaingun's idle and mesh, the right glove's finger joints 10 to 17 cm from the gun's mesh, 13.1 median,
  against 2.5 to 6.1 as the game's left glove holds the handle; on every other gun the reflected grip is as
  close to the mesh as the game's, the meshes being symmetric there; rig: the capture of the fist beside the
  gun]. With the default off hand the right glove follows the right controller on the chaingun, as the free
  off hand does on every gun (the free off hand's rules decide when, its fingers the game's right hand's).
- **Grips only from the item's own hold.** The game's grips are read only while the weapon arm's rules have no
  reason of their own to give the arms to the game (`input::readsGameGrip`): not during a raise, a switch, a
  melee's fists or a quick action, whose pose is the action's. Read then, a grip moved the whole model while the
  game had the arms (rig `hc-lf1`, the default off hand): 16.6 to 19.9 cm three times through the Unmaykr's first
  raise, 66.9 cm and 59 degrees through the Crucible's, and at each later melee by 53.8 cm (from the combat
  shotgun) and 87.4 cm (from the Crucible) where the right hand's step is 8.3 and 48.7 cm (the melee's fists
  kept a grip of their own, read from the punch). After: steps only at an item change, the rows' difference as
  in the right hand (a melee from the combat shotgun 9.4 cm). Still: the Crucible's first raise of a session
  moves the model 68.6 cm and 59 degrees as its grip is read, 0.25 s before the arms blend in; on later levels
  its kept grip places it from the first frame (hands-rig.md, section 16).
- **Rig, the rest of the held items** (hands-rig.md, section 16; the default off hand). The left glove's wrist
  from the left hand's wrist point, median / largest: Unmaykr 0.2 / 0.3 cm, Crucible 0.2 / 0.5 (its row's turn
  reflected), chaingun 0.2 / 0.3, combat shotgun 0.1 / 0.2. At the water's surface with the gun in the slot the
  left glove held as on the swim's fists (11.2 / 12.4 cm; 127.8 / 137.5 before: "At the water's surface" above). The
  swim's fists: 11.0 / 12.4 cm, the stroke about a
  grip read at one moment of it (the right hand's figures above are about the stroke's median). Melee and the
  chainsaw's button hand both arms back for the whole action, as in the right hand.
- **Not verified.** In a headset; glory kills with the weapon in the left hand [static: a forced view keeps the
  game's own hands animation, the viewmodel hook leaves the model where the game puts it and the arm policy
  gives both arms back, so it plays as the flat game's, right-handed]; the hammer and the Berserk's fists (not
  brought out on the rig in either hand).
- **Logs.** `left-handed arms: the left glove holds the gun (the right glove's grip reflected), the right
  arm is the off hand (on the right controller | the game's left glove reflected)` and `left-handed arms: no
  grip of the game's for the held item yet ...` as that changes; the left arm's lines read `weapon arm (left
  glove): ...`, the off hand's `weapon arm: ...` as the weapon arm's did, both with the weapon arm's trace. On
  the chaingun with the default off hand, once per item: `left-handed arms: 'weapon/player/chaingun': the
  game's left grip, reflected, holds nothing on this gun: the right glove follows the right controller`.

## Hidden arms (ETERNALVR_ARMS)

`ETERNALVR_ARMS=hidden` (the launcher's "Show arms" off) hides both first-person arms and leaves the weapon
alone. It was the fallback for the weapon in the left hand until the arms took each other's roles there
("The weapon in the left hand", above; GitHub issue #23), and it hides both arms the same way in either
hand.

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
| `ETERNALVR_WEAPON_ARM` | `ik` / `game` (`ik`) | Who poses the weapon arm's forearm, elbow and upper arm; the gun stays the game's, and so does the wrist but on the chaingun and the ballista, whose glove goes on a grip |
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
right controller. With the weapon arm on (the default) the arms take each other's roles ("The weapon in the
left hand" above). Only with `ETERNALVR_WEAPON_ARM=game` (rig and debugging) does the model's left arm still
reach for the right controller: `ETERNALVR_OFFHAND_OFFSET`, `_SHOULDER` and `_ELBOW`,
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
