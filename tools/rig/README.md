# Rig scripts

PowerShell scripts for launching DOOM Eternal runs on the development rig without touching the owner's
screen, speakers, keyboard focus or settings. Windows PowerShell 5.1; no administrator rights, no
prompts, every wait bounded. Run each one as:

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools\rig\<script>.ps1 [parameters]
```

or, from PowerShell, `& tools\rig\run.ps1 -Exe retail -Args @('+logFile 2', '+com_skipIntroVideo 1')`.
With `-File`, `-Args "a","b"` arrives as the single string `a,b`; `run.ps1` splits it again at every comma
followed by `+` or `-` (and `-GameEnv` at every comma followed by `NAME=`).

The contract is `docs/DECISIONS.md` T-097; `tests/run-tests.ps1` is its specification.

## Work session

```
session.ps1 start          # new session ID; cleans up finished runs, removes a display an earlier session left
run.ps1 -Exe retail -Args "+logFile 2"
collect.ps1                # screenshot of the virtual display, game logs since the run started
stop.ps1                   # mute back to its prior state, window-close request, forced stop after 20 s, restore
...more runs...
session.ps1 end            # cleanup and removal of the virtual display (end of the work block)
```

## Scripts

| Script | What it does |
|---|---|
| `run.ps1 -Exe retail\|sandbox\|lab\|<path>` | Refuses when elevated, when `steam.exe` is not running, when a game process (`DOOMEternalx64vk`, `DOOMSandBox64vk`, `idTechLauncher`) is running, when a pending run cannot be cleaned up, or when the settings differ from the last verified restore (before the first one: from the pre-development backups in `<workspace>\backups\*\*-pre-dev`). Then: `CLEANUP_PENDING` first, snapshot of every settings location (saves included) into `config-before\` with `SHA256SUMS`, virtual display for the work block (2560x1440, `-DisplayWidth`/`-DisplayHeight`), exe started from the game root with `SteamAppId=782330` and `+r_fullscreen 0 +r_windowWidth W +r_windowHeight H` (the virtual display's size, unless `-Args` sets them) plus `-Args`; the game's own audio session is muted as soon as it exists, with its prior mute state recorded; the window is moved onto the virtual display without being activated, and if the game takes the foreground it is handed back to the window that had it before. The game opens its window about a minute after it starts, later than this launch watch, so a hidden `guard.ps1` keeps doing both for the rest of the run (logged as `guard:` lines in the run's `rig.log`, summary in `guard.json`; it ends with the game or the cleanup; with `-KeepFocus` it only keeps the window on the virtual display; `-Owner` runs have none). `run.json` records it all. Returns while the game runs. Options: `-Label`, `-NoDisplay`, `-NoMute`, `-Owner` (the owner at the controls: his display and sound, no forced cvars, focus left alone, same snapshot and restore), `-GameEnv NAME=VALUE`, `-AcknowledgeSettingsChange` (only after the owner confirmed a reported change is his). Exit codes: 0 running, 1 error, 2 refused, 3 exited early (cleaned up), 4 hand-off (tracked; our environment may be lost). |
| `stop.ps1 [-Run <folder>\|latest] [-EndBlock] [-AcknowledgeSettingsChange] [-RestoreCloudFiles]` | The only script that stops a live run: puts the audio-session mute back to its prior state, sends `WM_CLOSE` to the run's windows, waits up to 20 s, stops what remains, waits until the processes have left the process list, then cleans the run up. |
| `cleanup.ps1 [-Run <folder>] [-All] [-EndBlock] [-AcknowledgeSettingsChange] [-RestoreCloudFiles]` | Idempotent; never stops a process (a live run is skipped) and, without `-Run`, never touches an owner run. For each pending run: waits for Steam's sync (nothing under `782330` changing for 10 s, at most 120 s), copies the post-exit settings to `config-after\` (once), then restores every changed or removed local settings file from `config-before\` (the file being replaced is first kept in `config-replaced\<attempt>\`; temporary copy, rename, SHA-256 check; the Steam-Cloud files keep the game's version, see Steam Cloud below) and clears `CLEANUP_PENDING` only after everything verifies. A file that changed after the run ended (it differs from `config-after\`) is held: reported, not overwritten, and restored only with `-AcknowledgeSettingsChange`. Files added during a run are kept and listed. On failure the marker stays, `cleanup.json` says why, the exit code is 1 and `run.ps1` refuses until it is resolved. `-EndBlock` also removes the virtual display. |
| `display.ps1 add\|remove\|status [-Width W -Height H]` | Extends the desktop onto the `VDD by MTT` monitor (`MTT1337`) with `SetDisplayConfig`, never as primary, and sets its mode with `ChangeDisplaySettingsEx` on that display only (no registry update; the requested mode, else the largest it offers). `remove` deactivates only the virtual display's path and compares the result with the topology saved before the add. `DISPLAY_ADDED` and `display-before.json` in the runs root; every change is logged with the topology before and after in `display-log.jsonl`. No display change while a game process runs; a failing display query is an error (the marker stays), not "unavailable". If Windows would need elevation for a change, it is reported, never elevated. `status` is read-only (the add check uses validation only) and shows the virtual display's mode. If the monitor is not available, `run.ps1` continues without it and warns. |
| `collect.ps1 [-Run]` | Screenshot of the virtual display (the primary display if none) into `screenshots\`; `*.log` files written since the run started from the game root, `base\` and the Saved Games `base\` into `logs\`. |
| `session.ps1 start\|end\|status` | Session ID in `<runs>\SESSION`; see above. |
| `keys.ps1 -Press\|-Hold <key> [-Ms] [-KeepFocus]`, `keys.ps1 -MoveX dx [-MoveY dy] -Steps n [-StepMs]` | Key presses and relative mouse moves (`SendInput`, which raw input sees) to the game window (the process's `Ghost_CLASS` window, not an OpenXR runtime's preview window in the same process); it is focused for them and the previous foreground window gets focus back unless `-KeepFocus`. |
| `launch-ht.ps1 -Layer <staged build> -Label <label> [-DisplayWidth -DisplayHeight] [-XrRuntimeJson] [-WindowSize WxH] [-Stereo [-StereoSameView] [-CaptureEyes <dir>,<N>]]` | A head-tracked run into e1m1 with the layer's cinematic skip, window captures and a timeline; see `docs/VR_HEAD_TRACKED.md` (Launch). `-Stereo` runs Route S with per-eye temporal history (`docs/VR_STEREO.md`), `-StereoV1` without temporal accumulation, `-StereoDlss` with DLSS per eye. |
| `qa/qa-suite.ps1 [-LayerSrc] [-Out] [-Only <scenarios>] [-LauncherBin]` | Automated QA of a layer build on the simulator: scripted-input scenarios through `launch-ht.ps1` and `stop.ps1`, screenshots, layer-log assertions, the 100% save restored after every run, and the launcher e2e suite; writes `report.md` (`qa/README.md`). |
| `cpu-cvar-ab.ps1 [-Tree] [-Rounds 4] [-Only <sets>] [-ParseOnly]` | A/B runs of CPU-saving cvar sets (`docs/rig-findings/perf-cpu-cvars.md`): each set once per round, interleaved with a base run, through the rig's `rsrun.ps1` (Route S on the simulator, `e1m2_battle` start, `ETERNALVR_CPU_TIMING=1`, the set as `ETERNALVR_DEBUG_CVARS`), about 60 s in the map, then `stop.ps1 -RestoreCloudFiles` (the batch stops unless it reports `cleanup done`); prints ticks/s and the per-eye stage times per run and per set. `-ParseOnly` only reads the logs. |

Only one mutating script runs at a time: `run`, `stop`, `cleanup`, `session start/end` and
`display add/remove` take `<runs>\LOCK` (PID and start time; a lock whose process is gone is taken over),
wait up to 120 s for it, then refuse with `BUSY`.

A run folder `<runs>\<yyyyMMdd-HHmmss>-<label>\` holds `CLEANUP_PENDING` (until a verified restore),
`run.json`, `config-before\`, `config-after\`, `config-replaced\`, `cleanup.json`, `stop.json`, `rig.log`,
`screenshots\` and `logs\`. Run folders are never moved or deleted by the scripts.

## Steam Cloud

Two classes of settings file (T-115). The Saved Games tree is local: text configs, `user\config.json` and
logs, restored whole. Everything under Steam's `userdata\<id>\782330\remote\` (`PROFILE\profile.bin` and
the save slots) is Steam-Cloud synced: Steam records each file's size and SHA-1 in
`782330\remotecache.vdf`, and a file put back behind its back leaves that record stale, after which the
game resets its profile at the next launch (`docs/rig-findings/launcher-live.md`, open question 8).

- **Default:** a changed, removed or added cloud file keeps the game's version. `cleanup.json` records it in
  `cloudFiles` (SHA-256 and size before the run, after the exit and at the cleanup) and `cloudKept`, the
  log has a `WARN` line per file, and the next run's baseline expects the kept version.
- **Forced cvars (T-092):** a forced cvar name (the run's `+name value` arguments) that occurs more often in
  a kept cloud file than before the run is a `WARN`, and so is any kept cloud change after a run that
  forced cvars. The text configs, where the forced cvars land on this rig, are restored as always.
- **`-RestoreCloudFiles`** (cleanup.ps1, stop.ps1; only for a pending run): restores the cloud files as
  well, then `Invoke-RigCloudResync` starts the retail exe directly (`SteamAppId=782330`,
  `+r_fullscreen 0 +s_volume 0`) and kills it 4 s in, before the profile load, so that Steam takes the
  restored files as current at the end of that app session; then it waits for Steam's sync. The launch
  must leave the cloud files as restored (local files it touched are restored again), and the marker is
  cleared only when `remotecache.vdf` matches every cloud file; otherwise it stays and a retry resyncs.
  It starts the real game, so it is never used while someone else is using the game.
- **Check:** the cleanup and `run.ps1` compare `remotecache.vdf` with the cloud files and report a stale
  record (`ERROR` in the cleanup log, `STEAM_CLOUD_RECORD_STALE` in `run.json`); neither refuses on it.
  The scripts only read Steam's files.

If a run is killed while the game's session is muted, Windows remembers that mute for the exe; the run
records "mute may persist", and the next muted run of the same exe knows the mute is ours and puts it
back to unmuted when it stops.

## Paths

| | Default | Variable |
|---|---|---|
| Runs root | `<workspace>\runs` | `EVR_RIG_RUNS_ROOT` |
| Game root | `E:\SteamLibrary\steamapps\common\DOOMEternal` | `EVR_RIG_GAME_ROOT` |
| Lab copy | `<workspace>\lab-install` | `EVR_RIG_LAB_ROOT` |
| Saved Games settings | `%USERPROFILE%\Saved Games\id Software\DOOMEternal` | `EVR_RIG_SAVED_GAMES` |
| Steam | `HKCU\Software\Valve\Steam\SteamPath` (read only), else `C:\Program Files (x86)\Steam`; every `userdata\*\782330\remote` is a settings location | `EVR_RIG_STEAM_ROOT` |
| Pre-development backups | `<workspace>\backups` | `EVR_RIG_BACKUPS_ROOT` |

`<workspace>` is `EVR_WORKSPACE` when set, otherwise the folder that contains the main checkout
(`workspace.ps1`).

The scripts write only under the runs root and the settings folders (restores), and refuse any other
path. Timeouts (close 20 s, kill 10 s, Steam sync 10 s quiet / 120 s, watch 10 s, window 30 s, mute 60 s,
lock 120 s) are fixed on the rig.

## Keeping runs off the owner's desktop

- **Windows.** `run.ps1` moves every window of the game process onto the virtual display without activating
  it, the OpenXR-Simulator preview included (it opens on the primary display first and is moved once it
  appears). A `-NoDisplay` run leaves them on the primary display: use it only when a test needs a real
  display, and never move or resize the preview there by hand or by script. The virtual display's place is
  read from the live layout at launch; when a monitor changes mode during a run (the TV switching
  resolution), the virtual display can shift and the game window end up on the owner's screen: stop the run.
- **The preview is not a measuring instrument.** Its shape follows its own window (it can be stretched);
  judge proportions and measure from the layer's captures (`ETERNALVR_CAPTURE_EYES`, `ETERNALVR_CAPTURE_UI`).
- **Input.** While the game's window is not in the foreground, the layer neutralises the desktop's raw
  mouse and keyboard records the game still reads (no motion, no wheel, no presses; releases pass, so
  nothing stays held; `keys: the desktop's mouse or keyboard reached the game while it is not in the
  foreground`, counted in the `menu:` 10 s line) and keeps the game's cursor moves and clips off the
  desktop; only the layer's own input moves the game then. The records are still handed over: failing the
  read made the game spin on it and stop answering its window. With the game in front, input passes as
  usual.

## Tests

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools\rig\tests\run-tests.ps1 [-TestApp <exe>] [-Only <name>]
```

