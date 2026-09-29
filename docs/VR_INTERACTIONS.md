# VR interactions beyond punching

Design for the README's planned feature "More VR interactions with the world, beyond punching": physical
motions that press one of the game's existing actions, with no new game content. Every interaction here
goes through the one path the controllers already use (`InputMapper`, `ActionHold`, the user-command
hook), which the multiplayer guard already gates; none writes game state of its own.

Status: the throw and the overhead swing (sections 2.1 and 2.2) are built, off by default, and covered by
unit tests; neither has been tried in a headset. The rest is design.

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
| 1 | Throw a grenade with the off hand | `equipment` | none | low (a wind-up pose gates it) | no camera motion | off | built |
| 2 | Overhead swing of the weapon hand | `crucible` | none | low (hand above the head) | as the button (Hammer: VH) | off | built |
| 3 | Reach and press to use | a new `use` (`_use` 0x8 alone) | one rig check, then the focus entity | none once `use` is harmless | none | off | design |
| 4 | Holsters: reach to a shoulder or hip | `weapon_slot_N`, `quick_switch` | none | medium (grip near the body) | none | off | design |
| 5 | Chainsaw by a draw from the hip | `chainsaw` | none | medium; costs fuel | kill animation | off | design |
| 6 | Blood Punch, Glory Kill by a punch | `melee` | none | as the punch | as today | on | works today |
| 7 | Weapon mod switch by a wrist flick | `switch_weapon_mod` | none | medium (fast aiming) | none | off | design |
| 8 | Dash by a lean or a two-hand shove | `dash` | none | high (room-scale leaning) | high | off | not recommended |
| 9 | Physical grabs: ledges, monkey bars, pickups, doors | none | large | n/a | n/a | n/a | not applicable |

### 2.1 Throw a grenade (built)

**Motion.** An overhand throw with the off hand: bring it up beside the ear (the wind-up), then swing it
forward. It presses `equipment` once, on the first frame of the swing that is fast enough.

**Trigger** (`features/input/arm_gestures.hpp`). Distances are from the eyes, along the head's heading
(its forward projected on the floor; looking straight down or up, its up direction stands in), so they work
in any facing, seated or standing.

- Wind-up: the off hand at most 0.15 m below the eyes and no more than 0.10 m ahead of them. This is where
  an overhand throw starts and where no punch, support grip, guard or rest pose puts the hand.
- The wind-up primes the throw for 0.8 s after the hand leaves it. Within that time, a forward speed along
  the heading of 2.0 m/s or more fires (`ETERNALVR_THROW_SPEED`, 1 to 5).
- One throw per wind-up; nothing within 0.5 s of a throw. A hand whose tracking is lost (or a head that is
  not tracked) has to wind up again.
- While primed, the off hand's punch is held back, and after the throw it has to slow below half the punch
  threshold before it can punch: the throw's own swing, which is faster than a punch, never also punches.
  The weapon hand punches as usual.

**Why not the equipment button's release.** The research's first idea (R06 section 3.4) fires on the
release of the off-hand trigger during a throwing motion. The default map already has the equipment
launcher on that trigger (on the press), so the release would have to be taken away from it, and the
left grip is the Flame Belch. The wind-up pose needs no button and leaves the map as it is.

**Direction.** The game aims the launcher along its view, so the grenade goes where the gun points (hand
aim) or where the player looks (head aim), not along the throw. A throw along the hand's velocity needs the
view held on the throw direction from the press until the grenade spawns (R13 section 6.4, and its RE task
9 in section 11: the ticks from the `_quickuse` edge to the spawn), written the way hand aim already writes
the view. That is the next step if players expect it; the grenade's arc stays the game's own.

**Risks.** Adjusting the headset strap puts a hand beside the head (wound up), but the hand comes back down
slowly, so nothing fires; a fast punch with that hand within 0.8 s is held back. A missed wind-up gives a
punch instead of a throw (the old behaviour). A false throw spends a grenade (its cooldown refills it).
Seated at a desk: the swing passes above the desk; the wind-up is by the ear either way.

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

### 2.3 Reach and press to use (design)

The wish: switches, doors, the Slayer key doors, secrets and pickups used by reaching out and touching
them. Four ways to define "touching" without new content:

1. **Press `melee` on a gentle reach** (the hand pushed forward past most of the arm's length, slower than
   a punch). Works today for anything the game uses on `melee`, but with nothing in focus it swings a melee
   at the air on every reach: too many false triggers. Not recommended.
2. **Press `_use` alone.** The game's key is `_attack2 _use` (0x2 | 0x8). If `_use` without `_attack2` uses
   what is in focus and does nothing otherwise, a reach can press it freely: a false trigger costs
   nothing. Needs one rig check: at an interactable (the debug-command `teleport <entity>` gets there), a
   command with only 0x8 set (a new `use` action, `game/eternal/usercmd_buttons.hpp`) against one with 0x2
   | 0x8, and 0x8 alone with nothing in focus. If it works, the gesture is a reach (hand at least 0.45 m
   ahead of the eyes along the heading, moving forward at 0.6 m/s or more, the trigger up) and can be on
   by default.
