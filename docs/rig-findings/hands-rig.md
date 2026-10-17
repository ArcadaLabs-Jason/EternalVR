# Hands rig: broken / not broken per pose, without a headset

A scripted rig check of the first-person arms: a fixed sweep of hand poses through the scripted controller input
(`ETERNALVR_TEST_INPUT`), the layer's own record of both arms at every moment (`ETERNALVR_TEST_ARM_DUMP`), and a
checker that says, per pose, which arm checks fail. It is the first step of the feature track in docs/ROADMAP.md
("Plan from v0.1.49"): the arm solve fixes, the weapon sights, the model swap, hands-only, hand poses and two-handed
holds are each checked on it, so time in the headset goes to how it feels, not to whether it is broken.

Everything here is rig use only. Without the variables the layer does nothing new.

## 1. The layer side

- **`ETERNALVR_TEST_ARM_DUMP=<file>`**: every 0.25 s the off-hand hook appends one JSON line to the file
  (`src/vkcore/arm_dump.{hpp,cpp}`; the line itself in `src/features/arm/arm_dump_line.{hpp,cpp}`, unit-tested). The
  line holds, for the moment it was written:
  - `t`: QueryPerformanceCounter seconds (the clock PowerShell's `Stopwatch` reads, so a script's pose log and the
    dump share one clock); `upm` (game units per metre) and the arms animator's model `scale`; `fault`;
  - `input`: the scripted input of each hand as the layer read it (aim yaw, pitch, roll in degrees; position from
    the head in LOCAL metres, or null for the side's rest position);
  - `arms.L` / `arms.R`: who poses the arm (`layer` or `game`) and the layer's share (`weight`), the wrist twist the
    solve found (degrees), how far the last read put the layer's joints from where they were written (`landed`), and
    four pose sets of the arm's eight joints (attach, hand, the four roll joints, forearm = elbow, upperarm =
    shoulder) in the arms model's space, game units: `final` (the poses the layer wrote this tick, after its joint
    modifiers), `read` (the poses GetJointTransforms gave this tick: for the six joints the layer overrides, the
    blend's result of the last frame with the overrides; for the attach joint and the wrist, the animated pose before
    the game's own modifier moves them), `animated` (the game's own pose the solve started from), and `target` (the
    off hand's wrist target: its controller grip plus the wrist offset; null for the weapon arm, whose wrist stays
    where the gun's animation has it); and `root`, the skeleton's root joint as read, which the overrides are placed
    from. A pose is 12 numbers: the position, then the axis rows forward, left, up;
  - `model`: the arms model's placement (the eye in the world, its offset from the eye and its axis), `head` (the
    rendered head's axis), `item` (the held item's decl name), `weapon` (the weapon's root: `righthandattach` as
    read; the weapon model hangs from it; null while the game poses the weapon arm, since it is read with the layer's
    pose of that arm), `aim` (the ray shots take: start from the eye and direction), `grip` and
    `offgrip` (each controller's grip from the eye).

  All of it comes from what the off-hand hook, the weapon arm and the viewmodel hook already read (the eye is the
  player field the viewmodel and fire hooks read); no game function, hook or field is added.
- **`ETERNALVR_TEST_ARM_FAULT=twist`**: development use only, the checker's positive control
  (`src/features/arm/twist_fault.{hpp,cpp}`, unit-tested). After the solve, every roll joint of both arms is turned
  back by the share of the wrist twist it took, so the whole twist sits on the wrist joint: a candy wrapper the
  checker must fail.

## 2. The sweep (`tmp-vr\rs\hands\hands-sweep.ps1` on the rig PC)

One OpenXR-Simulator run into `e1m1_intro` (god mode, the first barge checkpoint, the view set at the barge), the
free off hand (`ETERNALVR_OFFHAND=free`) and the weapon arm in `ik` (the default), hand aim, no aim smoothing, Route
S at 1024x1104, the combat shotgun in hand. Four seconds after the schedule's last step it writes each pose to the
scripted input file (an in-place rename, so the layer never finds the file missing) and holds it 1.5 s, logging each
pose's name, group, start and end (on the dump's clock and the wall clock) and input to `poses.ndjson` next to the
dump. Positions are metres from the head in LOCAL (x right, y up, z back); the neutral hand is 0.2 m to its side,
0.3 m below and 0.4 m ahead of the head, pointing ahead. The 70 poses (72 from section 7 on, with a lead-in hold
before each roll sweep):

