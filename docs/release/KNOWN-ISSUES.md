# Known issues

What I already know is missing or wrong in this alpha. No need to report these, but do tell me if one of
them is worse for you than described here.

## The big ones

- **The HUD is on a floating panel.** Health, armour, ammo and the rest of the HUD are shown on a flat
  panel in front of you that follows your head. "Health and ammo" on the launcher's Advanced tab can put
  health, armour and ammo on the inside of your off hand's wrist instead, or the ammo just above the back
  of the gun in your weapon hand. Both are experimental and have not been tried in a headset yet. The ammo
  sits in one place for every weapon, so on some guns it may float a little off the gun.
- **Glory kills keep the game's camera by default.** The game moves the camera during a glory kill; in VR
  the view follows it, facing the demon, and turns with your head from there. If camera motion bothers you,
  the Play tab's "Glory kills" setting keeps the view steady, fades it out, or shows the kill on a flat
  screen instead; these are new and have not been tried in a headset yet. Cutscenes play on a flat 16:9 screen in front of you
  with the game's own camera (`ETERNALVR_CUTSCENES=immersive` puts you in the cutscene's camera instead).
- **Room-scale walking is new and lightly tested.** When you step around the room while standing, the
  Slayer walks after you in short pulses, so you may hear footsteps start and stop. It has been tested
  in a headset, but not much against walls, on stairs or in heavy combat. It
  pauses in menus, cutscenes, the air and while you use the move stick (since 0.1.8 the view then rides
  along with the Slayer instead of fading), and it is off when seated. If it bothers you, untick
  "Room-scale" on the Play tab. Leaning more than about 60 cm while seated still fades the view to black:
  sit back, or hold both sticks pressed for 2 seconds to recenter. After a second and a half of black you
  are moved back onto your body, and "Fade in walls" on the Play tab turns the fade off. Standing up or sitting
  down is noticed after a second or so (the view blinks while your height is reset); if it is not (a very
  tall chair, no floor height from your headset), hold both sticks pressed for 2 seconds.
- **Only tested on NVIDIA:** an RTX 4080 and an RTX 3080 Ti (12 GB). AMD and Intel graphics cards have not
  been tried.
- **Bindings for HP Reverb G2, Windows Mixed Reality, HTC Vive Cosmos, HTC Vive wands, Pico 4 and Steam
  Frame controllers are new and untested on real hardware.** Please report how they work. The Vive wands
  have a reduced layout (see CONTROLS.md), and a resting thumb on the trackpad moves or turns you. The
  Steam Frame has a layout of its own (see CONTROLS.md). Pimax and other
  controllers get whatever layout their runtime maps them to.
