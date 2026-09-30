# Game cvars that cut the CPU work of a render (the CPU Saver)

Retail `DOOMEternalx64vk.exe`, Steam build 25216728; all addresses are RVAs in this build. Static research only
(no rig run yet): which game cvars are most likely to cut the CPU work per render, what the game's own Video menu
sets, the provisional preset behind the launcher's "CPU Saver" and the plan to measure it. Stereo renders the
scene twice per tick, one eye after the other (`perf-baselines.md`, `perf-stereo-cpu.md`), so a saving per render
counts twice, and the critical path is each eye's command recording and submission.

## 1. How the game sets these cvars

- **The Video menu writes cvars in code, not from a data file.** Each Advanced setting has a setter
  (`idPlayerProfileShell::Set<X>(profile, level)`) that stores the level in the profile and calls the cvar setters
  with force set: SetFloat 0x3761F0, SetInt 0x376250, SetBool 0x376190, SetString 0x376020 (the one the layer
  calls). Levels 1 to 6 are Low, Medium, High, Ultra, Nightmare, Ultra Nightmare (the preset builder 0x1417C80
  fills six `videoQualityPresets_t` at 0x46852F0, preset k with every field k).
- **They run again on every profile load and every menu apply**: the profile serializer 0x141D040 calls every
  setter when the profile loads (about 0x1420420 to 0x14207E7), the menu's apply is 0x15E1600, the overall preset
  0x1421730 sets every sub-setting to one level. So a cvar the menu owns must be held at run time (the layer's
  `runtime_cvars`, written again whenever it differs), not set once.
- **A fresh profile** (0x1414180) starts at High with shadows at Ultra; 0x141B930 then steps the texture pool and
  shadows down by VRAM (unless `menu_advanced_ignoreVRAMAmount`).
- `profile_skipRenderingDefaults` ("skip setting rendering cvar values based on menu quality settings") is
  registered and never read: it does nothing in this build.
- **Flags** (registration 0x375C70): 0x1 bool, 0x2 int, 0x4 float, 0x8 cheat (added unless 0x10 "no cheat",
  0x10000 or 0x20000 is given), 0x4000 command line only, 0x8000 read-only. A change without force to a cvar
  without 0x10 is refused with "Not allowed" when the game's cheat check says so; the menu's setters and the
  layer (force true) are not. "Resetting cheat cvar: %s" (0x179B270) can put cheat cvars back to their defaults,
  which the layer's hold then undoes at the next present.
- **The value block** a cvar object points to: +0x00 the string, +0x08 the integer, +0x0C the float (the renderer
  reads floats there, for example 0x1CFC7C3), +0x40 flags with 0x40000 "modified" (list cvars such as
  `r_shadowMaxStaleFrames` are parsed again only when it is set, 0x1D04C30).
- 81 per-platform overrides (0x375790) apply only when `com_platform` is 0 or more; it is -1 on PC, so none apply.
- **Per-platform or cheat does not matter to the layer**: it writes through SetString with force, only while the
  multiplayer guard allows touching the game (single player), and only in stereo.

## 2. What the Video menu writes (L / M / H / U / NM / UN)

