# Walking texture in the fog under Route S: the scattering history per eye

In the fifth and sixth Quest 3 test sessions, the faint fog and the god-ray shafts in e1m1's first room showed a
"walking texture" as the head moved. The tester's A/B in session 6 isolated it: with `r_lightScattering 0` the walk is
gone, and so is the fog. With `r_lightScatteringQuality 3` it stays; the smoke below "looks great", while the walk
shows "in the faint fog, particular where light shines through in god rays". So the cause is the volumetric
light scattering, not the sprite particles. This page records how it was isolated on a second test PC (RTX 3080 Ti, i7-9700K;
OpenXR-Simulator with the Quest 3 profile, 1904 x 2048 per eye, Steam build 25216728) and the fix. RVAs are in
that build.

## 1. Cause

The scattering volume is a froxel grid of render size / 16 by 64 slices (119 x 128 x 64 at 1904 x 2048). The
game filters it over time (`r_lightScatteringTAA`). The history is four images in the device context
(global 0x66E3B88): the pair at + 0x2D0 / + 0x2D8 and the pair at + 0x2E0 / + 0x2E8. The compute pass
(RVA 0x1C70420) writes the pair of the render counter's parity and reads the other one. The counter goes up
by one per render, so under Route S eye L always writes one pair and eye R the other, and each eye would
filter its fog with the other eye's volume, taken from the other eye's position. The per-eye TAA set therefore
held `r_lightScatteringTAA 0`. Without the filter, the volume's coarse cells crawl across the image as the
head moves: the walking texture.

Ruled out on the rig: per-eye placement (with both game eyes at the head centre, eye R's fog matches eye L's
shifted by the asymmetric projection, with no offset), and per-frame flicker at a still view (the same with
the filter on or off).

## 2. Fix: per-eye scattering history (default; `ETERNALVR_STEREO_SCATTER_TAA=0` turns it off) (`src/vkcore/scatter_hooks.*`, `src/stereo_seq/scatter_history.*`)

- **Eye R's images.** Right after the device context constructor makes the engine's four (RVA 0x1C1CD80, hook
  after the last store at RVA 0x1C1F05C), eye R's four are made through the image manager (0x5BF13C8,
  create RVA 0x1C3D890) with the same image description.
- **Per render.** At the scattering setup's entry (RVA 0x1C71F90, before the compute pass of the same render),
  the eye's own pairs go into the two slots (its last written pair where the engine reads, its other pair
  where it writes), and the filter's state (the struct at state + 0x60 .. + 0x94: last frame, volume size,
  camera position, reset count) is swapped in when the eye changes. The engine resets the filter when its last
  frame is not the render just before, and an eye's own last render is always two back, so the state is
  passed off as the previous render's. Each eye's first two renders clear their history: the compute clears
  only the pair it reads when the last frame is 0, and cells it never clears show as blocks of stale light.
- **Resizes.** When the render size changes, the engine resizes the four volumes in place (RVA 0x1CDD6D0,
  `resize(image, width, height, depth, mips)` at RVA 0x1C4AE30). The start-up size is 98 x 64 x 64. Right after
  the engine's last resize (hook at RVA 0x1CDDD7D), eye R's four follow, and so do whichever of the engine's
  four eye R had in the slots at that moment. Both eyes then clear their history again. Before this, eye R
  rendered into volumes of the start-up size and its fog showed sharp cell-shaped blocks, with most of the
  haze missing. A diff of the engine's and eye R's image objects mid-run (+ 0x64 width, + 0x68 height,
  + 0x6C depth, + 0x110 memory size) found it.
- With all three hooks in place, the per-eye TAA set keeps `r_lightScatteringTAA 1`. Anything missing leaves
  the game untouched and the filter held off.

## 3. Checks (second test PC, 2026-09-27)

| Check | Result |
|---|---|
| Held view of the burning spike gate (`tools/lightrun.ps1 -Sway '0,0,30,180' -Size 1904x2048`), aligned L/R crops | both eyes clean, fog and haze in both, no blocks |
| Ghost check, 1280 x 1400 (`tools/ghostrun.ps1`, mean of 12 pairs) | feature off: eye L <- R 0.013, eye R <- L 0.008, control 0.008; on: 0.010 / 0.005, control -0.003 |
| `r_lightScatteringTAA` | off: per-eye TAA writes 1 -> 0 at about 17.7 s; on: stays 1 |

The walk itself is sub-degree motion that the simulator's captures (about one pair a second) can't show, so
the fix still needs a Quest 3 check.

## 4. Open

- The god-ray shafts: whether their sampling noise has a history of its own that needs to be per eye.
- The headset check (on by default since the alpha, 2026-09-27: the fog is to be fixed for the alpha).
