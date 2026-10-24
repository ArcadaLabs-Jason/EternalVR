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
maxs) reach the frustum around the eye pair gets its visible bit too. (That is 0.1.69 to 0.1.79; since section 7 the nearest of those decals are chosen at the
  gather and listed after the engine's own, scored by its jobs.) That frustum is centred on the eye's axis
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

## 7. Too many decals listed: the floor's own decals stop drawing (Urdak)

A player's headset session (Urdak, `game/sp/e3m3_maykr/e3m3_maykr`) showed a tunnel wall and floor whose
textures "only load when I look in a specific direction". The rig reproduced it at the player's spot
(`setviewpos 47.20 175.64 -160.24`, yaw swept 85 to 145 in steps of 5, a capture 3 s after each; each capture
resampled to one world-fixed window so a wall point sits at the same pixel at every yaw; Release, Route S,
2048 x 2208 per eye, ray tracing and SSR on, 0.1.79). Beside the round door the surface shows either a dark
grated pattern (mean luminance of the patch about 42 to 57) or a smooth pale panel (83 to 97), and it switches as
a step with the view yaw (panel at yaw 125 and below, grate from 130) while the patch is 5 to 15 degrees from the
view centre. The flat game's view (`ETERNALVR_STEREO_SAME_VIEW=1`) shows only the grate at every yaw.

### 7.1 Which objects

`r_skipDecals 1` shows the panel at every yaw, so the grate is a decal drawn over the panel. Umbra reports 13 to
24 decals visible at this spot, out of 13,067 active ones. The pair gather of section 4 adds every active decal in
the frustum around the pair: 12,900 at yaw 104, 3,400 at yaw 145, hidden ones included. In a player's 0.1.79
session (95 ten-second windows over several missions) 71 windows added 2,000 or more decals a gather, the median
4,176 and the most 12,884: the flood is the normal case on the default path, not an Urdak one.

| Arm (yaw 85 to 145) | Where the panel shows |
|---|---|
| 0.1.79 (every decal in the frustum added) | 125 and below |
| the added decals left out (lab) | nowhere |
| same view, Umbra on | nowhere |
| same view, `r_useUmbraCulling 0` (the engine lists every decal in the frustum) | 105 to 125 |
| added decals within 30 m, or within 100 m, of the eye (lab) | nowhere |
| only added decals with an index above the floor's (13,200 and up) | nowhere |
| only added decals with an index below the floor's (13,158 and down) | 125 and below |
| the nearest 1,000 added | nowhere |
| the nearest 2,000 added | 120 and below |
| `r_geoDecalMaxProjections 4096` or `16384` on 0.1.79 (it reads 1008) | unchanged |

### 7.2 The limit

The engine scores and ranks a view's listed decals in two jobs (read statically): the first (RVA 0x1CB8950)
drops decals that face away or are too small and gives the rest a key (decals within the fade distance first, the
others by size over distance, ties by index); the second (RVA 0x1CB8D80) sorts the keys, keeps the first 2048 and
rewrites the list in rank order. At the eye view the scored count, with the list sizes of the rig arms (a lab
build logged the second job's input and output counts at the eye's origin, every 150 ms):

| List (added decals) | Scored decals at yaw 105 / 110 / 115 / 120 / 125 / 130 | Panel at |
|---|---|---|
| Umbra's + the nearest 1,800 in all | 1660 / 1651 / 1651 / 1649 / 1595 / 1449 | none |
| ... 1,900 in all | 1741 / 1718 / 1694 / 1678 / 1607 / 1459 | 105 to 120 |
| ... 2,100 in all | 1779 / 1747 / 1721 / 1705 / 1622 / 1467 | 105 to 120 |
| every decal in the frustum | 2011 / 1972 / 1942 / 1920 / 1819 / 1614 | 105 to 125 |

The floor's decals drew with 1,660 scored decals or fewer and stopped from 1,678 (the rig has no run between the
two), at every yaw and every list size. The second job's output equals its input in all of these (below 2048, so
it cuts nothing), so the limit lies after it, in what takes the sorted list on (not read; a fixed-size
per-view buffer of 1,664 is the inference). With a thousand and more hidden decals ranked ahead of them, the
floor's decals (Umbra lists them; the engine ranks them low) fall past it.

### 7.3 Fix

The gather no longer adds to Umbra's list. For an eye view it chooses the decals around the pair that the list
lacks (active, inside the pair frustum, not already listed), the nearest 2048 (`stereo_seq::nearestDecals`), and
leaves them for the view's scoring. A detour on the sorting job (RVA 0x1CB8D80, found by signature together with
the scoring job, RVA 0x1CB8950; both unique in the Steam build and the Game Pass 1.0.56.0 image, RVA 0x1D44EE0
and 0x1D45310) lets the engine finish its own list, then scores and ranks the chosen decals with the engine's own
two jobs on a copy of the job's parameters (so decals that face away or are too small drop out as they do for the
engine's) and puts the nearest of them after the engine's list, each with its key (section 7.4), up to
`kViewDecalSafeCount` (1,632, the 1,664 limit less 32) in all. The engine's decals keep their places. Where the engine's list leaves less than `kPairMinimum`
(640) of room, its last-ranked decals give way for that many: at the Reclaimed Earth start the engine keeps about
1,380 decals and leaves 250, and the wet patches came back for eye L with 400 chosen decals and for both eyes with
640 (nothing given way: neither; 250 chosen: neither; 400: eye L only).

