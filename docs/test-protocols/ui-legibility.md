# UI legibility protocol

The test behind M6's legibility criterion (D-035, T-077, ROADMAP foundation step 5): every message the game shows
must be readable in the headset at default settings, not only the HUD's numbers. It has two parts: a size check
computed from GUI captures, which can run on the rig, and a reading test in a headset, which only a person can do.
The beta gate needs both passed at default settings.

## Settings

- The launcher's defaults: HUD panel (Play tab HUD "Panel"), HUD width 2.0 m at 1.5 m, menu panel 2.0 m at
  1.5 m, render size from the headset table.
- The game's own defaults, except Subtitles on (Settings, Audio or Game, depending on the game version), so
  that there are subtitle lines to read.
- Reference headset: Quest 3 (about 25 pixels per degree in the middle of the lens). Another headset may be used;
  write down which.

## Part 1: the size check (rig)

For each item below, capture the GUI target while the item is on screen (`ETERNALVR_CAPTURE_UI`, see
`docs/VR_MENUS.md`) and measure the cap height of its smallest text in pixels: the height of a capital letter
without accents (an H, E or T), in the capture's own pixels.

The HUD panel shows the target's 16:9 band, `HUD width` metres wide at `HUD distance` metres, so one pixel of the
band spans `HUD width / band width` metres. Near the middle of the view a cap height of `h` pixels is

    angle (degrees) = 57.3 * h * HUD width / (band width * HUD distance)

At the defaults (2.0 m at 1.5 m) and a band 2048 pixels wide that is 0.0373 degrees per pixel. The menu panel uses
the same formula with its own width and distance.

**Pass:** the smallest text of every item is at least **0.5 degrees** tall (13.4 band pixels at a 2048-wide band;
a starting value, T-077). An item under it fails Part 1 whatever Part 2 shows. Record the capture size, the band
width, `h` and the angle per item.

## Part 2: the reading test (headset)

The reader (the owner, plus one tester where available) plays at the settings above and reads each item aloud
while it is shown, without leaning in, moving the panel or changing any setting. A second person, or a recording
of the session with the game's audio, notes what was read.

**Pass:** every item is read correctly and completely. A word read wrong or skipped fails the item; a line that
goes away before it can be read fails too (that is a timing problem, not a size problem, and is noted as such).

## Items

| Item | Count | Where |
| --- | --- | --- |
| Wrist-glance or panel HUD: health, armor and ammo | 10 prompted glances | the combat route of the benchmark, at random moments the observer calls out |
| Subtitles | 10 lines | any mission's dialogue (the first mission's opening has several) |
| Notifications (pick-ups, codex entries, secrets) | 5 | any mission |
| Pop-ups (tutorial pop-ups, the mission-complete screen) | 3 | first mission, end of any mission |
| Tutorial prompts ("[button] to ...") | 3 | first mission |

## Results

One row per run. A run that fails an item lists it, with what was read and why it failed, and the item is
fixed or listed in `docs/release/KNOWN-ISSUES.md` before the beta.

| Date | Build | Headset | Reader | Part 1 | Part 2 | Failed items |
| --- | --- | --- | --- | --- | --- | --- |

### Part 1 measurements

0.1.56 (dev main 8cb43340), Steam version, render 2048 x 2208 per eye (band 2048 x 1152), launcher defaults,
subtitles on, the first mission's opening; cap heights from the GUI captures (pixels whose brightest channel is
over 150 of 255, so a glyph's glow may add a pixel):

| Item | Text measured | Cap height | Angle | Part 1 |
| --- | --- | --- | --- | --- |
| Subtitle | "Rip and tear, until it is done." (the R) | 19 px | 0.71 deg | pass |
| Hint bar prompt | "HOLD TO SKIP" | 15 px | 0.56 deg | pass |
| HUD health | "100" | 24 px | 0.90 deg | pass |
| HUD ammo | "16" | 27 px | 1.01 deg | pass |
| Interact prompt (third mission) | "[Right Stick Click] USE" (the R) | 17 px | 0.63 deg | pass |
| Mission challenges notification, title | "MISSION CHALLENGES" | 14 px | 0.52 deg | pass |
| Mission challenges notification, names | "Master of Turrets" (the M) | 12 px | 0.45 deg | **fail** |
| Mission challenges notification, hint | "PRESS [HOLD X] TO VIEW CHALLENGE DETAILS" | 10 px | 0.37 deg | **fail** |
| Demonic corruption bar title | "DEMONIC CORRUPTION" | 12 px | 0.45 deg | **fail** |
| Equipment button labels (above the equipment slots) | "Left Grip", "Right Stick", "Left Trigger" | 22 px | 0.82 deg | **fail**: the names are longer than their boxes and overlap ("Left Grip ght St... ft Tr...") |
| Pop-ups, tutorial prompts, subtitles in play | not captured yet | | | to do |

