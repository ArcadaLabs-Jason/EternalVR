# Menus: the game's cursor and mouse input (M6)

Retail `DOOMEternalx64vk.exe`, Steam build 25216728. Found with static analysis (`analysis/m6-menus`:
import cross-references, capstone) and live probes on the rig (a hardware write watchpoint on the cursor,
and memory scans while injecting mouse motion). All addresses are RVAs in this build. Used by
`docs/VR_MENUS.md`.

## 1. How the game reads the mouse

- Only through raw input. The window procedure (0x1DC3B90) handles `WM_INPUT` (0xFF): `GetRawInputData`,
  then per device type: mouse -> 0x1DC18B0, keyboard -> 0x1DC1110. `WM_MOUSEMOVE` goes to
  `DefWindowProc`; the game's code calls `GetCursorPos` nowhere (only the embedded MFC tools do).
- Mouse (0x1DC18B0): only while the mouse is taken (`input + 0x12`); a record with motion first puts the
  desktop cursor back in the window's centre (`SetCursorPos`), then the `RAWMOUSE` is queued; absolute
  motion (`MOUSE_MOVE_ABSOLUTE`) is turned into deltas from the last absolute position. Each frame 0x1DC1AD0
  turns the queue into the user command's mouse samples and into system events: `SE_MOUSE` (type 3) with
  the deltas, key events `K_MOUSE1`.. (0x11E + button, swapped with `SM_SWAPBUTTON`) and wheel keys
  (0x127 / 0x128, one press and release per 120). Events go into the system event ring at 0x6B4AC88 (0x181
  entries of 32 bytes; poster 0x1DB5C30).
- Keyboard (0x1DC1110): by scan code, with the E0 and E1 prefixes folded in (0xE00000, 0xE10000), not by
  virtual key. An injected key needs its scan code and `RI_KEY_E0` where the key has one.
- Taking the mouse (0x1DC1E60, on activation): `RegisterRawInputDevices` for the mouse with flags 0x230
  (`RIDEV_NOLEGACY | RIDEV_CAPTUREMOUSE`), `ClipCursor` to the window, cursor hidden, `SetCursorPos` to the
  centre. `in_unlockMouseInMenus` (default 0) would let the cursor leave the window in menus.

## 2. The menu cursor

- One global `idCursor` (type info: 0x58 bytes; `active` +0x00, `mouseX` +0x10, `mouseY` +0x14,
  `lastMouseX/Y` +0x18/+0x1C, `gui` +0x30), reached through the pointer at 0x47DDB88 (`engine_t::cursor`;
  the instance was at 0x541E460 in every run). Every menu, the title screen, pause and the in-game GUI
  screens use it; each `idSWF` keeps its own stage position (`mouseX` +0x70, `cursorParms` +0x50, stage
  3840 x 2160 for the menus), which follows it.
- `idCursor::HandleEvent` (0x18000E0; signature `40 53 48 83 EC 20 83 3A 03 48 8B D9 75 ?? 8B 42 04 01 41
  10 8B 42 08 01 41 14`): for `SE_MOUSE` adds dx and dy, clamps to 0..render width and 0..render height
  (render system vtable 0x200 / 0x208: the output size, the GUI target's size), sets `needTrace`. Motion is
  1:1 in GUI pixels (live: 600, 400 of injected motion put the cursor at 600, 400).
- The event loop calls it as `mov rcx,[0x47DDB88]; lea rdx,[rbp-0x20]; call 0x18000E0` (0x43E362); that is
  the only call of the handler through the pointer, which the layer uses to find the pointer.
- `idCursor::Update` (0x1800260, called from 0x439F97 with r8b = whether the cursor is shown): shown ->
  `active = 1` and the cursor drawn; hidden -> the cursor is put back in the centre and `active = 0`. So
  `active` is the game's own "a menu is up" signal.

## 3. What this means for VR

- Relative raw mouse records answered by the layer's `GetRawInputData` replacement move the cursor exactly,
  click (`RI_MOUSE_LEFT_BUTTON_DOWN` / `UP`) and scroll (`RI_MOUSE_WHEEL`) without focus. Reading the
  cursor back closes the loop; nothing is written to the game.
- The menus ignore the arrow keys (tried in settings and pause, with the E0 flag). Escape goes back, Q and E
  switch tabs, sliders take clicks and drags.
- Because the game warps and clips the desktop cursor whenever it believes it has the mouse, and the layer
  keeps it believing it is active, those calls must not reach the desktop while the window is not in front
  (`vkcore/key_inject.cpp`, `installCursorGuard`).

## 4. The weapon wheel [static-verified]

- The wheel is `idSWFWidget_WeaponWheel` (vtable 0x2DDD1D0), a `idSWFWidget_Wheel` (0x2DDD7F8). The frame
  update (0x1597860) picks the pointer by the active device (0x1826350: 2, keyboard and mouse, unless
  `swf_platformOverride` names a platform or the joystick is active): vtable 0x180 for the mouse,
  0x178 (0x1596910) for a stick.
- Mouse (0x1592BE0; the base widget's is 0x1596DF0): reads the **menu cursor** (`idCursor` through
  0x47DDB88, `mouseX` +0x10, `mouseY` +0x14) against the wheel's centre (vtable 0xF0, 0x1592AA0: the
  widget's stage position over the stage size times the render size, 0x1596780). A cursor farther than
  `swf_wheel_mouse_clampRadius` (200) is clamped onto that circle and written back (0x1800220, the cursor's
  setter). An offset beyond `swf_wheel_mouse_deadZone` (50) on either axis highlights the segment in its
  direction (0x1596030); the widget's +0x1AC keeps the highlighted segment.
- The cursor moves with every `SE_MOUSE` event, shown or not (the event loop 0x43E1C0 calls
  `idCursor::HandleEvent` 0x18000E0 for every event outside the console; it adds the motion and clamps
  it to the render size). So relative raw mouse motion, 1:1 in GUI pixels, is what moves the wheel's
  pointer. Hidden, the cursor is recentred only when it goes from shown to hidden (0x1800260), not every
  frame.
- The view-angle path (the generator 0x17FC650, mouse to angles in 0x17FEFD0: the mouse samples times
  `m_yaw` / `m_pitch` and the per-user sensitivity it is given) has no wheel test of its own; adding to
  the accumulated angles, as the controllers first did, never reaches the wheel.
- On the rig's log (2026-09-27, Quest 3, `weapon_wheel` at 875.9 s) the wheel showed no menu cursor: the
  menu router stayed out and the wheel took no selection from the angle path.

## 5. Hiding the cursor while the sticks drag the map [static-verified]

- No cvar hides it: `guiCursor_arrow` / `guiCursor_hand` are the cursor's material fields (type info
  `idCursor` +0x20 / +0x28), not cvars. Their materials are `textures/guis/cursor_empty` (despite the name,
  the arrow picture `guicursor_arrow.tga`) and `textures/guis/cursor_hover` (`guicursor_hand.tga`), both
  `template/gui/gui_guiblend` (the `guiblend` program, alpha blending); no other cursor picture is
  referenced.
