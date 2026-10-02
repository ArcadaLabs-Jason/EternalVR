# Jump pads and boosters: where the game launches the player (bHaptics launch)

Static analysis only, 2026-10-01; the game was not run. Input: `DOOMEternalx64vk.exe`, Steam build 25216728
(Rev 3.2), the same exe as `engine-facts.md`. Tools: the analysis scripts of `engine-facts.md` section 8,
Ghidra 12.1 headless, the type-info reader, and the maps' `.entities` and entityDef decls pulled from the
game's `.resources`. All addresses are RVAs in this build. Used by `src/vkcore/bhaptics_launch.cpp` and
`docs/BHAPTICS.md` (the Launch effect, a player's idea in public issue #1).

`[static-verified]`: read from the code or data end to end, and every signature below matches exactly once
in `.text`. `[inferred]`: a reading that a live run must confirm (section 5).

## 1. What the maps use [static-verified]

- Jump pads are `idTrigger_BouncePad` (entityDef `trigger/bounce_pad`, `launchFX = "fx/bounce_pad/basic"`):
  16 in e1m1, 20 in e1m2, 24 in e1m3, 10 in e1m4. Each names a `destination` (an `idInfo_BounceDestination`)
  and either `launchSpeed` (m/s) or `useFlightTime` with `flightTime` (s); a few carry `triggerFirst`
  (scripted on later) or sit in a dormancy or visibility layer.
- e1m2 has one `idTrigger_SonicBoom`, `capitol_trigger_boost_1`: a booster that blasts the player to
  `capitol_info_bounce_destination_1` at `speed` 75, gravity off, at the top of the (scripted)
  `capitol_trigger_bounce_pad_4`. `idInteractable_SonicBoost` (a booster used with a button, with countdowns
  and charged levels) exists in the type info but none of the dumped maps (e1m1 to e1m4) places one.
- `idTrigger_Push` (e1m2's four `invasion_trigger_push_*`) has `playerCanActivate = false`: for demon
  players only. Not a launch of the Slayer.

## 2. Jump pads [static-verified unless marked]

- `idTrigger_BouncePad` vtable 0x2C89700 (RTTI). Its slot 601 (+0x12C8) is `TriggerStuff_Impl` at 0xDA9300
  (the slot that is `idTrigger_Teleporter::TriggerStuff_Impl` in that class, docs/BHAPTICS.md): rcx the pad,
  rdx the activator. It works out the launch velocity (to the destination, from `launchSpeed` +0xCB4 or
  `flightTime` +0xCBC with `useFlightTime` +0xCB8; `failedTrajectoryBehavior` +0xCB0 = 2 returns without
  launching), hands it to the activator's physics (vslot +0xE78, then that object's vslot +0xA0 with the
  velocity; [inferred] GetPhysics and SetLinearVelocity) and then posts the activator the event at
  0x6C2B2B0 with the pad and its destination. For a player it then plays `launchFX` unless the last one
  (`idPlayer::lastBounceFxTime`, +0x8A18) is within the pad's `minTimeForLaunchFX` (+0xCD0).
- The event is defined at static init (0x2B8220): name `touchedBouncePad`, comment "Player touched a bounce
  pad", arguments `bouncePadEntity;bouncePadDestinationEntity;` (`ee`). The only code that loads 0x6C2B2B0
  besides its definition is that post in 0xDA9300.
- idPlayer's event dispatch (0x210D2A0) calls the handler at 0x13E5650 with rcx the player, r8 the first
  argument (the pad), r9 the second; nothing else calls it and no table points at it. The handler casts the
  pad to `idTrigger_BouncePad` (0x2133320, a type-info range check), and on success: when
  `bouncePadIsInTransit` (+0x2F4F3) is already set, clears it with `bouncePadLiftInAir` (+0x2F4F2) and
  unlocks movement if `bouncePadLockedMovement` (+0x2F4F4) was set; locks movement for a pad with
  `lockPlayerMovement` (+0xCC1); sets `bouncePadIsInTransit`; then a bot helper (0xB64280).
- 0x14037D0 (called from 0x1401360, [inferred] the player's think) sets `bouncePadLiftInAir` while in
  transit and the physics' vslot +0x148 is false, and clears the transit (0x13CFDE0) when it is true again
  after that: [inferred] +0x148 is "on the ground", so the flag holds from the launch to the landing.
  0x13D34F0 (idPlayer vtable slot 84) does the same.
- Hook: 0x13E5686, the `cmp byte [rbx+0x2F4F3], 0` right after the cast succeeded (handler + 0x36): rbx the
  player, rsi the pad. Signature (from the handler's start):
  `48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 20 48 8B D9 4D 8B F1 49 8B C8
  49 8B E8 48 8B FA E8 ?? ?? ?? ?? 48 8B F0 48 85 C0 74 78 80 BB F3 F4 02 00 00`.
- So a hook call is a launch that happened: the velocity was set, and a pad that found no trajectory and
  gives up (`failedTrajectoryBehavior` 2) never gets there. Jumps, double jumps, dashes, the Meat Hook and
  landings do not post `touchedBouncePad`.
- [inferred] `idTrigger` runs a trigger again for every frame the player stays in its volume (the pads set no
  `wait`), and a pad's throw takes a frame or two to carry the player out, so a launch can call
  the handler more than once. The layer counts touches less than 0.5 s apart as one launch
  (`bhaptics::LaunchFilter`); the log's `same launch` lines show how many there are.

## 3. Boosters [static-verified unless marked]

- `idTrigger_SonicBoom` vtable 0x2C93168; slot 601 is 0xDAF3B0: rcx the trigger, rdx the activator. It checks
  the activator is a player (0x211FB40, [inferred] by the same test 0xDA9300 uses), asks the player's sonic
  blast mechanic (`idPlayer::playerMechanicSonicBlast`, +0x36F78, vslot +0x28) whether it may start (if not it
  logs "Sonic Blast requested via idTrigger_SonicBoom when it's not ..." and returns), then either, for
  `isLandingPad` (+0xD28), sends the mechanic's state machine (player +0x371C0) transition 0x1B, or copies
  the destination, `speed` (+0xCDC) and the rest into the mechanic and sends transition 0x1A ([inferred] end
  and start of a blast).
- Hook: 0xDAF404 (+0x54), the `mov [rsp+0x38], rbp` reached only when the mechanic said yes: rsi the trigger,
  rdi the player. Signature: `48 89 74 24 20 57 48 83 EC 20 48 8B F1 48 8B FA 48 8B CA E8 ?? ?? ?? ?? 84 C0 0F
  84 ?? ?? ?? ?? 48 89 5C 24 30 48 8D 9F 78 6F 03 00 48 8B 03 48 8B CB FF 50 28 84 C0 75 1B 48 8D 0D ?? ?? ??
  ?? 48 8B 5C 24 30 48 8B 74 24 48 48 83 C4 20 5F E9 ?? ?? ?? ?? 48 89 6C 24 38`. The layer skips a trigger
  with `isLandingPad`.

## 4. Other signals looked at

- `idPlayer::bouncePadIsInTransit` read from the camera hook: one rising edge per launch, but a chain of pads
  (the next pad touched before the player's think saw the ground) never clears it, and the handler clears and
  sets it in one go when it is already set, so the second throw would be lost. Kept for the log only.
- `idPlayer::lastBounceFxTime`: moves only when the launch effect plays, gated by each pad's
  `minTimeForLaunchFX`.
- The feet's height (the landing detector): a pad's throw is a fast rise without a jump, but so is the Meat
  Hook, a double jump's second push, lifts and being thrown by a demon. Not used.

## 5. Rig recipe (not run yet)

`tmp-vr/rs/jprun.sh <name>` on the rig (it starts the mock Player and stops the game itself):

- Map `game/sp/e1m2_battle/e1m2_battle -checkpoint cp_09_first_signal` (player start -39.4 -579.1 -6.6),
  `ETERNALVR_BHAPTICS=1`, `ETERNALVR_TEST_INPUT` for the jumps, and
  `ETERNALVR_DEBUG_COMMANDS=3:god|4:getviewpos|6:setviewpos -20.9 -552.5 -3.6 0|7.5:getviewpos|9:setviewpos
  -39.4 -579.1 -4.95 0|34:setviewpos -20.9 -552.5 -3.6 0|35.5:getviewpos|37:setviewpos -39.4 -579.1 -4.95
  0|40:setviewpos 206.4 316.05 -22.7 0|43:getviewpos`.
- `trenches_trigger_bounce_pad_2` (-20.9 -552.5, trigger origin z -5.6, floor about -6.7, `launchSpeed` 21,
  destination 10.4 m straight above, in no layer, no `triggerFirst`), 32 m from the checkpoint. `setviewpos`
  takes the eye height, so the 6 s step puts the player about 1.4 m above the floor, falling into the pad.
  Expected: `bhaptics: jump pad hook at RVA 0x13E5686` and `bhaptics: booster hook at RVA 0xDAF404` at the
  start; at about 6 s one `bhaptics: launch: jump pad 'trenches_trigger_bounce_pad_2' (launch speed 21.0,
  during a pad's flight 0)`, perhaps `same launch` lines; getviewpos at 7.5 s well above the pad.
- Negative control: two jumps, two double jumps and two dashes from the start (the script's right primary
  and secondary) from about 12 s: `landing:` lines as before, no `launch:`.
- A second pad visit at 34 s: a second `launch:` line, not `same launch`.
- The booster at 40 s (`capitol_trigger_boost_1`, 206.4 316.05 -24.35, in the air): `launch: booster
  'capitol_trigger_boost_1' (speed 75.0)` and getviewpos near its destination (223.7 316.1 -26.8), if that far
  part of the map is awake and the blast mechanic accepts (qconsole says so when it does not).
- The 30 s summary counts `launch` frames, 8 per launch; the mock Player prints
  `launch VestFront 4 dots max 85 70 ms key evr_launch_VestFront` first.

Closer pads for a second try: e1m2 `cp_19_final_battle` (start 91.65 -1076.86 -83.8) has
`extraction_trigger_bounce_pad_1` 11 m away (97.6 -1086.6, trigger z -82.5, floor about -83.8: `setviewpos
97.6 -1086.6 -80.8 0`); e1m1 `cp_10_post_uac_basement` has `uac_hq_trigger_bounce_pad_1` 18 m away.
