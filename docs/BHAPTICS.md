# bHaptics suits and sleeves

Experimental, off by default, and not yet tried on a real suit. `ETERNALVR_BHAPTICS=1` (the launcher's
**bHaptics (experimental)** on the Play tab) makes the layer play effects on bHaptics vests and arm
sleeves through the bHaptics Player running on the same PC. Code: `src/features/bhaptics/` (the effects
and the messages, pure and tested), `src/vkcore/bhaptics_game.cpp` (what is read from the game) and
`src/vkcore/bhaptics_link.cpp` (the connection).

## Route

The bHaptics Player (the PC app, v3.x in 2026) serves a local WebSocket on `ws://127.0.0.1:15881`. Its
first protocol, `/v2/feedbacks`, takes JSON messages with a `Submit` list; a `"frame"` entry plays raw
motor intensities on one device for a given time:

```json
{"Submit":[{"Type":"frame","Key":"evr_shot_ForearmR","Frame":{"position":"ForearmR",
  "dotPoints":[{"index":0,"intensity":65}],"pathPoints":[],"durationMillis":90}}]}
```

The layer uses only that: it builds every frame itself, so it ships no bHaptics binary and no pattern
(`.tact`) files, registers nothing with bHaptics and needs no app id or API key (`app_id` and `app_name`
in the query are free-form labels the Player shows). The newer SDK2 (`bhaptics_library.dll`, events
designed and deployed on the bHaptics developer portal) needs a workspace id and an API key and comes
under the bHaptics SDK agreement, which does not sit well with an open-source repository; it is not used.
Player v3.5.8 (September 2026) still serves `/v2/feedbacks`, although bHaptics now points developers at
SDK2. If a later Player drops it, the transport is the only part to change.

The connection is WinHTTP's own WebSocket client (Windows 8 and later), loaded from System32 only when
bHaptics is on, on a thread of its own. The game threads only note what they read (a few guarded memory
reads per frame and a counter per shot); nothing on the render, input or game threads waits on the
network. With no Player the connection is refused (Windows takes about 2 s to say so for 127.0.0.1) and
is tried again every 3 s; effects made meanwhile are dropped, never played late. The Player's own
status messages are read and dropped on a second thread (the first one is logged).

## Effects

| Effect | From | Plays |
|---|---|---|
| Shot | the fire hook (`idHands::FireWeapon`, `aim_hooks.cpp`), every shot of the local player, whatever the aim | the weapon arm's sleeve (all 6 motors) and the top two rows of the chest on the weapon's side; Light (Plasma Rifle, Chaingun, Heavy Cannon, Unmaykr) 35 for 60 ms, Medium (Combat Shotgun, anything unknown) 65 for 90 ms, Heavy (Super Shotgun, Rocket Launcher, Ballista) 100 for 130 ms, Huge (BFG) 100 for 350 ms, the chest at 40 to 90 % of that; at most one every 70 ms |
| Damage | health plus armor dropping (`idPlayer::playerHealth`) | with the direction of a new hit (the damage feedback buffer) the two vest columns nearest it, middle rows, front or back; without one the middle of both sides at 80 %. 35 plus 1.3 per point lost, up to 100, for 150 ms |
| Heartbeat | health above 0 and below 30 | lub (80) and dub (50) on the chest's inner left column, a beat every 1.1 s at 30 health down to 0.6 s near 0 |
| Glory kill | a sync kill starting (`idPlayer::syncMaster` set, as `offhand_hook.cpp` reads it) | the whole front and both sleeves at 80 for 250 ms |
| Death | `isDead` of the health component | both sides of the vest at 100 for 700 ms |
| Flame Belch | a shot from the fire hook while the controllers hold the Flame Belch's button (it fires through the same hook with the held weapon's decl; that shot is not a weapon shot) | the Slayer's left shoulder, where the Belch sits: the top two rows of the wearer's left two columns, front and back, at 70 for 300 ms; at most one every 250 ms |
| Equipment | the controllers' equipment launcher button going down (the launcher does not go through the fire hook) | the same left shoulder at 45 for 150 ms. A press with no charge left plays it too |

Nothing plays while a menu holds gameplay back or while the camera hook is not running (loads); coming
back does not replay what changed meanwhile. `ETERNALVR_BHAPTICS_INTENSITY` (0 to 1, default 1;
`bhaptics_intensity` in `launcher.ini`) scales every intensity, on top of the Player's own settings.

