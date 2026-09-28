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