0.1.75 (dev main 6a2e235b), the same settings, mission three at the checkpoint before the gate (the challenges panel
and the demonic corruption bar on screen), once at the game's default HUD scale and once with its HUD scale held at
Large (`swf_anchorProfile` 2), plus the first mission's opening on a new profile. Same measure:

| Item | Text measured | Default HUD scale | Large HUD scale |
| --- | --- | --- | --- |
| Mission challenges notification, title | "MISSION CHALLENGES" (the M) | 14 px, 0.52 deg, pass | 16 px, 0.60 deg, pass |
| Mission challenges notification, names | "Master of Turrets" (the M) | 12 px, 0.45 deg, **fail** | 14 px, 0.52 deg, pass |
| Mission challenges notification, hint | "PRESS [HOLD X] TO VIEW CHALLENGE DETAILS" (the P) | 10 px, 0.37 deg, **fail** | 13 px, 0.49 deg, **fail** |
| Demonic corruption bar title | "DEMONIC CORRUPTION" (the D) | 12 px, 0.45 deg, **fail** | 13 px, 0.49 deg, **fail** |
| Equipment button labels | "LG" (the L; short names since 0.1.58, no longer overlapping) | 13 px, 0.49 deg, **fail** | 15 px, 0.56 deg, pass |
| Low ammo warning (first mission) | "LOW AMMO" (the L) | 15 px, 0.56 deg, pass | not captured |
| Pop-ups, tutorial prompts | not captured: a new profile on the first mission's opening showed no tutorial pop-up in four minutes of scripted play | | to do |

At the default HUD scale four of the game's small texts are under the bar (0.37 to 0.49 deg); at Large every one of
them gains two or three pixels, the names and the labels pass, and the hint and the corruption title fall short by
less than a pixel (0.49 deg). Part 2 decides whether 0.5 deg is the right bar (T-077).

0.1.77 (dev main f07c9ef8), the same settings with the HUD Scale held at Large (the VR default since 0.1.77), on a new
profile (no tutorial seen): the Slayer Tutorial pop-up on the game's tutorial map (`+devmap
game/tutorials/tutorial_sp`, the first pop-up of a new profile), the first mission's loading screen and the death
screen, and the prompts in the tutorial map's first room. Same measure (white text: pixels whose darkest channel is
over 150, so the orange or green behind it does not count):

| Item | Text measured | Cap height | Angle | Part 1 |
| --- | --- | --- | --- | --- |
| Tutorial pop-up, title | "SLAYER TUTORIAL" (the S) | 21 px | 0.78 deg | pass |
| Tutorial pop-up, body | "This is the Slayer Tutorial." (the T) | 16 px | 0.60 deg | pass |
| Tutorial pop-up, dismiss line | "[A] TO DISMISS" (the D) | 15 px | 0.56 deg | pass |
| Loading screen tip | "Keep moving in combat." (the K) | 16 px | 0.60 deg | pass |
| Death screen | "LOAD CHECKPOINT", "EXIT" (the L, the E) | 18 px | 0.67 deg | pass |
| Tutorial prompts on the tutorial map | "Double Jump [A], [A]" (the D; a sign in the world, measured in the eye image at about 0.077 deg per pixel near the middle, from where the player first sees it) | 26 px | about 2.0 deg | pass |
| Mission-complete screen | not captured: needs the end of a mission | | | to do |

The pop-up is dismissed with A as its line says (the rig's scripted A press closed it). The tutorial map's prompts are
signs in the world, so their size depends on the distance; the first mission's "[button] to ..." prompts use the hint
bar measured above ("HOLD TO SKIP", 15 px at the default HUD scale).
