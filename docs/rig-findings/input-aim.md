# Input, aim, viewmodel and listener: hook points for M5

Static analysis only; the game was not run. Input: `DOOMEternalx64vk.exe`, Steam build 25216728 (Rev 3.2),
PE timestamp 0x6A7B9B8C, SHA-256 `69dc13e88d1c19133ead7950dc64ebcbd4a5a3f6bd6f9c336ebffe56df6a1c11` (the same
exe as `engine-facts.md` and `mp-guard.md`). Tools: the analysis scripts of `engine-facts.md` section 8,
Ghidra 12.1 headless for decompilation, the type-info reader (`tools/typeinfo/`) for field offsets. All
addresses are RVAs in this build.

`[static-verified]`: read from the code or data end to end (decompiled or disassembled, or a type-info
record), and every signature below matches exactly once in `.text` (`??` masks rel32, RIP displacements and
structure displacements). `[inferred]`: a reading of names, strings and call structure that a live
experiment must confirm (section 8).

Every hook below touches the local player's game state, so each one calls `mp_guard::allowsGameTouch()`
before it acts and is installed only once the guard is armed (`mp-guard.md` section 1).

## 1. The user command: layout, build path and injection point

Two corrections to R13 (`docs/research/13-input-and-aim.md`): `idUserCmd` **is** in the type-info tables,
and XInput is imported **by ordinal**, with no `XInputGetCapabilities` import (section 5).

### 1.1 `idUserCmd` (type info, size 0x98) [static-verified]

| Offset | Type | Field | Notes |
|---|---|---|---|
| 0x00 | int64 | gameTime | overwritten by the caller after the build (1.3) |
| 0x08 | bool | fromBot | |
| 0x09 | bool | inhibited | from the command tracker's inhibit info |
| 0x10 | uint64 | buttons | `usercmdButton_t` bitmask (1.2) |
| 0x18 | int8 | forwardmove | -128..127; keys give ±127, the stick gives `-stickY * 127.5` (0x17FE810) |
| 0x19 | int8 | rightmove | same scale |
| 0x1A | int8 | upmove | |
| 0x1C | int16[3] | angles | pitch, yaw, roll; `short = (int)(deg * 182.04445)` (constant 0x2AA4C50), read back as `short * 0.0054931640625` (0x2AA4C48): 65536 units per turn |
| 0x22 | touch_t[2] | touch | |
| 0x30 | float[6] | joystickAxis | |
| 0x48 | float[20] | vrAxis | filled from the dormant VR device, VR keys 0x131 and up (R15) |

Related layouts [static-verified]: `idUCmdTracker` (0x24F8): `prevcmd` +0x0, `usercmd` +0x98,
`commandBuffer[60]` +0x130, `viewAngles` +0x24D4, `prevViewAngles` +0x24E0, `cmdAngles` +0x24EC.
`idHavokPhysics_Player` (at `idPlayer` +0x8A50): `command` +0x3DE0, `prevcmd` +0x3E78, `viewAngles` +0x3F10,
`deltaViewAngles` +0x3F1C. `idPlayer.inhibitFlags` is at +0x87FC.

### 1.2 Buttons: the reflected `usercmdButton_t` enum (record 0x3AE3FC0) [static-verified]

| Bit | Button (bind) | Bit | Button (bind) |
|---|---|---|---|
| 0x1 | ATTACK1, fire (`_attack1`) | 0x400000 | DASH (`_dash`) |
| 0x2 | ATTACK2, melee / use (`_attack2`) | 0x800000 | QUICK_USE, equipment (`_quickuse`) |
| 0x4 | ALTFIRE, weapon mod (`_altfire`) | 0x1000000..0x8000000 | QUICK_0..3 (`_quick0..3`); 0x8000000 is also WEAP_SIDEARM |
| 0x8 | USE (`_use`) | 0x10000000 / 0x20000000 | NEXT / PREV_QUICK_ITEM |
| 0x10 | ZOOM (`_zoom`) | 0x40000000 | INVENTORY (`_inventory`) |
| 0x20 | SPRINT (`_sprint`, `_speed`) | 0x80000000 | ACTIVATE_ABILITY |
| 0x40 | CHANGEWEAPON: quick switch, wheel (`_changeWeapon`) | 0x100000000 | MOVEUP, jump (`_jump`, `_moveUp`) |
| 0x80 | WEAP_RELOAD (`_reload`) | 0x200000000 | GROUNDSLAM (`_groundslam`) |
| 0x100 / 0x200 | WEAP_NEXT / PREV (`_weapnext`, `_weapprev`) | 0x400000000 | CRUCIBLE (`_crucible`) |
| 0x400 << n, n = 0..9 | WEAP_0..9 (`_weap0..9`) | 0x4000000000 | POWER (`_power`) |
| 0x100000 | BFG (`_bfg`) | 0x8000000000 | OBJECTIVES, mission info (`_objectives`) |
| 0x200000 | WALK (`_walk`) | 1<<48 .. 1<<56 | INPUT_MOVEDOWN/LEFT/RIGHT/FORWARD/BACK, LOOKUP/DOWN/LEFT/RIGHT |

- The bind table (`_name` to `cmdGenButton_t`, 54 entries) is at 0x38A7710.
- The inhibit enum (record 0x40B4AE0) matches R13: VIEW 0x8, VIEW_ONCE 0x100, BUTTONS 0x10, CHANGE_WEAPON
  0x20, DASH 0x40, JUMP 0x4000, CHAINSAW 0x10000, ALL 0x1F87F.
- Chainsaw and Flame Belch are `_quick1` / `_quick2` [inferred: the settings menu lists exactly those two
  under "Combat and Abilities"].

### 1.3 From input to the player [static-verified unless marked]

