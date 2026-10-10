# EternalVR alpha

EternalVR is a VR mod for DOOM Eternal. It renders the game in stereo in your headset, tracks your head,
and lets your motion controllers drive the game: you aim with your hand, move and turn with the sticks,
and use the menus with a laser pointer. It is not a port and it changes no game files; a small launcher
starts the game with the mod for one session and puts your settings back afterwards.

This is an alpha for a few trusted testers. It works on my PC, but it is rough, it has been tested on
exactly one PC, and some things you will expect from a VR game are not done yet. Please read
`docs\KNOWN-ISSUES.md` before you start, and expect to send me logs.

The exact version and commit are in `BUILD-INFO.txt`. Please quote them in every report.

## What you need

- **Windows 10 or 11**, 64-bit.
- **An NVIDIA RTX graphics card.** Tested: an RTX 4080, and an RTX 3080 Ti (12 GB). AMD and Intel cards
  have not been tried at all.
- **A headset with an OpenXR runtime.** Tested: Meta Quest 3 through Virtual Desktop, with Virtual
  Desktop's own OpenXR runtime (VDXR). SteamVR, Meta Horizon Link and every other runtime or headset are
  untested; they may work, and I would like to hear either way.
- **Motion controllers:** Meta Touch controllers (Quest). Valve Index, HP Reverb G2, Windows Mixed
  Reality, HTC Vive Cosmos, HTC Vive wands, Pico 4 and Steam Frame controllers have bindings but are
  untested. Other
  controllers (Pimax, for one) get whatever layout their runtime maps them to; the mod's log names the
  controller it saw.
- **DOOM Eternal from Steam**, the current retail build: Steam build 25216728 (Rev 3.2), with
  `DOOMEternalx64vk.exe` having SHA-256
  `69dc13e88d1c19133ead7950dc64ebcbd4a5a3f6bd6f9c336ebffe56df6a1c11`. The launcher checks this and shows
  "Build 25216728: supported". After a DOOM Eternal update, VR stays off (the game runs flat) until an
  EternalVR update supports the new build.
- **Or DOOM Eternal from Game Pass** (Microsoft Store / Xbox app), package version 1.0.56.0. The launcher
  finds it in the XboxGames folder of any drive and shows "Game Pass 1.0.56.0: supported".
- **Steam running and logged in** before you start the launcher (Steam version only).
- **.NET Framework 4.8** for the launcher; Windows 10 1903 and later include it. The mod itself needs
  nothing else installed.
- **Any monitor.** Each eye renders at the size your headset's runtime asks for, fitted within about 4.6
  million pixels per eye by default ("Resolution: Auto" on the launcher's Play tab, about 2056x2216 for a
  Quest 3). Resolution can also render all of what the runtime asks for, or the headset's native panel, and
  its number raises or lowers it. The game's own window is a small 1280x720 mirror on your desktop.

## Single-player only

The mod is for the campaign. It never touches BATTLEMODE or any online mode:

- The launcher refuses to start with command-line arguments that ask for multiplayer (joining a lobby,
  connecting, BATTLEMODE maps and similar).
- Inside the game, the mod watches for multiplayer: a BATTLEMODE screen or map, a lobby, an accepted
  invite. The moment it sees one, every VR feature switches itself off for the rest of that game session
  and the game carries on as a flat game on a screen in the headset. Restart the game from the launcher to
  get VR back.
- If the mod cannot find one of those detection points in your game build, it keeps VR off for the whole
  session rather than guess.

Please do not try to play multiplayer with it anyway.

## What is in this folder

