# Menus in VR: a panel and a laser pointer (M6)

Builds on the UI layer (`docs/rig-findings/ui-layer.md`) and the motion controllers
(`docs/VR_CONTROLLERS.md`). While the game shows its menu cursor, the menu is on a world-locked panel in
front of the player, a laser from the pointing hand meets the panel, and the controllers drive the game's
own mouse cursor. No keyboard or mouse is needed from the title screen to gameplay and back. The engine
facts are in `docs/rig-findings/menus.md` (build 25216728).

**Status.** Built, unit-tested and checked live on the rig with OpenXR-Simulator and scripted controller
input (the live checks below). On by default wherever the UI layer is on (the launcher's stereo default)
and on the cinema path; `ETERNALVR_MENU_POINTER=0` turns it off. Not yet tried on a real headset.

## How it works

```
XR worker, every XR frame                                     game (message pump, main thread)
  menu_cursor::read(): the game's idCursor (active, x, y)
  active and a panel can show it?  -> menu mode               WM_INPUT -> GetRawInputData (replaced):
    on entry: panel placed in front of the head (yaw only)     an injected RAWMOUSE / RAWKEYBOARD record
  each hand's aim ray (controller snapshot, LOCAL)          ->  idCursor += dx, dy; clicks and the wheel
    -> intersectPanel -> (u, v) -> cursor pixel                 become MOUSE1 / wheel key events; Escape,
  MenuRouter: move / click / wheel / keys, suppression          Q and E are read as scan codes
  layers: panel (UI quad or the game's frame) + beam + dot
```

- **Menu mode** is the game's own signal: the engine's global idCursor `active` flag, set whenever a menu,
  the title screen, the pause menu or another GUI screen shows the mouse cursor. It is read every XR frame
  (the cursor is located once by signature; anything that does not match leaves the pointer off).
- **The panel.** When the cursor appears, a panel is placed `ETERNALVR_MENU_DISTANCE` ahead of the head
  (yaw only, at the UI quad's height), `ETERNALVR_MENU_WIDTH` wide (both default to the UI quad's 1.5 m and
  2.0 m, so the pause menu keeps its size), world-locked in LOCAL until the cursor goes away. What it shows
  depends on the frame: over a head-tracked frame (pause, the in-game screens) the UI quad moves onto the
  panel; on the cinema path (title screen, main menu, their sub-screens) the game's whole frame, 3D menu
  scene and GUI, is shown on the panel instead of the 2.5 m cinema screen. Loading screens keep the cinema
  screen.
- **The desktop window.** Over a head-tracked frame the GUI is only on the panel, so the game's window
  (the desktop mirror) shows the panel's image, the GUI over black, fitted like an eye
  (`ETERNALVR_MIRROR_CROP`); before, it was black for the whole pause menu (`docs/VR_STEREO.md`, Desktop
  window, Menus in the window). On the cinema path the window already shows the whole frame.
- **The panel's hold.** The cursor can go while its menu stays up (the pause menu logged `the cursor is
  gone` 0.12 s after it opened). Over a head-tracked frame, while the UI layer's backdrop test
  (`docs/rig-findings/ui-layer.md` section 5) still sees the menu's full-screen backdrop, the UI quad stays
  on the panel for up to 1 s without the pointer, and the controllers' gameplay input is released as usual.
  A cursor back within that time finds the panel where it was instead of placing it again. Resuming the game
  removes the backdrop, which ends the hold within a few frames.
- **The crosshair mask.** While the cursor is up, the panel shows or its hold lasts, the hand-aim mask of
  the game's crosshair is off, so menus never show a see-through square in their centre.
- **The pointer.** Each hand's aim ray (the controller snapshot, with scripted input laid over it) is
  intersected with the panel (`features/menu/panel_pointer.hpp`). The snapshot is in room space (LOCAL
  under the recenter transform) and the panel in LOCAL, so each ray (and the head the beam faces) is taken
  back into LOCAL with the room transform the snapshot was located with (`InputFrame::roomFromLocal`,
  `menu::localFromRoom`). Before this the rays were used in room space: after the first anchor, and far more
  after a standing height re-anchor (1.1 m), the ray missed where the hand pointed (the owner's weapon
  upgrade screen while standing, 2026-09-27). When the room is re-anchored while a panel is up (a recenter,
  ours or the runtime's, or the height after standing up or sitting down: the room transform jumps by more
  than 5 cm or 3 degrees, or the runtime moves LOCAL), the panel is placed again in front of the head
  (`menu: the room was re-anchored; the panel is placed again ...`). It is also placed again when the
  head has faced more than 60 degrees away from it (horizontally) for a second (`menu: the head turned away
  from the panel; ...`, `features/menu/panel_follow.hpp`; `ETERNALVR_MENU_FOLLOW=0` keeps it where it
  appeared). A glance away leaves it in place. The pointer is on the dominant hand
  (`ETERNALVR_HANDEDNESS`); pulling the other hand's trigger (or pressing A / X) while it points at the
  panel moves the pointer to that hand. A pale beam (a thin quad from the hand to the hit, turned to face
  the head; `ETERNALVR_MENU_BEAM=0` leaves it out) and a dot on the panel (the reticle image, 0.9 degrees)
  are drawn as quad layers; the game draws its own cursor at the same place a frame or two later.
- **The cursor, closed-loop.** The game moves its cursor only by relative mouse motion, 1:1 in GUI pixels,
  clamped to the GUI's size. The router (`features/menu/menu_router.hpp`) reads the game's cursor, sends the
  difference to the pixel under the ray as one relative raw mouse event, and sends the next one only once the
  game shows the cursor where the last should have put it (or after 150 ms). Nothing is written to the
  game's memory.
- **Input into the game.** The layer already replaces the exe's `GetRawInputData` import for key injection
  (`vkcore/key_inject.cpp`); injected mouse events travel the same way: a posted `WM_INPUT` whose handle the
  replacement answers with a `RAWMOUSE` record (relative motion, `RI_MOUSE_*` button flags, the wheel). It
  needs no focus and never touches the desktop cursor. Keys are answered with their scan code and the E0
  flag where the key has one (the game reads scan codes).
- **The desktop cursor is left alone.** A game that believes its window is active (the layer keeps it so
  while VR runs) takes the mouse: it registers raw mouse input with `RIDEV_CAPTUREMOUSE`, clips the cursor to
  its window, and puts the cursor back in the window's centre after every mouse movement, the injected ones
  included. The layer replaces the exe's `SetCursorPos`, `ClipCursor` and `RegisterRawInputDevices` imports:
  while the game's window is not the foreground window those moves and clips are dropped and the capture
  flag is removed, so a person at the PC keeps the mouse. With the game in front nothing changes.
- **The view while a menu is up.** Head aim writes nothing while a menu or popup is up over the game, and
  the body keeps the yaw it had when the menu came up (`aim: a menu is up: head aim holds the body yaw at
  ...`), so the world stays put while the head looks around. Before, the body was worked out every frame
  from the game's angles, which the game rewrites every frame while a popup holds its view (about 94
  rewrites a second in the owner's Objective Marker popup, 2026-09-27, with the logged body yaw moving from
  95.0 to 112.6 degrees while the game's view stayed at 80.3): the owner saw the view turn the wrong way when
  he looked around in the popup.
- **Gameplay held back.** While the menu is up, the controllers' gameplay actions, movement and turning are
  not sent (only the pause key, so the Menu button still opens and closes the pause menu). After the menu
  closes they stay held back until every trigger, grip and face button is let go, so the pull that clicked
  Resume does not also fire the gun.

## Controls while a menu is up

| Input | Does |
|---|---|
| Aim (either hand; the dominant one first) | Moves the game's cursor to where the beam meets the panel |
| Trigger, or A / X, of the pointing hand | Left click (hold to drag: sliders, scroll bars) |
| Other hand's trigger or A / X, pointed at the panel | Takes the pointer, then clicks |
| B or Y, either hand | Back (Escape): closes dialogs and sub-screens; at the pause menu's root it resumes, at the main menu's root it asks to quit |
| Left grip / right grip | Previous / next tab (Q / E) in tabbed screens (settings, the Dossier) |
| Either stick up / down | Scroll (the mouse wheel), repeating while held (the Dossier map: below) |
| Either stick left / right | Previous / next tab (Q / E) (the Dossier map: below) |
| Either stick click | C, the Dossier map's centre key (E in a popup, below); none for the second stick of the recenter chord |
| A / X in a tutorial or lore popup | Space, the popup's continue key (below) |
| Y in a tutorial or lore popup | Left Alt, held while Y is: the objectives key (below) |
| A gameplay button in a tutorial or lore popup | Its action's default key, held while the button is (the Flame Belch button R, and so on; below) |
| Left Menu (tap) | Pause / resume, as in gameplay |

The grips were Back (the pointing hand) and the right mouse button (the other hand) until the owner's Quest
3 session of 2026-09-27 asked for the tabs on the grips; the Dossier map's rotation moved to a stick.

The game's menus do not move their focus with the arrow keys, so none are sent; sliders are set by
clicking or dragging on them.

**Tutorial and lore popups.** A tutorial or lore popup shows the game's cursor over the game, so it comes
up in menu mode, but it waits for Space (continue) or E (use). On the owner's Quest 3 (2026-09-27,
`e1m1_intro` from the Glory Kill tutorial checkpoint) four such screens came up unasked, for 2 to 28 s; the
clicks, Escapes, C and tab keys the router sent in them did not close them, the keyboard did. A cursor screen over the game that comes up without the
controllers having asked for one in the 1.5 s before (the pause key, the Dossier or mission information,
`game::opensMenu`) is taken for such a popup (`menu: the game shows its cursor (a popup over the game, not
asked for: ...)`). In it A / X, either hand, taps Space instead of clicking and a stick click taps E
instead of C; the trigger still clicks and B still goes back. Y holds Left Alt (`VK_LMENU`, scan code
0x38, no E0 prefix) for as long as it is held: the Objective Marker tutorial ("[L.ALT] PULL UP OBJECTIVES
TO DISMISS", the owner's session of 2026-09-27) waits for the objectives key and could not be closed from
the controllers (the pointer and the mapper's Y hold, `_objectives`, are held back in menu mode). The key
goes up when Y is let go, or when the popup closes (`menu: key down 0xa4` / `up 0xa4`). A menu opened from the
keyboard (Escape) is taken for a popup too: A then resumes the pause menu (Space does that there), which
the rig showed; the trigger clicks as usual. One menu changing screens hides the cursor for a moment
(Settings from the pause menu: 0.25 s on the rig, 2026-09-29, in 1 of 9 opens), so a cursor back within 0.75 s of the last one
going keeps that menu's kind (`features/menu/menu_kind.hpp`; `menu: the cursor is back after 0.25 s: the same
menu (screen) on another screen`); before that the Settings screen was taken for a popup, and holding the turn
stick down there sent Q (the game's previous tab) and B also sent Left Shift. The Dossier comes back as a plain
screen, since its page is not known then. The earlier fix (jump sends Space and melee E while the game
suppresses the user command's buttons, `usercmd: popup key`) never fired on the headset: in menu mode the
controllers' gameplay actions are held back before they reach it.

**The mechanic's own key.** A tutorial popup that introduces a mechanic waits for that mechanic's key: on
the owner's Quest 3 (2026-09-27, 126.6 s into the session) one could not be closed with Space, Left Alt,
Escape, Q, E or clicks, and he had to restart the checkpoint. In a popup every gameplay action the
controllers press now also presses the key the shipped Slayer binds put it on (bindset 0,
`game::defaultKey`; docs/notes/eternal-pc-keybinds.md), held while the action is and at least the
router's minimum hold: Flame Belch R, chainsaw C, equipment Left Ctrl, switch equipment G, dash Left
Shift, switch weapon mod F, quick switch and the weapon wheel Q, the Crucible V, the weapon slots 1 to 8,
and the Dossier TAB (a tutorial asks for TAB to open the Dossier; hold X). Only pause stays on its own buttons.
The mapper publishes the actions it maps before a menu holds them back (`controllers::heldActions`, taken
as released when the user command has not been built for 0.25 s) and the router presses the keys
(`menu::popupActionKey`). Only a press that starts in the popup counts, and every key goes up when the
popup closes. The first press of each action in a popup is logged (`menu: popup: flame_belch sends R
(0x52)`), then the key as usual (`menu: key down 0x52`). No key is sent for fire, the weapon mod and next
/ previous weapon (on the mouse; the trigger stays the pointer's click), for movement and turning (the
sticks are not actions), for pause, the Dossier and the automap (they open screens of their own), nor for
jump, melee and mission information: their keys (Space, E, Left Alt) are the popup's own on A / X, a
stick click and Y in every control map, and sending them from the action too would press them twice for
one button. Buttons that are both keep both: B also sends Escape, the left grip also Q (its tab key), X
Space before switch equipment's G. The player's own key binds are not read: a player who rebound the
Flame Belch still has R sent.

## The Dossier, its map and the upgrade screens

Holding X (`_inventory`) opens the Dossier on its Map page, and the game shows its cursor there, so menu
mode, the panel and the pointer come up as for the pause menu (`the game shows its cursor (a menu over the
game)`). The map is drawn in 3D in the game's own view (head-tracked, around the player), with the
Dossier's GUI on the panel. The flat game's hints on the map are: left drag pans, right drag rotates, the
wheel zooms, C centres, Escape goes back; the game also has pad and keyboard pan speeds
(`automap_panSpeedController`, `automap_panSpeedKeyboard`) but no keyboard rotation. From the controllers:

| Screen | Works | How |
|---|---|---|
| Tabs (Map, Arsenal, Codex, Challenges) | Yes | click the tab, or the grips (left Q, right E); a stick left / right off the map |
| Map: pan | Yes | the weapon hand's stick (left drags, below), or the trigger held while the pointer moves |
| Map: zoom | Yes | the other stick up / down (the wheel, a notch every 0.1 s) |
| Map: rotate | Yes | the other stick left / right (right drags, below) |
| Map: centre | Yes | a stick click (C) |

**The map from the sticks** (`features/menu/map_drag.hpp`). The owner found moving the map with the hand
disorienting (2026-09-27). The game pans only with a left drag and rotates with a right drag (its pad and
keyboard pan speeds exist, `automap_panSpeedController`, `automap_panSpeedKeyboard`, but which keys pan is
not known and a pad would switch the game's glyphs), so a held stick becomes drags: the cursor goes to the
middle of the screen with the buttons up, the button goes down, the cursor moves at the stick's speed (pan:
0.6 GUI heights per second at full deflection, the drag follows the stick, so the map moves the way the stick is pushed;
rotate: 0.5), and after 0.2 of the GUI's smaller side, or when the stick is let go, the button goes up and
the next stroke starts from the middle. The cursor never reaches the edge of its range, where the game
would clamp it. While a stroke lasts the drag owns the cursor and the ray does not move it; with the sticks
at rest the ray has it again, so the map's buttons can still be pointed at and clicked. A drag does not
start while the trigger holds the left button.

**Which page is up.** The game does not say. The router takes a menu that comes up within 1.5 s of the
controllers' Dossier (or automap) action to be the Dossier on its map page (`controllers::
dossierRequestedWithin`; logged `the game shows its cursor (the Dossier, on its map: ...)`), then counts
the tab keys it sends (Map, Arsenal, Codex, Challenges). A click on the tab strip (the top 8% of the
panel) or a tab key past either end (the game may wrap or stop) makes the page unknown, which turns the
map's sticks off until the Dossier is opened again; a popup never counts. Changes are logged (`menu: the
Dossier's map page: ...` / `not the Dossier's map page (or not known): ...`).
| Arsenal: weapon choice | Yes | click the weapon's icon (the A / D keys are not needed) |
| Arsenal: a weapon's upgrade screen (Upgrades, Mastery) | Yes | click the mod; B returns to the Arsenal |
| Arsenal: buy an upgrade | Clicks land on it; not seen bought (no weapon points in the save) | click |
| Arsenal: customize weapon wheel (T), equip / unequip mod (X) | No | key-only; the hint bar is not clickable |
| Codex: categories, entries | Yes | click the category icon or entry; a stick up / down scrolls |
| Challenges: Mission / Weekly | Yes | click |
| Close | Yes | B (Escape) at any tab |

The Praetor suit screen did not appear in the Arsenal of the rig's e1m2 save (only the two weapons owned
there), so it is untested. Holding Y (`_objectives`) sends the action (`controllers: action mission_info`)
but showed no overlay in e1m2 and no cursor, so it needs no menu handling; what the game shows for it is
still to be seen on a headset.

## The weapon wheel

The weapon wheel is not a menu, although it selects with the same cursor (`docs/rig-findings/menus.md`
section 4; the stick side, and pointing with the weapon hand instead (`ETERNALVR_WHEEL_SELECT=hand`), are
in `docs/VR_CONTROLLERS.md`, "Weapon wheel"). On the rig the wheel did not show
the cursor, so the router never saw it. Should the game show it while the controllers hold `weapon_wheel`
and no menu is up, that cursor belongs to the wheel (`features/menu/wheel_cursor.hpp`): no panel, no
pointer, no hold on gameplay input, so the stick's motion and the release that picks the weapon still reach
the game. It stays the wheel's until the cursor goes, or 0.3 s after the button is let go (a cursor still
shown then is a menu of its own, taken as a new one). Popups, pause, the Dossier and the map are unchanged:
the cursor is only the wheel's when it comes up during the hold, and the multiplayer guard gates it like
every menu. Log: `menu: the weapon wheel is up: the game shows its cursor at x, y; ...` and `menu: the
weapon wheel's cursor is gone`.

## Settings

| Variable | Values | Default |
|---|---|---|
| `ETERNALVR_MENU_POINTER` | `1` / `0` | `1` |
| `ETERNALVR_MENU_DISTANCE` | metres, 0.3 to 10 | `ETERNALVR_UI_DISTANCE` (1.5) |
| `ETERNALVR_MENU_WIDTH` | metres, 0.1 to 10 | `ETERNALVR_UI_WIDTH` (2.0) |
| `ETERNALVR_MENU_BEAM` | `1` / `0` | `1` |
| `ETERNALVR_MENU_FOLLOW` | `1` / `0` | `1` |

## Log lines

`menu:` the located cursor (`cursor event handler at RVA 0x18000E0, cursor pointer at RVA 0x47DDB88`),
the map page (`the Dossier's map page ...`), a panel placed again after a re-anchor,
`pointer on; panel ...`, `right button down` / `up`, `the game shows its cursor (a menu screen | a menu over the game): panel ...`,
`the cursor is gone; the panel and the pointer are down` (or `...; the pointer is down, the panel stays
while the menu's backdrop shows`, then `the cursor is back; the panel stays where it was` or `the panel is
down (held N s)`), every button, key and wheel event and the first moves, `controllers' gameplay input
held back (menu)` / `back on`, and every 10 s the counts (panel frames, moves, clicks, wheels, keys, not
delivered, the cursor, and the game's desktop cursor moves and clips kept off the desktop). `keys:` `the
game read an injected mouse event`, `desktop cursor kept from the game while it is not in the
foreground`.

## Verified

- Unit tests: `tests/features/menu` (ray and panel: hits, corners, misses, turned panels, pixels, the dot,
  the beam quad and image; the router: closed-loop moves and the timeout, clicks after the cursor settles,
  quick taps, a trigger held into a menu, back, scroll and tabs with repeat, the other hand's grip as the
  right button and its release when the menu closes, the stick click's C, the pointer changing hands,
  release and the gameplay latch when the menu closes, left-handed, NaN input) and the menu settings in
  `tests/ui_layer`.
- Offline against the exe of build 25216728: both signatures match once; the event loop's call is the only
  call of the handler through the cursor pointer.

## Live checks (rig, OpenXR-Simulator, Route S stereo, scripted input)

**The Dossier** (e1m2_battle, hand aim; runs `<workspace>\runs\20260927-153742-mr1` and
`-154841-mr2`; simulator captures in `<workspace>\tmp-vr\mr`, pointing with `tmp-vr\mr\pt.py`):
X held opened the Dossier on the map (`controllers: action dossier`, then `menu: the game shows its cursor
(a menu over the game)`, `s1.png`); a left drag panned the map and a still pointer left it still
(`s3-pan.png`, `s3b-still.png`); the stick zoomed (`s4-zoom.png`); a stick right switched to Arsenal and on
to Codex (`menu: key down 0x45`, `t1-arsenal.png`, `t2-codex.png`); clicks chose a weapon, opened the mod's
upgrade screen and switched Codex categories and the Challenges tab (`t5c.png`, `cmp3.png`, `cmp5.png`,
`cmp6.png`); B went back from the upgrade screen and closed the Dossier (`menu: key down 0x1b`, `the cursor
is gone`); with the change, the other hand's grip rotated the map both ways with 7 cursor moves in all and
no rotation after the pointer stopped (`menu: right button down` / `up`, `cmp9.png`, `cmp10.png`) and a
stick click centred it after a pan (`menu: key down 0x43`, `cmp12.png`).

Runs `<workspace>\runs\20260926-071909-m6t1` (title screen to a new game and back),
`-072718-m6t2` (e1m2: pause, resume, settings, exit), `-073459-m6t3` (with the desktop-cursor guard);
captures in `<workspace>\tmp-vr\m6` and `tmp-vr\m6t1-ui` to `m6t3-ui`; the scripted pointing is
`tmp-vr\m6\point.py` (a GUI pixel to a `right.aim`).

| # | Check | Result |
|---|---|---|
| 1 | Title screen: cursor seen, panel placed, pointer to the centre, trigger | Pass: `the game shows its cursor (a menu screen)`, one move to (1032, 1050), the click passed the title (Bethesda.net sign-in, then the main menu) |
| 2 | Main menu to Campaign, the save slots, back with B | Pass: the cursor lands on the pointed item (log and GUI capture agree to the pixel), B returns |
| 3 | Settings from the main menu and from pause: toggle a setting and back, a confirmation dialog (No), scroll with the stick, tabs with the stick, a slider by click and by drag | Pass: Weapon Bob Off and On again; Tutorials dialog answered No; the list scrolls; E switches tabs; Music Volume 100 -> 79 by a click, back to 100 by a drag. Arrow keys: no effect in the game's menus (sent with the E0 flag), so the router sends none |
| 4 | New game: a free slot, the welcome and controller pop-ups, difficulty | Pass: e1m1_intro loads, `the cursor is gone`, gameplay input back on |
| 5 | Pause (Menu tap), pointer on the pause menu | Pass: the UI quad on the world-locked panel, beam and dot drawn at the item (simulator capture `t1-pause-sim.png`) |
| 6 | Resume by clicking with the trigger held 2.5 s after the click | Pass: the menu closes on the press, no shot while held (`action fire` only after a new pull, `2 shot(s)`) |
| 7 | B at the pause root resumes; Exit to main menu and Accept | Pass in e1m2. In e1m1_intro's reduced pause menu (Load Checkpoint, Exit) neither Escape nor the Menu button closes it (the game's own behaviour there) |
| 8 | Quit from the main menu (B, Yes) | Pass: the game exits |
| 9 | Desktop cursor during all of the above | Pass after the guard: the game's `SetCursorPos` (dropped), `ClipCursor` and `RIDEV_CAPTUREMOUSE` kept off the desktop; a cursor watcher saw only the person's own movement |

**Through the launcher** (main after the merge, Release launcher, layer staged in `tmp-vr\launcher-layer`,
OpenXR-Simulator, the launcher's own defaults: no `+map`, the title screen): title, Campaign, the save slot,
Continue Game (e1m2 loads), a shot, pause with the Menu button, Resume clicked with the trigger held (no
shot until a new pull), pause again, Exit to Main Menu and Accept, settings (Weapon Bob off and on
again), back, Quit and Yes: all by scripted controllers; the log counts 14 moves, 9 clicks and 15 of the
game's desktop cursor moves kept off the desktop, and no failed frame. Evidence
`<workspace>\tmp-launcher\m6\L-menu` (GUI captures, `l-pause-sim.png`, layer-logs).

The simulator's preview shows nothing for frames without a projection layer, so the title screen and the
main menu on the panel are seen in the GUI captures and the log (the panel's placement and layers), not in
the preview.

## Gaps

- Headset: panel size and distance, pointer comfort and precision, beam look, dot size (U4-style sweep).
- The panel is placed when the cursor appears and stays; it does not follow a player who turns away.
- A flat, not curved, panel (cylinder layers are M6's next step).
- Mono without the UI layer: a menu over a head-tracked frame has no panel, so no pointer.
- The launcher's settings restore leaves `r_windowPosX` / `r_windowPosY` in `DOOMEternalConfig.local` (the
  game saves the window position the layer set); after the live run they were put back by hand. Fixed in the
  launcher: the window and display keys (`launcher/data/session-keys.txt`) are now restored like the forced
  keys; not yet seen on the rig.
- Menus the game navigates only with a pad (if any) would need the virtual gamepad's D-pad; none was found.
- The Arsenal's key-only shortcuts (T customize weapon wheel, X equip / unequip mod) have no controller
  input; the Praetor suit screen is untested.
- The Dossier map's 3D view is the game's own head-tracked view, not on the panel: it sits around the
  player and does not follow the panel.