| # | What | RVA | Signature (unique in `.text`) |
|---|---|---|---|
| a | `idUsercmdGen*` global (`usercmdGen->Init` at 0x432263) | .data 0x47DDB50 | read at 0x43225C: `48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 50 08` |
| b | `idUsercmdGenLocal` vtable (RTTI): slot 18 (+0x90) build, slot 22 (+0xB0) "last look input was the stick" (+0xA44), slot 23 (+0xB8) +0xA46 | 0x2E52C30 | – |
| c | `idUsercmdGenLocal::BuildCurrentUsercmd(gen, idUserCmd* out, int localUser, ctrlInfo*, bool menuMode)` | 0x17FC650 | `48 8B C4 48 89 50 10 55 48 8D 68 B1` |
| d | The generator's angle-to-short conversion (per-user accumulated float angles to the command's shorts) | 0x17FD3C0 | `48 8B 8F D8 08 00 00 F3 0F 10 15 ?? ?? ?? ?? 0F 28 B4 24 90 00 00 00 4C 8B B4 24 A8 00 00 00 F3 0F 10 41 04 4C 8B AC 24 B0 00 00 00` |
| e | Per frame: build the commands of the four local users | 0x43E760 | `48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 F0 48 81 EC 10 01 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 00 4C 8B E9 4C 8B E2` |
| f | **The call of `idUserCmdMgr::PutUserCmd` in (e)** | 0x43E8DD (pattern starts 0x43E8B1; the call is at +0x2C) | `45 84 F6 4C 8D 44 24 60 8B D7 48 8B 08 48 8B 44 24 70 48 89 4C 24 60 B9 00 00 00 00 48 0F 45 C1 49 8D 8D 58 21 00 00 48 89 44 24 70 E8 ?? ?? ?? ??` |
| g | `idUserCmdMgr::PutUserCmd(mgr, playerIdx, const idUserCmd*)`: copies 0x98 bytes to `mgr + idx*0x98`; its only callers are 0x43E760 and 0x6E3C70 | 0x17FF7F0 | `41 0F 10 00 48 63 C2 48 69 D0 98 00 00 00` |
| h | `idUCmdTracker::IsPressed(tracker, mask)` (current = +0xA8 unless +0xA1, previous = +0x10 unless +0x9) | 0x146BE40 | `33 C0 44 8B C0 38 81 A1 00 00 00 75 07 4C 8B 81 A8 00 00 00 4C 85 C2 74 11` |
| i | `idPlayer::ProcessInput` (profiling label `idPlayer::ProcessInput_ServerAndClient_Local`) | 0x1442710 | `40 55 53 56 48 8D 6C 24 90 48 81 EC 70 01 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 48` |
| j | `idPlayer::UpdateViewAngles`: tracker angles plus delta, aim assist, recoil, the post-sync lerp | 0x1455D70 | `40 55 53 56 48 8B EC 48 83 EC 70 48 8B F1 48 8B 0D ?? ?? ?? ??` |
| k | `idPlayer::SetViewAngles(player, angles*, bool force)` (48 references); skips when `inhibitFlags & 0x108` | 0x1454480 | `48 89 5C 24 10 48 89 6C 24 18 57 41 56 41 57 48 83 EC 20 41 0F B6 E8 4C 8B FA 48 8B F9` |
| l | `idUCmdTracker::SetViewAngles(tracker, angles, outDelta)`: `delta = angles − SHORT2ANGLE(cmd.angles)`, `viewAngles = angles` | 0x146B870 | `0F BF 81 B4 00 00 00 F3 0F 10 02 F3 0F 10 15 ?? ?? ?? ?? 66 0F 6E C8 0F 5B C9 F3 0F 59 CA F3 0F 5C C1 F3 41 0F 11 00` |
| m | `idHavokPhysics_Player::SetPlayerInput(prevcmd, cmd, viewAngles)`, vtable 0x2A86E18 slot 161 (+0x508); writes +0x3E78 / +0x3DE0 / +0x3F10 | 0x535230 | `0F 10 02 0F 11 81 78 3E 00 00 0F 10 4A 10` |
| n | The call of (m) with `tracker+0` / `tracker+0x98`, in 0x1440C70 (called from ProcessInput) | 0x144168A (pattern starts 0x144166D) | `4C 8B 96 50 8A 00 00 4C 8D 83 98 00 00 00 4C 8D 4C 24 40 48 8B D3 48 8D 8E 50 8A 00 00 41 FF 92 08 05 00 00` |

The chain:

1. Once per frame, (e) calls (c) for each local user. (c) drains the key-event queue through the bind table,
   reads the mouse and the stick, ORs the pressed buttons into `gen+0x910`, writes the moves to
   `gen+0x918..0x91A`, converts the per-user accumulated float angles (`*(gen+0x8D8)` +4 / +8 / +0xC: pitch,
   yaw, roll in degrees; the mouse adds to them in 0x17FEFD0, the stick in 0x17FE810) into shorts (d), and
   copies the 0x98-byte command from `gen+0x900` to `out`.
2. (e) then overwrites `gameTime`, **zeroes `buttons` when r14b is set** (console open,
   `com_editorActive`, or 0x66C240), and stores the command with (g) into the `idUserCmdMgr` at
   `common+0x2158`, which reaches the game frame through `frameInput+0x38` (0x43E4B0).
3. Each player has an `idUCmdTracker` (0x13E7740 → `gameLocal+0x1266D0[clientNum]` → vfunc +0x40).
   ProcessInput (i) tests buttons with (h); weapon slots are `0x400 << i` → `SelectWeaponForSelectionGroup`
   0x14620F0. It calls (j), which calls (k) at 0x14562A3 (return address 0x14562A8), and 0x1440C70 copies
   the tracker's command into the physics object with (m).
4. How the manager's command gets into the tracker, and whether that adds a tick of delay, is [inferred]
   (the function was not pinned).

### 1.4 Recommended injection points

- **Buttons and movement: mid-hook at 0x43E8DD (f)** [static-verified shape]. There `r8` = the final
  `idUserCmd*` (after the gameTime and button fix-up), `edx` = the game player index, `ebx` = the local user
  index and `r14b` = 1 when the game suppressed buttons (console, editor); then no buttons are injected.
  Real input is already in the command, so injection adds to it: OR the edge-latched buttons into +0x10 and
  add to +0x18 / +0x19 with a clamp to ±127. Keyboard, mouse and pad keep working.
- **Angle deltas: mid-hook at 0x17FD3C0 (d)** [static-verified shape]: add the turn and aim deltas to the
  generator's accumulated floats (`rdi` = gen; `[[rdi+0x8D8]+4]` pitch, `+8` yaw). They then persist exactly
  like mouse motion. A delta added only to the command's shorts is lost on the next frame, because the next
  build starts again from the accumulator. The existing `deltaViewAngles` write (head aim, `player_aim.cpp`)
  stays as the fallback.
- The dormant VR path inside (c) reads a VR device (0x1DCF2A0) and, with cvar `vr_controllerMovement`, adds
  the VR stick × 127.5 to forward / right (R15). Keep it off; the injection above does not need it.
- (c)'s `menuMode` argument clears the look accumulation and handles triggers only, so menus never see
  injected look.
- Stick look sets `gen+0xA44`, which gates aim assist in UpdateViewAngles [inferred]. Injection through the
  accumulator (not the stick path) leaves aim assist off, as a tracked hand wants.

