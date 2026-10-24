# Screen-space reflections per eye under Route S: the packed projection assumes a centred frustum

With ray tracing off, puddles and glossy floors reflected in one eye and not the other, or not at all except from
some positions (a headset session on 0.1.72, Quest 3; a player's report of the same at the start of Urdak and in
the first arena of The Ancient Gods part two's second mission). This page records the cause, read from the game's
code and shaders, the rig measurements on the test PC (RTX 3080 Ti, OpenXR-Simulator with the Quest 3 profile,
2048 x 2208 per eye, Steam build 25216728) and the fix. RVAs are in that build.

## 1. Cause [static]

The SSR/SSDO parameter setup (RVA 0x1C71630, called by the render-view job at 0x1C56AF7) hands the screen-space
passes the view's projection (the latched `projectionMatrix`, idRenderView + 0x29340, row-major) packed into four
floats:

| Parameter | Value |
|---|---|
| `packedProjection` | (0.5 m0 / m14, -0.5 m5 / m14, m10 / m14, m11 / m14) |
| `packedInverseProjection0` | (2 i0, -2 i5, -i0, i5), i = the latched inverse projection (+0x29380) |
| `packedInverseProjection1` | (i14, i15, i11, 0) |

Only the diagonal terms are kept: the off-centre terms m2 and m6 of an asymmetric frustum are dropped. The
opaque pass's materials (18 fragment shaders in a shader dump of a map) do the SSR march themselves:

- the pixel's view position: `vec3(inv0.xy * uv + inv0.zw, inv1.z) / (inv1.x * depth + inv1.y)`;
- the reflection: `reflect(-V, N)` in world space, turned into view space by `mat3(p0.xyz, p1.xyz, p2.xyz) * R`,
  the three parameters being `inverseViewMatrixX/Y/Z` (set by the render-view job at RVA 0x1C54C53 from
  idRenderView + 0x29400; their xyz are the columns of the world-to-view rotation);
- the march: start and end projected with `uv = 0.5 + packedProjection.xy * xy / z` (0.5 fixed in the code), 8, 12
  or 16 depth samples between them, the hit reprojected with `ssrReprojectionMatrix` into the last frame's colour.

Inverse and projection agree with each other: a pixel's position projects back to the same pixel. Both describe a
view space sheared by S = [[1, 0, c], [0, 1, d], [0, 0, 1]], c = m2 / m0, d = m6 / m5 (Quest 3: c = -0.269 in eye L,
+0.269 in eye R, d = -0.213 in both). The reflection direction is turned by the true rotation, though, so the march
follows the screen line of S^-1 R instead of R: about 13 to 17 degrees off for floor reflections (median over the
eye image, head level or pitched down 20 degrees), sideways in opposite directions in the two eyes and tilted
toward the floor in both. A floor's reflected ray that should rise to the scenery behind the puddle runs into the
floor or out of the image, so the reflection is missing; where it does hit, the two eyes take it from different
places. The flat game and mono frames have m2 = m6 = 0 and are not affected.

SSDO (3 compute shaders) reconstructs positions the same way, takes its normal from the cross product of
neighbouring positions (in the sheared space that is S^-T N) and turns its bent normal back to world space with the
same three parameters transposed. The RT path's compute SSR (`computeSSRTrace`, 4 compute shaders, run instead of
the opaque pass's SSR while ray-traced reflections are on) reflects in view space with the sheared position.

## 2. Fix

`src/vkcore/screen_space_view_hooks.cpp` hooks the render-view job on the instruction after
`inverseViewMatrixZ`'s set (RVA 0x1C54C95; r13 the idRenderView, r15 the parameter context) and sets the three
parameters again with the engine's vec4 setter (RVA 0x1C53720), each column sheared for the view's own projection
(`stereo_seq::screenSpaceShear`, `shearRotationRows`: x += c z, y += d z). The opaque pass's march then runs in the
sheared space from end to end, which `packedProjection` maps onto the true pixels: exact for any ray. The same
parameters give SSDO's bent normal S^T S^-T N = N for an unoccluded pixel (before: off by about 12 degrees on floors
and side walls). The RT path's compute SSR keeps an error of the same size as before (16 to 17 degrees median in a
model of both, it reflects in view space with the sheared position, which no rotation can undo). A symmetric
projection gives c = d = 0 and nothing is set, so mono frames and the flat game are unchanged.

