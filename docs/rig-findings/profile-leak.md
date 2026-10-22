# Held cvars in the player's profile (profile.bin)

Steam build 25216728 (RVAs below), Game Pass 1.0.56.0 checked offline. Labels: `[static]` read from the exe,
`[rig]` measured on the test PC (OpenXR-Simulator, Route S, e1m2), `[inferred]` a reading of the two.

## What the game saves

The profile is `userdata\<account>\782330\remote\PROFILE\profile.bin` (Steam Cloud). The file is AES-128-GCM
(the save files' scheme: key = SHA-256("<SteamID64>MANCUBUS" + "profile.bin")[0..16), the same text as the
associated data, 12-byte nonce first, 16-byte tag last). The plain text starts with `a9 0d 8d aa`, version 2
(big-endian) and a FarmHash32 (farmhashmk Hash32, RVA 0x1DDAB90) of everything after byte 12, big-endian at
byte 8 (the save, RVA 0x14978E0) [static; rig: a file edited and re-hashed this way loads without the game's
`Profile corrupt` line, `qconsole.log` prints the time since its last save]. Cvar
entries are `<2 x name length><name> 0a <2 x value length><value text>`.

`idPlayerProfileShell::InternalSerialize` (0x1417FF0) calls the cvar serializer at 0x141D040 once (0x1418AA0),
for the load and for the save alike, switched on the first byte of its second argument (0: load) [static].
A save writes each listed cvar's live value text unless its flags hold 0x30000 (archived to `.cfg` or `.local`);
a load sets each listed cvar from the profile (`SetString`, forced, no flag check) [static]. Not archived, so
saved from the live value: `view_skipShakes` (flags 0x1, registered at "0"), `r_sharpening`, `r_antialiasing`
and `r_raytracedReflectionsTemporalUpscaleQuality` [static]. The load sets the last two again from the
profile's own fields (the DLSS index, the Reflections level), so a held value saved there does not survive
[static]. The launcher's restore covers the text configs only; save backups skip `PROFILE` [code].

## The leak (stage 1, 2026-10-12, dev main 91b5e55f + hands-rig)

- Profile edited to `view_skipShakes 0` (`r_sharpening 2`, `r_antialiasing 1`), installed and taken by Steam
  (the rig's resync launch). One Route S session in e1m2 for 40 s, quit through the window close.
- The game wrote profile.bin about 22 s after the launch, right after loading it (`qconsole.log`: a second
  `idPlayerProfileShell::InternalSerialize` after `loading profile for user handle`), and not again at the quit:
  `view_skipShakes 1`, `r_antialiasing 0` [rig]. The layer log: `cvars: view_skipShakes 0 -> 1 (reads 1); held
  in VR`.
- So every stereo session since the comfort hold (2026-09-27, 67b5962c) left `view_skipShakes 1` in the
  player's profile, and the flat game loads it: no camera shake until the player changes it [rig for the value;
  the shake itself not looked at]. The launcher's Sharpening (Off/Low/Medium/High, not Game) left its level the
  same way [static, code].

## The fix (vkcore/profile_hooks.hpp)

A detour of 0x141D040 (signature once in `.text` on both builds; view_skipShakes' archive check found inside
it, +0x1A8F on both) for `mp_policy::profileStoredCvars` (view_skipShakes, r_sharpening, and since the HUD
Scale hold `swf_anchorProfile`: docs/rig-findings/hud-scale.md):

- load: a cvar the layer wrote first gets its value from before the layer's first write, so the load sets the
  profile's value, or leaves the game's own when the profile has no key. The value after the load is the
  player's; the hold writes its value again on the next present.
- save: each held cvar gets the player's value for the write and the held value back after it, through the
  cvar book (the multiplayer guard decides).
- locking: the swaps are made under `runtime_cvars::heldMutex`, which is released while the game's serializer
  runs; a profile I/O flag (`runtime_cvars::setProfileIo`) keeps `apply()` off the profile cvars until the hook
  takes the lock again to finish. No game code runs under the layer's lock, and no present waits on a profile
  load or save.
- a trip gives the profile cvars the load's value back (`cvar_book::setTripValue`), not the value before the
  layer's first write, which is the registered default when the layer wrote before the load.
- a change the player makes to one of these settings in the game's menu during VR is not saved: the hold
  overrides it live and the save writes the load's value.
- r_antialiasing and the reflections upscale cvar are left out: the load overwrites them, and swapping the AA
  mode for a save would switch the renderer's anti-aliasing for that moment.

