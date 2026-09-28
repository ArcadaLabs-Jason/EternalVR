# Tile-shaped lighting under Route S: the binning's symmetric tile grid

The third Quest 3 test session showed lit areas ending in staircase edges of screen tiles (a wall lantern's
light, rectangular light and dark patches on the e1m1 spike gate), in the left eye as Virtual Desktop grabs
it. This page records how it was reproduced and isolated on a second test PC (RTX 3080 Ti, i7-9700K; OpenXR-Simulator with
the Quest 3 profile, 1904 x 2048 per eye, Steam build 25216728) and the fix. RVAs are in that build.

## 1. Cause

The binning setup (RVA 0x1CFC050, called once per view render from the render-view job at RVA 0x1C57999)
gives the light and decal binning its tile grid as four shader parameters, computed from the render view's
`fov_x` and `fov_y` (idRenderView + 0x289F8 / + 0x289FC):

| Parameter | Value |
|---|---|
| `binTileWidth` | 2 tan(fov_x / 2) * 32 / render width |
| `binTileHeight` | 2 tan(fov_y / 2) * 32 / render height |
| `binTileLeft` | -tan(fov_x / 2) |
| `binTileTop` | -tan(fov_y / 2) |

That is a symmetric frustum of 32-pixel tiles. Each Route S eye is drawn with an asymmetric explicit
projection (Quest 3: 54 degrees out, 40 in, 44 up, 54 down), while `fov_x` / `fov_y` hold the symmetric
enclosing FOV the layer gives the game (108 x 108.5 degrees). The binning therefore assigns lights and decals
to tiles of another frustum than the one drawn: exact at the frustum's wide edge, off by up to a quarter of the
image at its narrow edge. Where the binned tile misses a light, that tile goes dark; the edges of lit areas
become staircases of tiles, worst on the eye's narrow side (eye L: right of centre, eye R: left). Decals are
binned the same way, which fits "effects in one eye only".

Only three functions read `fov_x` / `fov_y` from the idRenderView: the latch (RVA 0x1CE1400, which uses the
explicit matrix when one is set), the AA pass's jitter call (RVA 0x1C9B5D0) and this setup.

## 2. How it was isolated

Scene: `+map game/sp/e1m1_intro/e1m1_intro`, the burning spike gate at the end of the spawn room (the gate of
the tester's -220640 grab), held with `ETERNALVR_TEST_HEAD_SWAY=0,0,30,180`, both eyes captured. The environment
probes' reflections hide most of the error on that wall; `r_environmentProbes 0` (isolation only) shows it as
a large staircase in eye L.

| Change | Eye L staircase |
|---|---|
| `r_SSR 0`, `r_raytracedReflections 0`, `r_enableAsyncCompute 0`, `r_clusterDataUseTransferQueue 0`, `r_useFastClusterMapping 0`, `r_useUmbraCulling 0`, `ETERNALVR_STEREO_DISCONTINUOUS=1` | unchanged |
| both eyes from the head centre with the game's symmetric FOV (`ETERNALVR_STEREO_EYE_POSES=0`) | gone; L and R pixel-equal except ray-traced floor reflections |
| same-view stereo (`ETERNALVR_STEREO_SAME_VIEW=1`) | gone |
| poses swapped between the two chains (lab) | follows eye L's pose, not the render order |
| eye L's pose with eye R's FOV (lab) | gone |
| both eyes with eye L's FOV (lab) | in both eyes |
| eye R's FOV, head turned so the gate is right of centre (lab) | in eye R, on the gate's left side |
| each eye with its enclosing symmetric FOV (lab) | gone in both eyes |

## 3. Fix

`src/vkcore/bin_tile_hooks.cpp` hooks the instruction after the setup's call (RVA 0x1C5799E; r12 + 0x18 is
the idRenderView, r15 the render context) and sets the four parameters again with the engine's own setter
(RVA 0x1C53420) from the view's latched projection (idRenderView + 0x29340), with
`stereo_seq::binTileParams`: left = tan(left edge), width = (tan right - tan left) * 32 / width, top = -tan(up
edge), height = (tan up - tan down) * 32 / height. For a symmetric projection these are the engine's values
bit for bit (logged for the loading screen's 35.98-degree view: width 0.010916, height 0.010149, left and top
-0.3248 from both), so mono frames are unchanged. The rows start at the up edge: starting them at the down
edge instead (tan(down)) broke both eyes. The render-view job only ever sees the latched projection of the
eye its frame is tagged with (the per-eye TAA counters), so the projection read there is that eye's.

Result on the scene above, probes off and on: no staircase in either eye. `ETERNALVR_STEREO_BIN_TILES=0` turns
the fix off.

## 4. Open

- The tester also reports fog and god rays that move a little with the headset. The light-scattering volumes are set
  up in the same function (after the bin tiles) but from the light grid parameters and the latched matrices,
  not from `fov_x`; not investigated.
- The headset check (the next Quest 3 session) is the final evidence.
