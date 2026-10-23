# Menus in VR: a panel and a laser pointer (M6)

Builds on the UI layer (`docs/rig-findings/ui-layer.md`) and the motion controllers
(`docs/VR_CONTROLLERS.md`). While the game shows its menu cursor, the menu is on a world-locked panel in
front of the player, a laser from the pointing hand meets the panel, and the controllers drive the game's
own mouse cursor. No keyboard or mouse is needed from the title screen to gameplay and back. The engine
facts are in `docs/rig-findings/menus.md` (build 25216728).

**Status.** Built, unit-tested and checked live on the rig with OpenXR-Simulator and scripted controller
input (the live checks below). On by default wherever the UI layer is on (the launcher turns it on in
stereo and in mono) and on the cinema path; `ETERNALVR_MENU_POINTER=0` turns it off. Not yet tried on a
real headset.

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
  removes the backdrop, which ends the hold within a few frames. The same hold keeps the whole frame on the
  panel when no frame is head-tracked yet (the end of a loading screen, the main menu changing screens);
  before, that frame jumped to the cinema screen for up to a second.
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
- **What the game took in front is given back when it loses the front** (GitHub issue 30,
  `platform/key_injection/cursor_hold.hpp`, `vkcore/cursor_guard.cpp`). A click into the game's window
  brings it in front, and the game takes the mouse on activation (`docs/rig-findings/menus.md` section 1:
  raw mouse 0x230, `ClipCursor` to the window). It never sees the deactivation that would make it let go,
  so up to 0.1.50 the cursor stayed in its window until the game quit. The window procedure the layer
  already puts in front of the game's (`keep_active.cpp`) sees that deactivation (`WM_ACTIVATEAPP` false,
  or `WM_ACTIVATE` inactive for a window the game does not own) on the window's own thread, and there gives
  back the clip (only while the clip in force is still the game's), the window's mouse capture
  (`ReleaseCapture`, which only the capturing thread can do) and the raw mouse's `RIDEV_CAPTUREMOUSE`
  (registered again with the game's other flags). No thread, hook or polling is added. The calls are made
  with the book's lock released (`ReleaseCapture` sends `WM_CAPTURECHANGED` to the game's window procedure at
  once, and a `ClipCursor` there comes back into the guard). When the window is
  in front again (`WM_ACTIVATEAPP`, `WM_ACTIVATE`, `WM_SETFOCUS` or `WM_SIZE` restored, after the game
  handled it; never while the window is minimized, whose client area is off the screen) whatever the
  game did not take again itself goes back: the clip moved with the window, the raw mouse with its flags,
  and also a clip or capture the guard kept off the desktop meanwhile. Logged per change (the first 20,
  then every 50th): `keys: the game's window lost the foreground; given back: cursor clip yes, mouse
  capture -, raw mouse capture yes (release 1)` and `keys: the game's window is in front again; put back:
  ...`.
- **The view while a menu is up.** Head aim writes nothing while a menu or popup is up over the game, and
  the body keeps the yaw it had when the menu came up (`aim: a menu is up: head aim holds the body yaw at
  ...`), so the world stays put while the head looks around. Before, the body was worked out every frame
  from the game's angles, which the game rewrites every frame while a popup holds its view (about 94
  rewrites a second in the owner's Objective Marker popup, 2026-09-27, with the logged body yaw moving from
  95.0 to 112.6 degrees while the game's view stayed at 80.3): the owner saw the view turn the wrong way when
  he looked around in the popup. When the game renders a camera of its own under the menu instead of the
  player's view (its forward more than 15 degrees from the player's view angles: the Fortress's collectible
  viewer below), the body takes the camera's heading without the head yaw the game's yaw holds, as for a
  scripted camera with no menu (`xr_math::menuBodyYaw`): the view faces what the camera shows while the head
  is where it was when the menu came up, and turns with the head from there. The log says `aim: under the
  menu the view faces the camera the game drives (body yaw ..., held ...; game view yaw ...)` and `... is the
  player's own again`.
- **Gameplay held back.** While the menu is up, the controllers' gameplay actions, movement and turning are
  not sent (only the pause key, so the Menu button still opens and closes the pause menu). After the menu
  closes they stay held back until every trigger, grip, A, B, X, Y and stick click is let go (the router's
  latch). Then each input still down, or let go on that very frame, is kept out of gameplay on its own
  until it is let go (`features/input/menu_release_latch.hpp`): the mapper reads it as released, or a stick
  as centred, and the presses and stick sweeps begun in the menu are used up, along with any tap the
  mapper's minimum hold was still sending. So the pull that clicked Resume does not also fire the gun, the
  B or Y that backed out does not switch the weapon mod on its release, and a stick held to scroll or to
  pan the Dossier's map does not become the chainsaw, the weapon wheel or a quick switch. An input pressed
  after the menu works at once. Before, the release that ended the latch was also the release that
  completed a tap binding: two Index players' logs (0.1.6 and 0.1.11) showed `gameplay input back on` and
  `action switch_weapon_mod` in the same millisecond every time B closed the pause menu.

## Controls while a menu is up