| Path | What it is |
|---|---|
| `EternalVR.Launcher.exe` | The launcher. Start this. |
| `Launch-Parallel-Eye-Test.cmd` | Starts the launcher with the experimental Parallel Eye Rendering option shown (see below) |
| `EternalVR.Launcher.Core.dll`, `EternalVR.Launcher.exe.config`, `data\` | Parts of the launcher |
| `layer\` | The mod itself (`EternalVR.dll`), its Vulkan layer manifest, and the Khronos OpenXR loader. The launcher loads it into the game for each VR session only. |
| `docs\INSTALL.md` | Setup, first launch, what the launcher changes, uninstalling |
| `docs\CONTROLS.md` | Motion controller bindings and menu controls |
| `docs\KNOWN-ISSUES.md` | What is missing or broken in this alpha |
| `docs\TROUBLESHOOTING.md` | Logs, launcher messages, getting your settings back |
| `LICENSE`, `THIRD_PARTY_NOTICES.md` | Licences |
| `BUILD-INFO.txt` | Version, commit and the supported game build |
| `SHA256SUMS.txt` | Checksums of every other file in this folder |

## Testing Parallel Eye Rendering

Parallel Eye Rendering is an experimental way to draw the two eyes. Normally the game draws the whole frame
for the left eye and then again for the right. With Parallel Eye Rendering it draws both eyes as two views of
one game frame, their work at the same time. That saves processor time, not graphics card time, so it can only
help when your processor is what holds the frame rate back; a PC held back by its graphics card gains little.
On the test rig (RTX 3080 Ti, i7-9700K, a desktop test runtime instead of a headset) it drew about 12% more
frames a second when the graphics load was turned down so the processor was the limit, but with ray tracing on
at a Quest 3's resolution both ways ran close to 90 frames a second, so the headset got no more new frames.
How it does in real headsets is what this test is for. It is off by default and still has known problems (`docs\KNOWN-ISSUES.md`, "Parallel Eye
Rendering").

**What it needs:**

- DOOM Eternal from Steam, the build this release supports. On Game Pass the checkbox is greyed out and
  the standard renderer runs.
- An NVIDIA card: it has not been tried on AMD or Intel.
- Anti-aliasing **TAA**, **DLSS** or **Off** (Play tab). With DLSS each eye gets its own DLSS.
- No Foveated rendering and no Alternate eyes: both are greyed out while it is on.
- The mod turns the game's async compute off and the lens flares off while it runs; nothing to do there.
- **12 GB of video memory or less:** set Texture Pool Size in the game's video settings one step lower
  before you try it. At the usual setting a 12 GB card can run out and the game turns into a slideshow.

**Turning it on:** close the launcher if it is open (a second one does not start), then start
`Launch-Parallel-Eye-Test.cmd` (in this folder) instead of `EternalVR.Launcher.exe`. On the Play tab, under
Picture, tick **Parallel Eye Rendering (experimental)**, then Launch VR. It cannot be switched during a session:
quit the game to change it.

**Turning it off:** untick the box, or start `EternalVR.Launcher.exe` as usual (without the script the box
is hidden and Parallel Eye Rendering stays off).

After a session the launcher's status line usually says whether it ran ("Parallel Eye Rendering was on.") or
why it did not. It may not when another problem is shown there; the logs in the exported report always say.

**What to send me:**

- Click **Export report...** right after the session, before you launch again: the report holds the logs of
  that session.
- When something looks wrong in the headset, take an in-headset capture at that moment (hold the left Menu
  button and pull a trigger; other controllers: `docs\TROUBLESHOOTING.md`). It saves both eyes.
- The time it happened, the map, your graphics card, and what was wrong: a slideshow, stutter, the eyes out
  of step, an effect in one eye only, or anything else.

## Please do not share this build

It is for the testers I sent it to. Please don't pass it on or post it anywhere; a public release will
come when it is ready.

## Reporting

When something goes wrong, click **Export report...** in the launcher and attach the zip it saves to a
GitHub issue (or your Discord message), with what you were doing. It holds the logs (the game's crash reports too), the versions, your PC's relevant
settings, the game's settings files and your own controls, with your user folder, user name, computer name, the
name you play under and Steam account ID replaced by placeholders; `docs\TROUBLESHOOTING.md` lists
exactly what is in it.
