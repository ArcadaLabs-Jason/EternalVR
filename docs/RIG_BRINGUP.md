# Rig bring-up runbook (milestone M1)

The first sessions on the Windows development PC (Ryzen 9 9950X3D with its integrated AMD GPU enabled,
RTX 4080 16 GB; D-044). Development runs locally on the rig (D-033) on a development drive that is not
`C:`, written `<Dev>`. The aim is to run the M1 rig experiments (`ARCHITECTURE.md` section 16) and
answer the rig-side open questions in `DECISIONS.md` with recorded evidence. Results go in
`docs/rig-findings/`, one file per experiment, with the game build and settings recorded. Game binaries,
captures, corpora and dumps stay under the artifact root (section 8) and are never committed. In these
documents the artifact root is written `<workspace>` and the checkout `<repo>` (by default
`<workspace>\EternalVR`).

## Manual steps

Everything not on this list is done by the rig scripts.

| # | When | What | Time |
|---|---|---|---|
| 1 | Before M1 | Steam updates for DOOM Eternal set to "Only update this game when I launch it" | 1 min |
| 2 | Before M1 | The game on a Steam library on the development drive | once |
| 3 | While scripted runs are going | Keep the PC awake and the desktop unlocked (a locked desktop blocks screenshots). Scripted runs use a virtual display with the game muted, so the PC stays usable (D-041) | 1 min |
| 4 | When asked | Approve administrator prompts for tool installs, batched together (D-039) | 1 min each |
| 5 | M1 | Play the parts that need a human: the three benchmark routes and the capture positions (section 4) | 1 to 2 hours in total |
| 6 | M3 onwards | Wear the Quest 3 for the headset checks (VDXR and Meta Link) | 15 to 30 min per check |
| 7 | When M4 is done | Recruit testers (AMD RDNA 2, 3 or 4, Index, PSVR2, Intel) | about 30 min |
| 8 | When ready | Make the repository public (D-030) | 5 min |
| 9 | M10 | Choose the code signing route (open question 7) | 10 min |

HAGS is left as it is (D-036). To keep the rig free while someone plays something other than DOOM
Eternal, create `RIG_BUSY` in the artifact root and remove it afterwards (T-108).

**Tags.** **[scripted]**: the rig scripts do it unattended. **[owner]**: needs a person at the controls
or in the headset; the scripts prepare everything around it.

**Unexpected outcomes.** Record the result in the step's findings file, take the documented fallback and
carry on. If an outcome changes scope or a Must (for example, the dormant VR test changes T-004), stop
that line of work, raise it with the owner, and record the decision in `DECISIONS.md`.

## 0. Rig rules

The rules are D-039, D-040, D-041, D-042 and D-045; the script behaviour that enforces them is the
contract of T-108, and multiplayer containment is T-109. In short: our folders on the development drive
are ours; `C:` data is never deleted or damaged; the game's settings and saves are backed up and
restored freely; reversible system settings are changed as needed with a one-line log note; the
playable install is never modified (reproducible measurements); scripted runs go to the virtual
display, muted; scripts and the game never run elevated, and Steam is running first. HAGS is left as it
is and recorded with each measurement (D-036, T-085).

## 1. Tools

Installed at bootstrap: Visual Studio 2022 Build Tools at `E:\VS\2022\BuildTools` (C++ workload, Windows
11 SDK, C++ CMake tools with CMake and Ninja), the Vulkan SDK at `E:\VulkanSDK\<version>`, Ninja, Python
3.12 (per user, `%LOCALAPPDATA%\Programs\Python\Python312\python.exe`; `python` and `python3` on PATH
are Store stubs), RenderDoc, the Temurin JDK 21, and portable ProcDump, PresentMon and Ghidra under
`<workspace>\tools\bin`. The rig has Windows PowerShell 5.1 only. Still to fetch when first
needed:

| Tool | First needed | Why |
|---|---|---|
| clang-format 18.1.8 (`pip`, in a virtual environment on the development drive, e.g. `<workspace>\venv`, created with the Python 3.12 path above; the pre-commit checks run with it activated so `python` is that interpreter) | Before the first commit | Pre-commit checks (`CONTRIBUTING.md`) |
| Meathook v7.2 (`XINPUT1_3.dll`), lab copy only | Section 3 | Console unlock, `mh_spmap`, type dumps before our own unlock in M2 (R03 section 3.3) |
| Nsight Graphics | If RenderDoc crashes (R09 sections 3.5 and 3.7) | Frame captures |
| Fossilize | Section 4, if Steam's cache is missing | Pipeline corpus (R09 section 3.4) |
| x64dbg | Section 3 step 1 (PLAN 1.17) | Hardware write breakpoints for the render-view build point; live debugging |
| .NET SDK 8 and the .NET Framework 4.8 reference assemblies | M4.5 | Launcher |
| OpenXR-Simulator (elliotttate, v1.5.0) | M3 | Headset-free OpenXR runtime (T-110) |