Located by signature (unique in the Steam build and in the Game Pass 1.0.56.0 image, where the three handles'
declarations also read `inverseViewMatrixX/Y/Z`) and checked by the parameters' names and the setter being a
function; anything missing logs `seq-ssr-view:` and leaves the game's parameters. Every 10 s a `seq-ssr-view:` line
counts the view renders sheared, left centred, skipped and held (below). Route S installs it with the other eye
fixes; Parallel Eye Rendering installs it with its own changes to the engine (`view_install.cpp`), the same hook
and the same code (section 5).

The shear alone moves reflections to their true place but leaves each eye's own screen-edge fade (section 3, "Both eyes
agreeing"): `smoothstep(0, 0.5, clamp(1 - dot(ndc, ndc), 0, 1))` on the hit's reprojected uv, the same
uv that samples the colour, constants in all 22 SSR shaders. In an eye whose image ends 40 degrees in and 54 out, that
fade runs from 27.2 degrees on the nose's side and 46.4 on the other, so a reflection in one eye's nasal band faded in
that eye only, and the eyes agreed less over their whole overlap than before at one position (0.92 -> 0.69).

`src/vkcore/ssr_fade_hooks.cpp` therefore patches those shaders as the game creates them (vkCreateShaderModule on its
device; `src/vkcore/ssr_fade_patch.cpp`): a dozen instructions before the fade's `dot` give it the horizontal frame
both eyes have pixels for, ndc_x' = (ndc_x + m2) / (1 - |m2|), so both eyes fade a direction alike: full to 30.7
degrees either side of straight ahead, nothing past 40. No data is added: with the shear set, the three rows the
shader already loads are S R, and S S^T = (S R)(S R)^T gives c = X.x X.z + Y.x Y.z + Z.x Z.z; m2 = c m0 = -2
`packedProjection.x` c. An unsheared rotation gives c = 0 and the game's fade (mono, the flat game). Vertically both
eyes share their frustum and keep it.

Only the 22 modules known by hash are patched (their view block's member indices are in the table), and a patched
module is used only when its words hash to the output that was validated with spirv-val (Vulkan SDK 1.4.363, target
Vulkan 1.1, all 22 valid); any other outcome creates the game's module. The shear runs only while every SSR shader
fades in the shared frame (`ssr_fade::coherent()`): all 22 patched, none failed, and no module of unknown hash
carrying the fade's signature (another build's shaders). Until then, or after such a module, each off-centre render
is left unsheared and counted as held (`seq-ssr-view:`). `ssr-fade: all 22 SSR shader modules patched` logs the
moment; on the test PC it came at the start (10 s), before the first map.

## 3. Rig

Urdak (`e3m3_maykr`) at the four positions of the headset captures (`setviewpos 401.63 -344.12 -97.64 37.6`, the
wet rock by the tree; `353.49 -360.81 -98.74 -168.3`, the round glossy platform before the first jump pad; `394.89
-350.65 -97.64 0`; `258.37 -500.87 -69.07 62.2`, a red corridor), head still and level, DLSS per eye, ray tracing
off, four frames averaged per eye. The SSR contribution is each eye's image with SSR minus the same run's
arm with `ETERNALVR_STEREO_SSR=off`; the reference is both eyes from the head centre with the game's symmetric
FOV (`ETERNALVR_STEREO_EYE_POSES=0`, both eyes pixel-equal), resampled to each eye's pixel directions. Correlation
of the contributions (6 px blur) and their mean magnitude on the floor (lower 45%), where the shader's screen-edge
fade is 1 (1 - |ndc|^2 >= 0.5) [rig]:

| Arm (same lab binary) | Platform eye L | Platform eye R | Third position eye L | Third position eye R |
|---|---|---|---|---|
| Engine's rotation | 0.31, 1.88 | 0.22, 2.14 | 0.88, 3.68 | 0.86, 1.84 |
| Sheared per eye (the fix) | 0.86, 4.28 | 0.46, 1.82 | 0.91, 4.41 | 0.90, 1.92 |
| Each eye given the other eye's shear | 0.10, 1.60 | 0.47, 3.51 | 0.64, 1.89 | 0.84, 1.90 |
| Reference magnitude | 4.61 | 2.59 | 4.38 | 2.06 |