Rig proof (2026-10-12, branch profile-leak, the stage 1 sequence again):

- Profile `view_skipShakes 0`, `r_sharpening 2`, with the launcher's Sharpening at High
  (`ETERNALVR_SHARPENING=3`). Layer log: `the game loaded the profile: view_skipShakes 0, r_sharpening 2` at
  13.1 s, `view_skipShakes 0 -> 1 (reads 1); held in VR` and `r_sharpening 2.000 -> 3` at 17.1 s, `the game
  saved the profile: the player's values went in: view_skipShakes 0 (held 1; the profile's load), r_sharpening 2
  (held 3; the profile's load)` at 21.5 s. profile.bin after the quit: `view_skipShakes 0`, `r_sharpening 2`
  [rig].
- Profile `view_skipShakes 1` (the rig's own, left by earlier sessions): `the game loaded the profile:
  view_skipShakes 1` (the cvar registers at 0, so the load sets the profile's value, as it does in the flat
  game), nothing for the hold to write, profile.bin still 1 after the quit [rig].
- In both runs the profile loaded before the hold's first write; the other order (the seed before the load) is
  covered by the unit tests only.

Parallel Eye Rendering (2026-10-12, `ETERNALVR_PARALLEL_EYES=1`, profile 0): there the hold writes before the
profile's load. Layer log: `cvars: view_skipShakes 0 -> 1` at 11.1 s, `before the profile's load view_skipShakes
1 -> 0, its value before the layer's first write` at 12.3 s, `the game loaded the profile: view_skipShakes 0` at
12.5 s, the hold's `0 -> 1` again at 12.5 s, `the game saved the profile: the player's values went in:
view_skipShakes 0 (held 1; the profile's load)` at 20.9 s; no hang; profile.bin after the quit 0 [rig].

Mono (2026-10-12, no `ETERNALVR_MODE`, `+view_skipShakes 1` on the command line as the launcher's mono launch
has it, profile 0): the game saved the profile after its load with 0 [rig]. The profile's load comes after the
command line, so in mono the profile's value wins: the command-line 1 neither leaks nor takes effect. The hooks
are not installed in mono.

## Players who already played: the one-time repair

Every stereo session before the hooks left `view_skipShakes 1` in the profile, and the hooks cannot tell that 1
from a player's own choice. The game's Settings, Game page has a Screen Shake entry
(`#str_code_settings_game_screenshake_title`, built at 0x15B6CB0; English text not decoded): its On/Off widget is
filled from `view_skipShakes` inverted (0x15B8E43) [static]. So the launcher asks the layer once:

- the launcher sets `ETERNALVR_PROFILE_REPAIR=1` (stereo launches only; a mono launch reads no log) while its
  marker `profile-repair.txt` is absent and a kept session's layer log (`logs\<session>\eternalvr-*.log`, the
  newest 20 sessions) has `cvars: held at run time: ... view_skipShakes 1` and no `profile cvar serializer hooked`
  line (a session with the hooks in left nothing to repair): a fresh install or a mono-only player is never asked
  for. A logs folder that cannot be listed means no repair; the launch goes on; a session folder that cannot be
  listed is skipped and the others are still read;
- the layer, at the session's first profile load: a stored 1 becomes 0 (logged); the status file gets
  `profile_repair=done` once the next save wrote it, `not_needed` when the profile held 0, `off` without the
  request;
- the launcher writes the marker once it reads any of the three, so a session that ends before the profile load
  asks again. A player whose stereo sessions are all older than the 20 kept sessions is not repaired. The marker
  (`%LOCALAPPDATA%\EternalVR`) is per PC, not per profile: a second Steam account, or Steam and Game Pass on one
  PC, is not repaired once the first settles.
- a multiplayer guard trip during that load skips the repair and reports nothing, so the launcher asks again.
- r_sharpening is not repaired (its value before VR is not known; the game's menu shows it).

Rig proof (2026-10-12): profile 1 with the request: `one-time repair (ETERNALVR_PROFILE_REPAIR=1): the profile's
view_skipShakes 1 ... set to 0` at the load, `one-time repair saved` at the save, `profile_repair=done`, profile.bin
0 after the quit. Then profile 1 without the request (the marker set): `the game loaded the profile:
view_skipShakes 1`, `profile_repair=off`, profile.bin still 1 [rig]. The launcher's side (the ask, the marker) is
covered by its unit tests, not by a launcher run.
