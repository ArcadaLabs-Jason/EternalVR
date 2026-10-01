# EternalVR roadmap

## At a glance

Status as of 2026-10-01 (v0.1.11).

| Milestone | What a player sees or plays | Needs a person? | Status |
|---|---|---|---|
| M0 Research and design | Documents only | Answers and decisions | Done |
| M1 Rig bring-up and reconnaissance | Nothing new; the game launched by scripts | About 1 to 2 hours of play for routes, captures and baselines ([owner] items; the M2 path needs none of them) | Done |
| M1.5 Offline stereo spike (parallel with M2 and M3) | Left and right still images of two scenes | [owner] check of the left and right images in a stereo viewer (optional; the disparity criterion is [scripted]) | Done (the census chose sequential stereo, Route S) |
| M2 Skeleton in the game | The flat game; a debug key turns the view without turning the aim | No, if command-line cvars reach the game (a level is loaded by `+map`); otherwise one short [owner] run into a level | Done |
| **M3 First head-tracked view** | **Looking around inside the game in the Quest 3 (mono, gamepad or mouse to play); optional debug stereo peek** | Wears the headset | Done |
| M4 Stereo | The game in 3D in the headset, head position tracked | Wears the headset | Done (per-eye TAA and DLSS, room-scale) |
| **M5 First playable** | **Playing with motion controllers: aim from the hand, move, turn, punch, seated** | Plays | Done (played in the headset; bindings for seven controller families, `docs/VR_CONTROLLERS.md`) |
| M6 VR UI | Menus on a panel with a laser pointer; HUD on the wrist; readable subtitles and messages | Reads the legibility checks | Mostly done: HUD panel, menus with a laser pointer, world GUIs in both eyes (`docs/VR_MENUS.md`), tutorial and HUD prompts that name the controller's buttons (on a branch); the wrist and weapon HUDs are options, checked on the rig, waiting for a headset test |
| M7 Game states and comfort | Glory kills and cutscenes handled; the whole game played through | Plays (seated campaign pass) | In progress: cutscenes on a screen or skipped, comfort effects off, a comfort vignette (option), look-at triggers that test the head, the Cultist Base Revenant piloted, glory kills with four views (follow the camera, the default; steady; fade out; flat screen); climbing that follows the head is on a branch |
| M8 Profiles, bindings, launcher | Per-player profiles and a bindings editor in the launcher | Tries the launcher | In progress: launcher with Play and Advanced tabs, VR settings profiles, each with its own controls, the controls editor, preflight, update alerts with in-place updates, and an Export report for bug reports |
| M9 Performance | DLSS per eye, foveation, 90 Hz | Wears the headset | In progress: DLSS per eye with NVIDIA's newest DLL, downloaded on request (experimental), Sharpening, CPU Saver and Alternate eyes; fixed foveated rendering on NVIDIA RTX with the menus and HUD at full rate (experimental, off by default); frame pacing to the headset (experimental, off by default) |
| M10 Beta and release | A public release | Code signing choice; going public | In progress: the repository is public; alpha prereleases v0.1.0 to v0.1.11 |
| M4.5 Release track (parallel from M4) | Builds testers can run | Tester recruitment (Flat2VR Discord); a 30-minute headset session; a SteamVR run on the Quest 3 | Done |

## How to read this file

Milestones are ordered by dependency, not by calendar. Each has explicit DONE WHEN criteria, in up to
three lists:
- **Required for v1:** implements a Must requirement (`SCOPE.md`) or something a later Must depends
  on. The milestone is done only when every one of these passes.
- **v1 if time allows:** implements a Should item (`SCOPE.md`). It may move to v1.x without blocking
  the milestone or the release.
- **Experiments and targets:** rig experiments and stretch measurements that inform later decisions.
  They never block a milestone and are not Should items.

"Rig" means the Windows development PC (RTX 4080), where development runs locally (D-033).
"Headset" means a real HMD session on the owner's Quest 3 (D-027); everything else must work
headset-free (ARCHITECTURE section 14). "Testers" means recruited community testers (T-035), who get
builds from public releases once the owner makes the repository public, and before that as trusted
collaborators (D-030, T-064); an item a tester would verify is never blocked by a missing volunteer,
it becomes a documented known limitation instead (T-076). "Benchmark routes" are the three 60-second
routes defined in M1. "World capture scenes" are the combat arena and outdoor scenes, each captured at
the start checkpoint of the matching benchmark route; the main menu is captured too (the "menu
scene"), for the corpus and UI work only, because menus are mono and have no world view (T-070). The
hub route has no capture scene. "Quest 3 size" is 2496 x 2688 pixels per eye at render scale 1.0, with
the runtime setting that produces it recorded (ARCHITECTURE section 11). "Performance gates" are the
two hard gates of T-075; everything else about frame rate is a target.

**Tags and definitions.** A criterion that needs a human at the controls or in the headset is tagged
[owner]; untagged criteria are [scripted] work the rig scripts run and the tests judge. The "Needs a
person?" column follows the tags. Frame time, gate runs, blockers and clean machines are defined in
T-105; pose age, reprojection, the default settings of gate runs and the M3 missed-frame gate in T-111.

Order (T-068): M1 → M2 → M3 (first head-tracked view) → M4 (stereo) → M5 (first playable) → M6 to
M10. M1.5 runs alongside M2 and M3 and must finish before M4. M4.5, the release track, runs in
parallel from M4 on and gates only builds that carry the in-game layer (T-112); it never blocks M5 to
M9. No build with the in-game layer leaves the rig without passing M4.5.

## M0: Research and design (done)

DONE WHEN
- [x] Research notes R01-R15 in `docs/research/`, each with sources and design implications
  (including R12, the content audit, and R15, the dormant VR subsystem)
- [x] Reference library in `reference/` with combined manifest and `tools/fetch_references.sh`
- [x] Plan QA gate closed at commit 23b3fbe: rounds 9 and 10 found no blocker or major defect (D-024,
  D-046); adversarial review continues for code and later phases
- [x] `docs/ARCHITECTURE.md` and this roadmap
- [x] Repository skeleton builds; portable unit tests pass on macOS and Linux CI (and builds on Windows MSVC)

## M1: Rig bring-up and reconnaissance

Items marked *(later)* are needed only by M1.5, M4 or M5 and may finish in parallel with M2 and M3.

Required for v1
- [ ] The project builds and its tests pass on the rig with the `windows-msvc` preset; the pre-commit
      checks run there
- [ ] Rig scripts (T-108): the suite in `tools/rig/tests/` passes on the rig, including a run killed at
      each contract step and recovered by the next run; `run.ps1` launches the
      game with a per-process environment into a run folder and returns, and refuses while the owner is
      playing, when elevated, or when Steam is not running
- [ ] Session-driven runs (D-041, T-108): the game appears only on the virtual display added for the
      work block and is silent; after the block the display is gone and the settings in both locations
      match their snapshots by SHA-256
- [ ] Lab copy made and checked to run, or the fallback recorded (T-108)
- [ ] Game builds recorded in `docs/rig-findings/builds.md` and the retail and sandbox exes archived
      under the artifact root (never in the repository); the retail exe's direct launch recorded,
      including any Steam hand-off. Older retail exes are v1.x (T-060)
- [ ] Resolver dry run: the resolver CLI (PLAN 1.14) run against the archived exes, output in
      `docs/rig-findings/resolver-dryrun.md` (string anchors and RTTI classes found per exe)
- [ ] The engine's render-view build point located (the camera hook's target for M2), and its render
      entry point with the per-frame state it advances (T-069, open question 27); the build point's
      thread, the engine's pipeline depth, and that it precedes the render-world submit and every
      visibility input are recorded (PLAN 1.17)
