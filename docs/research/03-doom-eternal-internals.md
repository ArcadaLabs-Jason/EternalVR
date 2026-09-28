# 03: DOOM Eternal / id Tech 7 internals and reverse-engineering resources

Status: research notes, 2026-09-25. Nothing here has been run against the game yet (no Windows
rig or game binary on the research machine). Tags: **[C]** = confirmed from source code, official
documentation or a primary data dump we have locally; **[U]** = unverified, inferred, or
confirmed only for an older build. Every [U] that matters has a matching task in the RE task list
at the end.

Reference material pulled for this topic lives in `reference/idtech7/` (docs, type/cvar dumps,
catalogs) and `reference/_cache/` (cloned repos, PDF). See `reference/idtech7/MANIFEST.part.md`.

---

## 1. Summary

- **The engine describes itself.** DOOM Eternal ships id's generated reflection tables
  (`idTypeInfoTools`, `classTypeInfo_t`, `enumTypeInfo_t`) for about 12,000 types, with every
  field's name, type, offset, size and designer comment [C: Meathook dump, 12,062 type names;
  32,365 distinct field names]. Meathook and the DE Advanced Options mod both resolve struct
  offsets by name at runtime through this, and struct layouts have survived patches that moved
  every code RVA (e.g. `idPlayer::hideHudForCinematic` at +0x84A6 on Rev 3 in 2024 and still read
  there by a Sep 2026 tool on Rev 3.2) [C]. This should be the backbone of how we survive game patches.
- **Everything else can be found from strings and MSVC RTTI.** Meathook finds its engine entry
  points with scanners anchored on assert/log strings and on RTTI class names
  (`.?AVidRenderThread@@`), not on raw code bytes [C].
