#pragma once

// Room-scale, posture and eye-height settings (docs/VR_ROOMSCALE.md), read at start-up from environment
// variables set by the launcher or the rig scripts. Parsing is here, apart from the environment, so every
// rule is tested; a value that cannot be used is reported and the default kept.
//
//   ETERNALVR_POSTURE          auto / seated / standing   posture override (REQ-06); auto detects it
//   ETERNALVR_HEIGHT           slayer / real              eye height: the game's (default) or the player's
//                                                         own above the floor (needs a floor space)
//   ETERNALVR_AUTO_ANCHOR      1 / 0                      anchor height, yaw and origin on the first stable
//                                                         worn head pose (T-045)
//   ETERNALVR_RECENTER_HOLD    seconds (0.3 to 5; 0 off)  how long both sticks are held to recenter (2)
//   ETERNALVR_LEAN_CAP         metres (0.05 to 2)         the horizontal head offset cap (T-062: 0.6)
//   ETERNALVR_HEAD_COLLISION   1 / 0                      the head sweep against the world (fade, shots)
//   ETERNALVR_HEAD_FADE        1 / 0                      the fade to black when the head is in geometry
//   ETERNALVR_IPD              millimetres (50 to 80; 0)  the eye separation the game renders with; 0 or
//                                                         unset: the runtime's
//   ETERNALVR_BODY_FOLLOW      1 / 0                      room-scale body follow (body_follow.hpp), on
//   ETERNALVR_BODY_FOLLOW_DEADZONE metres (0.01 to 0.5)   the gap beyond which the body follows (0.04)
//   ETERNALVR_BODY_FOLLOW_WALK     30 to 127              the move value of the game's walk tier (85)
//   ETERNALVR_BODY_FOLLOW_CREEP    20 to 127              the move value of its creep tier (60)
//   ETERNALVR_BODY_FOLLOW_COAST    seconds (0 to 0.5)     the body coasts its speed times this (0.1)
//   ETERNALVR_BODY_FOLLOW_SPEED    m/s (0.5 to 5)         the fastest follow moves the body (3)
//   ETERNALVR_TEST_HEAD_OFFSET x,y,z[,period]             test: metres added to the head in room space
//                                                         (+x right, +y up, -z forward); with a period the
//                                                         offset eases in and out over that many seconds
//   ETERNALVR_TEST_RECENTER    seconds                    test: one user recenter this long after tracking
//                                                         starts
//   ETERNALVR_TEST_STEPS       metres,metres,...          test: scripted head steps for body follow
//                                                         (follow_test_steps.hpp), from one hold after the
//                                                         first anchor
//   ETERNALVR_TEST_STEP_SECONDS seconds (0.5 to 60)       test: how long each leg of a step is held (3)
//   ETERNALVR_TEST_STEP_AXIS   forward / right            test: the step direction (forward)

#include "features/posture/posture_detector.hpp"
#include "features/roomscale/body_follow.hpp"
#include "features/roomscale/follow_test_steps.hpp"
#include "features/roomscale/head_offset.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace evr::roomscale {

struct TestHeadOffset {
    Vec3 metres;
    float periodSeconds = 0.0f; // 0: constant
};

struct RoomScaleSettings {
    posture::PostureOverride posture = posture::PostureOverride::Auto;
    HeightMode height = HeightMode::Slayer;
    bool autoAnchor = true;
    float recenterHoldSeconds = 2.0f; // 0: no recenter (both sticks held)
    HeadOffsetLimits limits;
    bool collision = true;
    bool fade = true;
    float ipdMetres = 0.0f; // 0: the runtime's
    BodyFollowSettings follow;
    std::optional<TestHeadOffset> testOffset;
    float testRecenterSeconds = 0.0f; // 0: none
    std::optional<TestSteps> testSteps;
};

struct RoomScaleIssue {
    std::string name;
    std::string value;
    std::string message;
};

struct RoomScaleSettingsResult {
    RoomScaleSettings settings;
    std::vector<RoomScaleIssue> issues;
};

// `lookup` returns a variable's value, or nullopt when it is not set. Empty values count as not set.
using RoomScaleLookup = std::function<std::optional<std::string>(std::string_view name)>;

RoomScaleSettingsResult parseRoomScaleSettings(const RoomScaleLookup& lookup);

// The test offset at `seconds` since it started: constant, or eased 0 -> offset -> 0 over each period.
Vec3 testOffsetAt(const TestHeadOffset& offset, double seconds);

const char* postureOverrideName(posture::PostureOverride value);
const char* postureName(posture::Posture value);
const char* heightModeName(HeightMode value);

} // namespace evr::roomscale