| Group | Poses |
|---|---|
| `neutral` | both hands neutral (mirrored) |
| `roll-L`, `roll-R` | one hand's roll -180 to 180 in 30 degree steps at the neutral position, the other neutral (13 each) |
| `flex-L`, `flex-R` | pitch -60, -30, 30, 60 and yaw -45, -22.5, 22.5, 45 degrees (8 each) |
| `reach-L`, `reach-R` | 0.30, 0.45, 0.60, 0.75 and 0.95 m ahead, high at 0.50 and 0.75, low at 0.50, across the body at 0.45 and 0.35 (10 each; the right hand's mirrored) |
| `mirror` | both hands mirrored in one pose: four positions with yaw, pitch and roll (the checker compares the arms) |
| `two` | the left hand on a point 0.25 to 0.32 m ahead of the right one along its aim, palm turned up under the barrel (3) |

Both eyes are captured without a shot (`ETERNALVR_CAPTURE_EYES`), and one pair is kept for each of eight chosen poses
(roll 0, 90, 180 and -90 of the left hand, flex up, reach 0.95, mirror-1, two-1): the last pair written in the hold,
named after the pose, for a person to look at. The sweep keeps the rig rules: it waits while another run, a build or
a headset session has the rig, holds the rig lock for the run, and puts the save back afterwards.

## 3. The checker (`tmp-vr\rs\hands\check_hands.py <dump> <poselog> [--mesh]`)

Per pose it takes the dump lines in the hold's last 0.75 s whose input is the pose's (others are counted and left
out) and checks each arm the layer poses, on the poses the layer wrote (`final`). Thresholds sit in one table at the
top of the script:

| Check | What it measures | Fails when |
|---|---|---|
| posed | the layer poses the arm on every line of the window | not |
| bones | each bone of attach-hand-roll3-roll2-roll1-forearmroll-forearm-upperarm against its length in the game's animated pose (rigid bones, so the bind length) | > 1 mm |
| twist step | the largest twist between neighbouring joints of hand-roll3-roll2-roll1-forearmroll-forearm about the forearm, beyond what the bind pose has (`--ref bind`, the default from section 7 on) or the animated pose (`--ref animated`, the first definition) | > 35 deg (a ring weighted half and half on two joints that far apart shrinks by 5%) |
| swing | how far the wrist bends away from the forearm beyond the bind pose (or, `--ref animated`, the animated pose) | > 70 deg (a real wrist's flex) |
| flicker | the largest change of a forearm joint's or the hand's orientation relative to the elbow between two consecutive lines of a hold (the input does not change in a hold) | > 5 deg |
| wrap | between consecutive poses of a roll sweep: the largest turn of a roll joint relative to the elbow, against the roll step | > the step + 10 deg (a joint going the long way round as the twist crosses 180) |
| reach | the off hand's wrist from its target; n/a when the target is beyond the arm's reach (the IK stops short on purpose) | > 10 mm |
| reach (weapon arm) | the weapon hand's wrist from its controller's grip | > 80 mm |
| landed | the layer's joints as read back from the game vs as written (the layer's own measure) | > 10 mm |
| mirror | on mirrored poses, the left arm's joints against the right arm's mirrored in the head's yaw frame | > 10 mm |
| sight | the angle between the held weapon's sight line (weapon space, from the sights survey: the combat shotgun's receiver top to its bead, the Heavy Cannon's precision bolt scope line, the chaingun's zoom axis, the ballista's zoom axis, the bore for the rest) and the ray shots take; the exit test of the angle-only sights step | > 0.25 deg |
| collapse (`--mesh`) | the narrowest cross-section of the forearm near the wrist (slices at 0.6 to 1.0 of elbow-to-wrist) over the same slice in the bind pose, skinning the base `arms.md6mesh` with the dumped joints as a proxy (its weights are fully decoded; joints the dump does not hold follow their nearest dumped parent) | < 0.70 |
| flipped (`--mesh`) | triangles whose normal opposes the bind normal turned by the triangle's skinning rotation, or squashed by it, beyond those the game's own animated pose of the same moment flips; counted for the wrist half (the forearm from mid-way, the hand) and the elbow half apart | > 10 per half (the proxy's creases fold up to about 10 small triangles either way in static holds) |
| holes (`--mesh`) | open edges added against the bind pose (seams that part) | > 0 |

It writes a text report and a CSV (one row per pose: name, metrics, PASS/FAIL, the failed checks), and ends with
`HANDS-RIG PASS` or `HANDS-RIG FAIL n/m`. With `--baseline <check.csv>` it also lists the checks that fail now and
did not in the baseline (regressions) and the ones fixed. A self-test on made-up arms (`selftest_check.py`): the
layer's twist share rule passes the twist step up to about 150 degrees of wrist twist (0.22 of the twist lands
between the wrist and the last roll joint), while the whole twist on the wrist fails it from 60.

## 4. Merge QA

`tmp-vr/rs/hands/hands-check.sh <layer src dir> <tag> [--fault]` runs the sweep (through `tools/unelevated.sh` from
an elevated shell) and the checker with `--mesh`, and ends with `HANDS-RIG PASS`, `HANDS-RIG FAIL n/m` or
`HANDS-RIG ERROR <why>`. With `runs/baseline.csv` in place the regressions against it are listed before the
verdict; until the arm work lands every pose fails at least the sight check, so the regressions line is what a
merge is judged by.

**Known-bistable checks** (2026-10-13; rig tooling outside the repo, beside the checker and the baseline:
`runs/known_bistable.csv`, columns pose, check, warn_limit, why). A few pose/check pairs toggle between sweeps
of one build of the default (animated) weapon-arm solve. Over 13 sweeps of the same arm code (three of 70
poses, ten of 72, re-checked with `--ref bind`), every pose/check gave the same verdict in all 13 except these:
roll-R+150's four weapon-arm checks (flicker, wrap, twist step, flipped elbow) failed in 7 or 8 of them: the
solved twist sits within 0.5 degrees of +-180 in that hold (-178.5 to -179.7 in some sweeps, +179.6 to +179.9
in others), so the known wrap of section 5 lands in the +150 hold instead of the +180 one, one defect a pose
early (roll-R+180's wrap then shows as fixed). Four flip counts of the mesh proxy (flex-L-p30 left elbow,
reach-L-across035 left wrist, reach-R-030 right elbow, roll-R-120 right elbow) step across the 10-triangle
limit by 1 to 7 triangles (7 to 13-14, 10 to 11, 9 to 13, 7 to 11) in 1 to 3 of the 13; they rose only in the sweeps where roll-R+150 did not flip, which
points to a state that holds for a whole sweep, probably the game's idle pose at the time [inferred, not
checked].

