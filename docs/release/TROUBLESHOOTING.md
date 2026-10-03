# Troubleshooting

## Logs

Everything is under `%LOCALAPPDATA%\EternalVR\` (the **Open data folder** button on the launcher's Checks and log tab opens it):

| Log | What it holds |
|---|---|
| `logs\launcher.log` | Every launcher start: the checks, the launch plan (the game's command line and environment, the window and render size), the settings restore after the session |
| `logs\<session>\eternalvr-*.log` | The mod's log for that session, written from inside the game: your GPU, the OpenXR runtime, which features found what they need in the game, the multiplayer guard, controller actions, the game's own video settings (a `game settings:` line a few seconds after each map load and again when one changes; not in multiplayer), and errors |

The session folder names start with the date and time, so the newest is the last one.

## Export report

After a problem, click **Export report...** in the launcher (or run
`EternalVR.Launcher.exe --export-report <file.zip>`). It shows the files it will save and their total size,
then asks where to save the zip (the Desktop, `EternalVR-report-v<version>-<date>-<ID>.zip`). The ID is six random letters and
digits, also at the top of `system.txt`; it only tells reports apart and says nothing about you or your PC. Attach that zip to a GitHub
issue (or your Discord message) and say what you were doing. If the game went wrong in an older session than the newest
five, zip that `logs\<session>` folder by hand as well.

**Something looks wrong in one eye, or only in the headset?** Hold the left Menu button and pull a trigger
while you see it (under SteamVR with Touch-type controllers: hold both sticks pressed, then pull a trigger; Index: the
firm left trackpad press; Steam Frame: View). The mod saves a screenshot of each eye and of the HUD (the log says `capture: saved`),
then carries on: the game does not pause, recenter or fire. Afterwards click **Export report...**: the newest captures go into the zip. Say
which capture shows the problem (the file names hold the time).

What the zip holds, in this order (the list lives in the launcher as `ReportManifest.cs`):

| File in the zip | What it is |
|---|---|
| `report-contents.txt` | This list for your report: every file and its size, what was shortened, left out or not found |
| `system.txt` | Launcher and layer versions and whether they match, Windows version, GPUs with driver version and date, the OpenXR runtime (active manifest and its name, and the one used for launches), the headset as the launcher last read it (the Play tab's Headset box: the headset and its route, the runtime's and the system's names, the native panel, the size the runtime asks for, when and with which runtime it was read, and a try that found no headset since) and your last session's refresh rate and summary, hardware-accelerated GPU scheduling (HAGS) on or off, a few SteamVR settings from Steam's `config\steamvr.vrsettings` (the headset SteamVR last saw, supersampling, motion smoothing and supersample filtering, any refresh rate set, and SteamVR's own settings for DOOM Eternal; nothing else from that file, never the headset's serial number), the game build, for the Game Pass or Microsoft Store game its package as Windows has it (version, Windows' status of it, folder) and the versions and status of Gaming Services, the Xbox app and the Xbox sign-in, the names of the files in the game's `Mods` folder (at most 30, never their contents) and any mod loader at the top of the game folder (EternalModInjector, DEternal_loadMods and the like), the folders in use, and the game's own video settings as your latest session's log last listed them (ray tracing, DLSS, resolution scaling, the Advanced quality settings, field of view) |
| `preflight.txt` | The launcher's checks, run when you export |
| `launcher.log` | `logs\launcher.log`, its last 4 MB |
| `launcher.ini` | The launcher settings |
| `BUILD-INFO.txt` | The release's version, commit and supported game builds |
| `layer/VK_LAYER_ETERNALVR.json` | The layer manifest |
| `controls/*.toml`, `controls/profiles/<profile>/*.toml` | Your own controller maps, the files you changed in the `controls` folder: those used with no VR settings profile and each profile's own; a longer map keeps its first and last 64 KB. Never the built-in copies in `defaults` or the README. The folder names are replaced as the text is (below), with `[player]` for `<player>` and so on, since a Windows file name cannot hold `<` or `>`. With no maps of your own, `report-contents.txt` says so |
| `windows-events.txt` | Windows event log entries of the last 7 days, newest first, at most 20 per log: crashes, crash reports and hangs of the game, the launcher or the mod (`DOOMEternalx64vk.exe`, `EternalVR.Launcher.exe`, `EternalVR.dll`) from the Application log, and graphics driver resets and errors (NVIDIA, AMD, Intel) from the System log. A log Windows does not let the launcher read is named in the file |
| `windows-crashes/*.txt` | Windows' own crash and hang reports (`Report.wer` under `ProgramData\Microsoft\Windows\WER` and `AppData\Local\Microsoft\Windows\WER`) of the game, the launcher or the mod, the newest 3 of the last 7 days: the module and offset the crash was in and every module loaded at the time (another overlay or Vulkan layer shows there). Only the text file, never a memory dump WER keeps beside it |
| `game-crashes/crash-*.html` | The game's own crash reports (`Crash.<computer>.<number>.html` in `Saved Games\id Software\DOOMEternal\base`, about 4 KB each: call stack, registers, exception code, game build, command line), the newest 3 written since the oldest of the newest 5 sessions started or in the last 7 days, whichever reaches back further. The zip names keep only the number. The memory dumps next to them (`crash-dumps`) are never included |
| `game/qconsole.log` | The game's console log from the same folder, of its latest start; a longer log keeps its first 1 MB and last 3 MB |
| `game/DOOMEternalConfig.cfg` | The game's settings file from the same folder (graphics settings, key binds), as it is when you export; a longer file keeps its first and last 128 KB |
| `game/DOOMEternalConfig.local` | The game's local settings file from the same folder (resolution and the like), as it is when you export |
| `sessions/<session>/LAYER_LOADED` | For each of the newest 5 sessions: the layer's note that it loaded |
| `sessions/<session>/eternalvr-*.log` | For each of the newest 5 sessions: the mod's log; a longer log keeps its first 1 MB and last 3 MB |
| `sessions/<session>/eternalvr-frames-*.csv` | The frame timing table of the newest session only: its header and last 3 MB |
| `sessions/<session>/captures/capture-*` | Your in-headset captures (left Menu held + a trigger; under SteamVR with Touch-type controllers, both sticks held, then a trigger; Index: left trackpad pressed firmly + a trigger; Steam Frame: View + a trigger) of the newest 5 sessions, newest first, whole captures up to 48 MB: each eye's image, the HUD image and a small text file |

The text in the zip is at most 20 MB (the zip itself is usually one or two MB); a file that would go past
that is left out and named in `report-contents.txt`. Captures come on top of that and make the zip larger
(the images shrink a lot in the zip; captures past 48 MB are left out, the oldest first). **Never included:** memory dumps (the mod's and the game's), save games and save
backups, settings snapshots, the game's `structured.log` (it holds account IDs) and its other files, older session
folders and anything not in the table. The launcher log names the settings files and keys it restored, as it always does.

**What is replaced before zipping**, in every file:

- your user folder (`C:\Users\<name>`, with `\` or `/`) becomes `%USERPROFILE%`; so does any other
  `X:\Users\<name>` folder except Public and Default;
- your computer name as a word of its own becomes `<computer>` (names shorter than 3 letters are left);
- your Windows user name as a word of its own becomes `<user>` (names shorter than 3 letters are left);
- the name you play under, in the game's console log (`User '<name>' signed in`), becomes `<player>`,
  and the number at the end of that line `<playerid>`; that name as a word of its own elsewhere (a VR
  settings profile named after you, say) becomes `<player>` too (names shorter than 3 letters are left);
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
| Running as administrator | Start the launcher normally, not "as administrator". As administrator, Windows and OpenXR ignore the helpers registered for your user and the runtime chosen in the launcher. |
| Steam is not running / no user is logged in | Start Steam and log in, then **Check**. (Steam version only; the Game Pass version does not need Steam.) |
| Already running: DOOMEternalx64vk ... | Quit the game (and id's launcher if it is open) first. |
| The previous VR session's settings restore has not completed | The game from the last session is still running. Quit it; the restore then runs. |
| The data folder cannot be written | Something blocks `%LOCALAPPDATA%\EternalVR` (permissions, antivirus). |
| The launcher is inside Program Files | Warning only. Moving the folder elsewhere is recommended. |
| DOOM Eternal was not found in the Steam libraries or the Game Pass folders | On the Advanced tab, next to **Game folder**, click **Choose...** and pick the folder that holds `DOOMEternalx64vk.exe` (for Game Pass, its `Content` folder). |
| ... is not the Content folder of the Game Pass DOOM Eternal | Choose the `Content` folder of the game, usually `XboxGames\Doom Eternal - PC\Content` on the drive it is installed on. |
| DOOM Eternal was updated ... Wait for an EternalVR update | VR is refused: your game is a different build from the one this alpha supports. Wait for an EternalVR update for that build. |
| Anti-cheat components found in the game folder | VR is refused. The retail Steam game has none; something was added to the game folder. |
| The EternalVR layer is incomplete | The `layer` folder next to the launcher is missing a file. Unzip again, keeping the folders. |
| Version mismatch: the launcher is X but the layer is Y | The launcher and the `layer` folder come from different releases. Unzip the whole release again into an empty folder, keeping its folders, and start the launcher from there. |
| Launcher X, layer Y: not compared | Warning only. A development build, or a layer too old to carry a version; a release zip always has a matching pair. |
| ETERNALVR_DISABLE_LAYER=1 is set | Remove that environment variable. |
| No active OpenXR runtime is set / manifest does not exist | Start your headset software and make it the active OpenXR runtime (for Virtual Desktop: VDXR), or pick a runtime in the launcher. |
| A layer warning (RTSS, OBS, Bandicam ...) | That overlay or capture tool is untested with the mod. If VR misbehaves, close it and try again. ReShade, Overwolf and other VR mods for DOOM Eternal are switched off for the session automatically. |
| OpenXR layer XR_APILAYER_MBUCCHIA_toolkit (or XR_APILAYER_NOVENDOR_toolkit): disabled for this launch | Nothing to do. OpenXR Toolkit is switched off for the game each launch (its current and older versions), so its settings, Turbo Mode included, do not apply in EternalVR; it stays on for your other games. |
| SteamVR limits DOOM Eternal to half the refresh rate (Throttling Behavior: Limit) | Warning only. SteamVR's per-application setting holds the game at half (or a third, a quarter ...) of the headset's rate, and frame pacing follows it. It is SteamVR Settings > Video > Per-Application Video Settings > DOOM Eternal > Throttling Behavior; Auto does not limit the game and gives no warning. |
| SteamVR uses a custom controller binding for DOOM Eternal | Warning only. SteamVR applies a binding chosen for the game instead of the mod's default: one you edited from the default in SteamVR (it works, `CONTROLS.md`), or one made for another VR mod of DOOM Eternal (it does not). If your controllers do nothing in game, see "Controllers do nothing in game (SteamVR)" below. |
| Hardware-accelerated GPU scheduling is on | Warning only. It caused hitching on my PC; the message says how to turn it off (needs a restart). |
| DOOM Eternal has not been started on this PC yet (no settings folder) | VR is refused. Start the game once through Steam (or the Xbox app), go to the main menu, quit, then try again. |
| Steam's cloud record is stale | Start DOOM Eternal once through Steam, wait for the main menu, quit, then use the launcher again. |
| Extra arguments refused (single-player) | Remove the multiplayer argument from **Extra arguments**. |
| Last session: Your graphics driver cannot render above the window size | Warning only. See "The picture is soft on an AMD graphics card" below. |
| Headset not detected; the render size is decided in-game | Warning only. The launcher could not ask the headset's runtime for its render size (the log says why), so the game starts small and switches size a few seconds in, at Resolution's Auto whatever base you chose. Connect the headset and start its software before launching. |
| Resolution: the headset's native panel is not known | Warning only. Resolution is on your headset's native size, but your headset is not in the launcher's list (the Headset box on the Play tab says "not in our list"), so Auto is used. Please tell me your headset's model. |

## Problems in the game

**The game starts flat, nothing in the headset.** Look for the session's `eternalvr-*.log`:

- No session folder or no log at all: the mod did not load. Check that the `layer` folder is complete and
  that the Microsoft Visual C++ 2015-2022 runtime (x64) is installed. An extra argument on the command
  line that asks for multiplayer also keeps the mod from loading.
- A log that says the multiplayer guard was **refused** or **tripped**: VR was switched off on purpose
  (a detection point was not found in your game build, or the game went into a multiplayer screen).
  Send an Export report.
- A log that stops at the OpenXR runtime: the headset runtime was not running or not connected. Connect
  the headset first, then launch.

**The launcher says the game handed off to Steam.** Steam restarted the game itself, without the mod.
Make sure Steam was running and logged in before you clicked Launch VR, and try again.

**The picture is soft on an AMD graphics card** (the launcher says "Your graphics driver cannot render above
the window size, so each eye rendered at ..."). On AMD Radeon RX 5000 and 6000 cards, and with some newer
AMD drivers, the driver cannot scale the game's image into its desktop window, so each eye renders at that
window's size. The mod makes the window as large as your display allows, in the eye's shape. On an NVIDIA
card the same message means an old driver: update it. What helps:

- Put the game on your largest, highest-resolution display (the launcher's desktop window settings), and
  keep the taskbar small or on another display: the window cannot be larger than the display's free area.
- Update the graphics driver; a newer one may add what is missing.
- The Play tab's "Each eye" line shows the size the last session really got.
- **Workaround that works today: a virtual display.** Each eye renders at the window's size, and with
  "Fill the monitor" the window covers its whole display, so an extra display shaped like a headset eye
  gives each eye close to its planned size. A player with an RX 6750 XT and a Quest 2 plays this way.
  1. Install a virtual display driver, for example
     [Virtual Display Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver), and give it a
     custom resolution about the size of one eye (for example 2000x2100; the launcher's message after a
     session says the planned size).
  2. In the launcher's Advanced tab, set "Window monitor" to the virtual display, "Window size" to
     "Fill the monitor", and turn "Crop to 16:9" off.
  3. Launch VR as usual. The game's window now sits on the virtual display, which you do not need to look at.

  A bigger eye costs frame rate: if it drops too far, use a smaller custom resolution or lower Resolution
  on the Play tab.

The mod's log says why (`size: render size off: ...; each eye renders at the window's ...`). A full fix is
being worked on; an Export report from your PC helps.

**"Failed to allocate video memory", or the game seems to hang a few seconds in.** The game switched to
the headset's render size during the session and the graphics card ran out of memory (seen on 12 GB
cards). Connect the headset before launching so the game starts at that size, and lower "Resolution" (Play tab)
if it still happens.

**Short freezes (about a second) now and then.** Send an Export report and say roughly when they
happened. The mod's log has a `stall:` line for each of the first 30 freezes during play: how long the game
went without a new frame, and what took the time meanwhile (the mod's own work, the game creating shaders
or allocating video memory, or the game saving a checkpoint). A freeze of about half a second right after a
fight or before a cutscene whose line ends with `the game saved a checkpoint in the gap` is the game's own
autosave (`KNOWN-ISSUES.md`). Every 10 s a `vram:` line shows how much video memory the game uses against
what Windows allows it; past 100% Windows moves memory out and back, which stutters (lower "Resolution" on
the Play tab, or close other programs that use the graphics card).

**What frame rate am I really getting?** EternalVR hands your headset a frame at the headset's own rate (90 or
120 a second, say). When the game has not finished a new pair of eye images yet, the newest pair is shown again,
turned to follow your head, so looking around stays smooth, but moving things only update as often as the game
draws. Headset overlays count this differently: some count every frame handed over (then they always show the
headset's rate), others only frames with a new image, which is close to the real rate. The real number is in the
mod's log: every 10 s a `rates:` line says how many `stereo pair(s)/s` the game drew and how many `frame(s)/s` went
to the headset; the `game settings:` line lists the game's video settings the frames were drawn at. After a
session the launcher's status line sums it up ("The game kept up with your headset: about 90 new frames a second at
90 Hz", or "The game drew about 72 new frames a second at 90 Hz"), and the Play tab's Headset box shows the refresh
rate your headset ran at. If the game was well below your headset's rate, see the next entries, or lower
"Resolution" on the Play tab if your graphics card is the limit.

**The launcher says the game was held to half the refresh rate.** When the game falls behind, Virtual Desktop's
SSW, Meta's ASW, Pimax's Smart Smoothing and SteamVR's Motion Smoothing or throttling ask the game for only half
the frames (or a third) and make up the rest; with Frame pacing the game is matched to that. The launcher counts
the 10-second stretches of play where that happened, from the mod's log, and says so after the session ("SteamVR
throttled the game to 72 of 144 Hz for 18% of play"). A large share means the game cannot keep the refresh rate
with these settings: lower the refresh rate in your runtime (or its resolution, or "Resolution" on the Play tab)
until it can. If you set SteamVR's per-game throttling or forced Motion Smoothing for DOOM Eternal, the game stays
at half the rate all the time. Virtual Desktop's SSW set to Always does the same. A refresh rate that changed
during play is said too ("The headset ran at 144 Hz, and at 90 Hz for about 2 minutes").

**Moving things look less smooth than in other VR games.** When the game draws more frames than your headset
shows (say 140 a second on a 90 Hz headset) and each headset frame shows the newest one, the world, your gun
and your movement advance in uneven steps, while looking around stays smooth. "Frame pacing: Matched to the
headset" on the Play tab (Picture), the default since 0.1.12, prevents that: the game draws exactly one frame
for each headset frame, started at the same moment of each. It changes nothing while the game runs below your
headset's rate. Check it is not set to "As fast as the game runs" (or that Alternate eyes is not on Auto, which
turns pacing off). The mod's log has a `pace:` line every 10 s: "with one" close to the number of headset frames
means every headset frame got exactly one new frame.

**Which DLSS preset should I use?** With a newer DLSS than the game's (the DLSS group on the Play tab: NVIDIA's
newest, downloaded once, or a file of your own), the "Preset" makes a big difference on RTX 20 and 30 series cards. One player's RTX 3070 with a
Ryzen 7 3800X (Quest 3 at 72 Hz, ray tracing on, DLSS Performance, "Resolution" 0.80) drew about 60 new frames
a second with preset M and about 80 with preset J. The game's own DLSS 2.3 was faster still (about 87) but
blurrier at medium and long distances. On those cards start with J or K: M looks best but costs the most. That
player settled on "Resolution" 0.90, DLSS Performance and preset J. On a faster card, DLSS 310 with preset K
and Quality (the launcher's defaults) is the one to try first: another player found it by far the sharpest and
cleanest picture in the headset. "In the headset" under the DLSS group says what will run.

**Game Pass: a black screen, then the game closes.** One player's Game Pass install did this on a 10-bit
monitor until they turned on "Reduced color mode" in the Compatibility tab of the game's `.exe` Properties
(right-click the file in the game folder). After that the game and the mod ran normally.

**Game Pass or Microsoft Store: how the launcher starts the game.** Like the Xbox app, the launcher starts the
game inside its Store package (through Windows PowerShell's `Invoke-CommandInDesktopPackage`, which runs a copy of
the launcher there for a moment). Before 0.1.18 it started the game's `.exe` directly, and one player's game then
crashed a second after starting whenever he had played it flat shortly before. The launcher log says
"started inside the package"; if that fails it says why and starts the `.exe` directly. Close the DOOM Eternal
Launcher (the window the Xbox app opens) before you launch VR: while it is open, the launcher counts the game as
running.

**The frame rate is low and lowering "Resolution" does not help.** Your processor, not your graphics card,
is probably what holds the game back. Check that "Texture streaming: Only what you see" on the Play tab is on
(it is by default), then try the "CPU Saver" checkboxes on the Play tab: each turns down one detail
setting that costs processor time, and its tooltip says how much it gained on the test rig and what it costs
in the picture. If that is not enough, try "Alternate eyes" on the Advanced tab: it draws one eye per frame
instead of both, which roughly halves the processor's work per frame. Each eye then updates at half the
rate, so fast motion can look doubled; see `KNOWN-ISSUES.md`. "Auto" does it only while the processor
cannot keep up. Turn it off again if you do not like it.

**Everything is blurry.** See "Render resolution" in `KNOWN-ISSUES.md`.

**Controllers do nothing in game (SteamVR).** The headset works, but the trigger, buttons and sticks do
nothing, there is no menu pointer and no hand aim. SteamVR keeps controller bindings per game, and every VR mod
of DOOM Eternal runs under the same game, so a binding chosen there for another mod (a community or workshop
binding, or one you saved while playing it) replaces the mod's own and binds none of its controls. To go back to the
default:

1. In SteamVR open **Settings > Controllers > Manage Controller Bindings** (or "Show old binding UI").
2. Pick **DOOM Eternal** and your controller.
3. Choose the **default** binding, not a community or custom one.
4. Launch again.

The launcher warns about such a binding before the launch ("SteamVR uses a custom controller binding for DOOM
Eternal") and again after a session in which it bound none of the mod's controls ("Controllers did nothing"),
and the mod's log has a `controllers: WARNING` line when no hand
pose ever arrives while you play. Also check that both controllers are on and tracked in SteamVR's window before you launch.

**The view is turned or at the wrong height.** Face forward and hold both sticks pressed for 2 seconds
to recenter, or use the headset's own recenter (hold the Meta / Oculus button).

**The game crashed.** Send an Export report (above): it holds the game's own crash report and console log
as well as the mod's log. Your settings are restored when the
game has exited (or the next time you open the launcher).

**"The game was ended (exit code -1)".** The game did not crash: it was closed, by itself or by another
program (often you, after it froze: the headset kept showing the last image). If it froze, send an Export
report (above) and say how you closed it.

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
the cloud record is still stale, start the game once through Steam and quit at the main menu. For the Game
Pass version the backup is a copy of the game's save containers only: the launcher does not write it back
(Windows syncs those saves with the Xbox cloud), and **Restore saves...** says where the copy is.

## The game started flat, or VR switched off

The mod writes why in `logs\<session>\eternalvr-status.txt` (and in the session's `eternalvr-*.log`):
`state=` is `vr` while the headset shows the game, `waiting` while no headset is found yet, and `flat` once
VR is off for that session, with `reason=` saying why in one sentence. `stereo=off` means the headset gets
one image for both eyes. Once VR is up the file also names the headset runtime (`runtime=`), the headset as the
runtime reports it (`system=`), the size per eye the runtime asks for (`recommended=`) and the size the game
renders (`render=`), the headset's refresh rate (`refresh_hz=`) and the share of play the runtime held the
game at half its refresh rate or less (`throttled_share=`, 0 to 1). The usual reasons:

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
