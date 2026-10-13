#pragma once

// The cvars the layer holds that the game also saves into the player's profile (profile.bin), and the value
// the profile gets instead of the held one (vkcore/profile_hooks.hpp, docs/rig-findings/profile-leak.md).
// Portable so the rules can be tested without the game.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace evr::mp_policy {

// The cvars the game's profile serializer writes from their live value (no archive flag keeps them out) and
// the layer may hold: view_skipShakes (the comfort set, held 1 in VR) and r_sharpening (the launcher's
// Sharpening). The serializer also writes r_antialiasing and r_raytracedReflectionsTemporalUpscaleQuality,
// but the profile's load sets both again from its own fields (the DLSS index and the Reflections level), so a
// held value saved there does not reach the flat game; putting another r_antialiasing in for a save would
// also switch the renderer's anti-aliasing for that moment. They are left out.
const std::vector<std::string_view>& profileStoredCvars();
bool profileStoredCvar(std::string_view name);

// The same value: the same number when both read as numbers in full ("1" and "1.0"), else the same text
// without regard to ASCII case.
bool sameCvarValue(std::string_view a, std::string_view b);

// Before a profile load, for one profile-stored cvar: the value to put in first, or nothing. `live` is its
// value now and `beforeLayer` its value before the layer's first write (nothing: the layer has not written
// it). A cvar the layer wrote gets that earlier value back, so the load sets the profile's value over it as
// in the flat game, and a profile without the key leaves what the game had without the layer, not the held
// value.
std::optional<std::string> profileLoadSeed(std::string_view live,
                                           const std::optional<std::string>& beforeLayer);

// What one profile-stored cvar puts into a profile write.
struct ProfileWrite {
    bool swap = false;       // put `value` in for the write and the live value back after it
    std::string value;       // the player's value (empty when not known)
    const char* source = ""; // where `value` comes from, or why nothing changes
};

// `live`: the cvar's value now. `loaded`: its value right after the last profile load (nothing: no load
// seen). `beforeLayer`: as for profileLoadSeed. A cvar the layer never wrote holds the game's own value,
// which the write keeps. Otherwise the player's value is the load's, else (no load seen) the value before the
// layer's first write, and it is put in when the live value differs.
ProfileWrite profileWrite(std::string_view live,
                          const std::optional<std::string>& loaded,
                          const std::optional<std::string>& beforeLayer);

// The one-time repair the launcher asks for (ETERNALVR_PROFILE_REPAIR=1) when the player played in stereo
// before the profile hooks: those sessions left view_skipShakes 1 in the profile, which the hooks then keep
// as the player's own. At the first profile load of a session: Off without the request; Repair (to 0) when
// the load set view_skipShakes 1 (`loadedShakes`); NotNeeded otherwise (0, or no value read). r_sharpening is
// never repaired: its value before VR is not known, and the game's menu shows it.
enum class ProfileRepair { Off, NotNeeded, Repair };
ProfileRepair profileRepair(bool requested, const std::optional<std::string>& loadedShakes);

// The status file's profile_repair value (vkcore/status_file.hpp): "off", "not_needed", or "done" once a
// profile save after the repair wrote 0; nullptr while a repair waits for that save.
const char* profileRepairStatus(ProfileRepair repair, bool savedSince);

} // namespace evr::mp_policy
