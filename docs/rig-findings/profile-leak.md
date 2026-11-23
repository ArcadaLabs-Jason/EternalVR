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

## The leak (stage 1, 2026-10-12, main 91b5e55f + hands-rig)

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
command line, so in mono the profile's value wins: the command-line 1 neither leaks nor takes effect. (2026-11-21:
the command-line 1 does not take even before the load: the first present reads 0. Head-tracked mono now holds
view_skipShakes 1 at run time and the hooks are installed there too: "Mono" below.)

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

## Photo mode and the game settings fields (2026-11-21)

From 0.1.99 the comfort set held `g_setting_photomode 0` (the player's Photo mode setting) and the hooks above kept
the player's value in its cvar entry. That was not enough: the profile also has 16 game settings fields
(`GAME_FIELD_*`, enum table at 0x4075F90) behind 16 cvars, and the game copies those cvars' live values into the
fields before every profile save (`idGameSettings::SetFieldsFromCVars`, 0x6ABA90, logged just before
`idPlayerProfile::Serialize - START`) and sets the cvars from the fields after every load, after the cvar entries
(`SetCVarsFromFields`, 0x6AB320) [static]. Neither runs inside the hooked serializer. The 16:
`g_weaponAutoSwitchOnEmpty`, `g_weaponAutoEquipNew`, `g_weaponDoomClassicPose`, `g_reticleMode`,
`g_setting_compass`, `g_setting_hud_show`, `g_setting_interact_prompt`, `g_setting_gk_highlight`,
`g_setting_boss_health`, `g_setting_objectiveMarkers`, `g_setting_hudNotifications`, `g_setting_combatScoring`,
`g_setting_tutorials`, `g_setting_aim_assist`, `g_setting_photomode`, `g_settings_dashStyle`
(`mp_policy::profileFieldCvars`). Of the cvars the layer holds, only `g_setting_photomode` was among them.

Rig, real launcher sessions on the Steam build (the launcher exe with `--launch`, stereo, e1m2, a map load from the debug schedule
for a save inside VR, the game quit through its window), on the 0.1.101 hold (layer `b1` of branch `photo-mono`):

- the player's Photo mode set on first (a session with the stereo set off: `g_setting_photomode 1`, then a map
  load): profile.bin `g_setting_photomode 1`, `GAME_FIELD_PHOTO_MODE` 1;
- one ordinary stereo session: `cvars: g_setting_photomode 1 -> 0 (reads 0); held in VR`, two saves with `the
  player's values went in: ... g_setting_photomode 1 (held 0; the profile's load)`; profile.bin after it:
  `g_setting_photomode 1` but `GAME_FIELD_PHOTO_MODE` 0;
- the next session with nothing held (the stereo set off): the layer's `the game loaded the profile: ...
  g_setting_photomode 1` (the cvar entry), then `Cvar 'g_setting_photomode' was modified to '0'` after
  `SetCVarsFromFields`, the console's own `"g_setting_photomode" is:"0"`, and the save wrote 0 in both places.
  So one VR session left the player's Photo mode off in the flat game [rig].

The fix holds `g_allowPhotoMode 0` instead. The game starts photo mode only with `g_allowPhotoMode` and
`g_setting_photomode` both on (0x143DFB0, `g_allowPhotoMode` tested first), each map load sets `g_allowPhotoMode`
itself (0x6D61EF, from a state the load reads; by players' guides photo mode opens only on a mission played
again), and nothing saves it: no config key, no profile entry, no field [static]. Game Pass 1.0.56.0 has the same
code (the check at 0x1441720, 147 instructions, and the map load's write at 0x6E1EA1 compare equal with relative
addresses masked) [static]; no session of this sequence ran on Game Pass. The same sequence on the Steam build
with the fix (layer `b2`): Photo mode set on (profile 1 and 1); a stereo session with the setting left alone
(`"g_setting_photomode" is:"1"`, `"g_allowPhotoMode" is:"0"` in VR): profile 1 and 1, and the next session with
nothing held read 1. The same in head-tracked mono (the launcher's Mono: `cvars: head-tracked mono: only the
window's mode and size (the render size) and g_allowPhotoMode 0 held`): 1 and 1, read 1 after. A stereo session
ended by killing the game process 80 s in (the launcher: exit code -1, settings restore complete): 1 and 1, read 1
after. Then a flat start without the layer or the launcher: `Cvar 'g_setting_photomode' was modified to '1'` after
its load, and the profile still 1 and 1 after its saves [rig].

## Mono: the comfort set (2026-11-21)

