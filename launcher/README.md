# EternalVR launcher (v0)

The desktop launcher that starts DOOM Eternal with the EternalVR layer for one session and puts
everything back afterwards. Design: `docs/ARCHITECTURE.md` sections 4, 4a and 12; what v0 covers and
where it differs from the design is T-113 in `docs/DECISIONS.md`.

| Project | Target | What it is |
|---|---|---|
| `src/EternalVR.Launcher` | .NET Framework 4.8, WinForms | The window, the command-line modes and the Windows side (registry reads, processes, the flag-gated HKCU registration) |
| `src/EternalVR.Launcher.Core` | netstandard2.0 | Everything that can be tested without Windows: VDF parsing, Steam and Game Pass discovery, config snapshot and key restore, save backups, preflight decisions, hash check, launch plan |
| `tests/EternalVR.Launcher.Core.Tests` | net8.0, xUnit | Unit tests of the core; any OS |
| `tests/FakeGame` | .NET Framework 4.8 | A stand-in for the game used by `tests/e2e.ps1` |
| `tests/UiShots` | .NET Framework 4.8, WinForms | `evr-ui-shots`: every window and dialog state as a PNG at the display's scale, for visual QA (`tests/UiShots/README.md`) |
| `data/*.txt` | | Known builds, forced cvars, session (window and display) keys, refused multiplayer arguments, known layers, anti-cheat names, the CPU Saver items, the DLSS downloads, the headsets known by name with their native panels (`headsets.txt`). Copied next to the exe |

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
`launcher\tests\update-e2e.ps1 -Root <dir> [-From 0.1.13]` tests the in-place update on the published
releases: it downloads release `-From`, runs its launcher with a data folder under `-Root` and drives the update
dialog through UI Automation (Later, Skip this version, a failed check, a failed download, Try again, the restart
into the newest release); it needs the network and a desktop.

On the development rig the SDK is user-local and every cache stays on the development drive:
`<workspace>\tools\evr-launcher.cmd [checkout]` builds and runs the unit tests
(`tools\dotnet-env.sh` sets the same environment for Git Bash).

## Using it

Put the layer next to the launcher as `layer\VK_LAYER_ETERNALVR.json` and `layer\EternalVR.dll` (the
CMake build's `src\vkcore` output), or point `--layer-dir` or `layer_dir` in `launcher.ini` at a build
folder. Start Steam (not needed for the Game Pass version), then `EternalVR.Launcher.exe`. Launch VR is
enabled when preflight has no failure.

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
campaign, a mission or a save in the game's menus. Mono (head-tracked, one image for both eyes) remains a
choice. In both modes the launcher turns the UI layer on (`ETERNALVR_UI_LAYER=1`,
docs/rig-findings/ui-layer.md): the HUD on its own quad, and the menus over the game on the menu panel
with the pointer (docs/VR_MENUS.md).

In stereo the launcher forces the stereo cvars (`r_TAASafeMode 0`, `r_antialiasing 1`: temporal AA with a
history per eye, docs/VR_STEREO.md; the temporal effects whose history the eyes would still share off:
`r_TAAAntiGhosting`, `r_SSDOTemporalAA`, `r_lightScatteringTAA`, `r_dofTAA`, `r_waterReflectionsTAA`,
`r_waterGridTAA`, `r_refractionTAA`, `r_raytracedReflectionsTemporalUpscaleQuality`; `rs_enable 0`,
`r_swapInterval 0`: no dynamic resolution, no vsync on the doubled render rate) and a windowed game
(`r_fullscreen 0`, `r_windowWidth`/`r_windowHeight`), and hands the layer the window's place and size
(`ETERNALVR_WINDOW=x,y,width,height`, client area).

"Texture streaming" (Play tab, Picture) and the "CPU Saver" group (Play tab) are the items of
`data\cpu-saver.txt`, one checkbox each (stereo only): game cvars that cut the processor's work per render
(`docs/rig-findings/perf-cpu-cvars.md`). Each item has an id, a default, the row it goes in, its checkbox text,
its tooltip and its cvars; the window builds the checkboxes from the file. Texture streaming (`is_cacheGreedily 0`:
the streamer loads only the mips a view needs, +8% on the rig, mostly lossless) is on by default; the CPU
Saver's items (your own shadow, near sun shadows, model detail distance, decal distance, distant shadows and
lights) are off by default. A stereo launch with a settings location hands the cvars of the items that are on to
the layer (`ETERNALVR_CPU_SAVER=name=value;...`, absent when none is on), which holds them at run time; `<=N`
values only lower a cvar the game's menu sets per quality level. The cvars of every item are restored in the
game's configs after every session like the forced cvars, whether the item was on or not.

