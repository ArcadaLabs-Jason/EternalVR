# Known issues

What I already know is missing or wrong in this alpha. No need to report these, but do tell me if one of
them is worse for you than described here.

## The big ones

- **The HUD is on a floating panel.** Health, armour, ammo and the rest of the HUD are shown on a flat
  panel in front of you that follows your head. "Health and ammo" on the launcher's Advanced tab can put
  health, armour and ammo on the inside of your off hand's wrist instead, or the ammo just above the back
  of the gun in your weapon hand. Both are experimental. In my headset the wrist worked, but it could come up
  upside down (I had to turn my wrist to read it) and the palm side is an odd place to look; a better wrist HUD
  is being worked on. The ammo sits in one place for every weapon, so on some guns it may float a little off
  the gun.
- **Glory kills keep the game's camera by default.** The game moves the camera during a glory kill; in VR
  the view follows it, facing the demon, and turns with your head from there. If camera motion bothers you,
  the Play tab's "Glory kills" setting keeps the view steady, shows the kill on a flat screen, or fades it
  out instead. Steady view and Flat screen have been tried in a headset; Fade out showed nothing with "Fade in
  walls" off before this build and has not been tried in a headset since. Cutscenes play on a flat 16:9 screen in front of you
  with the game's own camera (Cutscene view on the Advanced tab, "Around you", puts you in the cutscene's
  camera instead).
  With cutscenes around you (Advanced tab, Cutscene view) and motion controllers on, cutscenes keep the game's
  own weapon view ("Arms in cutscenes" on keeps the VR one, as before). Whether that keeps the Slayer's arms out
  of sight has not been checked in a headset yet. A camera cut that turns the view more than 90 degrees puts
  the new shot in front of where you look. Close-ups can still feel very near: the game frames them for a
  narrow flat screen.
- **Room-scale walking is new and lightly tested.** When you step around the room while standing, the
  Slayer walks after you in short pulses, so you may hear footsteps start and stop. It has been tested
  in a headset, but not much against walls, on stairs or in heavy combat. It
  pauses in menus, cutscenes, the air and while you use the move stick (since 0.1.8 the view then rides
  along with the Slayer instead of fading), and it is off when seated. If it bothers you, untick
  "Room-scale" on the Play tab. Leaning more than about 60 cm while seated still fades the view to black:
  sit back, or hold both sticks pressed for 2 seconds to recenter. After a second and a half of black you
  are moved back onto your body, and "Fade in walls" on the Play tab turns the fade off. Standing up or sitting
  down is noticed after a second or so (the view blinks while your height is reset); if it is not (a very
  tall chair, no floor height from your headset), hold both sticks pressed for 2 seconds. If walking stops
  following you while you stand (a headset that reports the floor in the wrong place), set Play position
  to Standing in the launcher. With motion controllers off (a gamepad, or keyboard and mouse) it works too,
  and it now also waits out glory kills and pickup animations there. If you play lying down, set Play
  position to Sitting: the mod ignores a head that low. On a chair with wheels, set Play position to Sitting
  too: rolling more than a metre counts as walking and switches to standing.
- **Only tested on NVIDIA here:** an RTX 4080 and an RTX 3080 Ti (12 GB). AMD and Intel graphics cards have
  not been tried here; what players with AMD cards have reported is under "Image and performance".
- **Bindings for HP Reverb G2, Windows Mixed Reality, HTC Vive Cosmos, HTC Vive wands and Pico 4
  controllers are untested on real hardware.** Please report how they work. The Vive wands have a reduced
  layout (see CONTROLS.md), and a resting thumb on the trackpad moves or turns you. Pimax and other
  controllers get whatever layout their runtime maps them to.
