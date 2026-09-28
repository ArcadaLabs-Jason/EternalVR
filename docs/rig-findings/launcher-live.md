# Launcher v0 on the rig, and what Steam Cloud does with a restored profile

2026-09-26. Rig as in `launch.md`; game build 25216728; no headset: OpenXR-Simulator 1.5.0 chosen as the
launch's runtime (`XR_RUNTIME_JSON`, the system runtime untouched). Launcher from `launcher-v0`
(bdf1679), layer from `main` after the multiplayer guard merge. Data folder: the launcher's real
`%LOCALAPPDATA%\EternalVR`. Run folder: `<workspace>\runs\20260926-021335-launcher-live`
(a settings copy taken before the sessions and the window capture); per-session records in
`<workspace>\tmp-launcher\live`.

## Sessions

Each session was the launcher's command-line session path with the real exe as the "test" exe, so the
launch plan, snapshot, save backup, start watch and restore are the ones the window runs:

```
EternalVR.Launcher.exe --test-exe <game root>\DOOMEternalx64vk.exe --launch --data-root %LOCALAPPDATA%\EternalVR
    --saved-games "%USERPROFILE%\Saved Games\id Software\DOOMEternal" --steam-root "C:\Program Files (x86)\Steam"
    --game-dir <game root> --layer-dir <staged layer>
```

with `launcher.ini` choosing the simulator and `extra_args = +r_fullscreen 0 +map game/sp/e1m1_intro/e1m1_intro`.
The game's audio session was muted and the foreground handed back, as the rig scripts do.

| Check | Result |
|---|---|
| `--dry-run` | Every preflight check passes (Steam logged in, build 25216728 known, layer found, simulator chosen, Virtual Desktop's OpenXR layer disabled for the launch, two settings locations); plan printed; `RESULT: would launch` |
| Layer loads through `VK_ADD_IMPLICIT_LAYER_PATH` | Layer log in `logs\<session>\` with `LAYER_LOADED`; guard armed; e1m1 single-player; head-tracked views, head aim and the cutscene skip (the launcher's `ETERNALVR_SKIP_CINEMATICS=1`) work |
| Forced cvars applied | The game's log echoes the command line with the forced cvars; the session leaves `r_motionblur "0"` and drops `r_hdrDisplay` in `DOOMEternalConfig.local` |
| Normal exit (WM_CLOSE) | Restore at exit: `r_hdrDisplay` back to `"1"`, `r_motionblur` removed (absent before); marker removed |
| Game killed | The launcher sees the exit and restores the same keys; marker removed |
| Launcher killed | `SESSION_PENDING` stays (`state = running`); the game is closed; the next start (`--dry-run`) logs `completing the settings restore of session ...`, restores the keys and removes the marker |
| Save backups | Six sessions and dry runs made six backups; five are kept (the oldest was removed) |
| Steam hand-off | Never reported on a direct launch |
| Window | Laid out as designed (capture `launcher-window.png` in the run folder) |

Not covered: the "Launch VR" button itself (it runs the same `SessionRunner.Run`), a headset, and the
HKCU route (T-113). With the owner's fullscreen setting and the game not in the foreground, the game
created no swapchain in two minutes (it had loaded e1m1): a fullscreen launch needs the game window in
the foreground, which it is when a player starts it. The sessions therefore ran windowed
(`+r_fullscreen 0` as an extra argument). After that fullscreen session the virtual display was found
at 1280x800 instead of 3840x2160 and was set back with the rig's display helper.

The game saved into `GAME-AUTOSAVE0` and rewrote `PROFILE\profile.bin` in these `+map` sessions; the
launcher backs the saves up and, by design (T-113), only reports the `profile.bin` change. Both were put
back from the settings copy after the tests.

## Open question 8: a restored `profile.bin` and Steam Cloud

The game reads and writes `profile.bin` through Steam, and Steam keeps each cloud file's size and SHA-1
in `userdata\<id>\782330\remotecache.vdf`. When a run makes the game write the profile and the file is
then restored on disk with the game closed (the rig's cleanup, or any whole-file restore), the cache
still describes the game's version. At the next direct launch the game logs `WARNING: Profile corrupt,
creating a new one...` (or `Profile error 0x10`), shows the first-run screens (graphics mode, gamma,
accessibility; level 1 on the main menu) and writes a fresh profile, which Steam uploads. The rig logs of the previous evening show the
same: most runs after one that wrote the profile hit one of these warnings, and a streak of "corrupt"
runs continues for as long as each run writes a fresh profile that the cleanup then replaces.
The owner's profile is never lost on disk (the restore puts it back each time), but the cloud copy and
the next run are wrong.

Steam compares the files with the cache at the end of an app session. A run that ends before the game
reaches its profile load (killed about 4 s after start, before `game/shell/shell` loads) leaves Steam to
find the restored file changed and take it as the current version: the cache and the cloud then hold the
restored profile (`remotecache.vdf` shows its size and SHA-1, `syncstate 1`) and the next launch loads it
(`It has been ... seconds since the profile was saved last`). After a run that restores `profile.bin`,
the rig needs this extra short run; launching the game through Steam (which syncs before the start) is
expected to do the same.

**Status (T-115).** The rig cleanup no longer restores the Steam-Cloud files by default. Everything under
`782330\remote\` keeps the game's version; each change is logged as a warning with its SHA-256 and size
before the run, after the exit and at the cleanup (`cleanup.json` `cloudFiles`), a forced cvar name found
in a kept cloud file is a T-092 warning, and the next run's baseline expects the kept version. The local
Saved Games files are still restored whole. `cleanup.ps1` or `stop.ps1 -RestoreCloudFiles` restores the
cloud files and then runs the short launch above as `Invoke-RigCloudResync` (killed 4 s in), and clears
the marker only when `remotecache.vdf` matches every cloud file. Both the cleanup and `run.ps1` read
`remotecache.vdf` (never write it) and report a stale record. Not yet seen on the rig: the default path
over a real run, and the opt-in path end to end. The launcher never writes `profile.bin` (a missing one is
logged and left), and its "Restore saves" is refused while the game runs or Steam syncs, compares the
restored saves with `remotecache.vdf` and, when the record is stale, runs the same short launch, or
tells the user to start the game once through Steam (T-115). Not yet seen on the rig either.
