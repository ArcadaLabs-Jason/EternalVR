# EternalVR scope

What we are building, for whom, and what is in and out. Each requirement lists the decision or
research note it comes from (see `DECISIONS.md` for the log). Design is in `ARCHITECTURE.md`; sequencing
and done-when criteria are in `ROADMAP.md`.

Requirement IDs are `REQ-nn`. Research notes are cited as `R01`..`R15`; the two never share a prefix.

## What v1 is and what's next

v1 is DOOM Eternal's single-player game (campaign, both DLCs, Master Levels, Horde Mode and the
Fortress) played in VR with both eyes rendered from one engine frame, head and hand tracking, a
wrist-glance HUD with readable subtitles and messages, seated play as a first-class mode, per-player
profiles set up in a desktop launcher, DLSS per eye on NVIDIA, and AMD upscaling through OptiScaler
where the game can be made to offer it. Next is M1, rig bring-up, run as a local session on the
owner's PC; the first milestone he can put the headset on for is M3, the first head-tracked view, and
the first he can play with motion controllers is M5 (`ROADMAP.md`, "At a glance"). Anything that only
a volunteer tester could verify is never allowed to block v1: without a volunteer it ships documented
as untested (T-076).

## Product

A free, MIT-licensed VR mod for DOOM Eternal on PC (Steam), distributed as a download with a launcher.
It has no paywall and gates no features; a Ko-fi link on the project page is the most it ever asks
(D-001). Unofficial fan project; no game assets are distributed.

**Primary goal:** the single-player game fully playable and polished in VR (D-020).

## Audience and platforms

- **Windows 10/11 PCs with a PC VR headset:**
  - Quest family over Virtual Desktop or Link: about two thirds of the audience
  - Valve Index
  - PSVR2 with the PC adapter
  - Pico
  - Bigscreen Beyond
  - Pimax
  - Steam Frame
  - Legacy WMR where the runtime allows
- **GPUs:** NVIDIA first (most of the audience and the dev rig). **AMD is not left behind** (D-012):
  AMD upscaling comes through OptiScaler (D-031), with our engine adapter unlocking the game's own DLSS
  option (T-072). The project has no AMD hardware (D-027), so AMD is verified by recruited community
  testers, recruited in the release track once there is a stereo build (M4.5) and starting with an
  offline shader-patcher check (T-035, T-068). Intel is best effort: XeSS through OptiScaler, verified
  by a tester or documented as untested (T-076).
- **Game versions:** the current Steam build of DOOM Eternal (`DOOMEternalx64vk.exe`). The idStudio
  sandbox executable is v1.x (T-109, T-112). Game Pass/Microsoft Store is out of scope.

## Requirements

### Must (v1)