## 2. Weapon fire origin and direction (T-055)

### Call chain [static-verified]

`idHands::FireWeapon` 0x135D210 → `idHands::GetWeaponFireInfo` 0x135F280 → an `idFireParms` on the stack →
`idWeapon::Fire` (vslot 0x3B0, 0x16BDDA0) or `idWeapon::DeferredFire` (vslot 0x3E0, 0x16B34E0) →
`idWeapon::FinishFire` (vslot 0x3C8, 0x16BA810) → `idMapInstanceLocal::FinishLaunchProjectile` 0x1681EE0,
`TestHitScan` 0x16899B0 or `ClientLaunchProjectile` 0x1679E50.

- `idHands` is embedded in `idPlayer` at +0xD2C8 (type info); its vtable is 0x2DA21C8 and `idHands.owner`
  is at +0x358.
- **There is no separate stored aim angle.** The fire origin and axis are derived for every shot from the
  player's first-person view (idPlayer vslot 0x478 origin and 0x470 axis, which read
  `firstPersonViewOrigin/Axis` at +0x16580 / +0x1658C) and from the muzzle tag.

`GetWeaponFireInfo(idHands* this, idWeapon* w, idDeclWeapon* d, idVec3* firePos, idMat3* fireAxis,
idVec3* muzzlePos, idMat3* muzzleAxis)`:

1. The muzzle starts as the view origin and axis, then is replaced by the hands item's muzzle tag (0x1389020,
   called on the item for the fire slot; on failure the game logs
   `(%d)%s - GetMuzzlePosition() failed on <%s>`).
2. Slots 9 and 10 (the shoulder equipment launchers), cvar `hands_updatePos` 0, or
   `idDeclWeapon.useMuzzleAsFireAxis` (+0xB90): fire = muzzle position and muzzle axis.
3. Otherwise, if (no projectile decl, or `idDeclProjectile.hitscan` (+0xB8), or
   `!notHitscanInfo.fireFromMuzzle` (+0x260)) and `!idDeclWeapon.traceMuzzleToCrosshairForFireAxis` (+0xB91):
   **fire = view origin and view axis**. This is the normal branch for hitscan weapons: the shot leaves the
   eye.
4. Otherwise (projectiles with `fireFromMuzzle`): firePos = muzzle; a trace from the view finds the aim
   point (optionally the deferred traces at `idHands` +0x90D4 / +0x9154, cvar
   `hands_useDeferredViewAimMuzzleTraces`), and fireAxis = normalize(aim hit − muzzle): the shot converges
   on what the view ray hits. If the view-to-muzzle trace is blocked, firePos = view origin.
5. A fire-to-muzzle trace clamps muzzlePos to firePos when blocked; the decl's `fireAxisHorizRotationAngles`
   (+0xB8C) rotates the axis.

`FireWeapon` then fills `idFireParms`: `start` (+0xF0) = firePos, `muzzleOffset` (+0xFC) = muzzle − firePos,
`muzzleAxis` (+0x108), `fireAxis` (+0x12C). `FinishLaunchProjectile`, `TestHitScan` and
`ClientLaunchProjectile` all read `start` and `fireAxis`. The fire position and angles are also copied to
`idClientWeaponFire` (idPlayer +0x8820, call 0x1265A20) for multiplayer replication.

**One hook covers hitscan and projectiles:** every `idHands::FireWeapon` shot goes through
`GetWeaponFireInfo`, including the burst-shot, primary-only and secondary-only animation events,
`Event_EquipmentLauncherBurstFire` 0x135CA70 and `UpdateWeapon_Default` 0x1382F10 [static-verified call
graph; inferred that no player weapon fires through another path, see section 9].

| Hook | RVA | Signature (unique in `.text`) | Conf. | Use |
|---|---|---|---|---|
| `idHands::GetWeaponFireInfo`: call the original, then overwrite `*firePos` / `*fireAxis` (and the muzzle if wanted) with the hand ray | 0x135F280 | `4C 8B DC 55 53 56 57 41 55 41 56 41 57 49 8D AB ?? ?? ?? ?? 48 81 EC 20 02 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 ?? ?? ?? ?? 48 83 B9 ?? ?? ?? ?? 00 4D 8B F1 48 8B B5 ?? ?? ?? ??` | [static-verified] | **Recommended T-055 hook.** Two callers: FireWeapon (0x135D72E) and the per-frame visual update 0x1380F70 (only when `hands_useDeferredViewAimMuzzleTraces` is non-zero). Spread, linked shots and the multiplayer copy are applied downstream |
| FireWeapon, just after GetWeaponFireInfo returns (mid-hook) | 0x135D733 (pattern starts 0x135D712) | `48 8D 4C 24 78 49 8B D6 48 89 4C 24 28 48 8D 8D ?? ?? ?? ?? 48 89 4C 24 20 49 8B CF E8 ?? ?? ?? ?? 0F B6 44 24 40 48 8D 4D B0 41 80 A6 ?? ?? ?? ?? DF C0 E0 05 41 08 86 ?? ?? ?? ??` | [static-verified] | Fire path only. r15 = idHands, r14 = idWeapon; firePos `[rsp+0x68]`, fireAxis `[rbp+0x2B0]`, muzzlePos `[rsp+0x78]`, muzzleAxis `[rbp+0x260]` |
| `idMapInstanceLocal::TestHitScan(fp)` | 0x16899B0 | `4C 8B DC 55 56 49 8D AB ?? ?? ?? ?? 48 81 EC D8 08 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 ?? ?? ?? ?? 8B 02 48 8B F2 49 89 5B E8 49 8B D9 49 89 7B E0 4D 89 63 D8 4C 8B E1 4D 89 6B D0` | [static-verified] | Per trace; monsters use it too (filter on `fp.attacker`). Debugging only |
| `idMapInstanceLocal::FinishLaunchProjectile(fp)` | 0x1681EE0 | `40 55 53 56 41 54 41 56 41 57 48 8D AC 24 ?? ?? ?? ?? 48 81 EC 08 03 00 00 44 0F 29 A4 24 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 ?? ?? ?? ?? 44 0F 10 A2 ?? ?? ?? ?? 4C 8B F9` | [static-verified] | Same caveat |
| Data path: set `useMuzzleAsFireAxis` (idDeclWeapon +0xB90) on the player's weapon decls | – | – | [static-verified] branch; [inferred] outcome | Every weapon then fires from the muzzle tag along the muzzle axis, which, with the viewmodel at the grip (section 3), is the tracked muzzle with no per-shot hook. How well each muzzle joint's axis matches its barrel is unknown |

