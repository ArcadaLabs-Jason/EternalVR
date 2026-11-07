// The weapon hand's index finger on the trigger (weapon_finger.hpp).

#include "vkcore/weapon_finger.hpp"

#include "features/arm/arm_frames.hpp"
#include "features/arm/trigger_finger.hpp"
#include "vkcore/arm_dump.hpp"
#include "vkcore/arm_solve_mode.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/game_arm.hpp"
#include "vkcore/log.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace evr::vkcore::controllers::weapon_finger {

namespace {

constexpr const char* kTag = "weapon arm";
constexpr std::uint32_t kMaxItemLines = 40;

// Per arm (off-hand hook thread only): the share and its sensors, the held item's row.
struct State {
    arm::TriggerFinger finger;
    const std::byte* decl = nullptr;
    std::optional<float> lift;
    arm::TriggerSensors sensors;
    float open = 0.0f;
};
std::array<State, 2> g_state;
std::uint32_t g_itemLines = 0;
std::uint32_t g_sensedLines = 0;

State& stateOf(arm::ArmSide side) {
    return g_state[side == arm::ArmSide::Left ? 0 : 1];
}

void quats(std::array<float, 12>& out, const arm::HandLocal& hand) {
    for (std::size_t b = 0; b < arm::kIndexJoints.size(); ++b) {
        const Quat q = arm::toQuat(hand[arm::kIndexJoints[b]].axis);
        out[b * 4] = q.x;
        out[b * 4 + 1] = q.y;
        out[b * 4 + 2] = q.z;
        out[b * 4 + 3] = q.w;
    }
}

} // namespace

std::optional<float>
update(arm::ArmSide side, const std::byte* decl, float dt, const input::ControllerSettings& cfg) {
    State& s = stateOf(side);
    const bool on = arm::followsTrigger(cfg.offhand == input::OffhandMode::Free, armSolveMode().writesPast);
    if (decl != s.decl) {
        s.decl = decl;
        const std::string name = decl ? itemDeclName(decl) : std::string{};
        s.lift = name.empty() ? std::nullopt : state().offsets.lookupTriggerLift(name);
        if (!name.empty() && g_itemLines < kMaxItemLines) {
            ++g_itemLines;
            if (s.lift && on) {
                EVR_LOG(
                    "%s: '%s': the index finger follows the trigger (lifts %.2f of the way straight off it)",
                    kTag, name.c_str(), static_cast<double>(*s.lift));
            } else {
                EVR_LOG("%s: '%s': the index finger the game's (%s)", kTag, name.c_str(),
                        s.lift ? "the free off hand and the bind solve only" : "no trigger finger row");
            }
        }
    }
    const std::optional<input::InputFrame> frame = latestFrame();
    if (!frame) {
        s.finger.reset();
        return std::nullopt;
    }
    const input::HandState& h =
        frame->hand(cfg.handedness == game::Handedness::Right ? input::Hand::Right : input::Hand::Left);
    s.sensors = {h.trigger, h.triggerTouch};
    if (!on || !s.lift) {
        s.finger.reset();
        return std::nullopt;
    }
    const bool sensed = s.finger.touchSensed();
    s.open = s.finger.update(s.sensors, h.gripValid, *s.lift, dt);
    if (s.finger.touchSensed() != sensed && g_sensedLines < kMaxItemLines) {
        ++g_sensedLines;
        EVR_LOG("%s: the weapon hand's trigger touch %s", kTag,
                sensed ? "forgotten (the controller not tracked): the finger rests on the trigger"
                       : "sensed: the index finger lifts off the trigger when it is not touched");
    }
    return cfg.testArmFault == input::ArmFault::Trigger ? kFaultOpen : s.open;
}

void release(arm::ArmSide side) {
    stateOf(side).finger.reset();
}

void noteGame(arm::ArmSide side, const std::byte* hands, const arm::HandLocal* lastGrip) {
    game_arm::HandRead read;
    if (!arm_dump::on() || !game_arm::readHand(side, hands, read)) {
        return;
    }
    const State& s = stateOf(side);
    arm::DumpTrigger d;
    d.value = s.sensors.trigger;
    d.touch = s.sensors.touch;
    d.sensed = s.finger.touchSensed();
    d.posed = false;
    d.lift = s.lift.value_or(0.0f);
    if (lastGrip) {
        quats(d.game, *lastGrip);
    }
    quats(d.want, read.local);
    d.read.emplace();
    quats(*d.read, read.local);
    arm_dump::noteTrigger(side, d);
}

void noteDump(arm::ArmSide side,
              const std::byte* hands,
              float open,
              const arm::HandLocal& game,
              const arm::HandLocal& written) {
    if (!arm_dump::on()) {
        return;
    }
    const State& s = stateOf(side);
    arm::DumpTrigger d;
    d.value = s.sensors.trigger;
    d.touch = s.sensors.touch;
    d.sensed = s.finger.touchSensed();
    d.lift = s.lift.value_or(0.0f);
    d.open = open;
    quats(d.game, game);
    quats(d.want, written);
    game_arm::HandRead read;
    if (game_arm::readHand(side, hands, read)) {
        d.read.emplace();
        quats(*d.read, read.local);
    }
    arm_dump::noteTrigger(side, d);
}

} // namespace evr::vkcore::controllers::weapon_finger