- `idCursor::Update` (0x1800260) is the only place the cursor is drawn. Shown (r8b): `active` = 1, the
  cursor's own GUI model (+0x30) sized to the render size (0x194F7D0), its blend state set to 0x2C
  (`[gui+0x4D8]`, source alpha / one minus source alpha), the colour (1, 1, 1, 1) at a constant
  (0x2A5B950) packed (0x3565C0) and stored in the model (`mov [rbx+0x4D0], eax` at 0x18002DE), then one
  32 x 32 picture at (`mouseX`, `mouseY`) through 0x7E9380 (into 0x194E0F0, which takes the model's
  +0x4D0 as the vertex colour). Which picture: `showHitState` (+0x0C) 0 draws +0x28; otherwise
  `hitState` (+0x04) 0 draws +0x20, 1 draws +0x28, anything else draws nothing. Then the trace (0x1800450)
  and, when the cursor moved, `hitState` and `numHits` cleared.
- Hidden (r8b clear): the cursor is put back in the middle and `active` = 0 once, on the change. That is the
  game's own hide, but it would end menu mode (the layer's signal) and recentre a drag, so it is not used.
  Forcing `showHitState` / `hitState` would need a write before and a restore after every draw (the trace
  clears `hitState` as the cursor moves) and a null material logs a warning on every draw.
- Used: a register-editing mid hook on the colour store (0x18002DE) sets eax to 0 while a stick drag owns
  the cursor (`vkcore/menu_cursor_hide.cpp`): the picture is drawn with alpha 0, which the blend leaves
  invisible. Nothing in the game's memory is changed for longer than the frame: the next Update stores the
  colour afresh. Located by a signature over Update from its start to the `showHitState` test (unique in
  `.text`), with the packed constant checked to be (1, 1, 1, 1). Log: `menu: cursor update at RVA
  0x1800260, its draw colour hooked at RVA 0x18002DE: ...`, then once each `a stick drags the map: the
  game's cursor is hidden (first time)` and `the game's cursor is shown again after the drag (first time;
  N cursor draw(s) hidden)`.
- Not yet seen live: that alpha 0 leaves no trace of the cursor on the Dossier map (the blend reading of
  0x2C follows id Tech's `GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA`; colour 0 hides the
  picture under additive blending too).

## 6. The Dossier map's input (idAutomap) [static-verified]

- `idAutomap::HandleEvent` (0xA54760; the automap object is `game + 0x1A7380`) gets every event from the
  game's event handler (0x6CF1C0, through 0x69C750) before the player's handler; its return value is ignored.
  It acts only while `isActive` (+0x1A28) and `shouldHandleInput` (+0x1A29) are set. Its input is
  `idAutomapInput_t` at +0x240 (type info): `leftDragDelta` +0x240, `rightDragDelta` +0x248,
  `middleDragDelta` +0x250, `wheel` +0x258, the three buttons +0x25C..+0x25E, the keys +0x260..+0x264, the
  pad's sticks, triggers and buttons +0x268..+0x280.