The log counts it every 10 s (`seq-decals: decal list after the engine's scoring`): eye views, decals the engine
kept, chosen decals that scored, appended, given way, and the views in which the chosen ones did not all fit.

| Scene (eye views a 10 s window, mean a view) | Engine kept | Chosen, scored | Appended | Gave way | Did not all fit |
|---|---|---|---|---|---|
| Urdak tunnel (1,569) | 13 | 1,079 | 1,079 | 0 | 0 views |
| Reclaimed Earth start (1,113) | 1,382 | 1,969 | 640 | 390 | all |
| `e2m2_base` arena, `checkpoints_player_start_cp_12` (1,446) | 471 | 1,896 | 1,161 | 0 | all |
| early hub, `setviewpos 0.5 -1.5 7.4` (1,419) | 503 | 1,778 | 1,128 | 0 | all |

Urdak, the same yaw sweep, the patch luminance (grate 42 to 57): 0.1.79 panel at 125 and below (83 to 97), the fix
29 to 57 at every yaw, as with the added decals left out and as the flat view.

The build that also writes the placed decals' keys and keeps each chosen set to its own gather (section 7.4)
reads the same: Urdak 29.4 to 57.1 at every yaw; the puddle 67.9 / 74.2 and the strip 81.0 / 78.9 (eye L /
eye R), 640 appended and 390 given way a view at Reclaimed Earth.

Puddle scene (section 1's `setviewpos 381 -886 -130.3 140`, mean luminance of the large puddle and of the wet
strip, eye L / eye R, four-frame captures):

| Arm | Puddle | Strip |
|---|---|---|
| same view (the reference) | 73.9 / 73.9 | 82.7 / 82.7 |
| 0.1.79 | 67.5 / 73.7 | 80.5 / 78.4 |
| the fix | 67.9 / 74.2 | 81.0 / 78.9 |
| the added decals left out | 60.8 / 65.5 | 66.4 / 65.2 |
| the fix with only the room under the limit (250) | 61.0 / 65.9 | 66.7 / 65.5 |
| ... and 400 of room | 67.3 / 65.5 | 80.4 / 65.1 |

In the arena scene each eye's picture against the same-view reference (mean absolute luminance difference, eye L / eye R,
over the upper 60% of the picture) is 8.6 / 7.9 with the fix, 8.7 / 8.1 and 8.7 / 8.0 on 0.1.79 (two runs) and
8.7 / 7.9 without the added decals; over the whole picture 13.6 / 13.9 with the fix and 13.6 / 13.8 without the
added decals, 14.4 to 14.6 / 14.6 to 14.8 on 0.1.79: no difference between the arms in the upper part beyond
parallax and noise, and the fix is as close to the reference as no added decals in the rest. Hub: the mean
luminance of eight yaws differs 0.3 or less between 0.1.79 and the fix.

