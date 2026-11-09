# Ghosting and smearing under motion: the four known issues against the flat game's render

Four known issues describe the same kind of fault, a trail or a soft copy behind something that moves on screen:
distant flying demons in Exultia, the gun in the weapon customize and mod screens, the displays in the Slayer's
office in the Fortress of Doom, and see-through surfaces while turning. This page reads what Route S keeps per eye
for the temporal passes (section 1), finds where each report came from (section 2) and measures each item on the
rig against the flat game's render at the same head motion (sections 3 and 4). RVAs are in Steam build 25216728.

Result: on the test PC none of the four is VR's own. In each, eye L and eye R match and both match the flat
game's render at the same head motion, with TAA and with DLSS. The flyers' trail is DLSS's (the playtest ran DLSS);
the gun's ghost did not show on the test PC at all; the displays' moving bars blur alike in VR and flat; the
glass's smear was reported while the eyes still shared the refraction history, which they no longer do. The one
temporal tool the VR render leaves off, TAA's anti-ghosting mask, changes nothing measurable here in either
render. Nothing was changed in the layer; the known issues are reworded to what was measured.

## 1. What is per eye, and what is not

Per eye under Route S, with per-eye TAA or DLSS on (the default; `stereo-temporal.md`, `stereo-object-motion.md`,
`stereo-moved-flag.md`, `stereo-geomcache-motion.md`):

- the TAA accumulation pair, the opaque accumulation and the previous frame's colour (`distortionLastFrameMap`),
  picked by each eye's own frame count; the exposure index; a DLSS feature for eye R;
- the camera's previous matrices, so camera motion (head or game turn) has each eye's own motion vectors;
- object motion: each render its own slot of the transform ring, the moved flag kept for eye R, eye R's previous
  model matrix, the geometry caches' slots;
- the refraction history (glass), SSDO, light scattering; SSR reads the per-eye TAA history.

Still shared, so held off in stereo (`stereo_seq::stereoTaaForcedCvars`): `r_TAAAntiGhosting` (TAA's anti-ghosting
mask pair `_taaGhostingMask0/1`, render system + 0x560 / + 0x568), depth of field's, water's and the ray-traced
reflections' temporal passes, dynamic resolution. Of these only the anti-ghosting mask takes part in TAA itself:
the AA pass's setup (0x1C9B900) binds `prevFrameGhostingMask` = mask[(b - 1) & 1] and writes mask[b & 1]
(0x1C9BB22 to 0x1C9BB93, b the backend frame stored at post-process + 0x148) when the TAA program is the
ghosting-fixup one, `r_TAAAntiGhosting` (0x66DFA40) is not 0 and the mask image exists; under Route S each eye
would read the other's mask. The DLSS branch does not read it. [code]

So the flat game's TAA has one tool against ghosting that the VR render does not use. Whether it matters for these
items is measured below (section 4.1).

## 2. Where the reports came from

- **Flyers, the gun, the office displays** (`387d5d65`, 2026-10-17): the owner's full playtest of 0.1.50 on his
  PC (2026-10-13), in the headset. His launch that evening (launcher log and layer log of session
  `20261013-201024`): DLSS per eye at Balanced with preset K (DLSS 310.9.1), 2496x2688 per eye, foveation
  Balanced, ray tracing off. So every one of these three was seen with DLSS, not TAA. The layer log also reads
  `seq-refract: off (the default) ... the refraction history is the engine's, shared between the eyes`: per-eye
  refraction was off by default from `0ca83a78` (2026-10-11) until `c6cacaa0` (2026-10-18). [log]
- His in-headset captures from Exultia (20:23:56 to 20:25:20, game position -54.79 301.06 13.96, facing about
  -14 degrees) show the flyers: small winged demons, 20 to 50 pixels, over the valley. Each flyer is at the same
  place in eye L and eye R once eye R is moved by the projections' offset (605 px at 2496 wide): position within
  0.5 px, area and darkness within a few percent (`flyers_lr.py` on the three pairs). Both eyes show the same
  faint streaks behind the wings; nothing about them is one eye's. [log, his captures]
- His capture at 21:54:50 in the Fortress (-0.11 40.55 -10.54) shows the office displays: two small screens on a
  pillar with animated red bars. No capture of the upgrade screen.
- **See-through surfaces** (`e873ba1c`, 2026-09-28): the seventh headset session (2026-09-27, release candidate
  `fc8ee49`): "stained-glass windows motion-blurred (translucent surfaces, early in the session)". The refraction
  history was then the engine's, shared between the eyes. [the session's notes]

