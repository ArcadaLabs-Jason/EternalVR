#pragma once

// The cvars the layer writes at run time and what a multiplayer guard trip gives back (docs/rig-findings/
// mp-guard.md, "What a trip puts back"; ARCHITECTURE section 4a).
//
// runtime_cvars.cpp and taa_hooks.cpp write through write(): under the book's lock it asks the guard, keeps
// the value the cvar had before the layer's first write (one book for both, so the earliest value wins where
// both write the same cvar, as for r_TAASafeMode and r_antialiasing), and calls the module's own setter
// (idCVar::SetString). The book's trip listener, on the thread that tripped, takes the same lock and writes
// each kept value back once, logging one line per cvar. That write is the one kind of write made after a
// trip: it undoes the layer's own change, as the wall-climb restore does (climb_hook.cpp). Because the guard
// is asked under the lock, no layer write can land after the restore.
//
// Left as the layer set them (logged): the window and present cvars and HDR output (mp_policy::
// cvarLeftOnTrip), which would resize the window or change the swapchain mid-game. Not given back here: the
// cvars the launcher sets on the command line and the layer never had to write; the command line set them
// before the layer could read them, and their flat values are not known (mp-guard.md).
//
// Threads, for every cvar write of the layer (runtime_cvars on the present thread, taa_hooks on the stereo
// tick, climb_hook on the camera hook's thread): idCVar::SetString frees the cvar's old value string, and no
// lock is shared with the game's readers, so a game thread reading that string at the same moment reads
// freed memory. The layer assumes the game reads these cvars through the integer and float the setter
// updates, not through the string. That is not shown for every cvar, and the TAA set is written again
// whenever the game puts a value back, so this is an accepted risk, not a proven safe one. The engine
// itself sets cvars from its render threads (taa_hooks.cpp).

#include <cstddef>
#include <string_view>

namespace evr::vkcore::cvar_book {

// idCVar::SetString (RVA 0x376020 in build 25216728): (cvar object, value, force).
using SetStringFn = void (*)(void* cvar, const char* value, bool force);

// Registers the book's trip listener (once; write() does it too), so a trip logs what it gave back even when
// the layer wrote nothing. Any thread, without the book's lock.
void listen();

// Writes `value` into the cvar `name` (`object`, the engine's cvar object) through `set`, keeping the value
// it had before the layer's first write. False, nothing written, when the guard does not allow game touches,
// an argument is null, or the trip listener could not be registered (fail closed: no write the trip cannot
// give back). Any thread; the caller may hold its own module's lock (taken before the book's).
bool write(std::string_view name, std::byte* object, SetStringFn set, const char* value);

} // namespace evr::vkcore::cvar_book
