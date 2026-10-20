# Wet floors darker per eye under Route S: Umbra culling decals with the eye's narrow inner side

A player reported that at the start of The Ancient Gods part two's second mission (`game/dlc2/e5m2_earth`)
the puddles on the balcony look different in each eye. This page records how it was isolated on the test PC
(RTX 3080 Ti, OpenXR-Simulator with the Quest 3 profile, 2048 x 2208 per eye, Steam build 25216728) and the
fix. RVAs are in that build.

## 1. Measurement

Scene: `setviewpos 381 -886 -130.3 140` at the level start, head still. Each eye's capture is resampled to the
flat game's pixel directions (each eye's own FOV), four frames averaged, mean luminance in fixed boxes: a large
puddle, the wet concrete strip beside it, and two opaque controls (dry concrete in sunlight, the wooden deck).
The flat reference is same-view stereo (`ETERNALVR_STEREO_SAME_VIEW=1`), which equals a mono run.

The puddles are glass-like wet patches whose sheen comes from the environment probes (`r_environmentProbes 0`
drops the puddle from 74 to 39). Screen-space and ray-traced reflections, water decals, SSDO and every temporal
history were ruled out first (no change with each turned off).

## 2. Isolation

Head-centre lab arms (both eyes at the head's position, only the projection changed; lab builds, never
merged), puddle / wet strip, eye L / eye R:

| Projection | Puddle | Strip |
|---|---|---|
| Flat reference (54 x 54.27 degrees, symmetric) | 74.1 / 74.2 | 82.9 / 82.9 |
| Each eye's own (54 out, 40 in, 43.98 up, 54.27 down) | 64.9 / 64.8 | 65.5 / 62.8 |
| Vertical asymmetry only | 73.2 / 73.2 | 81.8 / 81.8 |
| Horizontal asymmetry only | 65.3 / 65.2 | 66.3 / 63.0 |
| The eye's horizontal width, centred (47.9 / 47.9) | 64.8 / 64.8 | 65.3 / 65.4 |

The darkening follows the frustum's horizontal extent, not its off-centre terms. `r_useUmbraCulling 0` in
stereo brings the puddle back (74.0 / 71.3); in the flat game Umbra changes nothing here (73.7). The light
gather's frustum second opinion (`docs/rig-findings/umbra-lights.md`) does not rescue it.

## 3. Which objects: decals

The Umbra query finish (RVA 0x1D1A5A0, finish(context, world)) waits for the query's jobs and sorts the
visible objects by type, the top three bits of each object's user id (`umbraObjectType_t`: GATE, LIGHT,
DECAL, STATICINSTANCE, BRUSHGEOMETRY, WORLDAREA), into the context's result sets: lights into a hash set
(+0x480, read by the light gather's gate A), decals into a bit set (+0x4B8, 0x7800 bits, one per decal
record), static instances and brush geometry into one bit set (+0x13B8, read by the static model gather at RVA
0x1C763A0, which tests no frustum of its own), world areas into a bit set (+0x33B8). Both Route S eyes share one
context.

A lab build ran a second query per eye with a frustum centred on the eye's axis at the flat game's width (54 x
54.27 degrees) and copied one result set at a time from it into the eye's own (puddle / strip, eye L / eye R;
flat reference 74.0 / 73.9, 82.7 / 82.7):

