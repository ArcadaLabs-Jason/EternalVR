# Installing and running EternalVR

Nothing is installed into Windows or into the game. The mod lives in the folder you unzip, and its own
data (settings, logs, backups) lives in `%LOCALAPPDATA%\EternalVR\`.

## 1. Unzip

1. Before unzipping, right-click the zip, choose **Properties**, tick **Unblock** if it is there, and
   click OK. Windows otherwise marks every file as downloaded from the internet.
2. Unzip it to a folder of your own, for example `D:\Games\EternalVR`. Keep the folder structure as it
   is: the `layer` folder must stay next to `EternalVR.Launcher.exe`.
3. Avoid `Program Files`. It works, but the launcher warns about it.

The launcher and the mod are not code-signed, so the first time you run the launcher Windows SmartScreen
may say "Windows protected your PC". Click **More info**, then **Run anyway**.

## 2. Every time you play

1. **Steam copy: start Steam** and make sure you are logged in. The launcher refuses to start the Steam
   copy without it, because the game would otherwise start its own Steam. The Game Pass or Microsoft Store
   copy does not need Steam. With both installed the launcher uses the Steam copy; to play the other one,
   pick its `Content` folder (for example `E:\XboxGames\Doom Eternal - PC\Content`) as the game folder on
   the Advanced tab.
2. **Start your headset's runtime.** On a Quest 3 with Virtual Desktop: start the Virtual Desktop
   Streamer on the PC, connect from the headset, and use VDXR as the OpenXR runtime. With another runtime
   (SteamVR, Meta Horizon Link), make it the active OpenXR runtime in its own settings. Untested, see
   `KNOWN-ISSUES.md`.
3. **Run `EternalVR.Launcher.exe`.** It checks everything it needs and lists the result: failures (the
   launch is refused) and warnings (it will launch). `docs\TROUBLESHOOTING.md` explains each message.
   **Check** runs the checks again.
4. Look at the settings once. The defaults are what I test with: stereo, motion controllers on, hand aim,
   world scale 1.00, posture auto, Slayer eye height, the headset's IPD, recenter with both sticks held, and
   cutscenes skipped automatically. Each change is saved at once.
5. Click **Launch VR**. The game starts. A small 1280x720 game window opens on your desktop (a virtual
   display if you have one); that is expected, the headset gets its own full-size images. Put the
   headset on.
6. In the game, use **Campaign, Continue** or load a save. Starting a new campaign currently has a
   problem, see `KNOWN-ISSUES.md`.
7. When you are done, quit the game normally. The launcher then puts your settings back. You can leave the
   launcher open or close it: if you close it while the game runs, a small helper it started with the game
   (no window) puts your settings back when the game exits. If that helper could not run either (the PC was
   shut down), the restore happens the next time you open the launcher.

## Updating

When the launcher starts, it asks GitHub (at most once an hour) whether a newer EternalVR release is out.
Nothing about you is sent beyond what any web request carries. If there is one, **Update to x.y.z...**
appears next to the other buttons: it shows what is new and offers **Download and install**, **Skip this
version** or **Later**. Installing downloads the release's zip, checks it against the SHA-256 GitHub lists
for it and against the zip's own `SHA256SUMS.txt`, replaces the files in the launcher's folder, and starts
the new launcher. Your settings, controls, profiles and backups in `%LOCALAPPDATA%\EternalVR\` stay as they
are. Quit the game first. When the launcher's folder cannot be written (for example under Program Files), the
dialog says so and its first button opens the release page instead. The check can be turned off, and run by
hand, on the **Checks and log** tab. You can always update by hand instead: unzip the new release over the old
folder, or anywhere new.

## What the launcher changes, and puts back

The launcher starts `DOOMEternalx64vk.exe` itself, directly, for one VR session.

**Only for that game process** it sets environment variables that load the mod and pass your settings:
`VK_ADD_IMPLICIT_LAYER_PATH` (the `layer` folder), `ETERNALVR_ENABLE_LAYER`, `ETERNALVR_LOG_DIR`, the
`ETERNALVR_*` settings, `SteamAppId=782330`, and `XR_RUNTIME_JSON` if you chose a runtime other than the
system's. Nothing is written to the registry, and nothing else on your PC sees these variables. A game
started from Steam afterwards is the normal flat game.

**On the game's command line** it sets the cvars in `data\forced-cvars.txt`. They turn off effects that
look wrong in VR (motion blur, depth of field, chromatic aberration, vignette, HDR output), skip the intro
videos, and in stereo set windowed mode, the window size, per-eye temporal anti-aliasing, no vsync and no
dynamic resolution.

**Your settings.** Before each launch the launcher copies the game's settings files into
`%LOCALAPPDATA%\EternalVR\snapshots\` with checksums: `DOOMEternalConfig.cfg`,
`DOOMEternalConfig.local` and `user\config.json` in `Saved Games\id Software\DOOMEternal`, and Steam's
`PROFILE` file. After the game exits, every key it forced, and the window and display keys in
`data\session-keys.txt`, are set back to what they were in the text configs; anything else you changed
in the game's menus during the session stays. The Steam Cloud files (your profile and saves) are never
written by the launcher; if they changed, that is logged and the game's version is kept. The Game Pass or
Microsoft Store copy keeps its settings in the same `Saved Games` folder.

**Your saves.** Before each launch the save slots are copied into
`%LOCALAPPDATA%\EternalVR\save-backups\` (the last five launches are kept). **Restore saves...** in the
launcher puts a backup back, carefully, because the saves are Steam Cloud files; see
`TROUBLESHOOTING.md`. The Game Pass or Microsoft Store saves are copied too, but never put back by the
launcher, because the Xbox cloud sync owns them; the backup folder is there if you need it.

**The game folder** is never modified.

If the launcher, the game or the PC crashes during a session, a marker file stays behind, and the next
time you open the launcher it finishes the restore first (or waits until the game has exited).

## Where things are

| Path | What |
|---|---|
| `%LOCALAPPDATA%\EternalVR\launcher.ini` | The launcher's settings |
| `%LOCALAPPDATA%\EternalVR\logs\launcher.log` | The launcher's log |
| `%LOCALAPPDATA%\EternalVR\logs\<session>\` | The mod's log for each session (`eternalvr-*.log`) |
| `%LOCALAPPDATA%\EternalVR\snapshots\` | Settings copies from recent sessions |
| `%LOCALAPPDATA%\EternalVR\save-backups\` | Save backups (the last five sessions) |

**Open data folder** on the launcher's Checks and log tab opens it. Paste `%LOCALAPPDATA%\EternalVR` into Explorer's address
bar to get there without the launcher.

## Uninstalling

1. Quit the game. Open the launcher once and close it: if a restore was still pending, it runs then.
2. Delete the folder you unzipped.
3. If you want nothing left behind, delete `%LOCALAPPDATA%\EternalVR` too. It holds your save backups
   and settings copies, so make sure you don't need them first.

Nothing else was installed: no registry entries, no files in the game folder, no services.
