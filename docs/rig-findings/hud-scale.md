# The game's HUD Scale in VR, and the hand HUDs' corner blocks

Steam build 25216728 (RVAs below), Game Pass 1.0.56.0 checked offline. Labels: `[static]` read from the exe,
`[rig]` measured on the test PC (OpenXR-Simulator, Route S), `[inferred]` a reading of the two.

## The setting

- The game's Settings, HUD Scale (`#str_code_settings_label_small`, `_medium`, `_large`, built at 0x15DB340)
  is the cvar `swf_anchorProfile` ("Anchor profile to use", registered at 0x22C0E0 with flags 0x2, default 0):
  0 Small, 1 Medium, 2 Large. The menu's change sets it and calls 0x182A7B0, which lays out every active GUI
  again [static]. Each GUI also compares the cvar with the profile it was laid out for on its update
  (0x1827540, `lastAnchorProfile` at +0xC10) and lays out again when they differ (0x1831830), so a value
  written at run time takes effect at the next GUI update [static; rig: a run holding 2 drew the Large HUD].
- It is stored only in `profile.bin` (no archive flag: not in `DOOMEternalConfig.local` or the `.cfg`): the
  profile's cvar serializer (0x141D040, the one the profile hooks detour) writes it through its generic cvar
  entry (0x14111B0) at +0x1A4F, just before `view_skipShakes`, and after a load calls 0x182A7B0 [static]. The
  Game Pass serializer (0x14207B0) has the same entry, and the cvar's registration is there too [static, on
  an image dump].
- Every profile save runs on a thread of the game's own while the game goes on drawing (rig logs: the load on
  the main thread, the saves on another) [rig].

## Legibility (docs/test-protocols/ui-legibility.md)

On the HUD quad at its default size (2.0 m wide at 1.5 m; 0.0373 degrees per pixel of a 2048-wide band), the
rig's 0.1.75 GUI captures in e1m3 at the checkpoint before the gate: at Medium (the rig profile's 1) the
mission challenge names measure 12 px (0.45 degrees), the challenge hint 10 px (0.37), the demonic corruption
title 12 px (0.45) and the equipment labels 13 px (0.49); at Large (held at 2) 14 px (0.52), 13 px (0.49),
13 px (0.49) and 15 px (0.56) [rig]. So VR holds Large (`stereo_seq::stereoHudScaleCvar`); the two that stay a
pixel short go to Part 2 of the protocol (whether half a degree is the right bar is a headset question).

## Keeping it out of the profile

`swf_anchorProfile` is in `mp_policy::profileStoredCvars`, so the profile hooks treat it as they treat
`view_skipShakes` (docs/rig-findings/profile-leak.md): the profile's load sets the player's value, the hold
writes 2 at the next present, and every save writes the load's value with the held one back after it. Before
this, a rig experiment holding it through `ETERNALVR_DEBUG_CVARS` saved 2 into the rig's profile (the QA
suite's profile check failed on it) [rig].

## The corner blocks at each HUD Scale

Rig GUI captures, band coordinates of what was drawn (alpha 16 or more), measured by the readback's grid
(`ui_layer/hud_blocks.hpp`, before its margin):

| HUD Scale | Health, armor (and extra lives) | Ammo, equipment, key hints |
| --- | --- | --- |
| Small (1280x720, no extra lives row) | x 0.038..0.175, y 0.867..0.944 | x 0.809..0.934, y 0.844..0.939 |
| Medium (2048x2208, e1m3, with the extra lives row) | x 0.029..0.188, y 0.780..0.947 | x 0.793..0.936, y 0.829..0.940 |
| Large (2048x2208, e1m3, with it) | x 0.037..0.225, y 0.749..0.944 | x 0.752..0.922, y 0.805..0.937 |

