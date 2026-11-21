#include "vkcore/body_jump_watch.hpp"

#include "features/comfort/teleport.hpp"
#include "vkcore/game_motion.hpp"
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

struct TeleportLog {
    comfort::TeleportWatch watch;
    std::uint64_t teleports = 0;
    bool forcedView = false; // in the view the jump came in
    bool cutscene = false;
};
TeleportLog g_teleport;
constexpr std::uint64_t kLoggedTeleports = 40;

void logTeleport(const comfort::Teleport& t) {
    if (++g_teleport.teleports > kLoggedTeleports) {
        return;
    }
    EVR_LOG(
        "game: teleport %llu: the player moved %.1f m in one game frame (%.1f %.1f %.1f -> %.1f %.1f %.1f, "
        "%.0f ms apart; forced view %s, cutscene %s); the view turned %.1f deg over it and %d game view(s) "
        "after (largest step %.1f)",
        static_cast<unsigned long long>(g_teleport.teleports), t.metres, t.from.x, t.from.y, t.from.z, t.to.x,
        t.to.y, t.to.z, t.seconds * 1000.0f, g_teleport.forcedView ? "yes" : "no",
        g_teleport.cutscene ? "yes" : "no", t.turn, t.views, t.largestStep);
}

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

void watchTeleport(const std::byte* player, double seconds, float yaw, bool forcedView, bool cutscene) {
    const std::optional<controllers::PlayerPlace> place = controllers::playerPhysicsPlace(player);
    const std::optional<comfort::Teleport> done =
        place ? g_teleport.watch.update(seconds, place->origin, place->velocity, yaw, forcedView || cutscene)
              : g_teleport.watch.update(seconds, std::nullopt, std::nullopt, yaw, forcedView || cutscene);
    if (done) {
        logTeleport(*done);
    }
    if (g_teleport.watch.startedLastUpdate()) {
        g_teleport.forcedView = forcedView;
        g_teleport.cutscene = cutscene;
    }
}

} // namespace evr::vkcore
