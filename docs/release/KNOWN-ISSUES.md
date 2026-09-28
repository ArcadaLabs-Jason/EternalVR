# Known issues

What I already know is missing or wrong in this alpha. No need to report these, but do tell me if one of
them is worse for you than described here.

## The big ones

- **The HUD is on a floating panel.** Health, armour, ammo and the rest of the HUD are shown on a flat
  panel in front of you that follows your head. A HUD on your wrist or on the weapon is not done yet.
- **Glory kills keep the game's camera.** The game moves the camera during a glory kill; in VR the view
  follows it, facing the demon, and turns with your head from there. The kills are short, but if camera
  motion bothers you, this is where you will feel it. Cutscenes play on a flat 16:9 screen in front of you
  with the game's own camera (`ETERNALVR_CUTSCENES=immersive` puts you in the cutscene's camera instead).
- **Room-scale walking is new and lightly tested.** When you step around the room while standing, the
  Slayer walks after you in short pulses, so you may hear footsteps start and stop. It has been tested
  in a headset, but not much against walls, on stairs or in heavy combat. It
  pauses in menus, cutscenes, the air and while you use the move stick, and it is off when seated. If it
  bothers you, set `ETERNALVR_BODY_FOLLOW=0`. Leaning more than about 60 cm while seated still fades the
  view to black: sit back, or hold both sticks pressed for 2 seconds to recenter. Standing up or sitting
  down is noticed after a second or so (the view blinks while your height is reset); if it is not (a very
  tall chair, no floor height from your headset), hold both sticks pressed for 2 seconds.
- **Only tested on NVIDIA:** an RTX 4080 and an RTX 3080 Ti (12 GB). AMD and Intel graphics cards have not
  been tried.
- **Bindings for HP Reverb G2, Windows Mixed Reality, HTC Vive Cosmos, HTC Vive wands and Pico 4
  controllers are new and untested on real hardware.** Please report how they work. The Vive wands have a
  reduced layout (see CONTROLS.md), and a resting thumb on the trackpad moves or turns you. Pimax and other
  controllers get whatever layout their runtime maps them to.
- **If the headset disconnects** (a Wi-Fi drop, the headset's runtime restarting), VR stays off for the
  rest of that game session. Quit the game and start it again from the launcher.
- **A DOOM Eternal update turns VR off** until EternalVR supports the new build; the game then runs flat.
- **SteamVR has had one test**, with a Quest 3 through Virtual Desktop's SteamVR mode. Under SteamVR the
  left Menu button opens SteamVR's dashboard, so hold Y to pause instead; the screenshot button (left Menu
  + trigger) does not reach the game there.
- **Only tested with a Quest 3 through Virtual Desktop (VDXR).** SteamVR, Meta Horizon Link, other
  runtimes and other headsets are untested. The launcher lets you pick a runtime; please tell me what
  happens.

## Image and performance

- **Render resolution.** Each eye renders at the headset runtime's recommended size, scaled down to a
  budget of about 4.6 million pixels per eye (about 2056x2216), times "Resolution" on the launcher's Play tab (0.50 to 2.00).
  Raise it for sharpness if your GPU has headroom, lower it if the frame rate drops below the headset's
  refresh rate. The mod's log says the size it used (`size: render size ...`). If the size cannot be
  set, the game renders at its small window's size and looks soft; the log says why.
- **Put the headset on before launching.** The launcher asks the headset's runtime for the render size
  and starts the game at it. If the headset is not detected ("Headset not detected; the render size is
  decided in-game" in the launcher's log), the game switches size a few seconds in; on graphics cards
  with 12 GB or less that switch can fail with "Failed to allocate video memory". Launch again with the
  headset connected.
- **Some effects are off in stereo.** Motion blur, depth of field, chromatic aberration and vignette are
  off in VR, and so are the red tint and blur when you take damage (the arrows showing where the hit
  came from and the low health warning stay). The game's other screen overlays are off too: the red
  edges at low health, double vision and screen shakes. A few temporal effects (screen-space ambient occlusion's temporal filter, water
  reflections and refraction) are turned off because the two eyes would share their history. The picture looks slightly different from the flat game.
- **DLSS is experimental.** The launcher's "Anti-aliasing" setting offers DLSS on NVIDIA RTX cards (Off is
  the default). Each eye gets its own DLSS history. DLSS only helps when the graphics card
  is what limits the frame rate: in stereo the processor is often the limit, even on a fast one, and then
  DLSS runs slower than TAA. The game's own Video menu shows DLSS as off even while the mod runs it. If
  DLSS cannot run per eye, the mod switches that session to TAA (the log says
  `DLSS has no per-eye feature`).
