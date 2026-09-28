# 09 - Development loop, tooling, build, CI and release

Status: research notes, 2026-09-25. Tags: **[C]** = confirmed against a primary source, a local
reference copy, or existing mod code we read; **[U]** = unverified or inferred, check on the rig before
relying on it. Reference copies for this topic live in `reference/tooling/docs/` (see
`reference/tooling/MANIFEST.part.md`, recreated by `reference/tooling/fetch.sh`).

## 1. Summary

- **Most iteration should not need the game running, and none of it should need the headset.**
  Three layers of test replace "put on the headset and look": pure unit tests (any OS), offline
  replay of captured DOOM Eternal frames through our layer (rig GPU, no game), and bounded game runs
  against a desktop OpenXR runtime (rig, no headset). The headset is reserved for owner acceptance
  passes.
- **Frame replay regression is feasible for the Vulkan half of the layer.** Capture raw game frames
  with GFXReconstruct with our layer *off*, then replay them with our layer *on* and a synthetic head
  pose. The stereo rewrite (multiview promotion, SPIR-V patching, per-eye constants) is identical in
  replay because the shader modules and pipelines are byte-identical. Engine hooks are not present in
  replay, so the layer needs a clean split between its Vulkan core and its engine adapter. [C: GFXR
  docs; design U]
- **Fossilize gives a second, cheaper corpus:** every graphics/compute pipeline the game creates,
  replayable without the game. Running our SPIR-V patcher over the whole corpus and compiling the
  result on the driver is the fastest way to catch patcher regressions after a game update. [C:
  Fossilize README; approach U]
- **Headset-free OpenXR:** OpenXR-Simulator (MIT, Windows, supports Vulkan sessions, already used
  by existing VR mods) as the default runtime on the rig; Meta XR Simulator as the second runtime and for its
  recorded-input replay; SteamVR null driver only for SteamVR-specific bugs. [C]
- **Remote control of the rig:** processes started over SSH land in a non-interactive session, so a
  game launched that way has no desktop. Use a small daemon that runs inside the logged-in user's
  desktop session (started at logon by Task Scheduler) and takes jobs from a spool folder that WSL can
  write to without Windows interop. [C: session behaviour; design U]
- **Build and CI:** MSVC + CMake presets + Ninja, a clang-cl preset for clang-tidy, FetchContent pins,
  sccache. Launcher in WinForms on .NET Framework 4.8 with its logic in a `netstandard2.0` library
  tested on macOS/Linux. Portable tests on hosted Linux; Windows builds on a self-hosted rig runner
  while private, on hosted runners once public (free, and required by SignPath); GPU replay stays on
  the rig.
- **Signing/AV:** expect Defender ML detections (other VR mod launchers have drawn `Bearfoos.A!ml` and
  `Wacatac.B!ml`).
  Ship unsigned at first public release with developer submissions to Microsoft, apply to SignPath
  Foundation as soon as the repo is public with a release, fall back to Azure Artifact Signing
  ($9.99/month, individual identity available to US residents) if SignPath says no. [C]

## 2. The remote dev loop on the rig (concrete design)

### 2.1 Why the obvious approach fails

