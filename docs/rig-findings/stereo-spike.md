# Stereo spike: shader census for Route M (T-049, T-070, T-105)

The first live measurement of what `MultiviewTransform` (Route M, T-004 default) would have to patch,
taken with the census tooling of `docs/rig-findings/stereo-routes.md` section 4 (`tools/shader_census`).

## Run (2026-09-26, rig, Steam build 25216728)

- Launch: `tools\rig\launch-ht.ps1` head-tracked mono (no stereo), OpenXR-Simulator, map
  `game/sp/e1m2_battle/e1m2_battle` (e1m1 now starts in the Fortress with no combat, so e1m2 gives a
  gameplay scene at once), `ETERNALVR_DUMP_SHADERS=<workspace>\tmp-vr\census1`,
  `ETERNALVR_DUMP_SKIP_FRAMES=4000`, `ETERNALVR_DUMP_FRAMES` 60 (default). Run folder
  `<workspace>\runs\20260926-034620-census1`.
- The layer logged `draw log complete after 4060 presents`; the dump holds 481 modules, 1048 indexed
  pipelines and a 186 MB draw log.
- Census: `python tools\shader_census\census.py <workspace>\tmp-vr\census1`, report in
  `<workspace>\tmp-vr\census1\census\census.md` and `census.json`.

## Result

| | Draws | Dispatches |
|---|---|---|
| Commands (60 frames, 4000 to 4059) | 505495 | 15780 |
| Commands with an unindexed pipeline | 0 | 0 |
| Distinct modules bound | 77 | 59 |
| Modules needing a semantic patch | 11 | 1 |
| **Module share** | **14.29%** | 1.69% |
| Commands with such a module | 183840 | 60 |
| **Draw share** | **36.37%** | 0.38% |

All 481 modules: 47 need a semantic patch; 344 would have images to promote; 118 compute modules use
`GlobalInvocationID`; 80 fetch screen textures by `gl_FragCoord`.

Every one of the 11 semantic modules bound by draws is a **`gl_FragCoord` bin lookup** (T-051: the
light and decal clusters are indexed by screen position). One of them, `ba435f66b053cc6b`, carries
169800 of the 505495 draws: the main clustered forward pass. No vertex-stage extra outputs and no
fragment stage reading view constants were among the bound modules.

## Decision input (T-105)

T-105's criterion is 15.0% on either share. The module share (14.29%) is just under it; the draw share
(36.37%) is well over it, because the one shader every lit surface uses needs the bin-lookup patch.
The view-constant rule is the structural upper bound (any matrix counts), but that does not matter
here: all semantic modules are counted for the `gl_FragCoord` bin lookup, not for view constants.

By that criterion **Route S stays the product route and Route M is dropped** (stereo-routes.md section 1,
step 3); recorded here for the owner's decision. Route S v1 runs on the rig (docs/VR_STEREO.md, "Live
results").