| Setting (setter) | Cvars |
|---|---|
| Geometric (0x14210A0) | `r_lodScale` 1.0 / 1.625 / 2.25 / 2.875 / 3.5 / 3.5; `gs_cascadeLods` 1,1,1,1,1,0; `r_lodForce` -1 x5, 0 |
| Shadows (0x1422000, values 0x1416CE0) | `r_skipPlayerShadow` 1,1,0,0,0,0; `prop_skipShadows` 1,1,0,0,0,0; `r_shadowRestrictGeomCachesToSun` 1,1,1,0,0,0; `r_shadowsDistanceFadeMultiplier` 0.8, 1.0, 1.0, 2.0, 2.5, 2.5; `r_shadowParallelMoveMargin` 0.25, 0.2, 0.05, 0, 0, 0; `r_shadowAtlasTileSize` 1024 then 2048; `r_shadowAtlasWidth` 4096 then 8192; `r_shadowAtlasHeight` 2048, 4096, 4096, 8192, 8192, 8192; `r_shadowNumAccurateSunSlices` 0,0,1,1,1,1; `r_shadowCacheHalfPrecision` 1,1,0,0,0,0 |
| Lights (0x1421350) | `r_lightDistanceFadeMultiplier` 0.8, 0.9, 1.0, 1.5, 2.0, 2.5 |
| Particles (0x1421880) | `r_particlesLightAtlasQuality` 0,1,1,2,2,3; `r_particleFadeQualityMultiplier` 0.8, 0.9, 1.0, 1.5, 2.0, 2.5 |
| Decals (0x1420CB0) | `r_decalDistanceFadeMultiplier` 0.25, 0.5, 1, 2, 4, 8; `r_decalLifetimeMultiplier` 1, 1, 1, 1.15, 1.3, 1.5; `r_decalFadeCulling` 2 |
| Directional occlusion (0x1420F20) | `r_SSDO` / `r_SSDOQuality`: off at 0, on with quality 0, 1, 2 from Low |
| Reflections (0x1421DC0) | `r_SSR` off at Low, quality 0/1/2 from Medium; `r_environmentProbes`; `r_raytracedReflectionsTemporalUpscaleQuality` 3,3,2,1,1,1; `r_SSRMinSmoothness` |
| Volumetrics (0x14225C0) | `r_lightScatteringQuality` 0 at Low, else 1 |
| Water (0x1422650) | `r_waterInterleaveUpdates` 1,1,1,0,0,0; `r_waterLodViewDistance` 16..64; caustics, hit simulation, post process off at L/M; `r_waterGridResolution` 192/256; `r_rainQuality` |
| Texture filtering (0x14221A0) | `r_materialAniso`, `r_materialAnisoCover` 1..16 |
| Motion blur quality (0x1421600) | `r_motionBlurQuality`, `r_blurRadialQuality` |
| Texture pool (0x14222D0) | `is_poolSize` 1024 to 4608 MiB, and the minimum mips |

Never written by the menu (only read by the renderer): `r_particleQualityLevel` (stays 3), `decal_MaxDecalsInRadius`,
the shadow triangle limits, `r_shadowMaxStaleFrames`, `r_shadowParallelMaxStaleFrames`,
`r_shadowPlayerMaxStaleFrames`, `r_skipShadowThrottle`, `r_shadowFadeRangeScale`, `r_contactShadows`. No gore,
ragdoll or particle-count cvar is touched by the menu. `r_umbraMaxShadowQueriesVisible` / `Invisible` have no code
reference beyond their registration (no effect), and `r_decalFilteringQuality` is probably unused.

## 3. The shadow update rate, per render

`r_shadowMaxStaleFrames` (default "0,0,1,2,2", flags 0x20010: not a cheat, not in the menu) and
`r_shadowParallelMaxStaleFrames` ("0,0,1,2", cheat) are parsed by 0x1D04C30 into 0x39AD4D8 (five ints, one per
shadow mip level of spot and point lights) and 0x39AD4F0 (four ints, one per sun cascade). The shadow update
0x1CFE2B0 increments each cached slice's age (+0x88) every time it runs, and redraws the slice (0x1D05BF0) when the
age is above the table's value for its mip or cascade (`r_shadowPlayerMaxStaleFrames` for the player's own models;
`r_skipShadowThrottle` 1 or a light's own setting at +0x8C override it). The function runs once per render, so
under Route S the age goes up twice per tick: with the default a dynamic shadow of the two largest sizes is drawn
for both eyes of every tick, although the game state (and so the shadow) is the same for both. With 1 for those two
sizes each such shadow is drawn once per tick, the rate of a flat game at the same frame rate. The eye that draws
it stays the same for a light; the other eye then sees it one tick old in the next tick, which matters only for a
caster that moves (a demon at 10 m/s moves 11 cm per tick at 90 Hz). Static shadows are cached anyway
(`r_shadowUpdateDynamicOnly` 1). Whether the caster gathering (GatherShadowCasters 0x1CA3EE0) is skipped with the
draw is not known yet: the entry built after the check keeps the cached slice when it is not redrawn.

