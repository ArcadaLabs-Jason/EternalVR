# Engine facts: render-view build point and render entry (PLAN 1.17, static phase)

Session 2, 2026-09-25. Static analysis only; the game was not run. Input: a copy of
`DOOMEternalx64vk.exe`, Steam build 25216728 (Rev 3.2), PE timestamp 2026-08-11 22:00:44 UTC,
SizeOfImage 0x7431000, ImageBase 0x140000000, SHA-256
`69dc13e88d1c19133ead7950dc64ebcbd4a5a3f6bd6f9c336ebffe56df6a1c11`.
Tools: Python (`pefile`, `numpy`, `capstone`); the type-info reader is `tools/typeinfo/`.

All addresses are RVAs in this build. `[C]` = confirmed from the static data (a type-info record, an
RTTI record, or code read end to end). `[U]` = inferred; the dynamic phase must confirm it.

## 1. Type-info tables (PLAN 2.4, offline path)

| Fact | Value | Conf. |
|---|---|---|
| Class tables | engine: RVA 0x3AEE930, 2873 records; game: RVA 0x40B74B0, 9290 records; 12163 classes in total | [C] |
| Table descriptors | engine at 0x39BB8C8 `{enums*, 0, classes*, 0, typedefs*, 0}` (enums 0x3AD00A0, typedefs 0x3B21180), read by code at 0x1DC62AB; game at 0x38A2D08 (enums 0x40A32E0, typedefs 0x415A9D0), read at 0x163A239 | [C] layout, [U] field names |
| `classTypeInfo_t` (0x48 bytes) | +0x00 name, +0x08 superType, +0x10 int size (-1 = opaque), +0x20 variables, +0x28 variableNameHashes, +0x30 createInstance, +0x38 createModel, +0x40 metaData | [C] |
| `classVariableInfo_t` (0x48 bytes, array ends at a null type) | +0x00 type, +0x08 ops, +0x10 name, +0x18 int offset, +0x1C int size, +0x20 flags, +0x28 comment, +0x30 get, +0x38 set, +0x40 reallocate | [C] |
| Runtime anchors (Meathook route) | `idParticleParm` string, used once at 0x1DC96C7 (inside 0x1DC95F0, the `FindClassInfo` user); `idAnimatedSimple_Faust::nodeFlags_t` at 0xD8F8E8; `duration_t` has 5 users, so it is no longer a unique anchor | [C] xrefs, [U] roles |
| Cross-check against the Rev 3 dump | `idPlayer` size 0x4D358, `hideHudForCinematic` 0x84A6, `playerHud` 0x472D0: unchanged | [C] |

Not in the tables: `idGameLocal`, `idPlayerView`, `idRenderWorldLocal` and `idRenderSystemLocal`
(both opaque, size -1). The game-side view object is `idView`.

## 2. Class layouts (this build)

`renderView_t` (size 0x970) [C]

| Field | Offset | Size | Notes |
|---|---|---|---|
| `viewID` | 0x08 | 4 | per-view model suppression |
| `inhibitModelFovScale` | 0x13 | 1 | comment says it is for stereoscopic 3D rendering |
| `inCutscene`, `cameraCut` | 0x15, 0x16 | 1, 1 | |
| `fov_x`, `fov_y` | 0x28, 0x2C | 4, 4 | the projection is derived from these |
| `weaponFOVX`, `weaponFOVY` | 0x30, 0x34 | 4, 4 | |
| `nominalFOVX`, `nominalFOVY` | 0x40, 0x44 | 4, 4 | |
| `zNear`, `zFar` | 0x48, 0x4C | 4, 4 | |
| `explicitProjectionMatrix` | 0x50 | 0x40 | `idRenderMatrix` |
| `useExplicitProjectionMatrix` | 0x90 | 1 | asymmetric per-eye projection without code patches, to test |
| `vieworg` | 0x94 | 0xC | `idVec3` |
| `viewaxis` | 0xA0 | 0x24 | `idMat3`, looks down +X |
| `usesViewOriginOffset`, `localViewOrigin`, `viewOriginOffset` | 0xC4, 0xC8, 0xD4 | | |
| `viewBypass` | 0xE8 | 0x48 | `idViewBypass`: "the renderer may be given the option of creating new origin / axis data" (late latch) |
| `forceIdentityViewMatrix` | 0x130 | 1 | explicit projection used as a full MVP |

