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