- [ ] *(later)* Dormant VR subsystem test run and written up in `docs/rig-findings/dormant-vr.md`, with
      a go/no-go on `EngineNativeStereo`, including whether the `viewIndex` / `multiView_60Hz` scaffold
      loops over views. (Raw dumps stay under the artifact root, never in the repository.)
- [ ] *(later)* [owner] positioning: one capture per world capture scene, with TAA off and jitter
      frozen (T-070), analysed in `docs/rig-findings/vulkan-recon.md` against the checklist of
      ARCHITECTURE section 16 item 6
- [ ] *(later)* `vulkaninfo` recorded, including the fragment-shading-rate features and
      `layeredShadingRateAttachments`; which extension `r_VRSEnabled` enables (NV or KHR, T-037)
- [ ] *(later)* Pipeline corpus built for patcher work (a Fossilize database, Steam's cache for 782330,
      or our own capture tool, R02 section 10 item 10) over the 2 world capture scenes. Coverage is checked,
      not assumed: every pipeline created during the 2 world-scene GFXReconstruct captures is present in the corpus,
      and `docs/rig-findings/corpus.md` records the counts and any gap
- [ ] *(later)* [owner] Flat baselines in two configurations (T-075): 1440p with RT off and DLSS off, and 4K
      with RT off and DLSS Quality. p50/p99 frame time, GPU busy and VRAM, with every setting, the
      display (resolution and refresh rate) and the current HAGS state recorded (T-085)
- [ ] The frame-rate cap and dynamic-resolution cvars identified from type info and the cvar list,
      before M2 (T-047)
- [ ] *(later)* [owner] Benchmark routes defined in `docs/rig-findings/bench-routes.md`: combat arena, outdoor,
      and hub (Fortress); each starts from a named checkpoint and lasts 60 s
- [ ] *(later)* [owner] Unit scale and eye height measured; usercmd layout and fire-axis fields located
- [ ] *(later)* Collision trace searched for (R10 open question 6); result recorded either way
- [ ] *(later, needed by M4.5)* Runtime-set cvars checked for persistence (T-100, in both settings locations, T-099):
      `config-persistence.md` lists per cvar where its test value landed, including `s_volume`, and how
      Steam Cloud behaves (open question 8); the settings are restored per T-108
- [ ] Menu-scene capture on the virtual display, before M2 (`RIG_BRINGUP.md` section 4 step 1): the
      game's instance and device parameters, the physical device it picks with both GPUs present, and
      its final eye image with its format and sRGB state recorded (T-080, T-081, T-082, T-111)
- [ ] *(later)* [owner] positioning: GFXReconstruct capture of the world capture scenes replays on the rig (harness proof, and the
      input of the stereo spike; the oracle set is re-taken in M4, T-043)

## M1.5: Offline stereo spike (T-049, T-070)

On the M1 captures, with no game, presenter or launcher. Runs alongside M2 and M3; must finish before
M4.

Required for v1
- [ ] Minimal Vulkan layer core (developed portable-first during M1, T-068): dispatch, shader-module
      interception, the `gl_Position = C_e * gl_Position` SPIR-V patch and the uniform-buffer scan. It
      loads under `gfxrecon-replay` (`replay.ps1`) with a hard-coded `C_e` (64 mm IPD, Quest 3 FOVs)
- [ ] View constants located by structural matching in both world capture scenes (T-050, T-070):
      buffer, offset and block member recorded
- [ ] Left and right images for each world capture scene, each from its own replay. For 3 named
      features per scene, the left-right disparity has the expected sign and a magnitude within 20% of
      the value computed from depth, 64 mm IPD and the FOV
- [ ] Census in `docs/rig-findings/stereo-spike.md`: vertex shaders patched; fragment and compute
      modules that read the view constants; `gl_FragCoord` consumers split into bin lookups and
      screen-texture fetches; previous-clip outputs; viewmodel projection; images and views that would
      be promoted to arrays, and the shaders whose image declarations change (T-051, T-053, T-070)
- [ ] Decision recorded in `DECISIONS.md`: multiview goes ahead, or, if the share of distinct SPIR-V
      modules bound by draws in the two world capture scenes that need a semantic patch (anything beyond
      the `gl_Position` rewrite and mechanical array promotion), or the share of those draws, exceeds
      15.0%, synchronized sequential stereo (D-032, D-037, T-066, T-069) is reweighed before M4; counts
      and denominators recorded in `stereo-spike.md` (T-105)

## M2: Skeleton in the game

A minimal layer, resolver and camera hook. Development launches come from the rig scripts; the
launcher arrives in the release track (T-068).

Required for v1
- [ ] `run.ps1` starts the game with the layer (per-process environment through
      `VK_ADD_IMPLICIT_LAYER_PATH`, never the registry, T-079; ARCHITECTURE section 15; and
      `XR_RUNTIME_JSON` when needed); the loader version the game loads is recorded. If M1 found that
      runtime-set cvars persist, the saved settings are restored after each run (T-108)
- [ ] Layer scope (T-109): the layer is active only in `DOOMEternalx64vk.exe`; every other process,
      including the sandbox exe, gets a pass-through layer, and the replay tool gets the Vulkan core only
      with `ETERNALVR_REPLAY=1` in a development build. Both `ETERNALVR_*` variables are honoured in
      code. Logs build, GPU, runtime and active layers. Honours kill switches. With Steam closed,
      `run.ps1` refuses to launch; the layer is inert in a non-DOOM Vulkan process with
      `ETERNALVR_ENABLE_LAYER=1`
