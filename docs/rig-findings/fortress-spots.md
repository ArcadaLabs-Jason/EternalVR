# The Fortress of Doom's rough spots: the outside surfaces, the black canister, the old computer

The owner's playtest of 0.1.50 (2026-10-13, in the headset) listed, for the Fortress of Doom: some outside surfaces
wrongly shaded, a canister black without its texture, and the old computer hard to use (docs/release/KNOWN-ISSUES.md,
Menus; the office displays are in docs/rig-findings/taa-ghost.md). In his test of 0.1.79 (2026-10-23) he put the
outside spots in one class with Urdak's tunnel, whose textures and decals showed only when looking one way [notes].
This page records where those spots are, what the test PC shows there now in each eye and in the flat game's
render, and the old computer under the laser pointer and hand aim. Evidence is [rig] unless marked; nothing here was
tried in a headset.

## 1. Where he was

His session `20261013-201024` (the launch of taa-ghost.md section 2: DLSS Balanced, preset K, 2496x2688 per eye)
came to the Fortress after Exultia, so it was the early-campaign hub (`game/hub/hub` from a campaign in progress),
not the late one a finished campaign's Continue loads (the late hub's window lights are red, the early one's blue;
his captures show blue). His 13 in-headset captures there, each with its game position in the `.txt` beside it
[log]:

| Capture | Position (x y z, yaw) | What it shows |
|---|---|---|
| 21:46:40 | -12.22 -42.01 -1.34, -132 | the grand stairs |
| 21:48:18 to 21:48:23 | -0.73 -76.35 -13.34, 79; -2.17 -75.55, 113; -2.29 -76.10, 103 | the room with the hanging cylinder on a chain, the candles and the long table: from the second spot on, a canister behind the table is a black shape in both eyes |
| 21:49:27 to 21:50:50 | -78.83 69.49 -2.64, -60 (twice); -70.51 46.52 -8.30, -90; 78.19 45.10 -10.64, -141 (three times) | the outside decks: the stone walls in sunlight with a hazy dark band of shadow beside them |
| 21:53:06 | -2.35 -137.05 -7.05, 2 | a fall below the ship, the moon underneath |
| 21:54:50, 21:55:41 | -0.11 40.55 -10.54, -97; 3.27 39.93 -10.54, -64 | the office (its displays: taa-ghost.md section 4.5) |

The canister is not the hanging cylinder: it is a glass tank with a domed metal cap and base, something dark inside,
standing behind the table, left of the arch with the candles; from the first spot it is half hidden behind the
hanging cylinder.

## 2. Rig method

The test PC (RTX 3080 Ti), OpenXR-Simulator, the Release layer of main `b601d2b7`, ray tracing on (the rig profile's
own), TAA (the default), foveation off, 2048x2208 per eye, the simulated head still and level. Each run puts the
player on a list of spots by `setviewpos` from the debug schedule and takes two in-headset captures at each, 3 to 5 s
after the move. The early hub by `-Map game/hub/hub` (as on his visit); the late hub by Game Slot 1's Continue Game
(the 100% save), keyed through the menus. Arms:

| Arm | What renders |
|---|---|
| VR | Route S, each eye its own view (the default) |
| mono | head-tracked mono: one render per tick on the flat game's render path, the headset's FOV (108 x 110) |
| game FOV | mono with `ETERNALVR_SET_FOV=0` and `ETERNALVR_HEAD_POSITION=0`: the game's own symmetric FOV and the game's own eye, the head still and level: the flat game's view from the same spot through the layer |
| RT off | VR with `ETERNALVR_DEBUG_CVARS=r_useRayTracing=0` |
| light-cull off | game FOV with `ETERNALVR_LIGHT_FRUSTUM=0` (the game's own light culling) |

A truly flat launch (no OpenXR runtime) has no debug schedule (the schedule's clock starts with head aim), and the
Cinema mode installs none either, so the game FOV arm stands in for the flat game: the stereo routes are not in
it, and the light-cull run takes out the one change the layer makes to the game's lighting in mono.

## 3. The canister

The tank is dark in every arm, from every angle tried (yaws 60 to 125 from (-2.17, -75.55), 79 to 113 from
(-0.73, -76.35)), in both hubs, and the same in eye L and eye R:

- VR, eye L at yaw 90: the tank's cap 17 of 255 (mean 8-bit grey) against 53 for the wall beside it; RT off 18
  against 55, the glass blacker still (with RT on a little of the inside shows through it). Eye R the same shape and
  darkness (`fs-cansweep-def`, `fs-cansweep-rtoff`).
- mono and game FOV: the same dark tank (game FOV at yaw 90: cap 19, wall 42); with the light-cull off the image is
  unchanged (mean difference 0.7 of 255, 0.1% of pixels over 20).
- Brightened five times, every arm shows the tank textured: rust on the cap and the base, the glass see-through,
  the shape inside. It is lit very little, not drawn without its material.

So the black canister is the game's own look at that spot in this version (the flat render path draws it as dark),
not something one eye or the stereo routes do. In the headset, with its contrast, a tank that dark reads as black.

## 4. The outside surfaces

At his outside spots eye L, eye R, mono and game FOV agree, and they agree with his captures: the stone in sunlight,
the hazy dark band where a tower shades the deck (`fs-early-def-a`, `fs-early-mono-a`, `fs-early-flatfov-a`; the
late hub's `fs-late-def-a`, `fs-late-mono-a`). Eye L against eye R (16-pixel blocks matched within the parallax,
as in taa-ghost.md): the blocks that differ are the hands and the gun, which are near, and edges.

For his "only when looking in a specific direction", yaw sweeps at two of his spots, 20 degrees a step: (-78.83,
69.49) from -120 to 0 and (78.19, 45.10) from -180 to -100, in VR and in mono (`fs-extsweep-def`,
`fs-extsweep-mono`). Each capture was warped onto the next by the pure rotation between them (the same eye, the same
position), the exposure's change taken out (the gain over the overlap: 0.72 to 1.02; the auto exposure moves when
the bright planet comes into view), and the overlap compared. Mean differences 9 to 17 of 255 in eye L and eye R,
9 to 14 in mono, almost all of it the hands and the gun (they turn with the head) and edges; no surface turned
darker or lighter as a patch in VR that did not in mono. The few view-dependent patches (the glyph wall behind the
haze band, some windows high on the tower) show in mono as well: the volumetric haze and the screen-space effects
of the game's own render.

So on the test PC, in this version, the outside spots look the same in both eyes and in the flat render path, and
turning shows no direction-dependent surface the flat path does not have. The two sessions that reported it ran
builds with known faults of that kind [log]:

- 0.1.50 (his session above): the light-cull fix on (`light-cull: on by default`) but no per-eye decals (no
  `seq-decals:` line): an eye got only the decals the game chose for its own view, so decals just past an eye's
  inner edge were left out and came back as the head turned (docs/rig-findings/stereo-umbra-decals.md;
  KNOWN-ISSUES, "Wet floors").
- 0.1.79 (his test of 2026-10-23, on the test PC; his note on the exterior: "a little texture/shading popin
  depending on angle. If in the corner of my eye it looks different than in center"): the decal gather that added
  every decal around the eye pair (`seq-decals: ... plus every decal in the frustum around the eye pair`), the
  version whose flood stopped the floor's own decals at some view directions (stereo-umbra-decals.md, fixed after
  0.1.79), and Foveated rendering on (`vrs: foveation: full rate within 24 deg of head-forward, half rate within 40
  deg, quarter outside`), which shades the edges of the view coarser by design.

Either fits what he saw [inferred]; neither build was run here. With Foveated rendering at Balanced on this build
(`fs-extsweep-fov`) the sweep's numbers are those without it (mean differences 9.3 to 17.2) and the fine detail at
the edges of the view drops by 7 to 17% (the Laplacian's variance, top and left edges of eye L): a softer edge, not
a surface that changes.

## 5. The old computer

It is the beige PC on the desk under the man cave's window, `mancave_interact_hub_doom_arcade_1` (the late hub;
`fs-pc-a` to `fs-pc-l`). `teleport` to it puts the player at (7.47, 45.59), view z -9.7, facing yaw 214.5, away from
it; it sits about a metre behind that, at yaw 34.5, below the view.

- **Opening it.** Use is the weapon hand's stick click (melee and use are one key), and under hand aim the use goes
  along the weapon hand: from that spot facing it the hand had to point 55 degrees down; at 20, 35 and 45 degrees
  down the click was only a punch. In gamepad mode (head aim, the head 50 degrees down) the pad's right stick click
  opened it. It opens as the game's own close-up, the camera moving to face the monitor (`controllers: forced view
  starts (view inhibited; inhibit 0x1F87F)`), and the head keeps its own pitch and roll (the general rule of
  docs/rig-findings/camera-takeovers.md): with the head level the monitor fills the view; with the head still 50
  degrees down, as it was to open it, the view shows the keyboard and only the monitor's lower edge until the head
  comes up.
- **Inside.** A file chooser ("Choose a File": a DOOM icon and a floppy with a question mark) whose prompts name the
  keyboard (arrow keys to move, Enter to select, Esc to close), or the pad when one is connected (the left stick, A,
  B). There is no cursor, so the laser pointer has nothing to point at, and the layer sees no menu (no `menu:` line
  while it is up). With the motion controllers nothing in it answered the sticks, A, B, X, Y or the trigger (each sent
  its gameplay action: the stick turned and walked, A jumped, B dashed, X the Dossier, Y the objectives, the trigger
  fired); the Menu button (the pause action) closed it. An Enter of the rig's key injection (meant for the main
  menu, late by chance) started classic DOOM on the monitor, its title menu over its demo; there no controller
  button changed anything, the Menu button included.
- **With a pad** (gamepad mode, the QA suite's virtual pad, `fs-pc-l`): D-pad right moved the selection from the DOOM
  icon to the floppy, D-pad left back, and A started classic DOOM, its title menu over its demo. What the pad does
  inside classic DOOM was not seen: the head was still 50 degrees down, so the captures show only the monitor's
  lower part (the demo playing on behind the menu through Down, A, B and Start). (`fs-pc-j` and `fs-pc-k` lost the
  pad after its second state: the harness's file swap, fixed for `fs-pc-l`.)

So the computer is the game's keyboard-and-pad minigame: with the motion controllers it opens (the hand pointed down
at it) and closes (the Menu button), and nothing in between works; a gamepad picks the file and starts classic DOOM.
A fix would be a key map of its own while it is up (the sticks to the arrow keys, A to Enter, B to Esc, and classic
DOOM's keys while that runs) and a way to tell that it is up (no cursor, no menu the layer knows): not small, so not
done here.

## 6. What this leaves

- The headset: the tank's darkness and the outside spots have not been looked at in a headset since October, and
  his session's build (0.1.50) was not run here.
- DLSS: not run here (no arm differed with TAA, and his captures with DLSS show the same tank).
- The old computer under head aim with the motion controllers, and playing classic DOOM with a pad.
- Outside spots other than his: the sweeps covered two of his spots; the rest of the Fortress's outside was not
  walked.