When a remote user logs in over SSH, the shell runs in a service session that is not the desktop
the local user sees. Starting `notepad.exe` there creates a process but no visible window
(Microsoft Q&A, PsExec write-ups) [C]. A Vulkan game started that way either fails to create a
swapchain or renders to an invisible desktop [U, but not worth testing]. On top of that, WSL
sessions that were not created by `wsl.exe` (systemd units, `machinectl`, SSH daemons inside the
distro) do not get `WSL_INTEROP`, so running `.exe` files from them fails (microsoft/WSL#8846,
#8889) [C]. Tailscale SSH into WSL is exactly that case: interop may or may not work depending on
how the distro was started [U, verify first].

### 2.2 Design: a user-session daemon plus a spool folder

```
remote dev (mac) --tailscale ssh--> WSL2 on rig
                                   |  writes job JSON to /mnt/c/evr/spool/in/
                                   v
C:\evr\spool\in\*.json  <--polls--  evr-rigd.exe  (runs in the logged-in desktop session,
                                                    started by a Task Scheduler "At log on" task,
                                                    "Run only when user is logged on")
                                   |  launches game / tools with the right environment
                                   v
C:\evr\runs\<run-id>\   (logs, eye PNGs, desktop screenshots, dumps, result.json)
                                   ^  read back from WSL via /mnt/c/evr/runs/
```

- Writing to `/mnt/c/...` from WSL needs no interop, so the loop works even if `WSL_INTEROP` is
  missing. The daemon (`evr-rigd`) is a small console app (C#, same solution as the launcher) that polls
  `spool\in`, moves a job to `spool\running`, executes it, writes `result.json`, then moves the job to
  `spool\done`. A localhost HTTP endpoint is optional; polling a folder is simpler and survives daemon
  restarts. [design]
- Alternatives considered: `psexec -i 1 -d` from an elevated SSH session (works, needs admin and the
  target session number, PsExec is itself a common AV flag) [C: PsExec docs]; `schtasks /run` of a
  pre-registered interactive task (works from session 0 because the Task Scheduler service does the
  launch, but each job needs its own task or a parameter file) [C]. Both are fine as manual fallbacks;
  the daemon is better because it can also capture the desktop, inject input and watch the process.
- The rig must auto-login or stay logged in and unlocked with the display on. A locked workstation
  still has a session, but desktop capture and `SendInput` do not work on the secure desktop. [U]
- A **busy lock**: the rig daemon refuses jobs while the owner is playing (headset runtime active, DOOM
  running without our run marker, or a `C:\evr\RIG_BUSY` file present).

### 2.3 Job types

| Job | What it does |
|---|---|
| `build` | `cmake --build --preset <p>` in the Windows checkout, returns the log tail and artifact hashes |
| `test` | `ctest --preset <p> -L <labels>`; labels `unit`, `stub`, `gpu`, `replay`, `game` |
| `replay` | run the replay harness (section 3.3) over a named capture set, return per-eye diff report |
| `launch` | bounded game run: env, args, runtime JSON, duration, capture triggers; kills the game at the deadline |
| `capture-gfxr` | bounded game run with our layer off and GFXReconstruct on, for new golden captures |
| `renderdoc` | bounded run with RenderDoc under our layer, one `.rdc` at a requested frame |
| `screenshot` | desktop and preview-window capture (Windows Graphics Capture) |
| `input` | a scripted `SendInput` sequence into the foreground game window |
| `collect` | evidence bundle: logs, dumps, layer and launcher versions, OpenXR active runtime, GPU driver version, hashes |
| `kill` | stop the game, the simulator and stray tools; restore registry and runtime selection |

Every run gets `C:\evr\runs\<utc-stamp>-<id>\` and nothing is written elsewhere except the dump
folder. The evidence (hash-manifested files, OpenXR runtime keys, build hashes) is gathered by a
service instead of a manual script.

### 2.4 Launching the game headlessly and reproducibly

- Start `DOOMEternalx64vk.exe` directly with `SteamAppId=782330` and `SteamGameId=782330` in the
  environment; never `steam -applaunch`, which loses the environment
  [C]. Steam must be running and logged in. Since August 2025 Steam starts `idTechLauncher.exe`
  first; running the game exe directly bypasses it according to community reports [U: verify that the
  direct start does not bounce through Steam and drop our environment].
- Use the layer's explicit mode for dev runs: `VK_LAYER_PATH` to the build output and
  `VK_INSTANCE_LAYERS` with our layer name, instead of touching the HKCU implicit-layer registration
  used by release builds [C: loader docs]. Also set `DISABLE_VK_LAYER_VALVE_steam_overlay_1=1`,
  `DISABLE_VK_LAYER_VALVE_steam_fossilize_1=1`, `DISABLE_VULKAN_OBS_CAPTURE=1` [C].
- Select the OpenXR runtime per process with `XR_RUNTIME_JSON` rather than changing the machine-wide
  active runtime, so the owner's headset setup is never touched [C: loader behaviour].
- Game arguments: `+com_skipIntroVideo 1 +com_skipKeyPressOnLoadScreens 1 +r_fullscreen 0 +r_mode ...
  +r_windowWidth 1280 +r_windowHeight 720`. All five cvars exist in the Eternal cvar list [C:
  `reference/_cache/cvarlist`]; the exact windowed-mode combination worked for DOOM 2016
  [U for Eternal]. `com_skipSignInManager` and `logFile`/`logFileName` also exist and should be tried
  for skipping the Bethesda.net prompt and for getting a console log file [U].
- Getting into a level: `map` is on the restricted-command list in retail builds; meathook exposes
  `mh_spmap <name>` (`map maps/game/sp/<name>/<name>`) [C: `_cache/meathook/README.md`,
  `_cache/restricted-cmds`]. Recommended: a dev-build-only console bridge inside our layer (a named
  pipe that forwards text to the engine's command buffer, using the cvar/command-system signatures from
  topic 03), plus a small set of known save games copied into the save folder before a run. Saves are
  the most deterministic way to put the player at a fixed spot. [U]
- Input for scripted runs: the console bridge for commands, `SendInput` from the daemon for menus.
  Virtual XInput pads are not worth it (ViGEmBus is retired) [U].

### 2.5 Evidence the layer produces by itself

The layer, not an external tool, should produce the primary images:
- **Eye capture:** on a trigger (frame number from env, a named event from the rig daemon, or a hotkey),
  copy both eye images just before `xrReleaseSwapchainImage`, read back, write PNG with
  `stb_image_write` plus a JSON sidecar (frame index, pose, FOVs, render size, pipeline counts).
- **Mono capture:** the same for the game's final pre-present image, which is the reference for the
  zero-IPD test below.
- **Structured log** per run with a startup health marker at the top and a periodic one-line heartbeat
  (a watchdog that only reads the start of the log misses a marker that verbose shader diagnostics
  push past the first 1 MiB).

### 2.6 Crash dumps and symbols

- In the dev loop run `procdump -accepteula -ma -e -w DOOMEternalx64vk.exe C:\evr\runs\<id>\` before
  the launch: it waits for the process and writes a full dump on an unhandled exception [C: ProcDump
  docs]. As a permanent fallback set WER `LocalDumps\DOOMEternalx64vk.exe` (`DumpType=2`,
  `DumpFolder`, `DumpCount`) under HKLM once; WER does not collect for apps with their own crash
  reporting, and the game may have one [C: WER docs; game U].
- No unhandled-exception filter of ours in release builds; a dev-only vectored handler that logs the
  faulting module and offset and continues the search is enough.
- Keep PDBs for every build keyed by the DLL's PE timestamp and size (`symstore add` into
  `C:\evr\symbols`), and set `_NT_SYMBOL_PATH` to that store plus the Microsoft and NVIDIA symbol
  servers. With `/Brepro` the PE timestamp becomes a content hash, which still works for symstore.

## 3. Frame capture, replay and GPU debugging

### 3.1 Layer ordering decides what a tool sees

The loader puts implicit layers closest to the application, then layers named in
`VK_LOADER_LAYERS_ENABLE`, then `VK_INSTANCE_LAYERS` in list order (first entry is closest to the
application), then layers the app enables [C: `reference/vulkan/loader/LoaderApplicationInterface.md`
L536-545, `LoaderLayerInterface.md` L494]. Consequences:
- To capture **what the game does**, the capture layer must sit above ours, or ours must be off. For
  golden captures, turn ours off entirely.
- To capture or validate **what we emit**, RenderDoc, GFXReconstruct or the validation layer must sit
  *below* ours: `VK_INSTANCE_LAYERS=VK_LAYER_<ours>;VK_LAYER_KHRONOS_validation` with our implicit
  registration disabled. RenderDoc's layer is normally implicit (turned on by
  `ENABLE_VULKAN_RENDERDOC_CAPTURE=1`), which would put it *above* us; listing it explicitly after ours
  should work because its manifest is found through `VK_ADD_LAYER_PATH` [U: verify with
  `VK_LOADER_DEBUG=layer`].

### 3.2 GFXReconstruct

- Capture layer `VK_LAYER_LUNARG_gfxreconstruct` (ships in the LunarG SDK). Trimming:
  `GFXRECON_CAPTURE_FRAMES=1200,1500-1501` writes one file per range; `GFXRECON_QUIT_AFTER_CAPTURE_FRAMES=true`
  ends the run; `GFXRECON_CAPTURE_TRIGGER=F10` for hotkey ranges; `GFXRECON_CAPTURE_FILE` supports
  `${AppName}`; compression LZ4/ZLIB/ZSTD [C: `gfxr-usage-desktop-vulkan.md`].
- Replay: `gfxrecon-replay --swapchain offscreen --screenshots 1-2 --screenshot-format png
  --screenshot-dir out/ capture.gfxr` runs without a window; `--dump-resources <json>` dumps render
  targets for chosen draws/render passes, which gives per-pass diffs; `--gpu`, `-m remap|rebind`,
  `--remove-unsupported`, `--sfa` help with portability; `--quit-after-frame` bounds the run [C].
- The replayer is an ordinary Vulkan application, so implicit and `VK_INSTANCE_LAYERS` layers load
  into it. That is how our layer gets into the replay. Our layer must not assume the host is the game:
  check the process image name and use the engine adapter only in `DOOMEternalx64vk.exe`. [C: loader;
  design]
- Limits: trimmed captures replay reliably on the same GPU and driver; DOOM Eternal is bindless and
  GPU-driven, uses buffer device address in places [U], and has RT and DLSS paths. Capture with ray
  tracing and DLSS off at first. GFXR's page-guard memory tracking installs an exception handler on
  first `vkMapMemory`, which can collide with a crash handler initialised concurrently [C: GFXR
  "Capture Limitations"]; if the game dies under capture, try `GFXRECON_MEMORY_TRACKING_MODE`
  alternatives [U]. Captures are large (full resource state at the trim point): store them on the rig
  (`D:\evr-captures`), never in git (they contain game assets), with game build and driver version
  recorded beside each one.

### 3.3 Replay regression harness

Structure the layer as **Vulkan core** (pipeline and shader identification, SPIR-V rewriting,
multiview promotion, per-eye constant delivery, XR submission) plus an **engine adapter** (camera
hooks, HUD, input). In replay only the core runs; per-eye constants come from a **pose script**: a
JSON file giving head pose, IPD and per-eye FOV per frame. The same pose script input is used in game
runs on the simulator, so an image produced in replay and one produced live are comparable. [design]

Oracles, from strongest to weakest:
1. **Zero-IPD identity.** With IPD 0, identical symmetric FOVs and the game's own projection, each eye
   must match the game's mono frame (the replay with our layer off) within a tight tolerance. This
   catches most rewrite bugs (wrong descriptor restore, lost push constants, broken barriers, missing
   passes) without any golden images. Expect small differences from TAA/jitter and reduced-resolution
   passes; freeze jitter if possible. [U: tolerance to be measured]
2. **Golden per-eye images** for a handful of fixed poses, compared with PSNR/SSIM thresholds and a
   difference heat map written into the run folder. Goldens are regenerated deliberately, never
   automatically.
3. **Stereo sanity checks** that need no golden: left and right differ; vertical disparity is near
   zero (row-wise cross-correlation over edge maps); disparity sign is consistent (near objects shift
   the right way); no eye is black or uniform; HUD and screen-space effects are not duplicated.
4. **Validation clean** under our layer: no new VUIDs versus the layer-off replay of the same capture
   (section 3.6).
5. **Per-pass diff** with `--dump-resources` when one of the above fails, to find the first pass that
   diverges.

Image comparison runs in Python (`numpy`, `Pillow`, `scikit-image` for SSIM) under `tools/replay/`
and emits a machine-readable report plus an HTML contact sheet. This is the part the remote developer
looks at instead of a headset.

### 3.4 Fossilize pipeline corpus

Capture a Fossilize database for a play session (`VK_LAYER_fossilize`, `FOSSILIZE_DUMP_PATH`), or reuse
Steam's shader pre-caching database for app 782330 under `steamapps/shadercache/` if present [C:
Fossilize README; Steam cache U]. A `patcher-corpus` test then walks every pipeline, runs our SPIR-V
rewrite on the matched shaders, runs `spirv-val`, and creates the pipeline on the real driver.
`fossilize-replay` with our layer loaded does the last step for free. This takes minutes, needs no game,
and is the first thing to run after a game update. [C: tools; approach U]

### 3.5 RenderDoc

- RenderDoc 1.46 (August 2026) [C: GitHub releases]. Launch the game through `renderdoccmd capture`
  with the working directory and environment set, or have the layer load RenderDoc's API when the dev
  environment asks for it: `GetModuleHandle("renderdoc.dll")` then `RENDERDOC_GetAPI`, and use
  `SetCaptureFilePathTemplate`, `TriggerCapture` or `StartFrameCapture`/`EndFrameCapture` with
  `RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE` so we capture exactly the frame we want, including one
  bracketing only our XR copy work [C: in-app API docs, `renderdoc_app.h`]. RenderDoc cannot inject
  into a process that has already initialised Vulkan [C].
- Automated inspection: the `renderdoc` Python module (`renderdoc.pyd` must match the RenderDoc build and
  its Python version) opens a capture headlessly, walks actions, and `SaveTexture` writes any target
  to disk [C: python docs]. Use it for "what did the eye-1 layer of target X contain after pass Y".
- RenderDoc 1.6/1.7 crashed on DOOM Eternal (issues #1794, #1796); current behaviour unknown [U].
  Vendor `renderdoc_app.h` (MIT).

### 3.6 Validation layers

Run `VK_LAYER_KHRONOS_validation` **below** our layer in a dedicated preset: core validation, sync
validation (`validate_sync`), and GPU-AV in separate runs because GPU-AV is slow. Configure through
`vk_layer_settings.txt` and `VK_LAYER_SETTINGS_PATH`, and log to a file [C]. The game itself will produce messages; baseline them from
a layer-off run of the same scene and diff by VUID, and use `message_id_filter` only for known game
noise [C: settings docs]. Our layer should label everything it records with `VK_EXT_debug_utils`
(`evr:` prefix) so messages and RenderDoc captures attribute cleanly.

### 3.7 Nsight Graphics, RGP and friends

Nsight Graphics is the right profiler on the rig's RTX 4080. `ngfx.exe` launches an app with `--args`
and `--env` and can generate GPU Trace or C++ captures non-interactively, so the rig daemon can run it
[C: `nsight-graphics-capture-cli.md`]. Radeon GPU Profiler only works on AMD hardware; keep it for AMD
tester reports, not the dev loop. Frame-time evidence should come from our own GPU timestamps and the
a p50/p95/p99 analyser.

## 4. Headset-free OpenXR

| Runtime | Vulkan | Fit | Notes |
|---|---|---|---|
| **OpenXR-Simulator** (elliotttate, MIT) | Yes: swapchain images are shared D3D12 resources imported into the app's `VkDevice`; timeline semaphore sync; `SIMXR_VK_NO_TIMELINE=1` fallback | **Default on the rig.** Registers via script, per-process via `XR_RUNTIME_JSON`; side-by-side preview window; settings in `%LOCALAPPDATA%\OpenXR-Simulator\settings.json` (render size, headset FOV profiles, mirror rate) | No controllers yet (roadmap), no haptics [C: README]. v1.5.0, 2026-08-14 [C] |
| **Meta XR Simulator** (standalone, v83+) | Yes (Windows D3D11/12, Vulkan) | Second runtime. Keyboard/mouse, Xbox pad or forwarded Quest controllers drive head and hands; **record a session to a VRS file and replay it** for repeatable motion | OpenXR 1.0-1.1 only; Meta extensions; may need a Meta developer login [U]. Different enumeration and timing from SteamVR, good for catching assumptions [C: Meta docs] |
| **SteamVR null driver** | Via SteamVR | Only for SteamVR-specific issues (Steam Input, `disableAsync`, compositor timing) | `requireHmd:false`, `forcedDriver:"null"`, `activateMultipleDrivers:true`, `driver_null.enable:true`. Put overrides in the user `steamvr.vrsettings`, not the shipped defaults, which updates overwrite [C: SteamVRNoHeadset] |
| Monado | Windows support is partial | Skip for now | Linux-first; Windows builds exist but the simulated/qwerty drivers are not a packaged Windows product [U] |
| Virtual Desktop VDXR | Needs a headset streaming | Not headset-free | - |

The OpenXR conformance suite tests runtimes, not applications, so it does not apply to us.
mbucchia's OpenXR API layer template is relevant only if we later add an OpenXR API layer [C].

Controller input without controllers: give our input system a **scripted source** that reads poses,
buttons and sticks from the pose-script file, used in replay and in simulator runs. That keeps input
tests independent of which simulator supports which controllers.

## 5. Build toolchain

- **Compiler:** MSVC (`cl`, latest VS 2022/2026 toolset, pinned in the preset) for release builds; the
  game is MSVC-built and every reference project (UEVR, REFramework) uses it. A `clang-cl`
  preset in CI for `clang-tidy`, `-Wall -Wextra` style warnings and a second compiler's opinion.
- **CMake presets** (`CMakePresets.json`): `win-msvc-debug`, `win-msvc-release`, `win-clangcl-tidy`,
  `portable-debug` (macOS/Linux; builds only the pure libraries and tests), with Ninja Multi-Config.
  Static CRT (`/MT`) for the layer DLL so it does not depend on the user's VC++ redistributable or
  collide with other mods [U: confirm no CRT-boundary issues with the OpenXR loader; link the loader
  statically too].
- **Dependencies: FetchContent pinned to commit SHAs**, one `cmake/deps.cmake`. safetyhook documents
  FetchContent and needs Zydis (`SAFETYHOOK_FETCH_ZYDIS=ON`); kananlib is cmkr-generated and fetches
  bddisasm and spdlog when asked [C: `safetyhook-readme.md`, `_cache/kananlib/cmake.toml`]. The OpenXR
  SDK, Vulkan-Headers, SPIRV-Headers and SPIRV-Tools all build cleanly with FetchContent. vcpkg would
  add a second package system for two libraries that are not in it anyway (kananlib); revisit only if
  SPIRV-Tools build time hurts, and then use sccache first. glslang is not needed at runtime if our own
  shaders are compiled to SPIR-V at build time.
- **Speed:** sccache (Apache-2.0) as the compiler launcher, working with MSVC in CI and locally
  (requires `/Z7` instead of `/Zi` for cacheable objects) [U: measure].
- **Building from WSL/macOS:** the remote developer should trigger Windows builds through the rig daemon (a
  `build` job) or through CI on the rig runner; do not rely on calling `cmake.exe` through interop from
  an SSH session. A Windows OpenSSH server on the rig is a reasonable second door for builds only,
  since builds do not need the desktop. Cross-compiling with `clang-cl` plus `xwin` in Linux works for
  fast compile checks of Windows-only code and is optional [U]; release artifacts always come from MSVC
  on Windows.
- **Launcher: C# WinForms on .NET Framework 4.8.** It is present on every supported Windows 10/11
  install, gives a small exe, and works with `LangVersion latest` [C]. .NET 10 self-contained single-file is larger (tens of MB)
  and WinForms cannot be trimmed or Native-AOT compiled because of its COM marshalling dependence [C:
  dotnet/sdk#34129]. Avalonia with Native AOT is possible but buys little. Put all launcher decisions in
  a `netstandard2.0` class library (`EternalVR.Launcher.Core`) with an xUnit project that runs under
  `dotnet test` on macOS and Linux; the WinForms project is a thin shell. The rig daemon shares the core
  library.

## 6. Code quality tooling

- **clang-format** with a checked-in `.clang-format` and a CI check; no reformat-on-save churn on
  vendored code. **clang-tidy** with a short allowlist (`bugprone-*`, `performance-*`,
  `cppcoreguidelines-pro-type-member-init`, `readability-else-after-return` off, `modernize-use-override`,
  `misc-const-correctness` warn) run on changed files in the clang-cl preset.
- **Warnings:** `/W4 /permissive- /Zc:__cplusplus /utf-8`, `/WX` in CI only, vendored targets exempt.
  Never let `NDEBUG` strip test asserts (`/UNDEBUG` on test targets).
- **Sanitizers:** MSVC ASan does **not** support an ASan DLL inside a non-instrumented exe, so it
  cannot be used on the layer in the game [C: MSVC ASan docs]. Use ASan+UBSan on the portable tests on
  macOS/Linux (most logic lives there) and MSVC ASan on Windows-only tests whose host exe we build
  (the stub-dispatch tests that load the layer DLL with a synthetic loader chain).
- **Tests:** doctest (MIT, single header, fast compile) for C++; CTest labels `unit` (any OS), `stub`
  (Windows, no GPU), `gpu` (headless Vulkan device, returns CTest skip code when no GPU), `replay`
  (rig only), `game` (rig only, launches DOOM). Catch2 v3 (BSL-1.0) is an acceptable alternative; GoogleTest is heavier
  than we need.
- **Logging: spdlog** (MIT) is already pulled in by kananlib; one file sink per run directory, a
  lock-free ring buffer that is flushed on error, and an `OutputDebugString` sink in dev builds. No
  logging in per-draw hot paths except behind a runtime verbosity check.
- **Config: TOML via toml++** (MIT, header-only) for the user-facing config, one file with comments,
  instead of scattered marker files and env vars. JSON (nlohmann/json, MIT) for
  machine-written files: run manifests, pose scripts, capture sidecars.
- **Debug UI: Dear ImGui** (MIT), rendered into its own OpenXR quad layer in VR and into the desktop
  mirror, dev panels compiled out of release builds or hidden behind a config flag.

## 7. Recommended toolchain and libraries

| Name | Purpose | License | MIT-compatible? |
|---|---|---|---|
| MSVC / CMake / Ninja | Build | proprietary tool / BSD-3 / Apache-2.0 | Tools only, not shipped |
| clang-cl, clang-format, clang-tidy | Second compiler, style, lint | Apache-2.0 with LLVM exception | Yes |
| sccache | Compiler cache | Apache-2.0 | Tool only |
| safetyhook (+Zydis) | Inline/mid hooks | BSL-1.0 (+MIT) | Yes, notices |
| kananlib (+bddisasm, spdlog) | Scanning, RTTI, disassembly | BSL-1.0 (+Apache-2.0, MIT) | Yes; bddisasm needs NOTICE |
| OpenXR SDK loader | OpenXR | Apache-2.0 | Yes, notices |
| Vulkan-Headers, SPIRV-Headers, SPIRV-Tools | Layer, SPIR-V rewrite and validation | Apache-2.0 / MIT | Yes |
| spdlog / fmt | Logging | MIT | Yes |
| toml++ | User config | MIT | Yes |
| nlohmann/json | Machine files | MIT | Yes |
| Dear ImGui | Debug and settings UI | MIT | Yes |
| stb_image_write | PNG eye captures | MIT / public domain | Yes |
| xxHash | Shader and pipeline IDs | BSD-2 | Yes |
| doctest | C++ tests | MIT | Yes |
| xUnit | Launcher tests | Apache-2.0 | Yes (test only) |
| renderdoc_app.h | In-app capture API | MIT | Yes |
| GFXReconstruct | Capture/replay | MIT | Tool only |
| Fossilize | Pipeline corpus | MIT | Tool only |
| Khronos validation layer | Validation | Apache-2.0 | Tool only |
| OpenXR-Simulator | Desktop OpenXR runtime | MIT | Tool only |
| Meta XR Simulator | Desktop OpenXR runtime | Meta SDK license | Tool only, never redistributed |
| Nsight Graphics, ProcDump, PsExec | Profiling, dumps | proprietary freeware | Tools only |
| numpy, Pillow, scikit-image | Image diff | BSD-3 / MIT-CMU / BSD-3 | Tools only |

## 8. CI plan

Current prices: private repos on GitHub Free get 2,000 included minutes a month; Linux 2-core costs
$0.006/min and Windows 2-core $0.010/min beyond that; hosted runners are free for public repos; and
self-hosted runners are free (a $0.002/min platform fee announced for March 2026 was postponed
indefinitely) [C: `gha-billing.md`; postponement C per GitHub changelog coverage].

**While private:**

| Workflow | Runner | Trigger | Content |
|---|---|---|---|
| `portable` | `ubuntu-latest` | push, PR | configure `portable-debug` with ASan/UBSan, doctest + xUnit (`dotnet test`), clang-format check. About 5 min |
| `windows-build` | self-hosted `rig` | push to main, manual | MSVC release + debug, stub tests, clang-tidy on changed files, package zip as artifact |
| `gpu-replay` | self-hosted `rig` | nightly, manual | `gpu` and `replay` labels, patcher corpus, report uploaded as artifact |
| `game-smoke` | self-hosted `rig` | manual only | bounded launch on OpenXR-Simulator, eye capture, evidence bundle |

A hosted `windows-latest` job once a week keeps the "builds on a clean machine" guarantee honest and
costs little. macOS runners are not needed (macOS minutes cost ten times Linux) because the portable
job on Linux covers the same code.

**Self-hosted runner on the rig:**
- GPU and game jobs need the interactive desktop, so the runner must run in the logged-in session
  (`run.cmd` started by the same logon task as the rig daemon), not as a service. Build-only jobs could run
  as a service under a separate user; one runner in the user session is simpler. [U]
- Concurrency 1, label `rig`, jobs refuse to start when the busy lock is set.
- GitHub's hardening guide warns that self-hosted runners on public repositories can be abused by pull
  requests from forks [C: `gha-secure-use.md`]. Before the repo goes public, restrict the rig runner to
  a runner group used only by workflows on `main` and `workflow_dispatch`, and never on
  `pull_request` from forks; or remove it from the repo and trigger rig work through the rig daemon.
- Game files, captures and goldens live on the rig, never in artifacts that leave it except diff
  reports and downscaled contact sheets.

**After going public:** Windows build and release move to hosted `windows-latest` (free, and SignPath
requires that every job leading to the signing request ran on GitHub-hosted runners [C:
`signpath-github-trusted-build.md`]). The rig keeps `gpu-replay` and `game-smoke`.

**Game-update detection:** a scheduled rig job hashes `DOOMEternalx64vk.exe`; on change it runs the
signature resolver against the new exe (kananlib-cli can verify signatures against archived binaries
[C: topic 04]) and the patcher corpus, and opens an issue with the result. Archived game binaries stay
on the rig.

**Release packaging and reproducibility:**
- `tools/package.ps1` builds the zip from a clean tree: layer DLL, layer JSON, launcher, licenses,
  `THIRD_PARTY_NOTICES.md`, `SHA256SUMS.txt`, `build-info.json` (git SHA, toolset version, dependency
  SHAs). A hash allowlist checks the package so test stubs cannot ship.
- Deterministic builds: `/Brepro` on compile and link, `/pathmap` to strip build paths, pinned MSVC
  toolset version, zip entries sorted with fixed timestamps from the commit time. Then two builds of the
  same commit on different machines can be compared byte for byte [U: verify with the actual toolset].
- `actions/attest-build-provenance` on release assets (free for public repos) [U: plan availability].

## 9. Antivirus and code signing

**Why we will be flagged.** A small unsigned exe that writes a registry key under
`HKCU\...\Vulkan\ImplicitLayers`, launches another process with a modified environment and ships next to
an unsigned DLL looks like a loader to ML models. Other VR mod launchers have carried standing notices
for `Trojan:Win32/Bearfoos.A!ml` and `Wacatac.B!ml` across releases [C]. `!ml` detections are model verdicts, not signatures, and are often cleared on
developer submission within days [C: Microsoft Q&A threads].

**What reduces them without signing:**
- No packers, no UPX, no self-extracting archives, no obfuscation, no downloading and running code.
- Full version resources (product, company, description, file version) on every binary.
- The launcher never touches the game directory unless it has to, and says what it changes.
- Use the implicit layer manifest's `enable_environment` gate so the registered layer is inert outside
  our launch, and remove the registration on exit.
- Submit every release binary at `microsoft.com/wdsi/filesubmission` as a software developer before
  announcing the release, and check VirusTotal (upload happens at release time, when the files are
  public anyway) [C: submission guide].

**Signing plan.**
1. **Private phase:** no signing. Add Defender exclusions for the build and run folders on the rig
   only.
2. **First public release:** unsigned, with a README notice and hashes, plus developer submissions.
   SignPath Foundation requires the software to be "already released in the form that should be
   signed", a public repository, an OSI license (MIT qualifies), MFA for all team members, defined
   roles, a code signing policy page with their attribution line, builds verifiably from source on
   GitHub-hosted runners, and manual approval of each signing request [C: `signpath-foundation-terms.md`,
   `signpath-github-trusted-build.md`]. So apply right after the first public release.
3. **SignPath risk points** we must satisfy explicitly: "must not modify user systems without
   warnings" and "must provide uninstallation capabilities" (our registry key: show it, and offer a
   clean-up button), no user data transfer without consent (the update check must be disclosed and
   opt-outable). Whether they accept a game mod that hooks another program is not stated either way
   [U: ask in the application].
4. **Fallback: Azure Artifact Signing** (renamed from Trusted Signing), Basic plan $9.99/month with
   5,000 signatures, needs a paid Azure subscription. Individual identity validation is available
   only to developers in the US or Canada, with the Azure billing account type set to Individual and a
   government ID check through Microsoft Authenticator/AU10TIX; the certificate carries the validated
   legal name and city/state/country, with no custom CN. Organisation validation would put a company
   name on a fan mod. No EV certificates; SmartScreen reputation still has to accumulate. Integrates
   with SignTool (dlib) and a GitHub Action (`azure/artifact-signing-action`) [C: quickstart, FAQ]. This
   is an owner decision because the certificate names a real person or company.
5. Sign both the launcher exe and the layer DLL, timestamped. Signing does not remove ML detections
   instantly, but it is the durable fix.

## 10. Release, distribution and updates

- **GitHub Releases** as the canonical channel: zip, `SHA256SUMS.txt`, release notes with the
  supported game build hashes, and (once public) provenance attestation.
- **Nexus Mods** as a mirror; same zip, linking back to GitHub for hashes and source.
- **Versioning:** SemVer for the mod (`0.x` until the first playable release), with the DLL and launcher
  always released together and checked against each other at launch, so a mismatched pair is caught.
  Embed the git SHA and the supported game exe hashes in `build-info.json`.
- **Update check:** once a day at most, a GET to the GitHub releases API
  (`/repos/<owner>/<repo>/releases/latest`), disclosed on first run and switchable off; show a
  notification with the release notes link. Never download and execute automatically: it is an AV
  trigger and conflicts with SignPath's consent rules. Unauthenticated API calls are limited per IP,
  which is fine at this frequency [U: current limit].
- **Game compatibility:** the launcher checks the exe hash against the supported list and refuses the
  VR path (with a clear message) on an unknown build instead of letting signature scans fail inside the
  game.

## 11. Open questions to verify on the rig

1. Does Tailscale SSH into WSL have working interop (`WSL_INTEROP` set, `cmd.exe /c ver` works)? If
   yes, which Windows session do launched processes land in? (Decides whether the spool folder is the
   only path.)
2. Does starting `DOOMEternalx64vk.exe` directly with `SteamAppId=782330` start the game without
   `idTechLauncher.exe` and without Steam relaunching it (environment preserved)?
3. Which windowed-mode cvars work on Eternal (`r_fullscreen 0`, `r_mode`, `r_windowWidth/Height`)?
   Does `logFile` produce a console log, and where?
4. Is `map` usable in retail, or do we need meathook or our own console bridge? Do save games load
   deterministically enough for fixed-spot tests?
5. Does GFXReconstruct capture and replay a trimmed DOOM Eternal frame on the 4080 (RT off, DLSS off),
   and with which memory tracking mode? Does replay with our layer loaded reproduce the live stereo
   output for the same pose script?
6. How close is the zero-IPD eye image to the mono frame? What tolerance is needed with TAA on, and
   can jitter be frozen?
7. With `VK_INSTANCE_LAYERS=<ours>;VK_LAYER_RENDERDOC_Capture`, does RenderDoc load below us and
   capture our command stream? Confirm with `VK_LOADER_DEBUG=layer`.
8. Does a Steam shader pre-caching Fossilize database exist for 782330 on the rig, and does it cover
   gameplay pipelines?
9. Does OpenXR-Simulator's Vulkan path work with DOOM Eternal's device (shared D3D12 import on the same
   LUID) and our timeline semaphore usage? Does Meta XR Simulator need an account login?
10. Does DOOM Eternal install its own crash handler (does WER LocalDumps fire, does ProcDump `-e`
    catch the exception first)?
11. MSVC `/Brepro` plus `/pathmap`: are two builds of one commit on the rig and on a hosted runner
    byte-identical?
12. Can the self-hosted runner run in the user session alongside the rig daemon without the owner
    noticing, and what is the busy-lock rule when the owner wants to play?

## Sources

Local copies (`reference/tooling/docs/`, fetched 2026-09-25): GFXReconstruct desktop Vulkan and
OpenXR usage; Khronos validation layer, GPU-AV and SyncVal docs; `renderdoc_app.h`; RenderDoc in-app
API, capture and Python pages; Nsight Graphics capture CLI; OpenXR-Simulator README; Meta XR Simulator
overview and getting started; SteamVRNoHeadset README; Monado README; OpenXR layer template README;
WER LocalDumps; ProcDump; PsExec; WSL file systems; safetyhook and kananlib READMEs; MSVC ASan; .NET
single-file; GitHub windows-2025 image, Actions billing, self-hosted runners, secure use; SignPath
Foundation terms, home and GitHub trusted build docs; Azure Artifact Signing quickstart, integrations
and FAQ; SmartScreen reputation; Defender submission guide and false positive guide.

Web:
- GFXReconstruct - https://github.com/LunarG/gfxreconstruct
- RenderDoc releases and DOOM Eternal issues - https://github.com/baldurk/renderdoc/releases,
  https://github.com/baldurk/renderdoc/issues/1794, https://github.com/baldurk/renderdoc/issues/1796
- OpenXR-Simulator - https://github.com/elliotttate/OpenXR-Simulator
- Meta XR Simulator - https://developers.meta.com/horizon/documentation/unity/xrsim-intro/,
  https://developers.meta.com/horizon/downloads/package/meta-xr-simulator-windows/
- SteamVR null driver - https://github.com/username223/SteamVRNoHeadset
- WSL interop outside wsl.exe sessions - https://github.com/microsoft/WSL/issues/8846,
  https://github.com/microsoft/WSL/issues/8889
- GUI from SSH sessions - https://learn.microsoft.com/en-us/answers/questions/675551/create-process-with-interactive-gui-from-remote-fo,
  https://note.com/veltrea/n/nc6c6b9fd4509
- WinForms and Native AOT - https://github.com/dotnet/sdk/issues/34129
- MSVC ASan - https://learn.microsoft.com/en-us/cpp/sanitizers/asan-building
- GitHub Actions pricing - https://github.blog/changelog/2025-12-16-coming-soon-simpler-pricing-and-a-better-experience-for-github-actions/,
  https://github.com/resources/insights/2026-pricing-changes-for-github-actions,
  https://docs.github.com/en/billing/concepts/product-billing/github-actions
- SignPath Foundation - https://signpath.org/terms.html, https://docs.signpath.io/trusted-build-systems/github
- Azure Artifact Signing - https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart,
  https://learn.microsoft.com/en-us/azure/artifact-signing/faq,
  https://azure.microsoft.com/en-us/pricing/details/artifact-signing/
- Defender ML false positives - https://learn.microsoft.com/en-us/answers/questions/4035275/windows-defender-win32-wacatac-b-ml-false-positive,
  https://www.microsoft.com/wdsi/filesubmission
- WER LocalDumps - https://learn.microsoft.com/en-us/windows/win32/wer/collecting-user-mode-dumps
- DOOM Eternal id launcher (August 2025) and launch options -
  https://steamcommunity.com/sharedfiles/filedetails/?id=3545439840,
  https://steamcommunity.com/app/782330/discussions/0/594031066432564578/

Local project references: `reference/vulkan/loader/*.md`, `reference/_cache/Fossilize/README.md`,
`reference/_cache/meathook/README.md`, `reference/_cache/restricted-cmds/README.md`,
`reference/_cache/cvarlist/`, `docs/research/02`, `04`.
