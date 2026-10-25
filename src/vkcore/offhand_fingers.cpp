// The free off hand's fingers from its controller (offhand_fingers.hpp).

#include "vkcore/offhand_fingers.hpp"

#include "features/arm/arm_frames.hpp"
#include "features/arm/hand_pose.hpp"
#include "vkcore/arm_dump.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/game_arm.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/offhand_mods.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string>

namespace evr::vkcore::controllers::offhand_fingers {

namespace {

constexpr float kDegreesPerRadian = 180.0f / std::numbers::pi_v<float>;
// The joint of the hand (kRightHandJointNames order) the trace reports: the middle finger's second bone.
constexpr std::size_t kTraceJoint = 9;
// Below this share of the posed fingers they are left to the game altogether.
constexpr float kGameFingers = 0.01f;

// Hook thread only. Started again on other hands and on every map load (a level's or a checkpoint's), which
// may reuse the hands' address.
struct State {
    const std::byte* hands = nullptr;
    std::uint32_t mapLoads = 0;
    // The game's hand as last read clean: the model's bone lengths, and the pose the fingers go back to.
    arm::HandLocal offsets{};
    bool haveOffsets = false;
    arm::FingerEase ease;
    arm::FingerHold hold;
    LONGLONG last = 0;
    arm::FingerCurls curls;
    arm::HandLocal posed{};
    FingerWrites writes{};
    bool posedLast = false;
    const char* why = "no clean read of the hand yet"; // why the fingers were the game's, for the trace
};
State g;

float turnDegrees(const xr_math::ModelPose& local) {
    const auto q = arm::toQuat(local.axis);
    return 2.0f * std::atan2(length(Vec3{q.x, q.y, q.z}), std::fabs(q.w)) * kDegreesPerRadian;
}

// The rig dump's fingers (arm_dump.hpp): the curls, each finger bone's turn written and as read this tick.
void noteDump(const std::byte* hands) {
    arm::DumpFingers d;
    d.curl = {g.curls.thumb, g.curls.index, g.curls.middle, g.curls.ring, g.curls.pinky};
    game_arm::HandRead read;
    const bool ok = game_arm::readHand(arm::ArmSide::Left, hands, read);
    std::array<float, arm::kFingerModCount> readTurns{};
    for (std::size_t k = 0; k < arm::kFingerModCount; ++k) {
        d.want[k] = turnDegrees(g.writes[k]);
        readTurns[k] = ok ? turnDegrees(read.local[arm::kFingerMods[k]]) : 0.0f;
    }
    if (ok) {
        d.read = readTurns;
    }
    arm_dump::noteFingers(arm::ArmSide::Left, d);
}

} // namespace

const FingerWrites*
pose(const std::byte* hands, bool clean, float armWeight, const input::ControllerSettings& cfg) {
    g.posedLast = false;
    if (offhand_mods::fingerCount(arm::ArmSide::Left) == 0) {
        return nullptr;
    }
    const std::uint32_t loads = mp_guard::mapLoads();
    if (hands != g.hands || loads != g.mapLoads) {
        g = State{};
        g.hands = hands;
        g.mapLoads = loads;
    }
    game_arm::HandRead read;
    if (clean && game_arm::readHand(arm::ArmSide::Left, hands, read)) {
        g.offsets = read.local;
        g.haveOffsets = true;
    }
    const std::optional<input::InputFrame> frame = latestFrame();
    if (!g.haveOffsets || !frame) {
        g.why = !g.haveOffsets ? "no clean read of the hand yet" : "no controller frame";
        return nullptr;
    }
    const input::HandState& h =
        frame->hand(cfg.handedness == game::Handedness::Right ? input::Hand::Left : input::Hand::Right);
    const arm::FingerSensors sensors{h.grip, h.trigger, h.triggerTouch,
                                     h.primaryTouch || h.secondaryTouch || h.stickTouch || h.trackpadTouch ||
                                         h.thumbRest};
    const LONGLONG now = nowQpc();
    const float dt = g.last != 0 ? static_cast<float>(secondsSince(g.last)) : 0.0f;
    g.last = now;
    g.curls = g.ease.update(arm::fingerCurls(sensors), dt);
    // A controller lost or switched off: the fingers ease back to the game's, as they do with the arm's
    // weight.
    const float shown = g.hold.update(h.gripValid, dt) * armWeight;
    if (!(shown > kGameFingers)) {
        g.ease.reset();
        g.why = h.gripValid ? "the arm given back" : "the controller lost or off";
        return nullptr;
    }
    static const arm::HandLocal open = arm::openHand(arm::leftGripHand());
    g.posed = arm::blendHand(g.offsets, arm::posedHand(open, arm::leftGripHand(), g.curls, g.offsets), shown);
    g.writes = arm::fingerWrites(g.posed);
    g.posedLast = true;
    if (arm_dump::on()) {
        noteDump(hands);
    }
    // The planted fault: the fingers left the game's all the same (the hand-pose check must fail it).
    return cfg.testArmFault == input::ArmFault::Fingers ? nullptr : &g.writes;
}

void giveBack() {
    g.ease.reset();
    g.hold.reset();
    g.last = 0;
    g.posedLast = false;
}

void trace(const std::byte* hands, const char* tag) {
    if (offhand_mods::fingerCount(arm::ArmSide::Left) == 0) {
        return;
    }
    if (!g.posedLast) {
        EVR_LOG("%s: fingers: the game's (%s)", tag, g.why);
        return;
    }
    game_arm::HandRead read;
    const bool ok = game_arm::readHand(arm::ArmSide::Left, hands, read);
    EVR_LOG(
        "%s: fingers: curl thumb %.2f index %.2f middle %.2f ring %.2f little %.2f; middle finger's second "
        "bone "
        "%.0f deg wanted, %s",
        tag, g.curls.thumb, g.curls.index, g.curls.middle, g.curls.ring, g.curls.pinky,
        turnDegrees(g.posed[kTraceJoint]),
        ok ? (std::to_string(static_cast<int>(std::lround(turnDegrees(read.local[kTraceJoint])))) + " read")
                 .c_str()
           : "not read");
}

} // namespace evr::vkcore::controllers::offhand_fingers