3. **Gate on the game's focus.** `idPlayer.focusTracker.focusEntity` (R13 section 6.1) is what the game
   uses to choose use targets; reading it (and whether it is usable, with its position) would allow a use
   only when the hand is within reach of it, and a use prompt near the hand. This is new reverse
   engineering: the field offset in this build and the usable test. Worth it if option 2 turns out to
   fire the melee anyway.
4. **Gate on world geometry.** `head_sweep.cpp` already sweeps a 0.16 m sphere through the collision world
   from the camera hook. A sweep from the eyes to the hand would say whether the hand is touching a
   surface. Switches are entities; whether contents 0x100009 includes them is unknown, and a wall is not
   an interactable, so this only helps together with 2 or 3.

Recommended order: the rig check of option 2, then option 3 if needed. Until then a punch at the switch
does the job, since the punch is the use key.

### 2.4 Holsters (design)

Reach the weapon hand to a zone on the body and squeeze: a weapon slot (the game's `_weapN` keys, fixed
per weapon) or the quick switch. Zones from the head (no body tracking): over the right shoulder, the left
shoulder, the right hip; at shoulder height when seated (T-074). The grip is the weapon mod on the weapon
hand, so the zone must be one the hand never occupies while aiming, and the grip press inside the zone has
to be held back from the weapon mod. Medium value (the wheel and the quick switch exist), medium risk.
Needs a per-zone slot setting in the launcher.

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

### 2.7 Weapon mod switch by a wrist flick (design)

A fast roll of the weapon hand (the controller turned about its pointing axis, for example 400 degrees per
second and back) presses `switch_weapon_mod`. `HandState` has no angular velocity yet; it can be derived
from the aim orientation between frames. Fast aiming turns the hand about other axes, so the roll alone
must be measured. Low value: the Y tap is easy.

### 2.8 Dash by a lean or a shove (not recommended)

A quick head lean or both hands shoved to a side, pressing `dash` with the stick's direction. Room-scale
leaning and dodging are exactly these motions, so false dashes would be frequent, and a dash the player did
not expect is the worst comfort case (high risk in R06). Keep dash on the button.

### 2.9 Not applicable

- Ledge grabs, monkey bars and swing poles are automatic in the game; physical grabs would need the
  traversal state (large RE) for nothing the player cannot already do.
- Pickups are collected by walking over them; doors open by themselves or through a switch (2.3).
- DOOM Eternal has no flashlight, no reload and no hand-held items to pick up.
- Two-handed aiming (the off hand on the fore-grip) and pointing at the weapon wheel exist already
  (`docs/VR_CONTROLLERS.md`).

## 3. Settings

| Variable | Launcher (Play tab, Gestures) | Values | Default |
|---|---|---|---|
| `ETERNALVR_THROW` | Throw grenades | `1` / `0` | `0` |
| `ETERNALVR_THROW_SPEED` | none | forward speed, 1 to 5 m/s | 2 |
| `ETERNALVR_SWING` | Overhead swing | `1` / `0` | `0` |
| `ETERNALVR_SWING_SPEED` | none | downward speed, 1 to 5 m/s | 2.5 |

Both follow the weapon hand setting (the off hand throws, the weapon hand swings). Both need motion
controllers. The `controllers: on:` line ends with `throw gesture on|off, overhead swing on|off`; a gesture
logs `controllers: gesture: throw` or `gesture: overhead swing`, followed by the action it sent
(`controllers: action equipment` or `action crucible`). The hands-jump gesture (`hands_jump.hpp`) is
built but not yet exposed; the same pattern would expose it.

## 4. Tests

- Unit (`tests/features/input/arm_gestures_tests.cpp`): off by default; the wind-up and the throw; a punch
  from the chest, a guard or a support hand is not a wind-up; the prime time, one throw per wind-up and the
  cooldown; the speed thresholds; only the off hand throws, and the hands swap with the weapon hand; the
  heading in any facing and looking straight down; tracking loss; the swing, its height and speed; both
  hands up never swings; tuning fallbacks; through the mapper, the throw presses `equipment` and not
  `melee`, the same motion with the throw off is a punch, and the swing presses `crucible` and not `melee`.
  `punch_detector_tests.cpp`: a held-back hand does not punch and has to slow down first.
- Scripted input (`ETERNALVR_TEST_INPUT`) now takes `left.velocity = x, y, z` (metres per second, LOCAL
  axes) for a hand given an aim, so a punch or a gesture can be driven on the rig
  (`docs/VR_CONTROLLERS.md`, live check 14).

## 5. What only a headset can tell

- Whether players wind up by the ear without being told, and whether 2.0 m/s forward (throw) and 2.5 m/s
  down (swing) catch a relaxed throw or chop without catching fast aiming.
- Whether a grenade flying where the gun points, not where the hand threw, feels wrong enough to do the
  throw direction (2.1).
- False throws or swings in a real fight: the strap adjusted mid-fight, the support hand near the face,
  aiming high, a seated player at a desk.
- Whether the gestures want a vibration pulse of their own (today only the game's rumble plays).