| Taken from the flat-width query | Puddle | Strip |
|---|---|---|
| Nothing (the eye's own query) | 65.0 / 63.1 | 65.4 / 62.7 |
| Lights (130 against the eye's 128) | 65.4 / 63.4 | 65.7 / 63.0 |
| World areas | 65.0 / 63.1 | 65.4 / 62.7 |
| Static instances and brush geometry | 65.0 / 62.9 | 65.4 / 62.7 |
| Decals | 73.6 / 70.9 | 81.4 / 78.7 |
| All four | 74.0 / 71.2 | 81.8 / 78.9 |

The wet patches are decals. With the eye's narrow frustum Umbra reports some of them hidden although they
cover floor in the eye's picture.

Culling the whole query at the flat game's width (the first attempt) fixes the puddles too, but each eye then
draws the geometry that width holds: at this view 9,320 draws per eye frame against 7,380, ticks -5.1% and GPU
+4.9%; taking out again the static instances whose box lies outside the eye's own frustum still left 8,650
draws (most of the extra geometry overlaps the eye's frustum and is hidden there only by occlusion). A second,
wider query per eye run inside the finish cost ticks -5.5% / -2.1% (the two scenes of section 5) and did not
keep the geometry at the eye's own: the gathers run off the first finish.

## 4. Fix

`src/vkcore/decal_frustum_hooks.cpp` detours the decal gather (RVA 0x1C77C90, a job; found by signature, with
its Umbra-off block checked as the witness of the decal record layout). Umbra's per-eye query is the engine's,
so geometry, lights and world areas are exactly as without the fix. Before the gather lists the decals Umbra
reported, every active decal record (0x50 bytes at [[viewContext + 0x28] + 0x10]: active when the word at
+0x38 and the dword at +0x3C are not 0, as the engine's Umbra-off path tests) whose bounds (+0x20, mins and
maxs) reach the frustum around the eye pair gets its visible bit too. That frustum is centred on the eye's axis
with, per axis, the larger of the eye's wider side and the flat game's half-angle (the view's latched fov_x /
fov_y, the head-tracked mono view: 54 x 54.27 degrees for a Quest 3; `stereo_seq::pairDecalProjection`), from
the eye's latched origin and axis. The test is the box against the four side planes, without occlusion: a
decal shows only on surfaces in view, so one behind a wall adds no pixels. A view already that wide and
centred (mono, same-view stereo) keeps the engine's list (`seq-decals:` counts it). Only a view the layer wrote an eye
pose into gets the decals around the pair: the layer notes each eye view's idRenderView and the projection it
wrote (`noteEyeViewProjection`), and a gather whose view is not one of them, or whose latched projection is not
that one, keeps the engine's list (menus, cinematics, zoom, probe views; counted as `not an eye view`).

Who reads the decal bits. In .text, the Umbra context's decal bit set (+0x4B8) is written by the query setup
(cleared, RVA 0x1D191C7) and the finish (RVA 0x1D1A953) and read only by this gather (RVA 0x1C77CC1); the other
`+0x4B8` operands near the renderer belong to other structures (spot-checked; static reading). Once listed, the
decals go through the light and decal binning, which bins each eye with its own projection
(`docs/rig-findings/stereo-bin-tiles.md`), so a decal outside an eye's frustum lands in none of its tiles. The
gather runs after the finish that fills the bits and before the next query's setup clears them: it reads the
eye's decals on the rig (the puddles follow it, section 4), and the second-query lab variant of section 3 showed
the gathers starting off a finish (inferred from its draw counts). The engine calls the finish right after the
kick (RVA 0x1C75924) or later (RVA 0x1C6688D) by the kickoff mode; the job graph itself was not read.

The same gather (one match of the signature, the same Umbra-off block) is in the Game Pass 1.0.56.0 image at
RVA 0x1D04220, read statically; not run there. On any game build where the pattern is not found nothing is
hooked and the log says why.

Results, puddle / strip, eye L / eye R (same-view reference 73.9 / 74.0, 82.7 / 82.7):

| Run | Puddle | Strip |
|---|---|---|
| Before (main) | 65.2 / 63.1 | 65.5 / 62.8 |
| After | 73.9 / 71.2 | 81.7 / 79.0 |
| After, with the added bits not set (positive control) | 65.2 / 63.2 | 65.5 / 62.8 |

The opaque controls stay within 1% (eye L's dry concrete 161.5 / 161.9, deck 43.1 / 43.6). The second view (yaw
-160) reads 87.4 / 86.2 against the reference's 86.2. Same-view stereo with the fix reads as before (73.9 /
74.0): every gather there keeps the engine's list.

## 5. Cost

A/B on the test PC against the branch's base (Release, Route S, TAA, ray tracing on, 2048 x 2208 per eye, frame
pacing off, head still; two runs each in the order A B B A, means over 40 s per run, median [range]; draws from
four dumped frames of one run):

| Scene | Ticks/s | GPU per stereo tick (ms) | Process CPU per tick (ms) | Eye R render wall (ms) | Draws per eye frame |
|---|---|---|---|---|---|
| Level start above (GPU bound) | 56.62 [56.55-56.68] -> 55.61 [55.38-55.83], -1.8% | 17.26 -> 17.44, +1.1% | 67.80 -> 67.84, +0.1% | 8.03 -> 8.30 | 7,384 -> 7,338 |
| Mission 2 arena (`teleport checkpoints_player_start_cp_12`) | 73.84 [73.47-74.22] -> 73.28 [73.10-73.45], -0.8% | 12.98 -> 12.65, -2.5% | 66.75 -> 67.45, +1.0% | 6.03 -> 6.36 | not dumped |

At the level start each gather adds about 12,200 decals (the log's `seq-decals:` count): every active decal in
the frustum around the pair, occluded ones included. The cost shows as about 0.3 ms more wall time in eye R's
render in both scenes; eye L's wall time did not grow.

The record scan is not that cost. A variant that scanned once per tick (the tick's other eye took the scan:
the same records, the same axis bit for bit, another origin) left eye R's render wall at 8.01 -> 8.26 ms and
read, at the level start, ticks -3.1% and GPU +4.2% (A B B A, one session; not kept). The cost is the added
decals: every active decal in the frustum, occluded ones included. Narrowing them to the decals that reach past
the eye's own edges (the band only Umbra's wider frustum covers) fixed eye R's puddle but not eye L's (65.0
against 73.9): eye L's missing wet patches lie wholly inside its own frustum.

## 6. Open

- Eye R's puddle stays about 4% darker than the flat game's (71.2 against 74.0), as it was with each eye's pose
  and the flat game's symmetric frustum (71.7), so most likely the eye's own position changes the reflection
  itself.
- The frustum around the pair is centred on the gathering eye; the other eye's outer edge starts one eye
  distance (about 6 cm) to the side, so a decal only in that sliver right next to the head is not added.
- Why Umbra reports a decal that covers visible floor as hidden with the narrower frustum was not traced.
- Parallel Eye Rendering does not install the detour yet.
- `packedInverseProjection0` and `packedProjection` (RVA 0x1C71630) still assume a centred frustum for the
  screen-space passes that read them (SSDO, SSR and the reflections composite); setting the inverse from each
  eye's projection (a lab hook) left the puddles unchanged.