## 4. Ranked candidates

CPU/GPU is the expected effect; "hold" says whether holding it at run time is safe (all are single player only,
the layer never touches multiplayer); "menu" whether the Video menu writes it (then a fixed value would fight the
menu or raise a lower setting, so it is held as a cap, `<=N`, written only while above N).

| # | Cvar and preset value | Default (menu range) | What it changes | CPU / GPU | Visual cost | Hold | Menu / cheat |
|---|---|---|---|---|---|---|---|
| 1 | `r_shadowMaxStaleFrames 1,1,1,2,2` | 0,0,1,2,2 | dynamic spot and point shadows of the two largest sizes redrawn every second render (section 3) | CPU and GPU: shadow draws recorded and rendered once per tick instead of per eye | nearly none in stereo: a moving shadow one tick late in one eye | yes, list cvar, re-parsed on change | no / no |
| 2 | `r_shadowsDistanceFadeMultiplier <=1` | registered 1.0 (0.8 to 2.5, fresh profile 2.0) | shadows fade out at High's distance: fewer shadowed lights | CPU (fewer lights gathered and drawn into the atlas) and GPU | distant light shadows fade earlier (Ultra and above only) | yes, as a cap | yes / no |
| 3 | `r_lightDistanceFadeMultiplier <=1` | registered 1.0 (0.8 to 2.5, fresh profile 1.0) | lights fade out at High's distance: fewer lights binned and shadowed | CPU (light culling, binning, shadow setup per view) and GPU | distant small lights fade earlier (Ultra and above only) | yes, as a cap | yes / no |
| 4 | `r_skipPlayerShadow 1` | 0 (1 at Low and Medium) | the player's hands and weapon cast no shadow on the player | CPU: a few skinned draws per shadow slice near the player | small; the menu's own Low and Medium | yes (1 is never dearer) | yes / cheat |
| 5 | `r_shadowNumAccurateSunSlices 0` | 1 (0 at Low and Medium) | the nearest sun slice uses the cached shadow geometry | CPU: no full caster pass for that slice | near sun shadows from LOD geometry | measure first: the sun slices follow each eye's frustum, the cache may be invalidated every render in stereo | yes / cheat |
| 6 | `r_shadowParallelMaxStaleFrames 1,1,1,2` | 0,0,1,2 | sun cascades 0 and 1 redrawn every second render | CPU and GPU | cascades fitted to the other eye's frustum for one render: edge artifacts possible | measure and look first | no / cheat |
| 7 | `prop_skipShadows 1` | 0 (1 at Low and Medium) | props cast no shadows | CPU (many caster draws) and GPU | visible: props lose their shadows | yes | yes / cheat |
| 8 | `r_particleFadeQualityMultiplier <=1` | 1.0 (0.8 to 2.5) | particle systems fade out at High's distance | CPU (the particle-model work, 0x1C86BA0 family, per render) and GPU | distant effects fade earlier | yes, as a cap | yes / cheat |
| 9 | `r_decalDistanceFadeMultiplier <=0.5` | 1 (0.25 to 8) | decals fade out nearer | CPU decal culling and binning per view, GPU | distant blood and scorch marks fade | yes, as a cap | yes / no |
| 10 | `r_particleQualityLevel 1` | 3 (menu never changes it) | "current particle quality level" (read in 0x1952660, 0x1953D90); effect unknown | unknown; measure | unknown; look | yes | no / cheat |
| 11 | `r_lodScale <=1.625` | 1.0 (1.0 to 3.5) | lower LODs sooner | mostly GPU (triangles); draw count unchanged | coarser models at distance | yes, as a cap | yes / cheat |
| 12 | `r_raytracedReflections 0` | the player's ray tracing choice | no ray-traced reflections | CPU (acceleration structure updates) and GPU | large on shiny surfaces | a menu choice, better left to the player (advice, not the preset) | yes |
| - | `r_skipFlares 1`, `r_skipBeams 1`, `r_skipRibbons 1` | 0 | diagnostic only: to name the stereo-only particle-model work (0x1C86BA0 / 0x1C8B930, about 100x mono) | | removes effects | no (diagnostic) | cheat |
| - | `r_umbraMaxShadowQueries*`, Umbra and job cvars | | no reference / measured before without gain (`perf-stereo-cpu.md` section 5) | none | | | |