| Input | Does |
|---|---|
| Aim (either hand; the dominant one first) | Moves the game's cursor to where the beam meets the panel |
| Trigger, or A / X, of the pointing hand | Left click (hold to drag: sliders, scroll bars) |
| Other hand's trigger or A / X, pointed at the panel | Takes the pointer, then clicks |
| B or Y, either hand | Back (Escape): closes dialogs and sub-screens; at the pause menu's root it resumes, at the main menu's root it asks to quit |
| Left grip / right grip | Previous / next tab (Q / E) in tabbed screens (settings, the Dossier) |
| Either stick up / down | Scroll (the mouse wheel), repeating while held (the Dossier map: below) |
| Either stick left / right | Previous / next tab (Q / E) (the Dossier map: below); on a screen with pages or tabs of its own inside a tab, its previous / next (A / D: below) |
| Either stick click | C, the Dossier map's centre key (E in a popup, below); on a screen with pages or tabs inside a tab, the left stick's the previous (A), the right stick's the next (D); none for the second stick of the recenter chord |
| A / X in a tutorial or lore popup | Space, the popup's continue key (below); so does the jump button, wherever it is |
| Y in a tutorial or lore popup | Left Alt, held while Y is: the objectives key (below) |
| A gameplay button in a tutorial or lore popup | Its action's default key, held while the button is (the Flame Belch button R, and so on; below) |
| Left Menu (tap) | Pause / resume, as in gameplay |
| Left X / Y, right stick click, while the hint bar shows F, X or T | The hint's key (the Arsenal's customize weapon, equip / unequip mod, customize weapon wheel: below), instead of the click, Back or the stick click's key |

The grips were Back (the pointing hand) and the right mouse button (the other hand) until the owner's Quest
3 session of 2026-09-27 asked for the tabs on the grips; the Dossier map's rotation moved to a stick.

The game's menus do not move their focus with the arrow keys, so none are sent; sliders are set by
clicking or dragging on them.

The menus' hint bars and tab lists name these controls instead of keys: "[B] BACK" for Escape, the
weapon hand's trigger for Enter's select (also on a hold button: "Right Trigger HOLD TO CONTINUE" at
the end of a mission, held with the pointer on it), the grips for the tabs' Q and E as LG and RG (the tab
lists cut longer names; `docs/VR_CONTROLLERS.md`, Button prompts).

**Pages and tabs inside a tab.** Some screens have a second level of previous / next inside one of their
tabs, on A and D: the mission-complete screen's Rewards tab turns its pages ("A 1/2 D" under the rewards),
and the Dossier's Arsenal, Codex and Challenges pages switch their inner tabs (the weapons, the categories,
the challenge lists). A pad does those with its triggers (`JOY_TRIGGER1` / `2`, keys 0x118 / 0x119) and the
tabs with its shoulders. No controller input sent A or D there: on the Rewards tab only pointing at an arrow
and pulling the trigger turned a page (rig, 2026-10-17). While such a screen is up, a stick pushed left or
right (either hand) sends A and D instead of the tab keys, the left stick's click A and the right stick's D,
and the grips keep changing the tabs, as the pad's shoulders do. The screens name the sticks, "LS 1/2 RS"
under the rewards and "LS" / "RS" beside the inner tabs: they have room for about two letters where the
game writes A and D ("RS Left" showed as "..."), and "LS" by the left arrow holds for a push to the left
and a click alike; a wand's trackpads read "LP" / "RP", not "LT" / "RT", which are its triggers. When the
tab lists' update cannot be hooked, their A and D keep the game's text, since no stick sends them then
(`features/menu/page_keys.hpp`, `vkcore/prompt_page_keys.hpp`). A screen counts as up
while the game runs its update: the Rewards tab's own update with its arrows up (with one page the game
hides them, and the sticks keep the tabs), or the update of a visible tab list on A / D with two tabs or
more, the last within 0.25 s. The Rewards tab's update runs only while that tab shows (rig: first run
15 ms after the tab key that opened it, gone 0.27 s after the left grip left it). Pointing at an arrow and
pulling the trigger still works.

Rig, OpenXR simulator, e1m2's level exit (2026-10-17, a release build): the Rewards tab read "LS 1/2 RS";
a right stick right went to page 2, the left stick's click back to 1, the right stick's click to 2 and the
trigger on the left arrow back to 1; on the Summary tab a stick right still sent E (Combat), and after the
left grip went to Exploration a stick right sent E back to Rewards. In the Dossier the Arsenal's and the
Codex's inner tab lists read "LS" / "RS"; a stick right, a stick left and the right stick's click each
changed the weapon, and a stick right the Codex category (`menu: page keys up (a tab list on A / D)`,
`menu: key down 0x44`). The Challenges page sets the same keys (offline); it was not opened. Not tried in a
headset.

The loading screen's tips bar ("[Q] PREVIOUS | [E] NEXT") is not a menu: the game shows no cursor there
and reads Q and E itself on every frame of the loading screen. While it does and no menu is up, the left
grip sends Q (the previous tip) and the right grip E (the next), and the bar reads "[LG] PREVIOUS | [RG]
NEXT". A grip already held when the loading screen comes up (a weapon mod held through the load) counts
once it has been let go, and the grips stop at the game's first view of play
(`features/menu/load_tips_keys.hpp`, `vkcore/prompt_load_tips.hpp`).

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

