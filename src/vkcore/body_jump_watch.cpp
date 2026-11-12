#include "vkcore/body_jump_watch.hpp"

#include "vkcore/log.hpp"
#include "xr_math/head_aim.hpp"

#include <cmath>
#include <cstdint>
#include <optional>

namespace evr::vkcore {

namespace {

struct BodyJumpWatch {
    std::optional<float> lastYaw;
    std::uint64_t jumps = 0;
};
BodyJumpWatch g_bodyJump;

} // namespace

void watchBodyYaw(float yaw, bool headAimed, bool forcedView, bool cutscene, bool menuUp) {
    if (g_bodyJump.lastYaw) {
        const float turn = xr_math::normalize180(yaw - *g_bodyJump.lastYaw);
        if (std::fabs(turn) > 90.0f && ++g_bodyJump.jumps <= 20) {
            EVR_LOG("aim: the body turned %.1f deg in one game frame (%.1f -> %.1f); %s, forced view %s, "
                    "cutscene %s, menu %s (jump %llu)",
                    turn, *g_bodyJump.lastYaw, yaw, headAimed ? "head aim" : "the game's yaw",
                    forcedView ? "yes" : "no", cutscene ? "yes" : "no", menuUp ? "yes" : "no",
                    static_cast<unsigned long long>(g_bodyJump.jumps));
        }
    }
    g_bodyJump.lastYaw = yaw;
}

} // namespace evr::vkcore