Left out: the triangle soft and hard limits (they only throttle once exceeded), `r_contactShadows`, SSR, SSDO,
volumetrics, water and texture settings (GPU), and physics, gore and ragdoll counts (per tick, not per render, and
the game frame is the small part of a stereo tick).

## 5. The preset

`launcher/data/cpu-saver.txt`: items, one checkbox each on the launcher's Play tab (stereo only), each with a stable
id, a default, its row, its checkbox text, its tooltip and its cvars. The layer holds the cvars of the items that
are on when the launcher sets `ETERNALVR_CPU_SAVER` (absent when none is on). Gains are the ticks/s medians of
section 6 on e1m2's start view:

| Item (`launcher.ini` key `cpu_saver_<id>`) | Cvars | Default | Gain | Visual cost |
|---|---|---|---|---|
| `texture_streaming` ("Texture streaming: Only what you see") | `is_cacheGreedily 0` | on | +8.0% | mostly lossless: textures may sharpen a moment later after a fast turn or in a new area |
| `own_shadow` | `r_skipPlayerShadow 1` | off | +2.4% | hands and gun cast no shadow on you (the game's Low and Medium) |
| `near_sun_shadows` | `r_shadowNumAccurateSunSlices 0` | off | +6.3% | sun shadows near you from simpler cached geometry (Low and Medium) |
| `model_detail` | `r_lodScale <=1.625` | off | +4.4% | simpler models a little closer (Medium) |
| `decal_distance` | `r_decalDistanceFadeMultiplier <=0.5` | off | +3.7% | blood and scorch marks fade nearer (Medium) |
| `distant_shadows_lights` | `r_shadowsDistanceFadeMultiplier <=1`, `r_lightDistanceFadeMultiplier <=1` | off | about 0 here | shadows and lights fade at the High distance; meant for lit arenas |

Every item together: +23.9% (207.7 against 167.6 ticks/s). Texture streaming is on by default because it is
mostly lossless and the largest single gain; the others change the picture a little and stay the player's
choice. An older `launcher.ini` with `cpu_saver = on` turns every item on until the window saves a choice for
each; `cpu_saver = off` or none leaves the CPU Saver's items off and texture streaming on.

Why these: each is CPU work that runs once per render, is visually mild (the menu's own Low, Medium or High
values), never raises a player's setting (the float ones are caps, the boolean is the cheaper state) and is not
already held by the stereo sets. Tune the data file from further runs (no layer rebuild is needed); keep an id
once shipped, since it is the player's key.

The layer logs `cvars: CPU Saver (ETERNALVR_CPU_SAVER) asks for: ...`, then `cvars: CPU Saver holds:
...` with the cvars it found, and each cvar's first write (`cvars: r_shadowsDistanceFadeMultiplier 2.000 -> at
most 1 (reads 1.000); CPU Saver`). The launcher's settings restore puts the preset's keys back in the game's
configs after every session (the game may save a held value, as `r_SSR` once did), for every item whether it was
on or not; a key the session did not change is left alone.

## 6. Measurement plan and results

`tools/rig/cpu-cvar-ab.ps1` runs the same scene once per set, interleaved (base, then each set, repeated
`-Rounds` times, 4 by default since simulator runs fall into two speed groups), with `ETERNALVR_CPU_TIMING=1` and
the set as `ETERNALVR_DEBUG_CVARS`: Route S on OpenXR-Simulator through the rig's `rsrun.ps1`, into
`e1m2_battle`'s start with no input (the player stands, the simulated head does not move), about 60 s after the
layer's `aim: head aim on`, then `stop.ps1 -RestoreCloudFiles`, whose last line must say `cleanup done` (else the
batch stops). It then prints, per run and per set (median over rounds), ticks/s, the tick period p50/p95, eye L
(the game frame and eye L's views) and eye R wall p50/p95, the whole process's CPU per tick p50/p95, and what the
layer logged for each cvar (written, already at the value, or not found).

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools\rig\cpu-cvar-ab.ps1 -Tree <worktree> -Rounds 4 -Only saver,stale,player,shadowfade,lightfade
powershell -NoProfile -ExecutionPolicy Bypass -File tools\rig\cpu-cvar-ab.ps1 -Tree <same> -Rounds 4 -Only sunslices,sunstale,props,lowfade,decals,particles,pquality,lod,flares
powershell -NoProfile -ExecutionPolicy Bypass -File tools\rig\cpu-cvar-ab.ps1 -ParseOnly
```

`rsrun.ps1` stages the tree's `build\windows-msvc` layer (the Debug preset): every set runs the same layer, so
the comparison holds, but the Debug layer adds several percent to the serial stretches, so the absolute numbers
are a little low. The tree must be built with this branch's layer (the caps, `<=N`, need it). A run takes about
2.5 minutes, so the first line is about 60 minutes. Decide by the per-set median of ticks/s and eye L
plus eye R p50: keep a cvar in the preset when it gains at least 2% over base in the medians of four interleaved
pairs and passes a look in the headset (or eye captures, `ETERNALVR_CAPTURE_EYES`); drop what gains nothing. The
start view has few lights and no demons, so a second pass in a fight (a saved arena) is worth it once the start
view has picked the finalists. Then update `launcher/data/cpu-saver.txt`, this page's table and the preset's
"provisional" note.

**Results (2026-09-28, rig: RTX 4080 + 9950X3D, simulator, 1280x1400 per eye, Debug layer, e1m2_battle start
view, 3 interleaved rounds; ticks/s medians):** base 168.2; saver (the four cvars first proposed) 174.3 (+3.6%);
`r_skipPlayerShadow 1` 172.3 (+2.4%); `r_shadowMaxStaleFrames 1,1,1,2,2` 166.3 (-1.1%, no gain); shadow fade cap
167.4 and light fade cap 168.4 (no change in this scene). Eye L and eye R p50 fell by 0.1 ms each with the saver.
So `r_shadowMaxStaleFrames` does not act here: the stale table only ranks slices when the shadow triangle budget
is exceeded (docs/rig-findings/perf-eye-r-reuse.md on the perf-reuse branch), so section 3's once-per-tick reading
does not hold below the budget, and it left the preset. The fade caps stay for lit arenas (unmeasured there);
a second pass in a fight is still to do, as is the second list of candidates.

**Second list (2026-09-29, same rig and scene, 2 interleaved rounds; ticks/s medians against base 164.8):**
`r_shadowNumAccurateSunSlices 0` +10.4 (+6.3%), `r_lodScale <=1.625` +7.3 (+4.4%), `r_decalDistanceFadeMultiplier
<=0.5` +6.1 (+3.7%), `prop_skipShadows 1` +4.2 (+2.5%), `r_particleQualityLevel 1` +2.0, `r_particleFadeQualityMultiplier
<=1` +0.8. The first three joined the preset (the game's own Low or Medium values); prop shadows stay out (visible).
`is_update 0` set at start-up stops the map from streaming in (0 ticks/s), so the image streamer's bound needs the
cvar set after the map has loaded (`ETERNALVR_DEBUG_COMMANDS`); `r_skipGPUParticles 1` reached the map too late for
a measurement window. The combined preset is still to be measured as one set.

**Third list and the whole preset (2026-09-29, same rig and scene, 3 interleaved rounds; ticks/s medians against
base 166.8):** the CPU Saver preset as one set (the six cvars of `launcher/data/cpu-saver.txt`) 189.7
(+13.7%, the three rounds within 0.3 of each other), more than its cvars gave one by one; eye L p50 fell from
3.12 to 2.79 ms and eye R from 2.69 to 2.27 ms. `r_shadowParallelSkipDynamicModelsFromSlice 2` (moving models cast
no sun shadow from the third sun slice out) +2.7 (+1.6%, steady; a visual cost at distance, left out of the
preset for now). `r_shadowLodBias 1` +1.7 (noise), `r_skipEffectParticles 1` nothing (the cvar is not registered in
this build). Two bounds set 1 s after the map loaded (`-SetCommands`, section 6a): `is_update 0` (the texture
streamer stops) +12.3 (+7.4%), with the whole process's CPU per tick down from 59 to 49 ms, so the image streamer is
a large share of the processor's work in stereo and worth a closer look (which of its parts, and whether it runs
once per render); `r_skipGPUParticles 1` +3.4 (one noisy round of three).

**The texture streamer (2026-09-29, same rig and scene, 3 interleaved rounds; base 166.3):** `is_cacheGreedily 0`
("cache additional mips for images"; the Video menu never writes it) 179.6 (+8.0%, rounds 178.7 to 180.3), with the
process's CPU per tick down from 59 to 49 ms, the same as stopping the streamer; `is_defrag 0` after the map loaded
+6.8 (noisy, 168 to 177); `r_skipAnalyze 1` +1.6 and `r_skipFeedbackCapture 1` +1.1 (noise). So greedy caching is the
streamer's whole processor cost in stereo. It does not settle: five-minute runs held about 168 (base) and 179
(greedy off) every 10 s. Still captures after five minutes matched (equal edge sharpness; the differences were the
animated sky, the swaying banner and the gun's idle sway); fast turns and new areas are for a headset look.
The preset with it (seven cvars) against the six-cvar preset and base: 207.7 against 188.7 and 167.6 ticks/s
(+23.9% and +12.6%), tick p50 4.72 ms against 5.88 ms, the process's CPU per tick 47.7 against 59.4 ms. It joined
`launcher/data/cpu-saver.txt`.

**Bounds on top of the seven-cvar preset (2026-09-29, same scene, 3 interleaved rounds, the rounds within 1%;
base 189.4, the rig slower than earlier that night, so compare within this batch only):** `r_skipBlendedSurfaces 1`
+16.7 (+8.8%), `r_skipFog 1` +7.6 (+4.0%), `r_skipDecals 1` +4.9 (+2.6%), `r_skipGuis 1` +0.9 (noise). These remove
visible content and are not for players; they say where eye R's recording goes next: blended surfaces (particles,
glass, effects) first, then fog. Candidates to find: cvars that cut blended draws without removing effects outright,
and fog work shared between the eyes.

### 6a. Cvars that only take effect after the map loads

Some cvars cannot be set at start-up (`is_update 0` then stops the map from streaming in at all). `cpu-cvar-ab.ps1
-SetCommands 'name=<seconds>:<command>|...'` gives a set console commands on the layer's debug schedule
(`debug-commands.md`; seconds count from the player being in the map), for example:

```
powershell -NoProfile -ExecutionPolicy Bypass -Command "& .\tools\rig\cpu-cvar-ab.ps1 -Tree <worktree> -Prefix st -Rounds 3 -Sets @('base=','stream=') -SetCommands @('stream=1:is_update 0')"
```

## 7. Risks and open points

- The shadow stale counter is per render (section 3); if the drawing eye alternates in some case, both eyes could
  see one-tick-old shadows. Look at a moving caster under a spot light.
- The layer holds a plain value by comparing the integer value (+0x08), so a plain value must differ from the
  default in its integer part to be written (true for every plain value above); float values use caps, which
  compare the float (+0x0C).
- The menu writes its values again on every apply and profile load; the layer puts the caps back at the next
  present (one frame at the menu's value).
- `r_skipPlayerShadow` and the other cheat-flagged cvars may be reset by the engine's cheat reset; the hold writes
  them again.
- Nothing here has been measured: the gains may be small if the shadow work already runs off the critical path as
  parallel jobs (as Umbra's did, `perf-stereo-cpu.md` section 5).
