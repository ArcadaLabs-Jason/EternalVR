# VR interactions beyond punching

Design for the README's planned feature "More VR interactions with the world, beyond punching": physical
motions that press one of the game's existing actions, with no new game content. Every interaction here
goes through the one path the controllers already use (`InputMapper`, `ActionHold`, the user-command
hook), which the multiplayer guard already gates; none writes game state of its own.

Status (after the owner's headset test of 2026-10-24, DECISIONS T-161 to T-163): Roll wrist (2.7, the wrist flick) is
on by default with its checkbox; the overhead swing (2.2) is built and off by default; the reach to use (2.3) is built
and off, a developer switch with no launcher row until round 2; rumble by hand is on (docs/VR_CONTROLLERS.md,
"Vibration"). The throw (2.1) and the holsters (2.4) were built, tried in the headset and removed: they do not fit how
DOOM Eternal plays. The rest is design.

## 1. What the game offers

- **Actions** (`src/game/eternal/game_action.hpp`): the same set a keyboard has. Relevant here: `melee`
  (the game's `_attack2 _use`, which is melee, Glory Kill, Blood Punch and use/interact in one key),
  `equipment` (`_quickuse`, the shoulder launcher: frag grenade or ice bomb), `crucible` (`_crucible`, the
  Sentinel Hammer in The Ancient Gods Part Two), `chainsaw` (`_quick3`), `dash`, `flame_belch`,
  `switch_weapon_mod` (`_reload`; the game has no reload), `weapon_slot_1` to `_8`.
- **The game picks the meaning of a press.** A melee press near a staggered demon is a Glory Kill, with the
  Blood Punch charged it is a Blood Punch, near a switch or a door panel it is a use. This is why the
  physical punch (`punch_detector.hpp`, 2.8 m/s toward the head's forward) already gives Glory Kills,
  Blood Punches and use: a player can punch a switch today.
- **Aim.** Everything the game aims follows its view angles: shots, the equipment launcher, the Glory Kill
  and use focus (`idPlayer.focusTracker.focusEntity`, `focus_defaultUsableDistance` 3.05 m; R13 section
  6.4). Under hand aim the view follows the weapon hand, so a grenade flies where the gun points.
- **What the layer knows each frame:** both hands' poses and linear velocities, the head pose, the posture
  (seated or standing), the weapon hand, and whether a menu is up. It does not know which entities are near,
  whether the Blood Punch is charged, how many Crucible charges are left, or whether a use prompt is shown:
  each of those is new reverse engineering.

## 2. Ranked interactions

Ranked by value to a player, times how robust a trigger can be from the hand, head and posture alone, times
how little new reverse engineering it needs.

| # | Interaction | Action | New RE | False-trigger risk | Comfort | Default | State |
|---|---|---|---|---|---|---|---|
| 1 | Throw a grenade with the off hand | `equipment` | none | low (a wind-up pose gates it) | no camera motion | n/a | removed (T-161) |
| 2 | Overhead swing of the weapon hand | `crucible` | none | low (hand above the head) | as the button (Hammer: VH) | off | built |
| 3 | Reach and press to use | a new `use` (`_use` 0x8 alone) | none (rig-checked) | none: `use` alone is harmless | none | off | built, developer switch |
| 4 | Holsters: reach to a shoulder or hip | `weapon_slot_N`, `quick_switch` | none | medium (grip near the body) | none | n/a | removed (T-161) |
| 5 | Chainsaw by a draw from the hip | `chainsaw` | none | medium; costs fuel | kill animation | off | design |
| 6 | Blood Punch, Glory Kill by a punch | `melee` | none | as the punch | as today | on | works today |
| 7 | Weapon mod switch by rolling the wrist (Roll wrist) | `switch_weapon_mod` | none | medium (fast aiming) | none | on | built |
| 8 | Dash by a lean or a two-hand shove | `dash` | none | high (room-scale leaning) | high | off | not recommended |
| 9 | Physical grabs: ledges, monkey bars, pickups, doors | none | large | n/a | n/a | n/a | not applicable |

### 2.1 Throw a grenade (removed)

Built (an overhand throw of the off hand, primed by a wind-up beside the ear, pressed `equipment`), tried in the owner's
headset test of 2026-10-24 and removed (DECISIONS T-161): DOOM Eternal's equipment fires from the shoulder launcher, so
an arm throw acts out something the Slayer does not do ("doesn't fit"). The code, its launcher row and key
(`throw_gesture`) and its variables (`ETERNALVR_THROW`, `ETERNALVR_THROW_SPEED`) are gone; the design is in the git
history.

### 2.2 Overhead swing: Crucible and Sentinel Hammer (built)

**Motion.** Raise the weapon hand above the head, then bring it down hard. It presses `crucible` once:
the Crucible's swing, or in The Ancient Gods Part Two the Sentinel Hammer's slam, which R06 already
imagined as an overhead-and-down motion.

**Trigger.** Raised: the weapon hand at least 0.10 m above the eyes. The pose primes the swing for 0.8 s;
a downward speed of 2.5 m/s or more fires (`ETERNALVR_SWING_SPEED`, 1 to 5); one per raise, 0.5 s
apart. Both hands above that height is a stretch, a celebration or the hands-jump gesture: it never primes
the swing, and the swing stays blocked until the weapon hand has come down once, whichever hand drops
first. While primed, the weapon hand's punch is held back (a chop also moves forward, often past the punch
threshold).

**Risks.** A false swing spends a Crucible charge (three at most) or a Hammer charge. Aiming at a flying
demon overhead keeps the gun at face height, pointing up, not above the head. Without the Crucible the press
does nothing. The Crucible's kill and the Hammer's slam are the game's own camera motion, as with the
button.

### 2.3 Reach and press to use (built, option 2; a developer switch)

Off by default, with no launcher row: `ETERNALVR_REACH_USE=1` turns it on. It got one real use in the owner's headset
test (a movable block), not enough to judge it, so it waits for the round-2 package (DECISIONS T-163).


The wish: switches, doors, the Slayer key doors, secrets and pickups used by reaching out and touching
them. Four ways to define "touching" without new content were weighed:

1. **Press `melee` on a gentle reach.** Works today for anything the game uses on `melee`, but with nothing in
   focus it swings a melee at the air on every reach: too many false triggers. Not done.
2. **Press `_use` alone.** The game's key is `_attack2 | _use` (0x2 | 0x8). **Built**, after the rig check below.
3. **Gate on the game's focus** (`idPlayer.focusTracker.focusEntity`, R13 section 6.1): new reverse engineering,
   worth it only if option 2 had fired the melee anyway. It did not, so it is not needed.
4. **Gate on world geometry** (a sweep from the eyes to the hand, `head_sweep.cpp`): only helps with 2 or 3.

**The rig check** (2026-10-24, OpenXR-Simulator, mono, the Fortress hub, scripted hands, release build):

- The use button alone (a new `use` action, `game/eternal/usercmd_buttons.hpp`, 0x8) pressed with the view on
  the mission-select console opened its popup: the game's cursor came up on the frame of the press (`controllers:
  action use`, then `menu: the game shows its cursor`). The melee press (0x2 | 0x8) from the same spot opened
  it too, so the game takes a use from `_use` alone.
- The same press from the spot with the view turned to where nothing in range was (only the ARC console, out of
  reach, was in view; a melee press there opened nothing either) opened nothing, and a capture a quarter second
  after it matches a capture of the same hand motion with the setting off: the same arms and gun, no melee swing and
  nothing else. (A melee press shows the fist swing in the same kind of capture.) So `_use` with nothing in focus
  does nothing, and a false reach costs nothing unless something usable is in view.

**Motion.** Reach a hand out ahead of the face and pull it back, as when pressing a switch, tapping a door panel
or taking a pickup. Either hand.

**Why out and back.** A first version pressed on the way out: the hand 0.45 m ahead, moving forward. Raising the
weapon from the hip to aim is that same motion, so every raise pressed use (and under hand aim turned the view
to the head for the press). What tells the two apart is what the hand does next: an aiming hand stays out, and a
lowered arm drops from the shoulder before it comes back; a hand that pressed something comes straight back.

**Trigger** (`features/input/reach_use.hpp`). Distances are from the eyes along the head's heading (its forward
projected on the floor; looking straight down or up, its up direction stands in), so they work in any facing, seated
or standing.

- Out: from inside 0.30 m, the hand goes past 0.45 m ahead in one movement (a pause on the way, slower than
  0.15 m/s, starts it over), at 0.6 m/s or more along the heading at some point and never faster than 2.4 m/s
  (faster is a punch, whose melee press already includes use).
- Back: within 0.8 s of passing 0.45 m, the hand comes back 0.12 m from the furthest it went, never having
  dropped below the height it had there by more than it came back plus 0.03 m. The press goes out then, one
  mapper frame, held by `ActionHold` like a tap.
- The trigger pulled (0.2 or more) at any point, or the hand primed for the overhead swing, ends the
  reach. One press per reach: the hand has to come back inside 0.30 m before the next, nothing fires within 0.5 s
  of a press, and a hand whose tracking starts or resumes out there has to come back first. Nothing starts while
  the game holds the gestures back (section 3).

**The proof against aiming** (`tests/features/input/reach_use_tests.cpp`, `gesture_mapper_tests.cpp`, synthetic
motion at 90 Hz, minimum-jerk arm movements): the weapon raised to aim from four rests (standing with the arms
hanging, standing with the gun held low, seated with the hands on the lap, seated with the arms by the chair),
to 0.45, 0.55 and 0.65 m ahead, over 0.25, 0.4 and 0.6 s, alone or with the other hand coming up to steady it,
left- and right-handed: held 2 s and lowered, then raised and lowered again at once with the arm dropping from the
shoulder. 144 raises, no use; through the whole mapper, left- and right-handed, standing and seated, no use either.
The same tracker presses once for a reach from each of those rests (the positive control), standing and seated,
in three facings, to a low, a level and a high switch.

**Aim.** The use focus is taken from the view angles. Under hand aim a reach aims with the head, as a punch does
(`action_aim.hpp`): what the player looks at is what is used, whatever the gun points at. A button the player
bound to `use` in their own map (the controls editor lists it as "Use (without melee)") keeps the melee aim setting.

**Risks.** One shape of aiming is still a reach: the weapon raised and pulled straight back towards the chest
within 0.8 s, without a shot and without the arm dropping first (a unit test records it). Something usable in view
is then used (a pickup, a door, a console) without the melee, and under hand aim the view follows the head for
that press. Pulling back is part of the gesture, so the press comes about a tenth of a second after the hand turns
round, not when it reaches the switch. The numbers are guesses for the headset to tune.

### 2.4 Holsters (removed)

Built (the weapon hand's grip squeezed at a spot over a shoulder or at the hip drew a weapon), tried in the owner's
headset test of 2026-10-24 and removed (DECISIONS T-161): they "don't make sense in a game where guns aren't on your
body"; DOOM Eternal switches weapons with no draw, by the wheel and the buttons. The code, the launcher's rows and keys
(`holsters`, `holster_shoulder`, `holster_other`, `holster_hip`), the variables (`ETERNALVR_HOLSTERS`,
`ETERNALVR_HOLSTER_SHOULDER`, `_OTHER`, `_HIP`) and the holster pulse are gone; the design is in the git history.

### 2.5 Chainsaw by a draw (design)

The off hand to the left hip, then a push forward: `chainsaw`. Without a button it would fire on any
fast motion out of the hip zone (walking arm swings in room-scale), and with the grip it collides with
the Flame Belch. A false trigger spends fuel pips and starts a kill animation. Lower value than the stick
gesture it would replace; later, if players ask.

### 2.6 Blood Punch and Glory Kill (works today)

Both are the melee key, and the game chooses: a staggered demon in focus gives a Glory Kill, a charged
Blood Punch gives a Blood Punch. So any physical punch already does both, with either hand. A "strong
punch only" Blood Punch would need the charge state (new RE) and would hold back a punch the game would
have thrown; not recommended. The punch threshold is the setting to tune (`PunchSettings`, 1 to 4 m/s).

### 2.7 Weapon mod switch by rolling the wrist: Roll wrist (built, on by default)

The launcher calls it Roll wrist (the Play tab's Gestures, on by default since the owner's headset test of 2026-10-24:
"works, felt good"; DECISIONS T-162); the code and its variables keep the name flick (`ETERNALVR_WRIST_FLICK`,
`ETERNALVR_FLICK_RATE`). The checkbox stays because a gesture can fire by accident.


**Motion.** The weapon hand turned over and back fast about its pointing axis, the controller rolled like a
wrist, presses `switch_weapon_mod` (the Y tap) once. `features/input/wrist_flick.hpp`. Up to 0.1.87 one fast roll
one way was enough, and that fired when players did not mean it (below, "Measured in play"; DECISIONS T-202).

**Roll rate.** `HandState` has no angular velocity, so the mapper takes it from the aim orientation between two
frames: the rotation between them as a rotation vector over the time between the two poses' samples
(`InputFrame::sampleSeconds`, the XR worker's sync time), split into the part about the pointing axis (the
roll) and the rest (the pointing direction swinging round), both smoothed over about 25 ms so one noisy frame is
no flick. The mapper runs on the game's commands, not the headset's frames: the same sample read twice adds
nothing, and a gap of 0.1 s or more between samples (a load, an alt-tab, the game stalled) reads no rate and
starts over, so a wrist that turned during a stall is no flick. (A first version took the mapper's
frame time, which the mapper caps at 0.1 s: a 5 s stall then read as 0.1 s and a 90 degree turn across it as
900 degrees per second.)

**Trigger.** The roll rate also adds up to the angle the hand has turned about its pointing axis, and the points
where that angle turns round (10 degrees back from its furthest) split it into strokes. A flick is two strokes:

- the over: 90 degrees or more one way, its smoothed roll rate reaching 450 degrees per second
  (`ETERNALVR_FLICK_RATE`, 200 to 1500) at least twice the swing rate of the pointing direction, with the hand
  moving no faster than 1.5 m/s, in the same frame;
- the back: the other way, its rate reaching the same 450 and at least the swing's, and 60 degrees back, both
  within 0.3 s of the over's furthest point. The press fires there: in the deliberate flicks measured, 35 to
  55 ms after that point.

A stroke under 20 degrees is no stroke: a wobble as the hand turns, or a hesitation in the middle of a roll,
leaves the stroke before it going on, so it neither loses an over nor splits one under 90. The over's furthest
point moves on for the 0.3 s only when the hand goes 2 degrees further, so a hand turned over and held with a
tremor is still held. The frame a flick starts on counts towards the over.

Either way round counts. One press per flick: nothing fires within 0.6 s of a flick, nor until the roll has stayed
below a quarter of the minimum (about 110 degrees per second) for 0.2 s, so rolling on round after the back, or
a wrist shaken over and back several times, is still the one press. A roll one way only never fires, however fast
or far, and neither does a hand turned over and held, then turned back (the back comes too late), or turned
back slowly; a roll quiet for 0.5 s starts the strokes over. A hand whose tracking is lost starts over. A turn of
more than 4000 degrees per second in one frame is a tracking jump, not a wrist, and reads nothing. Nothing fires
while the game holds the gestures back (section 3), the weapon wheel included, whose pointer can be the weapon
hand. Log: `controllers: gesture: roll wrist (over O deg, back B deg, roll R deg/s, swing S deg/s, pointing P
deg, hand V m/s, head turn H deg/s)`.

**Fast aiming.** Aiming swings the pointing direction, which is the part a flick does not have, and carries the
hand. The unit tests turn the aim sideways at 900 degrees per second level and 45 degrees up and at 700 degrees
per second 60 degrees up, and roll over and back at 700 degrees per second while swinging the aim at 600: none
flicks, and the same rates about the pointing axis do (rolls of 150, 300 and 400 do not). Looking nearly straight
up, a fast turn of the body is mostly a roll of the controller, but it is one way: it makes an over at most.

**Measured in play** (2026-10-30, DECISIONS T-202). The frame table (`eternalvr-frames-<pid>.csv`,
docs/VR_HEAD_TRACKED.md) holds the weapon hand's aim orientation at every shown view; replaying it through the
detector reproduces the logged flicks (a player's 0.1.84 line, roll 460 deg/s and swing 195, replays as 457 and
199; the owner's three flicks of 2026-10-24 replay within about 30 ms of their log lines). Hand speed is not in the
table, so the replay leaves the 1.5 m/s test out and counts a little high.

| | Sessions | Hours | One-stroke flick (0.1.83 to 0.1.87) | Over and back (now) |
|---|---|---|---|---|
| Recorded play, Roll wrist off or not yet built (14 players, the owner) | 67 | 6.2 | 129 (21 an hour) | 0 |
| The owner's deliberate flicks in a headset (2026-10-24) | 1 | | 3 of 3 | 3 of 3 |
| A player's flick logged in 0.1.84 (`gesture: roll wrist`, roll 460, swing 195, head turn -30) | 1 | | fired | none |

The 129 rolls the one-stroke flick took: rate 450 to 1360 degrees per second (median 510); the stroke they
fired in 70 degrees at the median (135 at the 90th percentile); the roll back after it 26 degrees at the median
and under 60 for 9 in 10; the head turning 14 degrees per second at the median and 60 or more in 19, steep aim
(over 45 degrees up or down) in 4. So most were a forearm turning one way in play, not a body turn and not
aiming up. The logged player flick was the forearm turning about 130 degrees one way over 0.8 s while the player
turned, and back only 25. The deliberate flicks turned over 130 to 160 degrees at peaks of 800 to 2600 degrees
per second and came straight back 70 to 140. A rate alone does not separate them (the accidental rolls reached
1360); the back does. Another player's log has 40 flicks in 17 minutes, then Roll wrist off in every later
session. `tests/features/input/wrist_flick_recorded_tests.cpp` replays the three deliberate flicks
(`wrist_flick_recorded.hpp`) and models five of the players' rolls from their measured sizes, rates, swing and
pitch (no player's motion is kept in the repository). Still to see in a headset: that a deliberate flick fires every time for players
other than the owner (a smaller wrist turn than 90 degrees is no flick now), and that nothing fires by accident.

**Rig check** (2026-10-24, the first version, OpenXR-Simulator, mono, scripted aim orientation): a roll of 90
degrees over 3 s and an aim swept sideways at about 1600 degrees per second did not flick; a roll of 180 degrees
at about 1200 degrees per second logged `gesture: wrist flick` and `action switch_weapon_mod` once.

**Risks.** A false flick switches the weapon mod and a second one switches it back; nothing is spent. A player
whose deliberate flick turns less than 90 degrees over or less than 60 back gets nothing, with nothing in the log:
a report should say what they did.

### 2.8 Dash by a lean or a shove (not recommended)

A quick head lean or both hands shoved to a side, pressing `dash` with the stick's direction. Room-scale
leaning and dodging are exactly these motions, so false dashes would be frequent, and a dash the player did
not expect is the worst comfort case (high risk in R06). Keep dash on the button.

### 2.9 Not applicable

- Ledge grabs, monkey bars and swing poles are automatic in the game; physical grabs would need the
  traversal state (large RE) for nothing the player cannot already do.
- Pickups are collected by walking over them; doors open by themselves or through a switch (2.3).
- DOOM Eternal has no flashlight, no reload and no hand-held items to pick up.
- Pointing at the weapon wheel exists already (`docs/VR_CONTROLLERS.md`). Two-handed aiming (the off hand
  on the fore-grip) does not: the mapper can hold back the off hand's grip bindings while that hand steadies
  the weapon (`MapperContext::supportHandOnWeapon`), but nothing detects the support grip, so it is never
  set.

## 3. Settings

| Variable | Launcher | Values | Default |
|---|---|---|---|
| `ETERNALVR_SWING` | Play tab, Gestures: Overhead swing | `1` / `0` | `0` |
| `ETERNALVR_SWING_SPEED` | none | downward speed, 1 to 5 m/s | 2.5 |
| `ETERNALVR_WRIST_FLICK` | Play tab, Gestures: Roll wrist | `1` / `0` | `1` |
| `ETERNALVR_FLICK_RATE` | none: an environment variable only, for tuning (the launcher has no control for it) | roll rate, 200 to 1500 degrees per second | 450 |
| `ETERNALVR_HANDS_JUMP` | Play tab, Gestures: Jump with both hands | `1` / `0` (hands 0.15 m higher when seated; `src/features/input/hands_jump.hpp`) | `0` |
| `ETERNALVR_REACH_USE` | none: a developer switch for now (T-163) | `1` / `0` | `0` |
| `ETERNALVR_RUMBLE_ROUTE` | none: rumble by hand is always on for players (T-163) | `1` / `0` | `1` |

The gestures follow the weapon hand setting (the weapon hand swings and rolls) and need motion controllers. The
`controllers: on:` line ends with `overhead swing on|off, hands-up jump on|off, ...`; the line `controllers:
interactions:` after it says `roll wrist on|off, rumble by hand on|off, reach to use on|off`. A gesture logs
`controllers: gesture:` and its name (`overhead swing`, `reach`, `roll wrist`, `hands-up jump`, or `hands-up jump
(seated height)` when judged with the seated height), followed by the action it sent (`action crucible`, `action
use`, `action switch_weapon_mod`, `action jump`).

The reach and Roll wrist start nothing, and start over afterwards, while the game holds such input back (the
thumb-rest wheel's own list: the game suppresses buttons for a popup, forces the view, plays a cutscene, or a demon
is piloted), while a menu holds gameplay input back, and while the weapon wheel is up (by the stick, a button or the
thumb rest; with Pointing at the wheel by hand its pointer is the weapon hand).

## 4. Tests

- Unit (`tests/features/input/arm_gestures_tests.cpp`): off by default; the swing, its height and speed; both
  hands up never swings; tracking loss; tuning fallbacks; through the mapper, the swing presses `crucible` and not
  `melee`, and an overhand motion of the off hand (the removed throw's) is a punch and presses no `equipment`.
  `punch_detector_tests.cpp`: a held-back hand does not punch and has to slow down first.
- Reach to use (`tests/features/input/reach_use_tests.cpp`, arm movements from `hand_motion.hpp`): off by
  default; a reach out and back presses use once, on the way back, either hand, standing and seated, in three
  facings, to a low, a level and a high switch; the weapon raised to aim (144 raises: four rests, three
  extensions, three speeds, one or two hands, left- and right-handed), held and lowered, or raised and dropped at
  once, presses nothing, with a reach from each rest as the positive control; a hand held out and then pulled
  back, too short, creeping, a punch's speed, or out in two movements does not press; a pulled trigger and a hand
  primed for the swing end it; the re-arm, a second reach and the cooldown between two hands; tracking lost or
  started out ahead; an untracked head or velocity (and a NaN); held back by the game; looking straight down;
  unusable tuning values; the known limit (a weapon raised and pulled straight back at once reads as a reach).
  `gesture_mapper_tests.cpp`, through the whole mapper: use and not melee for a reach and melee and not use for a
  punch; nothing with the reach off; no use for the weapon raised to aim, left- and right-handed, standing and
  seated, with the other hand's reach as the control; a reach's use waits for the head's aim under hand aim
  (`pressAimsWithHead`, as the command hook passes it) and a bound use button does not; nothing while the game or
  a menu holds the gestures back or the weapon wheel is open on the stick. `action_aim_punch_tests.cpp`: a reach's
  use aims with the head and a bound use button follows the melee aim setting. `usercmd_buttons_tests.cpp`: `use`
  is 0x8 alone.
- Roll wrist (`tests/features/input/wrist_flick_tests.cpp`): on by default; a fast roll flicks once and the roll
  back does not flick again, in either direction; slow rolls do not (and the same roll faster does: the
  positive control); a fast swing of the aim, level and tilted up, is not a flick; a roll while the aim swings fast
  and a roll while the hand travels fast are not; the cooldown and the quiet wrist before the next: rolled on
  slowly and back fast, or held 0.35 s and back fast, still one flick; held still a second and back fast, a second
  flick; the same way again after half a second, a second flick; the rate from the samples' times with
  the mapper running twice per sample (400 degrees per second does not flick, 700 does, and the mapper's own times
  would have flicked at 400); tracking loss, a stalled frame and a tracking jump (150 degrees in one frame); only
  the weapon hand's roll counts; unusable tuning values; through the mapper, one press of the weapon mod switch,
  no flick across a 5 s stall with or without sample times (and a flick after it), and none while the game or a
  menu holds the gestures back or the weapon wheel is open.
- Scripted input (`ETERNALVR_TEST_INPUT`) now takes `left.velocity = x, y, z` (metres per second, LOCAL
  axes) for a hand given an aim, so a punch or a gesture can be driven on the rig
  (`docs/VR_CONTROLLERS.md`, live check 14).

## 5. What only a headset can tell

- Whether 2.5 m/s down (swing) catches a relaxed chop without catching fast aiming, and false swings in a real fight:
  the strap adjusted mid-fight, aiming high, a seated player at a desk.
- Whether the gestures want a vibration pulse of their own (today only the game's rumble plays).
- Whether 450 degrees per second catches a relaxed roll of the wrist without catching fast aiming, and whether a fast
  body turn while aiming nearly straight up switches the mod by mistake (2.7). The first headset test: it "felt good".
- Whether the reach's pull back feels natural on a switch, and how often a raised and pulled-back weapon uses
  something by mistake (2.3); round 2.