**Popups without a head-tracked frame behind them.** A prompt the game raises right after a cinematic or a
load pauses the game before its first head-tracked frame, so the frame behind the cursor is still the
cinematic's or the loading screen's. Decided by the frame alone such a cursor was a menu screen: A clicked
and Space was never sent (a player's 0.1.31 log: 58 s of clicks, Escapes, C, E, Q and wheel at a prompt in
`e1m1_intro`, `menu: the game shows its cursor (a menu screen)`, never `key down 0x20`). Two more players
reported a prompt no button closed, before the TAG1 boss and after Deag Ranak in the Doom Hunter Base;
there are no logs of those sessions, so they are possibly the same class. In a play map (any single-player
map but the main menu's shell, `mp_policy::isPlayMap`, from the map-load hook) a cursor nobody asked for is
now a popup whatever is behind it, and a screen nobody asked for becomes one there (`menu: the screen is up
in a play map and nobody asked for it: a popup ...`).

The game loads a map while the menu that asked for the load still shows its cursor (156 of 157 loads in
the players' logs on hand): the cursor goes only afterwards. So each chain of screens (`menu::MenuChain`)
keeps the map loads counted when it began, and a cursor that comes up after a load begins a new chain,
whether it comes back within 0.75 s or onto the panel held for the menu's backdrop (`menu: a map load came
since the last menu began: this cursor begins a new one`). After Pause, Exit to Main Menu and Continue (or
Load Checkpoint), a prompt right after the load is decided afresh, a popup, instead of keeping the asked-for
pause menu's screen; a popup's chain carried into the main menu ends there, and the main menu is a screen.
Once a load has come, the chain also stops counting as asked for while its cursor stays up.

Outside a play map little changes: the title screen and the main menu click with A. One exception: a main
menu nobody asked for becomes a popup when it loads a play map (Continue, a mission) and stays one until
its cursor goes (A sends Space there during the load, which has nothing left to click). The clickable
screens the game raises by itself in a play map (the Fortress's and Horde's among them) were popups already
in the players' logs on hand: of 252 cursors nobody asked for in play maps and decided afresh, 250 were
popups at once and one end-of-level screen turned popup 0.28 s later, closed with the trigger's clicks or
with Space; the 252nd is the stuck prompt. The trigger still clicks in a popup. Sending a click as well as
Space from A was not chosen: the layer cannot see whether a screen has a focused button, and the prompt
hooks see a prompt's text when the game builds it (it is cached), not which prompt is on screen.

Each cursor nobody asked for in a play map logs the foreground window, since the desktop's keyboard reaches
the game only while its window is in front (`menu: nobody asked for this cursor (a play map, no
head-tracked frame behind it); in front: the game's window`, or the window's class and process, never its
title); a cursor that continues a menu does not log it. A cursor nobody asked for in a play map that stays a
screen, because it continues a menu that was asked for (the pause menu's Settings), logs `menu: a cursor
nobody asked for in a play map is taken for a screen of the menu asked for before (A / X clicks, no
Space)`, at most 10 times.

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
sticks are not actions), for pause and the automap (they open screens of their own), nor for melee and
mission information: their keys (E, Left Alt) are the popup's own on a stick click and Y in every control
map, and sending them from the action too would press them twice for one button. Jump is sent although A /
X already sends Space: the popup's "[A] TO DISMISS" names the jump button, and on the Vive wand (the left
trackpad click) or in a changed map that is not A / X. A / X's Space and the jump action's from one press,
at most 0.3 s apart (the mapper publishes the action a game frame or two after the router sees the
button), are one Space; the first press of jump from another button logs `menu: popup: jump sends Space
(0x20)`. The hands-up jump gesture sends it too. Buttons that are both keep both: B also sends Escape, the
left grip also Q (its tab key), X Space before switch equipment's G. The player's own key binds are not
read: a player who rebound the Flame Belch still has R sent.

## The Dossier, its map and the upgrade screens

Holding X (`_inventory`) opens the Dossier on its Map page, and the game shows its cursor there, so menu
mode, the panel and the pointer come up as for the pause menu (`the game shows its cursor (a menu over the
game)`). The map is drawn in 3D in the game's own view (head-tracked, around the player), with the
Dossier's GUI on the panel. The flat game's hints on the map are: left drag pans, right drag rotates, the
wheel zooms, C centres, Escape goes back; the game also has pad and keyboard pan speeds
(`automap_panSpeedController`, `automap_panSpeedKeyboard`) but no keyboard rotation. From the controllers:

| Screen | Works | How |
|---|---|---|
| Tabs (Map, Arsenal, Codex, Challenges) | Yes | click the tab, or the grips (left Q, right E); a stick left / right on a page without inner tabs |
| Map: pan | Yes | the weapon hand's stick (a left drag held, or W A S D while the other stick rotates, below; the other hand's with `ETERNALVR_MAP_STICKS=other`), or the trigger held while the pointer moves |
| Map: zoom | Yes | the other stick up / down (the wheel, a notch every 0.1 s) |
| Map: rotate | Yes | the other stick left / right (a right drag held, below), at the same time as the pan |
| Map: centre | Yes | a stick click (C) |

**The first input after it opens.** The Dossier opens from the controllers' action (the game takes it as the
pad's), and the game then ignores the first key or mouse input it gets, which only switches it to the
keyboard and mouse. Rig, OpenXR simulator, e1m2 (2026-10-18): with the pointer still and its ray off the panel,
the first tab key on the map page never turned the page, whether E or Q, 60 to 300 ms long and 1.5 to 6.6 s
after opening; a second key did, at 60 ms; a key the Dossier does not use (K, UP), a map drag or three cursor
moves first made the next E work. The menu router's page count then ran one ahead of the game. So a menu's
first frame with the cursor known moves the cursor once, a pixel aside when the ray misses the panel (a ray on
the panel moves it to the ray anyway; `features/menu/menu_router.hpp`, `wakePending_`). Rig, the same setup with
that build: the right grip as the first input turned Map to Arsenal (`1 move(s)` in the menu line, the cursor
from x 57 to 58), and so did a single 60 ms E.

**The map from the sticks** (`features/menu/map_drag.hpp`). The owner found moving the map with the hand
disorienting (2026-09-27). The game pans with a left drag and rotates with a right drag (its pad's sticks
would do both; no pad is used, it would make the pad the game's input device), so a held stick becomes a drag. The
automap adds up the raw mouse motion that comes while the button is down, not the cursor's position
(`docs/rig-findings/menus.md` section 6), so the motion still counts with the cursor held at the edge of its
range: the cursor goes to the middle of the screen with the buttons up, the button goes down, and it stays
down, the stick's motion streamed every frame (pan: 1000 counts a second at full deflection, the speed of
the game's own W A S D pan; the drag follows the stick, so the map moves the way the stick is pushed;
rotate: 1100 counts a second, about 110 degrees), until the stick is back near its centre (0.15; a drag
starts past 0.2, a rotation past 0.3 left or right). The first build drew short strokes from the middle
instead (release, back to the middle, press again, about three a second at full deflection), which the
owner felt as the map stopping and starting (2026-10-02).

With both buttons held every motion pans, so one drag cannot pan and rotate at once (the owner could not,
2026-10-02). While the other stick rotates, the pan stick pans with W, A, S and D, which the automap reads
on its own (as it reads C), and the right button rotates: both sticks work together. The keys are on or off
and the game evens out a pair, so a stick held part of the way or between two directions is spread over the
frames (`features/menu/map_pan_keys.hpp`: each frame holds the key pair that keeps the sum held closest to
the stick's). A pan that went onto the keys stays there until its stick is let go; a pan begun alone is a
drag again. Every press is made with the cursor in the middle of the screen, as before. While a stick moves
the map the drag owns the cursor and the ray does not move it, and the cursor is hidden; with the sticks at
rest the ray has it again, so the map's buttons can still be pointed at and clicked. A drag does not start
while the trigger holds the left button. The log has each change (`menu: map sticks: the pan stick pans
with the left button held, the other stick does not rotate`, `... pans with W A S D, the other stick
rotates with the right button held`; the first 60). The pan keys are counted in the menu's key count, not
logged one by one.

**Which stick pans.** `ETERNALVR_MAP_STICKS=other` swaps the two sticks' roles on the map: the other
hand's stick pans and the weapon hand's stick zooms and rotates (`features/input/map_sticks.hpp`; the
router swaps them in `MenuRouter::mapSticks`, for either weapon hand). The sticks are not in the control
maps, so the controls files cannot do this. The launcher's Play tab sets it (Controls, "Dossier map
sticks"; `map_sticks` in `launcher.ini`). The choice is in the `controllers: on:` start-up line (`Dossier
map panned by the weapon hand's stick`, or `by the other stick`).

**Which page is up.** The game does not say. The router takes a menu that comes up within 1.5 s of the
controllers' Dossier (or automap) action to be the Dossier on its map page (`controllers::
dossierRequestedWithin`; logged `the game shows its cursor (the Dossier, on its map: ...)`), then counts
the tab keys it sends (Map, Arsenal, Codex, Challenges). A tab key past either end is not sent (a grip or a
stick at the map going back, or at Challenges going on, does nothing): whether the game wraps there or stops
is not known, and either way the count stays right. Up to 0.1.50 such a key made the page unknown, which
turned the map's sticks off until the Dossier was opened again (Jason's 0.1.50 playtest: the sticks changed
tabs on the map). The same key kept back again within 1 s is sent after all and makes the page unknown
(the count can be wrong: a key the game ignored, a Dossier that opened elsewhere than its map), so the end
tab is always reachable; logged `menu: Dossier page map -> not known (Q asked again at the end: sent)`. The
page is kept while the headset stops showing frames with the menu up (`releaseHeld` lets go of held input
only) and is dropped when the menu closes (`... -> not known (closed)`). A click on the tab strip (the top 8% of the panel) still makes the page unknown (which tab
it opens is not known), and a popup never counts. Every page change is logged (`menu: Dossier page map ->
arsenal (E)`, `... (opened)`, `... (a tab clicked)`; a key kept back: `menu: Dossier page map: Q not sent at
the first tab`, the first 20), as is the sticks' mode (`menu: the Dossier's map page: ...` / `not the
Dossier's map page (or not known): ...`). The game's automap object has an `isActive` flag
(`docs/rig-findings/menus.md` section 6) that could tell the map page directly; it is not read yet.
| Arsenal: weapon choice | Yes | click the weapon's icon, or a stick left / right or a stick click (A / D, the page's inner tabs: above) |
| Arsenal: a weapon's upgrade screen (Upgrades, Mastery) | Yes | click the mod; B returns to the Arsenal |
| Arsenal: buy an upgrade | Clicks land on it; not seen bought (no weapon points in the save) | click |
| Arsenal: customize weapon (F), equip / unequip mod (X), customize weapon wheel (T) | Yes | the hint bar names X, Y and Right Stick Click (on Touch) and those buttons send F, X and T while it shows them (below); the bar itself takes no clicks |
| Codex: categories, entries | Yes | click the category icon or entry; a stick up / down scrolls |
| Challenges: Mission / Weekly | Yes | click |
| Close | Yes | B (Escape) at any tab |

The Praetor suit screen did not appear in the Arsenal of the rig's e1m2 save (only the two weapons owned
there), so it is untested. Holding Y (`_objectives`) sends the action (`controllers: action mission_info`)
but showed no overlay in e1m2 and no cursor, so it needs no menu handling; what the game shows for it is
still to be seen on a headset.