Our own capture layer is PLAN task 1.15, written only if RenderDoc, Nsight Graphics and GFXReconstruct
all fail.

## 2. Session 1: plumbing (about 1 hour)

Outputs: `docs/rig-findings/rig-setup.md` (steps 1 and 2), `launch.md` (step 3), `builds.md` (step 4),
`config-persistence.md` (step 5).

1. **[scripted] Build and scripts.** Build and test with the command in `CONTRIBUTING.md` (done on the
   rig at bootstrap: 8 of 8 suites pass); record the compiler and CMake versions. Write the rig scripts
   and their tests (PLAN 1.1, T-108) and run the suite on the rig. Find how the virtual display is added
   and removed from a script (open question 28).
2. **[scripted] Lab copy.** `lab-copy.ps1` (PLAN 1.2); check that the copy starts with
   `SteamAppId=782330`, otherwise record it (fallback in T-108).
3. **[scripted] Direct launch on the virtual display.** With Steam running, start `DOOMEternalx64vk.exe`
   with `run.ps1` (`+r_fullscreen 0`, the window-size cvars, `+s_volume 0`, `+logFile 2`). Record
   whether it runs without the Electron launcher, whether Steam relaunches it and drops our environment
   (T-109), whether `+cvar` arguments reach the game (R03 marks this unverified), and where the log
   lands. If the window cvars do not reach the game, `run.ps1` moves the window; if `+s_volume` does
   not, the optional audio-session mute of T-108 is used.