- [ ] Resolver finds the type-info tables and cvar system by anchors and prints a coverage report
      (`evr_selftest`). With one resolver target forced to fail, only the feature that depends on it is
      disabled, and the report names that feature (REQ-08). The resolver check script passes on the
      current retail exe: every resolver target resolves to exactly one address (T-060)
- [ ] Camera hook: the point where the engine builds the render view is hooked; `P_c` and `V_c` are
      logged per frame; a debug yaw offset applied through the hook turns the rendered view without
      changing the player's aim, and at a 90° offset no geometry is missing compared with a flat view
      facing that way
- [ ] Forced cvars applied at runtime from the authoritative list kept as data in `game/eternal`, with
      a CI check that code sets no cvar outside it (T-092): bob, kicks, shakes, blur, aim assist,
      `hands_fovScale 1` (permanent, T-053), `g_setting_objectiveMarkers 0` (T-058), `r_hdrDisplay 0`
      (T-080), the pacing cvars of T-047 (`r_swapInterval 0`, `rs_enable 0`, frame-rate cap off) and
      GPU triangle culling off (T-073)
- [ ] BATTLEMODE guard (REQ-16, T-109): each signal (the lobby-join and matchmaking entry, the Steam
      join callbacks the game registers, the BATTLEMODE screens, a `game/pvp/*` load) is injected through
      a development-build test hook; the log names the signal and every game-touching feature is latched
      off within one frame while the game keeps running, the headset falling back to the cinema quad
      (T-114); opening the BATTLEMODE menu with the layer on latches them off (one real-signal
      smoke check). With the detection points forced to fail, VR refuses to start. The anti-cheat
      check refuses when a known module is loaded (tested with a stand-in module name) and does not trip
      on the rig's system-wide Denuvo Anti-Cheat install or the `installscript.vdf` stanza. Until this
      passes, layer-on runs are [scripted] runs only

## M3: First head-tracked view

The game rendered from the head, in the headset (T-068).

Required for v1
- [ ] [owner] `D3D12Presenter` runs an OpenXR session on the Quest 3 in VDXR and Meta Link
- [ ] Head-tracked mono view: the headset's orientation goes through the camera hook (render yaw = the
      player's yaw plus the head's; pitch and roll from the head); the game's image is submitted as a
      projection layer with the render pose and the game's projection tangents from the logged `P_c`.
      Using the development-build synthetic pose source and view-angle hook (PLAN 3.5), over a 30 s head
      sweep, the logged render yaw minus
      the head yaw equals the player yaw within 0.1°; the player's aim is unchanged by head motion; a
      30° mouse pitch input changes the rendered pitch by less than 0.1° (REQ-03). [owner] In the
      headset a static object stays put while the head turns
- [ ] Frame loop shape 1 in the present hook (T-022). Pose snapshots are generation-counted; the render
      side never waits for a snapshot; at most one XR frame open and no more engine frames in GPU flight than the depth measured in M1
      (open question 17);
      no lock held across `xrWaitFrame` (T-023). Session states follow T-039: with `shouldRender` false
      the frame calls continue with zero layers (checked by forcing the runtime's dashboard or a
      proximity-sensor off state)
- [ ] Shared-image ring per T-040: resources and fences created in D3D12 and imported into Vulkan; the
      slots carry colour (depth joins in M4 and UI in M6). Timeout test: the D3D12 side stalled on
      purpose for 5 s leaves DOOM's queue running, with no DEVICE_LOST, and "slot read" values keep
      advancing
- [ ] GPU choice on the two-GPU rig (D-044, T-111): the game runs on the RTX 4080 (per-application
      GPU preference set by the rig scripts and logged), the OpenXR system's LUID matches the game's device,
      and a forced mismatch, and a launch with the runtime or headset down, each give the refusal message
- [ ] Game render size set independently of the desktop window (T-031), clamped to the runtime's
      maximum sizes (T-110)
- [ ] [owner] After each of a runtime restart, a session loss (headset sleep and wake; the T-110 state
      machine) and a game resize, the log
      shows a new `XR_SESSION_STATE_FOCUSED` and frames submitted within 10 s, and no dump
- [ ] Forced pacing cvars (T-047) logged at session start with their effective values, which match
- [ ] Colour (T-080): on OpenXR-Simulator, a read-back of the submitted swapchain image matches the
      game's final eye image of the same frame within 2/255 per channel on 99% of pixels (no double
      gamma); the validation layer reports no error on the copy and present path, and repeated
      swapchain-wait timeouts give no `XR_ERROR_CALL_ORDER_INVALID` (T-081)
- [ ] No flicker (T-091, T-111): in the layer's frame log over a 5 s stall of the D3D12 side, no frame
      has zero layers while a good image exists
- [ ] Recorded in `docs/rig-findings/presenter.md`, with the current HAGS state (T-085): Vulkan copy, D3D12
      copy, fence waits, pose age (T-111), `xrWaitFrame` blocked time, and the interop
      copies against their 1.0 ms budget (T-040, T-075). p99 pose age as defined by T-111 (our added
      latency) is within (pipeline depth + 1) periods at the runtime's reported refresh rate; the runtime's own
      prediction horizon is logged beside it

Experiments and targets
- [ ] Debug stereo peek (T-068): alternate-eye rendering through the camera hook, or the M1.5 `C_e`
      patch applied live; each eye holds its last image (T-091). Debug only, never in a build that
      leaves the rig

## M4: Stereo

Required for v1
- [ ] Minimal game-state classifier (gameplay / camera takeover / menu or loading) with a debug overlay.
      Authored rotation is stripped in gameplay and camera takeover; menus and loading show on a
      virtual screen
- [ ] Engine camera replaced by the enclosing camera with headset rotation and position
- [ ] Positional tracking (REQ-03): a logged 10 cm sideways head move moves the rendered eyes 10 cm times
      world size, within 5 mm; [owner] in the headset a static object stays fixed in the world while
      the head moves
- [ ] The camera hook's live view-constant match agrees with the spike's (T-050)
- [ ] Oracle capture set re-taken (T-043): the 2 world capture scenes with the enclosing camera active,
      the `P_c`/pose sidecar, TAA off, jitter frozen, HUD hidden, SSR and RT reflections off; plus a
      short TAA-on sequence per scene for the motion-vector oracle
- [ ] Shaders classified (T-027, T-051, T-053): world, sky, screen-space and UI per vertex shader;
      bin-lookup and screen-fetch uses of `gl_FragCoord` per use; previous-clip outputs; viewmodel draws
      as world draws (per-draw identification only if M1.5 showed a distinct projection). Every corpus
      pipeline gets exactly one class
