#pragma once

// The player's own values in profile.bin while the layer holds cvars the game saves there
// (mp_policy/profile_cvars.hpp, docs/rig-findings/profile-leak.md; Steam build 25216728, Game Pass 1.0.56.0).
//
// The game's profile serializer for cvars (RVA 0x141D040 in the Steam build, called only from
// idPlayerProfileShell::InternalSerialize at 0x1417FF0, call site 0x1418AA0) runs for the profile's load and
// for its save, switched on the first byte of its second argument (0: load). A save writes each listed cvar's
// live value unless the cvar is archived to a text config (flags 0x30000); a load sets each cvar from the
// profile. The game saves the profile in every session, so a held value went into profile.bin and stayed in
// the flat game: view_skipShakes 1 (no camera shake) after every Route S or Parallel Eye Rendering session
// (rig: 0 before, 1 after), and the launcher's Sharpening level; the HUD Scale VR holds at Large would have
// stayed Large in the flat game the same way. A detour of the serializer, for the cvars of
// mp_policy::profileStoredCvars:
// - load: a cvar the layer wrote first gets its value from before the layer's first write, so the load sets
//   the profile's value over it, or leaves the game's own when the profile has no key; the value after the
//   load is kept as the player's (also what a multiplayer guard trip gives back, cvar_book::setTripValue).
//   The hold writes its value again on the next present.
// - save: a held cvar gets the player's value for the write and the held value back after it (through the
//   cvar book, which asks the multiplayer guard). Without a load seen, the player's value is the one before
//   the layer's first write.
// - a change the player makes to one of these settings in the game's menu during a VR session is not saved:
//   the hold overrides it live, and the save writes the value the profile's load gave. Except the HUD Scale,
//   whose hold yields to it (runtime_cvars::playerChose, profilePlayerChoice): the saves write the choice.
//
// Locking: the swaps are made under runtime_cvars::heldMutex, which is then released while the game's
// serializer runs; runtime_cvars::setProfileIo keeps apply() off these cvars until the hook takes the lock
// again to put the held values back. So no game code beyond SetString, as in apply(), runs under the layer's
// lock (the serializer calls SetString through the engine's cvar system and is called with whatever locks the
// game's profile code holds), the present thread never waits on a profile load or save, and no present writes
// a held value into the middle of one. The other held cvars are written as usual meanwhile.
//
// The one-time repair (ETERNALVR_PROFILE_REPAIR=1, set by the launcher while its marker is absent and its
// session logs show a stereo session that held view_skipShakes 1): at the session's first profile load, a
// stored view_skipShakes 1 becomes 0, which the next save writes (not after a multiplayer guard trip during
// that load: nothing is reported and the launcher asks again). The status file's profile_repair says off,
// not_needed or done (after that save); the launcher sets its marker once it reads one of them. The marker is
// per PC, not per profile: a second Steam account, or Steam and Game Pass on one PC, is not repaired once the
// first settles.
//
// Only while the multiplayer guard allows game touches; after a trip the book gave the player's values back
// and the serializer runs as is.

#include <string>
#include <string_view>

namespace evr::vkcore {

// From vkCreateInstance with Route S or Parallel Eye Rendering, after the multiplayer guard, before the game
// loads the profile. Locates the serializer (its signature once in .text, view_skipShakes' archive check
// inside it) and hooks it; logs and does nothing when a check fails.
void installProfileHooks();

// Under runtime_cvars::heldMutex: the player chose `value` for the profile-stored cvar `name` in the game's
// menu while the layer held it (runtime_cvars.hpp, playerChose): the saves write it from now on, and a
// multiplayer guard trip gives it back.
void profilePlayerChoice(std::string_view name, const std::string& value);

} // namespace evr::vkcore