No game, no real settings, no real Steam folder, no display changes: each test uses its own folder under
`<workspace>\tmp-rigtests` (on CI `%RUNNER_TEMP%`), fake settings trees, the test
application `testapp/` (CMake target `evr_rig_testapp`, built with the project) as the game, and a JSON
stub as the display controller. Test mode (`EVR_RIG_TEST_ROOT`) enables the seams: elevation probe,
Steam process name, game process names, shorter timeouts, pauses and kill points (`EVR_RIG_*`); it
refuses every path outside the test root. Outside test mode the seams are ignored. The suite covers the
refusals, marker first, snapshot and verified restore, idempotent cleanup, a failed restore that keeps the
marker and blocks the next run, a corrupt snapshot, files changed after the run ended, the lock, the
implicit-cleanup rules, graceful, slow and forced stops, a run killed at each step (and a cleanup killed
mid-restore), early exit, hand-off, comma-joined arguments, paths with brackets, the display fallback,
failure, mode and work-block rules, owner runs, the audio-session mute and its prior state, the
pre-development baseline, the Steam-Cloud rules (a fake `remotecache.vdf` in Steam's format: the game's
cloud versions kept and reported with the forced-cvar warning and taken as the next baseline, a stale
record reported, and `-RestoreCloudFiles` with a stand-in resync launch that Steam accepts, or not, so the
marker stays until a retry), and that the seams are off outside test mode. The resync launch has its own
test seams (`EVR_RIG_RESYNC_EXE`, `EVR_RIG_RESYNC_ARGS`, `EVR_RIG_RESYNC_KILL_MS`).

## Getting into a level without a human

```
run.ps1 -Exe retail -Args @('+com_skipKeyPressOnLoadScreens 1','+com_skipIntroVideo 1','+com_skipSignInManager 1',
    '+hud_skipCinematic_holdTimeOverride 0.05','+r_hdrDisplay 0','+map game/sp/e1m1_intro/e1m1_intro')
keys.ps1 -Hold R -Ms 1500 -KeepFocus      # skip a cinematic
keys.ps1 -Press ESC -KeepFocus            # leave the pause menu
```

Without the layer, the game pauses whenever its window loses focus (`in_controlInactiveWindow` and
`menu_dontpause` do not change that), so flat runs that need the game to keep playing leave it focused
(`-KeepFocus`). With a VR session running, the layer's keep-active hook keeps the game playing in the
background, so VR runs do not need `-KeepFocus`: leave the foreground (and the mouse) with the owner.
`keys.ps1` writes `runs\FOCUS_HOLD` while it sends keys, so the guard does not take the focus back
mid-press (with `-KeepFocus`, until the game exits).