`idViewBypass` (size 0x48) [C]: `allowBypass` 0x00, `deltaViewAngles` 0x04, `angleTransform` 0x10
(`idMat3`), `baseOrigin` 0x34, `neckX` 0x40, `neckZ` 0x44.

`idRenderView` (size 0x29950) [C]: `g` 0x0 (renderView_t "set by the game"), `viewIndex` 0x28990,
`r` 0x289D0 (renderView_t "latched from 'g' at EndFrame time for renderer use"), `projectionMatrix`
0x29340, `inverseProjectionMatrix` 0x29380, `viewMatrix` 0x293C0, `inverseViewMatrix` 0x29400,
`viewProjectionMatrix` 0x29440, `previousViewProjectionMatrix` 0x29480, `inverseViewProjectionMatrix`
0x294C0, `customViewProjectionMatrix` 0x295B0 (hands/guns FOV), `customViewProjectionMatrix2` 0x29630,
`centeredViewProjectionMatrix` 0x296B0, `renderWidth`/`renderHeight` 0x298D8/0x298DC, `viewport`
0x298E8, `owningWorld` 0x29918, `cameraCutCounter` 0x29944.

`idScreenView` (size 0xA80) [C]: `screenRect` 0x0, `renderRect` 0x10, `viewIndex` 0x20, `world` 0x28,
`g` 0x30 (renderView_t), `viewGuis` 0x9A0, `guiOriginOffset` 0xA78 ("for stereo 3D").

Game-to-engine hand-off [C]:
- `gameFrameReturn_t` (0x8350): `gameFrameCount` 0x28, `players` 0x40 (`idArray<gameReturnPlayer_t,12>`),
  `mainMenuView` 0x78A0, `isPaused` 0x8219.
- `gameReturnPlayer_t` (0xA00): `valid` 0x0, `view` 0x10 (renderView_t), `visMask` 0x9F8. So player 0's
  `vieworg` is at gameFrameReturn_t+0xE4 and its `viewaxis` at +0xF0.
- `idGameSystemLocal::runFrameParms_t`: `self` 0x0, `gameReturn` 0x18. `idGameSystemLocal.mapInstance` 0x50.
- `idView` (0x38A0): `camFov` 0x50, `gameview` 0x90 (renderView_t; `fov_x` at idView+0xB8), `kickAngles`
  0x1010, `viewBob` 0x1020, `viewBobAngles` 0x102C, shake fields 0x1038-0x10C0.

`idPlayer` (size 0x4D358, super `idActor`) [C], view-related fields:

| Field | Offset | Notes |
|---|---|---|
| `fovParms` | 0x6E38 | `idPlayer::fovInterpolationParms_t` (`blendToFOV` +0x18) |
| `zoomFov`, `wantZoom` | 0x7380, 0x7410 | |
| `hideReticle`, `hideHudForCinematic` | 0x84A5, 0x84A6 | |
| `viewCallbacks`, `cameraShake`, `stepUpViewSpring` | 0x86E0, 0x8700, 0x8724 | |
| `physicsObjHavok` | 0x8A50 | `idHavokPhysics_Player` |
| `firstPersonViewOrigin` | 0x16580 | `idVec3` |
| `firstPersonViewAxis` | 0x1658C | `idMat3` |
| `deferredFirstPersonViewOrigin` | 0x165B0 | previous frame's value |
| `deferredFirstPersonViewAxis` | 0x165BC | |
| `photoModeOrigin`, `photoModeAxis` | 0x165E0, 0x165EC | |
| `modelAxis` | 0x48ECC | |
| `isViewedFirstPersonLocally` | 0x49389 | |
| cameras (`dynamicInteractionCamera`, `spectatorCamera`, `slowMotionCamera`, `springCamera`, `activeSpectacleCamera`) | 0x49530, 0x49550, 0x49570, 0x49648, 0x4D2A0 | `idManagedClassPtr` |