- **Valve Index controllers: the hands can sit at a wrong angle.** A player plays with them, but by default
  the hands looked tilted and placed too high to him (GitHub #16); hand placement was tuned on Quest
  controllers. I'm looking into it. Steam Frame controllers have their own layout (see CONTROLS.md) and work
  for a player through SteamVR.
- **If the headset disconnects** (a Wi-Fi drop, the headset's runtime restarting), the game carries on
  flat on your desktop and VR comes back by itself once the headset and its runtime are back, usually
  within a few seconds. This has been tested on a headset simulator, not yet with a real headset
  dropping out; please tell me if VR does not come back. If you close VR for the game from the
  headset's own menu, VR stays off until you start the game again from the launcher.
  If the graphics card is reset while you play, VR stays off until you start the game again; the
  launcher then says "the graphics card was reset".
- **A DOOM Eternal update turns VR off** until EternalVR supports the new build; the game then runs flat.
- **SteamVR**: players use it with Index, Steam Frame and PSVR2 controllers; I have tested it once myself, with a
  Quest 3 through Virtual Desktop's SteamVR mode. Under SteamVR the left Menu button of Touch controllers opens
  SteamVR's dashboard, so hold Y to pause instead (B with **Left (buttons and sticks)**), and take a screenshot by
  holding both sticks pressed and pulling a trigger (instead of left Menu + trigger).
- **SteamVR's dashboard can reset the graphics driver when video memory is nearly full.** A player's game
  used 97 to 99% of the video memory Windows gave it (a 12 GB card next to two large monitors); opening
  the dashboard over the game then reset the NVIDIA driver and SteamVR stopped showing the game. A lower
  Texture Pool Size in the game's video settings fixed it. After a session that close to the limit the
  launcher says so ("The game used nearly all the video memory Windows gives it ...").
- **The Game Pass and Microsoft Store version has had one test**, on one PC with a Quest 3 through Virtual
  Desktop: a level and the Revenant in Cultist Base played as on Steam. On its first start that version
  asks you to log in to Bethesda.net, and clicking a text box opens Windows' own typing window on the
  desktop, which the headset does not show: what you type goes there and is entered when you press Enter.
  It is easier to log in once without VR, starting the game from the Xbox app, before the first VR session.
- **Places that open when you look at them** (a ladder, the tram exit and a door in Doom Hunter Base, and a
  few others) check where your head looks. That was fixed in v0.1.4; before it they waited for the gun.
  Please tell me anywhere you get stuck.
- **Mostly tested with a Quest 3 through Virtual Desktop (VDXR)**, plus the one SteamVR test above. Meta
  Horizon Link, other runtimes and other headsets have not been tested here (players' reports on SteamVR are
  above). The launcher lets you pick a runtime; please tell me what happens.

## Image and performance

- **Long sessions on Virtual Desktop can slow down.** A player on a Quest 3 with Virtual Desktop saw 90 fps
  fall to the low 70s over about an hour of play, and a trip out of the game and back (hold the headset's home
  button, then return) brought 90 back. His logs show Virtual Desktop holding each frame longer and longer, not
  the game working harder. Other players put it down to the stream's decoding on the headset; Virtual Desktop's
  beta restarts its stream to avoid it. If it happens to you: try the Virtual Desktop beta, or step out of the
  game and back in. The log names it (`xr: the runtime has held each frame in xrEndFrame ...`), so an export
  shows it.
- **Render resolution.** "Resolution" on the launcher's Play tab is a size times a number (0.50 to 2.00).
  Auto, the default, is the size your headset's runtime asks for, fitted within about 4.6 million pixels
  per eye: a Quest 3 at Virtual Desktop's High asks for 2496x2688 and renders 2056x2216 at 1.00, so 1.00 is
  not the headset's own size, and Virtual Desktop's High, Ultra and Godlike all render about the same.
  Choose "Virtual Desktop native" (or "SteamVR native", "Quest Link native"...) to make 1.00 exactly the size
  the runtime asks for, as in other OpenXR games; then Virtual Desktop's quality or SteamVR's resolution counts
  in full. "Quest 3 native" (named after your headset) makes 1.00 the headset's own panel, when the launcher
  knows the headset's model. Updating keeps your number
  on Auto, so nothing changes until you choose. The "Each eye" line shows the size and its percent of what
  the runtime asks for and of the panel. Raise the number for sharpness if your GPU has headroom, lower it if
  the frame rate drops below the headset's refresh rate. The mod's log says the size it used
  (`size: render size ...`). If the size cannot be set, the game renders at its small window's size and
  looks soft; the log says why.
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
  decided in-game" in the launcher's log), the mod uses Resolution's Auto whatever you chose, and the game
  switches size a few seconds in; on graphics cards
  with 12 GB or less that switch can fail with "Failed to allocate video memory". Launch again with the
  headset connected.
- **Smoke, sparks, blood and other sprite effects are one frame behind in the left eye.** The left eye
  draws them as they were a frame earlier than the right eye; the scenery is not affected.
  0.1.34 fixed this, but the fix drew snow and some ice effects in the right eye as tiles in one mission, so
  0.1.35 turned it off. 0.1.36 fixes the cause of the tiles but keeps the fix off until it has had more
  testing.
- **Some effects are off in stereo.** Motion blur (also the zoom blur when you dash), depth of field,
  chromatic aberration and vignette are off in VR, and so are the red tint and blur when you take damage
  (the arrows showing where the hit came from and the low health warning stay). The game's other screen
  overlays are off too: the red edges at low health, double vision and screen shakes, and the wobble of
  the whole picture under water (the water's tint and fog stay). The HUD draws its own copy of the red
  damage and low-health tint; the mod filters that off the floating HUD panel (see "Squares on the HUD
  when you get hit" below). A few effects lose the smoothing they get
  over several frames, because the two eyes would share it: water reflections still show, only that
  smoothing is off. The picture looks slightly different from the flat game.
- **Screen shake was left off in the flat game.** Before this version, playing in stereo turned DOOM's own
  Screen Shake setting (Settings, Game) off in your profile, and it stayed off when you played without the
  mod. VR sessions now leave the setting as you had it, and the first stereo session with this version turns
  it back on once if a VR session had turned it off and one of the last 20 sessions was in stereo. This is
  done once per PC: a second Steam account, or Steam and Game Pass on one PC, is not repaired after the first.
  If you had turned screen shake off in DOOM's settings yourself, turn it off again. Changing that setting in
  the game's menu during a VR session is not kept.
- **TAA on some moving shapes.** TAA (the game's own anti-aliasing, the default) keeps a separate history
  for each eye, and moving demons look the same in both eyes. Props that play a recorded animation (swaying
  banners, hanging corpses, the damned souls reaching out of the walls) were smeared in the right eye before
  0.1.31; they now get their motion in both eyes, which has not been checked in a headset yet. If one eye
  still looks different on a moving shape, please report where. The launcher's "Anti-aliasing" setting can
  switch to Off (sharp, with some shimmer on edges) or DLSS.
- **Pickup camera animations do not play.** At a pickup that moves the camera (the chainsaw, for
  example) the view holds still until the tutorial popup instead of playing the animation. The game
  carries on normally afterwards.
- **DLSS is experimental.** It is offered on NVIDIA RTX cards and helps only when the graphics card, not
  the processor, is what holds the frame rate back; when the processor is, DLSS can be slower than TAA.
  Each eye gets its own DLSS history. Choose DLSS under "Anti-aliasing" on the launcher's Play tab, then its
  quality (DLAA, Quality, Balanced, Performance or Ultra Performance) in the DLSS group; DLAA needs a newer DLSS than the
  game's. In VR the game's own video menu shows the DLSS setting that is running (Ultra Performance shows as Performance and
  DLAA as Quality, which the menu does not have). With the
  launcher's DLSS or Off, changing DLSS in the game's menu has no effect in VR, and your saved game settings
  keep their own DLSS choice for flat play. With the launcher's TAA the game's own DLSS setting is used: if
  it is on, DLSS runs in VR too, and changing it in the menu works as usual. If DLSS cannot run per eye,
  the mod switches to TAA (the log says `DLSS has no per-eye feature`) and the menu shows DLSS as off. The
  mod tries DLSS again by itself a few times (after 5, 10 and 20 seconds); choosing a DLSS quality in the
  game's menu tries it again at once.
- **A newer DLSS is experimental too.** The game ships DLSS 2.3. The DLSS group's "Version" offers NVIDIA's
  newest by default: **Download** fetches it once, straight from NVIDIA's GitHub after you accept NVIDIA's
  license (the mod does not include it), and until then the game's 2.3 runs. "Load file..." uses a
  `nvngx_dlss.dll` you downloaded yourself (NVIDIA's DLSS page on GitHub, `lib/Windows_x86_64/rel/nvngx_dlss.dll`).
  The file stays in EternalVR's data folder or where you keep it, and nothing is copied into the game folder.
  "In the headset" says what will run. If the game cannot use the file, it keeps its own DLSS and the log says
  why (lines starting with `dlss:`).
- **See-through surfaces can blur when you move.** Stained-glass windows and similar translucent surfaces
  may smear briefly while you turn. Each eye now keeps its own history for the glass, and standing still
  both eyes match the flat game on a test rig; whether the brief blur while turning is gone has not been
  checked in a headset yet.
- **Demons flying far off can leave a trail.** In a headset test in Exultia, distant flying demons smeared
  the way anti-aliasing (TAA) can smear fast motion. Anti-aliasing Off on the Play tab avoids TAA's
  smearing, at the cost of some shimmer on edges.
- **The HUD's markers are drawn at the HUD's distance.** Objective and other map markers now point at the place
  they mark; before, they could sit well off it. Since 0.1.84 they also stay on their spot while you turn your
  head (tried in my headset). They are still drawn on the HUD panel, so while you look at a far-off spot its
  marker may look doubled (and the other way round).
  A marker above or below the HUD panel waits at its top or bottom edge.
- **Below 90 fps the HUD panel can jitter a little as you look around.** Since 0.1.84 the panel is shown where
  your head was when its frame was drawn, which is what keeps the markers on their targets; when the game runs
  under the headset's rate, a frame is shown twice and the panel shifts a bit until the next one. Seen in my
  headset at 60 to 75 fps. I'm looking into giving the markers their own layer so the panel can stay put.
- **Shadows can pop in** on some walls as you turn your head.
- **A lit spot can go dark from some places.** The game's own light culling can drop a light from
  certain spots, so an area can turn darker when you move a single step (the stairs up to the dais in the
  Fortress of Doom are one place). A fix is on by default on the Steam and Game Pass (1.0.56.0) versions of
  the game; on any other version the game keeps its own light culling and the log says why (lines starting
  with `light-cull:`). If a spot looks wrongly lit since this fix, please tell me where.
- **Wet floors and puddles were darker, and different in each eye** (the start of the second mission of The
  Ancient Gods part two, for one). The game left out wet patches (decals) just past each eye's inner edge. The
  eyes now also get the decals nearest to them in the view around both eyes, placed after the game's own list so
  the game's own decals keep drawing, on the Steam and Game Pass (1.0.56.0) versions of the game. On any other
  version they keep the game's own decal culling and the log says why (lines starting with `seq-decals:`). On my
  test PC's captures the right eye's puddles match the flat game's brightness and the left eye's are about 8%
  darker. I haven't tried it in a headset yet, nor on Game Pass. Through 0.1.79 the eyes got so many decals that
  at some view directions the floor's own decals stopped drawing (I saw the grate in an Urdak tunnel turn into a
  smooth panel). That's fixed too.
- **Reflections in puddles and glossy floors with ray tracing off** (the screen-space reflections) pointed
  the wrong way in each eye: some puddles did not reflect at all except from certain spots, and then the two
  eyes showed different things (the round platform at the start of Urdak, for one). Each eye now marches its
  reflections along its own view, and both eyes fade them out toward the sides at the same angles. On my test PC
  both eyes' reflections are now much more alike across the view; at the far left and right edges (past about 40
  degrees, where only one eye sees) these reflections fade out sooner than in the flat game. With ray tracing on,
  some spots can still differ between the eyes. Not tried in a headset yet, nor on Game Pass.
- **An effect at the very edge of one eye's view** (a glow, a spark) can be missing in that eye while the
  other eye shows it.
- **Fog and light shafts** can differ a little between the eyes in a few places (a hallway in the second
  mission, for example).
- **HDR output is off** during VR sessions.
- **Squares on the HUD when you get hit.** In 0.1.46 and 0.1.47 hard hits (most of all in The Ancient
  Gods) and low health could leave blocky edges and orange boxes around the HUD's health and ammo panels.
  They came from the mod's filter that takes the game's red damage tint off the floating HUD panel. 0.1.48
  replaced that filter, and on my test PC's captures the boxes are gone, but it hasn't been tried in a
  headset yet. If the HUD still looks wrong (squares, a red tint that stays, or anything else that wasn't
  there before), please tell me where it happened.
- **Frame rate.** Stereo renders every frame twice. Slower cards than the tested RTX 4080 may struggle; a
  player's RTX 3070 plays well with DLSS Performance and preset J (see "Which DLSS preset should I use?" in
  `TROUBLESHOOTING.md`). Foveated rendering on the Play tab can help on NVIDIA RTX, AMD Radeon RX 6000 or
  newer and Intel Arc cards, but it is experimental and off by default.
- **Foveated rendering (experimental) is less sharp toward the edges, by design.** Since 0.1.48 the sharp
  area is measured as an angle from where your head points, so a spot in the scene gets the same detail in
  both eyes (before, some detail off to the side could be sharp in one eye and blurry in the other). On
  headsets whose eye images aren't symmetric (Quest 3 for one) the blurrier band toward your nose and at
  the top is narrower than in 0.1.47 and more of it sits on the outer side. The bottom of the view is about
  as far from where your head points as the outer edges (on Quest 3, 55 degrees down against 44 up). A
  headset test showed floors there blocky and streaked with DLSS, because that band was shaded at the
  coarsest, quarter rate. I moved the quarter rate out to 56 degrees from where your head points for every
  setting: the bottom and the outer edges of a Quest 3's view now get half rate, and only the far corners of
  each eye keep quarter rate. Floors there can still look a little coarser than the middle, as half rate does,
  so tell me if they still bother you. Loading a checkpoint
  doesn't move it from one eye to the other: I measured each eye's pattern before and after map loads on my
  test PC and in captures from a headset session. If it looks different to you after a load, an in-headset
  capture before and one after it from the same spot (hold the left Menu button and pull a trigger) would
  help me a lot. It is off by default and off with Parallel Eye Rendering. Since 0.1.56 it also runs on AMD Radeon RX 6000 or newer and Intel Arc cards,
  through Vulkan's standard shading rate (a player's RX 6750 XT ran it; Intel Arc has not been tried, and
  my test PC has neither). If you use it, tell me how it looks to you.
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
- **Stereoscopic cutscenes cost about twice the graphics card's time while a cutscene plays.** "Stereoscopic
  cutscenes" on the launcher's Advanced tab (on by default) draws each cutscene on the flat screen from two cameras a
  little apart and shows the pair in 3D, like a 3D movie. On my RTX 3080 Ti it held 88.8 fps on average at 90 Hz
  over a minute of cutscenes, 83 at the worst moment; on a slower card it may not keep up, and then turning it off
  gives you the flat screen as before. It only works with the flat screen (Cutscene view), VR mode stereo,
  and neither Alternate eyes nor Parallel Eye Rendering. The two cameras are at most your eye distance apart (the
  launcher's setting, else 63 mm), closer together in shots with a narrow lens or with something near the camera
  in the level. In my headset they looked great, but I set the amounts from numbers, so some shots may still feel
  flat or too deep.
  The mod cannot see the Slayer or a prop right at the lens, so in some close-ups (the opening cutscene has a
  few) the picture pops out of the screen further than is comfortable, and a few subtitles and popup texts there
  can be hard to focus on. It doubles the GPU time of a cutscene
  frame (4.9 to 9.9 ms on my RTX 3080 Ti at 2048x2208 per eye, where the headset's 90 Hz did not hold in the
  heavy shots). Foveated rendering is left off for these frames (its sharp centre follows the headset's
  view, not the screen), so they cost the full amount.
- **Parallel Eye Rendering is experimental and for testing.** It renders both eyes as two views of one game
  frame instead of one eye after the other: the Play tab's Parallel Eye Rendering (highly experimental),
  off by default (`README-ALPHA.md`, "Testing Parallel Eye Rendering"). Steam version only
  (greyed out on Game Pass), tried on NVIDIA only. It works with Anti-aliasing TAA, DLSS or Off; Foveated
  rendering and Alternate eyes are greyed out while it is on. What is still wrong with it:
  - **It needs more video memory.** On a 12 GB card the game can run at about 95% of its video memory and
    turn into a slideshow, which is hard on comfort. Set the game's Texture Pool Size one step lower before
    you try it.
  - **After 31 resolution changes in one session** (the game's resolution or resolution scale, or ray
    tracing turned on or off), both eyes show the same image, without depth, until the next start.
  - **Stutter and eyes out of step.** Now and then the headset shows the same picture twice. Both eyes
    normally show the same game frame, but right after a load, or when no new pair of eye images comes for a
    few frames, the headset can briefly show eyes from different frames.
  - **Some effects differ between the eyes.** The screen-space reflections (ray tracing off) now point the
    right way in each eye here too, as in the normal mode; the wet patches on floors (puddles) are still drawn the
    old way, so they can be darker or missing in one eye. I haven't tried the reflections in a headset yet.
    Glass and the underwater view can look different in each eye. Close demons can show a wrong pose in the left eye for a
    single frame. Menus can flash. The muzzle flash may look wrong; it is not known yet whether that comes
    from Parallel Eye Rendering.
  - **Smaller ones:** after the game rebuilds its swapchain the headset can keep the last picture for a few
    frames; things can pop in or out for a frame; sprites face the left eye's position; faint blocky patches
    were seen low in the right eye.
  - **Not tried yet:** long sessions, cutscenes, and AMD or Intel graphics cards.
  - **Lens flares** are now drawn for each eye from its own view; that has not been checked in a headset yet.
    If the game freezes or the headset goes black with Parallel Eye Rendering on, turn Parallel Eye Rendering
    off and please tell me.
  - If it cannot finish starting, both eyes show the same image for that session (the log says
    `parallel eyes: FAILED`). After a session the launcher's status line usually says whether it ran, or why
    not; the logs in the exported report always say.

## Controllers and aiming

- **With head aim the gun now sits in front of your face.** With motion controllers on and Aim with set to
  Head, your shots already went where you look, but the gun stayed in your hand, so with your hands down it
  pointed at the floor and you had to lift the controller to see it. Now it's placed like in gamepad mode: in
  front of you, pointing where you look, and it follows your head when you lean or crouch. A few things come
  with that: both arms are the game's own (like the flat game), a punch, grenades and the Flame Belch go where
  you look, the ammo on the gun ("Health and ammo") stays on your hand, and with the weapon in your left hand
  the gun still shows on the right. With some guns you may see the open end of the Slayer's left sleeve at
  the bottom of your view, same as in gamepad mode: the game's arms are made for a flat screen's narrower
  view. Hand aim is unchanged. Test rig only so far, not yet in a headset.

- **A sweep of the right stick down can swap weapons.** The weapon wheel opens sooner (0.15 s) when the stick is
  pushed within 20 degrees of straight down, so a sweep that drifts out of that cone after 0.15 s counts as a
  hold and can swap weapons when you let go.
- **The Chaingun and the Ballista: the right glove now holds them.** Every gun is placed so the glove's grip
  sits where your hand holds the controller. The game's own pose holds these two by a handle on top that's
  turned way off from how you hold a controller, so the glove now takes the Heavy Cannon's grip instead (the
  same grip the Chaingun uses when you zoom). The Ballista has no real grip, so the glove closes on the ridge
  under its stock. The glove now stays on the gun while it fires and kicks too (before, it could trail the
  Ballista's kick by up to 15 cm for a frame); for that frame the arm moves with the gun, so the shoulder end
  can jump by up to about 17 cm for one frame with a Ballista shot. A stutter (one long frame) no longer hands
  your arms back to the game's pose for a moment either: on these two guns that slid the glove off the grip
  and back over about half a second. In my headset the hands felt right on every gun; if a glove or a gun
  sits off your hand, please tell me which one and roughly how far.
- **Floating at the water's surface** your hands now stay on your real hands. Up there the game keeps
  swimming with both arms and tucks the gun away out of view (it can't be fired either), even though it
  counts the gun as held. Before, the swimming hand sat about 30 cm off your hand, and with the weapon in
  the left hand the left glove ended up more than a metre behind you. Now the arms are placed like they are
  under water, so the swim stroke still moves your hands around by up to about 17 cm. When the game briefly
  raises or lowers the gun up there, you may see it for a split second a bit off your hand. Test rig only.
- **The Unmaykr and the Crucible now sit in your hand too.** The Unmaykr is held like the other guns. The
  Crucible is held in your right fist the way the game holds it, so the blade stands up out of your fist
  (it's turned a bit from the flat game's angle so the hand matches yours). A punch, a chainsaw cut, a
  Crucible swing and the Sentinel Hammer's slam still play the game's own arm animation. Under water the
  swimming hands now sit at your right hand too (before, about 25 cm ahead of it); the swim stroke still moves
  them around by up to about 10 cm. The Berserk fists and the Sentinel Hammer haven't been checked yet (I
  couldn't get either into the Slayer's hands on the test PC). Test rig only so far, not yet in a headset.
- **Sights are lined up on every gun.** In my headset they matched the shots on every gun except the Heavy Cannon
  with Micro Missiles, which is a bit off. Every gun points its sights where the shots go, and the Heavy Cannon
  follows the mod you have active: the Precision Bolt's scope with the Bolt, the iron sights otherwise. The sights
  run parallel to the shot line, about 9 to 20 cm above it, so up close they sit a bit above where the shots go.
  Guns with no sight parts (Plasma Rifle, Rocket Launcher, Ballista, BFG 9000, Unmaykr) line up along the barrel,
  and the Chaingun along its zoom axis. Shots always go along your controller's aim, and the Aim dot sits exactly
  on that line at any distance.
- **The Heavy Cannon's Precision Bolt scope does not magnify.** Zooming in keeps the gun in your hand and
  the HUD up, with no scope overlay, and the view stays at your headset's field of view. With hand aim the
  Aim dot shows where shots go.
- **Switching the Heavy Cannon's mod turns the gun by about 3 degrees.** The Precision Bolt's scope and the
  iron sights point about 3 degrees apart, so when you switch between the Bolt and the Micro Missiles the gun
  turns a little to put the sights you now have on your aim. Your hand stays where it is. I haven't tried it
  in a headset yet, so I don't know yet whether the turn bothers you.
- **With head aim there is no crosshair or aim mark while the Precision Bolt is zoomed in.** Shots go to the
  middle of your view. On the Game Pass version, head aim may still show the scope's ring (the zoom
  bit is unchecked there; I have not tested it).
- **The Chaingun's Energy Shield** is drawn where the game's flat view would put it, so the gun in your
  hand can cover it.
- **The arms.** The Slayer's other arm follows your off-hand controller, its fingers follow your grip, trigger
  and thumb, and both arms bend by the new arm solve; there is no option for the old arms. While the game plays
  an animation in which his left arm is the action (a melee, a glory kill, the chainsaw, a ledge grab, the Super
  Shotgun loading its shells) the arm is the game's for that moment, easing there and back. A weapon switch or a
  weapon mod switch leaves your off hand where it is. With head aim (Aim with: Head) or a gamepad the arms are
  the game's, as before.
  With the weapon in your left hand the Slayer's left glove holds the gun and his right arm is your off hand,
  following your right controller. The gun itself is not mirrored. Left-handed play is checked on the test PC
  only, not in a headset, with the guns, the Unmaykr and the Crucible held, fired, switched and a melee and
  the chainsaw: glory kills and the Sentinel Hammer have not been tried left-handed yet. While the game plays
  its own arm animations (a switch, a melee, a glory kill) the game's right
  glove holds the gun in your left hand about 2 cm from where it would sit for right-handers. The first time
  you bring out each weapon after starting the game the arms stay the game's for about half a second while
  the mod reads how the Slayer holds it, then the gun shifts by 1 to 2.5 cm into your hand; after that it
  remembers, also across levels. The Crucible now sits in your left hand from its first raise too (before, it
  swung in from about 70 cm the first time). (Before, the Unmaykr could also hop around by about
  20 cm while it first came up, and a melee could throw the gun about half a metre for a moment; both fixed.)
  Show arms on Hidden (Play tab) hides both arms.
- **Hands only (Show arms, Play tab)** leaves the Slayer's gloves and folds the arms behind them away. In my first
  headset try the off-hand glove looked turned about 90 degrees from my hand; it now turns the way it does with the
  arms shown, which looked right. I checked that on the test PC, not yet in the headset. While the game plays its own
  arm animations (a melee, a glory kill) it draws the whole arms for that moment, and with head aim (Aim with: Head)
  the arms are the game's, so they show whole all the time.
- **Shots can pass through thin walls** if you push the gun through one; the shot starts at your hand
  and there is no wall check yet.
- **Vibration** (Vibration on the Play tab) is new and has only been tried on Quest 3 controllers; its
  strength and feel may change.
- **bHaptics** (bHaptics (experimental) on the Play tab) is experimental. On a real suit a player has felt
  the shots, the landing from a fall and glory kills; the other effects (portals, the Sentinel Crystal, the
  pickup waves including the new ammo wave, jump pads and the rest) have only been checked against a
  stand-in for the bHaptics Player. The portal on a fast travel from the map has not been tried at all yet.
  Which side of the vest a hit plays on, and the heartbeat's place, may be mirrored until a tester confirms
  them.
- **The weapon wheel** (hold the right stick down, then turn it) works in the headset; while it is open it is
  shown straight ahead, with motion controllers or a gamepad. The mod finds the wheel where the game drew
  it, so a larger HUD layout no longer leaves the wheel's edges behind on the HUD; the very first time it
  opens in a session it can sit at its usual place on the right for a frame or two.
- **The game's HUD Scale is Large in VR.** The mod sets the game's own HUD Scale to Large while you play in
  VR, so its small text (mission challenges, the demonic corruption bar, the equipment labels) is easier to
  read; your own HUD Scale stays for the flat game. If you change HUD Scale in the game's menu during VR, your
  choice sticks for the rest of that session and is saved as your own setting, like in the flat game (picking
  Large changes nothing, since that's what VR already shows). To make the HUD bigger or smaller in VR without
  touching the game's setting, change HUD distance or size on the Advanced tab. In my headset the hint under
  the mission challenges, the demonic corruption title and the equipment labels read fine at the default HUD
  distance.
- **Health and ammo on the wrist or the gun** now follow where the game draws them, at any HUD Scale (checked
  on the test PC, not in a headset yet). When you start the game they show on the floating panel for a moment
  until the mod has found them; after that they stay on the wrist or the gun across levels. Something new in
  them (an extra life, the equipment row) can show on the panel for a moment before it moves too. The wrist
  needs both the health and the ammo block on screen, the gun needs the ammo block: if you hid one in the
  game's HUD settings, the whole HUD stays on the floating panel.
- **Picking on the weapon wheel with your hand** (Weapon wheel: Point with your hand, on the Play tab) is
  new and has not been tried in a headset yet. How far you need to turn your hand may change.
- **The weapon wheel on a button** (given to one in the controls editor, the turn stick pointing) is new
  and has not been tried in a headset yet.
- **The weapon wheel from the thumb rest** (Quest and Rift controllers; Thumb-rest wheel on the Play tab, off
  by default) is new and has had a few headset tests. Touch, then push is hard to get right and picking with
  the game's wheel is slow to open; While touched with Weapon by direction works best so far. Weapon by
  direction follows the game's own wheel (the rocket launcher left, the heavy cannon right, and so on):
  **Weapon directions...** on the Play tab changes it. If nothing happens when you rest a thumb, your VR
  runtime may not report the thumb rest. Once the game's wheel is open it cannot be cancelled. Turn stick picks (standing) has not been tried
  in a headset yet, nor has the thumb rest's new touch handling (0.1.41: a touch counts at once, a short loss
  is forgiven).
- **A thumb rest as a button** (Quest and Rift controllers, in the controls editor) is new and has not been
  tried in a headset yet. If nothing happens when you rest a thumb, your VR runtime may not report the thumb
  rest. A controls file with a thumb rest in it makes a version of the mod older than 0.1.45 use the built-in
  controls.
- **Touches as buttons** (A, B, X, Y, a stick, a trackpad or a trigger touched without pressing it, in the
  controls editor) are new and have not been tried in a headset yet. If nothing happens when you touch one,
  your VR runtime may not report that touch. A press or hold on a touch can fire when you rest a finger
  there without thinking, and a press is on even while your thumb only brushes past, so Tap is the easiest
  start. A controls file with a touch in it makes a version of the mod older than 0.1.46 use the built-in
  controls.
- **Controls files from a newer version.** From 0.1.47 on, a button line a newer version wrote that this one
  does not know is skipped and the rest of your controls are kept (the log names each skipped line; the
  controls editor lists them as not used and keeps them). Versions before 0.1.47 still use the built-in
  controls for such a file. An action a version does not know, on a button it does know, still makes it use
  the built-in controls.
- **Edit controls...** should open much faster now (it used to take several seconds and show "Not
  Responding" first). If it still takes several seconds, tell me.
- **The overhead swing** (Gestures on the Play tab, off by default) has had little headset time. How hard you
  need to swing may change. A swing you did not mean still spends a Crucible charge, so turn it off if it fires by
  mistake.
- **Roll wrist** (Gestures on the Play tab, on by default): turning your gun hand over and back quickly switches
  the weapon mod. Turning fast while aiming almost straight up can look like a roll to the mod; if it switches your
  weapon mod when you didn't mean it, you can turn it off there, and please tell me what you were doing.
- **Rumble by hand** (always on): the game does not say what a vibration is for, so the mod guesses from what you
  pressed in the last moments: a hit that lands late, or two things at once, can go to the wrong hand. Tell me if a
  vibration is on the wrong hand.
- **The meathook and glory kills with hand aim** have had little testing.
- **Jumping off climbable walls where you look** is new; it works on a Quest 3, other headsets are untried.
  While you hang on the wall the view is yours: the game no longer limits how far you can look to the side
  or down, and your gun follows your head there, not your hand. If it misbehaves, please tell me where.
- **Piloting the Revenant** in Cultist Base: it turns with the stick, moves where you face and aims where
  your weapon hand points (where you look, with head aim). The Slayer's left arm can stay in view while you
  pilot it, and at some angles you can see inside its shoulders.
- **Playing with a gamepad** (Motion controllers unticked on the Advanced tab): the recenter hold (both sticks
  pressed for 2 seconds; the game sees both clicks too, so the right one also melees), the comfort vignette
  (it follows your sticks), the Glory kills options, Room-scale and bHaptics now work as with motion
  controllers, and the weapon wheel shows straight ahead. They have been tried on a headset simulator with a
  virtual Xbox pad, not in a headset yet. Still missing with a gamepad: the in-headset screenshot (it needs
  the motion controllers), the vignette for a dash or for keyboard and mouse movement, and the equipment
  launcher on a bHaptics vest.

## Menus

- The menu panel is flat. It stays where it appeared until you look away from it for a second, then it
  comes back in front of you.
- In the short pause menu of the very first mission (Load Checkpoint, Exit), neither B nor the Menu
  button closes it; the game does the same with Escape when played flat.
- A prompt that comes up right after a cutscene or a load ("[A] TO DISMISS") sometimes could not be
  closed from the controllers in earlier versions. A, X and the jump button now send the Space it waits
  for there; this has not been tried in a headset yet. If one still won't close, bring the game's window
  to the front on your desktop (click its title bar or its taskbar button, or Alt+Tab to it) and press
  Space on the keyboard, then click **Export report...** after the session: the log now says which window
  was in front when the prompt came up.
- A few screens still name a keyboard key instead of a controller button: tips whose text itself names a
  key ("pressing [M]"), the custom weapon wheel screen's reset to default (R), and on Vive wands the
  Arsenal's customize weapon and equip mod (F, X), which have no button there. The mission-complete
  Rewards tab now reads "LS 1/2 RS" and the Dossier's weapon, Codex and challenge lists "LS" / "RS": push
  either stick left or right, or click the stick named, to turn them (in the Arsenal the right stick's
  click customizes the weapon wheel instead). The Arsenal's hints name X, Y and Right Stick Click, which do
  them; the Fortress's collectible viewer names the grips (LG / RG) that show the previous and next item;
  the loading screen's tips read "[LG] PREVIOUS | [RG] NEXT" and the grips change the tip.
- **In the weapon upgrade screen** the gun can ghost a little when you move your head.
- **The Fortress of Doom** has a few rough spots: some outside surfaces look wrongly shaded, a canister can
  show black without its texture, the old computer is hard to use, and the displays in the Slayer's office
  can ghost.

## Launcher and settings

- **Start the headset's runtime and connect the headset before Launch VR.** When the runtime reports no
  headset, or does not answer, the launcher asks before it starts the game.
- **Steam Cloud warning.** If the launcher warns that "Steam's cloud record is stale", start DOOM Eternal
  once normally through Steam, let it reach the main menu, quit, then use the launcher again.
- **A small game window sits on your desktop** during a VR session. That is normal. While the game is
  not the focused window, your mouse stays yours. Clicking into that window gives the mouse to the game,
  as when you play flat; Alt+Tab or the Windows key gives it back (up to 0.1.50 it stayed trapped in the
  game's window until the game quit, GitHub #30).
- **The comfort vignette** (Vignette on the Play tab) has been checked on a headset simulator, not yet
  in a headset; its strength may change.
- **Use the headset's graphics card** (Advanced tab) is for a PC with two or more graphics cards where VR
  does not start because the game picked another card than the headset's. It is new, off by default, and
  not yet tried on such a PC with a headset; please tell me if you need it and how it went.
- **Not code-signed.** Windows SmartScreen may warn the first time (`INSTALL.md`).

## The DLC (The Ancient Gods, parts one and two)

- The DLC missions, the ARC Carrier and the Master Levels are single-player and allowed, but they have
  only had a short check. Things to watch: swimming (you swim and dash where you look, also under hand
  aim), the Sentinel Hammer (on the Crucible button, the left stick
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
  only turns multiplayer and achievements off. A signed-in check on Steam and Game Pass found no sign that
  the game's online side (Events, challenges, the account link) reacts to the mod, but the server's side
  can't be seen from the PC. To be safe, don't use EternalVR to earn Event or challenge progress, and
  never try to play online with it.