"Parallel Eye Rendering (highly experimental)" (Play tab, Picture, stereo only, off by default; until 2026-10-10 the
row was shown only with `ETERNALVR_SHOW_PARALLEL_EYES=1`, which is no longer read) renders both eyes as two
views of one game frame, their work at the same time, instead of eye L's whole frame and then eye R's. A stereo
launch with it on sets `ETERNALVR_PARALLEL_EYES=1` and nothing else: the layer runs its two-view renderer on the game
version it knows (the standard renderer on others) and holds what it needs itself, async compute off among them
(`docs/VR_STEREO.md` "Parallel Eye Rendering"). It takes Anti-aliasing TAA, DLSS or Off: with DLSS each view runs its
own DLSS feature (`src/vkcore/view_dlss.hpp`; the layer's `ETERNALVR_PE_DLSS=0`, which the launch never sets, keeps the
standard renderer for DLSS). With it on, Foveated rendering is greyed out ("Not with Parallel Eye Rendering
(Play tab).") and the launch does not set `ETERNALVR_FOVEATION`: the layer's foveation takes each pass's eye from
the standard renderer's eye tags and is off with Parallel Eye Rendering. It runs on one Steam build only
(`BuildCheck.ParallelEyesSteamBuild`, the one the layer knows): on another supported build (Game Pass) the row is
greyed out ("Not on this game version yet."), the launch does not set the variable, and Alternate eyes and Foveated
rendering apply as without it.

Each eye renders at the headset's size, not the window's (`docs/rig-findings/render-size.md`): the launcher
sets `ETERNALVR_RENDER_SIZE` (`render_size` in `launcher.ini`: `auto`, the default, is the runtime's
recommended view size within a 2064x2208 pixel budget; `WxH` is that size) and `ETERNALVR_RENDER_SCALE`
(`render_scale`, 0.50 to 2.00, the number of "Resolution" on the window's Play tab). Resolution's base
(`resolution_base`) says what the number multiplies: `auto` (the default, and every file without the key, so an
update changes no one's size) is the layer's own rule, the budgeted recommendation (a Quest 3 through VDXR at
1.00 gets 2056x2216 per eye); `ask` is the runtime's recommended size itself; `panel` is the headset's native
panel from `data\headsets.txt` (`HeadsetTable`, matched by `HeadsetIdentity`). `ask` and `panel` are worked out
by the launcher into a fixed size (`RenderSizeChoice`, fitted to the runtime's limits and to 8192 per side), so the
layer needs nothing new; without the runtime's answer, or for `panel` without a known panel, the launch uses Auto
and the log says so ("Resolution: ..."). The game window is then only the desktop mirror: 1280x720 at
the top-left of a virtual display when there is one (a virtual display driver, Virtual Desktop's or Meta's
virtual monitor), else of the primary display (`ETERNALVR_MIRROR_WINDOW`, the same as `ETERNALVR_WINDOW`), and
the layer scales the eye into it. If the layer cannot set the render size the game renders at the mirror's
size (the layer log says why: `size: render size off`).

Below Resolution, the Play tab's "Each eye" line gives the size each eye will render at and its percent, per
side as Virtual Desktop gives its resolution, of the runtime's recommended size and of the native panel, for
example "2056 x 2216   82% of what VD asks for, 100% of the native panel"; with DLSS, about the size DLSS draws
(the quality's factor), and past a quarter more pixels than Auto at 1.00 a note (`HeadsetView.EachEye`).

The Headset box at the top of the Play tab (`MainForm.Headset.cs`, words in `HeadsetView`) shows the headset and
its route (`HeadsetIdentity`: the runtime's and the system's names from the probe; on SteamVR, whose system name
is only its tracking system, the model SteamVR last saw in `steamvr.vrsettings`), its native panel, the size the
runtime asks for and when it was read, and the last session's refresh rate. The runtime is asked at Launch VR, by
**Detect again** (the same probe on a worker thread; it starts SteamVR when SteamVR is the runtime), and when the
launcher opens, in the background, only if the runtime is already up (`HeadsetAutoRead`: the Virtual Desktop
Streamer for VDXR, `OVRServer_x64` for Meta's runtime, `vrserver` for SteamVR; never another runtime, so opening
the launcher never starts one), and the answer is kept in `headset.txt`; the box says when it was read with another runtime than the one
set now, or when a later try found no headset. At the end of a session the launcher reads the layer's log
(`SessionSummary`: the refresh rate is the shortest display period the session held, a 10 s window at 2 to 6
times it was held to part of it, a steady period that is no such multiple is another refresh rate; the share held
is the layer's time-based `xr: refresh summary` when it logged one, and a share of fewer than 60 windows (10 minutes) is said
as "of a short session"), keeps the result in `headset.txt` and says it in the status line, naming what to change
for the route. A session that asked for Parallel Eye Rendering also says whether it ran, or fell back and why
(`ParallelEyesRun`, from the layer's `parallel eyes: on / off: <why> / not available / FAILED` lines; the status is
a warning when it did not run as asked), kept as `session_parallel_eyes` and in the report. A session whose log has
no headset frame keeps no summary: its line is then only in the launcher log and the status line, and a problem
shown during the session stays on the status line instead.

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
   of `data\forced-cvars.txt` for the mode (`| stereo` ones only in stereo; the comfort set, `| mono`, only in
   mono: in stereo the layer holds it at run time, so a multiplayer guard trip gives it back) and the extra
   arguments on the command line, and in its environment only:
   `SteamAppId=782330`, `VK_ADD_IMPLICIT_LAYER_PATH=<layer folder>`, `ETERNALVR_ENABLE_LAYER=1`,
   `ETERNALVR_LOG_DIR=<data>\logs\<session>`, `ETERNALVR_WORLD_SCALE`, `ETERNALVR_AIM` (`hand`, `head`
   or `view`; hand aim without controllers is sent as `head`), `ETERNALVR_DEMON_AIM` (`hand` or `head`,
   what aims the Revenant while piloting it; only when Revenant aim with is not Same as Aim with, and hand
   without controllers is sent as `head`), `ETERNALVR_MELEE_AIM` and `ETERNALVR_EQUIPMENT_AIM` (`head` or
   `offhand`, what aims melee, or the equipment launcher and the Flame Belch, under hand aim; only when not Same
   as Aim with), `ETERNALVR_CONTROLLERS`, in stereo
   `ETERNALVR_MODE=stereo`, `ETERNALVR_WINDOW` and, with Foveated rendering on, `ETERNALVR_FOVEATION` (`subtle`,
   `balanced`, `aggressive` or `maximum`; not with Parallel Eye Rendering on), `ETERNALVR_PACE` (`headset` with Frame pacing matched to the headset in stereo,
   unless Alternate eyes is Auto without Parallel Eye Rendering; else `off`), `ETERNALVR_SKIP_CINEMATICS`, the room-scale settings
   `ETERNALVR_POSTURE`, `ETERNALVR_HEIGHT`, `ETERNALVR_RECENTER_HOLD` and (when set) `ETERNALVR_IPD`
   (`docs/VR_ROOMSCALE.md`), `ETERNALVR_CONTROLLER_DATA=<data>\controls` (or the active profile's `controls\profiles\<name>`) when that folder holds a map
   of the player's, `XR_RUNTIME_JSON` when a runtime other than the system's is chosen, and
   the disable variables of known-bad layers (read from their own manifests).
5. For 10 s the start is watched. When the started exe exits in that window, the launcher looks for a
   new game process for 8 s more (Steam starts it only after the first has gone): one appearing is a
   hand-off to Steam, reported by name (T-109); none is an early exit.
6. When every game process has exited, the forced keys and the session keys (`data\session-keys.txt`,
   the window and display keys) of the text configs are put back to their snapshot values (a key that
   was absent is removed, and a text config the game created during the session loses every one of
   them); other keys the player changed stay. A key the plan keeps (`LaunchPlan.KeptKeys`, recorded in the
   snapshot's `KEPT_KEYS`) stays as the game saved it when the layer's status file confirms it
   (`ssr_follow=1`, `ssdo_follow=1`: the layer held it at the player's own game setting with per-eye TAA on;
   `LayerStatusFile.Followed`), the game saved it at the value the layer held (`ssr_value=`, `ssdo_value=`; a
   key the game left out counts as its default, 1 for both) and no Extra game argument set it: `r_SSDO` and
   `r_SSR` in stereo with TAA or DLSS (for `r_SSR` also Screen-space reflections at the game's setting), so a
   change made in the game's menu during the session stays (logged as `restore: kept ...: r_SSR as the game
   saved it ("0"; absent before)`). Without the confirmation (the player quit before a map, per-eye TAA failed
   closed, the game's setter was not found, mono, an older layer without the value) they are put back as
   before, and so is a value saved before the follow (the game crashed after it; logged as `restore: not
   kept, saved at another value than the layer held: ...`). Every other byte of the file is kept as it was.
   Other files are compared and a change is logged with the snapshot kept. The Steam-Cloud files (everything
   under `782330\remote\`, the profile and the save slots) are never written, not even when the game removed
   one: that is logged and the game's state kept (T-115). Then the marker is removed, only if it is still
   this session's. If a game process is still running at that point, the restore waits for it.

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

Preflight: not elevated; for Steam, Steam running and logged in (`HKCU\Software\Valve\Steam\ActiveProcess`); no
`DOOMEternalx64vk`, `DOOMSandBox64vk` or `idTechLauncher` running; no pending restore; data folder
writable (a program folder under Program Files only warns); game found and its build known (the exe hash
for Steam, the package version for Game Pass); no anti-cheat files in the game folder; layer folder
complete; OpenXR runtime manifest present
(the system's active one from `HKLM\SOFTWARE\Khronos\OpenXR\1`, read only, unless one is chosen);
implicit Vulkan and OpenXR layers checked against `data\known-layers.txt` (OpenXR Toolkit, `XR_APILAYER_MBUCCHIA_toolkit` and the
older `XR_APILAYER_NOVENDOR_toolkit`, is switched off for the launch through its manifest's disable variable); under SteamVR,
a throttling for the game warns (`framesToThrottle` 1 to 15 in `steamvr.vrsettings`' `steam.app.782330`: Throttling Behavior
Limit at the refresh rate divided by n + 1; Auto removes the key, and 0 is Limit at the full rate; read from SteamVR's own
settings page and `vrserver.exe`); HAGS on warns; no settings
location found refuses (with `--test-exe` it warns and forces nothing); a stale Steam cloud record warns (Steam only); extra arguments asking for
multiplayer (`data\refused-args.txt`) are refused.

## Game Pass

The Game Pass (Microsoft Store) version is found when Steam has none, or when the chosen game folder is a store
one (`Game/GamePassInstall.cs`): every fixed drive's Gaming Services folders (those its hidden `.GamingRoot`
names, and `XboxGames`) are searched for a `Content` folder whose `MicrosoftGame.Config` identity is
`BethesdaSoftworks.DOOMEternal-PC`. The package's own install location (`Program Files\WindowsApps`) is not
used: it can still point there after the game was moved. The exe cannot be opened for reading, so the build
is known by the package version (`gamepass` records in `data\known-builds.txt`); an unknown version is
refused like an unknown Steam build. The launch is the same as for Steam: `Content\DOOMEternalx64vk.exe` is
started directly with the same command line and environment (not through `gamelaunchhelper.exe` or the
Store app). Steam is not checked. The settings are the Saved Games folder only (shared with the Steam
version). The saves go through XGameSave into `%LOCALAPPDATA%\Packages\BethesdaSoftworks.DOOMEternal-PC_*\SystemAppData\wgs`:
that folder is copied into each save backup (skipped when absent), and such a backup is never written back
(Windows syncs those saves with the Xbox cloud); "Restore saves" says where the copy is. There is no Steam
Cloud, so no cloud record check and no resync.

The launcher reads the registry and never writes it, except with `--register-hkcu` (the HKCU
implicit-layer route of ARCHITECTURE section 4, off by default and untested, T-113).

## User data

`%LOCALAPPDATA%\EternalVR\` (`--data-root` overrides it):

| Path | Contents |
|---|---|
| `profiles\<name>.ini` | A VR settings profile (the picker above the tabs): the same keys as `launcher.ini` without the game folder, layer folder, runtime and DLSS file (`dlss_dll_path`), which stay this machine's. Its controls are in `controls\profiles\<name>\`. **Save** writes the settings shown into it; `launcher.ini` gets every change at once |
| `launcher.ini` | Mode (`stereo`, `mono`), `alternate_eyes` (0, `auto` or 1: in stereo each game tick renders one eye, for slower processors; `auto` only while the processor cannot keep up with the headset; passed as `ETERNALVR_ALTERNATE_EYES`, `0` with Parallel Eye Rendering on, docs/rig-findings/alternate-eye.md), `parallel_eyes` (0 or 1: "Parallel Eye Rendering (highly experimental)" on the Play tab, off by default; in stereo passed as `ETERNALVR_PARALLEL_EYES=1`, not set when off; docs/VR_STEREO.md "Parallel Eye Rendering"), controllers, aim (`hand`, `head`, `view`), `revenant_aim` (`same`, `hand`, `head`: what aims the Revenant while piloting it; `same` follows aim), `melee_aim` and `equipment_aim` (`same`, `head`, `offhand`: what aims melee, or the equipment launcher and the Flame Belch, under hand aim; passed as `ETERNALVR_MELEE_AIM` and `ETERNALVR_EQUIPMENT_AIM` when not `same`), per-eye size, world scale, cutscene skip, posture (`auto`, `seated`, `standing`), eye height (`slayer`, `real`), `ipd_mm` (0 = the headset's), `recenter_hold`, turning (`turn` smooth, snap or off, `snap_degrees` 15-90, `turn_rate` 150-400), `vignette` (`off`, `light`, `strong`: the comfort vignette), `glory_kills` (`follow`, `steady`, `fade`, `screen`: how glory kills are shown, passed as `ETERNALVR_GLORY_KILLS`; `follow` by default), `handedness` (`right`, `left`, `left_mirror`), `locomotion` (`head`, `hand`), `dossier` (`hold`: X hold opens the Dossier; `tap`: X tap does), `map_sticks` (`weapon`: the weapon hand's stick pans the Dossier's map and the other stick zooms and rotates it; `other`: the other way round; passed as `ETERNALVR_MAP_STICKS`), `wheel_select` (`stick`, or `hand`: the weapon hand points at the weapon wheel), `aim_dot` (1, or 0 to hide the hand-aim dot), `anti_aliasing` (`taa`, `off`, or `dlss`: experimental, NVIDIA RTX only), `dlss_quality` (`quality`, `balanced`, `performance`, `ultra_performance`, or `dlaa`: the full size, with a newer DLL only, else Quality; passed as `ETERNALVR_STEREO_DLSS_QUALITY`), `dlss_version` (`newest`: the newest DLL listed in `data\dlss-downloads.txt`, which Download in the Play tab's DLSS group fetches from NVIDIA's repository after the player accepts NVIDIA's license, checks by size and SHA-256 and keeps in `dlss\<version>\` in the data folder, the game's own until it is there; `game`; or `file`: the `nvngx_dlss.dll` at `dlss_dll_path`; a newer DLL is passed to the layer as `ETERNALVR_DLSS_DLL` with DLSS in stereo, docs/rig-findings/dlss-dll.md; an older launcher's `dlss_dll = game` is read as `newest`), `dlss_dll_path` (this machine's, kept by Reset), `dlss_preset` (`K` by default, `default` for NVIDIA's pick, `J`, `M`, `L`, `F`; an older launcher's `default` is read as `K`), `resolution_base` (`auto`, the default and every file without the key: the runtime's recommended size within the pixel budget, times `render_scale`; `ask`: the recommended size itself; `panel`: the headset's native panel from `data\headsets.txt`), `sharpening` (`game`, `off`, `low`, `medium`, `high`: `r_sharpening` held at 0 to 3 in stereo, passed as `ETERNALVR_SHARPENING`; `game` leaves the player's own), `screen_reflections` (`game`, the default: screen-space reflections follow the game's Reflections setting, passed as `ETERNALVR_STEREO_SSR` `1` or `0` from the player's `r_SSR`; `off`: held off in VR, passed as `off`), `foveation` (`off`, `subtle`, `balanced`, `aggressive`, `maximum`: experimental fixed foveated rendering, NVIDIA RTX only; passed as `ETERNALVR_FOVEATION` in stereo when not `off`, not with Parallel Eye Rendering on; off by default), `frame_pacing` (`headset`, the default: the game held to one pair of eye images per headset frame, timed to the headset; or `off`: as fast as the game runs; passed as `ETERNALVR_PACE` in stereo, `off` in mono and with `alternate_eyes = auto` unless Parallel Eye Rendering is on, docs/VR_STEREO.md "Frame pacing"; launcher 0.1.11's `pace` is read only when it says `headset`, since 0.1.11 wrote `pace = off` as its default), `cpu_saver_<id>` (`on`, `off`: one key per item of `data\cpu-saver.txt`, stereo only: `cpu_saver_texture_streaming`, on by default, and the CPU Saver's `cpu_saver_own_shadow`, `cpu_saver_near_sun_shadows`, `cpu_saver_model_detail`, `cpu_saver_decal_distance`, `cpu_saver_distant_shadows_lights`, off by default; an item without a key takes its default, and an older launcher's `cpu_saver = on` turns every item without a key on), `throw_gesture` (0 or 1: the off hand's overhand throw fires the equipment launcher, passed as `ETERNALVR_THROW`; off by default), `swing_gesture` (0 or 1: the weapon hand's overhead swing fires the Crucible, passed as `ETERNALVR_SWING`; off by default; both in docs/VR_INTERACTIONS.md), `hands_jump` (0 or 1: both hands thrown up above the head jump, passed as `ETERNALVR_HANDS_JUMP`; off by default, hands higher when seated), `body_follow` (1 or 0), `head_fade` (1 or 0: the fade to black with the head in a wall or too far from the body, passed as `ETERNALVR_HEAD_FADE`; on by default), `aim_smoothing` (0 to 1), `vibration` (0 to 1: 0 off, 0.35 light, 0.6 medium, 1 strong), `hud` (`panel`, `wrist`, `weapon`: where health, armour and ammo are, docs/VR_HANDS_HUD.md), `hud_distance`, `hud_width`, `hud_height` (metres), `mirror` (`left`, `right`, `off`), `mirror_display` (`auto`, `primary`, or a display's top-left `x,y`), `mirror_size` (`WxH`, default 1280x720), `mirror_crop` (`full`, `16:9`), `cinema_aspect` (`16:9`, `16:10`, `full`), `cutscene_view` (`cinema`, `immersive`), `cutscene_arms` (0 or 1: the first-person arms in cutscenes around you, hidden by default; passed as `ETERNALVR_CUTSCENE_ARMS`), `shot_origin` (`hand`, `eye`), `aim_dot_size` (degrees), `menu_beam` (1 or 0), `profile` (the active player profile), runtime, game and layer folder overrides, extra arguments; `schema_version`. Every key after the paths is optional (a missing one takes its default), and keys the launcher does not know are kept as they are |
| `headset.txt` | The headset as the last runtime probe that answered read it: its sizes (`recommended`, `max_image`, `max_swapchain`, each `WxH`), the runtime's and the system's names (`runtime`, `system`), the runtime manifest it ran with (`runtime_manifest`), when and by what (`read_at`, `read_by` launch or detect), and a probe that failed since (`failed_at`, `failed`: the values are then old). Also the last session's (`session`, `session_at`, `session_runtime`, `session_refresh_hz`, `session_refresh`, `session_summary`, `session_controls`, `session_parallel_eyes`: `SessionSummary` at the session's end). For the Play tab's Headset box and "Each eye" line. A file from launcher 0.1.11 or older holds only the sizes. This machine's: no profile or Reset touches it |
| `logs\launcher.log`, `logs\<session>\` | The launcher's log; the layer's log folder for each session |
| `snapshots\<session>\` | Settings copies, `SHA256SUMS`, `locations.txt`, `restore.txt`, files replaced by the restore (ten kept) |
| `controls\` | The controls of no profile ("(none)"): the player's own controller maps (`*.toml`, edited copies of the built-in ones), `README.txt`, and `defaults\` with a copy of every built-in map (`data\controllers` next to the exe), refreshed by **Edit controls...** (the controls editor, which saves the player's maps here) and **Open folder** on the Play tab. `controls\profiles\<name>\` holds each profile's own controls in the same shape (`ControlSets`): a new profile gets a copy of the controls in use, Delete removes its folder, and on start a profile without a folder (an older launcher's, when all profiles shared `controls\`) gets a copy of the maps of no profile; until then it uses them. The launch sets `ETERNALVR_CONTROLLER_DATA` to the active profile's set when it holds a map (with controllers on); the layer reads only the files directly in that folder. See `docs/release/CONTROLS.md` |
| `save-backups\<session>\` | Save slots with `SHA256SUMS` (five kept), or for Game Pass a copy of the package's `wgs` save containers under `gamepass\`; `pre-restore-*` holds the saves found before a "Restore saves" |
| `SESSION_PENDING`, `REGISTRATION_PENDING` | Crash-safety markers |

## Updates

On start (at most once an hour, `updates\state.txt` in the data folder) the window asks GitHub's API for the
public repository's releases (`Update/UpdateClient.cs`, `Update/ReleaseFeed.cs`): drafts and tags that are
not `vX.Y.Z` are ignored, pre-releases count (alphas are published as pre-releases), and a release needs an
`EternalVR-*.zip` asset with GitHub's SHA-256 digest or a `.sha256` asset. A newer one shows **Update to
x.y.z...** (`MainForm.Update.cs`, `UpdateDialog.cs`): **Download and install** downloads the zip into
`updates\<version>\` (size and SHA-256 checked, `Net/VerifiedDownload.cs`), unpacks it into a staging folder
(every entry must stay inside it; every file must match the zip's `SHA256SUMS.txt`, and the launcher's file
version the tag: `Update/UpdatePackage.cs`), then installs it over the launcher's folder: each replaced file
is renamed to `*.evr-old` first (Windows allows renaming a running exe or a loaded DLL) and everything is put
back if a step fails. The window then closes, `Program` starts the new launcher with the same options once
the instance lock is released, and the new launcher deletes the `*.evr-old` files. Installing is refused
(`Update/InstallBlock.cs`, the reason in bold next to the buttons) for a launcher that is not an unpacked release
(no `BUILD-INFO.txt`, as in a build folder) and when the launcher's folder is not writable (for both the first
button opens the release page instead), and while the game runs (asked again when the dialog comes back to the
front, and before installing). A launcher that is not a release is not checked at start (its version need not
match a tag: a build of an old branch would be offered every release); **Check now** still shows what is out.
**Skip this version** is kept in `updates\state.txt`; the Checks and log tab turns the check off (`check = off`)
and has **Check now**.

## Version check and Export report

Preflight compares the launcher's version (`Directory.Build.props`, read from the exe's informational
version) with the layer's (`CMakeLists.txt`'s project version, in `EternalVR.dll`'s version resource, read
without loading the DLL) and refuses a launch when two release versions differ. Debug builds carry `-dev`
on either side, and a DLL built before the version resource reads as unknown: both only warn
(`Preflight/VersionCheck.cs`).

**Export report...** (or `--export-report <zip>`) collects the files listed in `Report/ReportManifest.cs`,
redacts them (`Report/Redactor.cs`) and zips them in memory; the window shows the file list and size
before asking where to save. `docs/release/TROUBLESHOOTING.md` documents the manifest and the redaction
rules for testers. The game's crash reports and console log come from the `base` folder of the Saved Games
settings location (the one the settings snapshot uses, Steam and Game Pass alike;
`Report/ReportBuilder.Game.cs`); the Windows event log entries are read by `Platform/WindowsEventLogs.cs` and
chosen and written by `Report/WindowsEvents.cs`.

## Start-up checks, status and the session finisher

- **Before anything else** `Program.Main` checks the program folder without touching the Core assembly
  (`InstallCheck.cs`): started from a temporary folder (from inside the zip) or with `EternalVR.Launcher.Core.dll`,
  `data\` or (window only) `layer\` missing, it says to extract the whole zip. A global handler
  (`CrashHandler.cs`) logs anything nothing else caught and shows a plain dialog.
- **Preflight refuses** an unknown game build (the layer's hooks are pinned to the builds in `known-builds.txt`),
  a Game Pass folder that is not the game's Content folder and a game never started on this PC (no settings folder), and **warns**
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

The TOML settings file for the layer (the launcher passes `ETERNALVR_*` variables instead), OptiScaler
(preflight only names its DLLs), and uninstall (T-113).