- **Anti-aliasing is off by default.** With TAA, moving demons still look slightly smeared in the right eye
  only (some people feel it more than they see it), so the launcher's "Anti-aliasing" setting defaults to
  Off: sharp in both eyes, with some shimmer on edges and shiny surfaces. TAA is still there if you prefer
  smoother edges.
- **See-through surfaces can blur when you move.** Stained-glass windows and similar translucent surfaces
  may smear briefly while you turn.
- **Shadows can pop in** on some walls as you turn your head.
- **Fog and light shafts** can differ a little between the eyes in a few places (a hallway in the second
  mission, for example).
- **HDR output is off** during VR sessions.
- **Frame rate.** Stereo renders every frame twice, and there is no foveated rendering or DLSS tuning for
  VR yet. Slower cards than the tested RTX 4080 may struggle.

## Controllers and aiming

- **Weapon placement** in your hand is one estimate for all weapons; some guns may sit a little off.
- **One arms model.** The game's viewmodel holds both arms, so the left arm follows your gun hand.
- **Shots can pass through thin walls** if you push the gun through one; the shot starts at your hand
  and there is no wall check yet.
- **No haptics** (controller vibration) yet.
- **Remapping the controls isn't in the launcher yet.** You can change them with a text file; see
  "Changing the controls" in `CONTROLS.md`.
- **The weapon wheel sits right of centre** on the HUD panel, where the game lays it out. Selecting works:
  hold the right stick down, push it toward a weapon, and let go.
- **The meathook and glory kills with hand aim** have had little testing.
- **Index controllers** have bindings but have never been tried.

## Menus

- The menu panel is flat and stays where it appeared; it does not follow you if you turn away.
- In the short pause menu of the very first mission (Load Checkpoint, Exit), neither B nor the Menu
  button closes it; the game does the same with Escape when played flat.

## Launcher and settings

- **The launcher does not check that the headset is connected.** It checks that an OpenXR runtime is
  set, not that it is running. Start the runtime and connect the headset before Launch VR.
- **Steam Cloud warning.** If the launcher warns that "Steam's cloud record is stale", start DOOM Eternal
  once normally through Steam, let it reach the main menu, quit, then use the launcher again.
- **A small game window sits on your desktop** during a VR session. That is normal. While the game is
  not the focused window, your mouse stays yours.
- **Not code-signed.** Windows SmartScreen may warn the first time (`INSTALL.md`).

## The DLC (The Ancient Gods, parts one and two)

- The DLC missions, the ARC Carrier and the Master Levels are single-player and allowed, but they have
  only had a short check. Things to watch: swimming (you swim where the game's view points, so under hand
  aim a lowered gun can make you dive), the Sentinel Hammer (on the Crucible button, the left stick
  click), the DLC's in-mission videos, and underwater scenes in both eyes. Holding both sticks pressed
  for 2 seconds recenters, so a very long Hammer and Blood Punch together can cancel both.
- A red overlay with light rays sometimes shows as a rectangle in the middle of the view instead of
  covering the whole screen.

## Multiplayer and online features

- By design the mod switches itself off in BATTLEMODE and any online mode, and the launcher refuses
  multiplayer arguments. Accepting a Steam invite while playing in VR also switches VR off for the rest
  of that session (restart from the launcher). The real invite path has not been tested with a second
  account.
- EternalVR does not modify any game file; it only changes the running game while it is started from the
  EternalVR launcher. We know of no bans for single-player mods in DOOM Eternal, and id's own mod support
  only turns multiplayer and achievements off, but id has never said anything about Events, weekly
  challenges or Slayer Points. To be safe, don't use EternalVR to earn Event or challenge progress, and
  never try to play online with it.