- [ ] Image promotion per T-052: bindless views promoted to `2D_ARRAY`, the closure rule applied, and the
      promotion log shows every promoted image in layer-owned memory
- [ ] Viewmodel check (T-053): in a world capture scene with the weapon drawn, the stereo oracle
      restricted to the pixels of viewmodel draws passes the T-043 threshold, and a planted
      untransformed viewmodel draw fails it
- [ ] Per-eye constants in a 3-slot ring in the reserved top set (T-041): zero `V_c` mismatches between
      our slot and the engine's view constants in the 2 world capture scenes and a 10-minute simulator
      run. If the data-patch fallback is used, the validation layer reports no synchronization hazard
      on it (T-071)
- [ ] Replay oracles (T-043) on the 2 world capture scenes: the no-harm check (each eye set to the
      enclosing camera) passes at 1/255 on 99.9% of pixels; the depth-reprojection oracle passes at
      64 mm IPD (99% of unmasked pixels within 4/255); each planted fault (unpatched world shader, wrong
      eye transform) drops the score below 95%. Replay uses rebind mode
- [ ] Motion-vector oracle (T-051): each eye's frame N-1 reprojected with that eye's motion vectors
      matches frame N at the stereo oracle's thresholds; a planted fault (`C_e` in place of `C_e(prev)`)
      fails it
- [ ] Culling test (T-038, T-073): in each world capture scene at 64 mm IPD, each eye rendered with the
      chosen culling option is compared with a reference render of that eye with Umbra and GPU triangle
      culling off; at most 0.05% of pixels differ by more than 8/255, and no connected region of
      differing pixels is larger than 64 pixels. The chosen option's cost and runtime effect are
      recorded
- [ ] SPIR-V patcher handles every pipeline in the M1 corpus without validation errors. Patched modules
      are cached on disk
- [ ] Each eye renders at the runtime's recommended size times render scale (near-square); the UI
      target keeps its own 16:9 size (T-031)
- [ ] Depth layer (T-057, T-080): depth crosses as `R32_FLOAT`, is submitted with `nearZ` and `farZ` in
      metres (game units divided by unit scale times world size), and is accepted by VDXR and Meta
      Link; [owner] a head-shake test shows no depth-induced warping at world size 0.85 and 1.2, after a
      planted fault (depth scaled by 2) was seen to warp in the same procedure; the depth copy is added
      to `presenter.md`
- [ ] [owner] Performance gates (T-075, measured as defined by T-105 and T-111: reprojection off, default settings pinned) in the headset at 72 Hz (Quest 3
      at Quest 3 size, RT off, HUD may still be in the eye buffers) on each benchmark route
- [ ] Posture needs no setup (T-029, T-045, T-063, T-106): unit tests feed synthetic pose streams (a
      still desk pose never anchors; worn-head jitter anchors within 2 s). [owner] With Auto, the
      logged posture matches the real posture and eye height lands within 5 cm of 1.657 m times world
      size in 10 of 10 seated and 10 of 10 standing fresh starts; a long-press recenter after standing
      up corrects height; a headset left on a desk for 60 s at launch shows no anchor in the log in 5 of
      5 trials. Seated is tested in every headset session
- [ ] If R15 is a go: the same criteria met via `EngineNativeStereo`.
- [ ] If M1.5 or the oracles chose synchronized sequential stereo (T-066, T-069), these replace the
      shader-classification, image-promotion, viewmodel, per-eye-constants and patcher criteria above
      (all other criteria still apply):
  - both eyes come from the same game tick: the tick counter logged per eye matches in every frame of
    each benchmark route
  - no per-frame state advances twice per tick: frame index, command-pool and ring indices, particle and
    GPU-simulation steps and exposure are logged per eye and advance once per tick
  - temporal history (TAA, exposure, previous-frame matrices) is per eye: the motion-vector oracle passes
    per eye on the TAA-on sequences
  - each eye's projection is its own asymmetric frustum, or a symmetric enclosing frustum cropped to
    it: the stereo oracle passes on the 2 world capture scenes
  - the present hook fires once per engine frame with both eyes; any present from the first eye's
    render is suppressed and counted
  - Umbra visibility is per eye or occlusion is off, and the culling test passes

Experiments and targets
- [ ] 72 Hz with p99 frame time within the 13.9 ms period and under 1% missed frames on each benchmark
      route
- [ ] 90 Hz at native Quest 3 size with the same measurement (R11 puts native Quest 3 at the 90 Hz limit
      before DLSS or foveation)

## M4.5: Release track (parallel from M4, T-068)

Everything a build that carries the in-game layer needs before it leaves the rig (T-112). It never
blocks M5 to M9.

Required for v1
- [ ] Multiplayer safety re-verified on the exact build that leaves the rig (REQ-16, T-109): the
      injected guard signals of M2, the fail-closed guard, the anti-cheat checks with their negative
      test, environment containment, and a hashed listing of the game folder (excluding the two files
      the idTechLauncher rewrites) unchanged by a launcher session, including a DLSS DLL choice (T-094).
      Mandatory before every external build. The real-invite test is not run (D-043)
- [ ] Launcher v0 launches the game with the layer via per-process environment and the chosen OpenXR
      runtime (default: the system's active runtime; SteamVR found through the Steam libraries and the
      other discovery paths of T-110, checked on the rig; the refusal names the runtime and how to
      switch), only with Steam already running and logged in, registers the layer manifest under HKCU
      for the launch and removes it afterwards, checks that the runtime and headset are up, and restores
      everything on exit; a hand-off to a Steam relaunch gives a named error, never a silent flat
      launch (T-109). The registration path is tested on a clean machine (T-105) or on the rig (a
      reversible registry value, D-045)
- [ ] Crash safety (T-093): with the launcher killed mid-session, its next start removes the stale
      registration and completes the pending restore; a stale registration never outlives a launcher
      start
- [ ] Settings and saves (T-036, T-092): the launcher snapshots the game's config files before launch
      and restores the forced keys after exit; killing the game mid-session and reopening the launcher
      restores them; **and** with the game killed and then launched flat from Steam without the
      launcher, the saved profile carries none of the forced values (the save-point defence). The config
      files are found by the discovery rule of ARCHITECTURE section 4, checked on a clean machine or
      recorded as untested (T-105). Save slots are backed up before each VR launch and "Restore saves"
      works