The ability rings around the crosshair are the same at every scale (x 0.414..0.582, y 0.461..0.560) [rig].
The fixed cuts up to 0.1.75 (x 0..0.215 and 0.765..1 from y 0.76) held Small and Medium; at Large three shapes
of the health block (1662 pixels) and one of the ammo block (1231 pixels) of a 2048x2208 capture crossed
them, on the head-locked HUD [rig, the QA check's control below].

## Rig results (2026-10-22, the hud-large branch, Release)

OpenXR-Simulator, Route S unless named; the rig profile at Medium (`swf_anchorProfile 1`).

- The hold: `cvars: swf_anchorProfile 1 -> 2 (reads 2); held in VR` at the first present, before the
  profile's load; the hooks then `before the profile's load swf_anchorProfile 2 -> 1`, `the game loaded the
  profile: ... swf_anchorProfile 1`, the hold `1 -> 2` again; every save `the player's values went in:
  swf_anchorProfile 1 (held 2; the profile's load)`. Parallel Eye Rendering the same [rig].
- profile.bin after each of 10 QA runs (wrist, weapon, panel, Large and Medium, Parallel Eye Rendering, the
  wheel, the guard's test trip), decrypted from the suite's `config-after` copy: `swf_anchorProfile 1`, as
  before the run; the suite's profile check passed in every run [rig]. Its positive control: the wheel-bounds
  experiment of 2026-10-21, which held 2 through `ETERNALVR_DEBUG_CVARS` without the cvar in the hooks' list,
  failed it with `swf_anchorProfile '1' -> '2'` [rig].
- The multiplayer guard's test trip: `cvar restore: swf_anchorProfile 2 -> 1, the player's value from the
  profile's load` [rig].
- Each save runs on a thread other than the game's main one, and the player's values are live for the whole
  serializer call: 1.22 to 1.51 ms in 12 saves [rig]. A save comes after the profile's load (during the map's
  loading on the rig) and at in-play events such as a pickup or a checkpoint (`SaveCheckPointAndGetFiles` is
  followed by a profile save in `qconsole.log`). A GUI updated inside that window would be laid out at the
  player's scale for a frame and then at Large again [inferred]. Measured with a rig-only build that logged
  every GUI layout on an anchor change (the call at 0x183183C in the GUI's update) and stretched the window to
  about 15 ms: a change written from the console was laid out by the GUIs within the same 16 ms log tick (on
  seven threads at once), and the map-start layouts (screen size) read 2; in the one in-play save of that run
  (a token pickup) the stretched window saw no layout at all [rig]. No pickup or checkpoint command repeats an
  in-play save on the rig (`activateCheckPoint` refuses the map's start checkpoint), so the sample is that one
  save: not enough to rule a rare one-frame layout out. Swapping the value only around the cvar's own entry
  (the serializer's generic helper, 0x14111B0) would shrink the window to that call.
- The player's own choice: with `swf_anchorProfile 0` written from the console during play (as the game's
  menu writes it: a value the layer did not write; the menu also lays the GUIs out, which the console write
  leaves to the GUIs' own update, and the layer sees only the value either way), the log showed `swf_anchorProfile
  0, not written by the layer (held 2; seen at a present): the player's choice in the game's menu, no longer
  held` and `profile: swf_anchorProfile 0 is the player's choice`; the GUIs laid out at 0 and nothing wrote 2
  again; the next in-play save (a token pickup) put 0 into profile.bin (decrypted from the run's `config-after`)
  [rig].
- Large in the GUI captures: the health block's cut ends at 0.234 of the band's width at 1280x720 and at
  2048x2208 (the block as drawn at 0.225), past the 0.215 where the fixed cut ended; with the hold set to 1
  (Medium) through `ETERNALVR_DEBUG_CVARS` the cut ends at 0.197 [rig].
- The cuts: 714 GUI captures in play at 2048x2208, Large, wrist mode (3 minutes in e1m3): no drawn shape (alpha
  16 or more, 8-connected) across a cut's edge, and no drawn pixel in a cut outside the measured block (sampled
  every 7th capture); origin/main's fixed cuts on the same captures: 1428 shapes across their edges, 2.07
  million pixels outside them. QA `hand-hud-large` at 1280x720 (5 captures each): wrist and weapon with health
  and armor at Large, wrist at Medium, Parallel Eye Rendering at Large: none across [rig].
- The readback's cost (2048x2208, wrist mode, 3 minutes): 60 readings every 1800 GUI copies; CPU 270 to 290 us a
  reading on average (65 to 80 reading the grid), 340 to 490 us at most; 9 to 10 us a GUI copy on average; GPU
  copy 104 to 107 us. The first readings of a session, of the map start's fade, took up to 1.1 ms labelling a
  whole corner before a corner more than half drawn was left unlabelled; with it, 112 to 214 us [rig]. Panel
  mode: no reading at all (no `ui: corner blocks:` line in two panel runs) [rig].
- The wheel at Large: `weapon wheel straight ahead ... GUI rect (759, 116) 456 x 452`, the drawn wheel 99.9%
  inside it, in panel and wrist mode; with the wrist cuts and the wheel the head-locked HUD was 6 pieces [rig].

Headset only: whether Large reads better at the player's own HUD width and distance, and how the wrist and
gun panels look with the blocks' new crops (the quads show what was cut, now the block where it was drawn).
