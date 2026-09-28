# Troubleshooting

## Logs

Everything is under `%LOCALAPPDATA%\EternalVR\` (the **Open data folder** button on the launcher's Checks and log tab opens it):

| Log | What it holds |
|---|---|
| `logs\launcher.log` | Every launcher start: the checks, the launch plan (the game's command line and environment, the window and render size), the settings restore after the session |
| `logs\<session>\eternalvr-*.log` | The mod's log for that session, written from inside the game: your GPU, the OpenXR runtime, which features found what they need in the game, the multiplayer guard, controller actions, and errors |

The session folder names start with the date and time, so the newest is the last one.

## Export report

After a problem, click **Export report...** in the launcher (or run
`EternalVR.Launcher.exe --export-report <file.zip>`). It shows the files it will save and their total size,
then asks where to save the zip (the Desktop, `EternalVR-report-<date>.zip`). Send me that zip, or attach
it to your GitHub issue if you have access, and say what you were doing. If the game went wrong in an older session than the newest
three, zip that `logs\<session>` folder by hand as well.

**Something looks wrong in one eye, or only in the headset?** Hold the left Menu button and pull a trigger
while you see it (under SteamVR: press Y and pull a trigger straight away). The mod saves a screenshot of each eye and of the HUD (the log says `capture: saved`),
then carries on: the game does not pause, recenter or fire. Afterwards click **Export report...**: the newest captures go into the zip. Say
which capture shows the problem (the file names hold the time).

What the zip holds, in this order (the list lives in the launcher as `ReportManifest.cs`):

| File in the zip | What it is |
|---|---|
| `report-contents.txt` | This list for your report: every file and its size, what was shortened, left out or not found |
| `system.txt` | Launcher and layer versions and whether they match, Windows version, GPUs with driver version and date, the OpenXR runtime (active manifest and its name, and the one used for launches), hardware-accelerated GPU scheduling (HAGS) on or off, the game build, the folders in use |
| `preflight.txt` | The launcher's checks, run when you export |
| `launcher.log` | `logs\launcher.log`, its last 4 MB |
| `launcher.ini` | The launcher settings |
| `BUILD-INFO.txt` | The release's version, commit and supported game builds |
| `layer/VK_LAYER_ETERNALVR.json` | The layer manifest |
| `sessions/<session>/LAYER_LOADED` | For each of the newest 3 sessions: the layer's note that it loaded |
| `sessions/<session>/eternalvr-*.log` | For each of the newest 3 sessions: the mod's log; a longer log keeps its first 1 MB and last 3 MB |
| `sessions/<session>/eternalvr-frames-*.csv` | The frame timing table of the newest session only: its header and last 3 MB |
| `sessions/<session>/captures/capture-*` | Your in-headset captures (left Menu held + a trigger) of the newest 3 sessions, newest first, whole captures up to 48 MB: each eye's image, the HUD image and a small text file |

The text in the zip is at most 20 MB (the zip itself is usually one or two MB); a file that would go past
that is left out and named in `report-contents.txt`. Captures come on top of that and make the zip larger
(the images shrink a lot in the zip; captures past 48 MB are left out, the oldest first). **Never included:** memory dumps, save games and save
backups, settings snapshots, the game's own config files, older session folders and anything not in the
table. The launcher log names the settings files and keys it restored, as it always does.

**What is replaced before zipping**, in every file:

- your user folder (`C:\Users\<name>`, with `\` or `/`) becomes `%USERPROFILE%`; so does any other
  `X:\Users\<name>` folder except Public and Default;
- your Windows user name as a word of its own becomes `<user>` (names shorter than 3 letters are left);
- a Steam account ID becomes `<steamid>`: in the game's `steam-<number>` save folders, in Steam's
  `userdata\<number>` folders, and wherever the same number appears alone; a 17-digit SteamID64
  (`7656119...`) becomes `<steamid64>`.

Version numbers, addresses, timestamps and hashes are kept. You can open the zip and read every file
before you send it.

## Launcher messages

The launcher runs its checks when it opens and when you click **Check**. A failure refuses the launch; a
warning does not.

| Message | What to do |
|---|---|
| Running as administrator | Start the launcher normally, not "as administrator". |
| Steam is not running / no user is logged in | Start Steam and log in, then **Check**. |
| Already running: DOOMEternalx64vk ... | Quit the game (and id's launcher if it is open) first. |
| The previous VR session's settings restore has not completed | The game from the last session is still running. Quit it; the restore then runs. |
| The data folder cannot be written | Something blocks `%LOCALAPPDATA%\EternalVR` (permissions, antivirus). |
| The launcher is inside Program Files | Warning only. Moving the folder elsewhere is recommended. |
| DOOM Eternal was not found in the Steam libraries | Click **Choose folder...** and pick the folder that holds `DOOMEternalx64vk.exe`. |
| Unknown game build | Your game is a different build from the one this alpha was made for. It may still work: the mod switches off what it cannot find. Please tell me your build. |
| Anti-cheat components found in the game folder | VR is refused. The retail Steam game has none; something was added to the game folder. |
| The EternalVR layer is incomplete | The `layer` folder next to the launcher is missing a file. Unzip again, keeping the folders. |
| Version mismatch: the launcher is X but the layer is Y | The launcher and the `layer` folder come from different releases. Unzip the whole release again into an empty folder, keeping its folders, and start the launcher from there. |
| Launcher X, layer Y: not compared | Warning only. A development build, or a layer too old to carry a version; a release zip always has a matching pair. |
| ETERNALVR_DISABLE_LAYER=1 is set | Remove that environment variable. |
| No active OpenXR runtime is set / manifest does not exist | Start your headset software and make it the active OpenXR runtime (for Virtual Desktop: VDXR), or pick a runtime in the launcher. |
| A layer warning (RTSS, OBS, Bandicam ...) | That overlay or capture tool is untested with the mod. If VR misbehaves, close it and try again. ReShade, OpenXR Toolkit, Overwolf and other VR mods for DOOM Eternal are switched off for the session automatically. |
| Hardware-accelerated GPU scheduling is on | Warning only. It caused hitching on my PC; the message says how to turn it off (needs a restart). |
| No DOOM Eternal settings folder was found | Start the game once normally through Steam, then try again. Without it, VR settings are not forced. |
| Steam's cloud record is stale | Start DOOM Eternal once through Steam, wait for the main menu, quit, then use the launcher again. |
| Extra arguments refused (single-player) | Remove the multiplayer argument from **Extra arguments**. |
| Headset not detected; the render size is decided in-game | Warning only. The launcher could not ask the headset's runtime for its render size (the log says why), so the game starts small and switches size a few seconds in. Connect the headset and start its software before launching. |

## Problems in the game

**The game starts flat, nothing in the headset.** Look for the session's `eternalvr-*.log`:

- No session folder or no log at all: the mod did not load. Check that the `layer` folder is complete and
  that the Microsoft Visual C++ 2015-2022 runtime (x64) is installed. An extra argument on the command
  line that asks for multiplayer also keeps the mod from loading.
- A log that says the multiplayer guard was **refused** or **tripped**: VR was switched off on purpose
  (a detection point was not found in your game build, or the game went into a multiplayer screen).
  Send me an Export report.
- A log that stops at the OpenXR runtime: the headset runtime was not running or not connected. Connect
  the headset first, then launch.

**The launcher says the game handed off to Steam.** Steam restarted the game itself, without the mod.
Make sure Steam was running and logged in before you clicked Launch VR, and try again.

**"Failed to allocate video memory", or the game seems to hang a few seconds in.** The game switched to
the headset's render size during the session and the graphics card ran out of memory (seen on 12 GB
cards). Connect the headset before launching so the game starts at that size, and lower "Resolution" (Play tab)
if it still happens.

**Everything is blurry.** See "Render resolution" in `KNOWN-ISSUES.md`.

**The view is turned or at the wrong height.** Face forward and hold both sticks pressed for 2 seconds
to recenter, or use the headset's own recenter (hold the Meta / Oculus button).

**The game crashed.** Send an Export report (above). Your settings are restored when the
game has exited (or the next time you open the launcher).

## Getting your settings back

The launcher restores the keys it changed after every session, and finishes an interrupted restore the
next time it opens. If your flat game still looks wrong afterwards (windowed, wrong resolution, effects
off):

1. Open the launcher once and close it, so any pending restore runs.
2. Check the game's own video settings and set them back by hand; that is always safe.
3. The settings from before each session are in `%LOCALAPPDATA%\EternalVR\snapshots\<session>\` if you
   want to compare them with `Saved Games\id Software\DOOMEternal\base\DOOMEternalConfig.cfg` and
   `DOOMEternalConfig.local`. Copy a file back only with the game closed.

**Saves.** A copy of your save slots is taken before every VR launch (the last five). To put one back,
quit the game, keep Steam running, and click **Restore saves...** in the launcher. It backs up your
current saves first, restores the chosen backup and checks it with Steam's cloud record. If it then says
the cloud record is still stale, start the game once through Steam and quit at the main menu.

## The game started flat, or VR switched off

The mod writes why in `logs\<session>\eternalvr-status.txt` (and in the session's `eternalvr-*.log`):
`state=` is `vr` while the headset shows the game, `waiting` while no headset is found yet, and `flat` once
VR is off for that session, with `reason=` saying why in one sentence. `stereo=off` means the headset gets
one image for both eyes. The usual reasons:

| Reason | What to do |
|---|---|
| This DOOM Eternal version is not the one this EternalVR supports | The game was updated; wait for an EternalVR update |
| The graphics driver lacks a feature, or Direct3D 12 could not start | Update the graphics driver |
| No headset found yet | Connect the headset and start its runtime (Virtual Desktop, SteamVR, Link) |
| The headset runtime uses a different graphics card than the game | On a laptop with two GPUs, make Windows run DOOM Eternal on the GPU the headset uses (Settings, Display, Graphics) |
| The headset disconnected or its runtime stopped | Quit the game and start again from the launcher |
| An online mode or a multiplayer invite switched VR off | By design; restart from the launcher |

No `eternalvr-status.txt` and no `LAYER_LOADED` in the session folder means the mod was not loaded at all:
check that the whole zip was extracted, and that your antivirus did not remove `layer\EternalVR.dll`.

## Antivirus

`EternalVR.dll` is not code-signed and, like every mod of its kind, it changes the running game's code in
memory (in the game's own process, only for the VR session). Some antivirus programs may flag or
quarantine it. If the launcher reports the layer missing, or the mod never loads, check your antivirus'
quarantine, restore the file and allow the EternalVR folder. The release's `SHA256SUMS.txt` lets you check
that the files are the ones I built.
