#pragma once

// Hides the game's menu cursor while a stick drags the Dossier map (features/menu/drag_cursor_hide.hpp,
// docs/rig-findings/menus.md section 5). The menu stays in mouse mode; only the cursor's picture is left out.
//
// idCursor::Update (RVA 0x1800260 in build 25216728) draws the cursor into its own GUI model every frame
// the cursor is shown: it packs the draw colour (1, 1, 1, 1) and stores it in the model (`mov [rbx+0x4D0],
// eax`, RVA 0x18002DE), then draws the 32 x 32 cursor picture with it. A hook on that store turns the colour
// to 0 (alpha 0: the GUI blend leaves the screen as it was) while hiding is asked for. Nothing in the game's
// memory changes: the next frame's Update stores the colour afresh, so the cursor comes back by itself on
// the first frame hiding is no longer asked for. The cursor's position, its `active` flag (menu mode) and
// its input are untouched.
//
// Only while the multiplayer guard allows touching the game (checked on every draw); a trip drops the
// request. ETERNALVR_MAP_CURSOR_HIDE=0 leaves it off. Located by signature, fail closed.

namespace evr::vkcore::menu_cursor_hide {

// Locates and hooks the cursor's colour store, once per process; later calls return the first answer.
bool install();

// Present thread, every menu frame: whether the cursor is to be hidden now. Ignored when not installed.
void setHidden(bool hidden);

} // namespace evr::vkcore::menu_cursor_hide