The rule: with `--baseline`, a failure that is new against the baseline and is on the list is a WARN, not a
regression, while its value is no worse than its warn limit (the bad state seen plus a small margin: at
roll-R+150 flicker 155 degrees, twist step 55, wrap 175, elbow flips 15; 13 to 16 for the proxy counts).
Beyond its limit, or for a pair not on the list, it is still a regression and the line says so (for example
`R:twiststep 60 beyond its known-bistable warn limit 55`). `--bistable <csv>` names another list and
`--bistable none` turns it off. The warns are printed on their own line right after the regressions line,
before the verdict, and the gate (`regressions vs ...: 0`) does not count them:

    regressions vs .../runs/baseline.csv: 0
    known-bistable warns (not regressions; known_bistable.csv): 4: roll-R+150 R:flicker 150.33 (warn limit 155),
      roll-R+150 R:flipped-elbow 13 (warn limit 15), roll-R+150 R:twiststep 50.19 (warn limit 55), roll-R+150
      R:wrap 170.72 (warn limit 175)
    HANDS-RIG FAIL 72/72

(one line in the output, wrapped here). Pairs that toggle but already fail in the baseline (roll-R+180 R:wrap,
flex-R-p60 R:swing on its 70-degree limit, reach-L-030 and reach-L-across035 L:twiststep on the 35-degree
limit) can only ever show as fixed, so they are not listed. A repeat rule (a failure must show in two sweeps)
was not chosen: the roll-R+150 flip came up in 8 of the 13 sweeps, so it would still fail most merges and
double the rig time.

`runs/baseline.csv` is built by `make_baseline.py` from all 13 sweeps: per pose, every check that failed in
any of them, minus the listed pairs (so those still show as warns whenever they fail). For the 70 older poses
that is exactly `ap-base0`'s set; it adds the lead-in rows (`lead-L`, `lead-R`), which nothing was compared
against before. Positive controls: both planted-fault runs still fail, `ap-fault` (the twist fault) with 37
regressions and no warn (its roll-R+150 twist step of 60 degrees is beyond the limit of 55) and `hr-fault1`
with 63 regressions, the same with and without the list. The 13 default sweeps all give 0 regressions.

Limitation: a real regression that touches only roll-R+150 or one of the four proxy counts, and stays within
those limits, is hidden; every other pose still guards the same checks. The margin is smallest on roll-R+150's
twist step (limit 55, the planted fault 60).

## 5. Baseline (2026-10-12, this branch on 0.1.49, OpenXR-Simulator, combat shotgun)

Runs `hr-base2` (no fault) and `hr-fault1` (`ETERNALVR_TEST_ARM_FAULT=twist`), 70 of 70 poses each, 566 and 567
dump lines. Both `HANDS-RIG FAIL 70/70`: every pose fails the sight check, as expected before the sights step.
Evidence: rig (the simulator and the scripted input), not a headset.

Without the fault:

- **Clean**: bones within 0.02 mm of the animated lengths, no flicker within a hold (at most 0.35 deg left, 0.81 deg
  right), the off hand's wrist on its target to 0.0 mm wherever the target is in reach (11 poses beyond reach: the
  reach poses from 0.60 m, high, low, across 0.35, mirror-4 and the two-handed holds), no seam opens (holes 0).
- **Twist**: the twist the solve finds is about 91 deg with the left controller at roll 0 (its zero is the game's
  animated hand on the forearm) and -24 deg on the right; 30 deg of roll gives 24 to 37 deg of twist (the forearm
  does not lie along the controller's pointing axis). Twist step fails where the twist passes about 158 deg (0.22 of
  it between the wrist and the last roll joint): left roll -90 (38.0 deg) and the three two-handed holds (37.6 to
  38.6 deg, the left palm up), right roll +150 (39.5 deg).
- **Wrap**: left roll -90 to -60 (the twist goes from -171 to 153: a roll joint turns 171 deg for a 30 deg roll step)
  and right roll +150 to +180 (170 deg). Every other roll step turns a roll joint 19 to 29 deg. This is the jump of
  the twist wrapping at 180 deg that shows as the flashing forearm.
- **Swing** beyond 70 deg: left roll -120 and -90 (70.3, 70.8), left pitch -60 (98.3), pitch -30 (72.1), yaw -45
  (75.0); right pitch -60 (87.6).
- **Collapse** (proxy mesh): left pitch -60 at 0.60, right pitch -60 at 0.63; everything else at 0.74 or more (the
  two-handed holds 0.74 to 0.75, left roll -90 0.74).
- **Flipped**: at the wrist the layer's pose turns over more triangles than the game's own pose in 7 left poses (the
  flex poses, reach 0.30, across 0.35, mirror-3: up to 34) and 14 right poses (roll beyond 60 deg either way, flex,
  mirror-1 and -3: up to 37). At the elbow the left arm flips about 19 more than the game's elbow in 62 poses, the
  right one in 5.