- **id Tech 7 has a dormant VR/stereo subsystem.** Retail cvars include `vr_enable` ("Enable to
  run in VR mode"), `vr_dummyDevice`, `vr_controllerMovement`, `vr_dominantHand`,
  `vr_metersToGameUnits`, `stereoRender_separation` / `screenSeparation` / `guiOffset` /
  `swapEyes` (in metres), and the typeinfo still contains `idVRInput` and `idVRController`
  [C: names exist]. Whether any backend or render path behind them survives in the shipping
  executable is unknown [U] and is the first thing to test on the rig.
- **Many VR comfort and presentation needs are plain cvars**, not code: `pm_noBob`,
  `view_skipKicks`, `view_skipShakes`, `view_skipBlur`, `r_motionblur`, `hands_offsetX/Y/Z` +
  `Pitch/Yaw/Roll`, `hands_fovScale`, `hands_show`, `g_showHud`, `hud_drawPerspective*`,
  `r_znear`, `rs_forceResolution` [C: names and descriptions]. Retail restricts console access;
  the restriction is a single argument byte that three independent tools patch the same way [C].
- **UI is idSWF (a Flash interpreter) drawn into its own full-resolution 8-bit premultiplied
  render target, composited during the tone-mapping compute pass** [C: Coenen frame study;
  eternalmods wiki; typeinfo]. That separate target is the natural capture point for an OpenXR
  quad layer.
- **Current game state (Sep 2026):** Steam build 25216728 (2026-09-17). Retail executable is
  `DOOMEternalx64vk.exe` 6.66 Rev 3.2 (2026-08); Steam now launches `launcher/idTechLauncher.exe`,
  and modded play runs a second executable, `doomSandBox/DOOMSandBox64vk.exe` [C]. The game is
  still being patched in 2026 (April, May, August, September), so hardcoded addresses are a
  non-starter.

---

## 2. Game build facts

| Fact | Value | Evidence |
|---|---|---|
| Steam app id | 782330 (idStudio: 2545650) | [C] Steam API; wiki |
| Install dir | `steamapps/common/DOOMEternal` | [C] SteamCMD `installdir` |
| Steam launch target | `launcher/idTechLauncher.exe` (Electron app: ffmpeg, libEGL, vk_swiftshader) | [C] SteamCMD `config.launch`; depot file list |
| Retail game exe | `DOOMEternalx64vk.exe` (x64, Vulkan-only) | [C] |
| Modded-play exe | `doomSandBox/DOOMSandBox64vk.exe` (no achievements or multiplayer, per the modding tools; corrected 2026-09-25: the installed exe carries the retail exe's lobby, invite and PvP code and shares `base\`, so it is not assumed offline, and it is not a v1 target, T-109) | [C] EternalPatcher.def, EternalModInjectorShell, wiki |
| Current public build | 25216728, updated 2026-09-17 | [C] SteamCMD API |
| Denuvo | Removed in 6.66 Rev 2.2 (2023-09-05); exe shrank from ~465 MB to ~77 MB | [C] Steam notes; downpatcher sizes |
| Rev 3 | 2024-08-08: PC Mod Preview beta + idStudio beta (sandbox exe introduced) | [C] Steam notes |
| PC Mods Update | 2025-08-07: launcher becomes default; mods official, 650+ mods | [C] Steam notes |
| Rev 3.1 | Steam 2026-04-03 (ModuleMemorySize 121806848) | [C] LiveSplit ASL, EternalPatcher.def |
| Rev 3.2 | Steam 2026-08-11/18 (ModuleMemorySize 121835520; SizeOfImage 0x7431000) | [C] ASL, AP probe |
| Later | 2026-09-03 and 2026-09-17 updates (new sandbox exe 2026-09-09; retail exe hash unchanged in EternalPatcher.def) | [C]/[U] |
| GOG release | 2026 (EternalPatcher.def lists `gog19032026`...) | [C] |
| Downpatching | `mcdalcin/DoomEternalDownpatcher` (depot manifests per version, Steam `download_depot`) | [C] |
| Launch options | `+cvar value` on the command line is the id convention (`+com_skipIntroVideo 1`); whether the launcher forwards them to the game exe is unverified | [U] |

The 2026 patches matter for our design: the Archipelago project had to add a second build profile
for Aug 2026 because its `isLoading`/`isInGame`/`cutsceneId` RVAs moved by ~0x5480 bytes between
Rev 3.1 and Rev 3.2, while the `idPlayer` field offsets it reads did not move [C]. We must support
at least two executables (retail and sandbox) and expect several rebuilds a year.

---

## 3. Meathook (m3337ho0o0ok)

Cloned to `reference/_cache/meathook/` (brongo mirror, HEAD e12dd75, v7.2; no license file in
the repo, so we study it and cite it but do not copy code). Author: chrispy. v7.1 released
2021-11-16, v7.2 GitHub release 2022-10-24 (game 6.66). Successors: `snowzzrra/Meathook-AP`
(Aug 2026) adds an Archipelago RPC runtime but leaves the scanners untouched, and the DOOM
Eternal Archipelago project (`reference/_cache/ap-mod/`) installs "verified Meathook v7.2" for
"Steam 6.66 Rev 3.1" in Sep 2026 [C]. So v7.2's scanners appear to still resolve on 2026 builds
[U for Rev 3.2]. No independent maintained successor exists on GitHub.

### 3.1 Loading and bootstrap [C]

- Proxies `XINPUT1_3.dll` (the game imports it); exports `XInputGetState`, `XInputSetState`,
  `XInputGetCapabilities` and forwards to System32. On Linux/Proton it needs
  `WINEDLLOVERRIDES="XINPUT1_3=n,b"`.
- `DllMain` hot-patches `kernel32!GetSystemInfo`; the first call triggers the critical scan (a
  Denuvo-era trick to wait until the image was unpacked).
- Swaps the gamelib-initialize pointer; after the engine's own init it runs the late scans and
  `meathook_init()`.
- Replaces the callback pointer used by `idCommonLocal::Frame` (found via the string
  `idCommonLocal::Frame - frameInfo->mapInstance == NULL!`) to get pre/post-frame callbacks on the
  game thread.
- Hooks `idRenderThread` (RTTI vtable slot 1), `idInputLocalWin32` event queue (RTTI vtable slot
  0x60/8, decoding `SE_KEY`/`SE_CHAR` through the engine's reflected enums), file-system and error
  handlers, and a debug-HUD render callback for its own overlay GUI.

### 3.2 Finding engine objects [C]

The full table is in `reference/idtech7/signature-catalog.md`. Key points:

- Most scanners are **string-anchored**: "`lea rcx, [rip+x]` where x points at this exact string,
  then the next `call`". Examples: `duration_t` -> `idTypeInfoTools` global;
  `idParticleParm` -> `FindClassInfo`; `idAnimatedSimple_Faust::nodeFlags_t` -> `FindEnumInfo`;
  `idRenderModelGui::AllocTris: Trying to alloc triangles...`; `idImageManager::ScratchImage
  called with empty name`; `InternalCallEvent() doesn't handle event %s.`
- **RTTI map**: scan the image for all MSVC `TypeDescriptor`s (via the `type_info` vtable), match
  Complete Object Locators, and build `name -> vtable`. Any polymorphic class is then reachable as
  `get_class_vtbl(".?AVidPlayer@@")`.
- **Globals block**: `idCVarSystem`, `idCmdSystem` and `idFileSystem` pointers sit at +8, +16,
  +24 from the memory-system global.
- **Behavioural searches**: the entity table inside `idGameLocal` is found at runtime by looking
  for `player1` at slot 0 and `world` 16382 slots later.

### 3.3 Console, commands and cvars [C]

- `idCmdSystemLocal` vtable (as used): +0x18 `AddCommand`, +0x40 `ExecuteCommandText`, +0x48
  append to buffer, +0x78 `ExecuteCommandBuffer`. `idCVarSystem` vtable +0x20 is `Find(name)`.
- `idCVar { cvarData_t* data; cvarData_t dataStorage; idCVar* next; }` with
  `cvarData_t { valueString, valueInteger, valueFloat, ..., name, resetString, description, flags,
  valueMin, valueMax, valueStrings, valueCompletion, onChange }`. Setting a value writes the
  fields, ORs `0x40000` (modified) and calls the onChange callbacks.
- **Console unlock**: the engine calls an `idCmdSystem` virtual (vtable +0x10) with a flag that
  restricts dev commands. Meathook replaces that slot with a no-op; DE Advanced Options patches
  the preceding `mov edx, 1` to `mov edx, 0` (`consoleUnlockAltSig`); EternalPatcher's
  "unrestrict cvars & console commands" patch (`084C8B0EBA01` -> `...BA00`) is the same edit.
  A second EternalPatcher patch forces a flag bit (0x10) into a cvar-setting function to
  "unrestrict cvars & launch parameters" and a third unrestricts binds. The eternalmods wiki
  states that "Console Commands are unlocked by default after running the Eternal Mod Injector".
- There is no `com_allowConsole`-style launch switch in the cvar list; `in_terminal` (terminal
  output) and `win_consoleVisibility` exist [C: cvar names]. Unlock = binary patch or in-process
  vtable/argument patch.

### 3.4 Type-info reflection [C]

- `idTypeInfoTools::FindClassInfo(name)` -> `classTypeInfo_t { char* name; char* superType; int
  size; ...; classVariableInfo_t* variables; uint64* variableNameHashes; createInstance;
  createModel; metaData }`.
- `classVariableInfo_t { type, ops, name, int offset, int size, int flags, comment, get, set,
  reallocate }`. `ops` carries pointer/array decoration, `comment` is the designer comment
  (Advanced Options' dumps show them, e.g. `hideReticle`: "TODO: replace this concept and do this
  through HUD state instead; this is a temporary fix for beta.").
- `FindEnumInfo(name)` -> `enumTypeInfo_t { name, flags, type, values[] {name, value} }`.
- Meathook adds convenience: nested lookup by path (`get_nested_field_by_name(obj, "idPlayer",
  "focusTracker", "focusEntity")`), per-call-site offset caches, superclass walking, `mh_type
  <class>` to print a class, `mh_kw` to keyword-search every type/field/comment/cvar/eventdef,
  and header/IDC generation. Separately the engine's `idTypeInfo` hierarchy (spawnable classes,
  `Spawn`, `CreateInstance`, `super`, `typeNum`) is still present, as in DOOM 3.
- `engine_t` is itself reflected: `engine_t::typeInfoTools`, `engine_t::debugHUD`, render system,
  console, cursor, etc. are reachable by field name from the `idTypeInfoTools` global.

Precedent from id Tech 6: DOOM 2016 carries the same reflection (a hook target there can be validated
by comparing against the reflected field record of `idPlayer::inhibitFlags`), so it exists one
generation earlier and is usable in practice. [U]

---

## 4. Other RE resources (what exists, for which build)

| Resource | What it gives us | Build | License |
|---|---|---|---|
| DE Advanced Options Mod (`SteamKaibz/DE_AdvancedOptionsModPublic`) | About 90 IDA-style signatures for Rev 3 incl. `idPlayer::ProcessInput`, FOV target/lerp, sync (glory kill) start/end, usercmd button send, `idSWF` sprite render, GUI draw, cvar/cmd systems, typeinfo; typeinfo-generated structs with offsets and comments (`idPlayer`, `idHUD`, `idDeclWeapon`, `idSWF*`...); MinHook; msimg32 proxy; ImGui overlay | 6.66 Rev 3 vanilla + sandbox (Nov 2024) | BSD-2 |
| Meathook (+ Meathook-AP fork) | Scanners, reflection, console, input/frame hooks; type, cvar and property name dumps | 6.66 (works on Rev 3.1 per AP) | none stated |
| KEX full cvar list | 7,397 cvars with description, value, type/range | 2024 | MIT |
| EternalPatcher.def (EternalBasher) | Exe patch patterns + MD5 of every Steam/GOG/MS Store/sandbox exe to Sep 2026 | to 2026-09-17 | GPL-3.0 |
| LiveSplit ASL (`loitho/doom-eternal`) | Per-version RVAs: loading, in-game, `cutsceneID`, `canMove`, position | to Rev 3.2 | none stated |
| DOOM Eternal Archipelago | Live 2026 use of Meathook + injector; build-profile keyed probe; `idGameSystemLocal->mapInstance(+0x50)->player(+0x1AF8)` | Rev 3.1/3.2 | none stated |
| FearlessRevolution CE table (SunBeam et al., thread t=11889) | Console/dev-command enabling scripts, pointer paths | up to 6.66 Rev 2.2 per thread; Rev 3 status unclear | forum |
| `pepe-god/DOOM-Eternal-Cheat-Table` | CE table + exe copy "for reversing" | 2026-02 push | GPL-3.0 |
| eternalmods wiki | File formats, RE notes, console, idStudio, SWF modding | ongoing | wiki |
| idStudio docs | Official mod structure, decls, console, release notes 2024-2026 | ongoing | official |
| Vk3DVision (helifax) | Closed Vulkan stereo driver; DOOM Eternal "FullVR" profile v0.90 (Dec 2024), stereo instancing, no motion controls | 2023-2024 | driver closed |
| Coenen frame study; SIGGRAPH 2020 "Rendering the Hellscape of DOOM Eternal" | Pass order, UI target, weapon handling, Umbra + GPU culling | 2020 | author/id |

No public "SDK" or full class-dump header for the current build exists beyond the Advanced Options
headers (partial, Rev 3) and Meathook's name lists. We will generate our own with `mh_type`-style
dumps on the rig.

---

## 5. id Tech lineage: what persists from DOOM 3 BFG

From the eternalmods RE notes (chrispy) plus our typeinfo checks [C unless marked]:

- idLib math, geometry, `idRenderMatrix` ("unchanged"), `idStr`, `idHashIndex`, threads,
  parser/lexer, `idBitMsg`: essentially the same.
- **SWF GUI code "basically identical to id Tech 4 and 5".** DOOM Eternal's HUD and menus are
  `idSWF` with `idSWFWidget_*` widgets (weapon wheel = `idSWFWidget_WeaponWheel`).
- `sysEvent_t` and usercmd generation "still very similar"; typeinfo has `idUCmdTracker`,
  `playerInput_t`, `idBotControllerInterface_UserCmd` [C]; exact `usercmd_t` layout [U].
- `renderView_t` still exists; field names `fov_x`, `fov_y`, `vieworg`, `viewaxis` exist in the
  property list [C], BFG's `viewEyeBuffer`/`stereoScreenSeparation` do not [C: absent from names],
  so Eternal's stereo design differs from BFG's [U].
- Player view: BFG's `idPlayerView` is replaced by `idView` (shakes, blur, DoF, screen effects,
  damage), `idFieldOfView`, `idGameViewInfo`, `gameView_t`, `localView_t`, `idPlayerViewCallbacks`
  [C: type names]. Player fields `viewAngles`, `cmdAngles`, `deltaViewAngles`,
  `firstPersonViewOrigin`, `firstPersonViewAxis`, `eyeOffset`, `viewBob*`, `kickAngles` exist [C].
- Menus keep the `idMenuScreen_*` / `idMenuManager_*` / `idMenu` naming [C].
- Event dispatch was regenerated per class; job lists became JobChains; `idRenderModelManager`
  -> `idStaticModelManager`; `srfTriangles_t` -> `idTriangles`.

`reference/idtech7/bfg-source-index.md` links each relevant BFG file with the VR reason to read it.

---

## 6. VR needs mapped to engine mechanisms

Mechanism key: **hook** (code hook located by string/RTTI/pattern), **TI** (typeinfo field
read/write by name), **cvar**, **decl** (data mod via idStudio/injector), **VK** (Vulkan-level).
The needs checklist is the set of hooks a DOOM 2016 VR mod needs (camera, weapon, HUD).

| VR need | Candidate mechanism | Evidence | Conf. |
|---|---|---|---|
| Console/cvar access in retail | hook: patch `idCmdSystem` restriction arg (or no-op vtable +0x10); cvar API via `idCVarSystem::Find` | Meathook, Advanced Options, EternalPatcher all do it | High |
| Test built-in VR/stereo | cvar: `vr_enable`, `vr_dummyDevice`, `stereoRender_*` | cvar names/descriptions (KEX 2024); `idVRInput` type | Low (probably dead code) |
| Per-eye camera origin/orientation | hook the render-view build (player `CalculateRenderView` equivalent) or the `idRenderWorld::RenderScene`-style submit; TI for `renderView_t.vieworg/viewaxis`; DOOM 2016 precedent: `idPlayer` view-axis virtual (slot 0x368) | BFG `EmitStereoEyeView`; field names; DOOM 2016 precedent | Medium |
| Asymmetric per-eye projection | hook projection build; TI fields `explicitProjectionMatrix`, `customViewProjectionMatrix`, `forceIdentityViewMatrix` look like existing overrides; cvar `r_customViewProjectionMatrixDepthBias` | property/cvar names only | Low-Med |
| FOV | cvar `g_fov`; hook `GetFovTargetVal` / `idPlayerFovLerp` to stop zoom and glory-kill FOV changes; cvars `sync_autoFOV 0`, `p_ForceFov` | Advanced Options sigs; cvars | High |
| Near plane / scale | cvar `r_znear` (0.06 m), `pm_normalViewHeight` 1.657 m; unit scale `vr_metersToGameUnits` 1.0; world units look like metres but many values are exact inch conversions | cvar dump | Med ([U] units) |
| Disable view bob, kicks, shakes, blur | cvar `pm_noBob`, `g_setting_hands_bob 0`, `view_skipKicks 1`, `view_skipShakes 1`, `view_skipBlur 1`, `view_skipDamageEffect`, `view_enableHelmetFX 0`, `r_motionblur 0`, `g_autoMotionBlurOnGK 0`, `r_dof 0`, `r_chromaticAberration 0`, `r_vignette 0`, `r_filmGrainRatio -1` | cvar descriptions | High (names); defaults [U] |
| Head-decoupled aim | hook `idPlayer::ProcessInput` / usercmd path; TI `viewAngles`/`cmdAngles`/`deltaViewAngles` | Advanced Options sig; BFG usercmd | Medium |
| Controller buttons/sticks | XInput-level virtual gamepad (the game already reads XInput) or `idUsercmdGenLocal` send-button hook or input event queue (RTTI `idInputLocalWin32`) | Meathook input hook; Advanced Options sig | High |
| Motion-aim (absolute aim deltas) | cvar `g_setting_motion_aim` exists, a possible native path for gyro-style deltas | cvar only | Low |
| Locomotion direction from controller/head | hook player physics input or rotate usercmd move vector | BFG `Physics_Player`; `vr_controllerMovement` cvar | Medium |
| Weapon model pose | cvars `hands_offsetX/Y/Z`, `hands_offsetPitch/Yaw/Roll`, `hands_updatePos` (static offset only); per-frame: hook `idHands` update (an `idHands` update wrapper worked in DOOM 2016) | cvars; types `idHands`, `idHandsWeaponLagData_t` | High (static) / Med (hook) |
| Weapon projection matches world | cvar `hands_fovScale` (gun drawn with its own FOV); zoom FOVs in `idDeclWeapon::zoomInfo_t` (`zoomedFOV`, `zoomedHandsFOV`) | cvar; decl struct dump | Medium |
| Weapon sway/lag | cvar `hands_weaponLagEnable 0`, `hands_bobOffsetZMax`, `hands_weaponBobMaxVel` | cvars | High |
| Muzzle / aim ray from weapon | TI `muzzleOrigin`/`muzzleAxis`; decl flag `useMuzzleAsFireAxis`; cvar `hands_useDeferredViewAimMuzzleTraces`, `hands_drawMuzzlePos` for debugging | names; the `useMuzzleAsFireAxis` branch was hookable in DOOM 2016 | Medium |
| Hide hands during glory kill / show | cvar `hands_show`, `hands_hideDuringSyncInteraction` | cvar | High |
| Glory kill / sync detection | hook `p_StartSync` / `syncEnd` (Advanced Options sigs); TI `idPlayerMechanicSync`, `idSyncAttack*` | sigs; types | Medium |
| Cutscene / cinematic detection | TI `idPlayer::hideHudForCinematic`, `idPlayer::hideReticle`; `cutsceneID` global (per-version RVA in ASL; find its writer); camera entities `idCinematicCamera`, `idAnimCamera`, `idInteractionCamera` | Advanced Options dumps; AP probe; ASL | High |
| Pause/menu state | TI `idHUD::gameWasPaused`, `currentHudMode`, `menus`; hooks on `idMenuScreen_*` Show/Hide (RTTI vtables) | dumps; DOOM 2016 precedent | High |
| HUD on/off, alpha | cvar `g_showHud`, `g_setting_hud_show`, `hud_globalAlpha`, `swf_skipRender` | cvars | High |
| HUD in 3D | cvar `hud_drawPerspective` + `...Angle/CenterDepth/OffsetX/Y/Z`; `stereoRender_guiOffset` | cvar descriptions | Low-Med |
| UI capture for OpenXR layer | VK: grab the 8-bit UI render target before the tone-map composite; or hook `idSWF` sprite render / `idRenderModelGui` draws | Coenen; sigs; Meathook GUI hooks | Medium |
| Resolution control | cvar `rs_forceResolution` (0..1), `r_enableResolutionScale`, `r_mode`, `r_windowWidth/Height` | cvars | Medium |
| AA | cvar `r_antialiasing` (0 off, 1 TSSAA, 2 DLSS); per-eye history if TAA stays on | cvar; Coenen | High |
| Frame pacing | cvars `com_adaptiveTick*`, `com_fixedTic`, `r_swapInterval`, `r_useSMP`; async compute overlaps frames | cvars; Khan interview | Medium |
| Culling for the second eye | cvars `r_umbraJobKickoff`, `r_skipModelGPUCulling`, `r_skipGPUTriangleCulling` for experiments; real fix: cull with a union frustum | SIGGRAPH slides (Umbra CPU + GPU triangle occlusion culling); per-eye visibility corruption reported in DOOM 2016 VR | Medium |

---

## 7. Typeinfo-driven strategy for surviving patches

The goal is that a game patch changes nothing in our code unless id renames a field or a class.

1. **Identify the build, but do not depend on it.** Read PE timestamp, SizeOfImage and file
   hash; log them; use them only to pick known-good fallbacks and to label bug reports.
2. **Tier 1 anchors (no byte patterns):**
   - `idTypeInfoTools` global via the `duration_t` string xref; `FindClassInfo` via the
     `idParticleParm` xref; `FindEnumInfo` via `idAnimatedSimple_Faust::nodeFlags_t`. As a
     cross-check, `FindClassInfo` must return a `classTypeInfo_t` whose `name` equals the query
     for a handful of known classes.
   - RTTI map built once at startup (TypeDescriptor -> COL -> vtable), so every polymorphic class
     (`idPlayer`, `idHands`, `idWeapon`, `idRenderThread`, `idInputLocalWin32`, `idMenuScreen_*`,
     `idSWF`...) has a vtable by name.
   - `engine_t` fields by name from the typeinfo global (render system, console, debug HUD,
     cursor, ...). `idGameLocal` / `idGameSystemLocal` via their typeinfo-described members or
     string xrefs.
   - Cvars via `idCVarSystem::Find("name")`; commands via `idCmdSystem`.
3. **Tier 2: virtual slots.** For hooks on virtual functions, get the vtable from RTTI and the
   slot index from a small disassembly check at startup (e.g. the slot whose body references a
   known string or calls a known function). Store slot indices per build only as a hint.
4. **Tier 3: string-anchored code scans** for non-virtual functions (the Meathook style): find the
   string, find the unique `lea r??, [rip+str]`, walk to the containing function start via the
   PE `.pdata` unwind table (exact, unlike prologue guessing). Prefer asserts and log strings
   that are unlikely to change.
5. **Tier 4: byte signatures** (Advanced Options style) only where nothing better exists; keep
   them short, wildcard all displacements and immediates that are offsets, and require a unique
   match inside `.text`.
6. **Field access by name, cached.** One resolver: `field<T>(obj, "idPlayer", "playerHud",
   "gameWasPaused")` -> walks `superType` and nested field types, caches the offset, and
   validates `size == sizeof(T)` and, where useful, the `type` string. Refuse to start a feature
   whose fields do not resolve.
7. **Every located thing is validated before use** (function contains the anchor string; vtable
   belongs to the right RTTI name; field type string matches). A failed feature is disabled with a
   log line, never a crash; the mod degrades to "no VR for this feature" rather than failing
   to load.
8. **Self-test command** (`evr_selftest`) that prints every anchor, slot and field it
   resolved, so a new build can be triaged from one log.
9. **Offline dump per build**: on the rig, dump all classes we touch (Meathook `mh_type`-style)
   into `reference/idtech7/typeinfo/<build>/` so diffs between builds show exactly what moved.

What typeinfo does **not** give us: function addresses, virtual slot numbers, local variables,
non-reflected structs (much of the renderer back end, possibly `usercmd_t` and parts of
`renderView_t`). Those need tiers 2-4 and a per-build check.

---

## 8. UI rendering and capture options

What is known [C]:

- The HUD, menus, dossier/codex, automap overlays and weapon wheel are idSWF movies (custom
  Flash interpreter: typeinfo `idSWF*`, `idSWFWidget_WeaponWheel`, `idHUDMenu_Dossier`,
  `idMenuScreen_Hud`; wiki: "the layout of DOOM Eternal's HUD is rendered by a custom flash
  interpreter"; ActionScript subset can set cvars). HUD element shapes come from textures; some
  menus are populated from decls (`idDeclHUDElement`, `idDeclUIColor`, `idDeclWeaponReticle`).
- idSWF draws through `idRenderModelGui` (Meathook and Advanced Options both locate
  `DrawStretchPic`, `DrawString`, `DrawChar`, `DrawFilled`, `AllocTris`, virtual width/height).
- In the frame, UI is "usually the last geometry pass", rendered into a **secondary LDR (8-bit)
  full-resolution render target with colour premultiplied by alpha**, then composited on top
  during the single tone-mapping compute shader (with an intensity boost) (Coenen, 2020).
- Separately, in-world GUIs (screens on entities, `idGuiEntity`, floating text) render in the
  3D scene (`r_skipInGameGuis` toggles them).
- Dormant options: `hud_drawPerspective` (draw HUD elements "with a perspective"),
  `stereoRender_guiOffset` ("shift guis so they don't appear at infinity in HMDs", metres).

Capture options, in order of preference:

1. **UI render target -> OpenXR quad layer (Vulkan level).** Identify the UI target (full-res,
   8-bit RGBA, premultiplied, written by the GUI pass and read by the tone-map compute); copy it
   into an XR swapchain each frame and present it as a quad (head-locked for HUD, world-locked
   for menus); stop it from being composited into the eye images (clear it after the copy, or
   zero the UI blend in the tone-map dispatch). Captures everything the game shows in 2D with one
   mechanism and no per-widget work. Risk: HUD elements sit at screen edges and need reflowing
   (`swf_safeFrame`, `hud_drawPerspective` offsets may help); the target has to be recognised
   reliably on each build (RenderDoc task below).
2. **Engine-level routing (hooks on `idSWF` render / `idRenderModelGui`).** Lets us split the
   weapon wheel, crosshair, subtitles and HUD into separate layers or attach them to hands
   (hand anchors). More hooks, more per-build risk; do it after option 1 works.
3. **Built-in perspective HUD** (`hud_drawPerspective 1` + offsets) drawn into the UI target,
   presented as in option 1. Cheap experiment; unknown quality.
4. **Built-in stereo GUI offset** (`stereoRender_guiOffset`) only helps if the built-in stereo
   path exists.

Menus that hide the world (`hud_disableMenuVisbilityMask` controls "hiding the world when showing
the dossier") suggest full-screen menus should become a world-locked panel with the scene frozen
behind it rather than head-locked.

---

## 9. What data mods can do without code

idStudio (Steam, app 2545650) and the launcher's mod portal are official; legacy zips go through
EternalModInjector / Atlan Mod Loader. Decls are text (`declType( X ) { edit = { ... } }`) and can
be hot-reloaded with Meathook's `mh_reload_decl`. idStudio mods can change decls, textures,
models, animations, strings, sounds, maps and SWFs [C: wiki/official FAQ]. They cannot change
code.

Useful for us [U unless noted]:

- `idDeclWeapon::zoomInfo_t` (`zoomedFOV`, `zoomedHandsFOV`, sensitivity scales) [C: struct
  dump]: neutralise ADS zoom FOV changes that would be uncomfortable in VR.
- `idDeclWeaponReticle` / `idDeclHUDElement` / SWF edits: remove or resize the crosshair and
  reposition HUD blocks for a VR-friendly layout.
- `idDeclViewShake` / `idDeclAdvancedViewShake` / `idDeclCameraTrigger::idTrigShakeView`:
  zero out shakes at the data level if `view_skipShakes` misses some.
- `idDeclPlayerProps`: player tuning.
- `devMenuOption` decls: load test maps directly (idStudio docs) for a repeatable RE sandbox.

Most comfort items are already cvars (section 6), which we can set at runtime without shipping
any data. A decl mod is only worth shipping for things with no cvar (zoom FOV, reticle, HUD
layout).

**Coexistence** [C]: code injection and data mods are independent. DE Advanced Options ships a
build for the sandbox exe; the DOOM Eternal Archipelago project runs Meathook together with an
EternalModInjector data package on Rev 3.1. Caveats: modded play via the launcher runs
`DOOMSandBox64vk.exe` (different binary, different hashes, no achievements); the legacy injector
patches the retail exe in place (EternalPatcher); Meathook occupies the `XINPUT1_3.dll` proxy slot,
so we should not also use that name if we want to coexist with it.

---

## 10. Implications for our design

1. Build a small "engine access" module first: RTTI map, typeinfo resolver, string-xref scanner
   with `.pdata` function bounds, cvar/cmd access, console unlock. Everything else depends on it,
   and it can be tested headless against each new exe (static scan) before any gameplay code.
2. Support two targets from day one: `DOOMEternalx64vk.exe` and `DOOMSandBox64vk.exe`. Detect
   which one loaded us; the same anchors should work in both (Advanced Options only needed
   separate sandbox signatures for three functions).
3. Do not ship RVAs. Hardcoding dozens of RVAs and signature-checking them works for a frozen game
   such as DOOM 2016, but not for one patched four times in 2026.
4. Apply comfort settings through cvars at map start and re-apply after menus (settings menus
   write some of them back): bob, kicks, shakes, blur, motion blur, DoF, vignette, film grain,
   chromatic aberration, weapon lag, glory-kill FOV/blur.
5. Test the dormant VR path before designing our own stereo renderer. If `vr_enable` still drives
   a stereo render path, per-eye culling and TAA are handled by the engine and our job shrinks to
   feeding poses and swapchains. If it is dead code, fall back to our own per-eye submission and
   plan for visibility (Umbra/GPU occlusion culling) with a union frustum, per-eye TAA history, and
   per-eye UI.
6. UI: plan for option 1 (capture the separate UI target into an OpenXR quad layer) as the MVP.
7. Input: start with an XInput-level virtual gamepad plus a view-angle override for head-decoupled
   aim; move to usercmd-level hooks only where the gamepad path cannot express something
   (absolute aim, per-hand actions).
8. Cinematics: key comfort switching (fixed-screen vs. head-tracked) off `hideHudForCinematic`,
   sync start/end hooks and camera-entity activity; verify on the rig which cutscenes set which.
9. Keep a per-build typeinfo dump and self-test log in the repo so patch-day triage is a diff.

---

## 11. RE tasks for the gaming rig (prioritised)

Tools: x64dbg (+ ScyllaHide not needed post-Denuvo), Ghidra or IDA (IDA sig maker / Ghidra
FunctionID), ReClass.NET, Cheat Engine, RenderDoc (Vulkan capture; launch the exe directly, not
through the Electron launcher), Meathook v7.2 as a live typeinfo console.

1. **Inventory the install** (15 min). Record exe/sandbox exe SHA-256, PE timestamp,
   SizeOfImage; list imports (confirm `XINPUT1_3.dll`, check for any OpenVR/OpenXR/LibOVR
   import or string); note whether Steam launch options reach the game through the launcher.
2. **Meathook on the current build** (Ghidra not needed). Drop v7.2 `XINPUT1_3.dll` into the
   retail folder; confirm it loads on Rev 3.2; if it does, run `mh_type` on: `idPlayer`, `idHands`,
   `idWeapon`, `idView`, `idFieldOfView`, `renderView_t`, `idRenderView`, `gameView_t`,
   `localView_t`, `idGameViewInfo`, `idHUD`, `idMenu`, `idSWF`, `idPlayerMechanicSync`,
   `idSyncAttack`, `idCinematicCamera`, `idVRInput`, `idVRController`, `engine_t`,
   `idDeclWeapon`, `idDeclPlayerProps`; save outputs to `reference/idtech7/typeinfo/<build>/`.
   Use `mh_kw stereo`, `mh_kw vr`, `mh_kw projection`, `mh_kw usercmd` to find more.
3. **Dormant VR test** (console unlocked): `vr_dummyDevice 1; vr_enable 1` (and the reverse
   order, and on the command line); `stereoRender_separation 0.032`; watch the log (`vr_logLevel
   4`, `in_terminal 1`) and a RenderDoc capture for doubled views. Ghidra: find xrefs to the
   `vr_enable` cvar object and to `stereoRender_separation`; determine whether any reader path is
   live.
4. **Console unlock in-process.** Locate the restriction call (`4C 8B 0E BA 01 00 00 00 48 8B CE
   44 8B F0 41 FF 51 ??`) and confirm the `idCmdSystem` vtable slot; implement our patch; verify
   `hands_offsetX`, `pm_noBob`, `view_skipShakes`, `r_znear` can be set and take effect.
5. **Render view path** (x64dbg + Ghidra). Put a hardware write breakpoint on the player's
   `firstPersonViewOrigin` (offset from typeinfo) and on `renderView_t.vieworg` of the main view;
   walk up to the function that builds the view and the function that submits it to the render
   world. Record: where FOV and projection are applied (look for `fov_x`, `r_znear` reads, and
   the `explicitProjectionMatrix` / `customViewProjectionMatrix` fields), and whether the weapon
   uses a separate view/projection (`hands_fovScale` reader).
6. **RenderDoc frame capture** (gameplay, menu, dossier, weapon wheel, glory kill, cutscene).
   Identify: the UI target (format, size, the pass that writes it, the tone-map dispatch that
   reads it), the first-person gun draws in the depth pre-pass and forward pass, motion-vector
   target, TAA history, the per-view uniform buffer layout (view/projection matrices). This
   decides UI option 1 and is shared with the Vulkan stereo work.
7. **Input path.** Break on XInput reads and on `idPlayer::ProcessInput` (Advanced Options sig
   as a start); find where view angles are integrated from input and where move direction is
   derived from yaw. Test a fake XInput device end to end.
8. **Weapon and muzzle.** Find `idHands` update and the muzzle transform used for projectiles
   (`useMuzzleAsFireAxis` decl flag readers, `hands_useDeferredViewAimMuzzleTraces`). Check with
   `hands_drawMuzzlePos 1`.
9. **Cinematic signals.** Log `hideHudForCinematic`, `hideReticle`, `gameWasPaused`,
   `currentHudMode`, the `cutsceneID` global and sync start/end across: intro, a Doom Hunter base
   cinematic, a glory kill, a Slayer Gate, the Fortress hub. Build the state table for comfort
   modes.
10. **Culling experiment.** Offset the view origin by ±3.2 cm and yaw by a few degrees with our
    hook; look for pop-in/missing geometry (Umbra and GPU occlusion culling use the main view).
    Try `r_umbraJobKickoff`, `r_skipModelGPUCulling 1`, `r_skipGPUTriangleCulling 1` to see which
    stage drops objects.
11. **Units.** Move a measured distance with noclip and compare origin delta with
    `pm_walkspeed`; settle metres vs. inches (`vr_metersToGameUnits` default 1.0 suggests metres).
12. **Sandbox exe.** Repeat 2, 4 and 6 briefly on `DOOMSandBox64vk.exe` via the launcher's
    "Play with Mods".
13. **Signature export.** For every function we hook, record the anchor string and an IDA/Ghidra
    signature in `reference/idtech7/signature-catalog.md` under a new "EternalVR" section.

---

## Sources

- Meathook: https://github.com/brongo/m3337ho0o0ok (releases: https://github.com/brongo/m3337ho0o0ok/releases);
  Nexus page https://www.nexusmods.com/doometernal/mods/851; fork https://github.com/snowzzrra/Meathook-AP
- DE Advanced Options Mod: https://github.com/SteamKaibz/DE_AdvancedOptionsModPublic ; https://www.nexusmods.com/doometernal/mods/1255
- DOOM Eternal Archipelago: https://github.com/snowzzrra/DoomEternal-AP-Mod
- KEX cvar list: https://github.com/Official-KEX/doom-eternal-full-cvarlist
- EternalBasher / EternalPatcher.def: https://github.com/leveste/EternalBasher ; EternalPatcher https://github.com/dcealopez/EternalPatcher
- Restricted commands unlocker: https://github.com/dpteam/DOOM_Eternal_Restricted_CMDs_Patcher_Unlocker
- LiveSplit ASL: https://github.com/loitho/doom-eternal
- Downpatcher: https://github.com/mcdalcin/DoomEternalDownpatcher
- Cheat tables: https://fearlessrevolution.com/viewtopic.php?t=11889 ; https://github.com/pepe-god/DOOM-Eternal-Cheat-Table
- eternalmods wiki: https://wiki.eternalmods.com/books/eternal-reverse-engineering-file-formats ,
  https://wiki.eternalmods.com/books/eternal-command-console , https://wiki.eternalmods.com/books/eternal-idstudio ,
  https://wiki.eternalmods.com/books/eternal-miscellaneous (SWF modding, Atlan)
- idStudio docs and release notes: https://idstudio.idsoftware.com/ (FAQ, mod packer, console, release notes 2024-08-27 to 2026-05-19)
- Atlan Mod Loader: https://github.com/FlavorfulGecko5/EntityAtlan
- Steam news (Rev 2.2, Rev 3, PC Mod Preview, PC Mods Update): https://store.steampowered.com/news/app/782330 ;
  build data: https://api.steamcmd.net/v1/info/782330
- Simon Coenen, "DOOM Eternal - Graphics Study": https://simoncoenen.com/blog/programming/graphics/DoomEternalStudy
- Geffroy, Wang, Gneiting, "Rendering the Hellscape of DOOM Eternal", SIGGRAPH 2020: https://advances.realtimerendering.com/s2020/RenderingDoomEternal.pdf
- Billy Khan interview (PCGH, 2020): https://www.pcgameshardware.de/Doom-Eternal-Spiel-72610/Specials/Interview-mit-Lead-Engine-Programmer-Billy-Khan-1360708/ ;
  Khronos note: https://www.khronos.org/news/permalink/vulkan-and-the-technology-behind-doom-eternal ;
  Slayers Club: https://slayersclub.bethesda.net/en-US/article/id-tech-7-interview
- DOOM 3 BFG source: https://github.com/id-Software/DOOM-3-BFG
- Vk3DVision: https://github.com/helifax/Vk3DVision-Public ; Flat2VR preview: https://x.com/Flat2VR/status/1704495949978984506
- FRAMED screenshot guide (console/photomode cvars): https://framedsc.com/GameGuides/doometernal.htm