**The hint bar's shortcuts** (`features/menu/hint_keys.hpp`, `vkcore/prompt_hint_keys.hpp`). The bar under a
menu ("[B] BACK", "[F] CUSTOMIZE WEAPON") puts each shortcut in a slot that is also the pad button which
presses it on a pad (the game's JOY number less one: 2 is X, 3 Y, 7 the right stick's click), and on the PC
names its keyboard key. The bar takes no clicks, and the Arsenal's F (customize weapon, slot 2), X (equip or
unequip the mod, slot 3) and T (customize weapon wheel, slot 7) had no controller button. While a visible bar
button shows one of them (its per-frame update ran within 0.25 s) and a menu that is not a popup is up, the
button in the pad's place sends the key: the left controller's primary (X on Touch) F, its secondary (Y) X,
the right stick's click T, a key down on the press and up on the release; the router does not see those
buttons meanwhile (the trigger still clicks, B still goes back, a stick pushed left or right or the left
stick's click still turns the weapon row). The bar names the button as it names the others: `[X] CUSTOMIZE
WEAPON`, `[Right Stick Click] CUSTOMIZE WEAPON WHEEL`, `[Y] EQUIP MOD` (Index `Left A` / `Left B`). The same
keys in the same slots elsewhere get the same buttons: the weapon skins' `[X] CLEAR SKIN`, the weapon wheel
screen's `[X] CHANGE SIZE` (its R, reset to default, stays a key). Vive wands have no left primary or
secondary button, so customize weapon and the mod keep F and X there. On the rig (2026-10-18, e1m2 on the
simulator): Y turned UNEQUIP MOD into EQUIP MOD, X opened the weapon skins, the right stick's click the
custom weapon wheel, B came back from each; logged `prompts: the hint bar names F (slot 2): [X]` and `menu:
hint bar: X down` / `up`, `... shown` / `gone`.

**The Fortress's collectible viewer** (the toy tables; `vkcore/prompt_viewer_keys.hpp`) shows its cursor, so
the router takes it for a popup; the grips send Q and E there as everywhere, which turn its items. Its
previous / next keys read `LG` / `RG` (the game wrote Q / E). The album posters open the same viewer with one
item and no previous / next. The log has the item shown each time it changes (`prompts: the Fortress viewer
shows item 0x...`, its title's text id).

The viewer (`idInteractable_CollectibleViewer`, the GUI `idHUDMenu_CollectibleViewer`) shows its models in
the 3D world through a camera of its own (`idCollectibleCamera_Orbit`, "Camera for viewing collectibles"):
first at the shelf, then, once an item is chosen, at an external scene where the item's model is placed
(`externalScene`, `externalSceneModel`), where the camera orbits, pans and zooms around it. Measured on the
rig (build 25216728, eye captures): the shelf camera looks the player's way, 17 degrees down; the toys'
close-up camera and the album posters' camera look 90 degrees to the side of the player's heading. While
the view kept the body yaw held for the menu, the close-up showed an empty floor (the toy 90 degrees to the
side) and the album viewer a corridor; the view now faces the viewer's camera (`The view while a menu is
up` above).

**The fast-travel notice with a Tab key** (`idHUD_Notification_FastTravel`, its key text at 0xF1355E in
build 25216728) is not the campaign's: a mission's fast travel unlock shows "FAST TRAVEL UNLOCKED / Use in the
AutoMap" with no key (rig, e1m2, the unlock target fired), and the notice's update never ran there nor at the
map's fast travel points. The only code found that raises it (its HUD event, 0x29, as a constant) is a
demon card's fast travel payload (`idDemonCard::Use`), a demon player's; its Tab is left as the game writes
it.

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

**Where the wheel shows** (`ui_layer/weapon_wheel.hpp`, `vkcore/presenter_wrist.cpp`). The game draws the wheel
right of the middle of the HUD: its centre at 79.7% of the 16:9 band's width and 48% of its height, 28% of
the band's width across (the rig's GUI captures at 2048x2208, 1760x1400 and 1440x1600, the same band
coordinates to 0.001; at 2048x864, wider than 16:9, the same to 0.002 in the centred 1536 x 864 band the game
fits its frame into). On the head-locked UI quad that is about 22 degrees right of straight ahead on the
default HUD (2.0 m wide at 1.5 m) and about 40 degrees on a 2.5 m HUD at 0.9 m, at the edge of the view or
past it (a player's report on a Pimax Dream Air). While the wheel is up, its rectangle of the GUI image goes
on a quad of its own straight ahead at the HUD's distance, at the angle it spans on the default HUD (23
degrees on the default layout), in every HUD mode (panel, wrist, weapon) and at any HUD width or distance; the
head-locked quad keeps the rest of the band, cut into rectangles around it as for the blocks the hand HUDs
move, so the wheel is not shown twice and nothing else moves.

The rectangle is where the game drew the wheel, measured from the GUI image (`ui_layer/wheel_bounds.hpp`,
`vkcore/wheel_probe.hpp`), not a fixed one: the game's layout of the wheel is not fixed. A player's 1736x1896
capture had it 17% larger and further in (band x 0.604..0.935, y 0.182..0.769), with the corner blocks larger
too; up to 0.1.73 the fixed rectangle (x 0.64..0.95, y 0.20..0.75) left the wheel's left edge, top and bottom
on the head-locked HUD at the game's wheel position, beside the centred quad. While the wheel is up, each GUI
copy also copies every 4th row of the band's right part (from 0.45 of its width) into a host buffer (cached
host memory; a GPU without it leaves the wheel where the game draws it, with a log line). Once the copy is
done the layer takes the connected drawn shape (alpha 16 or more on a 4-pixel grid) that crosses the band's
row 0.48 right of the middle, is clear of the band's top and bottom and has a wheel's size and shape. A shape
more than 6% wider than tall is the wheel with something touching it on the left (the crosshair's rings end
about 0.012 of the band's width left of the larger wheel above): it is cut to its right part, as wide as it
is tall. Shapes lying wholly within 2.5% of the ring's height around it are added (a part of the wheel apart
from the ring; on the rig's 79 wheel captures, highlighted segments included, and the player's, no drawn shape
other than the ring lay within 8% of it, and the nearest other HUD element, the ammo block's prompts, is 5.9%
below it). The bounding box is grown by the grid's step and 1% of the band's width. Within one time up the
rectangle only grows (a highlighted segment's bar); the next time up starts from it, so the wheel is centred
from its first frame. That rectangle is kept while the new measurements agree with it within 5% of the
wheel's width on every edge; measurements reaching outside it, or smaller for 15 readings, replace it (the HUD
laid out anew), and a wheel still opening never shrinks it. A GUI target resized while the wheel is up drops
the rectangle. Before the first measurement of a session (one or two game frames) the wheel stays where the
game draws it. The readback runs only while the wheel is up: every frame until three readings in a row find
the same rectangle, then every 8th frame (a reading that differs goes back to every frame). With the larger
HUD on the rig (Release) that took the CPU spent measuring from 255 to 37 us a frame on average at 1280x720
and from 435 to 59 at 1736x1896. The measuring runs on the readback's own thread, not on the present hook,
which only records the copy and hands the done reading over (docs/rig-findings/gui-readback.md): on the rig at
2048x2208 with the wheel open, 8 us a frame on average on the present hook and 67 to 138 us at most (46 us of
it recording the copy), against 95 us and 1.0 to 1.1 ms when the present hook measured (up to 0.1.77); a
reading is published 0.65 ms after its copy is seen done. The log's `weapon wheel bounds:` lines (when the
wheel goes down, and every 600 readings) give the measuring, present-hook and GPU time and the latency. Nothing here reads game memory, so it is
the same on Steam and Game Pass. The wrist and weapon HUDs' corner blocks are measured by the same readback
while the wheel is down (docs/VR_HANDS_HUD.md, "Where the corner blocks are"). An in-headset capture's `-HUD.png` draws the quads of the last headset frame
(at most 0.25 s before the capture) over the GUI image saved with it, a frame or two later.

The band is `ui_layer::wideContentRect`: the centred rows of a target taller than 16:9, the centred
columns of a wider one. Up to 0.1.70 a wider target was taken whole, so the rectangle started right of the
wheel's left edge (at 2.37:1 about 11% of the wheel, more on wider images): that slice stayed on the
head-locked quad at the wheel's place on the HUD, just right of the centre, and showed through the centred
wheel's right side, the shape of a player's report of a "sliced" wheel. A map marker the game draws inside
that rectangle while the wheel is up moves with it. Selection is unchanged: the stick, the thumb rest or the
hand move the game's cursor around the wheel's centre in the GUI image, wherever the quad shows it.

"The wheel is up" is the forced-view gate's weapon wheel state (`features/input/forced_angles.hpp`: the
game's view and button inhibit bits, 0x18, set while `weapon_wheel` is held), read at the camera hook after
the gate's update and kept with that frame's view record (`ViewRecord::weaponWheel`). The quad follows the
frame of the GUI image the headset shows, not the game's latest frame, and stays 0.1 s after the last image
with the wheel up (`WheelCentring::kHoldSeconds`). The gate needs the controllers: with
`ETERNALVR_CONTROLLERS=0` the wheel stays where the game draws it. The gate's inputs are the camera hook, the
player's vtable (per build, `game_build.cpp`, both builds) and `idPlayer::inhibitFlags` (+0x87FC, the same
type layout on the Game Pass build), so nothing here is Steam-only. A frame then has up to 15 layers with the
wrist HUD (OpenXR guarantees 16): with the wheel and both corner blocks cut at the Large HUD Scale the
head-locked quad is up to 7 pieces. Rig QA: `wheel-centre` (`tools/rig/qa/README.md`) checks that the wheel the
game drew lies in the logged rectangle.

## The menu's 3D model

Some screens show a 3D model next to their menu: the weapon in the weapon mod screen ("Choose a weapon
mod") and in the Dossier's Arsenal, Customize weapon. The game does not draw it in the GUI image. Every
tick it places the model in the world a few centimetres in front of its camera (r_znear deep, pushed back
by the model's size), along the line through the spot of the screen the menu keeps for it, and draws it in
the 3D view under the GUI (the world is dark then, only menu models show). In VR the game's camera is the
head, so the model stayed in front of the eyes, in the same spot of the eye images however the head moved,
while the menu was on the world-locked panel (rig, 2026-10-02, eye captures under the test sway).

Now, while the panel shows the menu over a head-tracked frame (pause and the in-game screens, not the
title screen and the main menu, whose whole frame is on the panel already), the model is placed from a
camera at the panel instead (`vkcore/menu_model_hook.hpp`, the maths in `features/menu/model_camera.hpp`):

- **The camera.** It faces the panel from the point where the whole GUI image, as the panel shows it, fills
  the field of view the game asked for (its own, before the headset's: 90 degrees across in the menus on
  the rig, so a 2 m panel is seen from 1 m in front of it). Its height follows from the image's shape, so
  the 16:9 band on the panel gets the flat screen's 58.7 degrees. The panel is in the headset's space and
  the camera goes into the game's world through the head of the latest game view (where it is in both), so
  it stays put in the world while the head moves.
- **On the panel.** The model, placed by the game from that camera, is then made bigger about the camera
  until its centre is on the panel's plane (its scale with it, about ten times for a model 10 cm in front
  of a camera 1 m from the panel). Seen from the camera nothing changes; seen from anywhere else it is on
  the panel where the flat menu puts it, at the size it has there next to the menu, with no parallax
  between the two eyes and the panel.
- **When.** Only while the multiplayer guard allows game touches, the frame is head-tracked and the panel
  shows the UI quad (the pointer's panel or its hold), and only with a game view and a panel less than
  0.25 s old. Otherwise the game places the model from its own view as before.
- **How.** Two hooks in the game's `idMenuWidget_3D_Stand::UpdatePosition` (RVA 0x15A6D30 in build
  25216728), each checked at start-up against the expected instructions. The first, right after the game
  asked the world for its render view, hands the function a copy of that view (the whole idRenderView,
  0x29950 bytes, kept per game thread) with the panel camera's place, direction and field of view; the game's
  own view is not changed. The second, after the function wrote the model's position, moves the position
  and the scale onto the panel.
- **Its lights.** The weapon in the weapon mod and customize screens is an entity lit by its own light rig
  (three point lights for the combat shotgun), which the game places every tick at fixed offsets from the
  model with a fixed size. Magnified about twelve times, the model outgrew its lights: it came out about a
  third darker, and parts away from them black (the far end of a weapon seen end-on). Two more hooks around
  the widget tick's light rig update (RVA 0x15A83FD and 0x15A840A) grow the rig with the model for that
  update and put the offsets back after it (`features/menu/model_camera.hpp`, `rigFactor`, `grownRigLight`):
  - each point light's offset, radius, centre and visible ranges grow by the rig's factor, which is the
    model's, but no more than keeps every light within 5 units of the model (lights further out light nothing
    of it, measured on the rig; the engine limit behind that is not known): about 4.3 for the shotgun;
  - its intensity grows by that factor squared (the light falls off with the square of the distance); a rig
    capped like that has its lights nearer the model, for its size, than on the flat screen, so they are
    dimmed by the rig's factor over the model's, to no less than half (half for the shotgun);
  - its shadows are off while it is grown (grown, the lights shadow the model onto itself, which on the flat
    screen they never do);
  - spot, parallel and area lights are left as they are (none in the rigs seen).

  Measured on the rig (mean brightness of the shotgun in the customize screen, flat screen 84): 60 before,
  98 grown, with no part left black. A rig grown before gets its lights back as the engine has them (from each
  light's own values) once its model is not magnified. The log says `the first light rig grown with its model` with both factors and each
  light's type, offset, radius and intensity. The Codex's models light themselves from their joints and are
  not changed.

`ETERNALVR_MENU_MODEL_PANEL=0` leaves the model where the game puts it (in front of the head);
`ETERNALVR_MENU_MODEL_PANEL=near` places it from the panel camera but leaves it a few centimetres in front of
that camera instead of on the panel (for comparing on the rig).

## Settings

| Variable | Values | Default |
|---|---|---|
| `ETERNALVR_MENU_POINTER` | `1` / `0` | `1` |
| `ETERNALVR_MENU_DISTANCE` | metres, 0.3 to 10 | `ETERNALVR_UI_DISTANCE` (1.5) |
| `ETERNALVR_MENU_WIDTH` | metres, 0.1 to 10 | `ETERNALVR_UI_WIDTH` (2.0) |
| `ETERNALVR_MENU_BEAM` | `1` / `0` | `1` |
| `ETERNALVR_MENU_FOLLOW` | `1` / `0` | `1` |
| `ETERNALVR_MENU_MODEL_PANEL` | `1` (a menu's 3D model on the panel) / `near` (placed from the panel camera, left in front of it) / `0` (in front of the head, as the game places it) | `1` |
| `ETERNALVR_MAP_STICKS` | `weapon` (the weapon hand's stick pans the Dossier's map) / `other` (the other hand's stick pans) | `weapon` |
| `ETERNALVR_MAP_CURSOR_HIDE` | `1` (the game's cursor is hidden while a stick drags the Dossier's map and until it is back on the ray; `features/menu/drag_cursor_hide.hpp`, `docs/rig-findings/menus.md` section 5) / `0` (shown) | `1` |

## Log lines

`prompts:` for the tips bar, at start-up `loading screen tips: the bar's update at RVA 0x456BF0`, `key poll
hooked at RVA 0x45824B`, `platform compare hooked at RVA 0x45869D (field +0x544)` and `format calls at RVA
0x4587E1, 0x4589A5`, then once each `the loading screen's tips bar polls its keys: ...` and `the loading
screen's tips bar names Q: LG`, each grip's key (`loading screen tips: Q down`, the first 20), and once a load
`the game's first view N s after the bar's last poll; the grips send nothing from then on`.

`prompts:` for the pages and tabs inside a tab, at start-up `tab lists: update hooked at RVA 0x1591E8D (keys
+0x20C / +0x210, tab count +0xA8)` and `Rewards tab: update hooked at RVA 0xF5F170; paginator update at RVA
0xF5FBB0 (arrows' flag +0x141), its key texts at 0xF5FEC4, 0xF5FFBE`, then the first time each tab list
shows (`a tab list shows: keys 0x1E / 0x20, 2 tabs (a stick left / right sends A / D)`, the first 12), once
`the Rewards tab is up (its arrows up | hidden)`, and `menu: page keys up (the Rewards tab's arrows | a tab
list on A / D): a stick left / right sends A / D` / `menu: page keys gone: ...` (the first 20).

`menu:` the located cursor (`cursor event handler at RVA 0x18000E0, cursor pointer at RVA 0x47DDB88`),
the map page (`the Dossier's map page ...`), a panel placed again after a re-anchor,
`pointer on; panel ...`, `right button down` / `up`,
`the game shows its cursor (a menu screen | a menu over the game): panel ...`,
`the cursor is gone; the panel and the pointer are down` (or `...; the pointer is down, the panel stays
while the menu's backdrop shows`, then `the cursor is back; the panel stays where it was` or `the panel is
down (held N s)`), every button, key and wheel event (the first 300, then one a minute) and the first
moves, `controllers' gameplay input held back (menu)` / `back on`, and every 10 s the counts (panel frames,
moves, clicks, wheels, keys, not delivered, the cursor, and the game's desktop cursor moves and clips kept
off the desktop). `keys:` `the game read an injected mouse event`, `desktop cursor kept from the game while
it is not in the foreground`. `ui:` `weapon wheel straight ahead (panel HUD): GUI rect (x, y) w x h where the game drew it
(from the GUI image; N headset frame(s) after it came up) on a W x H m quad D m ahead (A deg wide); the head-locked HUD in N piece(s) around it` when the wheel goes
ahead and `weapon wheel back on the HUD after S s (N headset frame(s) straight ahead of M up; last GUI rect (x, y)
w x h)` when it goes back, and `weapon wheel bounds: ... reading(s) in all, ... with the wheel found; ... CPU
... us a reading ...; GPU copy ... us ...` after the first wheel found and every 600 readings (30 lines)
(40 wheel lines in all, these and `weapon wheel up for S s (N headset frame(s)), but where the game drew it was
not measured: it stayed on the HUD` counted together); once, `the head-locked HUD
is cut into N pieces, only M fit; the rest is not shown` if the HUD ever needs more quads than the
layer has. `menu model:` at start-up `idMenuWidget_3D_Stand::UpdatePosition view at RVA 0x15A6D84` and
`hooks at RVA 0x15A6D97 (the render view) and 0x15A7134 (the placed model); ...` (or `off
(ETERNALVR_MENU_MODEL_PANEL=0); ...`, or why a check failed), then once `the first model placed from the
panel camera: camera (...) fwd (...), fov 90.00 x ..., the panel N unit(s) ahead; the model at (...),
magnified onto the panel by F`.

## Verified

- The menu's 3D model: the panel camera and the magnification are unit-tested
  (`tests/features/menu/model_camera_tests.cpp`: a GUI element's ray from the camera meets the panel where
  the panel shows it, for a straight, a turned and an off-centre panel and a scaled world; the camera stays
  put in the world while the head turns and moves; the model ends on the panel's plane, unchanged as the
  camera sees it; bad input places nothing). The hook sites, the view fields the function reads (fov_x / fov_y
  0x28 / 0x2C, vieworg 0x94, viewaxis 0xA0) and the idRenderView's size (0x29950) are read from the exe of
  build 25216728; on the rig and a headset it is still to be seen.
- Unit tests: `tests/features/menu` (ray and panel: hits, corners, misses, turned panels, pixels, the dot,
  the beam quad and image; the router: closed-loop moves and the timeout, clicks after the cursor settles,
  quick taps, a trigger held into a menu, back, scroll and tabs with repeat, the grips as the tab keys Q
  and E, the stick click's C, the page keys A and D from the sticks while the Rewards tab or a tab list on
  A / D is up (`page_keys_tests.cpp`), the Dossier map's sticks (pan, zoom and rotate, and a drag let go
  when the menu closes), the pointer changing hands, release and the gameplay latch when the menu closes,
  left-handed, NaN input) and the menu settings in `tests/ui_layer`. The inputs held through the close:
  `tests/features/input/menu_release_latch_tests.cpp` (B and a stick held through the close, a fresh press
  after it, a press let go in the menu, several inputs let go one by one, analog thresholds) and
  `input_mapper_menu_tests.cpp` (through the mapper: no weapon-mod switch from the B or Y that backed out,
  no wheel, quick switch, chainsaw or turn from a stick held through, no shot from the trigger that clicked
  Resume until it is pulled again, a fresh press at once).
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
is gone`); with the build of that run, the other hand's grip rotated the map both ways with 7 cursor moves
in all and no rotation after the pointer stopped (`menu: right button down` / `up`, `cmp9.png`,
`cmp10.png`), and a stick click centred it after a pan (`menu: key down 0x43`, `cmp12.png`). Since then the
grips are the tab keys Q and E and the map rotates on a stick (`menu_router_map_tests.cpp`).

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
- The panel follows only a turn away of more than 60 degrees held for a second (`ETERNALVR_MENU_FOLLOW`);
  how that feels in a headset is untested.
- A flat, not curved, panel (cylinder layers are M6's next step).
- Without the UI layer (`ETERNALVR_UI_LAYER=0`, a mono run outside the launcher, which leaves it off, or a
  UI layer that failed its checks at start-up) a menu over a head-tracked frame has no panel: no pointer
  and no Back, and gameplay input is not held back. In a popup only jump (Space) and melee (E) reach it, so
  a tutorial that waits for another mechanic's key stays up. The Menu tap still pauses and resumes; the
  title screen and the main menu (the cinema path) work as usual.
- The launcher's settings restore leaves `r_windowPosX` / `r_windowPosY` in `DOOMEternalConfig.local` (the
  game saves the window position the layer set); after the live run they were put back by hand. Fixed in the
  launcher: the window and display keys (`launcher/data/session-keys.txt`) are now restored like the forced
  keys; not yet seen on the rig.
- Menus the game navigates only with a pad (if any) would need the virtual gamepad's D-pad; none was found.
- On Vive wands the Arsenal's customize weapon (F) and equip / unequip mod (X) have no controller button;
  the Praetor suit screen is untested.
- The Dossier map's 3D view is the game's own head-tracked view, not on the panel: it sits around the
  player and does not follow the panel.