- **Landed**: the layer's joints read back 16 to 31 mm from where they were written on the left in most poses (19 mm
  in the neutral pose), and 77 to 97 mm at left roll -60 to +60 (twist 39 to 153 deg). With the left arm held at
  neutral, its offset also follows the right hand's roll: 19 mm at right roll 0, then rising steadily to 20, 25, 33,
  43, 53 and 61 mm at +30 to +180; on the other side 22, 29 and 37 mm to -90, then 80 to 84 mm at -120, 69 to 70 at
  -150 and 63 to 65 at -180 (both runs). So where the left arm's overrides land depends on the right arm's pose too. On the right 4 to 40 mm, growing with its
  twist. Not the skeleton's root: the repeat run below dumped it (`root`) and it read (-0.24, 0, -1.591) with no
  rotation in all 1090 arm records.
- **Weapon arm reach**: the weapon hand's wrist sits 146 mm from its controller's grip in every pose (the gun is
  placed at the grip by the offset table; the wrist under it is not).
- **Mirror**: the left arm against the mirrored right arm 81 to 97 mm apart in neutral and mirror-1 to -3, 34 mm in
  mirror-4: the free off hand and the weapon arm are not posed alike.
- **Sight**: 2.04 to 2.34 deg between the combat shotgun's receiver-to-bead line and the shot ray in every pose (the
  sights survey had about 2 deg of toe-in in the idle pose).
- Twice in the run both arms went back to the game's pose for a moment (`game (untracked)`, weight 0) while a pose
  was being switched; no hold window was touched.

A repeat run without the fault (`hr-base3`, the build with `root` in the dump) failed the same checks in 66 of the
70 poses; the other 4 differ only by wrist or elbow flip counts crossing 10 (0 regressions against `hr-base2`, 4
fixed). Between the two runs the metrics moved at most 0.17 deg (twist step), 0.25 deg (sight), 0.3 mm (weapon
wrist) and 4.5 mm (left landed).

With the fault (positive control): twist step fails in 67 left poses (up to 173 deg) and 18 right ones (up to 180),
against 4 and 1 without; collapse in 14 left and 7 right poses (down to 0.52 and 0.54), against 1 and 1; wrist flips
in 62 left and 21 right poses, against 7 and 14. The wrap goes (the roll joints no longer turn at all), and the
checks the fault does not touch (bones, swing, reach, landed, mirror, sight) give the same poses as without it.

## 6. Weapon sights, angle only (2026-10-12, OpenXR-Simulator)

`ETERNALVR_WEAPON_SIGHTS=1` (on by default since T-135) turns each weapon with a `[sights]` row in `data/weapons/viewmodel_offsets.toml` about
the grip so its sight line runs parallel to the aim ray (docs/VR_CONTROLLERS.md, "Viewmodel offsets and tuning").
The rig run for it (`tmp-vr\rs\hands\sights-sweep.ps1` on the rig PC) is the sweep above with `give all` in the
debug schedule and a rig controls file that binds a touch per weapon slot (`right.primary_touch.tap` =
`weapon_slot_1` and so on, `ETERNALVR_THUMBREST_WHEEL=off`): per slot a 0.4 s touch, 3 s for the raise, then five
holds of the weapon hand (neutral, roll +-90, pitch +-30), the off hand neutral. `sights_check.py` takes each
weapon's sight line in weapon space (the checker's sights table: the combat shotgun's receiver top to its bead,
the Heavy Cannon's iron sights 3.5 deg above the bore, the chaingun's zoom axis, the super shotgun's rear sight top
to its muzzle top, the bore for the rest), turns it by the dumped attach joint into the arms model's frame, and
reports its median and spread over every dump line of the holds' last 0.75 s, and its largest angle to the ray
shots take. Each row is the centre of the line's spread (the midpoint of its smallest and largest pitch and yaw) in
the run without the switch, which keeps the sway's worst case smallest; the first rows were the medians.

| Weapon | Row: the line in the model's frame (down, left) | Spread (down, left) | Without the switch | Median rows (`sa-on1`), max | Centre rows (`sa-on2`), max |
|---|---|---|---|---|---|
| combat shotgun | 0.21, 2.17 (median 0.21, 2.18) | 0.15, 0.31 | 2.06 to 2.34 deg | 0.167 deg | 0.162 deg, pass |
| Heavy Cannon | -3.45, 0.19 (same as the median) | 0.09, 0.38 | 3.45 to 3.50 deg | 0.200 deg | 0.194 deg, pass |
| super shotgun | 0.69, 0.16 (median 0.68, 0.17) | 0.09, 0.30 | 0.68 to 0.79 deg | 0.160 deg | 0.156 deg, pass |
| chaingun | 0.32, -0.01 (median 0.28, 0.03) | 0.37, 0.19 | 0.28 to 0.51 deg | 0.243 deg | 0.202 deg, pass |
| ballista | -0.83, 1.08 (median -0.88, 1.07) | 0.40, 0.06 | 1.25 to 1.50 deg | 0.252 deg (fail) | 0.204 deg, pass |
| plasma rifle, rocket launcher, BFG | no row (bore within 0.06 to 0.33 deg) | | | not turned | not turned |

Runs `sa-off1` (the switch off, the positive control), `sa-on1` (on, median rows) and `sa-on2` (on, centre rows),
45 of 45 holds each; 2 or 3 dump lines per hold. With the switch on the line in the model's frame stays where it was
(the idle animation is the game's) and the angle to the ray falls to the line's distance from its row: what is left
is the idle sway between holds, 0.16 to 0.20 deg at worst with the centre rows. Evidence: rig (the simulator and the
scripted input), not a headset.

- Weapon mods do not change the held decl: the rows cover every mod of a weapon.
- `give all` and a second press of slot 8 did not bring up the Unmaykr: the slot-8 action was logged twice and the
  BFG stayed in hand.

Open:

1. **The active mod**: the Heavy Cannon's row is its iron sights; the precision bolt's scope line sits about 3.3 deg
   off it. A row per mod needs the active mod's decl read from the weapon (`idWeapon` weapon mod overrides, from the
   type info, untested), checked on the Game Pass build too.
