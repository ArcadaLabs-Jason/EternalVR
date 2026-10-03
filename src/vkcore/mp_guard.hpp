#pragma once

// Multiplayer and online safety guard (REQ-16, T-109, ARCHITECTURE section 4a,
// docs/rig-findings/mp-guard.md).
//
// Everything the layer does to the game (camera writes, head aim, key injection, keeping the window
// active, and anything added later) asks allowsGameTouch() first and does nothing when it is false.
// It is true only while the guard is Armed:
//
// - Unarmed until install() has put every detection point in place. Features stay off meanwhile.
// - Refused when any detection point cannot be located and validated (fail closed): features stay off
//   for the life of the process, and the log says which point failed.
// - Tripped as soon as any signal fires: an online or unknown map load, a Steam join request, an
//   accepted invite, a BATTLEMODE lobby or game session, or the main menu on a BATTLEMODE or online
//   screen (seen by a hook on the menu's screen change and, independently, by poll()). Tripped never
//   resets; the hooks stay installed and only watch.
//
// The command line is screened before the layer arms at all: refused arguments (a BATTLEMODE map,
// +connect_lobby from a Steam invite, network settings and so on) keep the layer from loading.

#include "platform/mp_policy/mp_policy.hpp"
#include "platform/mp_policy/trip_listeners.hpp"

#include <cstdint>

namespace evr::vkcore::mp_guard {

// Screens the process command line (once; later calls return the first answer). False, logged, when
// it carries a multiplayer argument; the latch is then tripped and the layer must not load.
bool screenCommandLine();

// Locates, validates and hooks every detection point in the loaded game, once per process, then arms
// or refuses. Returns the state after the attempt.
mp_policy::GuardState install();

// The question every game-touching feature asks before acting, on any thread. Lock-free.
bool allowsGameTouch();

mp_policy::GuardState state();

// The map loads the map-load hook has seen so far (the main menu is one); 0 before the first or without the
// hook. Any thread.
std::uint32_t mapLoads();

// Trip listeners: each is called once, on the thread that trips the guard, right after the latch closes, in
// the order added; one added after a trip is called at once, on the adding thread. A fixed list of
// mp_policy::TripListeners::kCapacity (8), lock-free (mp_policy/trip_listeners.hpp); false (not added) when
// it is full or `listener` is null. Key injection posts one release for each key it holds; the cvar book
// gives back the cvars the layer wrote (cvar_book.hpp); the prompt hooks make the game rebuild its cached
// prompts. A listener may run on any game thread or the present thread, so it takes its own module's locks
// and must not wait for another thread.
using TripListener = mp_policy::TripListeners::Listener;
bool addTripListener(TripListener listener);

// Reads the main menu's current and next screen and trips on an online one. Cheap; the layer calls it
// on every present. Also fires the development test trip (ETERNALVR_GUARD_TEST_TRIP_MS).
void poll();

} // namespace evr::vkcore::mp_guard