| ID | Requirement | Source |
|---|---|---|
| REQ-01 | Campaign, The Ancient Gods 1 and 2, Master Levels, Horde Mode and the Fortress playable start to finish in VR. The campaign, both DLCs and the Fortress have no blocker (a fault that, with default settings, stops a mission from being completed without disabling the mod, leaving VR, or retrying the same checkpoint more than once, T-105); Master Levels and Horde Mode may ship with documented known issues | D-020, R12, T-076, T-105 |
| REQ-02 | Single-frame stereo (both eyes from one engine frame). Multiview is the default; synchronized sequential rendering (both eyes from one game tick) is the candidate fallback, unproven on id Tech 7 until its M4 criteria pass. Alternate-eye rendering is never a player-facing mode; debug aid only | D-004, D-032, D-037 |
| REQ-03 | 6DoF head tracking; the headset alone controls view rotation | D-004, R06 |
| REQ-04 | Tracked motion controllers; weapon aim from the hand, independent of the head | D-004, T-010 |
| REQ-05 | **Seated play is first-class** and a primary tested configuration: seated reach for holsters and punches (holster zones at shoulder height, desk-safe weapon offsets, any hands-jump gesture off by default when seated), and seated is the default in the owner's profile; room-scale supported (head offset with lean cap and collision fade in v1, T-062) | D-017, T-074 |
| REQ-06 | **Zero-setup posture:** seated and standing both work without configuring anything. The launcher offers an optional Seated / Standing / Auto override | D-018, T-029 |
| REQ-07 | VR-native UI: HUD and menus leave the eye buffers, and UI handling is a priority. A wrist-glance HUD, and notifications, subtitles, pop-ups and tutorial prompts fully readable under a stated legibility protocol | D-002, D-035 (goal); T-008, T-077 (technique) |
| REQ-08 | **Survives game patches:** engine found by name and type info, not fixed addresses; features fail individually with a clear report | D-002 (goal); T-002 (technique) |
| REQ-09 | **Desktop launcher is the main configuration surface** (not in-game menus). Settings hot-reload while playing, which the owner accepted in principle; each setting is marked live, next level load or restart (an `ApplyClass` in the settings schema, T-106) | D-015, D-028, T-032, T-061, T-106 |
| REQ-10 | **Savable per-player profiles:** preset plus personal edits, e.g. an "intense" and a "comfort" player on one PC | D-016, D-025 |
| REQ-11 | **Remappable controls:** bindings are data, stored per profile, never hardcoded. The launcher has a bindings editor that rejects conflicting bindings with a clear message naming both actions (and both inputs; the compiler side is done, the editor is still to do, T-106) | D-019, T-106 |
| REQ-12 | **DLSS 4.x** support in stereo, plus help updating the DLSS DLL the user already has (the game ships DLSS 2.3.0; DLSS 4.x comes from a newer DLL the user supplies, loaded from our folder, T-094, T-103) | D-011, T-103 |
| REQ-13 | **AMD/Intel upscaling** through the same per-eye interface: OptiScaler (FSR 3.1 on AMD, XeSS on Intel) as the provider under our NGX interposition, with the game's DLSS option unlocked by our gate patch. If the rig proxy test or the AMD tester check fails, v1 ships the game's native TAA on AMD and our own NGX shim moves to v1.1. Intel is verified by a tester or documented as untested. FSR 4 is a Should item. No FSR1 | D-012, D-031, T-072, T-076 |
| REQ-14 | **Fixed foveated rendering** on every headset, "just centered and perhaps wider than you'd make it with eye tracking" (owner's words). Degree-based presets and per-eye lens centring are the technical design | D-013 (goal); T-012 (presets, centring) |
| REQ-15 | Comfort options for Eternal's speed: turning modes, vignette, glory-kill and cutscene presentation choices. New profiles start from the Recommended preset with the vignette on; a first-launch health notice (photosensitivity, motion sickness, clear space); no full-field flicker from the presenter (a missed frame repeats the last good image) | D-004, R06, T-091, T-096 |
| REQ-16 | **Multiplayer safety:** never active in BATTLEMODE, including a lobby joined from a Steam invite (the guard fails closed and latches every VR feature off for the process, T-114), no game files changed (no DLSS DLL, `steam_appid.txt` or proxy DLL in the game folder), opt-in per launch, the layer inert outside `DOOMEternalx64vk.exe` and our environment kept out of Steam, anti-cheat tripwire, and a clean uninstall. Campaign online features checked before release (DECISIONS open question 6) | D-021, D-043, T-093, T-094, T-109, T-114 |
| REQ-17 | Diagnostics a tester can send in one click, with a documented manifest and personal data (user path, Steam ID) redacted; preflight warnings for known problems (e.g. HAGS on, Steam not running) | R14 (diagnostics); D-014, T-021, D-036 (HAGS); T-095 |
| REQ-18 | **Clean, maintainable code:** conventional formatting (clang-format checked in CI), static analysis (clang-tidy), small modules (no file past about 600 lines), engine code isolated in `engine/eternal` and game data in `game/eternal`; the size limit and the isolation are checked in CI from M4.5 | D-003 |
| REQ-19 | MIT licence. Code adapted from other projects is attributed in `THIRD_PARTY_NOTICES.md`; UEVR and GPL code (including OptiScaler) is never copied or bundled | D-001, D-005, D-031 |
| REQ-20 | **Performance.** Hard gates, because they protect players (frame time measured from the layer's `xrEndFrame` log, loading excluded by the classifier, three runs after a warm-up, T-105): after the first run, no frame longer than 250 ms outside loading screens, and p99 frame time within two display periods on each benchmark route, at 90 Hz and 2496 x 2688 per eye on the RTX 4080 at the default settings, RT off. Targets, measured and recorded but not blocking, separately with DLSS Quality alone and with Balanced fixed foveation alone (T-105): p99 within one 90 Hz period; no shader-compile hitch above 50 ms after the first run; our stereo overhead against a stated mono reference (0.5 ms GPU and 0.5 ms CPU for multiview, per-source budgets in T-075); the presenter's interop copies within 1.0 ms GPU | Derived from the goals of D-011 and D-013 (upscaling and foveation exist for performance); R11; T-075 |

### Should (v1 if time allows, otherwise v1.x)

Milestone criteria that implement these are marked "v1 if time allows" in `ROADMAP.md`. Rig
experiments and stretch measurements are a separate, non-blocking label there ("Experiments and
targets"); they are not Should items.

- Eye-tracked foveation where the headset exposes gaze (D-013). Needs a tester with a gaze-capable
  headset (D-027).
- Per-hand haptics from game rumble.
- The rest of UI tier B: ammo on the weapon and a projected reticle (the wrist-glance HUD itself is v1,
  D-035).
- Two-handed weapon grip and virtual stock.
- FSR 4 through OptiScaler, verified by a tester with an RDNA 3 or RDNA 4 GPU (D-031).
- Room-scale body follow, as a superset of the v1 head offset, if the usercmd precision test passes
  (T-062).
- 120 Hz on Quest-3-class headsets (DLSS Quality, fixed foveation and about 0.85 render scale on an
  RTX 4080, R11).

### Stretch

- **DLSS 5** (very expensive; must pass a both-eyes consistency test) (D-011).
- Foveated upscaling.
- bHaptics vest, PSVR2 adaptive triggers and Index finger curl, as optional modules.
- Per-widget HUD placement (UI tier C).
- A small in-headset quick menu for live tunables.
- DOOM: The Dark Ages adapter reusing the core.
- **VR in BATTLEMODE**, far future, gated on ban-risk research and an owner go/no-go (D-022).

### Deferred to v1.x

Moved out of v1 to keep it lean; each can return if a measurement calls for it.
- Our own NGX shim for AMD, if OptiScaler's rig tests fail (v1.1, D-031).
- Our own GLB hand models (T-054).
- Reprojecting HUD markers to world depth (T-058).
- The Vulkan enable2 presenter and a dedicated XR thread (T-059).
- Resolver checks on older retail exes (Rev 3, Rev 3.1) (T-060).
- HAGS-on measurements: an optional tester item, never asked of the owner (D-036).

### Out of scope

- Game Pass/Microsoft Store builds.
- Frame generation.
- FSR1 (D-031).
- Full-body avatar.
- Distributing any game files, NVIDIA DLLs or OptiScaler (the launcher downloads the latter, D-031).
- No legal research (D-010).
- Remote-driven development tooling: a job daemon, remote shells and remote GUI, automatic logon
  (D-033). Development runs locally on the rig.

## Constraints

- **Development runs as a local session on the owner's Windows PC** (RTX 4080, Ryzen 9 9950X3D with
  its integrated AMD GPU; D-008, D-033, D-044) on a development drive that is not `C:`, under the rig
  rules of D-039, D-040, D-042 and D-045 and the script contract of T-108: `C:` data is never deleted
  or damaged, settings and saves are backed up and restored freely, the playable install stays
  unmodified, and session-driven runs use a virtual display with the game muted (D-041).
- **Test hardware** (D-027): Meta Quest 3 (primary; VDXR and Meta Link) and an Oculus Rift S for an
  occasional Meta-runtime check. No discrete AMD GPU, Index, PSVR2, Quest Pro or eye-tracking headset;
  those are covered by recruited community testers. The CPU's integrated AMD GPU gives AMD driver
  evidence only, not performance results (D-044).
- **Tester builds** come from public GitHub releases once the owner makes the repository public;
  before that, only trusted people are invited as collaborators, with `main` protected (D-030, T-064).
  Builds contain only our binaries, never game files or game-derived data.
- **Every phase starts with research** and local reference copies of the documentation (D-006, D-007).
- **Fresh repository, not a fork of an existing mod** (final, D-026). Other VR mods are reference material only (D-005).
  The repository stays private until the owner makes it public (D-009).

## Definition of v1 done

Every Must requirement met, and every "required for v1" criterion of ROADMAP milestones M1 to M10
(including M1.5 and M4.5) passes. "v1 if time allows" criteria implement Should items and may move to
v1.x without blocking release. Verified on the release test matrix (ROADMAP M10):
- Owner, RTX 4080 with Quest 3: VDXR and Meta Link.
- Community testers: SteamVR with Index; PSVR2; AMD GPUs on VDXR, Meta Link and SteamVR; Intel with
  XeSS. A combination without a volunteer is listed in the release notes as untested (T-076).
