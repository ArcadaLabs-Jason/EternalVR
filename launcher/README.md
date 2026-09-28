# EternalVR launcher (v0)

The desktop launcher that starts DOOM Eternal with the EternalVR layer for one session and puts
everything back afterwards. Design: `docs/ARCHITECTURE.md` sections 4, 4a and 12; what v0 covers and
where it differs from the design is T-113 in `docs/DECISIONS.md`.

| Project | Target | What it is |
|---|---|---|
| `src/EternalVR.Launcher` | .NET Framework 4.8, WinForms | The window, the command-line modes and the Windows side (registry reads, processes, the flag-gated HKCU registration) |
| `src/EternalVR.Launcher.Core` | netstandard2.0 | Everything that can be tested without Windows: VDF parsing, Steam discovery, config snapshot and key restore, save backups, preflight decisions, hash check, launch plan |
| `tests/EternalVR.Launcher.Core.Tests` | net8.0, xUnit | Unit tests of the core; any OS |
| `tests/FakeGame` | .NET Framework 4.8 | A stand-in for the game used by `tests/e2e.ps1` |
| `data/*.txt` | | Known builds, forced cvars, session (window and display) keys, refused multiplayer arguments, known layers, anti-cheat names. Copied next to the exe |

## Build and test

Any machine with the .NET 8 SDK (the .NET Framework 4.8 reference assemblies come from NuGet, so no
targeting pack is needed):

```
dotnet build launcher/EternalVR.sln -c Release
dotnet test launcher/tests/EternalVR.Launcher.Core.Tests -c Release
powershell -NoProfile -ExecutionPolicy Bypass -File launcher\tests\e2e.ps1
```

The launcher is `launcher/src/EternalVR.Launcher/bin/Release/EternalVR.Launcher.exe` with its `data`
folder. The end-to-end script needs Steam running and an OpenXR runtime set (preflight checks both), and
writes only under its `-Root` folder (default `%EVR_TEST_TMP%\e2e`, else `%TEMP%\evr-launcher-e2e`).

On the development rig the SDK is user-local and every cache stays on the development drive:
`<workspace>\tools\evr-launcher.cmd [checkout]` builds and runs the unit tests
(`tools\dotnet-env.sh` sets the same environment for Git Bash).

## Using it