The platform's sky reflection left of the gun, present in the reference and missing in both eyes before, is back
in eye L with the fix; the swapped arm, which gives each eye the wrong sign, loses it again in eye L and
overshoots in eye R, so each eye reads its own parameters. The corridor and the start position have little SSR
(contribution under 1.3) and change little. Eye R reads its own parameters too: the image difference between
its own and the swapped shear is larger in eye R than in eye L on the platform floor (3.83 against 2.55, mean
absolute, 3 px blur); its correlation stays lower because its reflection there lies toward its nasal edge. The magnitudes are the mean absolute luminance change, 8-bit.

Where the edge fade is below 1 on the eye's inner side (the band toward the nose, |ndc.x| > 0.3), the fix takes
reflection away: eye R at the third position 3.34 -> 1.15 (reference 3.78), eye L on the platform 2.30 -> 1.03 (2.41). The
engine's rotation tilted the rays toward the image's centre there, so their hits landed inside the fade's full
region; with the true direction the hits lie near the inner edge and fade out. That is the screen-space limit of an
eye whose image ends 40 degrees in, against 54 degrees out (section 4).

SSDO alone (SSR off, minus SSR and SSDO off): the fix leaves the floor's occlusion as it was (correlation with the
reference 0.61 / 0.46 against 0.61 / 0.46 at the start position, 0.49 / 0.38 against 0.50 / 0.40 on the
platform; magnitudes within 6%). Ray tracing on: the fix changes the floor by 0.5 to 0.9 on average (99th
percentile 3 to 7), the size of the run-to-run change with SSR off (0.2 to 0.7, 99th percentile 2 to 7); with ray
tracing off it changes it by up to 2.0 (99th percentile 30) on the platform. Refraction at Urdak's big window
(`setviewpos -159.84 480.27 -150.34 8`): glass luminance eye L / eye R 1.001 before, 1.005 with the fix.
The hook sets the rotation for every eye render (about 170 per second at the level start, both eyes) and none of the
symmetric arm's renders.

Performance of the shear alone (same binary, hook off against on, warm-up then M F F M M F F M, ray tracing off,
pacing off): the platform ticks +0.2%, stereo tick GPU -0.2%; the Reclaimed Earth start ticks -1.3%, GPU -0.6%, arm
ranges overlapping [rig].

Before the fix, the headset captures that showed it (Urdak, ray tracing off, eye R matched to eye L by block
search over disparity): the residual on the floor was 1.3 to 1.6 times the residual above the horizon at the rock
and 2.6 to 3.7 times on the platform, against 0.8 to 1.3 times in the ray-traced captures [headset capture].

### Both eyes agreeing

Eye R's contribution resampled to eye L's directions and aligned by a per-block disparity found on the SSR-off
image (32 px blocks, normalized correlation >= 0.85), floor rows, 6 px blur; correlation L~R and disagreement
(mean |L - R| over the mean magnitude), engine's rotation -> fix [rig]:

| Position | Overlap (+-40 degrees) | Centre (+-20) | Eye L's nasal band (27 to 40 right) | Eye R's nasal band (27 to 40 left) |
|---|---|---|---|---|
| Rock (`401.63 -344.12`) | 0.94 -> 0.94; 0.59 -> 0.44 | 0.95 -> 0.98; 0.55 -> 0.33 | 0.19 -> 0.25 | 0.71 -> 0.88 |
| Platform | 0.28 -> 0.33; 0.87 -> 1.13 | 0.45 -> 0.64; 0.93 -> 0.72 | 0.11 -> -0.01 | 0.45 -> 0.32; 0.66 -> 1.33 |
| Third position | 0.92 -> 0.69; 0.37 -> 0.56 | 0.84 -> 0.98; 0.49 -> 0.18 | 0.87 -> 0.87 | 0.93 -> 0.49; 0.31 -> 1.26 |

The centre agrees better everywhere. In each eye's nasal band (its own fade, 27.2 to 40 degrees from straight
ahead, 12.8 degrees wide horizontally) the fix leaves a reflection in the other eye only: in eye R's band at the
platform eye L shows 2.55 and eye R 0.58 (before 1.33 and 1.37), at the third position 4.98 and 1.28 (4.56 and
4.74); the centre's gain is of the same size (platform eye L 1.67 -> 2.88). Before, the engine's tilt kept those
hits inside both eyes' full-fade region, in the wrong place but alike.

### Shear and shared-frame fade together

