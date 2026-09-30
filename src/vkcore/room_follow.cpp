// Room-scale body follow and its precision test in the room (room_scale.hpp, body_follow.hpp,
// docs/VR_ROOMSCALE.md "Body follow").

#include "vkcore/room_scale.hpp"

#include "vkcore/body_follow.hpp"
#include "vkcore/log.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

namespace evr::vkcore {

namespace {

float flatLength(Vec3 v) {
    return std::sqrt(v.x * v.x + v.z * v.z);
}

std::uint32_t bitOf(roomscale::FollowBlock block) {
    return 1u << static_cast<unsigned>(block);
}

} // namespace

void RoomScale::noteBody(Vec3 forward, Vec3 left) {
    bodyForward_ = forward;
    bodyLeft_ = left;
    bodyKnown_ = true;
}

Vec3 RoomScale::testStep(double seconds) {
    const auto& steps = roomScaleSettings().testSteps;
    if (!steps || firstAnchorSeconds_ < 0.0) {
        return {};
    }
    const roomscale::TestStepPhase phase =
        roomscale::testStepAt(*steps, seconds - firstAnchorSeconds_ - steps->holdSeconds);
    if (phase.leg == stepLeg_) {
        return phase.offset;
    }
    if (probe_.active() && steps->commands) {
        // A constant command: the speed it held over the second half, when the body first moved, and how
        // far it went in all (on a stop leg: how far it coasted).
        const roomscale::StepReport r = probe_.report();
        EVR_LOG(
            "room: test move leg %d: command %d of 127 %s: moved %.3f m in %.2f s, steady %.3f m/s, first "
            "motion %s, across %.3f",
            r.leg, testCommand_, steps->sideways ? "right" : "forward", r.along, r.seconds, r.speed,
            r.firstMotion >= 0.0 ? (std::to_string(static_cast<int>(r.firstMotion * 1000.0)) + " ms").c_str()
                                 : "never",
            r.across);
    } else if (probe_.active()) {
        // Asked is the head's step; moved is what the body really did; the anchor took what body follow
        // accepted as its own; the largest command axis is the biggest move value it sent (of 127).
        const roomscale::StepReport r = probe_.report();
        // The gap is the criterion: the leg starts from what the last one left (up to half the deadzone),
        // so the body's move differs from the step by that much.
        const auto ms = [](double seconds) {
            return seconds >= 0.0 ? std::to_string(static_cast<int>(seconds * 1000.0)) + " ms"
                                  : std::string("never");
        };
        EVR_LOG("room: test step leg %d: asked %.3f m, gap %.3f -> %.3f m, within %.0f cm at %s; body moved "
                "%.3f m "
                "(peak %.3f, across %.3f) in %.2f s, 90%% at %s; anchor took %.3f m; largest command axis %d",
                r.leg, std::fabs(r.asked), r.gapStart, r.gapLeft, roomscale::kCloseMetres * 100.0f,
                ms(r.closeSeconds).c_str(), r.along, r.peak, r.across, r.seconds, ms(r.reachSeconds).c_str(),
                r.absorbed, body_follow::takePeakAxis());
    }
    stepLeg_ = phase.leg;
    if (phase.leg >= 0 && steps->commands) {
        testCommand_ = phase.command;
        body_follow::setTestCommand(phase.command, steps->sideways);
        probe_.begin(phase.leg, 1.0f, roomscale::testStepAxis(*steps), seconds, steps->holdSeconds);
        EVR_LOG("room: test move leg %d: command %d of 127 %s", phase.leg, phase.command,
                steps->sideways ? "right" : "forward");
    } else if (phase.leg >= 0) {
        probe_.begin(phase.leg, phase.asked, roomscale::testStepAxis(*steps), seconds);
        body_follow::takePeakAxis();
        const int index = phase.leg % phase.legs;
        EVR_LOG("room: test step leg %d (step %d of %zu, %s): head %+.3f m %s", phase.leg, index / 2 + 1,
                steps->metres.size(), index % 2 == 0 ? "out" : "back", phase.asked,
                steps->sideways ? "right" : "forward");
    }
    return phase.offset;
}

void RoomScale::follow(const Input& in, Vec3 testOffset) {
    const roomscale::RoomScaleSettings& cfg = roomScaleSettings();
    if (!cfg.follow.enabled) {
        return;
    }
    if (!followReady_) {
        followReady_ = true;
        follow_ = roomscale::BodyFollow(cfg.follow);
    }
    // The origin's move since the last frame, in room axes through the body frame the view was built with.
    std::optional<Vec3> displacement;
    float worldJump = 0.0f;
    if (in.bodyOrigin && lastOrigin_ && bodyKnown_) {
        const Vec3 world = *in.bodyOrigin - *lastOrigin_;
        worldJump = length(world) / in.unitsPerMetre;
        displacement = roomscale::displacementInRoom(world, bodyForward_, bodyLeft_, in.unitsPerMetre);
    }
    lastOrigin_ = in.bodyOrigin;

    const body_follow::CommandState command = body_follow::takeCommandState();
    if (cfg.testSteps && cfg.testSteps->commands) {
        // The command test drives the body itself: nothing is followed or taken into the room.
        body_follow::publish({});
        if (probe_.active()) {
            probe_.add(displacement.value_or(Vec3{}), {}, in.seconds);
        }
        return;
    }
    roomscale::FollowTick tick;
    tick.seconds = in.seconds;
    tick.gapRoom =
        (in.positionValid ? roomscale::toRoom(anchor_, in.localHead).position : Vec3{}) + testOffset;
    tick.displacement = displacement;
    tick.unitsPerMetre = in.unitsPerMetre;
    tick.commanded = command.commanded;
    tick.hookInstalled = command.hookInstalled;
    tick.anchored = anchorTaken_ && in.positionValid;
    tick.seated = roomPosture() == posture::Posture::Seated;
    tick.menu = in.menu;
    tick.cutscene = in.cutscene;
    tick.forcedView = in.forcedView;
    tick.stick = command.stick;
    tick.jumpOrDash = command.jumpOrDash;
    const roomscale::FollowStep step = follow_.update(tick);
    anchor_ = roomscale::shiftedBy(anchor_, step.absorbed);
    // While something else moves the body, the head's walk past the lean cap is taken into the room: the view
    // rides with the body instead of fading (a player walking through a glory kill or while using the stick).
    if (in.positionValid && roomscale::followBlockRidesWithBody(step.block)) {
        const Vec3 past = roomscale::leanPastCap(tick.gapRoom - step.absorbed, cfg.limits);
        if (flatLength(past) > 0.0f) {
            anchor_ = roomscale::shiftedBy(anchor_, past);
            ridden_ += flatLength(past);
            if (!loggedRide_) {
                loggedRide_ = true;
                EVR_LOG(
                    "room: body follow: the head went past the lean cap while %s moved the body; the room "
                    "rides along (%.3f m) instead of fading",
                    roomscale::followBlockName(step.block), flatLength(past));
            }
        }
    }
    body_follow::publish(step.request.move);
    if (probe_.active()) {
        probe_.add(displacement.value_or(Vec3{}), step.absorbed, in.seconds,
                   step.block == roomscale::FollowBlock::None ? step.gap : -1.0f);
    }

    ++followBlocks_[static_cast<std::size_t>(step.block)];
    const float taken = flatLength(step.absorbed);
    followAbsorbed_ += taken;
    followMaxGap_ = std::max(followMaxGap_, flatLength(tick.gapRoom));
    if (step.block == roomscale::FollowBlock::None) {
        ++followFrames_;
    }
    if (!command.hookInstalled && !loggedNoHook_) {
        loggedNoHook_ = true;
        EVR_LOG("room: body follow is on but the user-command hook is not in place (it needs "
                "ETERNALVR_CONTROLLERS=1 and the command path); the body does not follow");
    }
    if (step.cause == roomscale::FollowBlock::Teleport && ++teleports_ <= 20) {
        EVR_LOG("room: body follow: the origin jumped %.2f m in one frame (teleport %llu); not followed",
                worldJump, static_cast<unsigned long long>(teleports_));
    } else if (step.cause != roomscale::FollowBlock::None && step.cause != roomscale::FollowBlock::Off &&
               !(loggedBlocks_ & bitOf(step.cause))) {
        loggedBlocks_ |= bitOf(step.cause);
        EVR_LOG("room: body follow: first frame blocked by %s", roomscale::followBlockName(step.cause));
    }
    if (step.request.engaged && !loggedFollowStart_) {
        loggedFollowStart_ = true;
        EVR_LOG(
            "room: body follow: first request: gap %.3f m, %s (right %.3f, forward %.3f of a full command)",
            step.gap, roomscale::followTierName(step.request.tier), step.request.move.right,
            step.request.move.forward);
    }
    ++followTiers_[static_cast<std::size_t>(step.request.tier)];
    if (cfg.testSteps && step.request.tier != lastTier_ && tierLogs_ < 2000) {
        // The step test traces every change of tier: how the pulses close the gap.
        ++tierLogs_;
        EVR_LOG("room: body follow: %s at gap %.3f m, body %.3f m/s toward the head",
                roomscale::followTierName(step.request.tier), step.gap, step.speedToward);
    }
    lastTier_ = step.request.tier;
    if (taken > 0.0f && !loggedFollowTaken_) {
        loggedFollowTaken_ = true;
        EVR_LOG("room: body follow: first move taken into the room: %.4f m (%.4f %.4f)", taken,
                step.absorbed.x, step.absorbed.z);
    }
}

void RoomScale::logFollowStats() {
    if (!roomScaleSettings().follow.enabled) {
        return;
    }
    std::string blocked;
    for (std::size_t i = 0; i < followBlocks_.size(); ++i) {
        const auto block = static_cast<roomscale::FollowBlock>(i);
        if (block == roomscale::FollowBlock::None || followBlocks_[i] == 0) {
            continue;
        }
        blocked += (blocked.empty() ? "" : ", ") + std::string(roomscale::followBlockName(block)) + " " +
                   std::to_string(followBlocks_[i]);
    }
    EVR_LOG(
        "room: body follow: %llu frame(s) following (walk %llu, creep %llu, coast %llu), %llu command(s) "
        "sent, %.3f m taken into the room, largest gap %.3f m, %.3f m ridden past the lean cap; blocked "
        "frames: %s",
        static_cast<unsigned long long>(followFrames_),
        static_cast<unsigned long long>(followTiers_[static_cast<std::size_t>(roomscale::FollowTier::Walk)]),
        static_cast<unsigned long long>(followTiers_[static_cast<std::size_t>(roomscale::FollowTier::Creep)]),
        static_cast<unsigned long long>(followTiers_[static_cast<std::size_t>(roomscale::FollowTier::Coast)]),
        static_cast<unsigned long long>(body_follow::followCommands()), followAbsorbed_, followMaxGap_,
        ridden_, blocked.empty() ? "none" : blocked.c_str());
    followMaxGap_ = 0.0f;
}

} // namespace evr::vkcore