2. **Parallax**: the sights run parallel to the ray, not on it: they sit their fixed height and side above the hand
   (about 13 to 21 cm up, 7 cm left by the sights survey). Moving the gun so the sights lie on the ray is the next
   step and is not measured here.
3. **The Unmaykr**: whether `give all` grants it, and how it is selected on the rig, is not known; it has no row
   either way.

## 7. Arm solve and model (feature step 3 prototype, 2026-10-12, OpenXR-Simulator)

The step's prototype: the solve fixed (VR_HANDS_HUD.md, "The whole arm", the forearm and upper arm) behind
`ETERNALVR_ARM_SOLVE=bind`, and two rig-only switches, `ETERNALVR_ARM_MODEL` ("Arms model") and
`ETERNALVR_ARMS=hands` ("Hands only"). Nothing changes for players: the solve's default is `animated`, the
solve as it was, and the switches are off. The runs `ap-s1a`, `ap-final`, `ap-fault`, `ap-skin3`,
`ap-base-model` and `ap-hands2` were made while the bind solve was still the only one (as with
`ETERNALVR_ARM_SOLVE=bind`); `ap-gate-default` and `ap-gate-bind` check the switch.

**The rig from this step on.** `hands-sweep.ps1` takes the rig scripts (`launch-ht.ps1`, `display.ps1`,
`stop.ps1`) from the checkout the layer was built in (or `-RigDir`), not from the rig PC's root checkout; can
take a lock owner and hold `tmp-vr\BUILDING` for the run (`-Owner`, `-HoldBuilding`); stops at once when
`tmp-vr\JASON-TESTING` appears; and has a lead-in hold before each roll sweep (`lead-L`, `lead-R`: that hand at
roll -90, group `lead`, 72 poses in all): the sweep starts half a turn from neutral, which a controller cannot
reach in one tick, and the solve keeps the twist continuous by taking the nearest way round. `hands-check.sh`
passes anything after the tag to the sweep. The checker measures the twist step and the swing against the bind
pose by default (`--ref bind`; `--ref animated` is the first definition, against the game's animated pose of
the moment; the other one is kept in the `*X` columns): the solve measures the twist from the bind pose and the
skin deforms against it, while the game's own idle turns the left hand by -132 degrees with the roll joints
sharing it, which the first definition counted as zero.

**Baseline on current tools** (`ap-base0`: origin/main 49a8e9fd, Release, 70 poses): against `hr-base2` 0
regressions and 4 flip counts back under 10; metrics within run-to-run noise (twist 2.2 deg, twist step 0.5 deg,
collapse 0.007, sight 0.24 deg, left landed 9 mm). `runs\baseline.csv` was its bind-referenced check until the
13-sweep baseline of section 4 replaced it (kept as `baseline-ap-base0.csv`; `baseline-anim.csv` the same run against the animated pose, `baseline-hr.csv` the old `hr-base2`).

**Solve** (`ap-s1a`, repeated as `ap-final` on the final build, the same verdicts in every pose), against
`ap-base0`, both arms posed in every hold:

| Check (worst over the poses) | Before | After |
|---|---|---|
| twist step against the bind pose, left / right | 67.6 / 43.9 deg (fails in 18 / 3 poses) | 26.7 / 34.0 deg (0 / 0) |
| wrap: a roll joint's turn between roll poses 30 deg apart, left / right | 171 / 170 deg (1 / 1) | 24 / 23 deg (0 / 0) |
| swing against the bind pose, left / right | 92 / 86 deg (2 / 3) | 52 / 86 deg (0 / 3: the weapon arm's wrist is the game's, not limited) |
| collapse (base-mesh proxy), left / right | 0.60 / 0.64 (1 / 1) | 0.83 / 0.68 (0 / 1: right pitch -60, the weapon arm's own wrist bend) |
| wrist flips beyond the game's, left / right | 35 / 37 (6 / 14) | 13 / 34 (2 / 13) |
| elbow flips beyond the game's, left / right | 22 / 12 (61 / 3) | 25 / 17 (54 / 2) |
| flicker, bones, holes, mirror, sight, weapon wrist | 0.8 deg, 0.02 mm, 0, 97 mm, 2.34 deg, 146 mm | the same |

Three checks cross their limit that did not before, in both runs: the weapon arm's read-back (`landed`) at roll
+30 (9.5 to 10.5 mm) and reach 0.60 (9.7 to 10.1 mm), and the left wrist's flips at across 0.35 (10 to 13).
The read-back grows because the overrides land about 95 % of the way from the game's pose to the written one
(VR_HANDS_HUD.md, "Where an override lands"), and the weapon arm's roll joints now sit at their share of the
twist from the bind pose instead of the game's animated twist. Against the animated pose (the first definition)
the left twist step fails in 10 poses instead of 4 (the roll sweep's -180 to -60, mirror-2 and the two-handed
holds): the solve no longer keeps the game's -132 degree idle twist in the roll joints, which that definition
takes as zero; the left swing fails in 1 instead of 5 and the wraps go.

The switch (`ap-gate-default`, no variable, and `ap-gate-bind`): with the default every check fails in the
same poses as the baseline but five: three flip counts (left elbow at flex up 30, left wrist at across 0.35,
right elbow at reach 0.30, the poses whose counts crossed 10 between `hr-base2` and `ap-base0`: the proxy's
noise) and the left twist step at reach 0.30 and across 0.35 (just under 35 deg now, just over it then); the
twist and the twist step within 2.8 and 1.1 deg of it, the left read-back within 39 mm (it follows the game's
idle of the moment). With `bind` the verdicts are those of `ap-final` but the right swing at pitch +60 (back
under its limit).

Positive control on the new solve (`ap-fault`, `ETERNALVR_TEST_ARM_FAULT=twist`): the twist step fails in 22
left and 21 right poses (up to 120 and 128 deg), collapse in 6 and 8 (down to 0.55 and 0.51), wrist flips in 12
and 16; without the fault 0, 0, 0, 1, 2 and 13.

Unit tests (`tests/features/arm/arm_solve_limits_tests.cpp`, `arm_twist_tests.cpp`): the limits tests use only
the solve's targets and joints, so they also build against the solve before the step, which fails three of the
four (a palm turned up with the game's idle twist: 67.8 deg between the last roll joint and the wrist; a
controller rolled through 380 deg 5 deg a tick: a forearm joint turns 152.8 deg in one tick; the wrist bent 100
and 60 deg: not limited); the new solve passes them, and the same roll sweep measured afresh every tick (no last
twist) still jumps more than 90 deg (the test's own positive control).

**Arms model.** `skin:3` (`ap-skin3`): the swap works and the joint checks are those of the game's model
(the solve does not depend on the mesh; the mesh checks use the base mesh as their proxy whatever is drawn),
but the left arm's overrides read back 57 to 102 mm away in the roll poses (19 to 24 mm with the game's
model) and the captures show the set 3 left glove away from the solved wrist (toward the gun's pump at roll 0,
in front of the camera at roll +90): not usable as is, cause not determined (found in section 8: the off
hand's attach joint never reached its target, on any model). `base` (`ap-base-model`): the swap
works, but the base model's animated arm is 2.5 times the game model's length and placed elsewhere, the view
looked at the ceiling, and the left arm's overrides read back 2.8 units away, so the layer's landing guard gave
it back to the game after 30 ticks: not usable as is.

**The joint-modifier weight** (`ap-w1`: bind, `ETERNALVR_TEST_ARM_MOD_WEIGHT=1`; `ap-skin3-w1`: the same with
skin set 3): every override of both arms read back where it was written (0.000 mm in all 72 holds, both runs),
so the 0.95 in the joint-modifier owner is what moves them; but the gun moved to the middle of the view, the
weapon hand turned (its twist at rest from -32 to +23 deg, so the right roll sweep now passes the twist's flip
point: wrap 140 deg at +180) and the set 3 left arm sank to the bottom of the view. Holding the weight is not
the fix (VR_HANDS_HUD.md, "Where an override lands").

**Hands only** (`ap-hands2`, `ETERNALVR_ARMS=hands`): both arms posed in every hold, the forearms and upper arms
gone from every capture, the gloves at the wrists, the cuff closing into a cone with spikes (vertices shared
between the hand and the last roll joint, pulled into the wrist by their roll-joint weight). The first run
(`ap-hands1`) gave both arms back to the game at once: the joints scaled to nothing read back without usable
axes, and their landing could not be measured; the layer now checks only the attach joint and the wrist while
those overrides show.

Captures (both eyes, eight poses each) in `tmp-vr\rs\hands\runs\<run>\captures` on the rig PC: `ap-base0`,
`ap-s1a`, `ap-final`, `ap-fault`, `ap-skin3`, `ap-base-model`, `ap-hands2`, `ap-gate-default`, `ap-gate-bind`,
`ap-w1`, `ap-skin3-w1`. Evidence: rig (the simulator and the
scripted input), not a headset.

## 8. Where the arm lands (feature step 3 prototype, round 2, 2026-10-12, OpenXR-Simulator)

All behind `ETERNALVR_ARM_SOLVE=bind` (VR_HANDS_HUD.md, "Where an override lands"); with the variables unset
nothing new is read or written.

**What the dumps showed.** The read-back of every earlier run fits one model: the blend mixes the arm without
modifiers and the arm with every modifier applied whole, joint by joint in each parent's frame, at the hands'
modifier weight (0.95). Fitted to `ap-final` the weapon arm's read is predicted to 1.5 mm (median, 4.0 at
most). The left arm needs one more thing: the read is the final pose of the last frame, so the attach joint's
change the layer computed from it was applied by the game to its pose without modifiers. With that, the left
arm is predicted to 0.9 to 1.6 mm (median); the pose without modifiers taken back from each read is the same
in every hold (2 mm, 0.8 degrees median spread). In every run from `hr-base1` to `ap-final` the left wrist read
back about 0.2 m from its target (median 189 to 208 mm): the glove stayed near the shotgun's pump and the posed
forearm stretched from it (the captures show it).

**Written past the target** (`ap2-comp2`, Release of this step, 72 holds, against `ap-final`):

| Read-back (`landed`, against the target) | `ap-final` | `ap2-comp2` |
|---|---|---|
| weapon arm, median / max | 5.9 / 44.8 mm, 26 holds over 10 | 1.5 / 4.4 mm, none over 10 |
| weapon arm per joint, median (roll3 to shoulder) | 1.3 2.5 3.6 4.8 5.9 5.8 mm | 0.2 0.3 0.4 0.6 0.7 1.5 mm |
| left arm, median / max | 25.7 / 106 mm, all 72 over 10 | 2.3 / 18.8 mm, 4 over 10 |
| left arm per joint, median (roll3 to shoulder) | 20.1 19.2 19.2 18.7 18.3 24.3 mm | 0.9 0.8 0.8 0.8 0.8 2.3 mm |
| left wrist from its target, median | 201 mm | 0 mm |

The four left holds over 10 mm: the roll -90 lead-in and sweep pose (11 mm; the attach joint reads back 6.5 to
7.0 degrees from its target in the last two dump lines of each hold of `ap2-comp2`, 6.8 to 7.3 in `ap3-bind`,
the same sweep after the last fix round; the wrist 12 mm, and the forearm is placed from where it lands), the
right roll +90 (10 mm; 5.1 to 5.7 degrees, 8.5 mm at the wrist) and mirror-2 (19 mm, the shoulder joint; the
roll joints 5 to 8 mm). The mix reaches at most 0.95 x 179 = 170.05 degrees (taking the blend for slerp; it is
nlerp, which reaches 172.98: section 9); the pose without modifiers taken
back from each of these reads along the arc through its target by that much is the same for the lead-in, the
right roll +90 and the three two-hand holds (within 0.8 degrees; the roll -90 sweep pose 6.5 off), so those
attach joints need about 175 to 177 degrees. The two-hand holds land 5.6 to 6.4 degrees short too,
under 10 mm. The first version without the half-turn limit (`ap2-comp`) landed those attach joints the other way round
(the forearm 58 mm off). Against `ap-final` no check fails that passed, 69 holds stop failing `landed`;
against the baseline (`ap-base0`) the one regression is `reach-L-across035`'s left wrist flips (13 against 10,
the pose that toggles between baseline runs, as in `ap-final`). The weapon wrist (146.4 mm from the grip), the
sight line (2.29 degrees) and the gun in the captures are as in `ap-final`; flicker at most 0.38 (left) and
0.78 (right) degrees, as before.