Same positions and method as "Both eyes agreeing" (eye R aligned to eye L by disparity, floor rows, 6 px blur), one
lab binary, three arms: the engine's rotation and fade, the shear alone, the shear with the patched fade; correlation
L~R [rig]:

| Position | Overlap (+-40) | Centre (+-20) | Eye R's nasal band, mean |L| / |R| |
|---|---|---|---|
| Rock (`401.63 -344.12`) | 0.94 / 0.94 / 0.99 | 0.95 / 0.98 / 0.99 | 0.71 / 0.92 / 0.98; 0.73 / 0.38 -> 0.48 / 0.42 |
| Platform | 0.31 / 0.30 / 0.79 | 0.47 / 0.62 / 0.79 | 0.47 / 0.28 / 0.91; 2.29 / 0.22 (shear) -> 0.32 / 0.32 |
| Third position | 0.92 / 0.69 / 0.98 | 0.84 / 0.97 / 0.98 | 0.93 / 0.50 / 0.98; 4.92 / 1.19 (shear) -> 2.01 / 1.97 |

Eye L's nasal band holds the gun and little reflection (magnitudes 0.2 to 0.4 in both eyes, equal within 10%). What
the shared frame costs, mean floor contribution by azimuth (10 degree bins, each eye in its own pixels): both eyes
now fade alike from about 30 degrees out (third position, eye L -40 to -30 degrees 4.98 -> 3.57, eye R 5.58 -> 3.68),
and each eye's outer strip past 40 degrees, which only that eye sees, loses most of its reflection (third position
eye L -50 to -40: 2.16 -> 0.26; rock eye R 40 to 50: 0.95 -> 0.29).

With ray tracing on the patched compute SSR changes the platform's floor (mean 1.32, 99th percentile 13 levels; with
SSR off the two arms differ by 0.3 to 0.7), while the eyes' agreement stays: the aligned L - R floor difference 4.80 /
14.34 / 5.40 -> 4.86 / 14.39 / 5.22 at the three positions. SSDO alone (SSR off) and refraction at Urdak's big window
(glass eye L / eye R 0.998 -> 1.007) are unchanged. The Game Pass build (1.0.56.0, its exe started directly with the
layer, main menu): all 22 modules matched by hash and were patched, the shear hook installed at its own RVA
(0x1CE1145), no module of unknown hash had the fade [log]. Performance, the same lab binary with both off against
both on, warm-up then M F F M M F F M, ray tracing off, pacing off: the platform (4 pairs) ticks -0.2%, stereo
tick GPU -0.1%; the Reclaimed Earth start (8 pairs) ticks -1.3%, GPU -0.1% (the first four pairs +2.2%, the next
four -1.5%) [rig].

### Other readers of the three parameters

