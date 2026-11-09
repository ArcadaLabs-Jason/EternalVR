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

// A standard Gaussian for (n, k): splitmix64 of both, two uniforms, Box-Muller.
float gaussian(std::uint64_t n, std::uint64_t k) {
    const auto mix = [](std::uint64_t x) {
        x += 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    };
    const std::uint64_t a = mix(n * 64 + k * 2);
    const std::uint64_t b = mix(n * 64 + k * 2 + 1);
    const double u1 = (static_cast<double>(a >> 11) + 1.0) / 9007199254740993.0; // (0, 1]
    const double u2 = static_cast<double>(b >> 11) / 9007199254740992.0;         // [0, 1)
    return static_cast<float>(std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * std::numbers::pi * u2));
}

// The side's rest position from the head: below, ahead and to the side.
Vec3 restPosition(Hand hand) {
    return {hand == Hand::Left ? -0.2f : 0.2f, -0.35f, -0.3f};
}

// The button a key names, or null.
std::optional<bool>* buttonOf(TestHand& h, std::string_view key) {
    const std::pair<std::string_view, std::optional<bool>*> buttons[] = {
        {"primary", &h.primary},
        {"secondary", &h.secondary},
        {"face3", &h.face3},
        {"face4", &h.face4},
        {"shoulder", &h.shoulder},
        {"click", &h.stickClick},
        {"menu", &h.menu},
        {"thumbrest", &h.thumbRest},
        {"primary_touch", &h.primaryTouch},
        {"secondary_touch", &h.secondaryTouch},
        {"thumbstick_touch", &h.stickTouch},
        {"trackpad_touch", &h.trackpadTouch},
        {"trigger_touch", &h.triggerTouch},
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
        if (key == "trigger" || key == "grip" || key == "grip_curl") {
            std::optional<float>& analog = key == "trigger" ? h.trigger : key == "grip" ? h.grip : h.gripCurl;
            one([&](float x) { analog = std::clamp(x, 0.0f, 1.0f); });
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
        } else if (key == "sweep") {
            if (v.size() != 3 || !(std::fabs(v[0]) <= 90.0f) || !(v[1] >= 0.05f) || !(v[2] > 0.0f) ||
                v[2] > 1000.0f) {
                issue("expected amplitude (degrees, within 90), period (seconds, 0.05 or more), periods");
                continue;
            }
            h.sweep = TestSweep{v[0], v[1], v[2]};
        } else if (key == "noise") {
            if (v.size() != 2 || !(v[0] >= 0.0f && v[0] <= 50.0f) || !(v[1] >= 0.0f && v[1] <= 10.0f)) {
                issue("expected millimetres (0 to 50), degrees (0 to 10)");
                continue;
            }
            h.noiseMillimetres = v[0];
            h.noiseDegrees = v[1];
        } else if (key == "velocity") {
            if (v.size() != 3 || std::fabs(v[0]) > 10.0f || std::fabs(v[1]) > 10.0f ||
                std::fabs(v[2]) > 10.0f) {
                issue("expected x, y, z in metres per second (within 10)");
                continue;
            }
            h.velocity = Vec3{v[0], v[1], v[2]};
        } else {
            issue("unknown input (trigger, grip, grip_curl, stick, primary, secondary, face3, face4, "
                  "shoulder, click, menu, thumbrest, primary_touch, secondary_touch, thumbstick_touch, "
                  "trackpad_touch, trigger_touch, aim, position, sweep, velocity, noise)");
        }
    }
    return input;
}

bool scriptPosesHand(const TestInput& input, Hand hand) {
    const TestHand& h = input.hand(hand);
    return h.aimYawDegrees && h.aimPitchDegrees;
}