`idCameraView : idCamera` (0xC18): `viewOrigin` 0xBB0, `viewAngles` 0xBBC [C].
`engine_t`: `renderSystem` 0x160, `renderManager` 0x1F8 [C].

## 3. RTTI and the route to idPlayer

| Class | vtable RVA | Conf. |
|---|---|---|
| `idPlayer` | 0x2DB5698 | [C] |
| `idCommonLocal` | 0x2A6B9C0 | [C] |
| `idGameSystemLocal` | 0x2AAA730 | [C] |
| `idRenderSystemLocal` | 0x2EAD3F8 | [C] |
| `idRenderWorldLocal` | 0x2E6B8A8 (slot 0x118 is `RenderViewForIndex`, 0x18E6C00) | [C] |
| `idRenderThread` | 0x2EB59F8 (slot 0x8 = 0x1CDE500, the thread body) | [C] |

`idPlayer` view getters (the Eternal equivalents of the DOOM 2016 view slots 0x368/0x370) [C]:
- slot 0x470 -> 0x13E8950: returns `this+0x1658C` (`firstPersonViewAxis`), or `this+0x165BC` (deferred) when
  cvar `p_useNonDeferredView` (default 1) is 0.
- slot 0x478 -> 0x13E8970: the same for `firstPersonViewOrigin` / `deferredFirstPersonViewOrigin`.
- Both are leaf functions without `.pdata` entries, so resolve them through the vtable, not by unwind lookup.
  368 functions call these slots (aim, traces, audio and so on). Only the caller in section 4.2 builds the frame's view.

Player pointer: per the AP probe, `idGameSystemLocal+0x50` -> map instance, then `+0x1AF8` -> player array.
Section 4.2 agrees: the map-instance function walks `this+0x1AF8` in steps of 8 for 12 players. [C] for
the loop, [U] that `this` is `idMapInstanceLocal`.

## 4. Code sites

### 4.1 First-person view calculation (game side)

| Site | RVA | Evidence | Conf. |
|---|---|---|---|
| `idPlayer::CalculateView` | 0x14514D0 | profiling label string `idPlayer::CalculateView`; copies `firstPersonView*` to `deferredFirstPersonView*`, then calls the function below with `&firstPersonViewOrigin`, `&firstPersonViewAxis` | [C] |
| `idPlayer::CalculateViewWithoutUpdates(origin*, axis*)` | 0x1451EE0 | own error string; reads cvars `pm_thirdPerson*`, `g_freeCam`, `p_applyAnimatedCamera`, `pm_doom4BobCycle`, `g_viewNodalX/Z`, `p_useStepUpSprings`, `hands_updatePos` | [C] |
| Other direct writers of `firstPersonViewOrigin` | 0x13F04B0, 0x13F1AB0 (wrapper that calls 0x13E8990 with the two fields), 0x14007E0, 0x13C6DC0 (large player update that also writes the deferred copy) | [U] which runs per frame |

### 4.2 Render-view build point (primary camera-hook candidate)

Function 0x6A2C10 (`this` = map instance, `rdx` = `gameFrameReturn_t*`), called from
`idMapInstanceLocal::RunFrame` (0x6E1F60, label string). For player 0 it:
1. sets `players[0].valid = 1` and copies `idView.gameview` into `players[0].view` with the
   renderView_t copy routine 0x43C530 (call at 0x6A3098) [C];
2. overwrites `players[0].view.vieworg` from `idPlayer` vslot 0x478 (call at 0x6A3124, return address
   0x6A312A) and `viewaxis` from vslot 0x470 (call at 0x6A3147, return address 0x6A314D) [C]. When any
   of three conditions holds (a local flag, the byte at map instance +0xAE614, a local index other than -1),
   it takes origin and axis from a local filled by 0x1481210 instead [C]; that these are the cinematic and
   camera overrides is [U];