**Skin set 3** (`ap2-skin3`, `ETERNALVR_ARM_MODEL=skin:3`): the set 3 glove follows the solved wrist in every
capture; left read-back 2.3 mm median (19 at most, the same four holds), weapon arm 1.5; no regression
against `ap-skin3`.

**Hands only** (`ap2-hands-fold`, `ap2-hands-stub`, `ap2-hands-ring`, `ETERNALVR_ARMS=hands` with
`ETERNALVR_TEST_HANDS_CUFF`): both arms posed in all 72 holds in each. With the glove now at the wrist the
`fold` cuff is a short rounded cap with no spikes (the `ap-hands2` cone was the stretch from the pump); `stub`
leaves a bulky stump; `ring` opens the gauntlet's ring. The bone, swing and mesh checks fail by design.

Unit tests (`tests/features/arm/mod_mix_tests.cpp`): the inverse lands every joint of a moved and turned arm
(both arms) to 0.2 mm under the forward model, and a 175 degree attach turn lands its forearm while the glove
lands within 10 degrees; the positive controls: written as wanted the arm lands more than 10 mm short, the
attach joint's change computed from the read never lands (0.1 m after 400 frames, the way the rig showed),
and written past by the whole inverse weight the 175 degree turn goes the other way (more than 15 degrees
off). `arm_solve_tests.cpp` runs every case under both solves.

Captures (both eyes, eight poses each): `ap2-comp`, `ap2-comp2`, `ap2-skin3`, `ap2-hands-fold`,
`ap2-hands-stub`, `ap2-hands-ring` in `tmp-vr\rs\hands\runs\<run>\captures` on the rig PC.

## 9. The rest of the turn, and the blend is nlerp (feature step 3 prototype, round 4, 2026-10-13, OpenXR-Simulator)

All behind `ETERNALVR_ARM_SOLVE=bind` (VR_HANDS_HUD.md, "Where an override lands"); with the variables unset
nothing new is read or written (`r4-default`: the log has the old six modifiers per arm and no weight lines,
0 regressions against the baseline). Reference: `r4-ref-bind`, a Release of origin/main 1bef0af7 with `bind`.

**Why the forearm was off by up to 19 mm** (the left shoulder residual of section 8). In `r4-ref-bind` (and
`ap3-bind`) the wrist landed on target in every hold the attach joint could reach, but the first roll joint
below it landed 0.1 to 1.9 degrees off, and every joint after it by the same angle (a rigid turn of the
forearm about the wrist: 19 mm at the shoulder in mirror-2). The error followed only the attach joint's turn
from its pose without modifiers (the left arm's configuration is the same in every right-roll hold): 0.5
degrees at 89, 1.0 at 118, 1.9 at 147, which is the difference between nlerp and slerp for the turn written
(0.44, 1.01, 1.93 predicted). The game's blend command is nlerp [static: 0x19EAE50 mixes each joint's local
quaternion as (1 - t) qA + t qB with the sign of qA.qB taken and renormalises it with rsqrtps; no trigonometry].
The attach joint still lands on target, since the layer takes its pose without modifiers back from the read,
but that estimate is then off by the same difference, and so is the wrist's pose the game converts the first
roll joint's override against. Fix: the mix and its inverse model nlerp (`features/arm/mod_mix.hpp`).