## 3. Rig method

A second test PC (RTX 3080 Ti), OpenXR-Simulator, the Release layer of main `77bb3610` (the code of `0daefec5`); for the
flat render with DLSS or AA off, the same code plus a rig-only knob, not on any branch, that lets
`ETERNALVR_DEBUG_CVARS` hold `r_antialiasing` in mono too (without it the player's profile sets the game's own AA
mode after the command line). Foveation off, fixed exposure (`+r_hdrAutoExposureRatio 0`), both hands held out of
view. In-headset capture bursts of 8 consecutive frames (`ETERNALVR_CAPTURE_BURST=8`), up to 50 frames a session.

| Arm | What renders |
|---|---|
| VR (Route S) | the default: two renders per tick, each eye its own view, per-eye TAA |
| same view | `ETERNALVR_STEREO_SAME_VIEW=1`: two renders per tick, both on the game's own view and symmetric projection |
| flat (mono) | head-tracked mono: one render per tick, the flat game's render path and projection |
| DLSS | `ETERNALVR_STEREO_DLSS=1`, Balanced, preset K, DLSS 310.9.1 (the playtest's); in mono the same DLL and preset |
| AA off | VR: `ETERNALVR_STEREO_TAA=0` (the launcher's Anti-aliasing Off); mono: `r_antialiasing 0` |
| anti-ghosting | VR with `r_TAAAntiGhosting 1` (`ETERNALVR_DEBUG_CVARS`; each eye then reads the other's mask); mono with `+r_TAAAntiGhosting 0` (the game's default is 1) |

Motion: the simulated head sways 30 degrees of yaw with a 2.4 s period (`ETERNALVR_TEST_HEAD_SWAY=30,0,2.4`), up to
78 degrees a second, about 0.8 degrees a frame at 90 to 94 frames a second (mono and Route S both run at the
simulator's 90 Hz pace); each arm also runs with the head still. Scenes:

- Urdak (`e3m3_maykr`), the big window (`setviewpos -159.84 480.27 -150.34 8`, the refraction checks' spot), at
  1024x1104 per eye;
- Exultia (`e1m2_battle`), the playtest's spot facing the flyers (`setviewpos -54.79 301.06 13.96 -14`), at
  2048x2208;
- the Fortress of Doom (Continue from the 100% save), the office pillar with the displays
  (`setviewpos -0.11 40.55 -10.54 -126`, the displays ahead at the sway's centre), at 2048x2208;
- the weapon customize screen (Exultia, Dossier > Arsenal > Customize weapon by keys; the combat shotgun on the
  menu panel), at 2048x2208.

Measures (scripts in `tmp-vr/rs/taaghost/`):

- **Eye L against eye R** (`lrdef.py`): eye R moved by the projections' offset, each 16x16 block of eye L
  matched within a few pixels (parallax), blocks whose best match still differs by more than 14 counted. In the
  same-view arm both eyes render one view, so they are compared as they are (`lrsv.py`). A one-eye ghost shows
  here.
- **Frame to frame** (`tres.py --norm`): each frame warped onto the next by dense optical flow (DIS), the mean
  residual in the item's region over the region's mean gradient (R), binned by the image motion (px per frame at
  512 wide). A smear that is the same in every frame warps away; trails that fade, ghosts that jump, aliasing
  shimmer and noise stay. Arms are compared at the same image motion. With the head still the raw residual in
  Exultia is 0.40 to 0.47 (8-bit) in every arm that keeps a history, VR, same view and flat, TAA and DLSS: no arm
  adds noise of its own.
- **Flyers** (head still; `flytrail2.py`): each burst's per-pixel median is the sky behind the flyers; the flyers
  and anything they leave behind are what is darker than it. Crops of every flyer found are compared side by side.

## 4. Results

The frame-to-frame figure below is R, the residual over the region's mean gradient: the raw residual follows how
much detail is in view, which differs between arms that happen to look at different parts of the scene while
swaying. Bins are image motion in px per frame at 512 wide (about 0.2 degrees each).

### 4.1 The anti-ghosting mask

In the flat render, turning the mask off (`+r_TAAAntiGhosting 0`, mono) changed nothing measurable: Urdak's window
under the sway, R 0.32 / 0.31 with the mask (2 to 4 / 4 to 6 px per frame) and 0.32 / 0.32 to 0.34 without it
(`tghost-urdak-mono-a`, `-monoag0-a`, `-monoag0-b`); the Exultia flyers, head still, look the same with and without
it (`tghost-e1m2-monost-f`, `-monoag0st-f`). In VR, turning it on (each eye then reads the other eye's mask) changed
neither eye L against eye R nor R (`tghost-urdak-defag1-a`: blocks p50 0, p90 1; R 0.29 / 0.30 against 0.33 /
0.31). The mask the VR render goes without is not what these reports see. [rig]

### 4.2 See-through surfaces (Urdak's window)

| Run | Eye L vs eye R, blocks per frame p50 / p90 / max | R at 2-4 px/f, eye L / eye R (mono) | at 4-6 px/f |
|---|---|---|---|
| VR, TAA (`tghost-urdak-def-a`) | 0 / 1 / 2 | 0.33 / 0.36 | 0.31 / 0.34 |
| same view (`-sv-a`) | 0 / 0 / 0 (as they are; mean difference 0.86 in every pair, still or moving) | 0.33 / 0.33 | 0.33 / 0.32 |
| flat, mono (`-mono-a`) | | 0.32 | 0.31 |
| VR, DLSS (`-dlss-a`) | 1 / 3 / 3 | 0.36 / 0.38 | 0.34 / 0.37 |
| VR, AA off (`-aaoff-a`) | 2 / 4 / 6 | 0.40 / 0.44 | 0.39 / 0.41 |

Eye L against eye R over the upper 60% of the image (the floor is left out, where the eyes' parallax is more than
the block search covers); R over the band 10% to 40% from the top (the window and what is behind it). The glass
looks the same in both eyes and in the flat render at the same head motion (contact sheet
`tmp-vr/rs/taaghost/contact-urdak-glass.jpg`); R, which a trail or a jumping ghost would raise, is the flat render's
in both eyes within run-to-run spread, and AA off (no history at all) raises it with aliasing shimmer. [rig]

### 4.3 Distant flyers (Exultia)

- **Eye L against eye R:** head still, no block differs (`tghost-e1m2-defst-f`, `-dlssst-f`: p50 0, max 0); swaying,
  p50 1 to 2, max 4 (`-def-f`, `-dlss-f`), where near edges' parallax beats the search; same view, the two eyes
  differ by 0.46 to 0.51 on average and no block (`-svdlssst-g`, `-svdlss-g`). Nothing one-eyed. [rig]
- **The flyers, head still** (every flyer found over the still sky, cropped side by side): with TAA they are
  small dark shapes in VR and in the flat render alike; with DLSS (Balanced, preset K) they leave faint streaks
  behind the wings, in VR, in the same-view render and in the flat render (`-dlssst-f`, `-svdlssst-g`, `-h`, `-i`,
  `-monodlssst-f`, `-h`, `-i`; 40 to 140 flyer crops per render). The playtest's captures show the same streaks in
  both eyes. [rig, his captures]
- **Swaying:** R over the sky and the far city (2 to 4 / 4 to 6 px per frame): VR TAA 0.30 / 0.33 (eye L),
  0.30 / 0.32 (eye R), same view TAA 0.29 / 0.34, flat TAA 0.32 / 0.33 (the same in a second run); VR DLSS
  0.34 / 0.34, same view DLSS 0.32 / 0.32, flat DLSS 0.32 / 0.33. [rig]

The trail is DLSS's on small, fast, high-contrast objects, the same in the flat render; it is what the playtest saw
(DLSS that evening). TAA, the default, leaves them without it. [rig] How long a trail is follows how far the flyer
moves between two frames of its eye: the VR render ran at 75 to 83 ticks a second at 2048x2208 here against the
flat render's 90, and a flat game on a monitor usually runs faster still, so the same trail can look a little
longer in a headset than on a monitor. [inferred]

### 4.4 The gun in the weapon customize screen

Exultia, Dossier > Arsenal > Customize weapon (the same 3D model widget as the mod screen: the combat shotgun,
placed on the menu panel, `docs/VR_MENUS.md` "The menu's 3D model"), the head swaying. Over the black background a
ghost or trail would be light outside the gun's silhouette: H is the brightness in a ring 1.5% to 7% of the gun's
width outside it, over the gun's own, per mille, split into the half behind the gun's motion (Hb) and ahead (Ha);
motion in px per frame at 2048 wide (`gunhalo.py`).

| Run | gun width | H head still | H at 6-10 / 10-16 px/f | Hb / Ha at 10-16 px/f |
|---|---|---|---|---|
| VR TAA eye L / eye R (`tghost-e1m2-def-u`, `-defst-u`) | 300 px | 10.1 / 10.8 | 9.6 / 9.4 ; 10.3 / 9.8 | 4.9 / 4.6 ; 5.1 / 4.8 |
| same view TAA (`-sv-u`) | 278 px | | 9.3 / 9.4 | 4.7 / 4.7 |
| flat TAA (`-mono-u`, `-monost-u`) | 252 px | 9.9 | 9.6 / 9.5 | 4.7 / 4.8 |
| VR DLSS eye L / eye R (`-dlss-u`, `-dlssst-u`) | 328 px | 8.7 / 9.2 | 9.3 / 9.3 ; 9.5 / 9.5 | 4.6 / 4.7 ; 4.8 / 4.7 |
| flat DLSS (`-monodlss-u`, `-monodlssst-u`) | 263 px | 8.9 | 8.7 / 8.6 | 4.2 / 4.5 |
| VR AA off eye L / eye R (`-aaoff-u`) | 346 px | | 10.0 / 9.6 ; 10.2 / 10.0 | 4.8 / 4.8 ; 4.9 / 5.0 |

Eye L against eye R: p50 0, max 2 to 3 blocks (`-def-u`, `-dlss-u`); same view, a mean difference of 0.46 to 0.48 in
every pair and no block (`-sv-u`). The light around the gun is the same with the gun still and moving at any speed,
with no history at all (AA off) as with TAA or DLSS, in both eyes and in the flat render, behind as ahead; the crops
brightened three times show no second copy (`tmp-vr/rs/taaghost/contact-gun.jpg`). An earlier check (0.1.84, TAA and
AA off, 2026-10-26) found the same. The ghost seen in the headset did not come back on the test PC with TAA or with
the playtest's DLSS. [rig]

What the rig cannot show: the headset's own compositing. The gun is drawn into the eye images on the panel's
plane and the menu is a separate panel layer the runtime composites; anything that moves the eye images against
the panel while the head turns (reprojection, frame extrapolation such as ASW on a Quest when the frame rate
drops) would show as the gun swimming against the menu. Not measured. [inferred]

### 4.5 The office displays (Fortress of Doom)

The office pillar's two small screens with animated red bars (the playtest's capture at 21:54:50). With the
30 degree sway the screens were sometimes out of view: the room is anchored to the head's yaw when tracking starts,
which the sway makes a random offset of up to 30 degrees; these runs sway 15 degrees with a 1.2 s period (the same
78 degrees a second at most).

| Run | Eye L vs eye R, blocks p50 / p90 / max | R at 2-4 / 4-6 px/f, eye L ; eye R (mono) |
|---|---|---|
| VR TAA (`tghost-fortress-def-g`) | 7 / 10 / 11 | 0.28 / 0.28 ; 0.28 / 0.29 |
| flat TAA (`-mono-g`) | | 0.28 / 0.31 |
| VR DLSS (`-dlss-g`) | 7 / 9 / 9 | 0.26 / 0.30 ; 0.28 / 0.33 |
| flat DLSS (`-monodlss-g`) | | 0.30 / 0.33 |

The blocks that differ between the eyes are the same with the head still (`-defst-g`, `-dlssst-g`: p50 10, max 10
to 11): the desk's glossy top and the near pillar, whose reflections and parallax differ between two eye positions
in reality too. In the same view both eyes differ by 0.56 on average in every pair and no block (`-sv-g`). [rig]

The screens themselves, found in every burst frame by their image (`dispcrop.py`), look alike in eye L, eye R and
the flat render, with TAA and with DLSS: the animated red marks blur a little while they change, the same way in
all of them (contact sheet `tmp-vr/rs/taaghost/contact-fortress-displays.jpg`). [rig]

## 5. What this leaves

- Nothing here is VR's own on the test PC, so nothing was changed. The known issues say what was measured.
- The headset: the playtest's DLSS trails on flyers are DLSS's (section 4.3); the gun's ghost and the office
  displays need a headset look with TAA and with DLSS, and for the gun with the runtime's frame extrapolation
  off, before anything else is tried. A player report with the headset, the anti-aliasing setting and the place
  would reopen any of them.
- The anti-ghosting mask stays off in stereo. Giving eye R its own mask pair (two more images like the
  refraction's, a resize follow, a hook at the selection 0x1C9BB22) is possible, but on these measurements it would
  change nothing a player sees (section 4.1).
- Not tried: stained glass itself (Urdak's window is the same refraction pass; the session-7 windows' place was not
  recorded), head turns faster than the sway's 78 degrees a second, render sizes above 2048x2208, foveation on.
- The rig's arms that look at different parts of a scene under the sway give different raw residuals; only the
  content-normalised figure (R) and same-frame eye L against eye R are comparable between arms.