3. both paths meet at 0x6A31B0 (last axis store); from 0x6A31B7 onwards the view in `gameFrameReturn_t`
   holds the final origin, axis and FOV for this game frame [C].

So the camera hook can (a) patch after 0x6A31B0 and rewrite `players[0].view` (origin, axis, and FOV
through `fov_x/fov_y` at +0x28/+0x2C), or (b) wrap vslots 0x470/0x478 and filter on the return
addresses above, the approach that worked for DOOM 2016 VR. (a) also covers the cinematic path. Thread: game frame
(RunFrame), [U] which OS thread.

### 4.3 Engine submit and render entry

| Site | RVA | Evidence | Conf. |
|---|---|---|---|
| `idCommonLocal::Frame` | 0x43A120 | assert string `idCommonLocal::Frame - frameInfo->mapInstance == NULL!` | [C] |
| Screen-view render loop | 0x17E8740 (via 0x17E7E20 from `idCommonLocal::Frame`) | walks `idScreenView`s (stride 0xA80); skipped by cvar `com_skipGameRenderView`; virtual call `[vtbl+0x130]` at 0x17E932C with `r9 = &screenView.g`, on the object at `this+0x2A58` | [C] loop, [U] callee: the object is not an `idRenderWorldLocal` (its slot 0x130 takes two arguments) |
| renderView_t copy (`operator=`) | 0x43C530 | copies field by field; used by the build point, the screen-view path (0x17E7910, 0x17E9B70) and the render latch | [C] |
| Render latch and matrix build (in `idRenderWorldLocal::Render`) | 0x1CE1400 | strings `idRenderWorldLocal::Render: bad FOVs`, `idRenderView: Projection Matrix Invert failed!`; `r = g` via 0x43C530 at 0x1CE145F; sets `viewIndex`, `r_znearOverride`; builds the matrices; honours `r_skipCommits` | [C] |
| Render-view validation job | 0x1C54650 | strings `NaN in renderView vieworg!` / `viewaxis!`; reads `idRenderView.r.vieworg/viewaxis` (0x28A64 / 0x28A70); called through the job pointer at 0x39A1DC8, under the render DAG built from `idRenderThread` slot 0x8 | [C] code, [U] exact thread |

Thread picture (static): game side = `RunFrame` -> 0x6A2C10 writes `gameFrameReturn_t`;
`idCommonLocal::Frame` hands screen views to the render system; the render thread
(`idRenderThread` 0x1CDE500 -> 0x1CDAFE0 render DAG -> jobs) latches `r` from `g` and builds matrices.
Pipeline depth is [U] (open question 17).

## 5. Signatures (this build, each matches once in `.text`)

`??` masks rel32 and RIP displacements; the second form also masks structure offsets.

| Name | RVA | Signature | Conf. |
|---|---|---|---|
| Build point: player view write | 0x6A311E | `49 8B 07 49 8B CF FF 90 ?? ?? ?? ?? 49 8B CF F2 0F 10 00 F2 41 0F 11 86 ?? ?? ?? ?? 8B 40 08 41 89 86 ?? ?? ?? ?? 49 8B 07 FF 90 ?? ?? ?? ?? 0F 10 00 41 0F 11 86 ?? ?? ?? ?? 0F 10 48 10 41 0F 11 8E ?? ?? ?? ?? 8B 40 20` | [C] unique |
| Build point: gameview copy | 0x6A3081 | `48 8D 95 ?? ?? ?? ?? 41 C6 86 ?? ?? ?? ?? 01 49 8D 8C 24 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B 45 00 48 8D 94 24 ?? ?? ?? ??` | [C] unique |
| `idPlayer` view-origin getter | 0x13E8970 | `48 8B 15 ?? ?? ?? ?? B8 80 65 01 00 41 B8 B0 65 01 00 83 7A 08 00 41 0F 44 C0 48 03 C1` | [C] unique |
| `idPlayer` view-axis getter | 0x13E8950 | `48 8B 15 ?? ?? ?? ?? B8 8C 65 01 00 41 B8 BC 65 01 00 83 7A 08 00 41 0F 44 C0 48 03 C1` | [C] unique |
| `idPlayer::CalculateView` body | 0x1451538 | `4C 8D B7 ?? ?? ?? ?? 48 8B CF 41 0F 10 4E 10 48 8D B7 ?? ?? ?? ?? F2 0F 10 06 8B 46 08 F2 0F 11 87 ?? ?? ?? ?? 41 0F 10 06 89 87 ?? ?? ?? ?? 41 8B 46 20 0F 11 87 ?? ?? ?? ?? 0F 11 8F ?? ?? ?? ?? 89 87 ?? ?? ?? ??` | [C] unique |
| Render latch (`r = g`) | 0x1CE1441 | `49 89 5B 10 48 8B D1 4D 89 63 18 48 81 C1 D0 89 02 00 4D 89 73 20 45 0F 29 B3 ?? ?? ?? ?? E8 ?? ?? ?? ?? 45 33 E4 44 38 A7 ?? ?? ?? ?? 75 2A` | [C] unique |
| renderView_t copy | 0x43C530 | `48 89 5C 24 08 57 48 83 EC 20 48 8B 02 48 8B DA 48 89 01 48 8B F9 8B 42 08 89 41 08 0F B6 42 0C 88 41 0C` | [C] unique |

