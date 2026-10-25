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
    `offgrip` (each controller's grip from the eye); `arms_item` (the item the arms are placed and held by: the
    swim's fists while the hands swim, section 17), `hstate` (idHands::destHandsState) and `hswim` (whether the
    hands swim).

  All of it comes from what the off-hand hook, the weapon arm and the viewmodel hook already read (the eye is the
  player field the viewmodel and fire hooks read); no game function, hook or field is added.
- **`ETERNALVR_TEST_ARM_FAULT=forearm`** (with `ETERNALVR_ARMS=hands`, section 20): hands only with the
  forearm left drawn, the hands-only checks' positive control. The dump's arm record also gives `zero`, the
  joints written scaled to nothing (bit n for the n-th joint role).
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

1. **The active mod** (done, section 18): the Heavy Cannon's row is its iron sights; the precision bolt's scope line
   sits about 3.3 deg off it. A row per mod needs the active mod's decl read from the weapon (`idWeapon` weapon mod
   overrides, from the type info, untested), checked on the Game Pass build too.
2. **Parallax**: the sights run parallel to the ray, not on it: they sit their fixed height and side above the hand
   (about 13 to 21 cm up, 7 cm left by the sights survey). Moving the gun so the sights lie on the ray is the next
   step and is not measured here.
3. **The Unmaykr** (done, sections 12 and 18): whether `give all` grants it, and how it is selected on the rig, is not
   known; it has no row either way.

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

## 10. The glove on the grip, per weapon (2026-10-18, OpenXR-Simulator)