std::optional<Pose> testHandPose(const TestInput& input, Hand hand, Vec3 headPosition, double nowSeconds) {
    const TestHand& h = input.hand(hand);
    if (!h.aimYawDegrees || !h.aimPitchDegrees) {
        return std::nullopt;
    }
    float yawDegrees = *h.aimYawDegrees;
    if (h.sweep) {
        const double t = nowSeconds - h.sweepStartSeconds;
        if (t > 0.0 && t < static_cast<double>(h.sweep->periods) * h.sweep->periodSeconds) {
            yawDegrees += h.sweep->amplitudeDegrees *
                          static_cast<float>(std::sin(2.0 * std::numbers::pi * t / h.sweep->periodSeconds));
        }
    }
    const Quat yaw = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, yawDegrees * kRadiansPerDegree);
    const Quat pitch = Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, *h.aimPitchDegrees * kRadiansPerDegree);
    const Quat roll = Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, h.aimRollDegrees * kRadiansPerDegree);
    return Pose{normalize(yaw * pitch * roll), headPosition + h.position.value_or(restPosition(hand))};
}

void startSweeps(TestInput& input, const TestInput* last, double nowSeconds) {
    for (std::size_t i = 0; i < input.hands.size(); ++i) {
        TestHand& h = input.hands[i];
        const bool same = last && h.sweep && last->hands[i].sweep == h.sweep;
        h.sweepStartSeconds = same ? last->hands[i].sweepStartSeconds : nowSeconds;
    }
}

Pose testHandNoise(const TestInput& input, Hand hand, Pose pose, std::uint64_t n) {
    const TestHand& h = input.hand(hand);
    if (h.noiseMillimetres <= 0.0f && h.noiseDegrees <= 0.0f) {
        return pose;
    }
    const std::uint64_t k = hand == Hand::Left ? 0 : 8;
    const float m = h.noiseMillimetres * 0.001f;
    pose.position = pose.position + Vec3{gaussian(n, k) * m, gaussian(n, k + 1) * m, gaussian(n, k + 2) * m};
    const float r = h.noiseDegrees * kRadiansPerDegree;
    const Vec3 turn{gaussian(n, k + 3) * r, gaussian(n, k + 4) * r, gaussian(n, k + 5) * r};
    const float angle = length(turn);
    if (angle > 0.0f) {
        pose.orientation = normalize(Quat::fromAxisAngle(turn * (1.0f / angle), angle) * pose.orientation);
    }
    return pose;
}

void applyTestInput(const TestInput& input, InputFrame& frame) {
    for (const Hand which : {Hand::Left, Hand::Right}) {
        const TestHand& t = input.hand(which);
        HandState& h = which == Hand::Left ? frame.left : frame.right;
        h.trigger = t.trigger.value_or(h.trigger);
        h.grip = t.grip.value_or(h.grip);
        h.gripCurl = t.gripCurl.value_or(h.gripCurl);
        h.stick = t.stick.value_or(h.stick);
        h.stickClick = t.stickClick.value_or(h.stickClick);
        h.primaryButton = t.primary.value_or(h.primaryButton);
        h.secondaryButton = t.secondary.value_or(h.secondaryButton);
        h.face3Button = t.face3.value_or(h.face3Button);
        h.face4Button = t.face4.value_or(h.face4Button);
        h.shoulderButton = t.shoulder.value_or(h.shoulderButton);
        h.menuButton = t.menu.value_or(h.menuButton);
        h.thumbRest = t.thumbRest.value_or(h.thumbRest);
        h.primaryTouch = t.primaryTouch.value_or(h.primaryTouch);
        h.secondaryTouch = t.secondaryTouch.value_or(h.secondaryTouch);
        h.stickTouch = t.stickTouch.value_or(h.stickTouch);
        h.trackpadTouch = t.trackpadTouch.value_or(h.trackpadTouch);
        h.triggerTouch = t.triggerTouch.value_or(h.triggerTouch);
        const Vec3 head = frame.head.poseValid ? frame.head.pose.position : Vec3{};
        if (const auto pose = testHandPose(input, which, head, frame.sampleSeconds)) {
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
