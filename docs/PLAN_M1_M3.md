# Detailed plan: M1 to M3

Task breakdown for the next milestones: M1, M2 and M3 (the first head-tracked view), and the M1.5
stereo spike that runs alongside M2 and M3. The done-when criteria in `ROADMAP.md` are the authority;
this file lists the work that gets us there, in order. Development runs as a local session on the rig
(D-033). Tasks marked (portable) need neither the game nor the rig's GPU and can be done on any machine.
All rig results go in `docs/rig-findings/`, one file per experiment, with the game build and settings
recorded. Game binaries, captures, corpora and dumps stay on the rig in the artifact root (`RIG_BRINGUP.md` section 8) and are never
committed (`RIG_BRINGUP.md` section 8, `CONTRIBUTING.md`).

## M1: Rig bring-up and reconnaissance

The session-by-session procedure is in `RIG_BRINGUP.md`. Tasks marked *(later)* feed M1.5, M4 or M5
and may finish in parallel with M2 and M3; the rest are on the path to the first head-tracked view.

| # | Task | Output |
|---|---|---|
| 1.0 | Build and test the project on the rig with the command in `CONTRIBUTING.md` (done at bootstrap: 8 of 8 suites pass); clang-format 18.1.8 for the pre-commit checks before the first commit | versions in `docs/rig-findings/rig-setup.md` |
| 1.1 | Rig scripts and their tests (T-108): `run.ps1`, `stop.ps1`, `cleanup.ps1`, `display.ps1`, `collect.ps1`, `replay.ps1`, `lab-copy.ps1` in `tools/rig/`, Windows PowerShell 5.1, with the automated suite in `tools/rig/tests/` (test seams, a Win32 test application as the fake game, temporary settings folders, no display change) as the specification; run on the rig first, CI job in M4.5 | `tools/rig/`, usage in `tools/rig/README.md` |
| 1.2 | `lab-copy.ps1`: copy the playable install to the artifact root's `lab-install\` and check that the copy runs with `SteamAppId=782330`; otherwise record the fallback (T-108) | `docs/rig-findings/rig-setup.md` |
| 1.3 | Record builds and archive the current retail and sandbox exes (`RIG_BRINGUP.md` section 2 step 4, the one home for build facts); the retail exe's direct-launch result, including any Steam hand-off (T-109). Older retail exes are v1.x (T-060) | `docs/rig-findings/builds.md`, `docs/rig-findings/launch.md` |
| 1.4 | *(later)* Dormant VR test (R15 section 6), in the lab copy; include whether the `viewIndex` / `multiView_60Hz` scaffold loops over views (T-069). Raw dumps stay in the artifact root's `dumps\` (`RIG_BRINGUP.md` section 8) | `docs/rig-findings/dormant-vr.md` with the T-004 go/no-go |
| 1.5a | Menu-scene capture on the virtual display, **before M2** ([scripted]): the game's instance and device parameters (T-082), the physical device it picks with both GPUs present (T-111; the VDXR and Meta `adapterLuid` values need a headset and are recorded in 3.1's first headset session), and the final eye image with its format and sRGB state (T-080, T-081) (`RIG_BRINGUP.md` section 4 step 1) | `docs/rig-findings/vulkan-recon.md` |
| 1.5 | *(later)* World-scene Vulkan reconnaissance with TAA off and jitter frozen (T-070): the checklist of `ARCHITECTURE.md` section 16 item 6, including the viewport Y sign, depth format and compare op (T-083). If RenderDoc crashes, Nsight Graphics, then task 1.15 | `docs/rig-findings/vulkan-recon.md` |
| 1.6 | *(later)* `vulkaninfo`: fragment-shading-rate features and `layeredShadingRateAttachments`; which extension `r_VRSEnabled` enables | `docs/rig-findings/vulkaninfo.md` |
| 1.7 | *(later)* Pipeline corpus: Fossilize database, Steam's cache for 782330 if present, or task 1.15. Coverage check: every pipeline created in the 2 world-scene GFXReconstruct captures is in the corpus | corpus in the artifact root's `corpus\` (`RIG_BRINGUP.md` section 8), `docs/rig-findings/corpus.md` |
| 1.8 | Frame-cap and dynamic-resolution cvars from type info and the cvar list, before M2 ([scripted], T-047). *(later)* Flat baselines (`RIG_BRINGUP.md` section 4 step 3; T-075, T-085) | `docs/rig-findings/baselines.md` |
| 1.9 | *(later)* Benchmark routes: combat arena, outdoor and hub, each from a named checkpoint, 60 s | `docs/rig-findings/bench-routes.md` |
| 1.10 | *(later)* GFXReconstruct capture and replay of the world capture scenes (harness proof and the stereo spike's input; the oracle set is re-taken in M4, T-043) | captures under the artifact root, `docs/rig-findings/replay.md` |
| 1.11 | *(later)* Engine facts: unit scale, eye height, usercmd location and layout, fire-axis fields, collision trace (R10 open question 6) | `docs/rig-findings/engine-facts.md` |
| 1.12 | *(later, needed by M4.5)* Settings persistence: the procedure and values of T-100 in both settings locations (T-099), then restore per T-108; record per cvar where it landed, including `s_volume`, and how Steam Cloud behaves (open question 8) | `docs/rig-findings/config-persistence.md` |
| 1.13 | *(later)* Decide T-004 (the dormant-VR go/no-go) and update `ARCHITECTURE.md`, `DECISIONS.md` and this plan | new decision entries |
| 1.14 | (portable) Resolver CLI and RTTI scan: add an MSVC RTTI scanner (type descriptors and complete-object locators to vtables) to the portable resolver, and a command-line tool that runs the resolver on an exe file. Output: one JSON file per exe with the exe's SHA-256, PE timestamp and SizeOfImage, every string anchor found (text, RVA, xref count) and every RTTI class (name, vtable RVA). Used by `RIG_BRINGUP.md` section 5 step 1. Built by the top-level CMake project (`add_subdirectory(tools/resolver-cli)`), so the normal presets build it | `tools/resolver-cli/`; JSON in the run folder; summary in `docs/rig-findings/resolver-dryrun.md` |
| 1.15 | Our own capture layer, only if RenderDoc, Nsight Graphics and GFXReconstruct all fail on the game, or Fossilize gives no corpus: a pass-through Vulkan layer that logs every pipeline creation with its duration (R11 section 12.2 item 5) and dumps the SPIR-V and create info (R02 section 10 item 10). Not written unless needed | `src/vkcore/` capture module |
| 1.17 | Render-view build point (the M2 camera hook's target) and the render entry point with the per-frame state it advances (T-069, open question 27). Methods in order: x64dbg hardware write breakpoints on `firstPersonViewOrigin` and `renderView_t.vieworg`; the `idPlayer` vtable from the 1.14 RTTI scan; the render-world submit; last resort (mono only) rotating `V_c` in the view uniform buffer found by the T-050 scan, with occlusion off. Records the build point's thread, the engine's pipeline depth (open question 17), and that the point precedes the render-world submit and every visibility input. Timebox about two sessions, then escalate | `docs/rig-findings/engine-facts.md` |

Risks:
- Steam relaunching the exe could drop our environment: Steam runs first, `SteamAppId=782330` is set,
  and a hand-off is reported by name (T-109).
- RenderDoc crashed on DOOM Eternal in the past (R09 section 3.5). Fallbacks: Nsight Graphics,
  GFXReconstruct, our own capture layer (task 1.15).
- The lab copy may not run outside Steam's library; fallback in T-108.
- The rig is shared with the owner's play; `run.ps1` refuses while he plays (T-108).
- A game update mid-milestone: `RIG_BRINGUP.md` section 9.

## M1.5: Offline stereo spike (T-049, T-070)

On the M1 captures, with no game, presenter or launcher. Runs alongside M2 and M3; must finish before
M4.

| # | Task | Notes |
|---|---|---|
| S.0 | (portable first, during M1) Minimal Vulkan layer core in two parts: S.0a, loader negotiation, dispatch tables and interception (on the M2 path); S.0b, shader-module interception, the `gl_Position = C_e * gl_Position` SPIR-V patch and the uniform-buffer scan (on the M1.5 path) | `src/vkcore/`; unit tests on sample modules |
| S.1 | `replay.ps1`: `gfxrecon-replay -m rebind` with the layer core on | `tools/rig/` |
| S.2 | Structural matching (T-050, T-070): find 4x4 candidates `M` in bound uniform ranges with `P_c⁻¹·M` rigid (jitter terms free); map buffer and offset to the block member. The matcher is portable logic with unit tests | `src/vkcore/`; the matching itself in a portable module |
| S.3 | Patch path: `gl_Position = C_e * gl_Position` in vertex shaders that read the located member; `C_e = P_e·E·P_c⁻¹` hard-coded per run (64 mm IPD, Quest 3 FOVs); no `V_c` needed | No multiview, no promotion |
| S.4 | Two replays per world capture scene (left, right); save both images | stereo viewer check |
| S.5 | Census: vertex shaders patched; fragment and compute modules reading the view constants; `gl_FragCoord` consumers (bin lookup versus screen fetch); previous-clip outputs; viewmodel projection; images and views that would be promoted and the shaders whose image declarations change (T-051, T-053, T-070) | `docs/rig-findings/stereo-spike.md` |
| S.6 | Decision: multiview, or reweigh toward synchronized sequential (D-032, D-037) if the census's share of distinct SPIR-V modules bound by draws in the two world capture scenes that need a semantic patch, or the share of those draws, exceeds 15.0% (T-070, T-105); counts and denominators recorded | new decision entry |

## M2: Skeleton in the game

A minimal layer, resolver and camera hook; development launches come from the rig scripts (T-108).

| # | Task | Notes |
|---|---|---|
| 2.1 | Layer entry on top of S.0a: the scope and gating of T-109 (active only in `DOOMEternalx64vk.exe`; the `ETERNALVR_*` variables read in code; pass-through in every other process; the replay host only in development builds with `ETERNALVR_REPLAY=1`). Tests: with Steam closed `run.ps1` refuses; the layer is inert in a non-DOOM Vulkan process with `ETERNALVR_ENABLE_LAYER=1` | T-109. Today's `src/vkcore/layer_entry.cpp` is split into `src/vkcore/layer/` when this lands |
| 2.2 | Logging (spdlog) and settings file load (toml++) in the layer; kill switches; the environment variables of ARCHITECTURE section 15 | `src/platform/`. The layer reads the settings file and never writes it (T-032) |
| 2.3 | Resolver on the live module: map the in-memory image, reuse the PE/pattern/string/xref code from `evr_resolver` | Loaded image rather than the file image: add a view type for loaded images |
| 2.4 | (portable, pulled forward into M1) Type-info reader: locate the tables from anchors, and look up class/field offsets by name; run offline on the exe file through the 1.14 CLI, so type info does not depend on Meathook | Format from R03 and the reference dumps; tests with synthetic tables |
| 2.5 | Cvar access and console unlock in our own layer (replaces the lab-only Meathook unlock) | Through the engine's cvar system, found by name |
| 2.6 | `evr_selftest` and a JSON resolver report; a forced-miss switch per resolver target for the REQ-08 check | Feeds the diagnostics |
| 2.7 | Resolver check script on the current retail exe; no self-hosted GitHub runner | T-060 |
| 2.8 | `run.ps1` support for the layer: per-process environment (`VK_ADD_IMPLICIT_LAYER_PATH`; fallback `VK_LAYER_PATH` and `VK_INSTANCE_LAYERS` when the layer is absent from the loader's layer list) and `XR_RUNTIME_JSON`; record the loader version the game loads | T-079, T-108, T-109 |
| 2.9 | Camera hook at the render-view build point (task 1.17): log `P_c` and `V_c`; a debug yaw offset | T-050; the live match against the spike is checked in M4 |
| 2.10 | BATTLEMODE guard (T-109, T-114): its signals, each latching every game-touching feature off for the process while the game keeps running; armed or refused; the anti-cheat tripwire; each signal tested by injection through a development-build hook. Until this task passes, layer-on runs are [scripted] runs only | ARCHITECTURE 4a, T-109 |
| 2.11 | Forced cvars at runtime from the authoritative list kept as data in `game/eternal`, with the CI check that code sets no cvar outside it: bob, kicks, shakes, blur, aim assist, `hands_fovScale 1`, `g_setting_objectiveMarkers 0`, `r_hdrDisplay 0`, the pacing cvars `r_swapInterval 0`, `rs_enable 0`, frame-rate cap off (cvar from M1), and GPU triangle culling off | T-047, T-053, T-058, T-073, T-080, T-092 |

In the release track (M4.5, T-068): the launcher's launch path, config snapshot and restore, preflight,
export report, the clang-tidy and source checks in CI, and the tester channel.

Portable work that can start before the rig is set up:
- S.0 layer core and S.2 matching logic with unit tests
- 1.14 resolver CLI and RTTI scan
- 2.4 type-info parsing against synthetic data
- the launcher core library (netstandard2.0): profiles, bindings (with conflict validation), preflight
  rules, settings classes and the pending-edits merge (T-061)
- settings TOML IO

## M3: First head-tracked view

| # | Task | Notes |
|---|---|---|
| 3.1 | Instance and device hooks: the OpenXR instance and system created in our `vkCreateInstance` (one per process) with a thread-local re-entrancy guard, the version and extension rules of T-110, the instance extensions a pre-1.1 game needs, and the device extensions and feature merging of T-082. The rig has two GPUs (D-044): the rig scripts set the game's per-application GPU preference to the RTX 4080 and logs it, the OpenXR system's LUID is checked against the game's device, and a mismatch refuses VR; steering by reordering is M4.5 unless 1.5a or this task's LUIDs show it is needed now (T-111). The first headset session records each runtime's `adapterLuid` (VDXR, Meta). Without an OpenXR system VR is refused | T-057, T-082, T-110, T-111 |
| 3.2 | Frame loop, shape 1 (T-022): in the present hook, `xrEndFrame` for the previous frame, then `xrWaitFrame`, `xrBeginFrame`, `xrLocateViews`, and publish a generation-counted snapshot. Frame records and rings have 3 slots (T-023). Session states per T-039: with `shouldRender` false the calls continue with zero layers | `src/xr/`. Queue lease released before `xrWaitFrame` |
| 3.3 | `D3D12Presenter`: LUID-matched device; 3-slot ring of shared images and two fences, all created in D3D12 with shared NT handles and imported into Vulkan as dedicated allocations with matching image parameters; each slot holds the colour image (depth joins in M4, UI in M6); D3D12 moves each resource from `COMMON` to `PIXEL_SHADER_RESOURCE` and back, and signals "slot read" for every written slot, skipped or not; D3D12 copy with crop into an `_SRGB` swapchain image, preserving the game's sRGB encoding (no second encode; the game's HDR output off); swapchain-wait timeouts handled by the re-wait state machine; a timed-out or skipped frame repeats the last good image, never an empty frame while a good image exists (the fade of T-091 is an M7 comfort target, T-111) | T-040, T-080, T-081, T-091 |
| 3.4 | Present hook: CPU check of the slot's "read" value, then the Vulkan copy of the game's final eye image (identified in M1) into the slot, between an acquire barrier from and a release barrier to `VK_QUEUE_FAMILY_EXTERNAL`, or a counted skip if the slot is busy; if the source is the swapchain image, `TRANSFER_SRC` usage is added at swapchain creation and the present's wait semaphores are replaced by our own, one binary semaphore per swapchain image; queue lease | T-040, T-081, T-091 |
| 3.5 | Head-tracked view: the camera hook takes the snapshot's head orientation (render yaw = player yaw + head yaw; pitch and roll from the head); the image goes out as a projection layer with the render pose and, for both eyes, the game's projection tangents from the logged `P_c` (not the runtime's view FOV). A development-build synthetic pose source (a scripted head sweep replacing `xrLocateViews` orientation) and a view-angle hook drive the [scripted] checks; the OpenXR-Simulator window is placed on the virtual display | T-068. First head-tracked view |
| 3.6 | Render size decoupled from the desktop window | T-031 |
| 3.7 | Session recovery: the session- and instance-loss state machine of T-110, runtime restart, game resize, focus changes; launch with the runtime or headset down gives the refusal message | T-110 |
| 3.8 | Measure both copies, fence waits, pose age as defined by T-111 (the runtime's horizon logged separately) and `xrWaitFrame` blocked time, with the HAGS state (T-085); compare the copies with the 1.0 ms interop budget | `docs/rig-findings/presenter.md` |
| 3.9 | OpenXR-Simulator runs through `run.ps1`, so this works headset-free (the 30-minute soak is in the release track) | Test tier 4 |
| 3.10 | Timeout test: stall the D3D12 side on purpose for 5 s; DOOM's queue keeps running, no DEVICE_LOST, and repeated swapchain-wait timeouts produce no `XR_ERROR_CALL_ORDER_INVALID` | T-040, T-081 |
| 3.11 | Log the forced pacing cvars' effective values at session start | T-047 |
| 3.12 | Optional debug stereo peek: alternate-eye rendering through the camera hook, or the S.3 patch applied live; each eye holds its last image, never blanked between its frames. Never in a build that leaves the rig | T-068, D-004, T-091 |

Moved out of M3: the headset tester and SteamVR checks (release track, T-068); the dedicated XR thread
experiment (v1.x, T-059); runtime recenter-event logging (dropped, T-063).

## Sequencing summary

```
M1 (rig scripts and tests, builds, menu capture, resolver dry run, render-view point, settings check)
   --> M2 (layer, resolver, camera hook) --> M3 (first head-tracked view) --> M4 (stereo) --> M5 ...
M1 (later) captures --> M1.5 (stereo spike, D-032/D-037 decision) ---------------^ (before M4)
   (portable, in parallel: layer core, matching, resolver CLI, type-info parser tests, launcher core,
    settings IO, input mapping)
   (release track M4.5, in parallel from M4: launcher v0, CI extras, testers, soaks; gates only
    builds that carry the in-game layer, T-112)
```