- **If the headset disconnects** (a Wi-Fi drop, the headset's runtime restarting), the game carries on
  flat on your desktop and VR comes back by itself once the headset and its runtime are back, usually
  within a few seconds. This has been tested on a headset simulator, not yet with a real headset
  dropping out; please tell me if VR does not come back. If you close VR for the game from the
  headset's own menu, VR stays off until you start the game again from the launcher.
- **A DOOM Eternal update turns VR off** until EternalVR supports the new build; the game then runs flat.
- **SteamVR has had one test**, with a Quest 3 through Virtual Desktop's SteamVR mode. Under SteamVR the
  left Menu button opens SteamVR's dashboard, so hold Y to pause instead, and take a screenshot with Y
  held and a trigger pulled (instead of left Menu + trigger).
- **The Game Pass and Microsoft Store version has had one test**, on one PC with a Quest 3 through Virtual
  Desktop: a level and the Revenant in Cultist Base played as on Steam. On its first start that version
  asks you to log in to Bethesda.net, and clicking a text box opens Windows' own typing window on the
  desktop, which the headset does not show: what you type goes there and is entered when you press Enter.
  It is easier to log in once without VR, starting the game from the Xbox app, before the first VR session.
- **Places that open when you look at them** (a ladder, the tram exit and a door in Doom Hunter Base, and a
  few others) check where your head looks. That was fixed in v0.1.4; before it they waited for the gun.
  Please tell me anywhere you get stuck.
- **Only tested with a Quest 3 through Virtual Desktop (VDXR).** SteamVR, Meta Horizon Link, other
  runtimes and other headsets are untested. The launcher lets you pick a runtime; please tell me what
  happens.

## Image and performance

- **Render resolution.** Each eye renders at the headset runtime's recommended size, scaled down to a
  budget of about 4.6 million pixels per eye (about 2056x2216), times "Resolution" on the launcher's Play tab (0.50 to 2.00).
  Raise it for sharpness if your GPU has headroom, lower it if the frame rate drops below the headset's
  refresh rate. The mod's log says the size it used (`size: render size ...`). If the size cannot be
  set, the game renders at its small window's size and looks soft; the log says why.
- **AMD Radeon RX 5000 and 6000 (and some newer AMD drivers): the picture renders at the window's
  size.** These drivers cannot scale the game's image into a smaller desktop window, so each eye renders
  at the size of the game's window on your desktop instead of the planned size, and looks soft. The mod
  then makes that window as large as your display allows (in the eye's shape), so a larger or
  higher-resolution display gives a sharper picture. After such a session the launcher says how large each
  eye really was ("Your graphics driver cannot render above the window size, so each eye rendered at ...").
  A virtual display works around it today (see "The picture is soft on an AMD graphics card" in
  [TROUBLESHOOTING.md](TROUBLESHOOTING.md)); a full fix is being worked on.
- **Put the headset on before launching.** The launcher asks the headset's runtime for the render size
  and starts the game at it. If the headset is not detected ("Headset not detected; the render size is
  decided in-game" in the launcher's log), the game switches size a few seconds in; on graphics cards
  with 12 GB or less that switch can fail with "Failed to allocate video memory". Launch again with the
  headset connected.
- **Some effects are off in stereo.** Motion blur, depth of field, chromatic aberration and vignette are
  off in VR, and so are the red tint and blur when you take damage (the arrows showing where the hit
  came from and the low health warning stay). The game's other screen overlays are off too: the red
  edges at low health, double vision and screen shakes. A few temporal effects (screen-space ambient
  occlusion's temporal filter, water reflections and refraction) are turned off because the two eyes
  would share their history. The picture looks slightly different from the flat game.
- **TAA on some moving shapes.** TAA (the game's own anti-aliasing, the default) keeps a separate history
  for each eye, and moving demons look the same in both eyes. A few animated shapes, such as the damned
  souls reaching out of the walls, can still look slightly different between the eyes. The launcher's
  "Anti-aliasing" setting can switch to Off (sharp, with some shimmer on edges) or DLSS.
- **Pickup camera animations do not play.** At a pickup that moves the camera (the chainsaw, for
  example) the view holds still until the tutorial popup instead of playing the animation. The game
  carries on normally afterwards.
- **DLSS is experimental.** It is offered on NVIDIA RTX cards and helps only when the graphics card, not
  the processor, is what holds the frame rate back; when the processor is, DLSS can be slower than TAA.
  Each eye gets its own DLSS history. Choose DLSS under "Anti-aliasing" on the launcher's Play tab, then its
  quality (Quality, Balanced, Performance or Ultra Performance) in the DLSS group. In VR the game's own video menu shows the DLSS
  setting that is running (Ultra Performance shows as Performance, which the menu does not have). With the
  launcher's DLSS or Off, changing DLSS in the game's menu has no effect in VR, and your saved game settings
  keep their own DLSS choice for flat play. With the launcher's TAA the game's own DLSS setting is used: if
  it is on, DLSS runs in VR too, and changing it in the menu works as usual. If DLSS cannot run per eye,
  the mod switches to TAA (the log says `DLSS has no per-eye feature`) and the menu shows DLSS as off. The
  mod tries DLSS again by itself a few times (after 5, 10 and 20 seconds); choosing a DLSS quality in the
  game's menu tries it again at once.
- **A newer DLSS is experimental too.** The game ships DLSS 2.3. The DLSS group's "Version" offers NVIDIA's
  newest by default: **Download** fetches it once, straight from NVIDIA's GitHub after you accept NVIDIA's
  license (the mod does not include it), and until then the game's 2.3 runs. "A file of mine" uses a
  `nvngx_dlss.dll` you downloaded yourself (NVIDIA's DLSS page on GitHub, `lib/Windows_x86_64/rel/nvngx_dlss.dll`).
  The file stays in EternalVR's data folder or where you keep it, and nothing is copied into the game folder.
  "In the headset" says what will run. If the game cannot use the file, it keeps its own DLSS and the log says
  why (lines starting with `dlss:`).
- **See-through surfaces can blur when you move.** Stained-glass windows and similar translucent surfaces
  may smear briefly while you turn.
- **Shadows can pop in** on some walls as you turn your head.
- **Fog and light shafts** can differ a little between the eyes in a few places (a hallway in the second
  mission, for example).
- **HDR output is off** during VR sessions.
- **Frame rate.** Stereo renders every frame twice. Slower cards than the tested RTX 4080 may struggle; a
  player's RTX 3070 plays well with DLSS Performance and preset J (see "Which DLSS preset should I use?" in
  `TROUBLESHOOTING.md`). Foveated rendering on the Play tab can help on NVIDIA RTX cards, but it is
  experimental and off by default.
- **A short freeze when the game saves a checkpoint.** Right after a fight or just before a cutscene the
  picture can stop for about half a second. That is the game's own checkpoint autosave, not the mod: the
  game holds everything while it writes the save. It happens in the flat game too, but in the headset a
  frozen picture stands out much more. The mod's log names it: the `stall:` line for that freeze ends with
  `the game saved a checkpoint in the gap`.
- **Textures can sharpen a moment late.** "Texture streaming: Only what you see" on the launcher's Play tab
  (on by default) has the game load only the texture detail the current view needs, which gave about 8% more
  frames per second on the test rig. A still view looks the same, but after a fast turn or in a new area a
  texture may look soft for a moment. Turn it off if that bothers you.
- **The CPU Saver is experimental.** Its checkboxes on the Play tab (all off by default) each turn down
  one of the game's detail settings that cost processor time: your own shadow, sun shadows near you, how far
  away models switch to simpler versions, and how far away marks, shadows and lights fade. Each one's tooltip
  says what it gained on the test rig and what it costs in the picture. The game's own settings are put back
  after you play.
- **Alternate eyes is new and not yet tried in a headset.** "Alternate eyes" on the launcher's Advanced tab
  (off by default) draws one eye per frame, taking turns, so a slower processor has about half the work per
  frame. Each eye then updates at half the rate: fast motion can look doubled or smeared, moving demons can
  seem slightly nearer or farther than they are, and some people find it uncomfortable. With "Anti-aliasing"
  on TAA, fast-moving demons can smear a little more than without it; Off avoids that. "Auto" does this only
  while your processor cannot keep up with the headset and draws both eyes again once it can (the switch
  takes a second or a few). Turn it off if it does not help.

## Controllers and aiming

- **Weapon placement** in your hand is one estimate for all weapons; some guns may sit a little off.
- **One arms model.** The game's viewmodel holds both arms, so the left arm follows your gun hand.
  `ETERNALVR_OFFHAND=free` lets the off arm reach for your off-hand controller instead. That is
  experimental: it has not been tried in a headset, in glory kills or across a level change, and the
  hand's angle and the shoulder are rough.
- **Shots can pass through thin walls** if you push the gun through one; the shot starts at your hand
  and there is no wall check yet.
- **Vibration** (Vibration on the Play tab) is new and has only been tried on Quest 3 controllers; its
  strength and feel may change.
- **bHaptics** (bHaptics (experimental) on the Play tab) is experimental. On a real suit a player has felt
  the shots, the landing from a fall and glory kills; the other effects (portals, the Sentinel Crystal, the
  pickup waves, jump pads and the rest) have only been checked against a stand-in for the bHaptics Player.
  Which side of the vest a hit plays on, and the heartbeat's place, may be mirrored until a tester confirms
  them.
- **The weapon wheel** (hold the right stick down, then turn it) works in the headset; it may sit a little
  right of centre.
- **Picking on the weapon wheel with your hand** (Weapon wheel: Point with your hand, on the Play tab) is
  new and has not been tried in a headset yet. How far you need to turn your hand may change.
- **The weapon wheel on a button** (given to one in the controls editor, the turn stick pointing) is new
  and has not been tried in a headset yet.
- **Throwing grenades and the overhead swing** (Gestures on the Play tab, both off by default) are new and
  have not been tried in a headset yet. How hard you need to throw or swing may change. The grenade flies
  where your gun points (or where you look with head aim), not where your hand threw it. A throw or a swing
  you did not mean still spends a grenade or a Crucible charge, so turn them off if they fire by mistake.
- **The meathook and glory kills with hand aim** have had little testing.
- **Jumping off climbable walls where you look** is new; it works on a Quest 3, other headsets are untried.
  While you hang on the wall the view is yours: the game no longer limits how far you can look to the side
  or down, and your gun follows your head there, not your hand. If it misbehaves, `ETERNALVR_CLIMB_LOOK=0` brings back the
  game's own wall climbing (turn with the stick before you jump).
- **Piloting the Revenant** in Cultist Base: it turns with the stick, moves where you face and aims where
  your weapon hand points (where you look, with head aim). The Slayer's left arm can stay in view while you
  pilot it, and at some angles you can see inside its shoulders.
- **Index controllers** have bindings but have never been tried.

## Menus

- The menu panel is flat. It stays where it appeared until you look away from it for a second, then it
  comes back in front of you.
- In the short pause menu of the very first mission (Load Checkpoint, Exit), neither B nor the Menu
  button closes it; the game does the same with Escape when played flat.

## Launcher and settings

- **Start the headset's runtime and connect the headset before Launch VR.** When the runtime reports no
  headset, or does not answer, the launcher asks before it starts the game.
- **Steam Cloud warning.** If the launcher warns that "Steam's cloud record is stale", start DOOM Eternal
  once normally through Steam, let it reach the main menu, quit, then use the launcher again.
- **A small game window sits on your desktop** during a VR session. That is normal. While the game is
  not the focused window, your mouse stays yours.
- **No rename for VR settings profiles.** The launcher can make and delete a profile but not rename it. To
  rename one by hand, close the launcher and rename both its file in `profiles` and its controls folder in
  `controls\profiles` (in `%LOCALAPPDATA%\EternalVR`); a profile whose controls folder is missing uses the
  controls of (none) until it gets its own copy (CONTROLS.md, "Controls for each profile").
- **The comfort vignette** (Vignette on the Play tab) has been checked on a headset simulator, not yet
  in a headset; its strength may change.
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
  EternalVR launcher. I know of no bans for single-player mods in DOOM Eternal, and id's own mod support
  only turns multiplayer and achievements off, but id has never said anything about Events, weekly
  challenges or Slayer Points. To be safe, don't use EternalVR to earn Event or challenge progress, and
  never try to play online with it.
