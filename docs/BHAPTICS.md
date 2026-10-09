# bHaptics suits and sleeves

Experimental and off by default. On a real suit a player has felt the shots, the landing from a fall and
glory kills (public issue #1); the other effects have not been felt yet. `ETERNALVR_BHAPTICS=1` (the
launcher's **bHaptics (experimental)** on the Play tab) makes the layer play effects on bHaptics vests and arm
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
| Glory kill | a sync kill starting (`idPlayer::savedSyncEntity` set, a `syncmelee/<demon>` sync entity), unless its sync entity's entityDef name starts with `interact/` (a pickup's animation: runes, Praetor tokens, mod bots, the automap, batteries) | the whole front and both sleeves at 80 for 250 ms |
| Sentinel Crystal | a sync whose entity is `interact/argent_cell/use_sync` (an upgrade picked at a crystal) | a wave from the centre of the vest out in all directions: from 1.9 s into the animation (about when the Slayer's hand takes the crystal; the animation runs about 3.3 s), timed from the sync's start, which comes as the upgrade menu closes, before gameplay is back, for 2 s. A ring grows from the two middle motors of each side (front and back alike) by distance from the centre of the 4 by 5 grid, a step every 80 ms, each for 100 ms: a motor rises as the ring comes near it and fades behind it, the middle of the chest and back first, the corners about 1.2 s in, then both sleeves (all 6 motors) as it passes the shoulders, about 1.5 s in. Up to 85 on the vest and 70 on the sleeves, each motor flickering at random down to 75 % of that. Back in play after the 2 s have passed, nothing plays (a tester's idea, public issue #1) |
| Praetor token | a sync whose entity is `interact/preator_suit_token/preator_suit_token_sync` (the game's spelling: a Praetor Suit token picked up; no menu opens, and the animation runs about 3.1 s from Use, traced on the rig) | the Sentinel Crystal's wave, from 0.1 s into the animation, as the Slayer's hands close on the coin (they hold it up until about 2.6 s), for 2 s (a tester's idea, public issue #1) |
| Rune | a sync whose entity's name starts with `interact/rune/` (`interact/rune/use_sync`: a rune picked; its menu comes first, and the sync starts as the menu closes and runs about 7.6 s, from a player's log) | the Sentinel Crystal's wave, from 1 s into the animation, twice back to back (4 s): the Slayer is shocked as he takes the rune for about 4 to 5 s (a tester's ask, public issue #25; twice as long since 0.1.37, the tester's ask on 0.1.36). The moment of the shock is not timed in a headset yet; the 1 s follows the tester's "right after you make your perk selection" |
| Death | `isDead` of the health component | both sides of the vest at 100 for 700 ms |
| Flame Belch | a shot from the fire hook while the controllers hold the Flame Belch's button (it fires through the same hook with the held weapon's decl; that shot is not a weapon shot) | the Slayer's left shoulder, where the Belch sits: the top two rows of the wearer's left two columns, front and back, at 70 for 300 ms; at most one every 250 ms |
| Equipment | the controllers' equipment launcher button going down (the launcher does not go through the fire hook) | the same left shoulder at 45 for 150 ms. A press with no charge left plays it too |
| Landing | the feet coming to rest after falling (features/bhaptics/landing.hpp, on every game frame from the player's physics origin), from 3.5 units or more above: on the rig a jump drops about 1.4 units and a double jump 3.2 to 3.3, so both are left out; teleports (faster than 60 units a second) and rises onto a ledge or a lift are not landings, and a dash in the air is not taken for one | the bottom row, front and back, from 35 at 3.5 units up to 70 for a long fall, for 120 ms (a tester's idea, public issue #1) |
| Portal | a trigger teleport of the player (`idTrigger_Teleporter::TriggerStuff_Impl`: pads, in-map portals, Slayer Gates, the Fortress's portals) or a level exit (`idTarget_LevelTransition`'s activate, first call), both hooked in `bhaptics_portal.cpp`; not the same classes' volumes that put the player back after a fall (a damage decl, or the falling stinger as the fade sound, except the Fortress's secret teleporter) | a crackle sweeping down the vest over 0.6 s, a step every 60 ms: the row under way at 55 to 85, about half the row above at 20 to 40, a fifth of the rest at 15 to 30, and half of each sleeve at 25 to 55, all at random, for 80 ms each (a tester's idea, public issue #1) |
| Health | health going up (`idPlayer::playerHealth`; features/bhaptics/pickups.hpp, on every game frame): a health pickup, a glory kill's drops. Rises less than 0.1 s apart are one gain (at most 0.4 s long); not felt: the first 0.5 s of readings (the start, after a load, a menu or a respawn), rises while dead or from 0 or below (a respawn), gains under 1 point. A rise from just above 0 is felt: a player's log has health going from 1.4 to 125 in one step, felt as a Mega Health | a wave up the vest, front and back together: the bottom row first, a row every 50 ms, each for 70 ms, the row just left at 40 %; 20 plus 2.4 per point, up to 80 at about 25 points; about 0.27 s in all. Left out while a hit, a glory kill, death, a landing, the crystal or a portal plays on the vest, and during a Sentinel Crystal's upgrade. No sleeves (a tester's idea, public issue #1) |
| Mega Health | a health gain of 100 or more (the maximum grows with the Sentinel Crystals' upgrades, so health above 100 alone is no sign of one) | the health wave at 100, a row every 75 ms, each for 95 ms: about 0.4 s |
| Armor | armor going up, as health (armor pickups, the Flame Belch's shards), also from 0: armor runs out in most fights (0.1.33 and earlier left every armor pickup from 0 out, public issue #25). Armor from 0 is left out only with health coming back from 0 (a respawn, an extra life) | the health wave reversed: down the vest from the top row, front and back, with the same timing and strength |
| Large armor | an armor gain with a single step (one reading to the next) of more than 26: the large armor (`pickup/armor/large`) and the Mega Armor, taken to give 50 or more in one step (not in a player's log yet: those show armor pickups of 25 and 5, one step each, and the Flame Belch's shards 2 at a time, about 25 at most in one gain). By the step, not the gain's total: a medium armor merged with a burst of shards is no large armor, and a large one capped by the maximum still is while it gives more than 26 | the armor wave at 100, a row every 75 ms, each for 95 ms, as a Mega Health (a tester's ask, public issue #25) |
| Launch | a jump pad or a booster launching the player, from two hooks in `bhaptics_launch.cpp`: idPlayer's handler of the `touchedBouncePad` event, which only `idTrigger_BouncePad` posts, right after it gave the player the launch velocity, and `idTrigger_SonicBoom`'s trigger (the boosters that blast the player to a destination; not its landing pads). A pad's trigger may run on several frames while the player is in it: touches less than 0.5 s apart are one launch, and a chain of pads a flight apart is a launch each. Jumps, double jumps, dashes and landings do not go through either path | the bottom row, front and back, a step every 50 ms, each for 70 ms: 85; 65 with the row above at 40; 45 and 30; 30 and 20: about 0.22 s, the push from the legs (a player's idea, public issue #1). A pickup's wave waits for it |

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
  (`idHavokPhysics_Player::viewAngles`) gives the side. Its `pickUpBuffer` (8 `idSoundEvent` pointers, a
  pickup's sound, from +0x7E8), `pickUpBufferIndex` (+0x828, serialized) and `lastPickUpBufferIndex`
  (+0x82C, the local one) are only logged for now (below).
- `idPlayer::savedSyncEntity`'s object at +0x8428: the sync entity of the animation the player is in, for
  its whole length (traced in headset sessions: `syncmelee/<demon>` for a glory kill, about 1.6 s;
  `interact/argent_cell/use_sync` for a Sentinel Crystal, about 3.3 s;
  `interact/preator_suit_token/preator_suit_token_sync` for a Praetor Suit token, about 3.1 s, traced on
  the rig; `interact/...` for other pickups).
  `idPlayer::syncMaster` (+0x7DB0) never changed in those sessions and is only a fallback. The sync entity's
  `idEntity::entityDef` at +0xA8 and its name at +0x8 tell the kind.
- Portals: `idTrigger_Teleporter::TriggerStuff_Impl` (RVA 0xDB2320, found by signature; rcx the trigger,
  rdx the activator, which must be the player), the class from its vtable's RTTI name, and for
  `idTrigger_Teleporter_Fade` its `damageDecl` (+0xE20) and `fadeOut.fadeSound` (+0xDD0 + 0x18, name at
  +0x8); the entity name (idStr at +0x40, text at +0x8). `idTarget_LevelTransition`'s activate (RVA
  0xD720D0), counted only while its `activated` (+0xC28) is still clear.
- Launches (docs/rig-findings/bhaptics-launch.md): idPlayer's `touchedBouncePad` handler (RVA 0x13E5650,
  found by signature, hooked at +0x36 just after its cast to `idTrigger_BouncePad`: rbx the player, rsi the
  pad), with the pad's `launchSpeed` (+0xCB4), `useFlightTime` (+0xCB8) and `flightTime` (+0xCBC) and
  `idPlayer::bouncePadIsInTransit` (+0x2F4F3) for the log; `idTrigger_SonicBoom::TriggerStuff_Impl` (RVA
  0xDAF3B0, hooked at +0x54, once the activator is a player and its sonic blast mechanic took the request:
  rsi the trigger, rdi the player), where `isLandingPad` (+0xD28) set is not a launch and `speed` (+0xCDC)
  goes to the log.
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
5. **Glory kill**: one jolt as each glory kill starts; the log's summary counts `glory kill`. Each sync
   logs `sync starts: '<entityDef>' (glory kill|pickup|Sentinel Crystal|Praetor token|rune)` and `sync
   ends after N s`.
6. **Portals**: each trigger teleport of the player logs `teleport '<entity>' (fade|plain, fade sound
   '...', damage 0|1): portal|hazard|out of bounds`, and each level exit `level exit '<entity>': portal`.
   A pad or portal must read `portal`; falling into a pit `hazard` or `out of bounds`.
7. **Weapons**: `firing '<decl>': <class> kick` for each weapon; names that land on medium but should not
   go in `weaponClassOf`.
8. **Pickups**: the first 60 gains felt log `pickup: health +25 (mega no), 50.0 to 75.0 in 1 step over
   0.00 s` (armor with `(large yes|no, biggest step +N)`), and the first 20 rises not felt `rise not felt (<why>): health
   +100, 0.0 to 100.0 in ...`. The steps and the time tell how the game moves a value for one pickup; a
   respawn or a level load must read as not felt, or not show at all. The first 60 changes of the damage
   feedback's pickup sound indices log `pickup sounds: buffer index A to B, last index C to D; slots 0 to
   7: '<sound>' ...` (a load starts over): next to the `pickup:` lines they tell which pickups write it, and by which sound (the large
   armor's and the Mega Armor's amounts, ammo, power-ups, weapons, extra lives).
9. **Launches**: each launch logs `launch: jump pad '<entity>' (launch speed S, during a pad's flight 0|1)`
   (`flight time T s` for a pad timed by its flight) or `launch: booster '<entity>' (speed S)`; the first 20
   touches within 0.5 s of the last log `same launch: ... again T s after the last touch`. A pad must give
   one `launch:` line per throw, jumps, double jumps and dashes none (rig recipe in
   docs/rig-findings/bhaptics-launch.md).
10. **Summary**: every 30 s with something new, `N frames (shot a, damage b, heartbeat c, glory kill d,
    death e, ..., launch l), m messages sent, f failed`.

To see the messages without a suit, run any WebSocket server on 127.0.0.1:15881 that prints what it
receives (quit the bHaptics Player first, it holds the port).

## Follow-ups

- Dash and double jump (the mapper's actions, `usercmd_hook.cpp`).
- The chainsaw (the hands' `destHandsState` 10 and 11, read by the off-hand hook), the Blood Punch and
  melee (the punch detector, `haptics_policy.hpp`).
- Ammo pickups, power-ups and the other pickups health and armor do not show, from the damage feedback's
  `pickUpBuffer` once its log lines (above) show what writes it; armor breaking (armor reaching 0).
- The game's own low-health flag (`idDamageFeedbackComponent::isShowingLowHealthWarning`) in place of a
  fixed 30, since maximum health grows with upgrades.
- The hit's height (`impactPoint`) for the row, head hits on a TactVisor or Tactal (`Head`), gloves.
- The game's rumble mix (`rumble_hook.cpp`) as a weak background on the vest (explosions, screen shake).
- A path frame (`pathPoints`) or an inline registered pattern for smoother shapes than single frames.