**What this means for decoupled aim.** With the design as it stands (view angles = hand ray, camera = head),
hitscan fires from the head along the hand's direction: the parallel-offset error R13 section 6.3 predicted,
now confirmed as the default branch. Projectiles with `fireFromMuzzle` converge from the muzzle onto the
view-ray hit, so they miss close in. Replacing firePos and fireAxis in the 0x135F280 wrapper fixes both,
and the closed loop (T-055) still keeps the view angles on the hand ray, so the crosshair, lock-on and
the Meathook's target selection (which use the view) agree with the shot.

The wrapper needs a wall check of its own: the game's view-to-muzzle and fire-to-muzzle traces (queries at
`idHands` +0x90B8 / +0x90C0 / +0x90C8) run from the view, not from our pose. Start the ray at the last valid
head position (T-062) and trace to the tracked muzzle, or keep the game's firePos when it differs from the
view origin (a blocked muzzle).

Other `idHands` fields [static-verified]: `lockOnPosition` +0x891C, `burstShootAimPoint` +0x8D48,
`overrideStartFxAxis` +0x8E3C (reset to identity by GetWeaponFireInfo), `forceHandsFOVScale` +0x8E38.

### Forced-angle states: when aim injection must yield

| Signal | Where | Conf. |
|---|---|---|
| `idPlayer::SetViewAngles` (0x1454480) called by anything other than UpdateViewAngles (return address other than **0x14562A8**): the game forces the view this tick. The 47 other callers include `idAlignedEntity::UpdateOwnerFromCamera` 0xB44CD0 and `AnimEvent_PlayerSnapToEntity` 0xB3DC10 (sync and glory kills), `MeleeLunge_SnapViewToEntity` 0x143F400, the meathook pull 0x166F5E0 (from `idDoubleBarrelShotgun::Service_PulledForward` 0x1673730), `WallClimb::DisconnectFromWall` 0x13B42B0, restrict-view 0xC9BA70, slow-motion, photo and trailer cameras (0xDC4BA0, 0xDC4F00, 0xDCF5D0) and the environment-suit revive 0xFD6350 | entry hook of 0x1454480 | [static-verified] callers; [inferred] meanings, from strings |
| `inhibitFlags & (VIEW 0x8 \| VIEW_ONCE 0x100)` at `idPlayer` +0x87FC: the game itself skips the view update | 0x1454480 body | [static-verified] |
| Post-sync view reset: while `now < idPlayer.syncResetTime` (+0x79D0) UpdateViewAngles lerps towards `syncResetAngles` (+0x79C4); cvars `pm_gradualSyncViewReset`, `pm_gradualSyncViewResetTime` | 0x1455D70 | [static-verified] |
| Meathook: `meatHook_playerViewOverrideMode` 0 (default) = "view is auto oriented until player adjusts view stick", 1 = "only oriented initially"; the layer sets it to 1 (R13) | cvar object 0x46A6AE0, read in 0x166F5E0 | [static-verified] |
| Climbable wall: `idPlayerMechanicWallClimb` (vtable 0x2DAF710; per-tick update slot 11 = 0x13B7190; states 0 Init, 1 Searching, 2 JumpToWall, 3 NoMove, 4 MovingTransition, 5 Moving, 6 ToPointing, 7 PointingTransition, 8 MountLook, 9 SwitchHands; above 2 = on the wall). With `wallclimb_takeoverViewAngles` 1 (default; object 0x467B750, flags bool only) Enter_JumpToWall 0x13B50B0 sets inhibitFlags to 0xF (7 with the cvar at 0) through 0x13FC530, and while on the wall the update turns the mechanic's own angles (+0x13B0) by the user command's angle change (command +0xB4 against +0x1C), clamps them to the wall and calls SetViewAngles(+0x13A4) every tick (return 0x13B87CE); DisconnectFromWall 0x13B42B0 clears 0xF (0x13D2DF0) and calls it once (return 0x13B4677). The dead-zone step 0x13B88F0 (`wallclimb_deadZone_enable`, object 0x467B8D0; called only on the wall) pushes a view that looks down at the wall out of a 0.8 m circle 0.9 m below the eye on a plane 0.3 m ahead: into the mechanic's angles with the takeover, through SetViewAngles (return 0x13B95D7) without it. The jump off, JumpOff 0x13B97B0 (from NoMove, MovingTransition, Moving, MountLook, SwitchHands), goes along +0x1380, the player's firstPersonViewAxis forward copied from vslot 0x470 at the top of each update, pitched up about its horizontal right axis by +0x1728 degrees, at speed +0x16B4; NoMove 0x13BC300 jumps off only when that forward is turned from the wall normal (+0x140C) by the angle at +0x172C, and lets go otherwise | disassembly and decompile of those functions | [static-verified]; the layer's use (`climb_hook.cpp`) needs a rig check |
| Root-motion animations on the player: the player think 0x1442710 calls UpdateViewAngles (0x144297B) and then the player-mechanic body update (0x13901C0 at 0x14429E0, object at `idPlayer` +0x371C0). While the third-person body's flags (+0x3104) have bit 1 set, 0x138F490 calls 0x138F210, which (with `pmec_ApplyAnimDeltasToPlayer` 1, object 0x4674380; `pmec_ApplyAnimDeltasForceClip` 0x4674400 and `pmec_ApplyAnimDeltasForceNoClip` 0x4674480 choose the clip) takes the animation's root delta (0xB41AE0), sets the view to the current view angles minus the delta rotation's angles (SetViewAngles, return **0x138F33B**) and moves the player by the delta translation (0x1444FA0). On a climbable wall this runs every tick for the whole climb, and with the view the player's own (takeover cvar 0) it re-sets the angles the view update has just made, turned only by the animation's rotation; so a foreign call does not end the climb state in the layer's gate, or hand aim would send nothing for the whole climb and the jump would follow the angles from the grab. It also fires off the wall (about 0.45 s episodes after a jump, likely mantles), which stay forced views. DisconnectFromWall's call (0x13B4677) sets the mechanic's copy of the view (+0x13A4, taken from the player at the top of each tick, roll 0) and is treated the same | disassembly of 0x1442710, 0x13901C0, 0x13902B0, 0x138F490, 0x138F210, 0x13B42B0; headset logs 2026-09-30 (0x138F33B from the grab to the jump) | [static-verified] call order and arithmetic; [inferred] the climb animations carry no root rotation (the climb stats line counts the turns) |
| Input blocked or cinematic: 0x143E220 returns true when `gameLocal+0x1252B8 != -1`, a playerHud state is active, or `idPlayer+0x736E & 0x18`; ProcessInput then returns early | 0x143E220 | [static-verified] logic; [inferred] field names |
| Lock-on (Rocket Launcher) and the Ballista only read the view; none of the SetViewAngles callers is a weapon lock-on, so the lock-on target is whatever the view (the hand ray under closed-loop aim) points at | caller list | [inferred] |
| Generic net: the game changed `deltaViewAngles` since our last write (head aim's `gameRewrote` test). Its only writer is `idUCmdTracker::SetViewAngles` 0x146B870 (callers 0x1454480, 0x12C0620 and a site at 0x140F3C8) | | [static-verified] |

The forced-angle input of `closedLoopAim` is then: a foreign SetViewAngles call this tick or the last,
`inhibitFlags & 0x108`, `now < syncResetTime`, 0x143E220 true, or `inCutscene` (renderView_t +0x15). While
any holds no aim delta is sent, and the body yaw is re-read afterwards, as head aim already does after a
cutscene.

The weapon wheel sets `inhibitFlags` to 0x18 (VIEW 0x8 and BUTTONS 0x10) about 60 ms before it shows and
clears them when it closes (player logs 2026-10-03: `forced view starts (view inhibited; inhibit 0x18)` just
before `the weapon wheel is up`). Treated as a forced view, it handed the viewmodel back to the game's
flatscreen placement and eased the head onto the game's eye for as long as the wheel was up. So with the
wheel's button held and no inhibit bit but those two, the gate's reason is `WeaponWheel`, which yields only
the aim (hand aim sends nothing, as the game skips the view update anyway): the viewmodel, the off hand, the
shots and the head's place go on, and no settling frames follow.

## 3. Viewmodel placement (T-054)

`idHands::UpdatePosition` 0x137FD60 (named by its log string) runs once per game frame from 0x1380F70
(gated on a frame counter at `idHands` +0xFC0); its caller is `idHands` vslot 6 → 0x135CD70. It computes
[static-verified unless marked]:

- `origin = V.origin + V.fwd*ox + V.left*oy + V.up*oz + extraWorldTranslation`, plus a step-up spring on z
  (`idPlayer` +0x8758 − +0x8728). V is idPlayer vslot 0x478 / 0x470, the same getters the render-view build
  uses. The offsets are the cvars `hands_offsetX/Y/Z` (objects 0x4670F60 / 0x4670FE0 / 0x4671060) plus
  `idDeclWeapon.handsOffset` (+0x1500); when `idHands.testModel` (+0x2998) is set a second cvar set replaces
  them.
- `axis = R(offsetPitch/Yaw/Roll) · R(player +0x8784) · extraWorldRotation · V.axis`, with the angles from
  the cvars `hands_offsetPitch/Yaw/Roll` (0x46710E0 / 0x4671160 / 0x46711E0) plus the decl's
  `handsOffsetAngles` (+0x150C). The exact composition order is [inferred].
- It writes `idHands.renderModel` (`idRenderModelSkinned*` at +0x370): `deferredOrigin` +0xF8 and
  `deferredAxis` +0x104, and, unless the flags byte at +0xB0 has both bits 0x0C set, `g.origin` +0x158 and
  `g.axis` +0x164; then it commits with 0x18DE470.
- `idView.weaponFOVScale` (+0x3890) = FUN_141360100(hands) × (`hands_fovScale` if above 0, else the
  interpolated `idDeclInventory.handsFovScale` (+0x120), else `forceHandsFOVScale`). **`hands_fovScale`
  defaults to "0", meaning no override** (object 0x4671260, read by 0x137FD60 and 0x1227F50). It also writes
  `idView.customFOVScale2` (+0x3894).

`extraWorldTranslation` (+0x8928) and `extraWorldRotation` (+0x8934) are read only by UpdatePosition and
written only by the reset/constructor 0x134BDF0 (zero and identity) and a copy routine 0x218FF60
[static-verified by a displacement scan; inferred that nothing writes them through reflection]. They are free
slots for the grip pose that need no code patch.

| Hook | RVA | Signature | Conf. | Use |
|---|---|---|---|---|
| **Mid-hook before the render-model store** | 0x13807EA | `0F B6 86 ?? ?? ?? ?? 0F 10 45 D0 24 0C 3C 0C 74 54 0F 11 86 ?? ?? ?? ?? 0F 10 4D E0 0F 11 8E ?? ?? ?? ?? 8B 45 F0 89 86 ?? ?? ?? ?? 0F 10 45 D0 0F 11 86 ?? ?? ?? ?? 0F 10 4D E0 0F 11 8E ?? ?? ?? ?? 8B 45 F0 89 86 ?? ?? ?? ??` | [static-verified] | **Recommended.** rsi = renderModel, rdi = idHands; the final origin is at `[rsp+0x48]` (idVec3), the final axis at `[rbp-0x30..-0x0C]` (idMat3, rows forward/left/up). Overwrite both with the grip pose plus the per-weapon offset; the existing stores and commit carry it through |
| `idHands::UpdatePosition` entry | 0x137FD60 | `48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 56 41 57 48 8D A8 ?? ?? ?? ?? 48 81 EC 80 03 00 00 0F 29 70 D8 0F 29 78 C8 44 0F 29 40 B8 44 0F 29 48 A8 44 0F 29 50 98 44 0F 29 58 88` | [static-verified] | Pre-hook for the data path: write `extraWorldTranslation = G.o − (V.o + Vᵀ·o)` and `extraWorldRotation = G.axis · V.axisᵀ` (offset angles zeroed or compensated) |
| `idView.weaponFOVScale` consumer (in 0x147DB10, caller 0x1481D30) | 0x147EB70 | – | [static-verified] | `weaponFOVX/Y` (renderView_t +0x30 / +0x34) = f(camera FOV × weaponFOVScale); `customFOV2X/Y` (+0x38 / +0x3C) = f(camera FOV × customFOVScale2) |
| Render latch builds `customViewProjectionMatrix` (idRenderView +0x295B0) from `weaponFOVX/Y` (+0x28A00 / +0x28A04) and `customViewProjectionMatrix2` (+0x29630) from `customFOV2X/Y` (+0x28A08 / +0x28A0C), through 0x39A310 | 0x1CE1850–0x1CE190B | – | [static-verified] | The hands and weapon use a projection of their own |

**Needed alongside T-054: the weapon FOV.** The head-tracked camera hook rewrites only `fov_x` / `fov_y`, so
the viewmodel still renders with the game's own narrower weapon FOV. At the build point (0x6A31B7) the layer
must also copy `fov_x` / `fov_y` into `weaponFOVX/Y` (+0x30 / +0x34) and `customFOV2X/Y` (+0x38 / +0x3C).
Setting `hands_fovScale 1` also stops the per-weapon `handsFovScale` scaling, which is the ROADMAP's
"`hands_fovScale` stays 1". `renderView_t.inhibitModelFovScale` (+0x13) has no reader at `r+0x13` in the
render latch; what it does is [inferred] and needs a live test. For stereo (M4) the two custom projection
matrices must also be made per eye: the latch builds them from symmetric FOVs, so they do not follow
`useExplicitProjectionMatrix` [inferred].

## 4. Audio listener

Chain [static-verified]:

- The engine frame job 0x43A1F0 is registered in the frame-job table at 0x388EE00, right after
  `idCommonLocal::Frame` 0x43A120 (table built at 0x43DB8F). For each of four local players whose
  `players[i].valid` is set it calls `soundSystem->vslot 0x68` (the sound-world manager at
  idSoundSystemLocal +0x58310; the soundSystem global is 0x47DD9E0) → `idSoundWorld2ManagerLocal` vslot 0x40,
  **SetListener(handle*, origin*, axis*)** 0x1D9F110.
- **The origin and axis passed are `gameFrameReturn_t.players[i].view.vieworg` / `.viewaxis`**: the frame
  block is at +0x3580 in the job data, the fields at `+0x3664 + i*0xA00` and `+0x3670 + i*0xA00`. That is
  the render view the head-tracked hook rewrites at 0x6A31B7.
- SetListener queues message 10 {position, forward = axis row 0, up = axis row 2}. On the sound thread,
  0x1D9E5C0 case 10 → 0x1D88A20 → listener queue message 3 → 0x1D87FA0 case 3 → 0x1EC4B40, the Wwise
  `SetPosition` call, with an AkTransform whose y and z are swapped (id Z-up to Wwise Y-up).
- A second SetListener caller, 0x1751190, is a GUI/dialog-screen listener (its caller mentions
  `dialog_screen`); not relevant here.
- `s_lockListener` (object 0x6B493B0) has no reader outside its registration [inferred: dead in retail].
  `s_showPaths` (object 0x6B44730) is read by the debug path 0x1D82590.

**Conclusion [inferred: frame ordering].** The head-tracked build point writes `players[0].view` during the
game frame and the listener job reads it afterwards in the same frame, so the listener already follows the
rendered head's position and orientation, and the M5 audio criterion may already hold. It keeps holding as
long as the stereo path keeps writing the centre-eye head pose into `players[0].view`.

| Hook | RVA | Signature | Conf. | Use |
|---|---|---|---|---|
| Listener job, at the SetListener call (r8 = &vieworg, r9 = &viewaxis, rdi = i × 0xA00, rbp = frame block) | 0x43A27A (call at 0x43A2A6) | `48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 50 68 4C 8D 8D ?? ?? ?? ?? 48 8B C8 4C 8D 85 ?? ?? ?? ?? 4C 03 CF 4C 03 C7 48 8D 54 24 20 4C 8B 10 41 FF 52 40 FF C3 83 FB 04 7C 8F E8 ?? ?? ?? ??` | [static-verified] | An explicit override if the ordering or the stereo path breaks the implicit route: point r8 / r9 at our head pose (clamped and collision-valid, T-062) |
| Listener job entry | 0x43A1F0 | `48 89 5C 24 18 55 56 41 56 B8 80 38 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 ?? ?? ?? ?? 48 8B F1 33 D2 48 8D 4C 24 30 E8 ?? ?? ?? ?? 48 8B 6E 08 E8 ?? ?? ?? ??` | [static-verified] | – |
| `idSoundWorld2ManagerLocal::SetListener` (vtable 0x2ECB480 + 0x40) | 0x1D9F110 | `48 83 EC 68 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 50 0F B7 02 BA FF FF 00 00 66 3B C2 0F 84 ?? ?? ?? ?? F3 41 0F 10 00 48 8D 54 24 20 F3 41 0F 10 48 04 F3 0F 11 44 24 2C F3 41 0F 10 40 08` (continues to the `C7 44 24 20 0A 00 00 00` message id) | [static-verified] | A vtable-slot swap covers both callers |

## 5. The gamepad (XInput) path

| What | RVA | Signature / evidence | Conf. |
|---|---|---|---|
| Imports: `XINPUT1_3.dll` **by ordinal only**: ordinal 2 `XInputGetState`, IAT slot **0x2A1C8A0**; ordinal 3 `XInputSetState`, IAT slot **0x2A1C8A8**. No `XInputGetCapabilities`, no GameInput, no Windows.Gaming.Input, no Steam Input API | – | the PE import table (re-read with `pefile`) | [static-verified] |
| Import thunks: 0x2267B63 `FF 25` → GetState, 0x2267B69 → SetState | – | – | [static-verified] |
| `idJoystickWin32` vtable (RTTI): slot 1 Init 0x1DC4960 (creates the "JoypadTimer" waitable timer), slot 4 SetRumble 0x1DC5850 | 0x2ED0598 | – | [static-verified] |
| Sampler thread: four pads, stride 0x1CC, re-probes disconnected pads, waits on the timer | 0x1DC4AA0 | `48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 41 54 41 55 41 56 41 57 48 83 EC 40 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 30 45 33 E4` | [static-verified] |
| PollPad: `XInputGetState`; when `dwPacketNumber` advanced, stores the state and **latches** `wButtons` (`pad+0x2C \|= buttons`) | 0x1DC57A0 | `48 89 5C 24 18 57 48 83 EC 40 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 30 48 63 FA` | [static-verified] |
| Event generation (game thread): button changes → `SE_KEY` K_JOY 0x100 + bit; sticks → `SE_JOYSTICK` axes 0x20..0x23 (Y negated) plus direction keys 0x110..0x117 at ±0x4000; triggers → axes 0x24 / 0x25 and keys 0x118 / 0x119 above 0x4000 after `<< 7` | 0x1DC4C00 (helper 0x1DC5310) | `48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 81 EC B0 00 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 A0 00 00 00 48 63 F2` | [static-verified] |
| Event sink: `idInputLocalWin32` static object .data 0x6B54908 (pointer at .rdata 0x2ECF708); vtable 0x2ECF718 slot 12 (+0x60) = QueueEvent 0x1DB5C30 | – | – | [static-verified] |
| Command side: keys go through the binds (`SendButtonPress` 0x17FEDC0); the sticks go through 0x17FE810 when `in_joystick` (cvar 0x6B4AB80) is set: `forwardmove -= y * 127.5`, look adds to the accumulated angles and sets `gen+0xA44` (the aim-assist gate) | – | – | [static-verified] |

**Fallback injection (T-009, L2):** replace IAT slot 0x2A1C8A0, matched by **ordinal 2** (there is no name
to match). The replacement runs on the sampler thread for user indices 0..3 and returns the real pad merged
with ours, so a connected pad still works. `XInputSetState` (0x2A1C8A8, ordinal 3) carries rumble, the
source for per-hand haptics ("v1 if time allows"); `in_joystickRumble` and `com_skipJoystickRumble` apply.
The fallback maps our `GameInput` onto the game's default pad binds, so it depends on those binds; the
usercmd hook does not.

## 6. Code built for M5 (portable, tested)

| Module | Files | What it does | Tests |
|---|---|---|---|
| OpenXR action sets | `src/features/input/xr_action_set.{hpp,cpp}` | The `gameplay` action set and its actions: per-hand trigger, grip, thumbstick, stick click, primary, secondary, face3, face4, shoulder, menu, aim pose, grip pose and haptic (a `menu` set planned for M6 was dropped: the menus read these actions). Every action has both hands as subaction paths, so handedness lives only in the control map | `xr_action_set_tests.cpp` |
| Interaction profiles | `src/features/input/interaction_profiles.{hpp,cpp}` | The input paths of `oculus/touch_controller` and `valve/index_controller` per hand, the type rules (which path suits a button, analog, two-axis, pose or haptic action) and the overlap rule (two bindings reading one component) | `controller_bindings_tests.cpp` |
| Controller data | `data/input/controllers/*.toml`, `src/features/input/controller_bindings.{hpp,cpp}`, `src/game/eternal/controller_data.{hpp,cpp.in}` | One file per controller family (Touch, Index, G2, WMR, Cosmos, Vive wands, Pico 4, Steam Frame; docs/VR_CONTROLLERS.md "Controller families"): `[profile]` holds the OpenXR suggested bindings, `[map.right]`, `[map.left_button_swap]` and `[map.left_full_mirror]` the default control maps. The files are built in by CMake, so the tests check the files that ship; a player's copy reads the same way. Index keeps the Touch layout: left A/B take the X/Y roles, grip reads the squeeze force, and the menu input is a firm press on the left trackpad (System belongs to SteamVR) | `controller_bindings_tests.cpp`, `tests/game/eternal/controller_data_tests.cpp` |
| Binding conflicts (T-106) | `src/features/input/binding_compiler.cpp`, `binding_issue.hpp` | Each conflict names both keys and what each binds, in the message and as fields (`conflict`, `value`, `otherKey`, `otherValue`). Kinds: press with tap or hold on one input, a stick role on both sticks, a gesture off the turn stick, and (controller data) two actions of one set on one physical input | one test per kind in `binding_compiler_tests.cpp` and `controller_bindings_tests.cpp` |
| Locomotion and turning for the user command | `src/features/input/usercmd_motion.{hpp,cpp}` | Head- or off-hand-relative move from the stick, rotated into the view frame (the game moves relative to its view yaw, which follows the weapon); quantised to integer move axes keeping the direction; turn degrees to the command's 16-bit angle units with the remainder carried, so a 360-degree turn is exactly 65536 units | `usercmd_motion_tests.cpp` (360-degree smooth and snap turns both ways, head- and hand-relative moves) |
| Hand ray to view angles | `src/xr_math/hand_aim.{hpp,cpp}` | The aim pose as a world ray (origin placed relative to the rendered head, axes `id = (-z, -x, y)`, body frame applied), its id Tech angles, the closed-loop correction (yields when forced or on implausible values), the aim-error metric of the M5 criterion and the convergence fallback | `tests/xr_math/hand_aim_tests.cpp` |

The existing policies stay as they are: `turn_policy` (smooth, snap, off), `locomotion_direction`,
`stick_response`, `turn_stick_arbiter` and `input_mapper`.

## 7. Recommended M5 implementation plan

Every step is behind `mp_guard::allowsGameTouch()` and installed only once the guard is armed. Every hook is
found by the signatures above and refuses (logs, stays off) on any mismatch, as `player_aim.cpp` does.

1. **OpenXR input (presenter side).** Create the two action sets from `xrActionSets()` / `xrActions()` with
   both hands as subaction paths. Read the controller data (`builtinControllerData`, or a player's copy) with
   `parseControllerData`; data with issues is refused and the built-in file used. Suggest its bindings with
   `xrSuggestInteractionProfileBindings` (Touch and Index now; the other profiles of R01 later, as more data
   files). Attach once, sync `gameplay` each frame (plus `menu` while a panel is open), and fill `InputFrame`
   from the action states and from the aim and grip spaces located at the predicted display time of the frame
   the game is simulating (T-055).
2. **Mapping.** Compile the control map for the player's handedness (`buildBindingProfile` with the player's
   overrides; a conflict is refused with its two-sided message) and run `InputMapper` each frame. Its
   `GameInput` goes to one writer.
3. **User command writer (L1).** Mid-hook at 0x43E8DD: map `GameAction`s to `usercmdButton_t` bits (a table in
   `game/eternal`, from 1.2; chainsaw and Flame Belch after the live bind check), OR them in unless r14b is
   set, and add `quantizeMove(move, 127)` to forward / right. Mid-hook at 0x17FD3C0: add the turn
   (`AngleUnitAccumulator` keeps whole turns exact) and the aim correction to the accumulated angles.
   Tap-type actions (quick switch, equipment switch) are held for at least one game tick, since the game
   samples at its own rate.
4. **Gamepad fallback (L2).** The IAT slot of ordinal 2, with the same `GameInput` mapped onto the default
   pad binds; selectable, and used when the L1 signatures do not match.
5. **Locomotion and turning.** `Locomotion` (head or off-hand frame, rotated into the view yaw, which follows
   the weapon) and `TurnPolicy` (smooth, 230°/s by default; snap 30/45/90; off). The body yaw that the head
   and the hand ray are composed with moves with the turn, so turning never moves the aim relative to the
   hand.
6. **Hand aim, closed loop (T-055).** Each frame: the body frame from the game's heading minus the injected
   aim (as head aim does), `handRayInWorld` from the aim pose placed relative to the rendered head,
   `anglesOfDirection` as the target, the game's view angles read back from `idHavokPhysics_Player`, and
   `closedLoopAim` with the forced-angle input of section 2. Pitch ownership moves from the head to the hand.
   Log `aimErrorDegrees` at every shot (the 0.5° p99 criterion). The render camera stays on the head.
7. **Shot origin.** Wrap `idHands::GetWeaponFireInfo` (0x135F280): after the original, set `*firePos` to the
   tracked muzzle (grip pose × per-weapon muzzle offset, traced from the last valid head position; keep the
   game's value when blocked) and `*fireAxis` to the hand ray. One hook covers hitscan and projectiles. The
   convergence fallback (`convergenceAngles` on the hand ray's hit) needs none of this and is the switch if
   the wrapper misbehaves.
8. **Viewmodel at the grip (T-054).** Mid-hook at 0x13807EA: replace the final origin and axis with the grip
   pose in the world (the same body-frame transform) times the per-weapon offset (data, per weapon decl). At
   the camera build point also copy `fov_x/fov_y` into `weaponFOVX/Y` and `customFOV2X/Y`, and force
   `hands_fovScale 1`. Seated (T-074): the desk-safe offsets are per-posture entries of the same table.
9. **Audio.** Nothing to add while the camera hook writes the head pose into `players[0].view`; confirm with
   `s_showPaths 1`. The explicit override at 0x43A27A is the fallback, and M4's stereo path must keep the
   centre-eye pose there.
10. **Cvars forced for the session** (restored on exit): `meatHook_playerViewOverrideMode 1`,
    `hands_fovScale 1`, and aim assist off.

Order: 1–3 and 5 first (move, turn and shoot with head aim), then 6–7 (decoupled aim), then 8 (viewmodel),
then 4 (fallback), and then the swap and seated protocols.

## 8. Live experiments that confirm each finding (rig, not run)

| # | Finding | Experiment | Pass |
|---|---|---|---|
| E1 | 1.3 build and hand-off | Mid-hook 0x43E8DD; log buttons, moves and angles per frame while pressing fire, jump, dash, C and R, and with the console open | bits 0x1, 0x100000000, 0x400000; C and R show which `QUICK_n` are chainsaw and Flame Belch; console open: r14b = 1 and buttons 0 |
| E2 | 1.4 injection | OR 0x1 in at 0x43E8DD; write forwardmove +127; add 2° to `[[gen+0x8D8]+8]` at 0x17FD3C0 | fires with no key held; walks; `idPlayer+0x8A50+0x3F10` yaw moves 2° and stays |
| E3 | 1.3 chain | Hardware breakpoint on `idPlayer+0x8A50+0x3DE0` | the writer is 0x535230 called from 0x144168A, once per tick |
| E4 | 1.3 tracker | Capture the tracker from IsPressed (0x146BE40); compare `tracker+0x98` with what 0x17FF7F0 wrote | equal, and the tick delay between them is known |
| E5 | 2 fire branches | Log `[rsp+0x68]` / `[rbp+0x2B0]` at 0x135D733 against `firstPersonViewOrigin/Axis` for every weapon and mod; record decl +0xB90 / +0xB91 and projectile decl +0xB8 / +0x260 | hitscan (Combat Shotgun, Heavy Cannon): firePos = view origin; projectiles (Rocket Launcher, Plasma): firePos = muzzle, axis converging on the view hit |
| E6 | 2 override hook | Wrap 0x135F280, rotate `*fireAxis` 10° in yaw, then move firePos 0.3 m right | impacts shift 10° for hitscan, projectiles and the Ballista; tracers still leave the muzzle |
| E7 | 2 data path | Set `useMuzzleAsFireAxis` on the current weapon decl; `hands_drawMuzzlePos 1` | shots follow the barrel |
| E8 | 2 forced angles | Hook 0x1454480 and log the return address in a glory kill, a meathook pull and a melee lunge; log +0x79D0 against game time after a sync, and +0x87FC in scripted sequences | the callers named in section 2; injection yields in each |
| E9 | 3 viewmodel | At 0x13807EA add 0.2 m along V.left and 20° of yaw; then the same through `extraWorldTranslation` | gun, arms, muzzle flash and shell ejection all move; no ghosting under DLSS / TAA |
| E10 | 3 weapon FOV | At 0x6A31B7 set `weaponFOVX/Y` and `customFOV2X/Y` = `fov_x/fov_y`; compare `hands_fovScale` 0 and 1, and `inhibitModelFovScale` 1 | the gun's apparent size matches the world at the headset FOV |
| E11 | 4 audio | `s_showPaths 1` in head-tracked mode while turning and swaying (`ETERNALVR_TEST_HEAD_SWAY`); breakpoint 0x1D9F110 and compare r8 / r9 with the pose the hook wrote; log the frame count in both hooks; with `ETERNALVR_HEAD_POSITION=1`, lean 0.3 m | the listener follows the head in the same frame, and the lean moves it |
| E12 | 5 XInput | Replace IAT slot 0x2A1C8A0 to return a synthetic pad | A shows up as `SE_KEY 0x100` in QueueEvent (0x2ECF718 + 0x60); the caller is the sampler thread, not the game thread |
| E13 | T-062 | Inject 2 to 20 cm steps as movement at 0x43E8DD and measure the physics-origin displacement | recorded in `usercmd-precision.md` |

## 9. Open questions

- Which function copies the manager's command into the per-player `idUCmdTracker`, and is the command delayed
  by a tick or more? (It sets the prediction time for the hand pose.)
- The exact binds of chainsaw and Flame Belch, and what `BUTTON_WEAP_0` does under "Change Ability".
- The meaning of the `idPlayer+0x736E` bits (bit 4 selects the tracker's alternate angle path, vfunc 0x160;
  0x18 blocks input), and where pitch is clamped (the tracker's vfunc 0x158, or physics).
- Can `vr_controllerMovement` and the VR device path in (c) be reused, or must they be kept off?
- 0x6E3C70, the second caller of PutUserCmd (bots or demos?). The 0x43E8DD hook avoids it.
- Does any player fire path skip `GetWeaponFireInfo`? Candidates: throwables (`idHands::ThrowItem`
  0x136D3A0), melee and Blood Punch traces (`idHandsMeleeTrace` +0x8958), the Crucible, BFG tracers, the
  Ballista's secondary and the Microwave Beam (`idPlasmaRifleCharged::microwaveMuzzleInfo_t` has its own
  muzzle).
- Does the muzzle tag (0x1389020) read this frame's render-model pose, so that it follows the viewmodel
  override, or last frame's?
- The order of the `idHands` update, `idPlayer::CalculateView` 0x14514D0 and the build point 0x6A2C10 within
  a frame; and whether the listener job's place after RunFrame is guaranteed or only usual.
- What `inhibitModelFovScale` does, and how to build the per-eye custom projection matrices in stereo.