**The rest of the turn.** Written 179 degrees, the attach joint lands 172.98 round (nlerp at 0.95); the roll
-90, right roll +90 and two-hand holds need more, so the glove landed 5 to 8 degrees short (8.5 to 13.8 mm). The
off hand now has a seventh modifier with `bind`: a change on its wrist (`LeftHand`), no change unless the attach
joint's turn is cut, then the rest of the turn, written past by the inverse of nlerp. The modifiers are applied in
list order, each against its parent as modified so far [static: 0x19E1040], so it has to come before the six
overrides: listed after them (`r4-bind`, an intermediate build) the wrist landed (0.5 to 0.7 mm) but carried the
forearm off with it (the first roll joint 10 degrees, the shoulder 38 mm off). The half-turn limit stays on both
joints, so neither is written the wrong way round. The wrist is read live while its change has been no change
for 3 ticks, and only after a change rebuilt from its offset from the attach joint, taken again at every
quiet read; that offset held still over the shotgun holds (0.02 mm, 0.0007 degrees), other weapons not
measured. Fix round 1 (`r4-bind3`, below) checks this.

| Left arm read-back against the target, 72 holds | `r4-ref-bind` | `r4-bind2` |
|---|---|---|
| worst joint per hold (wrist to shoulder), median / max | 2.2 / 19.1 mm, 6 holds over 10 | 0.53 / 0.77 mm, none over 10 |
| wrist, max | 13.8 mm, 8.0 degrees (roll -90) | 0.05 mm, 0.03 degrees |
| first roll joint's turn, median / max | 0.50 / 2.86 degrees | 0.04 / 0.05 degrees |
| `landed` (the layer's measure, now with the wrist), median / max | 2.3 / 19.5 mm | 0.67 / 0.81 mm |
| weapon arm `landed`, median / max | 1.5 / 4.5 mm | 1.4 / 1.8 mm |

The failing checks are the same set but `L:landed` (4 holds: lead-L, roll-L-90, roll-R+90, mirror-2, all gone).
Twist (step 26.7 / 33.5 degrees at most), swing, wrap (24 degrees), bones, collapse, flips, mirror, the weapon wrist
(146.4 mm from the grip) and the sight line (2.29 degrees) are as in `r4-ref-bind`; left flicker 0.06 degrees median
(0.21). Against the baseline: 0 regressions, the known-bistable warn on `reach-L-across035` as before.

**Models and hands only with `bind`** (Release of this round, against round 2's runs of the same mode):

| Run | Against | Result |
|---|---|---|
| `r4-skin3` (`ETERNALVR_ARM_MODEL=skin:3`) | `ap2-skin3` | left worst joint 0.54 / 0.75 mm (from 2.1 / 19.0), wrist 0.05 mm at most (13.9); weapon arm 1.4 / 1.8 (1.5 / 4.4); `L:landed` 4 holds to 0, `R:swing` flex-R-p60 no longer fails; 0 regressions against the baseline |
| `r4-hands` (`ETERNALVR_ARMS=hands`, `fold`) | `ap2-hands-fold` | both arms posed in all 72 holds; left `landed` 0.05 / 0.07 mm (0.95 / 4.8); the bone, swing and mesh checks fail by design, as before |
| `r4-base` (`ETERNALVR_ARM_MODEL=base`) | `ap-base-model` | the left arm never posed (2119 ticks refused: the wrist target is 3 m from the model's wrist); the base model is its skeleton's bind pose times 2.50 (VR_HANDS_HUD.md, "Arms model"): not a candidate (the switch stays for rig tests) |

Unit tests (`tests/features/arm/mod_mix_tests.cpp`): the blend turns 90 and 150 degrees to 85.88 and 144.26 and
`pastPose` undoes it (positive control: written past by 1/weight along the arc, the old inverse, 150 degrees lands
more than 1.5 degrees past); 179 degrees written lands 172.98; with the wrist's change a 175, 177 and 179.5 degree
turn lands the wrist and every joint after it (positive control: without it the wrist lands short by the rest);
a turn the attach joint reaches leaves the wrist with no change.

**Fix round 1** (`r4-bind3`, `bind`, against `r4-bind2`; the wrist read live while its change is none, offsets
dropped on new hands, node or skeleton, a clean read 150 ms and 3 ticks after the last write): the worst left joint
per hold 0.55 / 0.77 mm (0.53 / 0.77), the wrist 0.04 mm and 0.04 degrees at most (0.05, 0.03), `landed` 0.67 /
0.84 mm (0.67 / 0.81), the weapon arm 1.44 / 1.80 mm (1.42 / 1.76); the same failing checks, twist, swing, wrap,
bones, sight and gun; left flicker 0.21 degrees median, 0.34 at most (0.06 / 0.36; `r4-ref-bind` 0.21 / 0.34). No
landing refusal in the log. `r4-default2` (no variables): 0 regressions against the baseline, the same failing
checks as `r4-default`, no wrist or weight lines.

Captures (both eyes, eight poses each): `r4-ref-bind`, `r4-bind`, `r4-bind2`, `r4-bind3`, `r4-skin3`,
`r4-hands`, `r4-base`, `r4-default`, `r4-default2` in `tmp-vr\rs\hands\runs\<run>\captures` on the rig PC; the
clearest before and after is `roll-L-90` and `two-1` in `r4-ref-bind` against `r4-bind2`.