Until now head-tracked mono (the launcher's Mono) held only the window set and `g_allowPhotoMode 0`; the rest of the
comfort set came from the launcher's command line (`launcher/data/forced-cvars.txt`, the `| mono` lines), and the
profile hooks were not installed. Rig, real launcher sessions on the Steam build (the launcher exe with `--launch
--test-exe`, Mono, render size auto, the simulator, e1m2), the player's flat settings on the uncomfortable side:
Screen Shake on (profile.bin `view_skipShakes 0`, saved by a mono session that wrote only that cvar, then a map
load) and `DOOMEternalConfig.local` with `r_motionblur "2"`, `r_dof "1"`, `r_chromaticAberration "1"`,
`r_hdrDisplay "1"`. Each comfort cvar was read at every present (`ETERNALVR_DEBUG_CVARS=<name>=?`, which logs the
value at the first present and each change after it). In the session: a map reload from the debug schedule, a
checkpoint load from the pause menu (gamepad mode, the virtual pad: Load Checkpoint, then the notice's accept; a
map load followed), and the Settings menu opened and closed (its Game, UI, Controls and, in a second session, Video
tabs; shots of each) [rig].

| Cvar | Held value | Mono before, at the first present (about 9 s) | Then | Where mono's value came from |
|---|---|---|---|---|
| `r_hdrDisplay` | 0 | 0 | no change | the command line (config 1) |
| `r_motionblur` | 0 | 0 | no change | the command line (config 2) |
| `r_dof` | 0 | 0 | no change | the command line (config 1; the profile's own `r_dof` entry is 0) |
| `r_chromaticAberration` | 0 | 1 | 0 at the profile's load (11.6 s) | the game: `Unknown command 'r_chromaticAberration'` at the command line (not registered yet), then the game's settings at the load (`Cvar 'r_chromaticAberration' was modified to '0'`); config 1 not taken |
| `r_vignette` | 0 | 1 | no change | the game |
| `pm_noBob` | 1 | 0 | no change | the game |
| `view_skipKicks` | 1 | 1 | no change | the game (its default is 1) |
| `view_skipShakes` | 1 | 0 | no change | the game, then the profile's 0 |
| `hands_fovScale` | 1 | 0 (0.000) | no change | the game |
| `meatHook_playerViewOverrideMode` | 1 | 0 | no change | the game |
| `view_skipDamageEffect` | 1 | 0 | no change | the game |
| `view_showPlayerDamageViewEffect` | 0 | 1 | no change | the game |
| `view_damageBlur` | 0 | 1 | no change | the game |
| `g_skipViewEffects` | 1 | 0 | no change | the game |
| `r_blurRadialScale` | 0 | 0 | 1 at the profile's load | the game writes 1 after `r_motionblur` 0 (`was modified to '1' local`) |
| `g_allowPhotoMode` | 0 | 0 | held | the hold |
| `r_waterPostProcess` | not held in mono | 1 | no change | the game (the underwater warp stays in mono) |

So nothing flipped back mid-session: the map reload, the checkpoint load and the Settings visits changed none of
them. The command line held only `r_hdrDisplay`, `r_motionblur` and `r_dof`; the camera shake, view bob, damage tint
and blur, the view effects' overlays (the low health vignette, double vision), the vignette, the dash's zoom blur,
the weapon's FOV scale and the Meathook's single view turn were the game's or the player's all session, the same
values a Route S session's hold writes over at its first present (`pm_noBob 0 -> 1`, `view_damageBlur 1 -> 0` and
the rest). Why the command line does not take for them is not known (no console line for them; only
`r_chromaticAberration` reports itself as unknown). Three sessions agree (each with a Settings visit, the third
with the Video tab; the first with the map reload and the checkpoint load). The cinema screen
(`ETERNALVR_MODE=cinema` through the launcher) read the same values.

The fix: head-tracked mono holds the comfort set but `r_waterPostProcess` at run time from its first present, at
the stereo hold's values (`stereo_seq::monoVrHeldCvars`; the launcher's `| mono` lines stay and must name the same
values, `profile_cvars_tests.cpp`), and the profile hooks are installed in head-tracked mono too, since the hold
writes `view_skipShakes`. No field-backed cvar is held (the same test as above). The cinema screen and flat play
are unchanged: the cinema screen holds only its window set (`cvars: cinema mono: only the window's mode and size
(the render size) held`, no hooks) and flat play does not load the layer. Rig, the same setup on layers `b2` and
`b3` of branch `mono-comfort`:

- `cvars: head-tracked mono: only the window's mode and size (the render size) and the comfort set but the
  underwater warp (16 cvars) held`, `profile cvar serializer hooked`, then at the first present (8.6 s)
  `r_chromaticAberration 1 -> 0`, `r_vignette 1 -> 0`, `pm_noBob 0 -> 1`, `view_skipShakes 0 -> 1`, `hands_fovScale
  0.000 -> 1`, `meatHook_playerViewOverrideMode 0 -> 1`, `view_skipDamageEffect 0 -> 1`,
  `view_showPlayerDamageViewEffect 1 -> 0`, `view_damageBlur 1 -> 0`, `g_skipViewEffects 0 -> 1`; at the profile's
  load `before the profile's load view_skipShakes 1 -> 0`, `the game loaded the profile: view_skipShakes 0`, then
  `view_skipShakes 0 -> 1` and `r_blurRadialScale 1.000 -> 0`. Every profile save after it (the map loads, the
  checkpoint load): `the player's values went in: view_skipShakes 0 (held 1; the profile's load)`. The readings stayed
  at the held values to the end, through the map reload, the checkpoint load and the Settings visit with the Video
  tab, with no write after the profile's load;
- profile.bin after the session `view_skipShakes 0` and `DOOMEternalConfig.local` as before it (the launcher's
  restore: `r_motionblur "0" -> "2"`, `r_dof "0" -> "1"`, `r_blurRadialScale "0" -> "1"` and the rest), after a
  session whose game was killed (exit code -1) and after one ended by a window close (exit code 0) [rig];
- the QA suite's `comfort-held-mono`, `photo-mode-held-mono`, `mono-size` and `photo-mode-held` pass on `b3`, each
  with the suite's `profile.bin: no held value saved` [rig].

Not checked: Game Pass (not run in mono; the hooks are the stereo ones), a headset,
and the player turning one of these settings on in the game's menu during a mono session (the hold writes it back,
as in stereo).