Devices: `VestFront` and `VestBack` (20 motors each, 4 columns by 5 rows, index = row * 4 + column,
row 0 at the top) and `ForearmL`/`ForearmR` (6 each). The Player maps these onto the model in use
(X16, X40, Pro, Air, TactSleeve). Heads, hands, feet and gloves get nothing yet.

## What is read from the game

Type-info offsets of Steam build 25216728, used only when `PlayerAim` recognises that build (otherwise
the log says `bhaptics: off: unknown game build`):

- `idPlayer::playerHealth` (`idPlayerHealth`) at +0x37218: health `cur` at +0x10 + 0x34, armor `cur` at
  +0x10 + 0xB0 + 0x34, `isDead` at +0x1B0.
- `idPlayer::damageFeedbackComponent` at +0x26CD8: 10 items of 0x80 from +0x88 (`damage` +0x0,
  `selfDamage` +0x28, `impactDir` +0x38, `addedTimeStamp` +0x70) and `damageFeedbackBufferPos` +0x588.
  The newest item is the one with the latest time (or the one before the position when none has a time).
  The hit is taken to come from `-impactDir`; its yaw from the view's yaw
  (`idHavokPhysics_Player::viewAngles`) gives the side.
- `idPlayer::syncMaster`'s object at +0x7DB0 (inferred, as in `offhand_hook.cpp`).
- The held weapon's decl name (`idHands::rightItem`, as the viewmodel hook reads it) for the kick.

## Unverified, and how to check

Every item below is checked from the layer's log (`bhaptics:` lines), most without a suit:

1. **Connection**: `bhaptics: on, intensity ...`, then `connected to the bHaptics Player (...)` and
   `the Player says: {...}` (its `ConnectedPositions` lists the suit's parts), or `no bHaptics Player on
   port 15881 (...)` while it is not running.
2. **Health reads**: `first player reading: health 100.0, armor ...` with the HUD's values.
3. **Hit direction**: the first 20 hits log `new hit: item ..., impact direction (x y z), view yaw Y, from
   D deg (0 ahead, 90 left)`. Face an enemy and let it hit you: D should be near 0; from behind near 180.
   If it reads 180 for a hit from ahead, `impactDir` points the other way (flip the sign in
   `bhaptics_game.cpp`).
4. **Vest sides**: the column order on each side of the vest (`vestColumn` in `body_haptics.hpp`) is not
   documented by bHaptics. The first build took `VestFront`'s column 0 to be the wearer's right, and a
   tester's suit felt right-handed recoil on the left of the chest (public issue #1, v0.1.5), so both
   sides now count from the wearer's left. The back's order is still unconfirmed. With a suit: a hit from
   the left must be felt on the left (front and back); the heartbeat on the left of the chest; right-handed
   shots on the right shoulder. If the back comes out mirrored, give it the opposite rule.
5. **Glory kill**: one jolt as each glory kill starts; the log's summary counts `glory kill`.
6. **Weapons**: `firing '<decl>': <class> kick` for each weapon; names that land on medium but should not
   go in `weaponClassOf`.
7. **Summary**: every 30 s with something new, `N frames (shot a, damage b, heartbeat c, glory kill d,
   death e), m messages sent, f failed`.

To see the messages without a suit, run any WebSocket server on 127.0.0.1:15881 that prints what it
receives (quit the bHaptics Player first, it holds the port).

## Follow-ups

- Dash and double jump (the mapper's actions, `usercmd_hook.cpp`), landing (`idPlayer::wasOnGround`
  +0x8970, `highestFallPosition` +0x8958 for its strength).
- The chainsaw (the hands' `destHandsState` 10 and 11, read by the off-hand hook), the Blood Punch and
  melee (the punch detector, `haptics_policy.hpp`).
- Pickups (health, armor and ammo going up, or the damage feedback's `pickUpBuffer`), armor breaking
  (armor reaching 0).
- The game's own low-health flag (`idDamageFeedbackComponent::isShowingLowHealthWarning`) in place of a
  fixed 30, since maximum health grows with upgrades.
- The hit's height (`impactPoint`) for the row, head hits on a TactVisor or Tactal (`Head`), gloves.
- The game's rumble mix (`rumble_hook.cpp`) as a weak background on the vest (explosions, screen shake).
- A path frame (`pathPoints`) or an inline registered pattern for smoother shapes than single frames.