Cost (Release, Route S, TAA, ray tracing on, 2048 x 2208 per eye, frame pacing off, GPU timing sampled, head
still; A = 0.1.79, B = the fix, run in the order A B B A; six ten-second windows each, from 35 s after the
schedule's clock; median [range] of the two runs of an arm):

| Scene | Ticks/s A -> B | GPU per stereo tick (ms) | Process CPU per tick (ms) | Eye R render wall (ms) |
|---|---|---|---|---|
| Reclaimed Earth start | 54.16 [53.25-55.07] -> 55.27 [54.55-56.00], +2.1% | 18.01 -> 17.72, -1.6% | 66.31 -> 65.24, -1.6% | 8.52 -> 8.35 |
| `e2m2_base` arena | 69.84 [66.77-72.92] -> 71.75 [71.35-72.15], +2.7% | 13.08 -> 13.23, +1.2% | 68.86 -> 67.47, -2.0% | 6.77 -> 6.36 |

A first pass with an earlier build of the fix (it sorted the 2,048 chosen decals and the engine's ranked ones; the
final build selects without sorting) read +0.1% at the level start and, in the arena, 73.96 [73.80-74.12] ->
70.93 [69.45-72.42] (-4.1%). The same A build read 69.84 in the second pass (one of its runs 66.77), so the
rig's run-to-run spread in the arena is about 9% and a difference under a few percent is not measurable there;
the fix's own two runs in the second pass were 71.35 and 72.15. The two sorts of that build were the added work that grew with the
candidate count (inferred as the cause); they were replaced by selections.


### 7.4 What the jobs read and write (static)

Read in the Steam build; the Game Pass 1.0.56.0 bodies (RVA 0x1D44EE0, 0x1D45310) are the same instruction for
instruction (225 and 117), each call shifted with the function (+0x8C590; the memmove +0x9CB70), and the submit
site (0x1CE9430 there) matches over its first 200 instructions.

- **Arguments.** The submit site (0x1C5CEA0) builds the jobs' parameter block at +0x522640 of its frame context
  and queues the jobs through a table (0x39A9540 the scoring job's thunk, 0x39A9548 the sorting job's), or calls
  them inline as `mov rcx, block; call [table]` (0x1C5D0CE, 0x1C5D170), rdx left as it is: one argument. In the
  scoring job rdx, r8 and r9 are first written at 0x1CB8AE6 (`imul`), 0x1CB8AD0 and 0x1CB8AD8; in the sorting job
  r8 at 0x1CB8D9E, r9 at 0x1CB8DF0, rdx at 0x1CB8DFC. Its byte at [rsp + 0x60] is its own home slot (an empty
  comparator). The detour and the direct calls pass the block alone.
- **The block.** 0x40 bytes (the next block starts at +0x522680): +0x08 the idRenderView, +0x10 the list
  (+0x7B2F8 of the context), +0x18 the keys (+0xB72F8), +0x20 the decal manager, +0x28 the count (+0x4D9B80);
  +0x00, +0x30 and +0x38 are read by neither job. The list and the keys are 0x3C000 bytes apart, 0x7800 entries,
  as many as there are decal records. The copy the fix hands the jobs is the 0x30 bytes up to the count, with its
  own list, keys and count; the scoring job reads the view's latched fields, the list and the manager's tables
  (+0x10 records, +0x20, +0x28, +0x38) and a cvar (0x1CBF920); it writes only keys[0..kept) and the count. The
  sorting job writes only the keys, the list and the count. Nothing engine-owned is written through the copy.
- **The keys.** The scoring job writes one key per decal it keeps, in order (0x1CB8CE4), the record index in the
  low 16 bits. The sorting job sorts keys[0..count) in place: a partial partition (0x1CB8770) when more than 2048,
  then an in-place sort (0x1CB9B40 or 0x1CB75A0, the latter a radix sort whose scratch is 1 MB of its own stack
  frame) and rebuilds list[0..count) from the keys (0x1CB8F00: records + (key & 0xFFFF) * 0x50). Nothing past the
  count is read or written, so keys the size of the chosen list are enough. A later stage reads the keys with the
  same count (a block at +0x6A75D0 of the context, set at 0x1C5F674: the viewport size, the view, the manager,
  the keys, the count; published at +0x5226F8 + 0x170). Its use was not read. So the decals placed after the
  engine's list get their keys at the same places as their list entries. The third job (0x1CB8F30, the same
  block) reads only +0x30 and +0x38.
- **The view.** The gather keys its chosen decals by [viewContext + 0x20] and the sorting job's detour looks them
  up by [block + 8]. They are the same object. In every 10 s window the count of gathers that chose decals and the
  count of sorting jobs that found them are equal (Urdak: `decal gathers: 1569 with the decals around the eye
  pair` and `decal list after the engine's scoring (limit 1632): 1569 eye views`; Reclaimed Earth: 1090 and 1090),
  and the scoring job read the eye's origin at +0x28A64 of [block + 8] (a lab build, 47.20 175.64 at the Urdak
  spot). Each gather drops what its view had waiting before it chooses, and the sorting job's detour checks the
  view is still the eye view the layer wrote. So a set is only used by the sorting job of the gather that chose
  it.
- **Order of install.** The three functions are found first, then the sorting job and the gather are hooked;
  both detours pass straight through until both are in, and for good when one install failed.

### 7.5 Open

- What takes the sorted list on and holds 1,664 was not read; the 1,664 is a measured bracket (1,660 drew, 1,678
  did not) at one spot, so `kViewDecalSafeCount` keeps 32 under it.
- A busy scene's room: where the engine's list is near the limit (Reclaimed Earth's start), 390 of its own
  last-ranked decals give way for the chosen ones, as they did for the flood of 0.1.79 (which ranked its thousands
  against them); which decals those are was not looked at.
- The late hub (the Fortress of Doom exterior a player reported): not reached by the stereo rig scripts, which
  have no menu key presses; the early hub was looked at instead (above), and its decal flood is 1,780 a gather.
- Occlusion: Umbra's occlusion buffer test (RVA 0x2272270, used by the light gather) would drop the hidden
  decals before they are chosen. Calling it from the decal gather with the packet's third field as the object
  closed the game within a second of the first gather; not pursued.
- Parallel Eye Rendering does not install the detour yet (as in section 6).