Whole frame, per eye, hook on against off, with a second off run as the noise (three scenes: the platform, the
Reclaimed Earth start, the e1m1 burning gate with fire, smoke and decals): the only differences above the noise are
the reflecting floors; the rest is the gun's pose, birds and fire, as large between the two off runs. A probe arm
(eye L's three rows turned 180 degrees about the vertical, SSR and SSDO off, eye R untouched as the control) adds
nothing over eye R's change in any of the three scenes, so no other pass visible there reads them [rig]. In the
shader dump the 26 shaders that build a rotation from three consecutive view parameters are the 18 opaque SSR
materials, the 3 SSDO passes, 4 compute SSR passes and one ray-traced reflection normal pass (it rotates a
world normal built from the true frustum vectors, so it gets S R n; ray tracing on changed within run-to-run noise)
[static].

## 4. Open

- The shared-frame fade takes most of the screen-space reflection off each eye's outer strip past 40 degrees, which
  the other eye does not see. Rendering each eye symmetric (54 degrees in too) would keep it, for about 24% more pixels
  per eye; not done.
- Eye L's nasal band agrees little at these positions (correlation -0.06 to 0.91) with little signal in it.
- Ray tracing on: the RT path's compute SSR (`computeSSRTrace`) reflects in view space from the sheared position
  and keeps an error of about the same size (a model of both rotations: 16 degrees median); no parameter undoes
  it. The ray-traced reflections themselves reconstruct positions from world-space frustum vectors the latch
  builds from the full matrix (RVA 0x1CE1B51) and are not affected.
- Not checked in a headset; on the Game Pass build only up to its main menu. Parallel Eye Rendering: ray tracing on
  and a headset not checked either (section 5).

## 5. Parallel Eye Rendering

Parallel Eye Rendering did not install the hook until the fix described here, so its screen-space reflections had
the engine's centred rotation in both views (section 1) and the shared-frame fade patch, installed for every mode,
did nothing without the shear (an unsheared rotation gives c = 0, the game's fade).

What the hook sees [log]. The hook sits in the per-view render-view job (RVA 0x1C54C95): r13 is that view's own
idRenderView, r15 its parameter context. Parallel Eye Rendering writes each view's pose and explicit projection with
the code Route S uses for an eye view (`writeEyePose`) and the engine's latch then builds the view's
`projectionMatrix`, the field the hook reads. First hook lines of a run, one per view, from two threads:

| View | Latched projection [0] [2] [5] [6] | Shear x, y | Row X |
|---|---|---|---|
| 0 (eye L) | 0.9027 -0.2425 0.8492 -0.1805 | -0.2686, -0.2125 | 0.9713 0.0000 0.2380 -> 0.9073 -0.0506 |
| 1 (eye R) | 0.9027 +0.2425 0.8492 -0.1805 | +0.2686, -0.2125 | 0.9713 0.0000 0.2380 -> 1.0352 -0.0506 |

Route S logs the same two lines, to the digit. The summary lines then count 2,100 to 2,500 sheared view renders per
10 s (both views, about 120 ticks per second), none skipped or held and, after the first summary (8 centred
renders in it), none centred; `ssr-fade: all 22 SSR shader modules
patched` comes at 8 s, before the first map. The hook only reads the view's own projection and writes the view's own
parameter context, and its counters are atomic, so the two views running at once share nothing it writes.

The fix installs it once from the engine changes of the Parallel Eye Rendering install (`view_install.cpp`), after
the bin tile hook, with no option of its own.

Rig [rig]. Urdak at the same four positions, head still and level, DLSS in each view (quality, preset K), ray tracing
off, pacing off, 2048 x 2208 per eye, four frames averaged per eye; contribution, reference, floor rows and blur as in
section 3, the reference being Route S with both eyes from the head centre (symmetric FOV). Correlation of the
contribution with the reference resampled to the eye's pixel directions, and its mean magnitude on the floor
(reference magnitude in the last column); then the correlation of eye L with eye R over +-40 degrees on a common grid:

| Arm | Position | Eye L | Eye R | Reference | L ~ R |
|---|---|---|---|---|---|
| Parallel Eye Rendering before | Rock | 0.94, 2.71 | 0.94, 2.60 | 2.16 / 1.97 | 0.89 |
| | Platform | 0.26, 3.83 | 0.13, 5.57 | 7.81 / 7.11 | 0.50 |
| | Third position | 0.77, 5.96 | 0.81, 3.94 | 6.27 / 3.69 | 0.74 |
| Parallel Eye Rendering with the hook | Rock | 0.96, 2.17 | 0.97, 1.99 | | 0.96 |
| | Platform | 0.55, 4.57 | 0.62, 4.37 | | 0.84 |
| | Third position | 0.62, 5.23 | 0.86, 4.42 | | 0.75 |
| Route S with the hook | Rock | 0.95, 2.48 | 0.97, 2.36 | | 0.96 |
| | Platform | 0.55, 4.56 | 0.62, 4.34 | | 0.84 |
| | Third position | 0.62, 5.25 | 0.86, 4.44 | | 0.75 |

With the hook the two modes agree to the second decimal on the platform and the third position (each eye's
correlation and magnitude), so nothing else of Route S's SSR work (the noise index, the history holds) was needed
to get there. The platform's eyes agree with each other at 0.84 instead of 0.50. The third position's eye L goes
down (0.77 to 0.62) as in Route S: the inner band's reflection fades out with the shared frame (section 3). The
corridor has little SSR (contribution about 1 to 1.5) and its numbers are noise in every arm. Ray tracing on and a
headset were not run for Parallel Eye Rendering.

Performance (Parallel Eye Rendering on, pacing off, the platform, warm-up then main, fix, fix, main, main, fix, fix,
main): ticks per second 107.1 on both (mean of four runs each, +0.02%), stereo tick GPU 8.67 ms on main and 8.68 ms
with the hook; the runs drift down by about 3% over the sequence (the card warming), alike in both arms [rig].