Put the layer next to the launcher as `layer\VK_LAYER_ETERNALVR.json` and `layer\EternalVR.dll` (the
CMake build's `src\vkcore` output), or point `--layer-dir` or `layer_dir` in `launcher.ini` at a build
folder. Start Steam, then `EternalVR.Launcher.exe`. Launch VR is enabled when preflight has no failure.

```
EternalVR.Launcher.exe --dry-run [--data-root <dir>]   preflight, snapshot, save backup and the launch plan; no game
EternalVR.Launcher.exe --restore-saves                 restore the newest save backup (Steam-safe, below)
EternalVR.Launcher.exe --export-report <zip>           write the redacted report zip (docs/release/TROUBLESHOOTING.md)
EternalVR.Launcher.exe --help                          every option
```

## What a launch starts

The defaults are the full experience: **stereo** (Route S, one render per eye, `docs/VR_STEREO.md`),
**motion controllers on** (`docs/VR_CONTROLLERS.md`) with **hand aim** (the weapon hand aims; head aim
and the game's own mouse aim stay selectable), and the normal game start: no `+map`, the player picks a
campaign, a mission or a save in the game's menus. The HUD and the menus are drawn in the left eye only
in stereo for now (docs/VR_STEREO.md, known gaps). Mono (head-tracked, one image for both eyes) remains a
choice.

In stereo the launcher forces the stereo cvars (`r_TAASafeMode 0`, `r_antialiasing 1`: temporal AA with a
history per eye, docs/VR_STEREO.md; the temporal effects whose history the eyes would still share off:
`r_TAAAntiGhosting`, `r_SSDOTemporalAA`, `r_lightScatteringTAA`, `r_dofTAA`, `r_waterReflectionsTAA`,
`r_waterGridTAA`, `r_refractionTAA`, `r_raytracedReflectionsTemporalUpscaleQuality`; `rs_enable 0`,
`r_swapInterval 0`: no dynamic resolution, no vsync on the doubled render rate) and a windowed game
(`r_fullscreen 0`, `r_windowWidth`/`r_windowHeight`), and hands the layer the window's place and size
(`ETERNALVR_WINDOW=x,y,width,height`, client area).

Each eye renders at the headset's size, not the window's (`docs/rig-findings/render-size.md`): the launcher
sets `ETERNALVR_RENDER_SIZE` (`render_size` in `launcher.ini`: `auto`, the default, is the runtime's
recommended view size within a 2064x2208 pixel budget; `WxH` is that size) and `ETERNALVR_RENDER_SCALE`
(`render_scale`, 0.50 to 2.00, "Resolution" on the window's Play tab; 1.00 is the budgeted recommendation, a
Quest 3 through VDXR gets 2056x2216 per eye). The game window is then only the desktop mirror: 1280x720 at
the top-left of a virtual display when there is one (a virtual display driver, Virtual Desktop's or Meta's
virtual monitor), else of the primary display (`ETERNALVR_MIRROR_WINDOW`, the same as `ETERNALVR_WINDOW`), and
the layer scales the eye into it. If the layer cannot set the render size the game renders at the mirror's
size (the layer log says why: `size: render size off`).

The game starts at that size, so it never resizes mid-session (a resize can fail on 12 GB cards): before the
launch the launcher asks the same runtime, with the game's OpenXR environment, for its recommended size
(`OpenXrProbe`, through the layer's `openxr_loader.dll`, at most 30 s), works the size out with the layer's
rules (`RenderSize`), and passes `ETERNALVR_RENDER_SIZE=WxH` and `+r_windowWidth W +r_windowHeight H`. The
plan's `render:` line says which. When the runtime does not answer (headset off, no runtime), it passes `auto`
and the mirror's size, as before, and logs "Headset not detected; the render size is decided in-game".

`render_size = off` keeps the earlier behaviour: each eye renders at the window's client size, 2064x2100
(`eye_size`). A window cannot be larger than its display, so that size is fitted to the display that holds
the largest image at that aspect (the primary one on a tie; displays are read in physical pixels) and scaled
down uniformly when even that one is smaller; its top-left corner is the display's, and the title bar sits
above it, off the display. The plan's `window:` line in the log (and `--dry-run`) says which display and size
were used. All of these keys are restored after the session like the other forced
cvars. The game also saves the window the layer placed when it quits (`r_windowPosX` / `r_windowPosY`, on
the virtual display for instance), which would open the next flat launch off screen, so the window and
display keys of `data\session-keys.txt` (`r_windowPosX`, `r_windowPosY`, `r_windowWidth`, `r_windowHeight`,
`r_fullscreen`, `r_mode`, `r_displayRefresh`) are restored with them, in mono as well.

Only one launcher runs per data folder (a named mutex); a second one is refused (exit code 3 on the
command line), so two launchers never share `SESSION_PENDING`.

A launch, in order (`SessionRunner`):

1. Clean-up from an earlier run: a stale HKCU registration named by `REGISTRATION_PENDING` is removed,
   and a pending settings restore is completed, or deferred while any game process runs.
2. Preflight (below); any failure refuses the launch.
3. `SESSION_PENDING` is written, then every settings location is snapshotted with SHA-256 sums
   (`Saved Games\id Software\DOOMEternal`: the two text configs and `user\config.json`; the active
   Steam user's `userdata\<id>\782330\remote\PROFILE`); the Saved Games text configs that do not exist
   yet are recorded as absent. Then the save slots are copied into a rotating set of five checksummed
   backups (no backup is made when there is no save file, so empty backups never push real ones out).
4. `DOOMEternalx64vk.exe` is started directly, working directory the game root, with the forced cvars
   of `data\forced-cvars.txt` and the extra arguments on the command line, and in its environment only:
   `SteamAppId=782330`, `VK_ADD_IMPLICIT_LAYER_PATH=<layer folder>`, `ETERNALVR_ENABLE_LAYER=1`,
   `ETERNALVR_LOG_DIR=<data>\logs\<session>`, `ETERNALVR_WORLD_SCALE`, `ETERNALVR_AIM` (`hand`, `head`
   or `view`; hand aim without controllers is sent as `head`), `ETERNALVR_CONTROLLERS`, in stereo
   `ETERNALVR_MODE=stereo` and `ETERNALVR_WINDOW`, `ETERNALVR_SKIP_CINEMATICS`, the room-scale settings
   `ETERNALVR_POSTURE`, `ETERNALVR_HEIGHT`, `ETERNALVR_RECENTER_HOLD` and (when set) `ETERNALVR_IPD`
   (`docs/VR_ROOMSCALE.md`), `XR_RUNTIME_JSON` when a runtime other than the system's is chosen, and
   the disable variables of known-bad layers (read from their own manifests).
5. For 10 s the start is watched. When the started exe exits in that window, the launcher looks for a
   new game process for 8 s more (Steam starts it only after the first has gone): one appearing is a
   hand-off to Steam, reported by name (T-109); none is an early exit.
6. When every game process has exited, the forced keys and the session keys (`data\session-keys.txt`,
   the window and display keys) of the text configs are put back to their snapshot values (a key that
   was absent is removed, and a text config the game created during the session loses every one of
   them); other keys the player changed stay. Every other byte of the file is kept
   as it was. Other files are compared and a change is logged with the snapshot kept. The Steam-Cloud
   files (everything under `782330\remote\`, the profile and the save slots) are never written, not even
   when the game removed one: that is logged and the game's state kept (T-115). Then the marker
   is removed, only if it is still this session's. If a game process is still running at that point,
   the restore waits for it.

If the launcher is closed or killed while the game runs, the marker stays and step 1 of the next start
finishes the job. The marker, settings and restored configs are written through a flushed temporary
file and a replace; a marker damaged anyway (a power loss) falls back to the session's own snapshot,
then to the newest complete one.

## Steam Cloud

Every file under `userdata\<id>\782330\remote\` (`PROFILE\profile.bin` and the save slots) is synced
by Steam, which records each one's size and SHA-1 in `782330\remotecache.vdf`. A file written behind
Steam's back leaves that record stale, and the game then reports "Profile corrupt, creating a new one"
and uploads a fresh profile (T-115, `docs/rig-findings/launcher-live.md`). The launcher only reads
`remotecache.vdf` (`SteamCloudCache`) and never edits Steam's files:

- Preflight compares every cloud file with the record and warns (`cloud-record`) when one is stale,
  with the advice to start the game once through Steam and quit at the main menu.
- The settings restore never touches the cloud files (above).
- "Restore saves" (`SaveRestore`) is refused while a game process runs, while Steam is not running,
  and while Steam is syncing (the `782330` folder changed within 2 s). After writing the saves it
  compares them with the record. When the record is stale it runs the resync step of the rig scripts
  (`CloudResync`, the rig's `Invoke-RigCloudResync`): the game exe is started directly with
  `+r_fullscreen 0 +s_volume 0`, `SteamAppId=782330` and `ETERNALVR_DISABLE_LAYER=1`, closed 4 s in,
  before it loads the profile (and any game process it handed off to as well); the local text configs
  it touched are put back and the restored cloud files must be unchanged; then the record is polled
  until it matches, for up to 60 s. The window and the log say what happened; when the resync does not
  take, the user is told to start the game once through Steam and quit at the main menu. Exit codes
  of `--restore-saves`: 0 restored (record matching, or no record to compare with, which is said), 2
  refused, 4 restored but the record still stale, 1 error.

Preflight: not elevated; Steam running and logged in (`HKCU\Software\Valve\Steam\ActiveProcess`); no
`DOOMEternalx64vk`, `DOOMSandBox64vk` or `idTechLauncher` running; no pending restore; data folder
writable (a program folder under Program Files only warns); game found and its exe hash known (unknown
warns); no anti-cheat files in the game folder; layer folder complete; OpenXR runtime manifest present
(the system's active one from `HKLM\SOFTWARE\Khronos\OpenXR\1`, read only, unless one is chosen);
implicit Vulkan and OpenXR layers checked against `data\known-layers.txt`; HAGS on warns; no settings
location found warns and forces nothing; a stale Steam cloud record warns; extra arguments asking for
multiplayer (`data\refused-args.txt`) are refused.

The launcher reads the registry and never writes it, except with `--register-hkcu` (the HKCU
implicit-layer route of ARCHITECTURE section 4, off by default and untested, T-113).

## User data

`%LOCALAPPDATA%\EternalVR\` (`--data-root` overrides it):

| Path | Contents |
|---|---|
| `launcher.ini` | Mode (`stereo`, `mono`), controllers, aim (`hand`, `head`, `view`), per-eye size, world scale, cutscene skip, posture (`auto`, `seated`, `standing`), eye height (`slayer`, `real`), `ipd_mm` (0 = the headset's), `recenter_hold`, turning (`turn` smooth, snap or off, `snap_degrees` 15-90, `turn_rate` 150-400), `handedness` (`right`, `left`, `left_mirror`), `locomotion` (`head`, `hand`), `dossier` (`hold`: X hold opens the Dossier; `tap`: X tap does), `aim_dot` (1, or 0 to hide the hand-aim dot), `anti_aliasing` (`taa`, or `dlss`: experimental, NVIDIA RTX only), `body_follow` (1 or 0), `aim_smoothing` (0 to 1), `hud_distance`, `hud_width`, `hud_height` (metres), `mirror` (`left`, `right`, `off`), `mirror_display` (`auto`, `primary`, or a display's top-left `x,y`), `mirror_size` (`WxH`, default 1280x720), `mirror_crop` (`full`, `16:9`), `cinema_aspect` (`16:9`, `16:10`, `full`), `cutscene_view` (`cinema`, `immersive`), `shot_origin` (`hand`, `eye`), `aim_dot_size` (degrees), `menu_beam` (1 or 0), runtime, game and layer folder overrides, extra arguments; `schema_version`. Every key after the paths is optional (a missing one takes its default), and keys the launcher does not know are kept as they are |
| `logs\launcher.log`, `logs\<session>\` | The launcher's log; the layer's log folder for each session |
| `snapshots\<session>\` | Settings copies, `SHA256SUMS`, `locations.txt`, `restore.txt`, files replaced by the restore (ten kept) |
| `save-backups\<session>\` | Save slots with `SHA256SUMS` (five kept); `pre-restore-*` holds the saves found before a "Restore saves" |
| `SESSION_PENDING`, `REGISTRATION_PENDING` | Crash-safety markers |

## Version check and Export report

Preflight compares the launcher's version (`Directory.Build.props`, read from the exe's informational
version) with the layer's (`CMakeLists.txt`'s project version, in `EternalVR.dll`'s version resource, read
without loading the DLL) and refuses a launch when two release versions differ. Debug builds carry `-dev`
on either side, and a DLL built before the version resource reads as unknown: both only warn
(`Preflight/VersionCheck.cs`).

**Export report...** (or `--export-report <zip>`) collects the files listed in `Report/ReportManifest.cs`,
redacts them (`Report/Redactor.cs`) and zips them in memory; the window shows the file list and size
before asking where to save. `docs/release/TROUBLESHOOTING.md` documents the manifest and the redaction
rules for testers.

## Start-up checks, status and the session finisher

- **Before anything else** `Program.Main` checks the program folder without touching the Core assembly
  (`InstallCheck.cs`): started from a temporary folder (from inside the zip) or with `EternalVR.Launcher.Core.dll`,
  `data\` or (window only) `layer\` missing, it says to extract the whole zip. A global handler
  (`CrashHandler.cs`) logs anything nothing else caught and shows a plain dialog.
- **Preflight refuses** an unknown game build (the layer's hooks are pinned to the builds in `known-builds.txt`),
  a Microsoft Store / Game Pass install and a game never started on this PC (no settings folder), and **warns**
  about 8 GB of video memory or less, more than one GPU, injector DLLs in the game folder (ReShade, SpecialK,
  OptiScaler), a system Vulkan loader older than 1.3.234 and paths outside ASCII
  (`Preflight/CompatibilityChecks.cs`).
- **The status line** under the buttons shows each session's outcome: a refusal, the Steam hand-off, an early
  exit, the layer's `eternalvr-status.txt` (starting, waiting for the headset, VR in stereo or mono, VR off with
  the reason; a problem only after it has stood 5 s with the game running) and a mod that never wrote
  `LAYER_LOADED` within 45 s (`Launch/LayerStatus.cs`). Problems also open a dialog.
- **The session finisher**: with each session the window starts a windowless copy of itself,
  `--finish-session`, which waits for the game to exit and restores the settings if the window is no longer
  open (closed or killed); while the window is open it leaves the restore to it (`FinishSession.cs`, e2e 2b/2c).
- A pending restore that fails is retried after 5 s, then twice as long each time up to 2 minutes; after three
  failures the window offers **Discard pending restore...**. **Restore saves...** lists every backup.
- The OpenXR probe waits up to 30 s (SteamVR and WMR cold starts), and only the newest 20 `logs\<session>`
  folders are kept.

## Not in v0

Profiles and bindings, the TOML settings file for the layer, checking that the runtime is up and the
headset connected, the update check, OptiScaler and DLSS management, and uninstall (T-113).
