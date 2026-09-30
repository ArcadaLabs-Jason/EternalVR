#include "features/bhaptics/bhaptics_message.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using evr::bhaptics::Device;
using evr::bhaptics::Effect;
using evr::bhaptics::feedbackPath;
using evr::bhaptics::Frame;
using evr::bhaptics::frameKey;
using evr::bhaptics::submitMessage;
using evr::bhaptics::turnOffAllMessage;

TEST_CASE("the feedback path carries the app's id and name, percent-encoded") {
    CHECK(feedbackPath() == "/v2/feedbacks?app_id=com.arcadalabs.eternalvr&app_name=EternalVR");
    CHECK(feedbackPath("a b", "x&y=z") == "/v2/feedbacks?app_id=a%20b&app_name=x%26y%3Dz");
}

TEST_CASE("each effect and device has its own key") {
    CHECK(frameKey(Effect::Shot, Device::ForearmR) == "evr_shot_ForearmR");
    CHECK(frameKey(Effect::Damage, Device::VestBack) == "evr_damage_VestBack");
    CHECK(frameKey(Effect::GloryKill, Device::VestFront) == "evr_glorykill_VestFront");
    CHECK(frameKey(Effect::Heartbeat, Device::VestFront) != frameKey(Effect::Damage, Device::VestFront));
}

TEST_CASE("a submission lists every frame with its position, dots and duration") {
    const std::vector<Frame> frames{
        Frame{Effect::Shot, Device::ForearmR, 90, {{0, 65}, {5, 65}}},
        Frame{Effect::Damage, Device::VestBack, 150, {{9, 100}}},
    };
    CHECK(submitMessage(frames) ==
          R"({"Submit":[)"
          R"({"Type":"frame","Key":"evr_shot_ForearmR","Frame":{"position":"ForearmR",)"
          R"("dotPoints":[{"index":0,"intensity":65},{"index":5,"intensity":65}],"pathPoints":[],)"
          R"("durationMillis":90}},)"
          R"({"Type":"frame","Key":"evr_damage_VestBack","Frame":{"position":"VestBack",)"
          R"("dotPoints":[{"index":9,"intensity":100}],"pathPoints":[],"durationMillis":150}}]})");
}

TEST_CASE("nothing to submit is an empty message; out-of-range dots and durations are kept in bounds") {
    CHECK(submitMessage({}).empty());
    const std::string text = submitMessage({Frame{Effect::Shot, Device::ForearmL, 0, {{6, 50}, {2, 200}}}});
    CHECK(text.find(R"("index":6)") == std::string::npos); // a sleeve has motors 0..5
    CHECK(text.find(R"({"index":2,"intensity":100})") != std::string::npos);
    CHECK(text.find(R"("durationMillis":1})") != std::string::npos);
}

TEST_CASE("turning everything off is one submission") {
    CHECK(turnOffAllMessage() == R"({"Submit":[{"Type":"turnOffAll","Key":""}]})");
}