- **Drags add up raw motion, not the cursor.** For `SE_MOUSE` (0xA5493C): left held -> `leftDragDelta -=
  (dx, dy)`; else right held -> `rightDragDelta`; else middle held -> `middleDragDelta`. `idCursor` and the
  SWF stage mouse are not read; `idCursor::HandleEvent` clamps only its own copy of the position. So motion
  keeps counting while the cursor sits at the edge of its range. With two buttons held, all motion goes to the
  first in that order (left pans). A button event only sets or clears its flag (keys 0x11E / 0x11F / 0x120);
  nothing is captured at the press, there is no drag threshold. For each raw record the motion is posted
  before the buttons, so motion in the record of a press does not count and motion in the record of a release
  does.
- `idAutomap::ProcessInput` (0xA56000, once a frame from `Update` 0xA53CF0) uses and clears the deltas: pan
  += leftDragDelta * `automap_panSpeedMouse` (0.00125), the view focus then moved by pan * max(
  `automap_minPanSpeedModifier` 100, view distance); yaw += rightDragDelta.x * `automap_rotateSpeedMouse`
  (0.1 degrees a count), pitch -= rightDragDelta.y * 0.1 * `automap_rotatePitchYawRatio` (0.8), within
  `automap_minPitchDegrees` 1 .. `automap_maxPitchDegrees` 80; zoom += wheel * `automap_zoomSpeedMouseScroll`
  (30) + middleDragDelta.y * `automap_zoomSpeedMouseDrag` (1). `wheel` is set to +1 / -1 by the wheel keys,
  not added: several notches in one frame are one step. The camera moves half way to where it should be
  every frame.
- **Keys** (fixed key numbers, not binds; 0xA54834..0xA5489B): 0x11 W up, 0x1E A left, 0x1F S down, 0x20 D
  right, 0x2E C recentre (on the press; not while the map-group or fast-travel screen is open). Pan +=
  normalize(A - D, W - S) * `automap_panSpeedKeyboard` (1.25) * dt: W pans as a drag up, D as a drag to the
  right, and the keyboard's full speed equals 1000 counts a second of a drag. The keys and a drag are read
  apart, so the keys can pan while the right button rotates (`features/menu/map_pan_keys.hpp`). The arrow keys
  and the movement binds are not read.
- **Pad** (`SE_JOYSTICK`, 0xA54A24): the left stick pans (`automap_panSpeedController` 1.25), the right
  stick rotates (`automap_rotateSpeedController` 150 degrees a second), the triggers zoom
  (`automap_zoomSpeedController` 500), all at once and scaled by time; deadzone `automap_deadzone` 0.2. Not
  used by the layer.
- Not checked: whether the Dossier's screen does anything with the left presses after the automap (the
  layer presses only with the cursor in the middle of the screen), and the engine handlers before the game in
  the event loop (mouse drags already reach the map, so they pass motion and buttons through).

## 7. How the game reads the keyboard [static-verified]

- Raw input only, by the key's place. The keyboard handler (0x1DC1110) reads a `RAWKEYBOARD`'s `MakeCode`
  and `Flags` and nothing else (not `VKey`, not `Message`): key number = `MakeCode | (RI_KEY_E0 ? 0x80 : 0)`,
  DirectInput's numbering (W 0x11, A 0x1E, up arrow 0xC8), posted as `SE_KEY`. Fake shifts (E0 2A/AA/36/B6)
  are dropped; 0x45 is Num Lock (0xC5), 0x54 Print Screen (0xB7), E1 1D 45 Pause; other E1 records and make
  codes from 0x80 up are dropped. The key state array (input + 0x18048) is written only here.
- No keyboard layout on this path: no `MapVirtualKey`, `ToUnicode` or `GetKeyboardLayout`. The layout is used
  only to show a key's label (0x1DBE0F0: `MapVirtualKeyA` + `ToUnicode`), so on a French AZERTY keyboard the
  key labelled Z is K_W and is shown as "Z". The key-name table (.rdata 0x38A54E0, 252 entries) and the
  automap's W/A/S/D/C (section 6) use these numbers.
- `WM_KEYDOWN`/`WM_KEYUP` go to `DefWindowProc`; `WM_SYSKEYDOWN` only for Alt+Enter; `WM_CHAR` (0x1DC3810)
  is the only text path. The game's own `GetAsyncKeyState` calls ask only for Control (the low-level
  keyboard hook 0x1DC3740 and `WM_SIZING` 0x1DC4510); `GetKeyState` and `GetKeyboardState` are called only by
  the embedded MFC tools.
- For the layer: an injected key is sent as its US-keyboard scan code (`src/platform/key_injection/
  us_scan_codes.hpp`). The layout's own scan code (`MapVirtualKeyW`) sent W as Z and A as Q on AZERTY.