4. **[scripted] Archive the executables.** Record SHA-256, PE timestamp and SizeOfImage for the retail
   and sandbox exes and copy them to `archive\<build>\`. Read at planning time, to confirm: Steam build
   25216728; retail PE timestamp 2026-08-11, SizeOfImage 0x7431000 (Rev 3.2, `base\candidate.cfg`
   `release-steam-2026-08`); sandbox PE timestamp 2026-09-09, SteamStub-wrapped; `nvngx_dlss.dll` 2.3.0
   in the game root and in `doomSandBox\` (T-103); the retail exe does not import
   `SteamAPI_RestartAppIfNecessary`. This file is the one home for build facts; other
   documents cite `builds.md`. Older retail exes are v1.x (T-060).
5. **[scripted] Settings persistence** (PLAN 1.12): the procedure and test values of T-100, in both
   settings locations (T-099). The result, per cvar including `s_volume`, and what Steam Cloud does on
   the next launch (open question 8) go in `config-persistence.md`.

## 3. Session 2: engine entry points and the dormant VR subsystem (about 2 hours)

Outputs: `docs/rig-findings/engine-facts.md` (step 1), `dormant-vr.md` (steps 2 to 5). Meathook runs
only in the lab copy (T-108).

1. **[scripted] Render-view build point and render entry** (PLAN 1.17, T-069; methods in 1.17's order,
   timeboxed to about two sessions): with x64dbg, Ghidra and type info, where the engine builds the
   render view (the M2 camera hook's target), the render entry point,
   and the per-frame state it advances (frame index, per-frame pools and rings, particle and
   GPU-simulation steps, TAA and exposure history, previous-frame matrices, the Umbra query kick).
2. **[scripted] Static checks** (R15 section 6): install listing, `dumpbin /imports`, strings near the
   `vr_*` help text, cross-references to the `vr_*` and `stereoRender_*` cvars and the `_Dummy` classes,
   and whether the `viewIndex` / `multiView_60Hz` scaffold loops the render over views.
3. **[scripted]** In the lab copy with Meathook: `+logFile 2 +vr_logLevel 4`, with `vr_enable` and
   `vr_dummyDevice` toggled at launch, with and without SteamVR running. If session 1 step 5 was
   deferred, run it here through the unlocked console (T-100).
4. **[scripted]** Probe with `r_debugInvert2ndView` and `multiView_60Hz`; a RenderDoc capture for pass
   count, view matrices and a second render target.
5. **[scripted]** Write the `EngineNativeStereo` go/no-go into `dormant-vr.md`.

## 4. Session 3: renderer reconnaissance (about 3 hours)

The **capture scenes** are the combat arena and outdoor scenes (the world capture scenes), each at the
start checkpoint of its benchmark route, and the main menu. The hub route has no capture scene.

1. **[scripted] Menu-scene capture, first and before M2** (PLAN 1.5a): a RenderDoc capture of the main
   menu on the virtual display records the game's `vkCreateInstance` `apiVersion` and extensions, its
   `vkCreateDevice` extensions and feature chain (T-082), and the final eye image with its format and
   sRGB state (T-080, T-081). The frame-rate-limit and dynamic-resolution cvars come from type info and
   the cvar list (T-047).
2. **[owner] Benchmark routes:** combat arena, outdoor and hub, each from a named checkpoint, 60 s long,
   in `bench-routes.md`.
3. **[owner] Flat baselines** (T-075): 1440p RT off DLSS off, and 4K RT off DLSS Quality, on the normal
   display with sound. Record p50/p99 frame time, GPU busy (PresentMon) and VRAM, every setting, the
   display with its resolution and refresh rate, and the HAGS state (T-085), in `baselines.md`.
4. **[owner] World captures:** at each capture position, one RenderDoc capture per world scene with TAA
   off and jitter frozen (`r_antialiasing 0`, `r_jitter 0`, T-070), then a GFXReconstruct capture of the
   same scenes with our layer off (harness proof and the stereo spike's input; replayed on the rig; the
   oracle set is re-taken in M4, T-043). The checklist of what `vulkan-recon.md` records is
   `ARCHITECTURE.md` section 16 item 6; the world captures add the viewport Y sign, depth format,
   compare op and reversed or infinite projection (T-083), which the menu may not show.
5. **[scripted] `vulkaninfo`:** fragment-shading-rate features including `layeredShadingRateAttachments`,
   and which extension `r_VRSEnabled 1` enables, for both GPUs, in `vulkaninfo.md`.
6. **[scripted] Pipeline corpus:** Steam's Fossilize database for 782330 (present on the rig), otherwise
   `VK_LAYER_fossilize` over the capture scenes; every pipeline created in the step 4 captures must be in
   it. Counts and gaps in `corpus.md`.

## 5. Session 4: engine facts for M2 and M5

1. **[scripted] Resolver dry run:** the resolver CLI (PLAN 1.14, part of the CMake build) on every
   archived exe; one JSON per exe in the run folder, summarised in `resolver-dryrun.md`.
2. **[owner] Unit scale:** noclip a known distance and compare coordinates; eye height from `getviewpos`
   or type-info fields. Steps 2 to 5 go in `engine-facts.md`.
3. **[scripted]** The usercmd builder via `engine_t::usercmdGen`, and the command layout (R13 section 11).
4. **[scripted]** Weapon fire-axis fields: `useMuzzleAsFireAxis`, `useMuzzleDirForFireAxis`,
   `fireFromMuzzle`, `hands_adjustFirePosDistCheck`.
5. **[scripted]** A callable trace on the player physics (R10 open question 6), recorded either way.

## 6. Rig scripts

The scripts are in `tools/rig/`; usage is in `tools/rig/README.md`. Their behaviour is the T-108
contract, and the automated suite in `tools/rig/tests/` is its specification: a change to behaviour is a
change to the tests. A run folder holds `run.json`, the `CLEANUP_PENDING` marker until a verified
cleanup, and, as produced, `logs\`, `screenshots\`, `dumps\`, `config-before\`, `config-after\` and
`config-replaced\`.

## 7. Headset checks

From M3 a person wears the headset for scheduled checks. The scripts prepare the launch and record
everything; the tester reports what they see against the criterion being checked.

## 8. Where rig artifacts live

```
<artifact root>\             <Dev>:\evr\ in general, written <workspace>
  EternalVR\                 the checkout, written <repo>
  runs\<stamp>-<name>\       one folder per run (section 6)
  backups\                   pre-development settings and save backups with SHA256SUMS (T-099)
  archive\<build>\           game exes per build, with hashes
  captures\  corpus\  dumps\ GFXReconstruct and RenderDoc captures, Fossilize databases, type-info dumps
  symbols\                   our PDB store (R09 section 2.6)
  lab-install\               lab copy of the game install, for Meathook
  SESSION  RIG_BUSY  DISPLAY_ADDED  PLAYABLE_INSTALL_MODIFIED   markers (T-108)
```

Never committed: game binaries, anything derived from them (dumps, captures, corpora, extracted
assets), and logs or screenshots that show personal data (account names, user paths, Steam IDs).
Write-ups quote only the facts we need, with personal details removed. CI artifacts never contain
game-derived data (R09 section 8).

## 9. Game updates during a milestone

1. The Steam setting (manual step 1) stops silent updates; our scripts start the exe directly, which does
   not trigger an update.
2. Before the game is updated, archive the current exes (section 2 step 4) if not yet archived.
3. After the update, record the new build in `builds.md`, archive the new exes, and refresh the lab
   copy.
4. Run the resolver CLI and, from M2, the resolver check script (T-060) on the new exe; record the
   coverage change.
5. Results already taken stay valid for the build they name; re-run an experiment only if its
   conclusion depends on engine code the resolver report shows has moved.
