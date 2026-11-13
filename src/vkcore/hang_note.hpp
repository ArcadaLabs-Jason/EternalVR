#pragma once

// The hang note (docs/VR_STEREO.md, "Hangs"): when the game stops presenting in play for 5 s
// (gpu_timing/present_hang.hpp), the XR worker, which keeps the headset on the last image, writes where each
// of the process's threads is: the module and offset of the instruction it stands at, its start address, and
// the first return addresses on its stack that lie in the game, the layer or a driver. A second note at 20 s
// tells a thread stuck in one place from one that keeps moving, and a line says so if the game presents
// again. A player's export of a freeze then shows whether a game thread was inside the layer, and where.
//
// Each thread is suspended on its own, only for the copy of its context and of its stack's top: nothing
// allocates, logs or takes a lock while a thread is suspended, and the modules are named from a list made
// while the game was presenting, so a thread that hangs holding the loader's or the heap's lock cannot stall
// the XR worker too.

namespace evr::vkcore::hang_note {

// XR worker, every iteration of its loop: refreshes the module list while the game presents and writes the
// notes. Cheap when nothing is due (a few atomic reads).
void check();

// The game's present hook (stall_watch::presentEntered), on the game's thread. Rig only:
// ETERNALVR_TEST_HANG=<seconds> or <milliseconds>ms (never set by the launcher) holds one present that long
// once the game has been in play for a while (with "@<seconds>", the first present in play that long after
// the player is in the map: gpu_timing::parseTestHang), so a run shows the notes of a real hang and the line
// when it ends, or a short stall such as a checkpoint save's at a known time.
void testHangAtPresent();

} // namespace evr::vkcore::hang_note
