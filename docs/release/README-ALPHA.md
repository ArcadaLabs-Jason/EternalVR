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
  Reality, HTC Vive Cosmos, HTC Vive wands and Pico 4 controllers have bindings but are untested. Other
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
- **Any monitor.** Each eye renders at the headset's recommended size (capped at about 4.6 million pixels
  per eye by default, about 2056x2216; "Resolution" on the launcher's Play tab raises or lowers it). The game's own window is a
  small 1280x720 mirror on your desktop.

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
| `EternalVR.Launcher.Core.dll`, `EternalVR.Launcher.exe.config`, `data\` | Parts of the launcher |
| `layer\` | The mod itself (`EternalVR.dll`), its Vulkan layer manifest, and the Khronos OpenXR loader. The launcher loads it into the game for each VR session only. |
| `docs\INSTALL.md` | Setup, first launch, what the launcher changes, uninstalling |
| `docs\CONTROLS.md` | Motion controller bindings and menu controls |
| `docs\KNOWN-ISSUES.md` | What is missing or broken in this alpha |
| `docs\TROUBLESHOOTING.md` | Logs, launcher messages, getting your settings back |
| `LICENSE`, `THIRD_PARTY_NOTICES.md` | Licences |
| `BUILD-INFO.txt` | Version, commit and the supported game build |
| `SHA256SUMS.txt` | Checksums of every other file in this folder |

## Please do not share this build

It is for the testers I sent it to. Please don't pass it on or post it anywhere; a public release will
come when it is ready.

## Reporting

When something goes wrong, click **Export report...** in the launcher and send me the zip it saves,
with what you were doing. It holds the logs (the game's crash reports too), the versions and your PC's relevant
settings, with your user folder, user name, computer name and Steam account ID replaced by placeholders; `docs\TROUBLESHOOTING.md` lists
exactly what is in it.