- [ ] Adapter steering (T-111): on a two-GPU machine the game's device is steered to the HMD adapter by
      LUID in `vkEnumeratePhysicalDevices` and `vkEnumeratePhysicalDeviceGroups`; tested on the rig
      with the per-application GPU preference cleared
- [ ] Launcher preflight v0: Steam running and logged in, elevation, an unwritable data folder (a
      program folder under `Program Files` only warns, T-106), HAGS on (T-021), anti-cheat components
      in the game process or folder (T-109), conflicting layers (Virtual Desktop's OpenXR API layer disabled
      when another runtime is chosen, T-110)
- [ ] Coexistence (T-095): one stereo session each with RTSS, OBS game capture and ReShade's Vulkan
      layer installed, each either working or named and disabled by preflight; the known-bad list is
      data
- [ ] "Export report" (T-095): the zip matches its documented manifest, including the HAGS state
      (T-085); a test report shows the user profile path and the Steam account ID redacted, no memory dump
      (v1 crash data is the logged faulting module and offset, exit code and log tail, T-112), and the
      file list and size shown before saving; the GitHub issue
      template asks for it
- [ ] User data and updates (T-093): all user data under `%LOCALAPPDATA%\EternalVR\`; install version
      N, create profiles and bindings, extract N+1 over the old folder, and the profiles and bindings
      survive intact; an older settings file is migrated with a backup; a settings file with a higher
      `schema_version` is refused with a message and left unmodified (unit test, T-106); a launcher and
      layer version mismatch is refused with a message
- [ ] CI (REQ-18): the clang-format check stays green; the rig-script suite runs on `windows-latest`
      (T-108); a clang-tidy job (clang-cl preset, changed files)
      passes with zero warnings under the checks configured in `.clang-tidy`; a check fails on any source
      file over 600 lines and on engine internals or game data outside `engine/eternal` and
      `game/eternal`
- [ ] No stall or deadlock (T-023): in a 30-minute simulator soak and an [owner] 30-minute headset
      session, the frame log shows no `xrEndFrame` gap over 1 s outside loading, and the run ends
      cleanly through `stop.ps1`
- [ ] [owner] SteamVR: the stereo build runs on SteamVR (Quest 3 over Steam Link); a tester on SteamVR with
      Index or on PSVR2 has run it and sent an "Export report", or the gap is recorded for the release
      notes (T-076)
- [ ] Tester channel (D-030, T-064): public releases, or trusted collaborators with `main` protected;
      recruitment started through the owner's Flat2VR Discord, aiming at two AMD testers (one RDNA 2,
      one RDNA 3 or 4)
- [ ] Early AMD check (T-035): the tester-run offline patcher tool is built (as an offline tool it needs
      only the notices, `SHA256SUMS.txt` and no game-derived data to leave the rig, T-112), also run on
      the rig's integrated AMD GPU (D-044); the AMD testers who have
      joined have run it and their reports are recorded and triaged (with none yet, the tool is ready
      and the gap recorded, T-076)
- [ ] No self-hosted GitHub runner is registered (T-060); if one ever is, it is removed or locked against
      fork pull requests before the repository goes public

Experiments and targets
- [ ] HAGS-on behaviour, from testers who run with it on (D-036); never asked of the owner

## M5: Hands, aim and locomotion (first playable)

Status: the controller input, the user-command writer, turning, hand aim with the forced-view yield, the
shot hook, the viewmodel at the grip with the per-weapon offset table, the weapon FOV and the virtual
gamepad are implemented, unit-tested and checked live on the rig with OpenXR-Simulator and scripted input
(`docs/VR_CONTROLLERS.md`, "Live checks"); controllers are on by default. Ticked below only what those
runs showed in full.

Required for v1
- [ ] Usercmd injection drives movement, buttons and view-angle deltas: every default-map action
      triggers its game action with the usercmd hook on, and again with it disabled (the XInput
      fallback), recorded in a checklist log
- [ ] Decoupled aim, closed loop (T-055): shots leave the tracked muzzle for every weapon (hitscan and
      projectile), or, with the convergence fallback, land where the hand ray points. With closed-loop
      aim, the angle error between the hand ray and the game's view angles at fire time is under 0.5
      degrees p99 over the swap-protocol run. Injection yields in forced-angle states (sync kills,
      meathook pull). With the Meathook and lock-on, the logged target is the one the hand ray points
      at, in 10 of 10 [owner] trials each
- [ ] The game's own arms and weapon placed at the controller grip joint with per-weapon offsets (T-054);
      `hands_fovScale` stays 1
- [x] Head-relative and hand-relative locomotion. Smooth and snap turn; a 360° seated turn by stick
      completes in both directions (rig, OpenXR-Simulator: `docs/VR_CONTROLLERS.md` live checks 3 and 4)
- [ ] Default control map for Touch and Index, right- and left-handed, loaded from the profile's
      bindings data (REQ-11). Each binding conflict message names both bound actions and both input keys,
      with a unit test per conflict kind (T-106; the compiler side is done, `tests/features/input/binding_compiler_tests.cpp` and `controller_bindings_tests.cpp`)
- [ ] [owner] Accidental-swap protocol (`docs/test-protocols/weapon-swap.md`, written as part of this
      milestone): 10 minutes in the combat benchmark arena on the default right-handed Touch map; the
      player presses a marker binding for each intended switch; the layer logs every weapon switch with
      the input that caused it; pass = every logged switch has a marker within 1 s
- [ ] [owner] Seated reach (REQ-05, T-074): in a chair with armrests, with a desk edge 35 cm in front of
      the chest at 75 cm high: 10 of 10 deliberate punches register and no false punch occurs in the
      seated run of the swap protocol; each combat input (equipment, weapon select and the rest) is
      performed 3 of 3 times; the logged weapon rest pose stays at least 5 cm from the desk plane. When
      seated, holster and gesture zones sit at shoulder height and any hands-jump gesture is off by
      default. If a seated punch threshold is needed, it has a unit test (T-106)
- [ ] Room-scale head offset (T-062): a 0.8 m lean logs a clamp at 0.60 ±0.01 m; the head moved into a
      wall reaches full fade within 150 ms; shots come from the last valid head position
- [ ] Audio listener follows the rendered head position and orientation (checked with `s_showPaths 1`
      while turning the head, R10 section 5)

v1 if time allows
- [ ] Two-hand support grip and virtual stock
- [ ] Per-hand haptics from game rumble
- [ ] Room-scale body follow through usercmd movement (T-062), once the precision test passes; the head
      offset, lean cap and fade still cover what the body cannot follow

Experiments and targets
- [ ] Usercmd movement precision (T-062): small scripted steps (2 to 20 cm) injected as movement, and the
      physics-origin displacement measured against the request; recorded in
      `docs/rig-findings/usercmd-precision.md`

## M6: VR UI

Required for v1
- [ ] idSWF target captured and removed from the eye buffers. Menus on a curved panel with a laser
      pointer that works in every menu screen in the R12 inventory. Done on the rig (T-116,
      `docs/VR_MENUS.md`): the GUI target on its own quad, menus on a flat world-locked panel with a laser
      pointer through the game's own cursor, checked from the title screen through settings, a new game,
      pause and quit; still open: a curved panel, the rest of the R12 inventory, the headset
- [ ] HUD split onto the wrist panel and the message panel (T-077); the body-locked HUD with look-down
      reveal is available as an option. Built, unit-tested and checked on the rig with OpenXR-Simulator
      (shown at a watch glance, hidden with the hand down or turned away, hidden under the pause menu;
      `docs/VR_HANDS_HUD.md`): the corner blocks on the off hand's wrist, or the ammo block above the gun
      in the weapon hand, the rest on the head-locked quad; both are options (the launcher's "Health and
      ammo"), and the whole head-locked HUD stays the default. The wrist HUD needs the free off hand
      (`ETERNALVR_OFFHAND=free`) before it can become a default; both wait for a headset test, and glory
      kills, cutscenes and respawns with the wrist HUD are untested
- [ ] Cylinder layers on runtimes that support them, quad fallback elsewhere
- [ ] The UI image joins the presenter's shared-image slots, and its copy is added to `presenter.md`
- [ ] Screen-projected markers (T-058): objective markers stay hidden; the interact prompt is checked in
      the combat and hub routes and hidden (`g_setting_interact_prompt 0`) if it misaligns
- [ ] [owner] Legibility protocol (`docs/test-protocols/ui-legibility.md`, T-077) written; each of the
      following passes it at default settings, read by the owner and one tester where available:
  - wrist-glance HUD: health, armor and ammo read correctly on 10 of 10 prompted glances during the
    combat benchmark route
  - subtitles: 10 of 10 lines read completely
  - notifications: 5 of 5 read completely
  - pop-ups: 3 of 3 read completely
  - tutorial prompts: 3 of 3 read completely

v1 if time allows
- [ ] Rest of tier B: ammo on the weapon, crosshair replaced by a projected reticle

## M7: Game states and comfort

Status: cutscenes play on a flat screen or are skipped, the game's camera shakes, blur and damage washes
are off, and a comfort vignette is an option. Look-at triggers (the Doom Hunter Base ladder and others)
test where the head looks (v0.1.4), and piloting the Cultist Base Revenant works with head or hand aim
(v0.1.3, v0.1.5). A whole-game audit found the Revenant to be the only body swap. Glory kills have four
presentations in the launcher (follow the camera, the default; a steady view, the rotation stripped; a
fade; a flat screen), not yet tried in a headset; the default waits for the owner's playtest.

Required for v1
- [ ] Full classifier (extends M4's minimal one) recognises every camera-takeover state in the R12
      inventory, with the debug overlay showing the current state
- [ ] Glory kills: for each of the immersive (rotation stripped), cinema window and blink options, 5
      glory kills log the classifier's takeover state on entry and exit, and in immersive mode the
      logged render rotation change during the kill equals the head's alone. [owner] Default chosen by
      playtest
- [ ] Cutscenes and videos presented per policy. Third-person cutscenes never move the player's view
      rotation
- [ ] Vignette with event pulses; Comfortable, Recommended, Advanced and Intense presets
- [ ] [owner] Every campaign mission, both DLCs and the Fortress completed start to finish in VR without a
      blocker (as defined above, T-105); each mission logged in
      `docs/test-protocols/playthrough-log.md` with build hash, date and issues, with a frame log to
      which the 250 ms gate applies (T-075); the campaign pass is played seated (REQ-05). Master Levels
      and Horde Mode played through, with any remaining issues documented as known issues (T-076)

Experiments and targets
- [ ] Missed-frame fade (T-091, T-111): after the starting 45 repeats, the view fades to a static dim
      layer over 300 ms; the values are tuned for comfort

## M8: Profiles, bindings and launcher polish

Status: the launcher has Play, Advanced and "Checks and log" tabs, VR settings profiles with a Save button
(each with its own controls), a controls editor that opens on the controllers of the last game, the Steam
and Game Pass builds (T-117), and update alerts with in-place updates from the public releases (v0.1.5, at
the owner's request). The profile criteria below (preset markers, reset per setting, `ApplyClass`) and the
editor's conflict refusal are not checked yet.

Required for v1
- [ ] Named per-player profiles (REQ-10): base preset plus overrides, changed-from-preset markers, reset
      per setting or all, save/duplicate/rename/delete, last used remembered
- [ ] Every setting has an `ApplyClass` (Live, NextLevelLoad or Restart), with a unit test that every
      key has one, and the launcher shows it (T-106). Each Live edit from the launcher appears in the
      layer log within 2 s without a level load; each NextLevelLoad edit only after the next load. Values tuned in game reach the active profile through the pending-edits file;
      only the launcher writes the settings file (T-032, T-061)
- [ ] Bindings editor (REQ-11): remap any action per profile; a profile with a conflicting binding
      cannot be saved, and the message names both actions and both inputs (T-106); reset to the default
      map
- [x] Preflight catches: Steam not running, elevation, unsupported build, missing runtime, conflicting
      layers and mods, **HAGS on** (REQ-17, T-021), anti-cheat components. Each has a clear message
      (`launcher/src/EternalVR.Launcher.Core/Preflight/`, shown on the "Checks and log" tab)
- [ ] Game builds (T-094): against a patched or renamed exe, the launcher warns about the unknown hash
      and runs the resolver; with a core feature forced unresolved (camera hook, present path,
      BATTLEMODE guard) VR is refused with a message naming the feature
- [ ] "Untested on this hardware" notice (T-076, T-095): for each untested matrix entry the launcher can
      detect (GPU vendor and generation, OpenXR runtime), the notice appears
- [ ] Health notice and comfort defaults (T-096): the first-launch health notice appears; a new profile
      starts from Recommended with the vignette on; in the per-frame vignette alpha log, the change per
      frame is at most 0.05 at 90 Hz and there are at most 2 pulse onsets in any 1 s window
- [ ] [owner] Posture override (REQ-06): in one headset session, each launcher option (Auto / Seated /
      Standing, Slayer height / Real height, explicit calibration) changes the posture and height the
      layer reports on its debug overlay as specified in ARCHITECTURE section 6.2. The owner's profile
      defaults to Seated (T-074), and a new profile defaults to Auto
- [ ] Startup watchdog tells "VR never appeared" apart from a slow launch, with guidance
- [ ] Tester matrix (T-076): AMD (RDNA 2 and RDNA 3 or 4), Intel with XeSS, SteamVR with Index, and
      PSVR2 each have a tester who has run a current build, or the gap is recorded for the release
      notes; at least one external "Export report" received and readable

v1 if time allows
- [ ] A tester with a gaze-capable headset recruited (needed for eye-tracked foveation in M9)

## M9: Performance features

Status: DLSS runs per eye (experimental). The launcher downloads NVIDIA's DLSS 310 DLL on request, after
the player accepts NVIDIA's licence, and loads it from the EternalVR data folder; the route was checked on
the rig (`docs/rig-findings/dlss-dll.md`). CPU Saver and Alternate eyes help slower processors. Fixed
foveated rendering is on dev `main` as an experimental launcher setting (Off by default, Subtle, Balanced,
Aggressive), NVIDIA RTX only through `VK_NV_shading_rate_image`: Balanced measured +15.8% frame rate on
the rig at 2064 x 2100 per eye, GPU-bound. AMD and Intel need the KHR attachment path. Not released yet;
the owner's headset check decides its default. Parallel eye rendering (both eyes' render work on separate
cores) is an experiment: stable, but it draws a wrong picture, so it is not shippable and none of its
speed figures count yet.

Required for v1
- [ ] Per-eye DLSS 4.x from the game's NGX call, with a user-supplied 310.x DLL (the game's own 2.3.0
      gives DLSS 2, T-103); NGX's own work passes through our layer untouched
      (T-033). History resets on the R07 event list. Launcher DLL version display; a newer DLL the
      user picks is kept in the EternalVR data folder and loaded from there through our NGX
      interposition during VR sessions only, never written into the game folder (T-094); the version
      shown for both exes' DLLs; preset forcing
- [ ] AMD and Intel through OptiScaler (D-031, T-065, T-072). With the user's consent, the launcher
      downloads the pinned OptiScaler release into the EternalVR data folder, verifies its SHA-256
      against `build-info.json` and refuses a mismatch, and shows its version (T-094). Rig tests on the RTX 4080 with
      OptiScaler's FSR backend forced: it loads from our folder, not as a game-folder proxy; two feature
      instances evaluate per-eye views; and the gate proxy test passes (driver `_nvngx.dll` blocked, our
      gate patch active, the game offers DLSS and its NGX calls reach OptiScaler). An AMD tester's
      Export report shows OptiScaler as the NGX provider, two feature handles and evaluate calls tagged
      per eye (T-105); an Intel tester runs XeSS, or that gap is recorded (T-076). If a rig test fails,
      the fallback is recorded instead: native TAA on AMD in v1, our own NGX shim in v1.1. With the rig
      tests passing and no AMD tester by the release candidate, the OptiScaler path ships marked
      untested (T-105)
- [ ] Fixed foveation (VRS) with Off/Subtle/Balanced/Aggressive; foveation path chosen per session
      (T-037)
- [ ] Per-eye lens centring (REQ-14, T-106): `evr_foveation_tests` assert that each eye's foveation
      centre equals an independent head-forward projection into that eye's frustum within 1e-4 NDC, for
      symmetric, asymmetric and canted per-eye FOVs and for each FOV set in
      `tests/features/foveation/fixtures/runtime_fovs.txt` (the owner's and testers' logged runtimes);
      [owner] on the Quest 3 the rate-image debug overlay shows each centre within 1% of the image width of
      that point
- [ ] Fixed foveation on by default on every headset (REQ-14): each headset and runtime in the release
      matrix starts with foveation on at the default preset, confirmed by the log and an overlay
      screenshot (for tester headsets, or the gap recorded, T-076)
- [ ] No ghosting on the weapon under DLSS Quality (T-054): on a capture sequence with fast hand motion
      in the combat route, the motion-vector oracle restricted to viewmodel pixels passes, and a
      planted fault (viewmodel motion vectors zeroed) fails it; [owner] a visual check in the headset
- [ ] [owner] Performance gates (T-075, measured as defined by T-105 and T-111: reprojection off, default settings pinned) at 90 Hz at Quest 3 size, RT off,
      at the default settings: no frame longer than 250 ms outside loading, and p99 frame time within
      two display periods (22.2 ms) on each benchmark route
- [ ] Performance targets measured and recorded (T-075, T-105), separately with DLSS Quality alone and
      with Balanced fixed foveation alone: p99 frame time against the 11.1 ms period and missed frames; stereo overhead against the mono reference for the active `StereoSource` (budgets
      0.5 ms GPU and 0.5 ms CPU for multiview); the interop copies against their budget of 1.0 ms GPU per
      frame at Quest 3 size (T-075); the longest shader-compile hitch after the first run (target 50 ms);
      each record carries the HAGS state (T-085)

v1 if time allows
- [ ] 120 Hz at Quest 3 size with DLSS Quality, fixed foveation and about 0.85 render scale (R11
      section 5.2), same measurement against the 8.3 ms period
- [ ] Eye-tracked foveation verified by a tester on a gaze-capable headset
- [ ] FSR 4 through OptiScaler, verified by a tester with an RDNA 3 or RDNA 4 GPU (T-065)

## M10: Beta and public release

Status: the repository is public, with alpha prereleases v0.1.0 to v0.1.5 and players reporting through
GitHub issues. The launcher checks the public releases and installs an update in place when the player
agrees, each file checked against `SHA256SUMS.txt` (v0.1.5); the check can be turned off. That goes
further than the update-check criterion below, which predates the owner's request for in-place updates;
the criterion needs a decision entry before it is judged.

Required for v1
- [ ] Release matrix passes. Owner on the RTX 4080 with the Quest 3: VDXR and Meta Link. Testers:
      SteamVR with Index, PSVR2, and AMD on VDXR, Meta Link and SteamVR; any combination without a
      volunteer is listed in the release notes as untested (T-076). (An owner check on the Rift S with
      the Meta runtime is optional.)
- [ ] Multiplayer safety re-verified on the release build (REQ-16): BATTLEMODE refusal, fail-closed
      guard and anti-cheat tripwire; DECISIONS open question 6 (campaign online features) answered
- [ ] `THIRD_PARTY_NOTICES.md` complete (REQ-19, T-095): every adapted file has an origin header and is
      listed, every redistributed library is listed with its full licence text and any required
      NOTICE, and the file is inside the release zip
- [ ] Uninstall (T-093): on a clean machine (T-105), after install, VR sessions and the launcher's uninstall
      action, the layer registry key and the game folder listing match a pre-install snapshot, and the
      flat settings equal the pre-VR snapshot
- [ ] Update check (T-093, R09 section 10): disclosed in the launcher, daily at most, switchable off,
      linking to the release rather than downloading it; the release notes list the supported exe
      hashes and the pinned OptiScaler version with its hash (T-094)
- [ ] Windows builds on hosted runners; no self-hosted runner can run fork pull requests (T-060)
- [ ] `SHA256SUMS.txt` published with the release zip and verified against the uploaded files by a CI
      job (T-105); Defender false-positive submissions done; the owner's code
      signing choice (open question 7) applied or filed
- [ ] README, install guide, controls reference, troubleshooting page and release notes (with known
      limitations, T-076) written. The install guide covers: SmartScreen ("More info", "Run anyway") on
      an unsigned build, the expected Defender verdicts and how to report a false positive, checking the
      download against `SHA256SUMS.txt`, extracting outside `Program Files`, Steam running before
      launch, the health notice, and uninstall (T-095). (Making the repository public is the owner's
      decision, D-030, and not a criterion.)

## Risks

| Risk | Effect | Mitigation |
|---|---|---|
| The render-view build point is hard to find (PLAN 1.17) | M2, M3 and every stereo source wait | Ordered methods in PLAN 1.17, type info read offline (PLAN 2.4), a two-session timebox then escalation; last resort a mono view rotation in the view uniform buffer |
| No AMD hardware (D-027) | AMD SPIR-V differs from NVIDIA's; late discovery would block release | AMD testers recruited in the release track, with the offline patcher tool (T-035); structural shader matching; OptiScaler tested on the RTX 4080 with its FSR backend forced (T-065); no volunteer means a documented limitation, not a blocked release (T-076) |
| The game's DLSS option cannot be unlocked on AMD (T-072) | No AMD upscaling | Gate patch tested by proxy on the RTX 4080; native TAA on AMD in v1, our own NGX shim in v1.1 |
| Umbra occlusion is single-apex (T-038) | Right-eye-only geometry culled, or a CPU and GPU cost if occlusion is turned off | Options chosen and costed by the M4 culling test; GPU triangle culling off on every vendor (T-073) |
| Collaborators on a personal-account repository get write access (D-030) | Accidental pushes before the repository is public | Trusted people only, `main` protected (T-064) |
| A self-hosted runner on a public repository | Fork pull requests could run code on the rig | No self-hosted runner in v1; if one is added, removed or locked against fork pull requests before going public (T-060) |
| Many shader modules need more than the `gl_Position` patch | Multiview stereo is costly and fragile | M1.5 census and decision (T-049, T-070); synchronized sequential fallback (D-032, T-066) |
| Synchronized sequential double-advances per-frame state on id Tech 7 (D-037) | The fallback does not work | M1 recon of the render entry point (T-069); the sequential criteria in M4 |
| OptiScaler cannot load from our folder or cannot run per eye | No AMD upscaling | Native TAA on AMD in v1; our own NGX shim in v1.1 (D-031, T-065) |
| No Index, PSVR2 or gaze-capable headset (D-027) | SteamVR/Index, PSVR2 and eye-tracked foveation cannot be checked in-house | Testers, or documented as untested (T-076); eye-tracked foveation is "v1 if time allows" |
| One rig, also used for play | Development blocked while someone plays | The "not while playing" check and scripted runs on a virtual display with the game muted (D-041, T-108); capture replay and the pipeline corpus run without the game |
| A development tool damages files outside the project (D-034, D-045) | Loss of user data on the rig | `C:` data is never deleted or damaged; work happens on the development drive; settings and saves are backed up before they are overwritten (D-045, T-108) |
| RenderDoc crashed on DOOM Eternal in the past (R09 section 3.5) | M1 recon captures may fail | Nsight Graphics (R09 section 3.7); our own capture layer (R02 section 10 item 10); GFXReconstruct |
| `r_VRSEnabled` may enable NVIDIA's shading-rate image, which excludes the KHR path (R08 section 6.2) | Foveation paths conflict | One path per session, KHR by default (T-037) |
| NGX ignores a layer view, or its calls re-enter our layer | Per-eye DLSS needs copies or a guard | Per-eye copy fallback; thread-local pass-through (T-033) |
| Game update moves code | Features fail | Resolver by name, per-feature failure, resolver check script on the current exes (T-060); update procedure in `RIG_BRINGUP.md` section 9 |
| Runtime-set cvars persist into the saved config | Flat play changed after a VR session, and synced by Steam Cloud | The adapter writes the player's own values at the profile-save point (T-092); the launcher's snapshot and restore as the backstop (T-036); on the rig, the verified whole-file restore of T-108 |
| Our environment reaches Steam or another game | Our layer active in a game with anti-cheat | Steam must be running before any launch; the layer is pass-through outside `DOOMEternalx64vk.exe` and checks its variables in code (T-109) |
| A killed run leaves the rig changed | Settings, display layout or game sound left wrong | The marker-first, verified-restore contract of T-108, run at every `run.ps1` and at session end |
| The game writes the owner's cloud save slots during runs | Lost progress, synced by Steam Cloud | Checksummed snapshot of `782330\remote\` before gameplay runs, one reserved development slot, restore when needed (D-042, T-108) |

## Stretch goals

- DLSS 5 backend (stereo-consistency test first)
- Foveated upscaling (DLSS sub-rectangle)
- PSVR2 adaptive triggers, Index finger curl (bHaptics vest and sleeves shipped as an experimental option in
  v0.1.5, `docs/BHAPTICS.md`)
- Parallel eye rendering as a launcher option, off by default, once its picture matches the standard
  renderer and it clears its speed bar
- Tier C per-widget HUD placement
- Small in-headset quick menu for live tunables
- id Tech 8 (DOOM: The Dark Ages) adapter reusing the core
- Modded play: support `doomSandBox\DOOMSandBox64vk.exe` so VR stacks with idStudio mods. Needs every
  hook signature and cross-check re-verified on that build, the multiplayer guard live-tested there (the
  sandbox exe carries the retail lobby, invite and PvP code, T-109), and the official mod-support terms
  checked. After v1
- VR in BATTLEMODE (far future). Gated on research into how BATTLEMODE treats modified clients
  today (ban risk), fairness of tracked aim against flat players, player population, and a VR design for
  the third-person demon side. Owner go/no-go required; v1 refuses BATTLEMODE (ARCHITECTURE 4a)
