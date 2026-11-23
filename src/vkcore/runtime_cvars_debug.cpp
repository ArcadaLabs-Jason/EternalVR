// ETERNALVR_DEBUG_CVARS' "name=?" entries (runtime_cvars_impl.hpp, logQuery): rig readings of a cvar the
// layer does not hold, so a run sees when the game's own settings put a value back (a map load, a checkpoint
// load, the settings menu).

#include "vkcore/log.hpp"
#include "vkcore/runtime_cvars_impl.hpp"

namespace evr::vkcore::runtime_cvars {

namespace {

constexpr int kQueryChanges = 40; // changes logged after the first reading

} // namespace

void logQuery(Held& h, int value) {
    const float number = readFloat(h.object);
    if (h.logged && value == h.queryValue && number == h.queryNumber) {
        return;
    }
    const bool first = !h.logged;
    h.logged = true;
    h.queryValue = value;
    h.queryNumber = number;
    if (first) {
        EVR_LOG("%s: %s reads %d (%g)", kTag, h.name.c_str(), value, static_cast<double>(number));
    } else if (h.queryChanges < kQueryChanges) {
        ++h.queryChanges;
        EVR_LOG("%s: %s now reads %d (%g)%s", kTag, h.name.c_str(), value, static_cast<double>(number),
                h.queryChanges == kQueryChanges ? "; later changes are not logged" : "");
    }
}

} // namespace evr::vkcore::runtime_cvars
