#include "features/input/test_input.hpp"

#include "common/parse_float.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <utility>

namespace evr::input {

namespace {

constexpr float kRadiansPerDegree = std::numbers::pi_v<float> / 180.0f;

std::string_view trim(std::string_view s) {
    const std::size_t first = s.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    return s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
}

// Comma-separated finite numbers.
std::vector<float> numbers(std::string_view text) {
    std::vector<float> out;
    while (true) {
        const std::size_t comma = text.find(',');
        const auto value = parseFloat(trim(text.substr(0, comma)));
        if (!value) {
            return {};
        }
        out.push_back(*value);
        if (comma == std::string_view::npos) {
            return out;
        }
        text.remove_prefix(comma + 1);
    }
}

// The side's rest position from the head: below, ahead and to the side.
Vec3 restPosition(Hand hand) {
    return {hand == Hand::Left ? -0.2f : 0.2f, -0.35f, -0.3f};
}

// The button a key names, or null.
std::optional<bool>* buttonOf(TestHand& h, std::string_view key) {
    const std::pair<std::string_view, std::optional<bool>*> buttons[] = {
        {"primary", &h.primary},   {"secondary", &h.secondary}, {"face3", &h.face3}, {"face4", &h.face4},
        {"shoulder", &h.shoulder}, {"click", &h.stickClick},    {"menu", &h.menu},
    };
    for (const auto& [name, button] : buttons) {
        if (name == key) {
            return button;
        }
    }
    return nullptr;
}

} // namespace

TestInput parseTestInput(std::string_view text) {
    TestInput input;
    int lineNumber = 0;
    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        std::string_view line = text.substr(0, newline);
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1);
        ++lineNumber;
        if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) {
            line = line.substr(0, hash);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        const auto issue = [&](const char* what) {
            input.issues.push_back("line " + std::to_string(lineNumber) + ": " + what);
        };
        const std::size_t equals = line.find('=');
        const std::size_t dot = line.find('.');
        if (equals == std::string_view::npos || dot == std::string_view::npos || dot > equals) {
            issue("expected <hand>.<input> = <value>");
            continue;
        }
        const std::string_view side = trim(line.substr(0, dot));
        const std::string_view key = trim(line.substr(dot + 1, equals - dot - 1));
        const std::vector<float> v = numbers(line.substr(equals + 1));
        if (side != "left" && side != "right") {
            issue("the hand must be left or right");
            continue;
        }
        TestHand& h = input.hands[static_cast<std::size_t>(side == "left" ? Hand::Left : Hand::Right)];
        const auto one = [&](auto set) {
            if (v.size() != 1) {
                issue("expected one number");
                return;
            }
            set(v[0]);
        };
        if (key == "trigger" || key == "grip") {
            one([&](float x) { (key == "trigger" ? h.trigger : h.grip) = std::clamp(x, 0.0f, 1.0f); });
        } else if (std::optional<bool>* button = buttonOf(h, key)) {
            one([&](float x) { *button = x != 0.0f; });
        } else if (key == "stick") {
            if (v.size() != 2) {
                issue("expected x, y");
                continue;
            }
            h.stick = Axis2{std::clamp(v[0], -1.0f, 1.0f), std::clamp(v[1], -1.0f, 1.0f)};
        } else if (key == "aim") {
            if ((v.size() != 2 && v.size() != 3) || std::fabs(v[1]) > 90.0f) {
                issue("expected yaw, pitch[, roll] in degrees (pitch within 90)");
                continue;
            }
            h.aimYawDegrees = v[0];
            h.aimPitchDegrees = v[1];
            h.aimRollDegrees = v.size() == 3 ? v[2] : 0.0f;
        } else if (key == "position") {
            if (v.size() != 3 || std::fabs(v[0]) > 3.0f || std::fabs(v[1]) > 3.0f || std::fabs(v[2]) > 3.0f) {
                issue("expected x, y, z in metres (within 3)");
                continue;
            }
            h.position = Vec3{v[0], v[1], v[2]};
        } else if (key == "velocity") {
            if (v.size() != 3 || std::fabs(v[0]) > 10.0f || std::fabs(v[1]) > 10.0f ||
                std::fabs(v[2]) > 10.0f) {
                issue("expected x, y, z in metres per second (within 10)");
                continue;
            }
            h.velocity = Vec3{v[0], v[1], v[2]};
        } else {
            issue("unknown input (trigger, grip, stick, primary, secondary, face3, face4, shoulder, click, "
                  "menu, aim, position, velocity)");
        }
    }
    return input;
}

std::optional<Pose> testHandPose(const TestInput& input, Hand hand, Vec3 headPosition) {
    const TestHand& h = input.hand(hand);
    if (!h.aimYawDegrees || !h.aimPitchDegrees) {
        return std::nullopt;
    }
    const Quat yaw = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, *h.aimYawDegrees * kRadiansPerDegree);
    const Quat pitch = Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, *h.aimPitchDegrees * kRadiansPerDegree);
    const Quat roll = Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, h.aimRollDegrees * kRadiansPerDegree);
    return Pose{normalize(yaw * pitch * roll), headPosition + h.position.value_or(restPosition(hand))};
}

void applyTestInput(const TestInput& input, InputFrame& frame) {
    for (const Hand which : {Hand::Left, Hand::Right}) {
        const TestHand& t = input.hand(which);
        HandState& h = which == Hand::Left ? frame.left : frame.right;
        h.trigger = t.trigger.value_or(h.trigger);
        h.grip = t.grip.value_or(h.grip);
        h.stick = t.stick.value_or(h.stick);
        h.stickClick = t.stickClick.value_or(h.stickClick);
        h.primaryButton = t.primary.value_or(h.primaryButton);
        h.secondaryButton = t.secondary.value_or(h.secondaryButton);
        h.face3Button = t.face3.value_or(h.face3Button);
        h.face4Button = t.face4.value_or(h.face4Button);
        h.shoulderButton = t.shoulder.value_or(h.shoulderButton);
        h.menuButton = t.menu.value_or(h.menuButton);
        const Vec3 head = frame.head.poseValid ? frame.head.pose.position : Vec3{};
        if (const auto pose = testHandPose(input, which, head)) {
            h.poseValid = true;
            h.aimPose = *pose;
            h.gripValid = true;
            h.gripPose = *pose;
            h.velocityValid = true;
            h.linearVelocity = t.velocity.value_or(Vec3{});
        }
    }
}

} // namespace evr::input