The sights sweep (section 6) with both eyes captured (`grip-sweep.ps1`, one pair kept per weapon from its neutral
hold) and a checker, `grip_check.py`; both are rig-local like the rest of the hands rig (the sweep holds the
rig's paths and the checker builds on `check_hands.py`). Per dump line it takes the right `hand` joint (the
glove's wrist) in the arms model's frame, places it with the dumped model pose, and measures it in the controller
grip's frame against the wrist target: the off hand's wrist rule (`kDefaultOffhandOffset`: 8 cm behind the grip's
centre, 3.5 cm toward the back of the hand) mirrored for the right hand, so both gloves sit alike on their
controllers. The grip frame is the model's axis with the weapon's `[sights]` turn taken back out. Per weapon it
reports the median and the largest miss over the five holds and the row that puts the wrist on the target
(`(-0.08, -0.035, 0)` less the wrist in the model's frame, through the sights turn). Evidence: rig (the simulator
and the scripted input, which gives the grip and the aim one pose), not a headset.

**The chaingun and the ballista hold the gun with the left hand.** In their idle animations the right glove is
parked off the gun, above and behind the camera (statically, from the idle animations: the right hand 3 cm above
the camera on the chaingun, at its height on the ballista; on the rig, 72 and 48 cm from the gun's grip point); the
left glove is on the gun. On every other gun the glove's wrist sits at one offset from the weapon's root joint
(`righthandattach`): `(-0.171, -0.040, -0.086)` m in the joint's frame, the median over 100 lines (the rocket
launcher's root sits elsewhere on its gun, `(0.013, -0.041, -0.118)`); the chaingun's own zoom pose, where the
right glove does hold it, puts it at `(-0.170, -0.041, -0.074)`. For these two the grip point is the root joint
plus that common offset, and the row puts it on the target: the gun sits in the hand, with no right glove on it.
(Section 11 corrects the reading: the right glove is not parked off the gun but holds its rear top handle.)

Before (`hg-main1`, dev main 0.1.60, every weapon on the `default` row) and after (`hg-grip1`, the rows in
`data/weapons/viewmodel_offsets.toml`), the wrist (or grip point) from its target in the grip's frame, cm, forward /
left / up; the owner's headset estimate is where the real hand was against the in-game one (headset 2026-10-13,
Quest 3, rough inches; minus the rig's miss is the same quantity):

| Weapon | Before: miss (f l u), largest | Real hand vs glove, rig / owner | After: largest miss |
|---|---|---|---|
| combat shotgun | -6.5, 5.2, -0.4; 8.4 | 6.5 ahead, 5.2 right, 0.4 up / 5 in ahead, 3 in right, 2 in below | 0.2 |
| Heavy Cannon | 2.1, 6.6, 1.4; 7.2 | 2.1 behind, 6.6 right, 1.4 below / 3 in ahead, 3 in right, 2 in below | 0.3 |
| super shotgun | -5.5, 4.5, -0.6; 7.2 | 5.5 ahead, 4.5 right / about the combat shotgun's | 0.2 |
| plasma rifle | -22.5, -2.1, -14.2; 26.7 | 22.5 ahead, 14 below / like the chaingun, less | 0.1 |
| rocket launcher | 9.4, 3.2, 0.5; 10.0 | 9.4 behind, 3.2 right, 0.5 below / 3 in behind, 2-3 in right, 2-3 in below | 0.2 |
| BFG | 10.5, 8.6, -16.3; 21.4 | 10.5 behind, 8.6 right, 16 above / 7 in behind, 5 in right, 7 in above | 0.2 |
| chaingun (grip point) | 36.5, 1.2, -11.4; 38.3 | the parked glove 26 behind, 25 above the hand / real hand far ahead | 0.2 |
| ballista (grip point) | 20.5, 0.7, -6.8; 21.7 | the parked glove 18 behind, 22 above / the hand not on the gun | 0.4 |

The directions agree on every weapon. Where they do not: the owner puts the real hand about 2 in lower than the
rig on the Heavy Cannon, the combat shotgun and the rocket launcher, and further ahead on the two shotguns and the
Heavy Cannon. The likely cause [inferred, not checked in a headset]: the gun follows the aim ray and the rows
place the wrist in that frame, while the real wrist is fixed in the grip's frame; on a headset the two differ by
the runtime's aim to grip turn, which the simulator does not have, so the 8.7 cm wrist offset lands several cm
off (about 6 cm for a 40 degree turn). The layer now keeps the wrist target in the grip's frame each frame
(`xr_math::placeViewmodel`: the model is moved, not turned, so the sights and shots keep the aim ray) and logs
the turn and the move ('the weapon hand's grip against its aim ray'). On the simulator the move is zero and the
numbers below are unchanged (`hg-grip3`). A seated session would also have put every glove 5 cm further back
(the old `[seated]` row).

The model's rotation is unchanged, so the sights stay parallel to the shot ray: `sights_check.py` on `hg-grip1`
passes every row (largest angle 0.16 to 0.20 deg, as in section 6). The sights move with the gun: the sight line
from the aim ray, left / up, before and after: combat shotgun 6.8 / 17.2 and 1.7 / 17.5 cm, Heavy Cannon (iron
sights) 7.4 / 18.9 and 0.8 / 17.4, chaingun (zoom axis) 6.5 / 7.7 and 5.3 / 19.1, ballista (bore) 1.4 / 1.9 and
0.7 / 8.7. Shots keep the aim ray; a muzzle-start weapon's start moves along it with the muzzle.

Captures (both eyes per weapon, the neutral hold): `hg-main1` and `hg-grip1`. The guns the glove holds show it on
the grip; the chaingun and the BFG fill the lower part of the view, against the chest, under the line of sight;
nothing reaches the centre of either eye.

Hands check (merge QA, combat shotgun, `hg-hands1`): one change against the baseline, `reach-R-060 R:landed` 12.4
mm (9.7 on main): with the wrist 6.7 cm further forward, the weapon arm is at full stretch at 0.60 m (shoulder to
wrist 0.549 of 0.551 m), where it was at 0.75 m before, and lands as far off as the stretched poses always have
(12.0 to 12.5 mm at 0.75 and 0.95 m). The weapon wrist from the grip (`R:reach`) goes from 146 to 88 mm, the
wrist target's own distance from the grip.

## 11. The glove on a grip: the chaingun and the ballista (2026-10-18, OpenXR-Simulator)

**What the two guns' idles hold** [static: the idle, shooting, turret, zoom, reload and switch animations of both
guns, their weapon meshes and `marine.md6skl`, decoded from `gameresources.resources`, build 25216728]. The right
glove is not parked off the gun: the chaingun's and the ballista's idles hold the gun by its rear top handle
(`chaingun_base_handle_rear`, `gauss_base_handle`, a bar along the gun at its back), the fist 1.5 and 1.2 cm from
the handle's mesh, the wrist at (-0.79, -0.04, 0.28) and (-0.56, -0.03, 0.20) m in the weapon root's frame, and
the hand turned 78 and 73 degrees from the grip of the other six guns (all within 7 degrees of the Heavy Cannon's;
their fists close on the gun's pistol grip, the mesh 0.3 to 0.4 cm from the finger joints on the Heavy Cannon,
the combat shotgun and the chaingun's zoom grip). The chaingun's zoom and reload hold it by its pistol grip in
exactly the Heavy Cannon's idle grip (wrist and fingers to 3e-6). The ballista's mesh has no pistol grip: at the
Heavy Cannon's offset the fist sits inside its stock (no vertex within 7 cm). The decoded local poses match the
hands rig's read (the combat shotgun's wrist in the root's frame to 1e-4), so the gun hangs from the root joint
and the wrist is its child.

**The change** (VR_HANDS_HUD.md, "The glove on the grip"; DECISIONS T-138): the weapon arm writes the glove's
wrist and fingers into the Heavy Cannon's grip on both guns (the ballista's wrist moved to (-0.302, -0.039,
-0.181) m, the fist on the ridge under its stock), the gun untouched, and the two `[standing]` rows put that wrist
on the target. Checker: `grip_check.py` as in section 10 with `--glove --read`: the glove's own wrist as read
back from the game (what landed), never the grip-point stand-in.

| Weapon | Before (`hg-grip3`, dev main): glove wrist miss, median / largest | After (`ga-grip3`) |
|---|---|---|
| chaingun | 72.3 / 72.5 cm | 0.1 / 0.3 cm |
| ballista | 48.0 / 48.2 cm | 0.2 / 0.5 cm |
| the other six | 0.1 to 0.2 / 0.2 to 0.3 cm | unchanged |

Five holds and a view hold per gun, the glove read back where written (`read` equal to `final`); GRIP PASS.
`sights_check.py` on `ga-grip3`: SIGHTS PASS, the largest angle 0.20 degrees on both guns; the sight lines from
the aim ray (left / up): chaingun zoom axis 5.1 / 18.0 cm (5.3 / 19.1 before), ballista bore 0.9 / 18.5 cm (0.7
/ 8.7 before: the hand is under the stock's ridge now, so the gun sits higher in it). With the Heavy Cannon's
offset left on the ballista (`ga-grip2`) the wrist landed as well (0.2 / 0.4 cm), but the captures show the
fingers inside the stock.

Captures (both eyes, the neutral hold and views with the gun turned so the glove shows): `ga-grip3`, `ga-bal1`
(the ballista's ridge grip from four sides), `ga-view4` (both guns held out low to the right and turned left:
the forearm goes into the chaingun's grip from below, the fist inside the grip's housing; the fist closed
under the ballista's stock) and `ga-view2` (`ETERNALVR_OFFHAND=game`: the game's left glove on
the chaingun's front handle as in flat play, none on the ballista). The run with the glove left at the Heavy
Cannon's offset on the ballista is `ga-grip2`.

Hand-backs (`ga-switch1`, `ga-switch2`: the combat shotgun, the chaingun, the shotgun, the ballista and the shotgun
in turn; in `ga-switch1` each of the two guns' weapon mod held 2.5 s, switched, held again; captures every 4
frames from 0.3 s before each switch or press to 5.5 s after; the arm's weight from the dump, every 0.25 s). A
switch hands the arm back within 1 s of the tap (the policy takes it at once and eases it out in a third of
0.15 s) and the weapon's kit then hides the right arm, glove included, as on every gun: the game's switch
animation runs without a right arm, and the glove is back on the new gun's grip 2.3 to 3.3 s after the tap,
once the gun is up (the policy's 0.25 s hold, then 0.15 s eased in). Holding the weapon mod (the chaingun's and
the ballista's, whichever the save has equipped) kept the weight at 1 on both: the glove stays on the grip
while the mod is used. Switching the mod handed the arm back for about 2 s (the game hides the hands and
animates the switch) and it came back eased the same way. A frame longer than 0.1 s also hands the arm back
(the policy's 'untracked'): with captures every 4 frames that happened 21 to 33 times a run, and each time the
glove eased off the grip toward the game's handle hand and back within about 0.45 s; with captures every 30
frames it happened only while the level loaded. The ease of each hand-back is the policy's (unit test: the
glove's landing moves at most 0.3 of the way in one 90 Hz tick of a 5-tick hand-back).

**The read's lag and the glove in the guns' own animations** (`ga-lag1`, `ga-mod1`, a build with the trace's
glove fields). In every read of the glove the rig could tell apart (the overrides more than 1 cm apart, while
the glove blends in), it showed the override written the tick before: 36 of 36, none older. Per phase, the
glove's wrist read back against where it was meant to land the tick before, in the gun's root frame (the
trace's largest per second): chaingun held still 0.1 mm; its weapon mod held, 35.5 mm in the second it comes
up, then 0.1 to 11.4; held again 0.1; firing 17.0, 9.2, 7.9 and 7.2. Ballista held still 0.1; its mod held
10.2, 0.6 and 4.5, the second time 14.6, 15.0 and 7.6; firing 157.1 and 150.7 in the first two seconds, then
12.4 and 9.2. The cause [inferred]: the glove's override is a model-space pose placed under the root joint as
read a frame before, so where the gun moves fast within a frame (a mod coming up, the shots' recoil, the
ballista's kick) the glove lands where the gun was. Not isolated: the chaingun's zoom pose (holding the
weapon mod ran whichever mod the save has equipped) and a reload (none was triggered).

## 12. The rest of the held items: Unmaykr, Crucible, chainsaw, fists, hammer (2026-10-18, OpenXR-Simulator)

One simulator run per build with a plan of taps, presses, holds (five and a view hold per item, as in section 11)
and console steps; the debug schedule's allow-list now takes `removeInventoryItem`, `judgementMeter_Set` and
`chrispy` (rig tests only, like the rest of it). Before: `gr-main1` (dev main, every item below on the `default`
row); after: `gr-rows1` (the Unmaykr's and the Crucible's rows). Evidence: rig (the simulator gives the grip and
the aim one pose), not a headset.

**How each comes out on the rig.**
- Unmaykr: `give all` does not give it (no earlier run ever held it); `give weapon/player/unmaykr` puts it in
  the hands, and the weapon slot 8 then switches between the BFG and the Unmaykr on each press.
- Crucible: `give weapon/player/crucible`, then the Crucible's button (left stick click). It becomes the held
  item (`weapon/player/crucible`) and stays out, idling, until a switch; the trigger swings it.
- Chainsaw: its button (right stick up) with no demon in reach puts `weapon/player/chainsaw` in the hands for
  0.8 s, then the item before it comes back. Held as a weapon it showed up only after the console had removed
  every gun: the next trigger presses brought it up and it idled like a gun.
- Fists: a melee puts `weapon/player/fists_doom5melee` in the hands for 0.5 s. Plain `weapon/player/fists`
  never showed: with every gun removed the held item stayed the BFG's decl, then the chainsaw. No Berserk:
  `give powerup/berserk`, `give pickup/powerup/berserk` and `chrispy pickup/powerup/berserk` (the hand aimed at
  the floor, then a walk over the spot) put no Berserk fists in the hands.
- Sentinel Hammer: with the Crucible swapped for it (`removeInventoryItem weapon/player/crucible`, `give
  weapon/player/hammer`, the wiki's swap) the Crucible's button changed nothing on e1m1, in both runs: no held
  item, no hand-back. The layer's arm policy hands the arm back for its throw and slam (the hands' pending
  action `HammerThrow` / `HammerSlam`, `features/input/offhand_policy.cpp`) [static].

**Who holds what** (the right and left wrists in the item's root joint's frame, `righthandattach`; the glove's
orientation against the Heavy Cannon's, both in the aim ray's frame at the neutral hold).
- Unmaykr: the right glove on the gun, its wrist (-0.168, -0.037, -0.080) m from the root, the guns' common
  offset (section 10), turned 3.6 degrees from the Heavy Cannon's glove (the eight guns are within 7); the
  game's left glove on the gun's front (0.02, 0.26, -0.04) m. Held like the other guns.
- Crucible: one-handed, in the right fist. The fist closes on the hilt just under the guard, the wrist (0.053,
  -0.063, -0.007) m from the root (the hilt hangs from it as a gun does), the blade along the root's z axis,
  up (0.92), leaning forward and to the right by about 15 degrees each; the glove is turned 37.0 degrees from
  the Heavy Cannon's. The game's left glove hangs behind and below the hilt, (-0.30, -0.14, -0.20) m, not on
  it. Split about the hilt: 22 degrees around it and 30 degrees off it.
- Chainsaw held as a weapon: the right glove on its rear handle, turned 38 degrees from the Heavy Cannon's;
  the left glove on its front.

**Hand-backs** (the dump's weight on both arms, a line every 0.25 s): the Crucible's swing goes to 0 within
one line of the trigger and the arm comes back eased 1.5 s later; the chainsaw's quick action and the melee
are at 0 for every line their item is held; the console emptying the inventory handed the arm back for 1 s.

| Item | Before: glove wrist from its target, median / largest; glove vs the Heavy Cannon's | After |
|---|---|---|
| Unmaykr | 13.7 / 13.8 cm (13.6 cm under it); 3.6 deg | 0.1 / 0.2 cm; 3.6 deg |
| Crucible | 40.0 / 40.1 cm (39.4 cm ahead of it); 37.0 deg | 0.3 / 0.4 cm; 0.1 deg |
| the eight guns | unchanged (the BFG, held in both runs: 0.2 / 0.2 cm) | unchanged |

`grip_check.py --glove --read` as in section 11; it now takes a row's own turn (pitch, yaw, roll) back out of
the model's axis to find the grip's frame, as it does the sights turn. Its per-line listing (`--per-hold`)
showed one other thing: after a scripted walk into the gate at the end of the hall, with every gun removed, the
BFG's glove read back 22.4 cm off its target at every hold, with the model's placement and the grip unchanged,
so the game's own pose of the glove had moved (the capture shows the gun lowered and to the left) [rig]; it is
not a row's doing and was not followed further.

Captures (both eyes, the neutral hold and the view hold, per item, before and after): before, the Crucible's
hilt sits 40 cm ahead of the hand at the right of the view with the blade leaning out; after, the fist sits
where the controller is, close by the right of the view, the blade standing up out of it. The Unmaykr's view
hold shows the right glove closed on its grip.

## 13. The glove in the gun's frame: the chaingun and the ballista firing (2026-10-18, OpenXR-Simulator)

**Where an override can be** [static: the modifier pass 0x19E1040, Steam, build 25216728; the Game Pass build's
pass at 0x19EC8A0 has the same code, the flag branches checked at the same distances]. Flag 1 of a modifier picks
its space: with it the pass builds the parent's model pose as modified so far and turns the modifier into the
joint's pose in that frame, so a model-space override lands where it was written whatever the parent does in that
frame; without it an override's rotation, scale and translation become the joint's pose under its parent as they
are. SetJointMod stores the flags it is handed. The layer's overrides were 0x22B (model space); the glove's are now
0x22A (features/arm/parent_frames.hpp; VR_HANDS_HUD.md, "In the gun's frame").

**The cause, per tick** (`gl-before`: dev main's model-space glove with a per-tick trace, the weapon-mod and fire
sequence of `ga-mod1` on both guns; `gl_ticks.py`). Per tick, the glove's wrist as read back in the gun's root
frame against where it was meant to land, and how far the root moved at the grip between the read the write was
placed under and the frame the game drew it in:

| Gun | Phase | Ticks | Miss median / p95 / largest, mm | Root's move at the grip, largest, mm | Correlation, slope |
|---|---|---|---|---|---|
| chaingun | held | 215 | 0.1 / 0.1 / 0.1 | 0.1 | 0.85, 0.89 |
| chaingun | mod held | 435 | 0.0 / 4.8 / 33.5 | 35.1 | 1.000, 0.96 |
| chaingun | firing | 257 | 1.6 / 7.6 / 17.1 | 17.9 | 1.000, 0.96 |
| ballista | held | 220 | 0.0 / 0.1 / 0.1 | 0.1 | 0.98, 0.97 |
| ballista | mod held | 394 | 0.1 / 3.9 / 15.0 | 15.8 | 1.000, 0.95 |
| ballista | firing | 256 | 1.1 / 12.0 / 151.3 | 158.0 | 0.999, 0.97 |

The miss is the root's move times the hands' modifier weight (0.95): the glove is placed where the gun was.

**After** (`gl-after`, the same sequence, the glove and the forearm in their parents' frames):

| Gun | Phase | Miss median / p95 / largest, mm | Root's move at the grip, largest, mm |
|---|---|---|---|
| chaingun | held | 0.0 / 0.0 / 0.0 | 0.1 |
| chaingun | mod held | 0.0 / 0.0 / 0.0 | 31.1 |
| chaingun | firing | 0.0 / 0.0 / 0.0 | 17.8 |
| ballista | held | 0.0 / 0.0 / 0.0 | 0.1 |
| ballista | mod held | 0.0 / 0.0 / 0.0 | 17.5 |
| ballista | firing | 0.0 / 1.6 / 15.0 | 158.0 |

Over 5 mm: 4 of the ballista's 259 firing ticks, in the first frames of its two shots (15.0, 6.2, 5.1 and
5.3 mm, slope 0.05 against the root's move): the blend's 1 - 0.95 share of the game's own hand under the glove,
which the read shows a frame late [inferred: the slope, and the ballista's shots move its handle hand 10 to 20 cm
in the decoded animation]. After the shots the ballista's hand moves again and the glove is up to 10.1 mm off for
a few ticks (9.5 before the change) while the wrist tracker waits for steady overrides; one tick of 6.1 mm while
the mod switches. The sleeve's end stays on the glove (2.9 mm at most, as before). The shoulder end now rides the
gun for the frame of a kick: up to 173.7 mm from its target in the ballista's first shot (48.6 before), back on
it at the next tick's solve.

The grip sweep on the change (`gl-grip`): GRIP PASS, the glove's wrist from its target, median / largest, chaingun
0.1 / 0.3 cm and ballista 0.2 / 0.5 (as `ga-grip3`), the other six 0.1 to 0.2 / 0.2 to 0.3; SIGHTS PASS, largest
angle 0.20 degrees on both guns (0.20 before). Hands check (`gl-hands`, combat shotgun): 0 regressions against
the baseline.

## 14. Hitches: the arms keep the controller, and the rig's own stalls (2026-10-19, OpenXR-Simulator)

**The one-off regressions.** Three hands checks on 2026-10-18 each reported one regression that a rerun cleared
(flex-L-y45, roll-L+0, roll-L+150), each on a 0.65 to 1.1 s gap in the dump at the start of that hold, where both
arms went back to the game (`game (untracked)`). The gaps are the game's own: the layer logs a `stall:` line for
each frame that long. Over the 103 earlier sweeps with a layer log, 90 of the 104 stalls logged during the poses
came within 50 ms after the layer read a new scripted input file (`controllers: test input ... read`), the read
the camera hook makes on the game's thread when the sweep has just replaced the file. The read now logs how long
it held its thread: in `sg-hands1` it took 300.3 ms once (a 274.6 ms stall) and 56.7 ms once (59.0 ms), median
0.4 ms. The same writes and reads outside the game (40 replacements each, with and without 3.4 MB files written
beside them as the captures are) took 11.6 ms at most, so the long reads need the game's load as well. With the
run folders (`tmp-vr\rs\hands\runs`) excluded from Defender's real-time scanning (allowed on the rig PC; set
once with `Add-MpPreference -ExclusionPath`), `sg-hands2` to `sg-hands4` read in 21.0 ms at most and logged no
stall in play. A rig that runs the sweep needs the same exclusion; without it the hands check's holds can start
with a long frame.

**Why a long frame handed the arms back.** The arm policy used the hands' poses only while they were at most 0.1 s
old at the read; that age is the gap since the last view, so any frame over 0.1 s handed both arms back for about
0.45 s (docs/VR_HANDS_HUD.md, "When the arms go back to the game"). A scripted hitch (`hitch-sweep.ps1`: the
game's process suspended for 150 or 400 ms, three of each with the Chaingun and with the Heavy Cannon held, the
hands still; `hs-sweep.sh`, checked by `hitch_check.py`):

| Run | Layer | Hitches handed back | Weapon arm weight after | Off hand's written wrist off its target |
|---|---|---|---|---|
| `sg-hb1` | dev main | 12 of 12 (both arms, `untracked`) | 0 (1 to 2 of the 4 dump lines in the next second posed by the game) | up to 24.0 cm while it blended back |
| `sg-hf2` | views by ticks only | 0 of 12 | 1.00 | 0.0, then from the Heavy Cannon's second hitch on growing to 36.3 cm and staying |
| `sg-hf3` | views by ticks and the clean read by ticks | 0 of 12 | 1.00 | 0.0 (HITCH PASS 12/12) |

In `sg-hb1` the right glove, caught mid-blend, was 62.9 and 71.6 cm from where it sat before the hitch on the
Chaingun. `sg-hf2` showed the second fault the hand-backs had hidden: the off hand's read counted as clean 150 ms
after the last write even while the layer was still writing, so after a hitch over 150 ms the solve fed on its own
override and the arm's reach shrank (0.74, then 0.53, 0.42, 0.36, 0.35 on the Heavy Cannon), the glove further off
the controller with each hitch. A read now counts as clean only 150 ms and 3 ticks after the last write on both
arms, as it already did for the wrist's change. Hands checks with both changes: `sg-hands2`, `sg-hands3` and
`sg-hands4`, 0 regressions each; `sg-hands1` (the first change only, no exclusion yet) had 7 (6 L:flipped-elbow,
1 L:twiststep), and no hand-back after its 274.6 ms stall.

The suspension stops the whole process, the XR worker and the simulator included; a hitch in the game's thread
alone leaves the worker running, which the rule does not depend on (the views and the ticks are both the game's).

## 15. The weapon in the left hand: the arms swap roles (2026-10-19, OpenXR-Simulator)

**The two attach joints are one pose** [rig: the arm dumps of the hands rig's runs since `ap-base-model`, every
line where the game had the left arm]. `lefthandattach` and `righthandattach` read the same position to 0.0 mm and
the same axes to 0.0000, both as animated and with the game's weapon-lag modifier, on the combat shotgun, the Heavy
Cannon, the chaingun, the ballista, the BFG and the Crucible; the bind pose has them at one point too [static:
marine.md6skl, (0.5715, 0, 1.2383)], and the left hand and fingers are the right ones reflected across the model's
x-z plane to 0.5 mm. The gun's root is turned up to 2.2 degrees from the model's axes (its y axis the model's left
to 0.991 or better on every gun dumped). So the left glove can be held in the left attach joint's frame as the
right glove is on a grip (section 13), each arm in the other's grip reflected across the root's x-z plane
(docs/VR_HANDS_HUD.md, "The weapon in the left hand").

**The sweep** (`tmp-vr\rs\hands\lefty-sweep.ps1 -Lefty`, grip-rest-sweep.ps1 with `ETERNALVR_HANDEDNESS=left`:
each hold mirrored onto the left controller, x, yaw and roll negated, the right controller at the left's rest
mirrored; the slot taps bound in `[map.left_button_swap]`, which the left-handed binding reads; checked by
`lefty_check.py`, the left glove's wrist against the left hand's wrist point in the grip's frame and the right
glove's against the right hand's in the right controller's).

| Run | Layer | Left glove from the left hand, median / largest, cm | Right glove (free off hand), cm | Sights largest, deg |
|---|---|---|---|---|
| `lf-free4` | the row reflected across the model's x-z plane through the root | 0.1 to 2.3 / 0.2 to 2.4 (combat shotgun 1.4 / 1.6, ballista 1.8 / 1.8, chaingun 2.3 / 2.4) | 0.0 / 0.0 | 0.20 |
| `lf-free5` | the model moved until the left glove's wrist is on the wrist point | 0.1 to 0.3 / 0.2 to 0.4 on all eight guns | 0.0 / 0.0 | 0.20 |

`lf-free4`'s misses were along the grip's left: the gun's root is turned from the model's axes (the combat
shotgun's by its own sight line), so the left glove's grip, reflected across the root's plane, is not the right one
reflected across a plane of the model's. Every gun's sight line reads as in the right hand (the dev main sweep of
section 11 gave the same largest angles per gun to 0.01 degree; the plasma rifle, the rocket launcher and the BFG
have no sight row).

**Firing** (`lf-fire3`, `ETERNALVR_OFFHAND=game`, the trigger held 3 s on the combat shotgun, the chaingun and the
ballista, the chaingun's mod held 2.5 s; `lf_ticks.py`, the weapon arm's glove trace per arm). The left glove's
wrist as read back in the gun's frame against where it was meant to land, per tick, median / 95th percentile /
largest, and the gun's root's move at the grip between the read the write was placed under and the frame drawn:

| Gun | Phase | Ticks | Left glove, mm | Right glove (off hand), mm | Root's move at the grip, largest, mm |
|---|---|---|---|---|---|
| combat shotgun | firing | 270 | 0.0 / 0.6 / 1.0 | 0.0 / 0.0 / 0.0 | 41.0 |
| chaingun | firing | 266 | 0.0 / 0.0 / 0.1 | 0.0 / 0.0 / 0.0 | 19.7 |
| ballista | firing | 267 | 0.0 / 0.0 / 3.3 | 0.0 / 0.4 / 15.0 | 156.3 |

The left glove rides the gun through the shots: the game moves the left attach joint with the gun's root in these
animations too. The chaingun's mod does not: while it is held the game puts the left attach joint 13 to 25 cm from
the gun's root (13 of the dump's lines; the trace above, measured in the left attach joint's frame, cannot see it).
In the dump, the left glove's wrist in the gun's root frame against its median over the posed lines: `lf-fire2`,
the grip held in the left attach joint's frame as read, 373.7 mm at most (133 mm median over those 13 lines);
`lf-fire3`, the grip placed on the gun's root as read each tick, 13.2 mm at most (0.0 median; the largest in the
first two lines of the mod, a frame behind as the joints part), and 0.6 mm on the combat shotgun, 0.0 on the
ballista and the BFG. The right glove's 15.0 mm is the right glove's own ballista figure of section 13 (its share of the
game's hand under it in the shots' first frames).

**Hand-backs** (`lf-hb1` before, `lf-hb3` after; the default off hand; the combat shotgun held, two melees, a
switch to the Heavy Cannon, a melee, a switch back; the model's place against the left hand in the dump, model
frame). Before, the gun was placed by the left glove's share on its grip, so each hand-back slid it to the right
glove's row and back: 2.0 cm on the shotgun's melee, 1.1 on the Heavy Cannon's (0.8 to 2.4 cm by gun, the
difference between the two rows, not the hands' distance). The grip taken from the last clean read was also
taken while the gun came back up after the melee: the shotgun's posed place then sat 8 cm from where it was
before the melee until the next switch (its posed place spread 6.8 cm over the run). After (the placement kept
while the game has the arms, a grip only from a root still for 3 reads, kept per held item): 0.0 cm on every
handed-back line of a melee, and the shotgun's posed place spread 0.4 cm over the run.

**The default off hand** (`lf-game3`, `ETERNALVR_OFFHAND=game`): the right glove holds each gun where the game's
left glove does, reflected. The left glove from the left hand: 0.1 to 0.2 cm median, 0.2 to 0.4 at most on all
eight guns; the sights check passes (0.20 degrees at most). The captures show the right glove on the combat
shotgun's fore-end from the right, and on the chaingun's right side, where the game's left glove holds its left.

**Right-handed play** (the same build): the grip sweep `rh-grip1` against section 13's `gl-grip` (dev main's
glove): every gun's glove from the real hand within 0.1 cm of it (GRIP PASS, 0.0 to 0.2 cm median, 0.5 at most),
the derived rows within 1 mm, the sights' largest angles within 0.01 degree (SIGHTS PASS); the merge-QA hands
check (`lf-hands2`, and `lf-hands3` after the hand-back fixes, the Release build): 0 regressions against its baseline, the same known-bistable warns
on roll-R+150 and the same standing R:reach finding as dev main's `sg-hands4`.

**Not run**: a headset; the Unmaykr, the Crucible, the hammer, glory kills and melee with the weapon in the left
hand.

## 16. The rest of the held items in both hands: DLC2, the hammer, the swim's and the Berserk's fists, quick actions (2026-10-19, OpenXR-Simulator)

The sweep of section 15 with a map and a plan per run (`hc-sweep.ps1`: `lefty-sweep.ps1` with `-Map`); a checker
for hand-backs and jumps, `hb_check.py` (per held item, who poses each arm over time; every step of the model's
placement against the weapon-hand grip, or of a glove, between two dump lines 0.25 s apart while the scripted input
has held for three lines). `lefty_check.py` now takes a row's own turn, reflected, out of the model's axis as
`grip_check.py` does (the Crucible's: without it the Crucible read 4.3 cm off the left hand). Before: the
lefty-arms layer (`hc-*` runs); after: this branch (`hc-a*`). Evidence: rig (the simulator gives the grip and the
aim one pose) and, where marked, static.

**The Ancient Gods, Part Two loads on the rig.** `+map game/dlc2/e5m1_spear/e5m1_spear` loads the World Spear as a
single-player map (the guard's `map load ... (single-player)`; captures of the village) and the debug schedule
runs there. The Sentinel Hammer still did not come out: `give weapon/player/hammer` (with `give all`), then the
Crucible's button (the left stick click right-handed, the right stick click left-handed) 13 and 28 s after the
give and again 126 s after it (a tap, then a 1.5 s hold): no held item change, no hand-back, the arms posed on the combat shotgun throughout
(`hc-d2r`, `hc-d2l`, `hc-d2w`). The hammer is left for a save that has it.

**The swim's fists** (`e4m1_rig`, `teleport bubbles_underwater_1`: under water, `weapon/player/fists_swim` in the
hands, the layer posing both arms). Right hand, the glove's wrist from the real hand's wrist point, median /
largest: 26.4 / 31.0 cm on the `default` row (24.3 ahead, 8.4 right, 4.3 up; `hc-swr`); the swim idle moves the
wrist about its median by 5.7 cm (median) and 10.7 at most over the holds, 22.7 cm side to side over the whole
swim. The row `[-0.493, 0.264, 0.177]` is that median's (grip_check's derived row). On it (`hc-a-swr`): 4.7 / 6.3
cm, the median 3.4 cm left of the first run's (the stroke's phase over the holds), the swing about the median 2.2 /
9.7 cm; going under water the model's placement changes by 34.2 cm (the shotgun's row to the swim's; 8.3 on the
`default` row) while the game has the arms for the swap. Left hand
(`hc-lf2`, the default off hand): the left glove 11.0 / 12.4 cm from the left hand, 10.8 cm ahead: the grip is
read at one moment of the stroke; the model moved 58.8 cm when it was read and 54.8 cm each time the held item
changed between the shotgun and the swim's fists at the surface. With the grips read only from the item's own
hold (`hc-c-lfs`) the steady rule still fires during the swim (the stroke has still moments: both arms posed for
6.2 s on the first dive), the left glove 11.5 / 12.4 cm from the left hand; on a second dive the arms were posed
from the first dive's grip (that dive's reads did not hold still: `not taken` logged). Under water going
in (`hc-c-wet`, right hand, captures every 12 frames): the shotgun lowers out of view, about 0.8 s with no right
hand in view, then the swim's hand rises in; the model's 34.2 cm change falls in those empty frames (no capture
shows a jump; the change frame itself was not captured).

**At the surface** [rig]. Floating at the water's surface the game puts the shotgun back in the hands but holds it
in a pose of its own: the right glove's wrist 34 cm (median) and 68 at most from the real hand (`hc-swr`, right
hand), the left glove 1.3 m behind the left hand (`hc-c-lfs`, the shotgun's grip read on land). Section 17.

**The Berserk's fists: not reached.** `chrispy pickup/powerup/berserk` twice with the weapon hand aimed straight
down (both signs of pitch; the player cannot walk on the rig), `give weapon/player/fists_berserk` (`hc-qa1`), and
section 12's `give powerup/berserk` and `give pickup/powerup/berserk`: none put `fists_berserk` in the hands. Plain
`weapon/player/fists` never shows (section 12).

**Quick actions, right hand** (`hc-rh1`, `hc-qa1`). A melee (`fists_doom5melee`, 0.25 to 0.5 s) and the chainsaw's
button (0.75 s) hand both arms back for the whole action (weights 0 on every line). The model's placement takes
the action item's `default` row at the change: 8.3 cm and 2.2 degrees from the combat shotgun's, 38.1 cm and 0.4
from the chaingun's, 48.7 cm and 37.0 from the Crucible's, the same back. Captures every 12 frames through a
Crucible melee: the game's punch (the left fist across the view), the right arm coming back up, the Crucible in
the right fist within about 0.7 s, no frame with the Crucible away from the hand; the frame of the change was not
captured and the dump has no joints while the game has the arm, so a step of the glove itself there is not
measured.

**Left hand, the Unmaykr, the Crucible and the chaingun** (`hc-lf1`, the default off hand). The left glove's wrist
from the left hand's wrist point, median / largest: Unmaykr 0.2 / 0.3 cm, Crucible 0.2 / 0.5, chaingun 0.2 / 0.3,
combat shotgun 0.1 / 0.2. Captures: the left fist on the Crucible's hilt, the blade up. Jumps while the game had the
arms, the model's placement against the left grip: through the Unmaykr's first raise (5.9 s) 16.6, 19.9, 19.2 and
18.0 cm; through the Crucible's (1.3 s) 66.9 cm and 59.3 degrees, then 13.9 cm; at a melee from the Crucible 87.4 cm
(48.7 in the right hand), from the combat shotgun 53.8 cm (8.3), and at the chainsaw's button 9.4 cm. The cause: the
game's grips were taken from any read that held still, also while the game played the raise or the punch, and the
melee's fists kept a grip of their own (kept per held item), so the left glove's placement followed those poses.
The fix (VR_HANDS_HUD.md, "Grips only from the item's own hold"): grips are read only while the weapon arm's rules
have no reason of their own to give the arms to the game. After (`hc-a-lf1`): no step while the game had the
arms other than at an item change, where it is the rows' difference as in the right hand (15.1 to 16.5 cm between
the combat shotgun, the BFG and the Unmaykr); a melee from the combat shotgun 9.4 cm, from the chaingun 38 cm, from
the Crucible 68.6 cm and 37 degrees (the Crucible's left-handed placement against the fists' `default` row; 48.7 in
the right hand); the left glove on the left hand as before (Unmaykr 0.2 / 0.3, Crucible 0.2 / 0.5, chaingun 0.2 /
0.4, combat shotgun 0.1 / 0.2 cm). The Crucible's first raise still moved the model by 68.6 cm and 59.3 degrees
when its grip was read (0.25 s before the arms blend in): until then the model sits by the right glove's row, and
the left glove's place is not known before its grip is read. Placed by the row's turn reflected from the first
frame the move was 68.6 cm without the turn (`hc-b-lf1`), and by the whole row reflected 53 cm (computed from the
rig's Crucible sample: the hilt is held 21 cm right of the model's middle), so neither was kept. Instead the grips
are kept by the item's name across new hands (`arm::GripBook`). Across a level load (`hc-d-lvl`, the debug
schedule's `map` loading e1m1 again; the default off hand): on the first level the Crucible's first raise moved the
model 68.5 cm and 59.3 degrees; on the reloaded level it sat at its left-hand placement from its first frame, no
step, and the combat shotgun and the BFG took their kept grips at once (logged `the grips kept from earlier hands`);
the shotgun's placement moved 0.5 cm when its grip was read on the new hands, the correction a weapon mod's own
idle would add on top (the decl does not name the mod; not measured per mod). Not explained: during the reloaded
level's start, while the game had the arms for 28 s before the player had control, the model sat 25 cm higher
against the grip and came back 0.25 s before the arms blended in (the first level's start had no placement to
compare).

**The chaingun's right glove with the default off hand** [static: the chaingun's idle, frame 0, and its mesh, build
25216728]. The game's left finger joints sit 2.5 to 6.1 cm (median 4.1) from the chaingun's mesh, on a side handle
at the gun's left (the mesh's parts at y 0.15 to 0.46 m in the root's frame; on the right nothing past 0.24 m);
reflected across the root's x-z plane, 10 to 17 cm (median 13.1). On the other guns the reflected grip is as close
to the mesh as the game's (medians, as held / reflected: combat shotgun 2.3 / 2.3, Heavy Cannon 2.1 / 2.1, super
shotgun 2.5 / 2.5, plasma rifle 1.3 / 1.3, rocket launcher 2.5 / 2.5, ballista 2.5 / 2.5, BFG 2.9 / 2.9, Unmaykr
4.7 / 4.7 cm; Crucible 0.9 / 1.1). The capture of section 15 (`lf-game3`) shows the right fist beside the
chaingun's right side. The change (DECISIONS T-141): on the chaingun the right glove follows the right controller. After (`hc-a-lf1`): its
wrist from the right hand's wrist point 0.0 / 0.0 cm over 18 posed lines, the left glove 0.2 / 0.4 cm, the log line
once per equip.

**Regression** (this branch). Right hand: the grip sweep `hc-agrip` against section 15's `rh-grip1`, every gun's
glove within 0.1 cm (GRIP PASS, 0.1 to 0.2 cm median, 0.5 at most), the derived rows within 1 mm; the sights'
largest angles within 0.005 degree (SIGHTS PASS); the merge-QA hands check (`hc-ahands`, the Release build): 0
regressions against its baseline, the same three known-bistable warns on roll-R+150 and the standing R:reach finding
(FAIL 72/72, as `lf-hands3`). Left hand, the free off hand (`hc-alfree`): the left glove 0.1 to 0.3 / 0.2 to 0.4 cm on
all eight guns, the right glove 0.0 / 0.0 (LEFTY PASS), as `lf-free5`.

**Not run**: a headset; glory kills in either hand (no staggered demon on the rig); the hammer; the Berserk's
fists.

## 17. The water's surface: the arms swim with the gun in the slot (2026-10-19, OpenXR-Simulator)

`e4m1_rig`, `teleport bubbles_underwater_1`, then no input for 50 s: the camera does not move in any capture, and the
held item changes every 0.5 to 12 s between the swim's fists and the gun held before the dive (the shotgun, or the
Heavy Cannon in one run). The runs before (`hw-m-r`, `hw-m-l`: dev main's layer) and after (`hw-c-r`, `hw-c-l`; `hw-b-*`
an earlier build of the change) take captures every 12 frames through it (`hw-sweep.ps1`, `hc-sweep.ps1` with this
branch's owner and worktree; `hw-m1.sh`, `hw-a1.sh`; the dump checked by `hb_check.py` and `water_check.py`, which
splits it by state). Evidence: rig, and static where marked.

**What the game does there.**

- **The arms keep the swim's pose; the gun's root stays where the swim parks it** [rig: the dump's joints, every line
  the layer posed]. With the gun in the slot the right attach joint (`righthandattach`, the gun's root: the gun hangs
  from it) sits 1.0 to 1.1 m behind the model's origin and 0.5 m down, turned 37 degrees up, the glove's wrist 1.46 to
  1.51 m from it: the same poses as with the swim's fists held (the wrist in the root's frame (1.30 to 1.33, -0.24 to
  -0.47, -0.59) m in both). On land the root sits at (0.27, -0.12, -0.14) and the wrist 0.20 m from it. The left
  attach joint is the gun's root's pose to 0.0 mm and 0.3 degrees in the water too (`hc-c-lfs`, `hw-m-l`): the gun's
  root does not move against the attach joints; both are parked with the swim's arms. So it is the same held item,
  not held: the gun out of view behind the camera, both gloves open in the stroke.
- **Captures** [rig]: in the gun's periods only the swim's open hand shows (`hw-m-r`, 13:34:19 and 13:34:32: no gun).
  The gun shows for one or two captures (0.1 to 0.3 s) as the game raises or lowers it at a change of the held item,
  the glove closed on it, while the game has the arms for that action (`action 10`, BringDown); with the weapon in the
  left hand nothing shows in those periods on dev main (`hw-m-l`: the left glove on the gun's parked root, 1.3 m back).
- **The hands' state** [static: the reflected type info of build 25216728; rig: the layer's trace lines]. The hands'
  flag `swimming` (idHands::handsFlags_t, bit 35) is set from 0.2 to 0.9 s after the teleport through the swim's fists and
  the gun's periods (69 of 79 trace lines in the water in `hw-a-r`, 142 of 169 in `hw-m-r`; clear only on the lines
  of a lower's first tick and through some lowers), and on none of 3,820 trace lines on land over the 162 runs logged
  (`swimming` sits between `startingQueuedBurst` and `shouldPerformOneHitMeleeKill` in the bitfield, which also puts
  the policy's reloading, throw, melee, switch, mod and custom-animation bits where offhand_policy.hpp has them). The
  hands' destination state is `HS_SWIM_IDLE` (12; the reflected enum has HS_NONE -1, HS_IDLE 0) through the gun's
  periods and HS_IDLE through the swim fists' after the first; `idPlayer::weaponFistsSwim` (+0x4D188, "swim fists
  decl") holds the swim's fists.
- **No shots** [log]: the trigger held 12 s across a gun's period (`hw-b-r`, `hw-c-r`, `hw-c-l`) fired nothing (the
  layer's shot counter logged no shot in any of the three runs).

So at the surface the game swims: the gun is in the slot, not in the hand. Placing the gun in the hand would mean
writing the gun's root, which the game parks away from both gloves, into an open swimming hand; the decision is to
place and hold the arms as the swim's fists' while the hands swim (DECISIONS T-142).

**Before and after**, the weapon hand's glove (the wrist) from the real hand's wrist point, median / largest over the
lines the layer posed at weight 1, cm; "settled" leaves out the first two lines (0.5 s) after the arms blend in:

| Hand | State | Before (dev main) | Before, settled | After | After, settled |
|---|---|---|---|---|---|
| right | the gun in the slot, the arms swimming | 29.7 / 38.4 (`hw-m-r`, Heavy Cannon); 34.4 / 42.6 (`hc-swr`, shotgun) | 28.3 / 38.4 (`hw-m-r`); 34.6 / 42.6 (`hc-swr`) | 6.2 / 38.1 (`hw-c-r`) | 5.7 / 17.1 |
| right | the swim's fists held | 5.4 / 84.0 | 5.8 / 18.4 | 5.2 / 40.9 | 5.2 / 17.9 |
| left | the gun in the slot, the arms swimming | 127.9 / 141.0 (`hw-m-l`) | 127.8 / 137.5 | 11.0 / 12.4 (`hw-c-l`) | 11.2 / 12.4 |
| left | the swim's fists held | 11.6 / 12.4 | 11.6 / 12.4 | 11.4 / 12.4 | 11.7 / 12.4 |
| right | the shotgun on land | 0.3 / 2.8 | 0.3 / 0.4 | 0.3 / 2.8 | 0.3 / 0.4 |
| left | the shotgun on land | 0.6 / 0.7 | 0.6 / 0.7 | 2.4 / 3.2 | 2.4 / 2.5 |

The surface's offset before the change is quoted elsewhere (VR_HANDS_HUD.md, DECISIONS T-142, offhand_policy.hpp) by
one statistic, the median of the settled lines: 28.3 to 34.6 cm (the Heavy Cannon, the shotgun). The left-handed land
line differs by run, not by build: its grip is the one read while the gun's root holds still
(the shotgun's read in `hw-b-l` and `hw-c-l`: 2.3 / 3.4 and 2.4 / 3.2; `hw-m-l` 0.6 / 0.7), and the regression sweeps
below are the measure of the land placement. The swim's own stroke moves the glove about its row's point (about
5 cm median, 17 cm at most, right hand).

**Transitions** (`hb_check.py`, the model's placement against the weapon-hand grip between dump lines 0.25 s apart,
the scripted input held). Before, every change of the held item in the water stepped the model by the rows'
difference (34.2 cm on the shotgun, 28.3 on the Heavy Cannon; left-handed 55.0 cm): 18 steps in `hw-m-r`'s 57 s in the
water (17 of 28.3 to 34.2 cm), 8 in `hw-m-l`. After, none at the changes (left-handed 0.0 cm on all 16), and one step and back in `hw-c-r`
(34.2 cm, 0.25 s, the game's arms), where both signals were clear through a lower. Going into the water both builds
step once (34.2 cm on the shotgun, 35.9 left-handed), at the same moment. A first build that took the swim's row only
in HS_SWIM_IDLE (`hw-a-r`) stepped 14 times in 54 s: the game's raise and lower at each change run in HS_IDLE, so the
row went to the gun's and back around each of them, while the hand on view was mostly still the swim's.

**A grip taken from the gun's pose** (`hw-b-l`, the earlier build without the read rule below). The hands swim a tick
before their pose leaves the gun's: at the dive the left-handed arms read the shotgun's land grip as the swim fists'
(still for 3 reads, no switch handing the arms back), and both the gun's periods and the swim fists' put the left
glove 127.8 / 140.7 and 127.5 / 128.3 cm from the left hand. The grips are now read only while the arms hold the held
item itself (left_hand_arms.cpp): `hw-c-l` 11.0 / 12.4 and 11.4 / 12.4.

**The way out and the hold-off** (review fix, after `e6982154`). The grips are now read only while the hands do not swim
and last swam 1.0 s ago or more (input::gripTakingAllowed, kSwimGripHoldOffSeconds), checked on the swim's signal itself
as well as on the arms' item (with the swim fists unknown the arms' item is the held one while the hands swim), and the
arms' item, the swim and the time since it are read once per tick of the hands by the viewmodel hook and passed to the
rows, the grips, the captures and the dump (ArmsTick). Runs: `e4m1_rig`, left-handed with the game's off hand, `teleport
bubbles_underwater_1` at 30, 62 and 94 s and `teleport checkpoints_player_start_18` (a deck) at 52, 84 and 116 s
(`hw-x1.sh`; `setviewpos` moves only the view, so the player goes out by teleport); `hw-x-o` the build before the fix,
`hw-x-n` and `hw-x-n2` after (`n2` the final build); `exit_check.py` splits the dump at each way out.

- **What the game does on a teleport out** [rig: trace and dump]. The hands' flag clears within 50 ms of the teleport; the game holds
  the arms for a forced view and then a left-arm animation (its raise), and the first line the layer posed after it, 0.5
  to 1.0 s after the flag cleared, has the gun's root on land (the shotgun (0.27, -0.11, -0.14), the Heavy Cannon (0.36,
  -0.11, -0.14)); the root was at the swim's parked pose on no line after the flag cleared (9 ways out over the three
  runs). The raise alone keeps the grips from being read there (readsGameGrip), on both builds.
- **In the water the swim's signal clears for a tick** at most changes of the held item (the layer's lines: the hands
  stop swimming, then swim again within 31 ms; 12 such in `hw-x-n`'s first 21 s in the water) and for one dump line
  (0.25 s) at some lowers. So the hold-off is longer than those (one tick to 0.25 s) and as long as the longest raise
  after a way out (0.5 to 1.0 s): 1.0 s. On land it only delays by that long a grip not read yet; a way out with no raise (swimming to a
  shore, climbing out) was not run, as the rig's player does not move.
- **The kept grips before and after** (the left glove's wrist from the real left hand on land, median / largest, cm,
  from 2 s after each way out; the shotgun, else the item named). Before the fix (`hw-x-o`): 0.6 / 0.7 before the first
  swim, then 0.4 / 0.5, 0.4 / 0.5 and 0.4 / 0.5 (Heavy Cannon). After (`hw-x-n2`): 2.4 / 19.1 before (19.1 and 4.5 on
  two lines early in the level, before any swim; the grip read at another moment, as section 17's land line), then 2.4 /
  2.5, 0.5 / 0.6 (Heavy Cannon) and 0.4 / 0.5; `hw-x-n` 0.6 / 0.8, then 0.5 / 0.6, 0.5 / 0.6 and 0.6 / 0.8. Every grip
  the final build kept was read with the gun's root on land (its log: the shotgun at (0.272, -0.103, -0.139) m before
  the first swim, the Heavy Cannon at (0.360, -0.108, -0.137) 1.06 s after the second teleport out, the shotgun at
  (0.273, -0.115, -0.140) 1.06 s after the third), none at the parked pose (x about -1.0). The way out by teleport did
  not reach the hazard on the build before the fix either: the case the hold-off covers (the flag clear for longer than
  a few ticks with the swim's pose still on the arms and no raise) has not been seen on the rig.
- **Regression** (final build): the grip sweep `hw-dgrip` against `hw-cgrip`, every gun's glove median and largest the
  same (GRIP PASS), the sights' largest angle per gun within 0.002 degree (SIGHTS PASS); the left-handed free off hand
  `hw-dfree` the left glove 0.1 to 0.3 / 0.2 to 0.4 cm on all eight guns, the right glove 0.0 / 0.0 (LEFTY PASS), as
  `hw-cfree` within 0.1 cm.

**Regression** (this branch, after the change; on land the hands never swim, so the arms go by the held item as
before). Right hand: the grip sweep `hw-cgrip` against section 16's `hc-agrip`, every gun's glove the same to 0.1 cm
(GRIP PASS, 0.1 to 0.2 cm median, 0.5 at most), the derived rows within 1 mm; the sights' largest angles within 0.005
degree (SIGHTS PASS; the double-barrel 0.152 to 0.157, the value of section 15's `rh-grip1`); the merge-QA hands check
(`hw-chands`, the Release build): 0 regressions against its baseline, two known-bistable warns on roll-R+150 and the
standing R:reach finding (FAIL 72/72, as `hc-ahands`). Left hand, the free off hand (`hw-cfree`): the left glove 0.1
to 0.2 / 0.2 to 0.4 cm on all eight guns, the right glove 0.0 / 0.0 (LEFTY PASS), as `hc-alfree`.

**Not run**: a headset; a swim that reaches the surface by moving (the rig's player does not move); leaving the water
onto land by swimming or climbing out (the rig cannot walk the player out; it was teleported out, above); other guns
than the shotgun and the Heavy Cannon in the slot.

## 18. Weapon sights for every gun, and the active weapon mod (2026-10-22, OpenXR-Simulator)

Feature step 2's exit (docs/ROADMAP.md, "Plan from v0.1.49"): with the eye on the sight line, the sight line parallel to
the aim ray within 0.25 deg for every weapon. Section 6 left the precision bolt (one row served both Heavy Cannon mods),
the plasma rifle, the rocket launcher, the BFG and the Unmaykr. Evidence: rig (the simulator and the scripted input),
Steam build 25216728; not a headset.

**The active mod.** The held item's decl stays the base weapon's whichever mod is active, but the weapon object in the
hands' right item (idHandsItem::item, its decl checked against the held one) carries one override per fire mode,
`idWeapon::weaponModOverrides[2].weaponDecl`, and that decl names the active mod. The layer reads both every update of
the hands (`src/vkcore/weapon_mods.cpp`; the zoom look reads the same fields) and logs each change (`held item '...'
(mods '...', '...')`, then `mods changed on held '...'` on a switch). Seen on the rig, both mods of each weapon (the
switch is the left controller's top button, `switch_weapon_mod`):

| Weapon | Overrides (fire mode 0, 1): mod A / mod B |
|---|---|
| combat shotgun | -, `shotgun_secondary_pop_rockets` / -, `shotgun_secondary_full_auto` |
| Heavy Cannon | -, `heavy_cannon_bolt_action` (precision bolt) / -, `heavy_cannon_burst_detonate_mastery` (micro missiles) |
| plasma rifle | -, `plasma_rifle_secondary_aoe` / -, `plasma_rifle_secondary_microwave` |
| rocket launcher | `rocket_launcher_detonate`, - / -, `rocket_launcher_lock_mod_mastery` |
| ballista | -, `gauss_rifle_ballista` / -, `gauss_rifle_destroyer` |
| chaingun | `chaingun_turret_primary`, `chaingun_turret_secondary` / `chaingun_energy_shell_primary`, `chaingun_energy_shell` |
| super shotgun | `double_barrel_primary_lockon`, `double_barrel_meat_hook` (one mod) |
| BFG | `bfg_charge_sphere_burn`, - (no mod to switch) |
| Unmaykr | -, - |

The switch shows in the same update as the zoom look's own read of the fields (both log lines at the same moment), so a
row keyed by a mod takes over on the next frame drawn. On Game Pass 1.0.56.0 idHands, idHandsItem, idInventoryItem,
idWeapon and weaponModOverrides_t have the same size and every field the same offset as on Steam (the two images' type
info tables, read with `tools/typeinfo/extract_typeinfo.py`) [static]; no signature is involved. `[sights]` rows can
now be keyed by a mod's decl (`WeaponOffsetTable::lookupSight` with the mods: a row for an active mod's decl wins over
the weapon's own; a weapon whose mods have no row keeps its row). A mod's row turns the gun about the glove's wrist
instead of the grip (`xr_math::translationKeepingWrist`): the offset rows are measured with the weapon's own turn, and
3.3 deg about the grip would move the wrist 0.5 cm.

**Which mods change the sight line.** Idle: none. Each weapon's line in the model's frame sits at the same centre in
both mods, within 0.01 deg (`s2-off1` below: shotgun (0.205, 2.173) and (0.206, 2.167); the Heavy Cannon's irons line
(-3.455, 0.185) with the micro missiles, the bolt's idle carrying the same irons line; plasma rifle (-0.143, -0.003) and
(-0.144, -0.003); rocket launcher (0.040, 0.141) and (0.041, 0.140); ballista (-0.831, 1.075) and (-0.829, 1.075);
chaingun (0.318, -0.011) and (0.317, -0.013)). Sight parts: only the Heavy Cannon's. The captures of the neutral hold
show the precision bolt's scope on top of the gun and, with the micro missiles, no scope: the rail with the rear iron
sight and the missile pods beside it; the other guns' tops are the same in both mods (the shotgun's mods hang under the
barrel). The precision bolt's line is its scope: the bolt's zoom pose's eye axis, 0.20 deg above the bore, which runs
through the scope's tube within 2 mm (the sights survey) [static]; the iron sights stay the line for the micro missiles
and with no mod.

**The Unmaykr on the rig.** `give weapon/player/unmaykr` after `give all` in the debug schedule (section 12): the first
slot-8 touch brought it out, and the weapon-mod switch then changed between it and the BFG (they share the slot).

**Runs** (the sights sweep on the rig PC, `tmp-vr\rs\sights2\sights2-sweep.ps1`: section 6's sweep with the Unmaykr
given and, per slot, a 0.4 s touch, five holds, the weapon-mod switch, five holds; slot 8 twice; 90 holds, 2 or 3 dump
lines each; the dump now also writes `mods`, the held weapon's overrides, and `sight`, the row the layer turned it
by): `s2-off1` this branch with `ETERNALVR_WEAPON_SIGHTS=0` (the rows' source: each new row is the centre of the
line's spread there, as in section 6), `s2-main1` the 0.1.75 build (dev main: the five rows of section 6, the mod not
read), `s2-on1` this branch. The checker next to it (`sights2_check.py`: section 6's `sights_check.py` per weapon and
mod) picks the weapon-space line by the dumped mods (the precision bolt's scope, else section 6's lines); for
`s2-main1`, whose dump has no mods, the mod per hold group comes from its zoom look lines (the bolt first, as in the
other runs).

| Weapon (mod) | Line in weapon space | Row (down, left) | Spread (down, left) | Sights off | 0.1.75 | This branch |
|---|---|---|---|---|---|---|
| combat shotgun (both) | receiver top to bead | 0.21, 2.17 | 0.15, 0.31 | 2.34 deg | 0.161 deg | 0.162 deg |
| Heavy Cannon (precision bolt) | the scope's axis | **-0.15, 0.15** (new) | 0.09, 0.31 | 0.33 deg | 3.340 deg | 0.170 deg |
| Heavy Cannon (micro missiles) | iron sights | -3.45, 0.19 | 0.09, 0.38 | 3.50 deg | 0.200 deg | 0.195 deg |
| plasma rifle (both) | bore | **-0.14, 0.00** (new) | 0.38, 0.01 | 0.33 deg | 0.334 deg | 0.194 deg |
| rocket launcher (both) | bore | **0.04, 0.14** (new) | 0.09, 0.31 | 0.30 deg | 0.306 deg | 0.160 deg |
| super shotgun | rear sight to muzzle top | 0.69, 0.16 | 0.09, 0.30 | 0.79 deg | 0.153 deg | 0.152 deg |
| ballista (both) | bore | -0.83, 1.08 | 0.40, 0.07 | 1.51 deg | 0.202 deg | 0.204 deg |
| chaingun (both) | zoom pose's eye axis | 0.32, -0.01 | 0.37, 0.19 | 0.51 deg | 0.204 deg | 0.202 deg |
| BFG | bore | none | 0.05, 0.00 | 0.057 deg | 0.057 deg | 0.057 deg |
| Unmaykr | bore | none | 0.05, 0.00 | 0.052 deg | 0.052 deg | 0.052 deg |

The largest angle to the ray per weapon over both mods' holds (`s2-main1`: three over 0.25, SIGHTS FAIL; `s2-on1`:
SIGHTS PASS, every weapon and mod within 0.21 deg, the idle sway as in section 6). The BFG's and the Unmaykr's bores run
within 0.06 deg of the ray unturned, so they keep no row. The precision bolt unturned (0.33 deg) is closer than with
the irons' row (3.34 deg): its scope sits near the bore, the irons 3.5 deg above it.

**The glove** (the grip checker of section 10 with `--glove`, the rig PC's copy that takes the row each dump line
names): the Heavy Cannon's glove wrist 0.1 / 0.3 cm (median / largest) from its target with either mod's row, every
other gun 0.1 to 0.2 / 0.2 to 0.5 cm (GRIP PASS). The offset rows the sweep derives for the plasma rifle and the
rocket launcher with their new turns are their rows within 1 mm, so those stay; the bolt's derived row is the irons'
row moved by the wrist-keeping step, (-0.270, 0.113, 0.209) against (-0.270, 0.113, 0.205).

**The weapon in the left hand** (review fix): the wrist-keeping step now takes the turn the model is placed with, the
row's turn reflected for the left glove included (`arm::rowPlacement`), and leaves an `ETERNALVR_VIEWMODEL_OFFSET`
untouched; unit tests in `left_hand_tests.cpp` fail on the first version (the wrist kept under the row's own turn).
Run `s2-lefty1` (the same sweep left-handed, section 15's mirrored holds, the free off hand; the combat shotgun, then
the Heavy Cannon with the bolt, the micro missiles, the micro missiles and the bolt again, 10 holds per mod), checked
with section 15's left-handed glove checker taking the dumped row: the left glove's wrist 0.1 / 0.2 cm from the left
hand's wrist point with either mod (median per axis within 1 mm of each other), the right glove 0.0 / 0.0 (LEFTY
PASS); the sight line 0.168 deg at most with the bolt, 0.200 with the micro missiles (SIGHTS PASS). These holds are
all at the left glove's full share; the blend while its wrist is first read is covered by the unit tests only.

**Not checked**: a headset (how the lined-up sights look and feel, and whether the 3.3 deg turn at a Heavy Cannon mod
switch is noticed: open until the owner's headset look); the Heavy Cannon with no mod (the save has both; the irons'
row applies by the key rules); the precision bolt zoomed (its zoom pose puts the scope on the model's forward, so
while zoomed the bolt's row leaves the scope about 0.2 deg off the ray, within the 0.25 deg budget) [static]; the
plasma rifle's, rocket launcher's, BFG's and Unmaykr's lines are their bores (no sight parts; the survey found none);
the mod read on Game Pass at run time (its type info is the Steam build's, above). On a weapon switch the mods were
already set on the first update with the new weapon (every switch's first held-item line in `s2-off1` and `s2-on1`
names them; 0 `mods changed` lines outside the mod switches and the level's start), so no frame on the weapon's own
row instead of its mod's was seen; a weapon whose object lagged its decl would show its own row for that update.

## 19. Arm solve and model, chunks A and B: room, and the solve's open fixes (2026-10-22/23, OpenXR-Simulator)

Feature step 3 (docs/ROADMAP.md, "Arm solve and model"). Chunk A makes room without a change of behaviour; chunk B
fixes what the bind solve (`ETERNALVR_ARM_SOLVE=bind`) still had open. The default solve stays `animated` until the
owner's headset look. Evidence: rig (the simulator and the scripted input), Steam build 25216728; not a headset.

**A: room.** The free off hand's arm moved out of `offhand_hook.cpp` (599 to 426 lines) into `offhand_arm.cpp`, the
arms' writes and give-back out of `game_arm.cpp` (589 to 428 lines, 431 after B) into `game_arm_write.cpp` (their shared state in
`game_arm_cache.hpp`), the weapon arm's state and trace out of `weapon_arm.cpp` (556 to 442, 444 after B) into
`weapon_arm_state.hpp` and `weapon_arm_trace.cpp`; every difference between the two solves is asked of one helper,
`arm::SolveMode` (`features/arm/solve_mode.hpp`, `vkcore/arm_solve_mode.hpp`). On 0.1.78, Release:

| Run | Layer, solve | Against | Result |
|---|---|---|---|
| `ab-a-def` | A, default | the baseline | 0 regressions; known-bistable warns on roll-R+150 only |
| `ab-a-bind` | A, `bind` | `ab-m-bind` (origin/main 9b56efcf, `bind`) | the same verdicts in 72 of 72 holds; both 0 regressions against the baseline |
| `ab-a-bind` | A, `bind` | section 9's `r4-bind3` | no check fails that passed there; there the sight (every hold), mirror and six right-arm checks also failed, since fixed on main (sections 11 to 18) |

The arm lines of the layer logs are the same (the log lines of `ab-a-bind` against `ab-m-bind`, numbers and handles
taken out: the arm's lines alike, the rest handles and the end of the session; `ab-a-def` against the merge QA run of
the same day: no arm line differs).

The viewmodel hook's camera-hook half (the game's view: the hands' poses relative to the eye, the weapon FOV) moved
out of `viewmodel_hook.cpp` (567 to 349 lines) into `viewmodel_view.cpp`, on top of B and 0.1.79 (`ab-c-def`: 0
regressions, the same verdicts as `ab-b-def` but the roll-R+150 warns; `ab-c-gripd`: GRIP PASS, SIGHTS PASS, 0.204
degrees at most; the log lines alike but the session's end).

**B, the weapon arm's landing per gun.** `landed` (the layer's own measure, its joints read back against where they
were written), the grip sweep's holds with `bind`, per held item and mod, the settled lines at full weight: at most
1.6 mm on every gun and mod (the combat shotgun 1.5 / 1.6 mm median / largest; the other eight guns and the Unmaykr
0.1 to 0.2 / 0.2 to 0.5) before the B changes (`ab-a-grip`) and after (`ab-b-grip`); with the weapon in the left hand
both arms at most 1.0 mm (`ab-a-lefty`, `ab-b-lefty`). No change was needed. The default solve, which does not write
past its targets, reads 4.5 to 35 mm (`ab-b-gripd`, as dev main's sights runs), the read 95 % of the way.

**B, the ballista's shoulder end.** With the glove on a grip every modifier is held in its parent's frame
(section 13), so in the frame of a shot the forearm, elbow and shoulder end ride the gun's kick and the shoulder end
jumps for that frame. With `bind` the shoulder end (the arm's last joint) is now a model-space override
(`arm::SolveMode::modelShoulder`); the glove and the forearm still ride the gun. The firing run: the ballista's trigger
held 3 s twice, the chaingun's once, `bind`, the glove trace (`fire_check.py`; the trace now also gives the shoulder
end's step from the read of the frame before):

| Run | Gun | Glove off its grip in the gun's frame, median / p95 / largest | Shoulder end off its target, largest | Its largest step between frames | The gun's root at the grip, largest |
|---|---|---|---|---|---|
| `ab-a-fire` (before) | ballista | 0.0 / 1.3 to 1.8 / 15.0 mm | 166.7 mm | (not traced) | 157.7 mm |
| `ab-b-fire` (after) | ballista | 0.0 / 0.6 to 1.5 / 15.0 mm | 30.8 mm | 31.0 mm | 157.6 mm |
| `ab-b-fire` (after) | chaingun | 0.0 / 0.0 / 0.0 mm | 4.9 mm | 1.3 mm | 17.8 mm |

What is left in the shot's first frame [inferred]: the blend mixes each joint at 0.95, so the shoulder end keeps the
0.05 share of the game's own upper arm, which the shot's animation swings under it (the same share as the glove's
15.0 mm, section 13); the layer reads it a frame late. Unit test (`parent_frames_tests.cpp`, a 16 cm and 10 degree
kick in one frame): the shoulder end's largest step 186 mm held in its parent's frame, 15 mm in model space.

**B, the Crucible's first raise with the weapon in the left hand.** The `[left_hand_wrist]` row (DECISIONS T-146)
places the model in the left hand from the item's first frame until its grip is read. The left-handed sweep (every
gun's holds, then the Crucible's button: its first raise of the session), `hb_check.py --lefty`, the model's largest
step against the left hand between dump lines with the same input:

| Run | Solve | Through the Crucible's first raise | LEFTY |
|---|---|---|---|
| `ab-a-lefty` (before) | `bind` | 68.7 cm and 59.3 degrees, as its grip was read | PASS |
| `ab-b-lefty` (after) | `bind` | 0.1 cm and 0.2 degrees | PASS |
| `ab-b-leftyd` (after) | default, the game's off hand | 0.2 cm and 0.2 degrees | PASS |

The row is the median of the posed left wrist on the Crucible at idle (two runs within 0.4 mm). At the change of the
held item the model still steps by the rows' difference while the game has the arms (shotgun to Crucible 73.0 cm and
38.4 degrees, as at every later switch). LEFTY: the left glove 0.1 to 0.5 cm (median / largest per gun, the Crucible
0.2 / 0.5) from the left hand in all three runs, the free right glove 0.0 / 0.0 cm.

**Regression, B on 0.1.79** (Release): the hands sweep `ab-b-def` 0 regressions (the roll-R+150 warns),
`ab-b-bind` 0 regressions and the same verdicts as `ab-m-bind` in 72 of 72 holds; the planted twist fault
`ab-b-fault` (`bind`) 62 regressions (the right twist step in 61 holds, the left in 11, collapse in 7 left and 8
right); GRIP PASS (`ab-b-grip`, `ab-b-gripd`, every gun's glove 0.1 to 0.2 / 0.2 to 0.5 cm), SIGHTS PASS in every run
(0.204 degrees at most).

**Review fixes** (on 0.1.79, Release): the `[left_hand_wrist]` row (metres) is taken at the world's scale (it was
used as game units, off by `ETERNALVR_WORLD_SCALE` away from 1; the rig runs at 1), and once the game's grip has
placed the held item, a kept grip that stops holding (its root moved) leaves the wrist that grip gave instead of
going back to the row, or with no row to the right glove's placement (`arm::LeftWristOnGun`; unit tests, each
failing on the code without its fix). Rig: `ab-d-lefty` (`bind`) and `ab-d-leftyd` (default) LEFTY PASS with the
same per-gun figures as `ab-b-lefty`/`ab-b-leftyd` but the Crucible's left glove median 0.3 cm (was 0.2) in
`ab-d-lefty`; the Crucible's first raise 0.2 cm and 0.2 degrees (`bind`), 0.1 cm and 0.2 degrees (default);
SIGHTS PASS (0.204 degrees at most); the hands sweep `ab-d-def` 0 regressions, the same verdicts as `ab-b-def` but
the roll-R+150 warns.

**Not run**: a headset (the shoulder end and the upper arm in a ballista shot, the Crucible's first raise in the
left hand); the upper arm's shape in the shot's frame (no capture lands on it); a world scale other than 1 on the
rig.

## 20. Hands only, the arms model rule and hand poses (feature steps 3 and 4, 2026-10-24, OpenXR-Simulator)

Feature step 3's chunk C (hands only as the third value of Show arms, the model rule; DECISIONS T-148) and feature
step 4 (the free off hand's fingers from its controller; T-149), both behind New arms or hands only and off by
default. Every run here is the branch's Release at its head (the fix of the left-handed check, below, included),
Steam build 25216728. Evidence: rig (the simulator and the scripted input), not a headset.

**The hands-only checks** (`check_hands.py --hands-only`, `hands_only.py` beside it on the rig PC). The arm checks
of section 3 measure an arm hands only does not draw (bones, twist step, swing, the forearm's flicker and wrap,
collapse, flipped), so per posed arm they are replaced by: `zero` (the dump's new `zero` mask holds the six folded
joints and not the wrist or the attach joint), `fold` (every folded joint as written on the wrist, within 0.5 mm),
`cut` (no vertex of the proxy skin, skinned with the dumped joints and their zero scales, further past the wrist's
cut toward the elbow than the glove's own skin reaches in the bind pose, 13.0 mm for the cuff vertices that are 94 %
on the wrist, plus 5 mm), `open` (no open edge of the posed proxy longer than 1 mm that either eye, the model's eye
-+ 32 mm along the head's left axis, sees in front of it from the side its triangle faces away), `holes` (no seam
parts), `reach` (the off hand's wrist on its target within 10 mm with no beyond-reach exemption), `aim` (the off
hand's glove turned as its target within 2 degrees), `held` (the weapon glove where the game holds it on the gun,
its wrist in the attach joint's frame against the animated pose, within 10 mm; the wrist's distance from the
controller's grip is reported only), `landed`, `flicker` and `jump` of the glove (5 degrees, 10 mm), the glove's
`wrap` over the roll sweeps, and mirror and sight as before. There is no baseline: every pose must pass. Positive
controls, from a real hands-only dump (`r4-hands`, `selftest_hands_only.py`): the forearm put back where the game's
arm has it fails `zero`, `fold` and `cut` in 72 of 72 poses; the zero mask cleared fails `zero`; the target moved
30 mm and turned 10 degrees fails `reach` and `aim`; the glove turned 8 degrees on every other line fails
`flicker`; the eye test on a made-up opening counts it from inside only. The normal mode gives the same report and
CSV as before on `ab-b-def`. The proxy is the base arms mesh (closed but for its shoulder cut), not the drawn model:
whether the drawn cuff is closed is judged from the captures.

| Run | Arms, model, solve | Result |
|---|---|---|
| `e-hands` | hands only, the game's model | HANDS-RIG PASS 72/72: fold 0.0 mm, zero mask 252 on both arms in every hold, cut 13.0 mm (the glove's own), no open edge seen, no seam parted; the off glove 0.0 mm and 0.21 degrees at most from its target (the reach and flex poses and the two-handed holds included), the weapon glove 0.0 mm from the game's hold; flicker 0.21 / 0.64 degrees, landed 0.09 / 0.17 mm, mirror 2.4 mm, sight 0.16 degrees |
| `e-hskin3` | hands only, skin set 3 (`ETERNALVR_ARM_MODEL=skin:3`, the log names `praetor_set3.md6`) | HANDS-RIG PASS 72/72, the same figures to 0.03 mm and 0.01 degrees (mirror 2.2 mm) |
| `e-hfault` | hands only, `ETERNALVR_TEST_ARM_FAULT=forearm` (positive control) | HANDS-RIG FAIL 72/72: zero, fold (274 mm) and cut (up to 285 mm past the glove) on both arms in every hold |
| `e-def` | shown, the default solve | 0 regressions against the baseline; known-bistable warns on roll-R+150 only |
| `e-bind` | shown, `ETERNALVR_ARM_SOLVE=bind` (the off hand's fingers posed, open) | 0 regressions; one known-bistable warn (reach-L-across035's left wrist flips, 13 at its limit) |
| `e-skin3` | shown, `bind`, skin set 3 | 0 regressions; the same warn |

Captures (both eyes, ten poses each) in `tmp-vr\rs\hands\runs\<run>\captures` on the rig PC: in `e-hands` and
`e-hskin3` the forearms and upper arms are gone in every capture and the off glove sits on its controller, open,
with the short rounded cap at the cuff (roll -90 shows it from the side); in `e-hfault` the forearm shows behind
the glove.

**The bug the hand-pose sweep found.** The first hand-pose run (`d-hp`, the commit before the fix) posed nothing:
the off-hand hook took finger modifiers on the left arm to mean the weapon in the left hand, so with the free off
hand's fingers it ran the left-handed arms ("no grip of the game's for the held item yet") and both arms stayed the
game's in all 297 dump lines. `offhand_mods::leftGlove()` now says it; every run above is after the fix.

**Hand poses** (`tmp-vr\rs\armc\handpose-sweep.ps1`, `hp_check.py`; VR_HANDS_HUD.md, "Hand poses"). The hands
sweep's run with the off hand at four places and turns (neutral, rolled 90, raised and pitched 30, across and turned),
six sensor states at each (open; half: grip 0.5, the trigger touched; grip; fist: grip and trigger, the stick
touched; point: grip, the stick touched, the trigger let go; thumb up: grip and trigger, the stick let go), 1.5 s
holds, a rig controls file without the left trigger's and grip's presses (so a squeeze throws nothing), both eyes
kept for every pose.

| Run | | Result |
|---|---|---|
| `e-hp` | `bind`, the free off hand | HANDPOSE PASS 24/24: every curl the sensors' (0.000 off once settled); every finger bone as read within 3.5 degrees of its turn as written (the hands' modifier weight leaves 5 % of the game's own fingers, which grip the shotgun's pump at 65 degrees on the middle finger's second bone); open, no bone but the thumb's base turned more than 3.5 degrees as read (the game's own: 65); squeezed, that bone 83.2 degrees as read for 84.3 written |
| `e-hpfault` | `bind`, `ETERNALVR_TEST_ARM_FAULT=fingers` (positive control) | HANDPOSE FAIL 24/24: lands (52 to 74 degrees) in every pose and opens (74 degrees) in the four open ones |
| `e-hphands` | hands only | HANDPOSE PASS 24/24, every bone within 3.5 degrees as in `e-hp` |

In the captures at the neutral and the rolled places the open hand is flat with the thumb up, the grip and the fist
close round a handle (the game's grip of a gun) with no finger through the palm; at the raised place the glove is
seen from its back and at the across place it is behind the forearm, so the fingers do not show there.

**Not run**: a headset (all of it: the cuff and the glove alone from a free head, the fingers with real
controllers); the moments the game draws the whole arms in hands only (a weapon switch, a melee, a glory kill: by
the code, not measured); hands only and hand poses with the weapon in the left hand (the left-handed sweep); a world
scale other than 1; the drawn model's own closed geometry beyond the captures (the proxy is the base mesh).

### 20a. After the review fixes (2026-10-24)

The branch's Release with the launcher's hand-aim rule, hands only's limits that grow with a far controller
(`arm::reachLimits`), the fingers mixed by the arm's weight and eased back when the controller goes
(`arm::FingerHold`), and the finger state started again on map loads. Evidence: rig, OpenXR-Simulator.

| Run | | Result |
|---|---|---|
| `f-def` | shown, the default solve | 0 regressions against the baseline; the roll-R+150 known-bistable warns only |
| `f-hands` | hands only, the game's model | HANDS-RIG PASS 72/72, the figures of `e-hands` to 0.05 degrees and 0.1 mm (fold 0.0 mm, zero mask 252, cut 0.0 mm past the glove's own, off glove 0.0 mm and 0.21 degrees from its target) |
| `f-hp` | hand poses, `bind` | HANDPOSE PASS 24/24, every finger bone within 3.5 degrees as read (n-fist's capture missed; its three dump lines pass) |

The sweeps' own stop now puts the Steam Cloud files back (`-RestoreCloudFiles`): all three runs report the
five changed files restored. Not run on the rig: a controller lost mid-pose (the simulator's grip pose stays
valid), a controller more than 1.5 m from the game's hand, a map load during a pose; those are covered by the
unit tests only.

## 21. The free off hand through switches and the game's arm animations (rel-arms, 2026-10-24, OpenXR-Simulator)

The owner's headset test of the round-1 package (2026-10-24) saw the left hand flash onto the gun and back on a weapon
mod switch, on some guns; with the free off hand now the default (DECISIONS T-160) every player would see it. Which of
the game's animations take the free off hand, for how long, and what the fix does (DECISIONS T-165). Evidence: rig (the
simulator and the scripted input), Steam build 25216728; not a headset.

**The sweep** (`ms-run.sh`, `hc-sweep.ps1 -Plan` on the rig PC): e1m1 with `give all`, the free off hand and the bind
solve, `ETERNALVR_OFFHAND_TRACE=1` (every change of the left arm's signals and decision, with the weight, in the
layer's log), the arm dump every 0.25 s, both eyes captured every 8 frames and kept around each press. The off hand is
held still at its rest (0.2 m left, 0.3 m below, 0.4 m ahead of the head) throughout. Per gun with a mod (the slots
of the combat shotgun, Heavy Cannon, plasma rifle, rocket launcher, ballista and chaingun): the slot's touch (a weapon
switch), then two mod switches (the left Y tap) 3 s apart; then the super shotgun fired twice, a melee, the equipment
launcher and the Flame Belch. Two checks: `ms_trace.py` (each hand-back of the off hand in the trace: its length, the
reasons and hands flags in it, the event before it) and `ms_glove.py` (per weapon switch and mod switch found in the
dump, the dump lines from 0.6 s before to 2.6 s after: the off arm's weight, whether the layer posed it, and its hand
joint against its controller's wrist target; lines with the hands hidden, `hstate` 4, are counted apart).

**Before** (`ms-base`, `ms2-base`: the merge of the three branches, the layer of the round-1 package's rules):

| Game animation | Hands signals (trace) | The off hand handed to the game | Per event |
|---|---|---|---|
| Weapon mod switch, every gun with a mod (12 switches) | the generic hide (action 24, fp hands disabled, hidden state 4) for 0.31 to 0.34 s while the game swaps the weapon, then the flag `modChangeAnim` (bit 47) for about 1.3 s | 1.56 to 1.67 s, then 0.25 s held and 0.15 s blended back | the off glove up to 427 mm and 78 degrees from its controller; the captures show the game's left arm out over the gun for about 2 s |
| Weapon switch (8) | `BringDown` (action 10), then `changingWeapon` (bit 29) | 0.69 to 1.39 s | up to 418 mm and 78 degrees |
| Super shotgun fired (2) | the flag `reloading` (bit 1, its shells loaded by the left hand) | 0.81 to 0.83 s | |
| Melee (3) | the `Melee` action (7), `meleeAnim`, `meleeLunge`, the transitioning state (14) | 0.36 s | up to 474 mm and 90 degrees (the melee's fists item) |
| Equipment launcher, Flame Belch, the combat shotgun and rocket launcher fired | none | none | |

Every hand-over went out in 0.05 s (the blend's third): 4 to 5 frames at 90 Hz for a glove 0.4 m from the gun.

**After** (this branch's Release):

- `ms-fix1` (`decideOffHand`, `ArmBlend::updateOffHand`): mod switches handed the off hand to the game only for the
  swap's hidden phase (0.31 to 0.34 s, every tick with the hands disabled or the hidden state); at the first tick the
  hands are drawn again the arm is the controller's at weight 1.00 (`offhand: arm controller (controller), weight
  1.00` with `modChangeAnim` still set). Weapon switches gave no hand-back in the trace, but the off arm was not
  posed on the dump line at the item's change on six of eight: the layer's own read refused it (`no animated pose of
  the arm without the layer's overrides yet`, the off hand's rejected count up by 11 or 12 at each switch,
  136 over the run; a diagnostic build logged
  nine rejected ticks in a row, 0.14 s, at each switch, every one `readArm`). The game makes the hands' modifier lists
  again at every weapon switch, and the layer forgot its offsets of the arm without its overrides on any new list
  node, then waited 150 ms for a clean read, with the arm the game's meanwhile: a blink of the off hand at every
  switch, which the old hand-over had hidden.
- `ms-fix2` (the offsets kept for the same hands, `vkcore/game_arm.cpp`): every one of the 12 mod switches (six guns)
  and 9 weapon switches (one of them `give all`'s to the BFG) holds the off glove on its controller at weight 1 on every dump line the hands are drawn
  (0.0 mm, 0.0 degrees from its wrist target, the 18 lines with the hands hidden apart); the trace's hand-backs are the
  12 hidden phases (0.31 to 0.33 s), the super shotgun's reload (0.81, 0.83 s) and two melees (0.34, 0.36 s); two
  rejected ticks in the whole run (136 in `ms-fix1`). The captures every 8 frames through the
  shotgun's mod switch show the off arm unmoved where `ms-base`'s show the game's left arm out over the gun.
- `ms-lfix2` (left-handed, the weapon in the left hand, the right glove the off hand; four guns, each raised for the
  first time in the session): every one of the 8 mod switches handed the right glove to the game only for the
  swap's hidden phase and gave it back at weight 1.00 on the first drawn tick (`weapon arm: game (hands hidden)`,
  then `weapon arm: ik (controller), weight 1.00`, 8 of 8 in the trace). Each weapon switch here was the item's first
  raise, which keeps both arms the game's until the game's grip is read (`no grip read yet`, about 0.4 s, as before;
  KNOWN-ISSUES), so a left-handed switch of an item already read is not measured. The left-handed dump has no wrist
  target for the gloves, so the glove against its controller is not measured there either.

Not measured here: glory kills, the chainsaw and ledge grabs (they need a demon or a ledge in the scripted level);
they keep the rule they had, now eased over 0.15 s both ways. The weapon arm keeps the full rules and the quick
hand-over for switches.

### 21a. The merge QA sweep on the new defaults (2026-10-24)

The layer's defaults are now the free off hand and the bind solve (DECISIONS T-160), so the merge QA sweep of section
4, which sets `ETERNALVR_OFFHAND=free` and leaves the solve to the layer, runs the bind solve. Its run on the merge of
the three branches with the free off hand and the bind solve (`ho-shown`, 72 poses, `check_hands.py --mesh --baseline
runs/baseline.csv`): 0 regressions, the known-bistable warn on reach-L-across035's wrist flips (13, at its limit),
and checks fixed against the baseline on all 72 poses (most of them the off hand's `landed` and twist step). The
baseline is left as it is: nothing in it is a regression now, and a baseline of the bind solve wants the repeat
sweeps of section 4 to find its bistable pairs first. Roll wrist is on by default in the layer (T-162) and the
sweeps' poses jump in one frame, so a roll step would read as a roll of the wrist and switch the weapon mod: the
sweep scripts set `ETERNALVR_WRIST_FLICK=0`, as they set the other gestures off.

## 22. Hands only against the shown arms (rel-arms, 2026-10-24, OpenXR-Simulator)

In the owner's headset test of the round-1 package (2026-10-24) the hands-only off-hand glove looked turned about 90
degrees from the controller: with the arm out and the palm down the glove stood upright, and turned
counterclockwise it lay flat; with the arms shown it was right. Question for the rig: for the same controller poses,
is the hands-only left glove turned as the shown arm's? Evidence: rig (the simulator gives the grip and the aim one
pose, which a real controller does not), Steam build 25216728; not a headset.

**The sweep.** The 72-pose sweep of section 2 twice on the merge of the three branches (the free off hand, the bind
solve), `ho-shown` (`ETERNALVR_ARMS=shown`) and `ho-hands` (`hands`), then `ho_compare.py`: per pose, the hand joint
as written (the dump's `final`, the hold's last 0.75 s) of one run against the other's, and each against the off
hand's wrist target (its controller's grip with the wrist offset).

**Result.** Hands only puts the left glove exactly on its target in all 72 poses (0.00 degrees). The shown arm does
too wherever its swing limit and reach allow, and differs from it in 12 poses, all where the bind solve limits the
wrist's swing (70 by 35 degrees) or the arm stops short: flex-L-p-60 53.1 degrees, flex-L-y45 27.6, flex-L-p-30 26.5,
mirror-3 25.8, flex-L-y22.5 12.1, flex-L-p60 7.1, mirror-1 6.9, reach-L-030 6.0, roll-L-120 4.6, roll-L-90 and lead-L
3.9, roll-L-150 3.7 (more than 2 degrees); in the roll sweep at the neutral position the two are within 4.6 degrees
at every step, so a hand rolled on its own axis turns the glove alike in both. The right (weapon) glove is within 1.1
degrees between the two runs. The captures of roll 0, -90, +90 and 180 show the hands-only glove turned as the shown
one. So on the rig hands only does not turn the glove 90 degrees from the shown arm's; where they differ, hands only
follows the target and the shown arm's swing limit does not.

**Not reproduced here, and why.** The 90 degrees the owner saw comes from a pose the rig cannot give: here the grip
and the aim are one pose, while on a Quest controller the grip pose is turned against the aim, and the glove's target is
the grip's. A grip-to-glove turn off by a large angle would show fully in hands only, where nothing limited the glove,
and only up to the swing limit with the arm drawn, which matches "with arms shown it is right" [inferred, not
measured].

**The fix: hands only keeps the shown arm's swing limit** (`arm::handsOnlyTargets` no longer turns it off; the wrist
still reaches a far target; DECISIONS T-160). The check: for the same controller poses the hands-only
off glove turned as the shown arm's within 2 degrees, the weapon glove unchanged, left-handed mode too. The sweep has
18 poses more for it (`ho-sweep.ps1`: the arm out 0.75 m with the palm down, turned up, rolled 180 and level, across
the chest palm down and up, raised palm down and up, for each hand). On the fix's build, shown against hands only:

| Runs | Off glove, shown vs hands only | Weapon glove, shown vs hands only |
|---|---|---|
| `hor-shown` / `hor-hands`, right-handed, 90 poses | 0.00 degrees in all 90 (in the flex poses both 53.1, 26.5, 27.6 ... degrees off the grip, the swing limit's) | within 0.46 degrees |
| `hor-lshown` / `hor-lhands`, left-handed (the right glove the off hand), 90 poses | 0.00 degrees in all 90 | within 0.94 degrees |

The weapon glove of hands only against the build before the fix (`ho-hands`, 72 poses): within 0.74 degrees; the shown
arm's off glove against it (`ho-shown`): 0.00. Hands only's fold (the zero mask 252 on both arms) as before. The
hands-only checker's `aim` check (section 20: the off glove turned as its target within 2 degrees) now fails by design
where the swing is limited (the glove follows the shown arm's limit, not the grip), so its reference is the shown
arm's glove from here on; the rest of its checks are unchanged.