String anchors, which are sturdier than the byte patterns (resolve the function through `.pdata`):
`idPlayer::CalculateView`, `idPlayer::CalculateViewWithoutUpdates`, `idMapInstanceLocal::RunFrame`,
`NaN in renderView vieworg!`, `idRenderWorldLocal::Render: bad FOVs: %f, %f`,
`idRenderView: Projection Matrix Invert failed!`, `com_skipGameRenderView` (cvar object 0x5BF0FA0),
`p_useNonDeferredView` (cvar object 0x4689E10).

## 6. DOOM 2016 prior art

A DOOM 2016 VR mod targets DOOM (2016), not Eternal. Its camera hooks (the `DOOMPatchCamera` tail copying into
`rcx+0xC0`, the cutscene FOV tail, the physics-origin call site, the focus, ledge and sync-attack patches, and the
reflected-field check) match nothing in this exe [C]. The few byte arrays that do match are generic
prologues or short idioms with several hits. What carries over is the method: wrap the `idPlayer`
view-axis/origin virtuals and filter on the caller's return address. In Eternal those virtuals are slots
0x470/0x478 and the render-view caller is 0x6A2C10.

## 7. For the dynamic phase (x64dbg)

1. Hardware write breakpoint on `idPlayer+0x16580` (4 bytes): expect 0x1451EE0 or its callees once per
   game frame; record the thread ID.
2. Hardware write breakpoint on `gameFrameReturn_t+0xE4` (player 0 `vieworg`): expect the store at
   0x6A3131; record the thread and `gameFrameCount` (+0x28).
3. Breakpoint at 0x6A31B7: confirm both branches meet there, and check that editing `players[0].view`
   there moves the rendered camera (mono yaw test, PLAN 2.9).
4. Breakpoint at 0x17E932C: read the RTTI name of `rcx`'s vtable (the submit callee) and the arguments.
5. Hardware write breakpoint on `idRenderView+0x28A64` (`r.vieworg`): expect the copy 0x43C530 called
   from 0x1CE145F; record the thread (render thread or job worker) and compare frame counters with step 2
   for the pipeline depth.
6. Read `renderView_t.viewBypass.allowBypass` (+0xE8) in `r` during play: if the renderer re-derives
   origin/axis from newer input, it offers a late-latch point.
7. Try `useExplicitProjectionMatrix` (+0x90) with an asymmetric matrix in `players[0].view`, for per-eye
   projection without patching code.
8. Hardware write breakpoint on `idView+0xB8` (`gameview.fov_x`): find the FOV writer.

## 8. Scripts

Analysis scripts are kept outside the repository in `<workspace>\analysis\scripts` (PE loader,
xref and pattern search, RTTI lookup, cvar-name resolver, signature builder, DOOM 2016 signature check). The
repository copy of the type-info reader is `tools/typeinfo/extract_typeinfo.py`.
